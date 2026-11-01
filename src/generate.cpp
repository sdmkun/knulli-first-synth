#include "generate.h"

#include <algorithm>
#include <cmath>

namespace {

// xorshift32; never seeded with 0.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u + 0x9E3779B9u) { if (!s) s = 1; }
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    float uni() { return (next() >> 8) * (1.0f / 16777216.0f); } // [0, 1)
    int   range(int n) { return n > 0 ? int(next() % uint32_t(n)) : 0; }
};

// Per-style hit chances for one 16-step bar, one string per pad in kit order
// KICK SNARE HAT OPENHAT CLAP TOM RIM BELL. '9' = always (the backbone),
// '1'-'8' = chance in tenths, scaled by DENSITY; '0' = never.
struct StyleTable {
    const char* rows[kDrumPads];
};

const StyleTable kStyles[] = {
    // FOUR: four-on-the-floor, offbeat hats, clap on 2 and 4.
    {{"9000900090009000", "0000000000000000", "3393339333933393", "0010001000100014",
      "0000900000009000", "0000000000000002", "0000000200000020", "0000000100000001"}},
    // BREAK: boom-bap / breakbeat with ghost snares.
    {{"9010000090300000", "0000901010009010", "9393939393939393", "0000000300000005",
      "0000000000000000", "0000000000000030", "0010000000100000", "0000000000000000"}},
    // HALF: half-time, snare on 3.
    {{"9000003000500000", "0000000090000000", "9030903090309030", "0000000000000400",
      "0000000040000000", "0000000000000033", "0003000000030000", "0000000000000000"}},
    // JUNGLE: amen-style kick and snare placement, busy ride on the bell.
    {{"9090000000990000", "0000900606009006", "6363636363636363", "0000000000000000",
      "0000000000000000", "0000000000000040", "0030000000300000", "3000300030003000"}},
};

bool hasHit(const Pattern& p, int pad, int step) { return p.steps[step].find(kDrumBase + pad) >= 0; }

// Adds a hit (an existing one keeps its velocity) or removes it.
void setHit(Pattern& p, int pad, int step, bool on, int vel = kDefaultVel) {
    if (!on) p.steps[step].remove(kDrumBase + pad);
    else if (!hasHit(p, pad, step)) p.steps[step].add(kDrumBase + pad, vel);
}

// Velocity for an accent of -1 (quietest) .. +1 (loudest), scaled by `dynamics`.
// The scale is lopsided because the default level sits near the top.
int dynVel(float accent, float dynamics) {
    accent    = std::clamp(accent, -1.0f, 1.0f);
    float off = accent > 0.0f ? accent * 2.0f : accent * 4.0f;
    return std::clamp(int(std::lround(kDefaultVel + off * dynamics)), 1, kMaxVel);
}

// Where a step sits in the beat: downbeats lean loud, off-beat sixteenths quiet.
float beatAccent(int step) { return step % 4 == 0 ? 0.5f : step % 2 == 1 ? -0.5f : 0.0f; }

bool inScale(int pitch, const ScaleInfo& s) {
    int pc = ((pitch - s.root) % 12 + 12) % 12;
    for (int i = 0; i < s.count; ++i)
        if (s.steps[i] == pc) return true;
    return false;
}

int degreePitch(int degree, int octave, const ScaleInfo& s) {
    int oct = degree >= 0 ? degree / s.count : -((-degree + s.count - 1) / s.count);
    int idx = degree - oct * s.count;
    return 12 * (octave + 1) + s.root + s.steps[idx] + 12 * oct;
}

void setOff(SeqStep& st, int col) {
    if (st.notes[col].pitch == SeqNote::kEmpty) st.notes[col].pitch = SeqNote::kOff;
}

} // namespace

const char* drumStyleName(DrumStyle s) {
    switch (s) {
    case DrumStyle::Four:   return "FOUR";
    case DrumStyle::Break:  return "BREAK";
    case DrumStyle::Half:   return "HALF";
    case DrumStyle::Jungle: return "JUNGLE";
    case DrumStyle::Random: return "RANDOM";
    default:                return "?";
    }
}

int rowHits(const Pattern& p, int pad) {
    int n = 0;
    for (int i = 0; i < p.length; ++i)
        if (hasHit(p, pad, i)) ++n;
    return n;
}

void euclidRow(Pattern& p, int pad, int hits, int rotate) {
    const int n = p.length;
    hits        = std::clamp(hits, 0, n);
    for (int i = 0; i < n; ++i) {
        int j = ((i - rotate) % n + n) % n;
        setHit(p, pad, i, hits > 0 && (j * hits) % n < hits);
    }
}

void generateDrums(Pattern& p, const DrumGen& g) {
    Rng rng(g.seed);
    for (int pad = 0; pad < kDrumPads; ++pad)
        for (int i = 0; i < p.length; ++i) setHit(p, pad, i, false);

    if (g.style == DrumStyle::Random) {
        // Euclidean layers with random counts and offsets; density scales how many.
        const int base[kDrumPads] = {3, 2, 6, 1, 1, 1, 2, 2};
        for (int pad = 0; pad < kDrumPads; ++pad) {
            int hits = int(std::round(base[pad] * p.length / 16.0f * (0.3f + 1.4f * g.density))) + rng.range(2);
            if (pad > 1 && rng.uni() > 0.4f + g.density * 0.6f) hits = 0;
            euclidRow(p, pad, hits, pad == 0 ? 0 : rng.range(p.length));
            for (int i = 0; i < p.length; ++i) {
                int c = p.steps[i].find(kDrumBase + pad);
                if (c >= 0) p.steps[i].notes[c].vel = uint8_t(dynVel(beatAccent(i) + (rng.uni() - 0.5f) * 0.6f, g.dynamics));
            }
        }
    } else {
        const StyleTable& t = kStyles[int(g.style)];
        for (int pad = 0; pad < kDrumPads; ++pad) {
            for (int i = 0; i < p.length; ++i) {
                int   d      = t.rows[pad][i % 16] - '0';
                float chance = d >= 9 ? 1.0f : d / 10.0f * g.density * 1.6f;
                float accent = d >= 9 ? 0.6f : beatAccent(i) + (rng.uni() - 0.5f) * 0.6f;
                if (rng.uni() < chance) setHit(p, pad, i, true, dynVel(accent, g.dynamics));
            }
        }
    }

    // Variation: ghost notes on snare / tom / rim, and fills in the last beat of some bars.
    for (int i = 0; i < p.length; ++i) {
        for (int pad : {1, 5, 6})
            if (rng.uni() < g.variation * 0.08f) setHit(p, pad, i, true, dynVel(-1.0f, g.dynamics));
    }
    for (int bar = 0; bar * 16 < p.length; ++bar) {
        if (rng.uni() >= g.variation * 0.5f) continue;
        int fillPad = rng.uni() < 0.5f ? 1 : 5;
        for (int i = bar * 16 + 12; i < std::min(bar * 16 + 16, p.length); ++i) // a crescendo into the next bar
            if (rng.uni() < 0.6f) setHit(p, fillPad, i, true, dynVel((i % 16 - 13.5f) * 0.5f, g.dynamics));
    }
}

void generateMelody(Pattern& p, const MelodyGen& g, const ScaleInfo& s) {
    Rng rng(g.seed);
    p.clear();
    const int maxDeg  = 3 + int(g.range * 11.0f);
    const int maxJump = 1 + int(g.motion * 5.0f);
    int       deg     = rng.range(std::min(maxDeg, s.count) + 1);
    int       dur[Pattern::kMaxSteps] = {}; // intended length of the note starting on each step

    for (int i = 0; i < p.length; ++i) {
        float weight = i % 4 == 0 ? 1.0f : i % 2 == 0 ? 0.6f : 0.35f;
        if (rng.uni() >= std::min(g.density * 1.4f * weight, 1.0f)) continue;

        int d = rng.range(2 * maxJump + 1) - maxJump;
        if (g.motion < 0.3f && rng.uni() < 0.4f) d = 0; // smooth lines repeat notes now and then
        deg += d;
        if (deg < 0) deg = -deg;                     // reflect at the bottom of the range
        if (deg > maxDeg) deg = 2 * maxDeg - deg;    // and at the top
        deg = std::clamp(deg, 0, maxDeg);

        dur[i]  = 1 + int(g.length * 7.0f * (0.5f + rng.uni()));
        int vel = dynVel(beatAccent(i) + (rng.uni() - 0.5f) * 0.6f, g.dynamics);
        p.steps[i].add(degreePitch(deg, g.octave, s), vel);
        if (rng.uni() < g.chord) {
            p.steps[i].add(degreePitch(deg + 2, g.octave, s), vel);
            p.steps[i].add(degreePitch(deg + 4, g.octave, s), vel);
        }
    }

    // End each note with an OFF after its length, or at the next note if that comes first.
    // A next note only ends its own columns, so a chord followed by a single note
    // also needs OFFs in the columns the single note leaves free.
    for (int i = 0; i < p.length; ++i) {
        if (p.steps[i].count() == 0) continue;
        int gap = 1;
        while (gap < p.length && p.steps[(i + gap) % p.length].count() == 0) ++gap;
        SeqStep& end = p.steps[(i + std::min(dur[i], gap)) % p.length];
        for (int c = 0; c < SeqStep::kMaxNotes; ++c)
            if (p.steps[i].notes[c].isNote()) setOff(end, c);
    }
}

int scaleStep(int pitch, int dir, const ScaleInfo& s) {
    for (int k = 1; k <= 12; ++k) {
        int q = pitch + (dir > 0 ? k : -k);
        if (q < 0 || q > 127) break;
        if (inScale(q, s)) return q;
    }
    return pitch;
}

void mutateDrums(Pattern& p, uint32_t seed, float amount) {
    Rng rng(seed);
    for (int pad = 0; pad < kDrumPads; ++pad) {
        for (int i = 0; i < p.length; ++i) {
            bool backbone = (pad == 0 && i % 8 == 0) || (pad == 1 && i % 8 == 4);
            if (backbone || rng.uni() >= amount * 0.12f) continue;
            int c = p.steps[i].find(kDrumBase + pad);
            if (c >= 0) {
                // Nudge the hit to a neighbouring step, change how hard it is, or drop it.
                int   vel = p.steps[i].notes[c].vel;
                float r   = rng.uni();
                if (r < 0.35f) {
                    p.steps[i].notes[c].vel = uint8_t(std::clamp(vel + (rng.uni() < 0.5f ? 1 : -1), 1, kMaxVel));
                    continue;
                }
                setHit(p, pad, i, false);
                if (r < 0.75f) setHit(p, pad, (i + (rng.uni() < 0.5f ? 1 : p.length - 1)) % p.length, true, vel);
            } else if (rng.uni() < 0.5f) {
                setHit(p, pad, i, true, 3 + rng.range(4));
            }
        }
    }
}

void mutateMelody(Pattern& p, const ScaleInfo& s, uint32_t seed, float amount) {
    Rng rng(seed);
    int last = -1; // a recent pitch to borrow when adding notes
    for (int i = 0; i < p.length; ++i) {
        SeqStep& st = p.steps[i];
        for (SeqNote& n : st.notes) {
            if (!n.isNote()) continue;
            last = n.pitch;
            if (rng.uni() >= amount * 0.25f) continue;
            float r = rng.uni();
            if (r < 0.5f) n.pitch = int8_t(scaleStep(n.pitch, rng.uni() < 0.5f ? 1 : -1, s));
            else if (r < 0.8f) n.vel = uint8_t(std::clamp(n.vel + (rng.uni() < 0.5f ? 1 : -1), 1, kMaxVel));
            else n.pitch = SeqNote::kOff; // drop it, leaving a rest
        }
        if (st.empty() && last >= 0 && i % 2 == 0 && rng.uni() < amount * 0.1f) {
            st.add(scaleStep(last, rng.uni() < 0.5f ? 1 : -1, s), kDefaultVel - 1);
            int end = i + 1 + rng.range(2); // a short added note, not one that rings to the next
            if (end < p.length) setOff(p.steps[end], 0);
        }
    }
}
