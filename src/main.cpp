// Knulli First Synth - a minimal polyphonic synth for the TrimUI Brick (Knulli).
// SDL2 only: video via SDL_Renderer, audio via SDL audio callback.

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "drumkit.h"
#include "font.h"
#include "sequencer.h"
#include "synth.h"

namespace {

constexpr int kW = 1024; // TrimUI Brick native resolution
constexpr int kH = 768;

// Buttons use SDL GameController positional names: A = south, B = east,
// X = west, Y = north (the printed labels on the Brick may differ).
enum Btn {
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_A, BTN_B, BTN_X, BTN_Y,
    BTN_L1, BTN_R1, BTN_L2, BTN_R2,
    BTN_SELECT, BTN_START, BTN_MENU,
    BTN_L3, BTN_R3, // the two small buttons top-centre on the Brick's face (LEFTSTICK / RIGHTSTICK)
    BTN_COUNT
};
const char* kBtnNames[BTN_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y",
    "L1", "R1", "L2", "R2", "SELECT", "START", "MENU", "L3", "R3",
};
constexpr int kTracks = Sequencer::kTracks;

// Eight playable pads in ascending scale order: D-pad first, then face buttons.
constexpr int kPads = 8;
const Btn kPadButtons[kPads] = {BTN_LEFT, BTN_DOWN, BTN_RIGHT, BTN_UP, BTN_X, BTN_A, BTN_B, BTN_Y};

struct Scale {
    const char* name;
    int         steps[7];
    int         count;
};
const Scale kScales[] = {
    {"MAJOR",  {0, 2, 4, 5, 7, 9, 11}, 7},
    {"MINOR",  {0, 2, 3, 5, 7, 8, 10}, 7},
    {"DORIAN", {0, 2, 3, 5, 7, 9, 10}, 7},
    {"PENTA",  {0, 2, 4, 7, 9},        5},
    {"BLUES",  {0, 3, 5, 6, 7, 10},    6},
};
constexpr int kScaleCount = sizeof(kScales) / sizeof(kScales[0]);

enum ParamId {
    P_WAVE, P_CUTOFF, P_RESO, P_ATTACK, P_RELEASE, P_DETUNE, P_VOLUME,
    P_FENV_AMT, P_FENV_DECAY,
    P_LFO_RATE, P_LFO_DEPTH, P_LFO_DEST,
    P_DLY_TIME, P_DLY_FDBK, P_DLY_MIX,
    P_COUNT
};
const char* kParamNames[P_COUNT] = {
    "WAVE", "CUTOFF", "RESO", "ATTACK", "RELEASE", "DETUNE", "VOLUME",
    "FENV AMT", "FENV DEC",
    "LFO RATE", "LFO DPTH", "LFO DEST",
    "DLY TIME", "DLY FDBK", "DLY MIX",
};
constexpr int kParamRows = 7; // rows visible in the parameter panel

// Fallback for joysticks SDL has no GameController mapping for. Indices are a
// guess; the footer shows raw indices so this table can be corrected on device.
const Btn kRawJoyButtons[] = {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_L1, BTN_R1, BTN_SELECT, BTN_START, BTN_MENU, BTN_L2, BTN_R2};

struct App {
    SDL_Window*       window   = nullptr;
    SDL_Renderer*     renderer = nullptr;
    SDL_AudioDeviceID audio      = 0;
    int               channels   = 2;
    int               sampleRate = 48000;
    std::vector<float> mono;

    // DSP load = callback time / buffer duration, written by the audio thread.
    std::atomic<float> dspLoad{0.0f}; // smoothed
    std::atomic<float> dspPeak{0.0f}; // max since the UI last took it
    float  shownPeak    = 0.0f;
    Uint32 peakResetAt  = 0;

    // Each track has both instruments; inst[] points at the one it plays.
    Synth       synths[kTracks];
    DrumKit     drums[kTracks];
    Instrument* inst[kTracks] = {};
    bool        isDrum[kTracks] = {};
    std::vector<Kit> kits; // loaded once at startup, never changed afterwards
    int         kit[kTracks]     = {};
    int         drumPad[kTracks] = {}; // pad whose settings EDIT shows on a drum track
    Sequencer   seq;
    Sequencer::RecHandle rec[8]; // per pad, while a recorded note is held

    // Master output for the scope, written by the audio thread.
    float scope[2048] = {};
    int   scopePos    = 0;

    bool   held[BTN_COUNT]     = {};
    Uint32 repeatAt[BTN_COUNT] = {};
    bool   padDown[kPads]      = {};

    int  track     = 0; // selected track: pads, presets, EDIT and recording act on it
    int  preset[kTracks] = {0, 2, 6, 3};
    bool edited[kTracks] = {};
    bool trackUsed = false; // L3 was part of a combo, so its release doesn't step the track
    int  octave    = 4;
    int  scale     = 0;
    int  root      = 0;     // key, 0 = C
    bool selectUsed = false; // SELECT was part of a combo, so its release doesn't toggle EDIT
    bool editMode  = false;
    int  paramSel  = P_CUTOFF;
    int  paramTop  = 0; // first visible row of the scrolling parameter list
    bool quit      = false;

    std::set<SDL_JoystickID> controllerIds; // joysticks handled as GameControllers
    Uint8       hatState[8] = {};
    bool        triggerDown[2] = {};
    std::string lastInput = "PRESS ANY BUTTON";
};

// ---------------------------------------------------------------- helpers

struct AudioLock {
    SDL_AudioDeviceID dev;
    explicit AudioLock(SDL_AudioDeviceID d) : dev(d) { if (dev) SDL_LockAudioDevice(dev); }
    ~AudioLock() { if (dev) SDL_UnlockAudioDevice(dev); }
};

std::string fmt(const char* f, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

int padMidi(const App& a, int pad) {
    if (a.isDrum[a.track]) return DrumKit::kBaseNote + pad;
    const Scale& s = kScales[a.scale];
    return 12 * (a.octave + 1) + a.root + s.steps[pad % s.count] + 12 * (pad / s.count);
}

const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

std::string noteName(int m) { return std::string(kNoteNames[m % 12]) + std::to_string(m / 12 - 1); }

// Continuous parameters; enum parameters (P_WAVE, P_LFO_DEST) return nullptr.
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

std::string paramText(const Params& p, int id) {
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

float paramNorm(const Params& p, int id) {
    if (id == P_WAVE) return float(int(p.wave)) / float(int(Wave::Count) - 1);
    if (id == P_LFO_DEST) return float(int(p.lfoDest)) / float(int(LfoDest::Count) - 1);
    return *paramValue(const_cast<Params&>(p), id);
}

// ---------------------------------------------------------------- audio

void audioCallback(void* userdata, Uint8* stream, int len) {
    const Uint64 start  = SDL_GetPerformanceCounter();
    App&         a      = *static_cast<App*>(userdata);
    float*       out    = reinterpret_cast<float*>(stream);
    const int    total  = len / int(sizeof(float) * a.channels);
    int          frames = total;
    while (frames > 0) {
        int n = std::min(frames, int(a.mono.size()));
        a.seq.process(a.inst, a.mono.data(), n);
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < a.channels; ++c) *out++ = a.mono[i];
            a.scope[a.scopePos] = a.mono[i];
            a.scopePos          = (a.scopePos + 1) % 2048;
        }
        frames -= n;
    }

    const double elapsed = double(SDL_GetPerformanceCounter() - start) / double(SDL_GetPerformanceFrequency());
    const float  load    = float(elapsed * a.sampleRate / total);
    a.dspLoad.store(a.dspLoad.load(std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
    if (load > a.dspPeak.load(std::memory_order_relaxed)) a.dspPeak.store(load, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- actions

Instrument& curInst(App& a) { return *a.inst[a.track]; }

// Preset name (with * when edited), or the kit name on a drum track.
std::string trackName(const App& a, int t) {
    if (a.isDrum[t]) return "KIT " + a.kits[a.kit[t]].name;
    return std::string(kPresets[a.preset[t]].name) + (a.edited[t] ? "*" : "");
}

int padIndex(Btn b) {
    for (int p = 0; p < kPads; ++p)
        if (kPadButtons[p] == b) return p;
    return -1;
}

int padForButton(const App& a, Btn b) {
    int p = padIndex(b);
    return (p >= 0 && p < 4 && a.editMode) ? -1 : p; // D-pad edits params in EDIT mode
}

void releasePad(App& a, int p) {
    if (!a.padDown[p]) return;
    curInst(a).noteOff(p);
    a.padDown[p] = false;
    a.seq.finishRecord(a.rec[p]);
    a.rec[p] = {};
}

void releaseDpadPads(App& a) {
    AudioLock lock(a.audio);
    for (int p = 0; p < 4; ++p) releasePad(a, p);
}

void selectTrack(App& a, int t) {
    AudioLock lock(a.audio);
    for (int p = 0; p < kPads; ++p) releasePad(a, p); // held notes belong to the old track's instrument
    a.track = (t + kTracks) % kTracks;
}

void loadPreset(App& a, int t, int index) {
    a.preset[t] = (index + kPresetCount) % kPresetCount;
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

// Switches the track between SYNTH and DRUM; needs at least one kit on disk.
void setTrackType(App& a, int t, bool drum) {
    if (drum && a.kits.empty()) {
        a.lastInput = "NO DRUM KITS FOUND IN SAMPLES/KITS";
        return;
    }
    AudioLock lock(a.audio);
    if (t == a.track)
        for (int p = 0; p < kPads; ++p) releasePad(a, p);
    a.inst[t]->allOff();
    a.isDrum[t] = drum;
    a.inst[t]   = drum ? static_cast<Instrument*>(&a.drums[t]) : &a.synths[t];
}

// ---- EDIT parameters: the synth's list, or the selected pad's settings on a drum track.

enum DrumParamId { D_PAD, D_PITCH, D_START, D_END, D_VOLUME, D_COUNT };
const char* kDrumParamNames[D_COUNT] = {"PAD", "PITCH", "START", "END", "VOLUME"};

int paramCount(const App& a) { return a.isDrum[a.track] ? int(D_COUNT) : int(P_COUNT); }

struct ParamRow {
    std::string name, value;
    float       norm;
};

// Snapshot of the visible parameter list. Lock audio first.
std::vector<ParamRow> paramRows(App& a) {
    std::vector<ParamRow> rows;
    const int t = a.track;
    if (!a.isDrum[t]) {
        const Params& p = a.synths[t].params;
        for (int i = 0; i < P_COUNT; ++i) rows.push_back({kParamNames[i], paramText(p, i), paramNorm(p, i)});
        return rows;
    }
    const int                     pad = a.drumPad[t];
    const DrumKit::PadSettings&   ps  = a.drums[t].pads[pad];
    const Kit*                    kit = a.drums[t].kit();
    std::string                   name = kit ? kit->pads[pad].name : "";
    if (name.size() > 7) name.resize(7); // fits the value column
    rows.push_back({kDrumParamNames[D_PAD], fmt("%d %s", pad + 1, name.c_str()), pad / float(kPads - 1)});
    rows.push_back({kDrumParamNames[D_PITCH], fmt("%+dST", ps.pitch), (ps.pitch + 24) / 48.0f});
    rows.push_back({kDrumParamNames[D_START], percent(ps.start), ps.start});
    rows.push_back({kDrumParamNames[D_END], percent(ps.end), ps.end});
    rows.push_back({kDrumParamNames[D_VOLUME], percent(ps.volume), ps.volume});
    return rows;
}

void adjustDrumParam(App& a, int dir) {
    const int             t  = a.track;
    DrumKit::PadSettings& ps = a.drums[t].pads[a.drumPad[t]];
    switch (a.paramSel) {
    case D_PAD:    a.drumPad[t] = (a.drumPad[t] + dir + kPads) % kPads; break;
    case D_PITCH:  ps.pitch = std::clamp(ps.pitch + dir, -24, 24); break;
    case D_START:  ps.start = std::clamp(ps.start + dir * 0.01f, 0.0f, ps.end - 0.01f); break;
    case D_END:    ps.end = std::clamp(ps.end + dir * 0.01f, ps.start + 0.01f, 1.0f); break;
    case D_VOLUME: ps.volume = std::clamp(ps.volume + dir * 0.02f, 0.0f, 1.0f); break;
    default: break;
    }
}

void adjustParam(App& a, int dir) {
    AudioLock lock(a.audio);
    a.paramSel = std::min(a.paramSel, paramCount(a) - 1);
    if (a.isDrum[a.track]) {
        adjustDrumParam(a, dir);
        return;
    }
    Params& p = a.synths[a.track].params;
    if (a.paramSel == P_WAVE) {
        int n  = int(Wave::Count);
        p.wave = Wave((int(p.wave) + dir + n) % n);
    } else if (a.paramSel == P_LFO_DEST) {
        int n     = int(LfoDest::Count);
        p.lfoDest = LfoDest((int(p.lfoDest) + dir + n) % n);
    } else {
        float* v = paramValue(p, a.paramSel);
        *v       = std::clamp(*v + dir * 0.02f, 0.0f, 1.0f);
    }
    a.edited[a.track] = true;
}

void togglePlay(App& a) {
    AudioLock lock(a.audio);
    if (a.seq.playing()) {
        a.seq.stop(a.inst);
        a.seq.recording = false;
    } else {
        a.seq.start();
    }
}

// Repeatable SELECT combos (BPM and pattern length), also driven by key repeat.
void selectNudge(App& a, Btn b) {
    AudioLock lock(a.audio);
    switch (b) {
    case BTN_LEFT:  a.seq.setBpm(a.seq.bpm() - 1.0f); break;
    case BTN_RIGHT: a.seq.setBpm(a.seq.bpm() + 1.0f); break;
    case BTN_DOWN:  a.seq.setLength(a.track, a.seq.patterns[a.track].length - 1); break;
    case BTN_UP:    a.seq.setLength(a.track, a.seq.patterns[a.track].length + 1); break;
    default: break;
    }
}

void toggleRecord(App& a) {
    AudioLock lock(a.audio);
    a.seq.recording = !a.seq.recording;
    if (a.seq.recording && !a.seq.playing()) a.seq.start();
}

// L3 held: the D-pad pads pick tracks 1-4, the face pads mute them, R1 solos the selected one.
void onTrackCombo(App& a, Btn b) {
    int p = padIndex(b);
    if (p >= 0 && p < 4) {
        selectTrack(a, p);
    } else if (p >= 4) {
        AudioLock lock(a.audio);
        a.seq.mute[p - 4] = !a.seq.mute[p - 4];
    } else if (b == BTN_R1) {
        AudioLock lock(a.audio);
        a.seq.solo = a.seq.solo == a.track ? -1 : a.track;
    } else if (b == BTN_L1) {
        setTrackType(a, a.track, !a.isDrum[a.track]);
    }
}

void onSelectCombo(App& a, Btn b) {
    switch (b) {
    case BTN_A: toggleRecord(a); break;
    case BTN_B: { AudioLock lock(a.audio); a.seq.undo(a.track); break; }
    case BTN_X: { AudioLock lock(a.audio); a.seq.clearPattern(a.track); break; }
    case BTN_Y: a.seq.metronome = !a.seq.metronome; break;
    case BTN_LEFT: case BTN_RIGHT: case BTN_UP: case BTN_DOWN:
        selectNudge(a, b);
        a.repeatAt[b] = SDL_GetTicks() + 350;
        break;
    case BTN_L1: a.scale = (a.scale + kScaleCount - 1) % kScaleCount; break;
    case BTN_R1: a.scale = (a.scale + 1) % kScaleCount; break;
    case BTN_L2: a.root = (a.root + 11) % 12; break;
    case BTN_R2: a.root = (a.root + 1) % 12; break;
    default: break;
    }
}

void onPress(App& a, Btn b) {
    if (a.held[b]) return;
    a.held[b] = true;

    if ((b == BTN_START && a.held[BTN_SELECT]) || (b == BTN_SELECT && a.held[BTN_START])) {
        a.quit = true;
        return;
    }

    // SELECT is a modifier; tapped on its own it toggles EDIT (on release).
    if (b == BTN_SELECT) {
        a.selectUsed = false;
        return;
    }
    if (a.held[BTN_SELECT]) {
        a.selectUsed = true;
        onSelectCombo(a, b);
        return;
    }
    // L3 is the track modifier; tapped on its own it steps to the next track (on release).
    if (b == BTN_L3) {
        a.trackUsed = false;
        return;
    }
    if (a.held[BTN_L3]) {
        a.trackUsed = true;
        onTrackCombo(a, b);
        return;
    }

    int pad = padForButton(a, b);
    if (pad >= 0) {
        AudioLock lock(a.audio);
        int note = padMidi(a, pad);
        curInst(a).noteOn(pad, note);
        a.padDown[pad] = true;
        if (a.isDrum[a.track]) a.drumPad[a.track] = pad; // EDIT follows the last hit
        if (a.seq.recording) a.rec[pad] = a.seq.record(a.track, note);
        return;
    }

    switch (b) {
    case BTN_L1:
    case BTN_R1: {
        int dir = b == BTN_L1 ? -1 : 1;
        if (a.isDrum[a.track]) loadKit(a, a.track, a.kit[a.track] + dir);
        else loadPreset(a, a.track, a.preset[a.track] + dir);
        break;
    }
    case BTN_R3: toggleRecord(a); break;
    case BTN_L2: a.octave = std::max(a.octave - 1, 1); break;
    case BTN_R2: a.octave = std::min(a.octave + 1, 7); break;
    case BTN_START: togglePlay(a); break;
    case BTN_UP:
    case BTN_DOWN:
        if (a.editMode) {
            int n      = paramCount(a);
            a.paramSel = (std::min(a.paramSel, n - 1) + (b == BTN_UP ? n - 1 : 1)) % n;
        }
        break;
    case BTN_LEFT:
    case BTN_RIGHT:
        if (a.editMode) {
            adjustParam(a, b == BTN_LEFT ? -1 : 1);
            a.repeatAt[b] = SDL_GetTicks() + 350;
        }
        break;
    default: break;
    }
}

void onRelease(App& a, Btn b) {
    if (!a.held[b]) return;
    a.held[b] = false;
    if (b == BTN_SELECT && !a.selectUsed) {
        a.editMode = !a.editMode;
        releaseDpadPads(a);
        return;
    }
    if (b == BTN_L3 && !a.trackUsed) {
        selectTrack(a, a.track + 1);
        return;
    }
    int p = padIndex(b);
    if (p >= 0 && a.padDown[p]) {
        AudioLock lock(a.audio);
        releasePad(a, p);
    }
}

void setButton(App& a, Btn b, bool down) { down ? onPress(a, b) : onRelease(a, b); }

// ---------------------------------------------------------------- input

int keyToBtn(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP:    case SDL_SCANCODE_W: return BTN_UP;
    case SDL_SCANCODE_DOWN:  case SDL_SCANCODE_S: return BTN_DOWN;
    case SDL_SCANCODE_LEFT:  case SDL_SCANCODE_A: return BTN_LEFT;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: return BTN_RIGHT;
    case SDL_SCANCODE_K: return BTN_A;
    case SDL_SCANCODE_L: return BTN_B;
    case SDL_SCANCODE_J: return BTN_X;
    case SDL_SCANCODE_I: return BTN_Y;
    case SDL_SCANCODE_Q: return BTN_L1;
    case SDL_SCANCODE_E: return BTN_R1;
    case SDL_SCANCODE_Z: return BTN_L2;
    case SDL_SCANCODE_C: return BTN_R2;
    case SDL_SCANCODE_TAB: case SDL_SCANCODE_BACKSPACE: return BTN_SELECT;
    case SDL_SCANCODE_RETURN: return BTN_START;
    case SDL_SCANCODE_T: return BTN_L3;
    case SDL_SCANCODE_R: return BTN_R3;
    default: return -1;
    }
}

int controllerToBtn(Uint8 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return BTN_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return BTN_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return BTN_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return BTN_RIGHT;
    case SDL_CONTROLLER_BUTTON_A:             return BTN_A;
    case SDL_CONTROLLER_BUTTON_B:             return BTN_B;
    case SDL_CONTROLLER_BUTTON_X:             return BTN_X;
    case SDL_CONTROLLER_BUTTON_Y:             return BTN_Y;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return BTN_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return BTN_R1;
    case SDL_CONTROLLER_BUTTON_BACK:          return BTN_SELECT;
    case SDL_CONTROLLER_BUTTON_START:         return BTN_START;
    case SDL_CONTROLLER_BUTTON_GUIDE:         return BTN_MENU;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return BTN_L3;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return BTN_R3;
    default: return -1;
    }
}

void openDevice(App& a, int index) {
    if (SDL_IsGameController(index)) {
        SDL_GameController* gc = SDL_GameControllerOpen(index);
        if (!gc) return;
        SDL_Joystick* js = SDL_GameControllerGetJoystick(gc);
        a.controllerIds.insert(SDL_JoystickInstanceID(js));
        char guid[64];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), guid, sizeof(guid));
        SDL_Log("controller: %s guid=%s", SDL_GameControllerName(gc), guid);
        a.lastInput = fmt("CONNECTED PAD: %s", SDL_GameControllerName(gc));
    } else {
        SDL_Joystick* js = SDL_JoystickOpen(index);
        if (!js) return;
        char guid[64];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), guid, sizeof(guid));
        SDL_Log("raw joystick: %s guid=%s buttons=%d axes=%d hats=%d", SDL_JoystickName(js), guid,
                SDL_JoystickNumButtons(js), SDL_JoystickNumAxes(js), SDL_JoystickNumHats(js));
        a.lastInput = fmt("CONNECTED RAW JOY: %s", SDL_JoystickName(js));
    }
}

void handleEvent(App& a, const SDL_Event& e) {
    switch (e.type) {
    case SDL_QUIT:
        a.quit = true;
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        if (e.key.repeat) break;
        if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { a.quit = true; break; }
        int b = keyToBtn(e.key.keysym.scancode);
        if (b >= 0) {
            setButton(a, Btn(b), e.type == SDL_KEYDOWN);
            a.lastInput = fmt("KEY %s -> %s", SDL_GetScancodeName(e.key.keysym.scancode), kBtnNames[b]);
        }
        break;
    }
    case SDL_JOYDEVICEADDED:
        openDevice(a, e.jdevice.which);
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        int b = controllerToBtn(e.cbutton.button);
        a.lastInput = fmt("PAD BTN %s -> %s",
                          SDL_GameControllerGetStringForButton(SDL_GameControllerButton(e.cbutton.button)),
                          b >= 0 ? kBtnNames[b] : "-");
        if (b >= 0) setButton(a, Btn(b), e.type == SDL_CONTROLLERBUTTONDOWN);
        break;
    }
    case SDL_CONTROLLERAXISMOTION: {
        int t = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 0
              : e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? 1 : -1;
        if (t < 0) break;
        bool down = a.triggerDown[t] ? e.caxis.value > 8000 : e.caxis.value > 16000;
        if (down != a.triggerDown[t]) {
            a.triggerDown[t] = down;
            setButton(a, t == 0 ? BTN_L2 : BTN_R2, down);
            a.lastInput = fmt("PAD TRIGGER %s", t == 0 ? "L2" : "R2");
        }
        break;
    }
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        if (a.controllerIds.count(e.jbutton.which)) break;
        int n  = sizeof(kRawJoyButtons) / sizeof(kRawJoyButtons[0]);
        int b  = e.jbutton.button < n ? kRawJoyButtons[e.jbutton.button] : -1;
        a.lastInput = fmt("RAW JOY BUTTON %d -> %s", e.jbutton.button, b >= 0 ? kBtnNames[b] : "-");
        if (b >= 0) setButton(a, Btn(b), e.type == SDL_JOYBUTTONDOWN);
        break;
    }
    case SDL_JOYHATMOTION: {
        if (a.controllerIds.count(e.jhat.which) || e.jhat.hat >= 8) break;
        Uint8 prev = a.hatState[e.jhat.hat];
        Uint8 now  = e.jhat.value;
        a.hatState[e.jhat.hat] = now;
        const struct { Uint8 bit; Btn btn; } dirs[] = {
            {SDL_HAT_UP, BTN_UP}, {SDL_HAT_DOWN, BTN_DOWN}, {SDL_HAT_LEFT, BTN_LEFT}, {SDL_HAT_RIGHT, BTN_RIGHT}};
        for (const auto& d : dirs)
            if ((prev ^ now) & d.bit) setButton(a, d.btn, (now & d.bit) != 0);
        a.lastInput = fmt("RAW JOY HAT %d = %d", e.jhat.hat, now);
        break;
    }
    case SDL_JOYAXISMOTION:
        if (!a.controllerIds.count(e.jaxis.which) && std::abs(e.jaxis.value) > 16000)
            a.lastInput = fmt("RAW JOY AXIS %d = %d", e.jaxis.axis, e.jaxis.value);
        break;
    default:
        break;
    }
}

void handleRepeat(App& a) {
    Uint32 now = SDL_GetTicks();
    if (a.held[BTN_SELECT]) {
        for (Btn b : {BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN}) {
            if (a.held[b] && SDL_TICKS_PASSED(now, a.repeatAt[b])) {
                selectNudge(a, b);
                a.repeatAt[b] = now + 60;
            }
        }
        return;
    }
    if (!a.editMode) return;
    for (Btn b : {BTN_LEFT, BTN_RIGHT}) {
        if (a.held[b] && SDL_TICKS_PASSED(now, a.repeatAt[b])) {
            adjustParam(a, b == BTN_LEFT ? -1 : 1);
            a.repeatAt[b] = now + 40;
        }
    }
}

// ---------------------------------------------------------------- drawing

const Glyph* gGlyphs[128] = {};

void initFont() {
    for (const Glyph& g : kGlyphs) gGlyphs[int(g.c)] = &g;
}

int textWidth(const std::string& s, int scale) { return s.empty() ? 0 : int(s.size()) * 6 * scale - scale; }

void drawText(SDL_Renderer* r, int x, int y, int scale, const std::string& s, SDL_Color c) {
    std::vector<SDL_Rect> rects;
    for (char ch : s) {
        int code = (ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
        const Glyph* g = (code > 0 && code < 128) ? gGlyphs[code] : nullptr;
        if (g)
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (g->rows[row][col] == '1') rects.push_back({x + col * scale, y + row * scale, scale, scale});
        x += 6 * scale;
    }
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    if (!rects.empty()) SDL_RenderFillRects(r, rects.data(), int(rects.size()));
}

void drawTextCentered(SDL_Renderer* r, int cx, int y, int scale, const std::string& s, SDL_Color c) {
    drawText(r, cx - textWidth(s, scale) / 2, y, scale, s, c);
}

void fillRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

void strokeRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderDrawRect(r, &rc);
}

const SDL_Color kBg      = {14, 16, 26, 255};
const SDL_Color kPanel   = {26, 30, 46, 255};
const SDL_Color kLine    = {58, 64, 92, 255};
const SDL_Color kText    = {220, 224, 236, 255};
const SDL_Color kDim     = {110, 118, 150, 255};
const SDL_Color kAccent  = {255, 176, 64, 255};
const SDL_Color kPlayCol = {96, 220, 140, 255};
const SDL_Color kScopeC  = {90, 210, 255, 255};

void drawParams(App& a) {
    SDL_Renderer* r = a.renderer;
    const int x = 24, y = 196, w = 480, rowH = 40;
    const int panelH = kParamRows * rowH + 20;
    fillRect(r, x, y - 12, w, panelH, kPanel);

    std::vector<ParamRow> rows;
    {
        AudioLock lock(a.audio);
        rows = paramRows(a);
    }
    const int count = int(rows.size());
    a.paramSel      = std::min(a.paramSel, count - 1);
    a.paramTop      = std::min(a.paramTop, std::max(count - kParamRows, 0));

    // Keep the selected row inside the visible window.
    if (a.paramSel < a.paramTop) a.paramTop = a.paramSel;
    if (a.paramSel >= a.paramTop + kParamRows) a.paramTop = a.paramSel - kParamRows + 1;

    // Scroll bar on the panel's right edge.
    const int trackH = panelH - 16;
    fillRect(r, x + w - 8, y - 4, 4, trackH, kLine);
    const int shown = std::min(kParamRows, count);
    fillRect(r, x + w - 8, y - 4 + trackH * a.paramTop / count, 4, trackH * shown / count, kDim);

    for (int i = a.paramTop; i < a.paramTop + shown; ++i) {
        int  ry  = y + (i - a.paramTop) * rowH;
        bool sel = a.editMode && i == a.paramSel;
        if (sel) fillRect(r, x + 4, ry - 6, w - 16, rowH - 4, {60, 48, 30, 255});
        SDL_Color c = sel ? kAccent : kText;
        drawText(r, x + 14, ry + 2, 3, sel ? ">" : " ", kAccent);
        drawText(r, x + 40, ry + 2, 3, rows[i].name, c);
        drawText(r, x + 214, ry + 2, 3, rows[i].value, c);
        int bx = x + 378, bw = 86;
        fillRect(r, bx, ry + 4, bw, 14, kLine);
        fillRect(r, bx, ry + 4, int(bw * rows[i].norm), 14, sel ? kAccent : kDim);
    }
}

// Master output, triggered on a rising zero crossing so periodic waves stand still.
void drawScope(App& a, int x, int y, int w, int h) {
    SDL_Renderer* r = a.renderer;
    SDL_SetRenderDrawColor(r, kLine.r, kLine.g, kLine.b, 255);
    SDL_RenderDrawLine(r, x, y + h / 2, x + w - 1, y + h / 2);

    constexpr int kSize = sizeof(a.scope) / sizeof(a.scope[0]);
    static float buf[kSize];
    {
        AudioLock lock(a.audio);
        for (int i = 0; i < kSize; ++i) buf[i] = a.scope[(a.scopePos + i) % kSize];
    }
    const int span = w * 2;
    int start = 0;
    for (int i = 1; i < kSize - span; ++i)
        if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) { start = i; break; }

    std::vector<SDL_Point> pts(w);
    for (int i = 0; i < w; ++i) {
        float s = std::clamp(buf[start + i * 2], -1.0f, 1.0f);
        pts[i]  = {x + i, y + h / 2 - int(s * (h / 2 - 2))};
    }
    SDL_SetRenderDrawColor(r, kScopeC.r, kScopeC.g, kScopeC.b, 255);
    SDL_RenderDrawLines(r, pts.data(), w);
}

struct SeqView {
    Pattern patterns[kTracks];
    int     step[kTracks];
    bool    audible[kTracks];
    bool    mute[kTracks];
    int     solo;
    bool    playing, recording, metronome, canUndo;
    float   bpm;
};

const SDL_Color kRecCol = {255, 80, 80, 255};

void drawPad(App& a, const SeqView& v, int pad, int cx, int cy, const char* label) {
    SDL_Renderer* r = a.renderer;
    const int w = 110, h = 46;
    std::string text;
    bool        on, disabled = false;
    if (a.held[BTN_L3]) { // track modifier: pads become track select / mute
        text = pad < 4 ? fmt("TRK%d", pad + 1) : fmt("MUTE%d", pad - 3);
        on   = pad < 4 ? pad == a.track : v.mute[pad - 4];
    } else if (a.isDrum[a.track]) {
        disabled = pad < 4 && a.editMode;
        text     = disabled ? "PARAM" : a.kits[a.kit[a.track]].pads[pad].name;
        on       = a.padDown[pad];
    } else {
        disabled = pad < 4 && a.editMode;
        text     = disabled ? "PARAM" : noteName(padMidi(a, pad));
        on       = a.padDown[pad];
    }
    // The pad whose settings EDIT shows on a drum track gets an accent outline.
    bool editing = a.editMode && a.isDrum[a.track] && pad == a.drumPad[a.track] && !a.held[BTN_L3];
    SDL_Color onCol = a.held[BTN_L3] ? (pad < 4 ? kAccent : kRecCol) : kPlayCol;
    fillRect(r, cx - w / 2, cy - h / 2, w, h, on ? onCol : kPanel);
    strokeRect(r, cx - w / 2, cy - h / 2, w, h, editing ? kAccent : disabled ? kLine : kDim);
    SDL_Color tc = on ? kBg : (disabled ? kLine : kText);
    if (text.size() <= 6) {
        drawTextCentered(r, cx, cy - 16, 3, text, tc);
    } else {
        if (text.size() > 8) text.resize(8);
        drawTextCentered(r, cx, cy - 12, 2, text, tc);
    }
    drawTextCentered(r, cx, cy + 6, 2, label, on ? kBg : kDim);
}

void drawPadCluster(App& a, const SeqView& v, int firstPad, int cx, int cy, const char* labels[4]) {
    // Pad order within a cluster: left, down, right, up.
    drawPad(a, v, firstPad + 0, cx - 120, cy, labels[0]);
    drawPad(a, v, firstPad + 1, cx, cy + 52, labels[1]);
    drawPad(a, v, firstPad + 2, cx + 120, cy, labels[2]);
    drawPad(a, v, firstPad + 3, cx, cy - 52, labels[3]);
}

// One row per track: preset, mute / solo, length, and a cell per step lit where notes are.
void drawTracks(App& a, const SeqView& v) {
    SDL_Renderer* r = a.renderer;
    const int x = 524, y = 184, w = 476, rowH = 75;
    fillRect(r, x, y, w, rowH * kTracks, kPanel);

    for (int t = 0; t < kTracks; ++t) {
        const int  ry  = y + t * rowH;
        const bool sel = t == a.track;
        if (sel) {
            fillRect(r, x, ry, w, rowH, {44, 40, 34, 255});
            fillRect(r, x, ry, 6, rowH, kAccent);
        }
        SDL_Color label = !v.audible[t] ? kDim : sel ? kAccent : kText;
        std::string name = fmt("%d %s", t + 1, trackName(a, t).c_str());
        drawText(r, x + 16, ry + 10, 2, name, label);

        const Pattern& pat = v.patterns[t];
        std::string    tag = fmt("LEN %d", pat.length);
        const int      tx  = x + w - 12 - textWidth(tag, 2);
        drawText(r, tx, ry + 10, 2, tag, kDim);
        if (v.solo == t) drawText(r, tx - 72, ry + 10, 2, "SOLO", kAccent);
        else if (v.mute[t]) drawText(r, tx - 72, ry + 10, 2, "MUTE", kRecCol);

        const int gx = x + 16, gw = w - 28, cy = ry + 32, ch = 32;
        const int len = pat.length;
        for (int i = 0; i < len; ++i) {
            int  cx0 = gx + gw * i / len, cx1 = gx + gw * (i + 1) / len;
            bool has = !pat.steps[i].empty();
            bool cur = v.playing && i == v.step[t];
            SDL_Color note = v.audible[t] ? kScopeC : SDL_Color{48, 92, 112, 255};
            SDL_Color c = cur ? (v.recording && sel ? kRecCol : kAccent)
                        : has ? note
                        : (i % 4 == 0 ? kLine : SDL_Color{40, 45, 66, 255});
            fillRect(r, cx0 + 1, cy, std::max(cx1 - cx0 - 2, 1), ch, c);
            if (cur && has) fillRect(r, cx0 + 3, cy + 4, std::max(cx1 - cx0 - 6, 1), ch - 8, note);
        }
    }
}

// Transport plus the master scope.
void drawTransport(App& a, const SeqView& v) {
    SDL_Renderer* r = a.renderer;
    const int x = 24, y = 652, h = 44;
    fillRect(r, x, y, kW - 48, h, kPanel);

    const char* state = v.recording ? "REC" : v.playing ? "PLAY" : "STOP";
    SDL_Color   sc    = v.recording ? kRecCol : v.playing ? kPlayCol : kDim;
    drawText(r, x + 10, y + 4, 3, state, sc);
    drawText(r, x + 112, y + 6, 2, fmt("%3d BPM", int(v.bpm + 0.5f)), kText);
    drawText(r, x + 112, y + 26, 2, fmt("TRACK %d", a.track + 1), kText);
    drawText(r, x + 10, y + 28, 2, "CLK", v.metronome ? kText : kLine);
    drawText(r, x + 56, y + 28, 2, "UNDO", v.canUndo ? kText : kLine);

    drawScope(a, x + 216, y + 2, kW - 48 - 216 - 8, h - 4);
}

void draw(App& a) {
    SDL_Renderer* r = a.renderer;
    int     voices;
    SeqView sv;
    {
        AudioLock lock(a.audio);
        voices = 0;
        for (int t = 0; t < kTracks; ++t) {
            voices += a.inst[t]->activeVoices();
            sv.patterns[t] = a.seq.patterns[t];
            sv.step[t]     = a.seq.currentStep(t);
            sv.audible[t]  = a.seq.audible(t);
            sv.mute[t]     = a.seq.mute[t];
        }
        sv.solo      = a.seq.solo;
        sv.playing   = a.seq.playing();
        sv.recording = a.seq.recording;
        sv.metronome = a.seq.metronome;
        sv.canUndo   = a.seq.canUndo(a.track);
        sv.bpm       = a.seq.bpm();
    }

    SDL_SetRenderDrawColor(r, kBg.r, kBg.g, kBg.b, 255);
    SDL_RenderClear(r);

    // Header
    fillRect(r, 0, 0, kW, 64, kPanel);
    drawText(r, 24, 18, 4, "KNULLI FIRST SYNTH", kAccent);
    const char* mode = a.editMode ? "EDIT" : "PLAY";
    SDL_Color   mc   = a.editMode ? kAccent : kPlayCol;
    fillRect(r, kW - 150, 12, 126, 40, mc);
    drawTextCentered(r, kW - 87, 18, 4, mode, kBg);

    // DSP load meter: smoothed value plus the peak over the last second.
    Uint32 now = SDL_GetTicks();
    if (SDL_TICKS_PASSED(now, a.peakResetAt)) {
        a.shownPeak   = a.dspPeak.exchange(0.0f);
        a.peakResetAt = now + 1000;
    }
    float     load = a.dspLoad.load() * 100.0f;
    float     peak = a.shownPeak * 100.0f;
    SDL_Color lc   = peak >= 80.0f ? SDL_Color{255, 80, 80, 255} : peak >= 50.0f ? kAccent : kPlayCol;
    drawText(r, 520, 14, 2, fmt("DSP %4.1f%%", load), lc);
    drawText(r, 520, 38, 2, fmt("PEAK %4.1f%%", peak), lc);
    fillRect(r, 680, 18, 160, 10, kLine);
    fillRect(r, 680, 18, int(160 * std::min(load / 100.0f, 1.0f)), 10, lc);
    fillRect(r, 680, 42, 160, 10, kLine);
    fillRect(r, 680, 42, int(160 * std::min(peak / 100.0f, 1.0f)), 10, lc);

    // Preset
    std::string name = trackName(a, a.track);
    drawText(r, 24, 96, 6, "<", kDim);
    drawText(r, kW - 24 - textWidth(">", 6), 96, 6, ">", kDim);
    drawTextCentered(r, kW / 2, 92, 6, name, kText);
    std::string sub = a.isDrum[a.track]
                    ? fmt("TRACK %d   DRUM KIT %d/%d   (L1 / R1)", a.track + 1, a.kit[a.track] + 1, int(a.kits.size()))
                    : fmt("TRACK %d   PRESET %d/%d   (L1 / R1)", a.track + 1, a.preset[a.track] + 1, kPresetCount);
    drawTextCentered(r, kW / 2, 142, 2, sub, kDim);

    drawParams(a);
    drawTracks(a, sv);

    // Pads
    const char* dpadLabels[4] = {"LEFT", "DOWN", "RIGHT", "UP"};
    const char* faceLabels[4] = {"X", "A", "B", "Y"};
    drawPadCluster(a, sv, 0, 200, 568, dpadLabels);
    drawPadCluster(a, sv, 4, 824, 568, faceLabels);

    if (a.isDrum[a.track]) {
        drawTextCentered(r, kW / 2, 540, 3, "DRUMS", kText);
    } else {
        drawTextCentered(r, kW / 2, 520, 3, fmt("OCT %d", a.octave), kText);
        drawTextCentered(r, kW / 2, 560, 3, fmt("%s %s", kNoteNames[a.root], kScales[a.scale].name), kText);
    }
    drawTextCentered(r, kW / 2, 600, 3, fmt("VOICES %d", voices), voices ? kPlayCol : kDim);
    if (!a.audio) drawTextCentered(r, kW / 2, 494, 3, "NO AUDIO", {255, 80, 80, 255});

    drawTransport(a, sv);

    // Footer
    fillRect(r, 0, kH - 64, kW, 64, kPanel);
    // While a modifier is held, list what it combines with.
    if (a.held[BTN_SELECT])
        drawText(r, 16, kH - 54, 2, "+A:REC +B:UNDO +X:CLEAR +Y:CLICK +</>:BPM +UP/DN:LEN +L1/R1:SCALE +L2/R2:KEY", kAccent);
    else if (a.held[BTN_L3])
        drawText(r, 16, kH - 54, 2, "L3 + D-PAD:TRACK  + X/A/B/Y:MUTE  + R1:SOLO  + L1:SYNTH/DRUM  (TAP:NEXT)", kAccent);
    else
        drawText(r, 16, kH - 54, 2, a.isDrum[a.track]
                                        ? "START:PLAY R3:REC L3:TRACK SELECT:EDIT/MORE L1/R1:KIT"
                                        : "START:PLAY R3:REC L3:TRACK SELECT:EDIT/MORE L1/R1:PRESET L2/R2:OCT",
                 kDim);
    std::string in = "IN: " + a.lastInput;
    if (in.size() > 84) in.resize(84);
    drawText(r, 16, kH - 26, 2, in, kText);

    SDL_RenderPresent(r);
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    App a;
    initFont();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_version v;
    SDL_GetVersion(&v);
    SDL_Log("SDL %d.%d.%d video=%s audio=%s", v.major, v.minor, v.patch, SDL_GetCurrentVideoDriver(),
            SDL_GetCurrentAudioDriver());

    int added = SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
    if (added > 0) SDL_Log("loaded %d controller mappings", added);

#if defined(_WIN32) || defined(__APPLE__)
    bool windowed = true;
#else
    bool windowed = std::getenv("KSYNTH_WINDOWED") != nullptr;
#endif
    Uint32 wflags = windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP;
    a.window = SDL_CreateWindow("Knulli First Synth", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kW, kH, wflags);
    if (!a.window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }
    a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a.renderer) a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_SOFTWARE);
    if (!a.renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return 1;
    }
    SDL_RendererInfo ri;
    SDL_GetRendererInfo(a.renderer, &ri);
    SDL_Log("renderer=%s", ri.name);
    SDL_RenderSetLogicalSize(a.renderer, kW, kH);
    if (!windowed) SDL_ShowCursor(SDL_DISABLE);

    SDL_AudioSpec want{}, have{};
    want.freq     = 48000;
    want.format   = AUDIO_F32SYS;
    want.channels = 2;
    want.samples  = 512;
    want.callback = audioCallback;
    want.userdata = &a;
    a.audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
                                  SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE |
                                      SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (a.audio) {
        a.channels   = have.channels;
        a.sampleRate = have.freq;
        a.mono.resize(std::max<int>(have.samples, 512));
        for (Synth& s : a.synths) s.init(have.freq);
        for (DrumKit& d : a.drums) d.init(have.freq);
        a.seq.init(have.freq);
        a.seq.latencyFrames = have.samples;
        SDL_Log("audio: %d Hz, %d ch, %d samples", have.freq, have.channels, have.samples);
    } else {
        SDL_Log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        for (Synth& s : a.synths) s.init(48000);
        for (DrumKit& d : a.drums) d.init(48000);
        a.seq.init(48000);
    }

    // Drum kits live in samples/kits/<kit>/*.wav next to the binary (on the device),
    // or under the working directory (running from the repo on PC).
    {
        char*       base = SDL_GetBasePath();
        std::string dir  = std::string(base ? base : "") + "samples/kits";
        SDL_free(base);
        a.kits = loadKits(dir);
        if (a.kits.empty()) a.kits = loadKits("samples/kits");
        SDL_Log("drum kits: %d", int(a.kits.size()));
    }
    for (int t = 0; t < kTracks; ++t) {
        a.inst[t] = &a.synths[t];
        loadPreset(a, t, a.preset[t]);
        loadKit(a, t, 0);
    }
    setTrackType(a, kTracks - 1, true); // the last track starts as drums when a kit is available
    if (a.audio) SDL_PauseAudioDevice(a.audio, 0);

    while (!a.quit) {
        Uint32  frameStart = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(a, e);
        handleRepeat(a);
        draw(a);
        Uint32 elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < 16) SDL_Delay(16 - elapsed); // in case vsync is unavailable
    }

    if (a.audio) SDL_CloseAudioDevice(a.audio);
    SDL_DestroyRenderer(a.renderer);
    SDL_DestroyWindow(a.window);
    SDL_Quit();
    return 0;
}
