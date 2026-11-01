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
// MODS (envelopes and LFOs) sits above INSTR: SELECT + up / down.
enum Screen { SCR_SEQ, SCR_PATTERN, SCR_INSTR, SCR_MODS, SCR_COUNT };
constexpr int kMainScreens = SCR_MODS; // the row SELECT + left / right walks

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

// Synth parameters as the UI sees them. The first kInstrParams are on INSTR,
// the rest (envelopes and LFOs) on MODS.
enum ParamId {
    P_WAVE, P_POS, P_PITCH, P_DETUNE, P_SUB, P_NOISE,
    P_FILTER, P_CUTOFF, P_RESO, P_DRIVE, P_VOLUME,
    P_DLY_TIME, P_DLY_FDBK, P_DLY_MIX,
    P_AMP_A, P_AMP_D, P_AMP_S, P_AMP_R,
    P_ENV_A, P_ENV_D, P_ENV_S, P_ENV_R, P_ENV_DEST, P_ENV_AMT,
    P_L1_SHAPE, P_L1_RATE, P_L1_DEST, P_L1_AMT, P_L1_TRIG,
    P_L2_SHAPE, P_L2_RATE, P_L2_DEST, P_L2_AMT, P_L2_TRIG,
    P_COUNT
};
constexpr int kInstrParams = P_AMP_A;
const char* paramName(int id);

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
    int    lastVel[kTracks]  = {kDefaultVel, kDefaultVel, kDefaultVel, kDefaultVel}; // and how hard

    Cursor seqCur{0, 0};  // row -1 = BPM / KEY / SCALE fields
    Cursor synthCur;      // cols: 4 per note column (NOTE, VEL, FX command, FX value)
    Cursor drumCur{0, 0}; // col -1 = row label (Euclid), 0..15 = steps
    Cursor instCur;
    Cursor modsCur; // col = block (AMP ENV, ENV 2, LFO 1, LFO 2), row within it

    bool   held[BTN_COUNT]     = {};
    Uint32 repeatAt[BTN_COUNT] = {};
    bool   genOpen   = false; // GENERATE popup shown; it takes the D-pad, A and B
    int    genRow    = 0;
    bool   genEdited = false; // a setting changed since the last undo point
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
void        preview(App& a, int t, int note, int vel = kDefaultVel); // locked; auditions a note for a moment
void        loadPreset(App& a, int t, int index);
void        loadKit(App& a, int t, int index);
void        setTrackType(App& a, int t, bool drum);
void        regenerate(App& a, int t);        // locked; rebuilds track t from its generator settings
void        mutate(App& a, int t);            // locked
void        newSeed(App& a);                  // current track: new seed, regenerate (with undo and a message)
void        mutateTrack(App& a);              // current track: mutate (with undo and a message)
float       synthParamNorm(const Params& p, int id); // bar position 0..1; bipolar amounts sit at 0.5 when 0
bool        synthParamBipolar(int id);
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
    void (*option)(App&, int fine, int coarse); // B held + D-pad; may be null
    const char* hints;
};
extern const ScreenOps kSeqScreen, kPatternScreen, kInstrScreen, kModsScreen;
const ScreenOps& screenOps(Screen s);

// GENERATE popup (ui_gen.cpp), drawn over the current screen while a.genOpen.
void drawGenMenu(App& a);
void genMove(App& a, int dy);
void genEdit(App& a, int fine, int coarse);
void genEnter(App& a);
void genClear(App& a);

// Shared chrome (ui.cpp)
void drawChrome(App& a);                                   // header, transport strip, footer
void drawScope(App& a, int x, int y, int w, int h);
void drawField(SDL_Renderer* r, int x, int y, const std::string& label, const std::string& value,
               bool selected, int valueX); // "LABEL   VALUE" row with cursor highlight
// A synth parameter row: label, value text and a bar (bipolar amounts grow from the middle).
void drawParam(SDL_Renderer* r, const Params& p, int id, int x, int y, int valueX, int barX, int barW, bool selected);
