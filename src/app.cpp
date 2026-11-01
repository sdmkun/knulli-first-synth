#include "app.h"

#include <algorithm>
#include <cmath>

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

constexpr int kWaves = int(Wave::Count), kFilters = int(FilterType::Count), kShapes = int(LfoShape::Count),
              kDests = int(ModDest::Count);

const ParamDesc kDesc[P_COUNT] = {
    {"WAVE", Kind::Enum, nullptr, &Params::wave, kWaves, waveName},
    {"POS", Kind::Unit, &Params::pos, nullptr, 0, nullptr},
    {"PITCH", Kind::Semis, nullptr, &Params::transpose, 0, nullptr},
    {"DETUNE", Kind::Unit, &Params::detune, nullptr, 0, nullptr},
    {"SUB", Kind::Unit, &Params::sub, nullptr, 0, nullptr},
    {"NOISE", Kind::Unit, &Params::noise, nullptr, 0, nullptr},
    {"FILTER", Kind::Enum, nullptr, &Params::filter, kFilters, filterName},
    {"CUTOFF", Kind::Unit, &Params::cutoff, nullptr, 0, nullptr},
    {"RESO", Kind::Unit, &Params::resonance, nullptr, 0, nullptr},
    {"DRIVE", Kind::Unit, &Params::drive, nullptr, 0, nullptr},
    {"VOLUME", Kind::Unit, &Params::volume, nullptr, 0, nullptr},
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
    if (a.isDrum[t]) return "KIT " + a.kits[a.kit[t]].name;
    return std::string(kPresets[a.preset[t]].name) + (a.edited[t] ? "*" : "");
}

std::string padName(const App& a, int t, int pad) {
    if (a.kits.empty()) return "-";
    const std::string& n = a.kits[a.kit[t]].pads[pad].name;
    return n.empty() ? "-" : n;
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
    a.drums[t].setKit(&a.kits[a.kit[t]]);
}

void setTrackType(App& a, int t, bool drum) {
    if (drum && a.kits.empty()) {
        a.message = "NO DRUM KITS FOUND IN SAMPLES/KITS";
        return;
    }
    if (drum == a.isDrum[t]) return;
    AudioLock lock(a.audio);
    a.inst[t]->allOff();
    a.isDrum[t] = drum;
    a.inst[t]   = drum ? static_cast<Instrument*>(&a.drums[t]) : &a.synths[t];
    // The notes of one type mean nothing to the other.
    pushUndo(a, t);
    int len = a.seq.patterns[t].length;
    a.seq.patterns[t].clear();
    a.seq.patterns[t].length = len;
}

void regenerate(App& a, int t) {
    Pattern& p = a.seq.patterns[t];
    if (a.isDrum[t]) {
        generateDrums(p, a.drumGen[t]);
    } else {
        int len = p.length;
        generateMelody(p, a.melodyGen[t], scaleInfo(a));
        p.length = len;
    }
}

void mutate(App& a, int t) {
    uint32_t seed = SDL_GetTicks() * 2654435761u + uint32_t(t);
    if (a.isDrum[t]) mutateDrums(a.seq.patterns[t], seed, 0.5f);
    else mutateMelody(a.seq.patterns[t], scaleInfo(a), seed, 0.5f);
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
    case P_DETUNE:   return fmt("%.1fCT", detuneCents(p.detune));
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
    else a.melodyGen[t].seed = seed;
    regenerate(a, t);
    a.message = fmt("T%d: NEW PATTERN, SEED %u  (SEL+B: UNDO)", t + 1, seed);
}

void mutateTrack(App& a) {
    AudioLock lock(a.audio);
    pushUndo(a, a.track);
    mutate(a, a.track);
    a.message = fmt("T%d: MUTATED  (SEL+B: UNDO)", a.track + 1);
}
