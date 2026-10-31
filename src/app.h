#pragma once

#include <SDL.h>

#include <atomic>
#include <set>
#include <string>
#include <vector>

#include "drumkit.h"
#include "generate.h"
#include "sequencer.h"
#include "synth.h"

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
extern const char* kBtnNames[BTN_COUNT];

// Screens sit side by side like the M8's; SELECT + left / right moves between them.
enum Screen { SCR_SEQ, SCR_PATTERN, SCR_INSTR, SCR_COUNT };

constexpr int kTracks    = Sequencer::kTracks;
constexpr int kPageSteps = 16; // steps shown at once; longer patterns are paged with L2 / R2
constexpr int kSynthCols = 4;  // note columns shown per step on a synth track

struct Scale {
    const char* name;
    int         steps[7];
    int         count;
};
extern const Scale  kScales[];
extern const int    kScaleCount;
extern const char*  kNoteNames[12];

enum ParamId {
    P_WAVE, P_CUTOFF, P_RESO, P_ATTACK, P_RELEASE, P_DETUNE, P_VOLUME,
    P_FENV_AMT, P_FENV_DECAY,
    P_LFO_RATE, P_LFO_DEPTH, P_LFO_DEST,
    P_DLY_TIME, P_DLY_FDBK, P_DLY_MIX,
    P_COUNT
};
extern const char* kParamNames[P_COUNT];

struct Cursor {
    int row = 0;
    int col = 0;
};

struct App {
    SDL_Window*       window     = nullptr;
    SDL_Renderer*     renderer   = nullptr;
    SDL_AudioDeviceID audio      = 0;
    int               channels   = 2;
    int               sampleRate = 48000;
    std::vector<float> mono;

    // DSP load = callback time / buffer duration, written by the audio thread.
    std::atomic<float> dspLoad{0.0f}; // smoothed
    std::atomic<float> dspPeak{0.0f}; // max since the UI last took it
    float  shownPeak   = 0.0f;
    Uint32 peakResetAt = 0;

    // Master output for the scope, written by the audio thread.
    float scope[2048] = {};
    int   scopePos    = 0;

    // Each track has both instruments; inst[] points at the one it plays.
    Synth            synths[kTracks];
    DrumKit          drums[kTracks];
    Instrument*      inst[kTracks]   = {};
    bool             isDrum[kTracks] = {};
    std::vector<Kit> kits; // loaded once at startup, never changed afterwards
    int              kit[kTracks]    = {};
    int              preset[kTracks] = {0, 2, 6, 3};
    bool             edited[kTracks] = {};
    Sequencer        seq;

    // Generator settings per track, and the rotation each drum row was last given.
    DrumGen   drumGen[kTracks];
    MelodyGen melodyGen[kTracks];
    int       euclidRot[kTracks][kDrumPads] = {};

    // One undo level per track; undo swaps, so pressing it again redoes.
    Pattern undo[kTracks];
    bool    hasUndo[kTracks] = {};

    Screen screen = SCR_SEQ;
    int    track  = 0;
    int    page   = 0;
    int    scale  = 1; // MINOR
    int    root   = 0; // C
    int    lastNote[kTracks] = {60, 36, 60, 60}; // what A drops into an empty synth cell

    Cursor seqCur{0, 0};  // row -1 = BPM / KEY / SCALE fields
    Cursor synthCur;      // cols 0..7 = note / length pairs, 8 = generator panel
    Cursor drumCur{0, 0}; // col -1 = row label (Euclid), 0..15 = steps, 16 = generator panel
    Cursor instCur;

    bool   held[BTN_COUNT]     = {};
    Uint32 repeatAt[BTN_COUNT] = {};
    Screen aScreen = SCR_SEQ; // screen A went down on; A+D-pad edits only if it is still showing

    int    previewKey   = -1; // note held by an audition, released at previewOffAt
    int    previewTrack = 0;
    Uint32 previewOffAt = 0;

    std::string message; // bottom line: last input, or a short notice
    bool        quit = false;

    std::set<SDL_JoystickID> controllerIds; // joysticks handled as GameControllers
    Uint8 hatState[8]    = {};
    bool  triggerDown[2] = {};
};

// Holds the audio device lock for a scope; everything shared with the audio thread goes through it.
struct AudioLock {
    SDL_AudioDeviceID dev;
    explicit AudioLock(SDL_AudioDeviceID d) : dev(d) { if (dev) SDL_LockAudioDevice(dev); }
    ~AudioLock() { if (dev) SDL_UnlockAudioDevice(dev); }
};

// ---- shared state helpers (app.cpp). Functions marked "locked" expect AudioLock held.
ScaleInfo   scaleInfo(const App& a);
std::string noteName(int midi);
std::string trackName(const App& a, int t);  // preset (with * when edited) or "KIT name"
std::string padName(const App& a, int t, int pad);
Pattern&    pattern(App& a, int t);
void        pushUndo(App& a, int t);          // locked
void        toggleUndo(App& a, int t);        // locked
void        preview(App& a, int t, int note); // locked; auditions a note for a moment
void        loadPreset(App& a, int t, int index);
void        loadKit(App& a, int t, int index);
void        setTrackType(App& a, int t, bool drum);
void        regenerate(App& a, int t);        // locked; rebuilds track t from its generator settings
void        mutate(App& a, int t);            // locked
float       synthParamNorm(const Params& p, int id);
std::string synthParamText(const Params& p, int id);
void        adjustSynthParam(Params& p, int id, int fine, int coarse);
void        resetSynthParam(Params& p, const Params& from, int id); // copies one parameter

// ---- screens. edit(): fine = left/right (-1/+1), coarse = up/down (+1/-1 = up/down).
struct ScreenOps {
    const char* name;
    void (*draw)(App&);
    void (*move)(App&, int dx, int dy);
    void (*edit)(App&, int fine, int coarse); // A held + D-pad
    void (*enter)(App&);                      // A pressed
    void (*clear)(App&);                      // B + A
    const char* hints;
};
extern const ScreenOps kSeqScreen, kPatternScreen, kInstrScreen;
const ScreenOps& screenOps(Screen s);

// Shared chrome (ui.cpp)
void drawChrome(App& a);                                   // header, transport strip, footer
void drawScope(App& a, int x, int y, int w, int h);
void drawField(SDL_Renderer* r, int x, int y, const std::string& label, const std::string& value,
               bool selected, int valueX); // "LABEL   VALUE" row with cursor highlight
