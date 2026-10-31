#include "app.h"

#include <algorithm>

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

const char* kParamNames[P_COUNT] = {
    "WAVE", "CUTOFF", "RESO", "ATTACK", "RELEASE", "DETUNE", "VOLUME",
    "FENV AMT", "FENV DEC",
    "LFO RATE", "LFO DPTH", "LFO DEST",
    "DLY TIME", "DLY FDBK", "DLY MIX",
};

namespace {

float* paramValue(Params& p, int id) {
    switch (id) {
    case P_CUTOFF:     return &p.cutoff;
    case P_RESO:       return &p.resonance;
    case P_ATTACK:     return &p.attack;
    case P_RELEASE:    return &p.release;
    case P_DETUNE:     return &p.detune;
    case P_VOLUME:     return &p.volume;
    case P_FENV_AMT:   return &p.fenvAmount;
    case P_FENV_DECAY: return &p.fenvDecay;
    case P_LFO_RATE:   return &p.lfoRate;
    case P_LFO_DEPTH:  return &p.lfoDepth;
    case P_DLY_TIME:   return &p.delayTime;
    case P_DLY_FDBK:   return &p.delayFeedback;
    case P_DLY_MIX:    return &p.delayMix;
    default:           return nullptr;
    }
}

std::string seconds(float s) { return s < 1.0f ? fmt("%dMS", int(s * 1000.0f + 0.5f)) : fmt("%.2fS", s); }
std::string percent(float n) { return fmt("%d%%", int(n * 100.0f + 0.5f)); }

constexpr int kPreviewKey = 90; // instrument key for auditions; pads used 0..7, the sequencer 100+

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
    for (SeqStep& s : a.seq.patterns[t].steps)
        for (SeqNote& n : s.notes) n.fresh = false;
}

void preview(App& a, int t, int note) {
    if (a.previewKey >= 0) a.inst[a.previewTrack]->noteOff(a.previewKey);
    a.inst[t]->noteOn(kPreviewKey, note);
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

float synthParamNorm(const Params& p, int id) {
    if (id == P_WAVE) return float(int(p.wave)) / float(int(Wave::Count) - 1);
    if (id == P_LFO_DEST) return float(int(p.lfoDest)) / float(int(LfoDest::Count) - 1);
    return *paramValue(const_cast<Params&>(p), id);
}

std::string synthParamText(const Params& p, int id) {
    switch (id) {
    case P_WAVE:    return waveName(p.wave);
    case P_CUTOFF: {
        float hz = cutoffHz(p.cutoff);
        return hz >= 1000.0f ? fmt("%.1fKHZ", hz / 1000.0f) : fmt("%dHZ", int(hz));
    }
    case P_RESO:       return percent(p.resonance);
    case P_ATTACK:     return seconds(attackSec(p.attack));
    case P_RELEASE:    return seconds(releaseSec(p.release));
    case P_DETUNE:     return fmt("%.1fCT", detuneCents(p.detune));
    case P_VOLUME:     return percent(p.volume);
    case P_FENV_AMT:   return fmt("+%.1fOCT", fenvOctaves(p.fenvAmount));
    case P_FENV_DECAY: return seconds(fenvDecaySec(p.fenvDecay));
    case P_LFO_RATE:   return fmt("%.2fHZ", lfoHz(p.lfoRate));
    case P_LFO_DEPTH:  return percent(p.lfoDepth);
    case P_LFO_DEST:   return lfoDestName(p.lfoDest);
    case P_DLY_TIME:   return seconds(delaySec(p.delayTime));
    case P_DLY_FDBK:   return percent(delayFeedbackGain(p.delayFeedback));
    case P_DLY_MIX:    return percent(p.delayMix);
    default:           return "";
    }
}

void adjustSynthParam(Params& p, int id, int fine, int coarse) {
    int dir = fine + coarse;
    if (id == P_WAVE) {
        int n  = int(Wave::Count);
        p.wave = Wave((int(p.wave) + dir + n) % n);
    } else if (id == P_LFO_DEST) {
        int n     = int(LfoDest::Count);
        p.lfoDest = LfoDest((int(p.lfoDest) + dir + n) % n);
    } else if (float* v = paramValue(p, id)) {
        *v = std::clamp(*v + fine * 0.01f + coarse * 0.1f, 0.0f, 1.0f);
    }
}

void resetSynthParam(Params& p, const Params& from, int id) {
    if (id == P_WAVE) p.wave = from.wave;
    else if (id == P_LFO_DEST) p.lfoDest = from.lfoDest;
    else if (float* v = paramValue(p, id)) *v = *paramValue(const_cast<Params&>(from), id);
}
