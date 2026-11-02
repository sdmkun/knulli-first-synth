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

// Gives an existing hit a note command.
void setFx(Pattern& p, int pad, int step, Fx fx, int value) {
    int c = p.steps[step].find(kDrumBase + pad);
    if (c < 0) return;
    p.steps[step].notes[c].fx    = fx;
    p.steps[step].notes[c].fxVal = uint8_t(value);
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
        if (rng.uni() < g.variation) setFx(p, fillPad, std::min(bar * 16 + 15, p.length - 1), Fx::Ret, rng.uni() < 0.5f ? 3 : 4);
    }

    // Variation: the odd hi-hat roll.
    for (int i = 0; i < p.length; ++i)
        if (rng.uni() < g.variation * 0.12f) setFx(p, 2, i, Fx::Ret, rng.uni() < 0.7f ? 6 : 4);
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

// ---------------------------------------------------------------- chords

namespace {

struct Prog {
    const char* name;
    int         degrees[8]; // scale degrees, 0 = the key's root; -1 ends the list
};
const Prog kProgs[] = {
    {"EPIC", {0, 5, 2, 6, -1}},        // i VI III VII (in minor)
    {"POP", {0, 4, 5, 3, -1}},         // I V vi IV
    {"DARK", {0, 3, 6, 2, -1}},        // i iv VII III
    {"JAZZ", {1, 4, 0, 5, -1}},        // ii V I vi
    {"DRONE", {0, 0, 5, 6, -1}},
    {"LIFT", {3, 4, 5, 5, -1}},        // IV V vi vi
    {"CIRCLE", {0, 3, 6, 2, 5, 1, 4, 0}}, // down in fourths through the key
    {"RANDOM", {-1}},
};
constexpr int kProgCount = sizeof(kProgs) / sizeof(kProgs[0]);

const int kMinor[7] = {0, 2, 3, 5, 7, 8, 10};

// Insertion sort for a chord's few notes (std::sort on these small arrays
// trips a false -Warray-bounds in GCC 12).
void sortNotes(int* p, int n) {
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0 && p[j - 1] > p[j]; --j) std::swap(p[j - 1], p[j]);
}

// Pitches (low to high) of the chord on `degree`, stacked in thirds of a
// seven-note scale. Pentatonic and blues scales borrow natural minor here.
int chordPitches(int degree, int count, bool sus, int octave, const ScaleInfo& in, int* out) {
    ScaleInfo s = in;
    if (s.count != 7) s.steps = kMinor, s.count = 7;
    // Thirds in the order they are added: 3rd, 5th, 7th, 9th, 13th, 11th. A six-note chord drops the 5th.
    int order[6] = {2, 4, 6, 8, 12, 10};
    if (sus) order[0] = 3; // a 4th instead of the 3rd
    int n = 0;
    out[n++] = degreePitch(degree, octave, s);
    for (int k = 0; k < 6 && n < count; ++k) {
        if (k == 1 && count == 6) continue;
        out[n++] = degreePitch(degree + order[k], octave, s);
    }
    sortNotes(out, n);
    return n;
}

// Moves notes by octaves so the chord sits closest to `prev` (or around `centre`).
void voiceLead(int* p, int n, const int* prev, int prevN, int centre) {
    int best[SeqStep::kMaxNotes], bestScore = 1 << 30;
    int cand[SeqStep::kMaxNotes];
    std::copy(p, p + n, best);
    for (int inv = 0; inv < n; ++inv) {
        for (int shift = -2; shift <= 1; ++shift) {
            for (int i = 0; i < n; ++i) cand[i] = p[i] + (i < inv ? 12 : 0) + 12 * shift;
            sortNotes(cand, n);
            int score = 0;
            if (prevN > 0) {
                for (int i = 0; i < n; ++i) score += std::abs(cand[i] - prev[std::min(i * prevN / n, prevN - 1)]);
            }
            int mid = (cand[0] + cand[n - 1]) / 2;
            score += std::abs(mid - centre) * (prevN > 0 ? 1 : 4);
            if (cand[0] < 28 || cand[n - 1] > 108) score += 1000;
            if (score < bestScore) {
                bestScore = score;
                std::copy(cand, cand + n, best);
            }
        }
    }
    std::copy(best, best + n, p);
}

bool rhythmHit(ChordRhythm r, int stepInChord, int step, Rng& rng, float accent) {
    const int b = step % 16;
    switch (r) {
    case ChordRhythm::Hold:    return stepInChord == 0;
    case ChordRhythm::Beats:   return b % 4 == 0;
    case ChordRhythm::Offbeat: return b % 4 == 2;
    case ChordRhythm::Stabs:   return b == 0 || b == 3 || b == 6 || b == 10 || b == 12;
    case ChordRhythm::Eighths: return b % 2 == 0;
    case ChordRhythm::Random:  return stepInChord == 0 || rng.uni() < (b % 2 == 0 ? 0.35f : 0.15f) + accent * 0.1f;
    default:                   return false;
    }
}

} // namespace

const char* chordRhythmName(ChordRhythm r) {
    static const char* kNames[] = {"HOLD", "BEATS", "OFFBEAT", "STABS", "EIGHTHS", "RANDOM"};
    return kNames[std::clamp(int(r), 0, int(ChordRhythm::Count) - 1)];
}
const char* chordVoicingName(ChordVoicing v) {
    static const char* kNames[] = {"CLOSE", "OPEN", "SPREAD"};
    return kNames[std::clamp(int(v), 0, int(ChordVoicing::Count) - 1)];
}
const char* chordNotesName(ChordNotes n) {
    static const char* kNames[] = {"FULL", "FULL+BASS", "ROOT ONLY"};
    return kNames[std::clamp(int(n), 0, int(ChordNotes::Count) - 1)];
}
const char* chordProgName(int prog) { return kProgs[std::clamp(prog, 0, kProgCount - 1)].name; }
int         chordProgCount() { return kProgCount; }

std::string chordProgText(int prog, const ScaleInfo& in) {
    static const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    ScaleInfo s = in;
    if (s.count != 7) s.steps = kMinor, s.count = 7;
    const Prog& p = kProgs[std::clamp(prog, 0, kProgCount - 1)];
    std::string out;
    for (int i = 0; i < 8 && p.degrees[i] >= 0; ++i) {
        const int d     = p.degrees[i];
        const int root  = degreePitch(d, 4, s);
        const int third = degreePitch(d + 2, 4, s) - root, fifth = degreePitch(d + 4, 4, s) - root;
        if (!out.empty()) out += ' ';
        out += kNames[root % 12];
        if (fifth == 6) out += "DIM";
        else if (third == 3) out += '-';
    }
    return out;
}

void generateChords(Pattern& p, const ChordGen& g, const ScaleInfo& s) {
    Rng       rng(g.seed);
    const int len = p.length;
    p.clear();
    p.length = len;

    // The progression: a fixed one, or a walk that favours strong moves (down a fifth, to the relative).
    const Prog& prog = kProgs[std::clamp(g.prog, 0, kProgCount - 1)];
    int degrees[16], nDeg = 0;
    if (prog.degrees[0] >= 0) {
        while (nDeg < 8 && prog.degrees[nDeg] >= 0) degrees[nDeg] = prog.degrees[nDeg], ++nDeg;
    } else {
        int d     = 0;
        const int moves[] = {3, 4, 5, 2, 6, 1};
        for (; nDeg < 4; ++nDeg) {
            degrees[nDeg] = d;
            d             = (d + moves[rng.range(rng.uni() < 0.6f ? 3 : 6)]) % 7;
        }
    }

    const int span   = kChordChanges[std::clamp(g.change, 0, 3)];
    const int count  = 3 + int(std::lround(std::clamp(g.complex, 0.0f, 1.0f) * 4.0f)); // 3 .. 7 notes
    const int centre = 12 * (g.octave + 1) + s.root + 5;

    // One voicing per chord, each led from the one before.
    int  voiced[32][SeqStep::kMaxNotes], nVoiced[32];
    int  prev[SeqStep::kMaxNotes], prevN = 0;
    const int chords = (len + span - 1) / span;
    for (int c = 0; c < chords && c < 32; ++c) {
        const int degree = degrees[c % nDeg];
        const bool sus   = g.complex > 0.3f && rng.uni() < g.complex * 0.25f;
        int pitches[SeqStep::kMaxNotes];
        int n = chordPitches(degree, std::min(count, 7), sus, g.octave, s, pitches);
        if (g.notes == ChordNotes::Root) {
            n = 1;
            int best = pitches[0];
            for (int o = -2; o <= 2; ++o) { // the root, in the octave nearest the last one
                int q = pitches[0] + 12 * o;
                if (std::abs(q - (prevN ? prev[0] : centre)) < std::abs(best - (prevN ? prev[0] : centre))) best = q;
            }
            pitches[0] = best;
        } else {
            voiceLead(pitches, n, prev, prevN, centre);
            if (g.voicing == ChordVoicing::Open && n >= 3) { // drop 2: second from the top goes down an octave
                pitches[n - 2] -= 12;
                sortNotes(pitches, n);
            } else if (g.voicing == ChordVoicing::Spread && n >= 3) { // every other note up an octave
                for (int i = 1; i < n; i += 2) pitches[i] += 12;
                pitches[0] -= 12;
                sortNotes(pitches, n);
            }
        }
        std::copy(pitches, pitches + n, prev);
        prevN = n;
        if (g.notes == ChordNotes::FullBass && n < SeqStep::kMaxNotes) { // the root two octaves under the chord's root
            int root = degreePitch(degree, g.octave - 2, s.count == 7 ? s : ScaleInfo{kMinor, 7, s.root});
            std::copy_backward(pitches, pitches + n, pitches + n + 1);
            pitches[0] = root;
            ++n;
        }
        std::copy(pitches, pitches + n, voiced[c]);
        nVoiced[c] = n;
    }

    // Hits: where the rhythm says, each playing its chord until the gate runs out.
    bool hit[Pattern::kMaxSteps] = {};
    for (int i = 0; i < len; ++i) hit[i] = rhythmHit(g.rhythm, i % span, i, rng, beatAccent(i));
    for (int i = 0; i < len; ++i) {
        if (!hit[i]) continue;
        const int c = std::min(i / span, 31);
        int next = i + 1;
        while (next < len && !hit[next]) ++next;
        const int gap  = next - i;
        const int hold = std::clamp(int(std::lround(gap * g.gate)), 1, gap);
        const int vel  = dynVel(beatAccent(i) + (rng.uni() - 0.5f) * 0.4f, g.dynamics);
        SeqStep& st    = p.steps[i];
        const int n    = nVoiced[c];
        for (int k = 0; k < n; ++k) {
            st.notes[k] = {int8_t(std::clamp(voiced[c][k], 0, 127)), uint8_t(vel)};
            int ticks   = n > 1 ? int(std::lround(g.strum * (kTicksPerStep - 1) * k / (n - 1))) : 0;
            if (ticks > 0) st.notes[k].fx = Fx::Dly, st.notes[k].fxVal = uint8_t(ticks);
        }
        if (hold < gap || next >= len) { // an OFF where the chord stops (the next hit would end it anyway)
            SeqStep& end = p.steps[(i + hold) % len];
            for (int k = 0; k < SeqStep::kMaxNotes; ++k) setOff(end, k);
        }
        if (hold == gap && next < len) { // columns the next chord leaves free still need ending
            for (int k = nVoiced[std::min(next / span, 31)]; k < n; ++k) setOff(p.steps[next], k);
        }
    }
}

void mutateChords(Pattern& p, uint32_t seed, float amount) {
    Rng rng(seed);
    for (int i = 0; i < p.length; ++i) {
        SeqStep& st = p.steps[i];
        if (st.count() == 0) continue;
        if (rng.uni() < amount * 0.3f) { // accent the whole chord up or down
            int d = rng.uni() < 0.5f ? 1 : -1;
            for (SeqNote& n : st.notes)
                if (n.isNote()) n.vel = uint8_t(std::clamp(n.vel + d, 1, kMaxVel));
        }
        // Echo the chord on the next empty off-beat, briefly.
        int j = i + 2;
        if (j + 1 < p.length && p.steps[j].empty() && j % 4 == 2 && rng.uni() < amount * 0.15f) {
            for (int k = 0; k < SeqStep::kMaxNotes; ++k) {
                if (!st.notes[k].isNote()) continue;
                p.steps[j].notes[k]     = st.notes[k];
                p.steps[j].notes[k].vel = uint8_t(std::max(1, st.notes[k].vel - 1));
                setOff(p.steps[j + 1], k);
            }
        }
    }
}
