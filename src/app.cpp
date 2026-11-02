#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "gfx.h"

const char* kBtnNames[BTN_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y",
    "L1", "R1", "L2", "R2", "SELECT", "START", "MENU", "L3", "R3",
};

const Scale kScales[] = {
    {"MAJOR",  {0, 2, 4, 5, 7, 9, 11}, 7},
    {"MINOR",  {0, 2, 3, 5, 7, 8, 10}, 7},
    {"DORIAN", {0, 2, 3, 5, 7, 9, 10}, 7},
    {"PENTA",  {0, 2, 4, 7, 9},        5},
    {"BLUES",  {0, 3, 5, 6, 7, 10},    6},
};
const int kScaleCount = sizeof(kScales) / sizeof(kScales[0]);

const int kEvolveEvery[Evolve::kChoices] = {0, 1, 2, 4, 8, 16};

const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

namespace {

// How each synth parameter is stored, edited and shown.
enum class Kind { Unit, Bipolar, Semis, Enum };
struct ParamDesc {
    const char* name;
    Kind        kind;
    float Params::*f;           // Unit / Bipolar
    int Params::*i;             // Semis / Enum
    int         count;          // Enum
    const char* (*enumName)(int);
};

const char* trigName(int t) { return t ? "RETRIG" : "FREE"; }
const char* unisonName(int u) {
    static const char* kNames[kMaxUnison] = {"1", "2", "3", "4", "5", "6", "7"};
    return kNames[std::clamp(u, 0, kMaxUnison - 1)];
}

constexpr int kWaves = int(Wave::Count), kFilters = int(FilterType::Count), kShapes = int(LfoShape::Count),
              kDests = int(ModDest::Count);

const ParamDesc kDesc[P_COUNT] = {
    {"WAVE", Kind::Enum, nullptr, &Params::wave, kWaves, waveName},
    {"POS", Kind::Unit, &Params::pos, nullptr, 0, nullptr},
    {"PITCH", Kind::Semis, nullptr, &Params::transpose, 0, nullptr},
    {"DETUNE", Kind::Unit, &Params::detune, nullptr, 0, nullptr},
    {"UNISON", Kind::Enum, nullptr, &Params::unison, kMaxUnison, unisonName},
    {"SPREAD", Kind::Unit, &Params::spread, nullptr, 0, nullptr},
    {"SUB", Kind::Unit, &Params::sub, nullptr, 0, nullptr},
    {"NOISE", Kind::Unit, &Params::noise, nullptr, 0, nullptr},
    {"FILTER", Kind::Enum, nullptr, &Params::filter, kFilters, filterName},
    {"CUTOFF", Kind::Unit, &Params::cutoff, nullptr, 0, nullptr},
    {"RESO", Kind::Unit, &Params::resonance, nullptr, 0, nullptr},
    {"DRIVE", Kind::Unit, &Params::drive, nullptr, 0, nullptr},
    {"VOLUME", Kind::Unit, &Params::volume, nullptr, 0, nullptr},
    {"WIDTH", Kind::Unit, &Params::width, nullptr, 0, nullptr},
    {"CHORUS", Kind::Unit, &Params::chorus, nullptr, 0, nullptr},
    {"DLY TIME", Kind::Unit, &Params::delayTime, nullptr, 0, nullptr},
    {"DLY FDBK", Kind::Unit, &Params::delayFeedback, nullptr, 0, nullptr},
    {"DLY MIX", Kind::Unit, &Params::delayMix, nullptr, 0, nullptr},
    {"ATTACK", Kind::Unit, &Params::ampA, nullptr, 0, nullptr},
    {"DECAY", Kind::Unit, &Params::ampD, nullptr, 0, nullptr},
    {"SUSTAIN", Kind::Unit, &Params::ampS, nullptr, 0, nullptr},
    {"RELEASE", Kind::Unit, &Params::ampR, nullptr, 0, nullptr},
    {"ATTACK", Kind::Unit, &Params::envA, nullptr, 0, nullptr},
    {"DECAY", Kind::Unit, &Params::envD, nullptr, 0, nullptr},
    {"SUSTAIN", Kind::Unit, &Params::envS, nullptr, 0, nullptr},
    {"RELEASE", Kind::Unit, &Params::envR, nullptr, 0, nullptr},
    {"DEST", Kind::Enum, nullptr, &Params::envDest, kDests, modDestName},
    {"AMOUNT", Kind::Bipolar, &Params::envAmt, nullptr, 0, nullptr},
    {"SHAPE", Kind::Enum, nullptr, &Params::lfo1Shape, kShapes, lfoShapeName},
    {"RATE", Kind::Unit, &Params::lfo1Rate, nullptr, 0, nullptr},
    {"DEST", Kind::Enum, nullptr, &Params::lfo1Dest, kDests, modDestName},
    {"AMOUNT", Kind::Bipolar, &Params::lfo1Amt, nullptr, 0, nullptr},
    {"TRIG", Kind::Enum, nullptr, &Params::lfo1Trig, 2, trigName},
    {"SHAPE", Kind::Enum, nullptr, &Params::lfo2Shape, kShapes, lfoShapeName},
    {"RATE", Kind::Unit, &Params::lfo2Rate, nullptr, 0, nullptr},
    {"DEST", Kind::Enum, nullptr, &Params::lfo2Dest, kDests, modDestName},
    {"AMOUNT", Kind::Bipolar, &Params::lfo2Amt, nullptr, 0, nullptr},
    {"TRIG", Kind::Enum, nullptr, &Params::lfo2Trig, 2, trigName},
};

std::string seconds(float s) { return s < 1.0f ? fmt("%dMS", int(s * 1000.0f + 0.5f)) : fmt("%.2fS", s); }
std::string percent(float n) { return fmt("%d%%", int(std::lround(n * 100.0f))); }

constexpr int kPreviewKey = 90; // instrument key for auditions; the sequencer uses 100+

} // namespace

ScaleInfo scaleInfo(const App& a) {
    const Scale& s = kScales[a.scale];
    return {s.steps, s.count, a.root};
}

std::string noteName(int m) { return std::string(kNoteNames[m % 12]) + std::to_string(m / 12 - 1); }

std::string trackName(const App& a, int t) {
    if (a.isDrum[t]) {
        const Kit* k = trackKit(a, t);
        return "KIT " + (k ? k->name : std::string("-")) + (a.usesOwnKit[t] ? "*" : "");
    }
    if (a.isSampler[t]) return "SMP " + (a.smpSample[t].name.empty() ? std::string("-") : a.smpSample[t].name);
    return std::string(kPresets[a.preset[t]].name) + (a.edited[t] ? "*" : "");
}

const Kit* trackKit(const App& a, int t) {
    if (a.usesOwnKit[t]) return &a.ownKit[t];
    return a.kits.empty() ? nullptr : &a.kits[a.kit[t]];
}

std::string padName(const App& a, int t, int pad) {
    const Kit* k = trackKit(a, t);
    if (!k) return "-";
    const std::string& n = k->pads[pad].name;
    return n.empty() ? "-" : n;
}

bool setPadSample(App& a, int t, int pad, const std::string& path) {
    Sample      smp;
    std::string error;
    if (!loadWav(path, smp, error)) {
        a.message = "CANNOT LOAD: " + fontSafe(error);
        return false;
    }
    smp.name = fontSafe(sampleName(path));
    // The shared kits stay as they are; the track gets its own copy to change.
    Kit copy;
    bool fresh = !a.usesOwnKit[t];
    if (fresh && !a.kits.empty()) copy = a.kits[a.kit[t]];
    AudioLock lock(a.audio);
    a.drums[t].allOff(); // no voice may keep reading a sample that is replaced
    if (fresh) {
        a.ownKit[t]     = std::move(copy);
        a.usesOwnKit[t] = true;
    }
    a.ownKit[t].pads[pad] = std::move(smp);
    a.drums[t].setKit(&a.ownKit[t]);
    a.padPath[t][pad] = path;
    return true;
}

std::string grooveName(const App& a) {
    return a.groove >= 0 && a.groove < int(a.grooves.size()) ? a.grooves[a.groove].name : "NONE";
}

void applyGroove(App& a) {
    if (a.groove < 0 || a.groove >= int(a.grooves.size())) {
        a.seq.setGroove(nullptr, nullptr, 0);
        return;
    }
    const Groove& g = a.grooves[a.groove];
    float start[Groove::kMaxSteps + 1], gain[Groove::kMaxSteps];
    for (int s = 0; s <= g.steps; ++s) start[s] = s + a.grooveTiming * (g.start[s] - s);
    for (int s = 0; s < g.steps; ++s) gain[s] = 1.0f - a.grooveVelocity * (1.0f - g.vel[s]);
    a.seq.setGroove(start, gain, g.steps);
}

Pattern& pattern(App& a, int t) { return a.seq.patterns[t]; }

void pushUndo(App& a, int t) {
    a.undo[t]    = a.seq.patterns[t];
    a.hasUndo[t] = true;
}

void toggleUndo(App& a, int t) {
    if (!a.hasUndo[t]) return;
    std::swap(a.undo[t], a.seq.patterns[t]);
}

void preview(App& a, int t, int note, int vel) {
    if (a.previewKey >= 0) a.inst[a.previewTrack]->noteOff(a.previewKey);
    a.inst[t]->noteOn(kPreviewKey, note, velocityGain(vel));
    a.previewKey   = kPreviewKey;
    a.previewTrack = t;
    a.previewOffAt = SDL_GetTicks() + 250;
}

void loadPreset(App& a, int t, int index) {
    a.preset[t] = (index % kPresetCount + kPresetCount) % kPresetCount;
    AudioLock lock(a.audio);
    a.synths[t].params = kPresets[a.preset[t]].params;
    a.edited[t]        = false;
}

void loadKit(App& a, int t, int index) {
    if (a.kits.empty()) return;
    int n    = int(a.kits.size());
    a.kit[t] = (index % n + n) % n;
    AudioLock lock(a.audio);
    a.usesOwnKit[t] = false; // picking a kit drops the track's own pad samples
    for (std::string& p : a.padPath[t]) p.clear();
    a.drums[t].setKit(&a.kits[a.kit[t]]);
}

TrackType trackType(const App& a, int t) {
    return a.isDrum[t] ? TT_DRUM : a.isSampler[t] ? TT_SAMPLER : TT_SYNTH;
}

void setTrackType(App& a, int t, TrackType type) {
    if (type == TT_DRUM && a.kits.empty()) {
        a.message = "NO DRUM KITS FOUND IN SAMPLES/KITS";
        return;
    }
    const TrackType was = trackType(a, t);
    if (type == was) return;
    AudioLock lock(a.audio);
    a.inst[t]->allOff();
    a.isDrum[t]        = type == TT_DRUM;
    a.isSampler[t]     = type == TT_SAMPLER;
    a.seq.drumTrack[t] = type == TT_DRUM;
    a.inst[t]          = type == TT_DRUM      ? static_cast<Instrument*>(&a.drums[t])
                         : type == TT_SAMPLER ? static_cast<Instrument*>(&a.samplers[t])
                                              : &a.synths[t];
    // Drum notes mean nothing to a melodic track and back; synth and sampler share theirs.
    if ((was == TT_DRUM) == (type == TT_DRUM)) return;
    pushUndo(a, t);
    int len = a.seq.patterns[t].length;
    a.seq.patterns[t].clear();
    a.seq.patterns[t].length = len;
}

bool setSamplerSample(App& a, int t, const std::string& path) {
    Sample      smp;
    std::string error;
    if (!loadWav(path, smp, error)) {
        a.message = "CANNOT LOAD: " + fontSafe(error);
        return false;
    }
    smp.name  = fontSafe(sampleName(path));
    int root  = rootFromName(path);
    AudioLock lock(a.audio);
    a.samplers[t].setSample(nullptr); // no voice may keep reading the sample that is replaced
    a.smpSample[t] = std::move(smp);
    a.smpPath[t]   = path;
    a.samplers[t].setSample(&a.smpSample[t]);
    if (root >= 0) a.samplers[t].settings.root = root;
    return true;
}

void regenerate(App& a, int t) {
    Pattern& p = a.seq.patterns[t];
    if (a.isDrum[t]) {
        generateDrums(p, a.drumGen[t]);
    } else if (a.chordMode[t]) {
        generateChords(p, a.chordGen[t], scaleInfo(a));
    } else {
        int len = p.length;
        generateMelody(p, a.melodyGen[t], scaleInfo(a));
        p.length = len;
    }
}

void mutate(App& a, int t, uint32_t seed, float amount) {
    if (a.isDrum[t]) mutateDrums(a.seq.patterns[t], seed, amount);
    else if (a.chordMode[t]) mutateChords(a.seq.patterns[t], seed, amount);
    else mutateMelody(a.seq.patterns[t], scaleInfo(a), seed, amount);
}

void evolveOnLoop(App& a, int t) {
    Evolve& e     = a.evolve[t];
    const int every = kEvolveEvery[e.every];
    if (every == 0 || ++e.count < every) return;
    e.count    = 0;
    Pattern& p = a.seq.patterns[t];
    if (e.vary) {
        // A pattern EVOLVE did not write is the user's (an edit, undo, a new generation): vary that from now on.
        if (!e.hasBase || std::memcmp(&p, &e.last, sizeof(Pattern)) != 0) {
            e.base    = p;
            e.hasBase = true;
        }
        p = e.base;
    }
    e.seed = e.seed * 1664525u + 1013904223u;
    mutate(a, t, e.seed, e.amount);
    e.last = p;
}

const char* paramName(int id) { return kDesc[id].name; }

bool synthParamBipolar(int id) { return kDesc[id].kind == Kind::Bipolar; }

float synthParamNorm(const Params& p, int id) {
    const ParamDesc& d = kDesc[id];
    switch (d.kind) {
    case Kind::Unit:    return p.*d.f;
    case Kind::Bipolar: return 0.5f + 0.5f * p.*d.f;
    case Kind::Semis:   return (p.*d.i + 24) / 48.0f;
    default:            return d.count > 1 ? float(p.*d.i) / float(d.count - 1) : 0.0f;
    }
}

// An amount as a percentage, or in the destination's own unit where that reads better.
std::string modAmount(float amt, int dest) {
    if (dest == int(ModDest::Pitch)) return fmt("%+.1fST", amt * kModPitchSemis);
    if (dest == int(ModDest::Cutoff)) return fmt("%+.1fOCT", amt * kModCutoffOct);
    return fmt("%+d%%", int(std::lround(amt * 100.0f)));
}

std::string synthParamText(const Params& p, int id) {
    const ParamDesc& d = kDesc[id];
    if (d.kind == Kind::Enum) return d.enumName(p.*d.i);
    switch (id) {
    case P_POS:
        if (isWavetable(p.wave)) return percent(p.pos);
        return p.wave == int(Wave::Square) ? fmt("PW %d%%", int(std::lround(pulseWidth(p.pos) * 100.0f))) : "-";
    case P_PITCH:  return fmt("%+d", p.transpose);
    case P_CUTOFF: {
        float hz = cutoffHz(p.cutoff);
        return hz >= 1000.0f ? fmt("%.1fKHZ", hz / 1000.0f) : fmt("%dHZ", int(hz));
    }
    case P_DETUNE:   return p.unison > 0 ? "-" : fmt("%.1fCT", detuneCents(p.detune));
    case P_SPREAD:   return p.unison > 0 ? fmt("%.1fCT", spreadCents(p.spread)) : "-";
    case P_DLY_TIME: return seconds(delaySec(p.delayTime));
    case P_DLY_FDBK: return percent(delayFeedbackGain(p.delayFeedback));
    case P_AMP_A:    return seconds(attackSec(p.ampA));
    case P_AMP_D:    return seconds(decaySec(p.ampD));
    case P_AMP_R:    return seconds(releaseSec(p.ampR));
    case P_ENV_A:    return seconds(attackSec(p.envA));
    case P_ENV_D:    return seconds(decaySec(p.envD));
    case P_ENV_R:    return seconds(releaseSec(p.envR));
    case P_ENV_AMT:  return modAmount(p.envAmt, p.envDest);
    case P_L1_RATE:  return fmt("%.2fHZ", lfoHz(p.lfo1Rate));
    case P_L2_RATE:  return fmt("%.2fHZ", lfoHz(p.lfo2Rate));
    case P_L1_AMT:   return modAmount(p.lfo1Amt, p.lfo1Dest);
    case P_L2_AMT:   return modAmount(p.lfo2Amt, p.lfo2Dest);
    default:         return percent(p.*d.f);
    }
}

void adjustSynthParam(Params& p, int id, int fine, int coarse) {
    const ParamDesc& d = kDesc[id];
    switch (d.kind) {
    case Kind::Unit:    p.*d.f = std::clamp(p.*d.f + fine * 0.01f + coarse * 0.1f, 0.0f, 1.0f); break;
    case Kind::Bipolar: p.*d.f = std::clamp(p.*d.f + fine * 0.01f + coarse * 0.1f, -1.0f, 1.0f); break;
    case Kind::Semis:   p.*d.i = std::clamp(p.*d.i + fine + coarse * 12, -24, 24); break;
    case Kind::Enum:    p.*d.i = (p.*d.i + fine + coarse + d.count) % d.count; break;
    }
}

void resetSynthParam(Params& p, const Params& from, int id) {
    const ParamDesc& d = kDesc[id];
    if (d.f) p.*d.f = from.*d.f;
    else p.*d.i = from.*d.i;
}

// A fresh seed for the current track's generator.
void newSeed(App& a) {
    const int t    = a.track;
    uint32_t  seed = (SDL_GetTicks() * 2654435761u) % 9999 + 1;
    AudioLock lock(a.audio);
    pushUndo(a, t);
    if (a.isDrum[t]) a.drumGen[t].seed = seed;
    else if (a.chordMode[t]) a.chordGen[t].seed = seed;
    else a.melodyGen[t].seed = seed;
    regenerate(a, t);
    a.message = fmt("T%d: NEW PATTERN, SEED %u  (SEL+B: UNDO)", t + 1, seed);
}

void mutateTrack(App& a) {
    AudioLock lock(a.audio);
    pushUndo(a, a.track);
    mutate(a, a.track, SDL_GetTicks() * 2654435761u + uint32_t(a.track), 0.5f);
    a.message = fmt("T%d: MUTATED  (SEL+B: UNDO)", a.track + 1);
}

// ---------------------------------------------------------------- clip bank

int keepClip(App& a, int t, int slot) {
    if (slot < 0)
        for (int s = 0; s < kSlots && slot < 0; ++s)
            if (!a.clips[t][s].used) slot = s;
    if (slot < 0 || slot >= kSlots) {
        a.message = fmt("T%d: NO FREE SLOT  (B+A ON A CLIP DELETES IT)", t + 1);
        return -1;
    }
    Clip& c = a.clips[t][slot];
    {
        AudioLock lock(a.audio);
        c.pat         = a.seq.patterns[t];
        a.seq.slot[t] = slot; // the live pattern now is this clip
    }
    c.used    = true;
    c.drum    = a.isDrum[t];
    a.message = fmt("T%d: KEPT IN SLOT %d", t + 1, slot + 1);
    return slot;
}

void launchClip(App& a, int t, int slot) {
    const Clip& c = a.clips[t][slot];
    if (!c.used) return;
    if (c.drum != a.isDrum[t]) {
        a.message = fmt("T%d: THIS CLIP IS FOR A %s TRACK", t + 1, c.drum ? "DRUM" : "SYNTH");
        return;
    }
    AudioLock lock(a.audio);
    pushUndo(a, t); // the live pattern it replaces can come back with SEL+B
    a.seq.queue(t, c.pat, slot);
}

bool clipMatchesLive(App& a, int t, int slot) {
    if (slot < 0 || slot >= kSlots || !a.clips[t][slot].used) return false;
    return std::memcmp(&a.clips[t][slot].pat, &a.seq.patterns[t], sizeof(Pattern)) == 0;
}
