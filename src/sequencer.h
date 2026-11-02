#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "reverb.h"

class Instrument;

// Note commands, played in ticks (kTicksPerStep per step):
//   RET v   retrigger every v ticks for the rest of the step (a roll)
//   ARP xy  cycle the note, +x and +y semitones every kArpTicks while it sounds
//   KIL v   cut the note v ticks after it starts
//   DLY v   start the note v ticks late
//   CHA v   play the note with a v% chance
enum class Fx : uint8_t { None, Ret, Arp, Kil, Dly, Cha, Count };
const char* fxName(Fx fx);
int         fxDefault(Fx fx);                  // value a freshly chosen command starts at
int         fxClamp(Fx fx, int value);         // keeps a value in the command's range
std::string fxValueText(Fx fx, int value);     // "03", "37" (ARP shows two hex digits), ...

constexpr int kTicksPerStep = 12; // so rolls can divide a step by 2, 3, 4 or 6
constexpr int kArpTicks     = 3;

// One entry in a step's column. Like a tracker, a note sounds until the next
// note or OFF in the same column (wrapping around the pattern), not for a set length.
struct SeqNote {
    static constexpr int8_t kEmpty = -1;
    static constexpr int8_t kOff   = -2;

    int8_t  pitch = kEmpty; // MIDI note, or kEmpty / kOff
    uint8_t vel   = 6;      // rough velocity 1..kMaxVel, see velocityGain()
    Fx      fx    = Fx::None;
    uint8_t fxVal = 0;

    bool isNote() const { return pitch >= 0; }
};

constexpr int kMaxVel     = 8;
constexpr int kDefaultVel = 6; // unity gain
float velocityGain(int vel);

// A note command on a whole row (step): it works on every note the row starts
// and, on a row without notes, on the notes still sounding (KIL cuts them, ARP
// arpeggiates them, RET retriggers them). Synth tracks use these; drum hits
// carry their own command in SeqNote instead.
struct StepFx {
    Fx      fx  = Fx::None;
    uint8_t val = 0;
};

struct SeqStep {
    static constexpr int kMaxNotes = 8; // columns; a drum step can hold the whole kit
    static constexpr int kFx       = 2; // row commands, shown at the right edge
    SeqNote notes[kMaxNotes];
    StepFx  fx[kFx];
    bool    empty() const;               // no notes and no OFFs (row commands aside)
    int     count() const;               // notes only
    bool    hasOff() const;
    int     find(int pitch) const;       // column holding `pitch`, or -1
    bool    add(int pitch, int vel);     // first free column; false when full. Same pitch updates its velocity
    void    remove(int pitch);
    // Notes (not OFFs) sorted low to high; returns how many were written to `out`.
    int     sorted(SeqNote* out) const;
};

struct Pattern {
    static constexpr int kMaxSteps = 64;
    int     length = 16; // in 1/16 steps
    SeqStep steps[kMaxSteps];
    void    clear();
};

// Multi-track step sequencer driven from the audio thread so steps land
// sample-accurately. Each track owns a pattern (with its own length, so tracks
// can run as a polymeter) and plays one Instrument. Everything except
// process() is for the UI thread and must be called with the audio device locked.
class Sequencer {
public:
    static constexpr int kTracks  = 4;
    static constexpr int kKeyBase = 100; // instrument keys for sequenced notes; auditions use lower ones
    static constexpr int kKeys    = 64;
    static constexpr int kKickNote = 36; // pad 1 of a drum kit  // keys rotate so a released note can ring out under the next one

    void init(int sampleRate);

    // Audio thread: renders and mixes every track's instrument into `left` /
    // `right`, firing steps on time. `inst` holds kTracks instruments.
    void process(Instrument* const* inst, float* left, float* right, int frames);

    void start();
    void stop(Instrument* const* inst);
    bool playing() const { return playing_; }
    int  currentStep(int track) const { return step_[track]; }

    void  setBpm(float bpm);
    float bpm() const { return bpm_; }
    void  setLength(int track, int steps);

    // Swing: 0.5 = straight. Every other step (counted from the song start) is
    // pushed later; 0.67 is a triplet shuffle, 0.75 the most this allows.
    void  setSwing(float s);
    float swing() const { return swing_; }

    // Groove: where each of `steps` steps starts (start[steps] closes the loop, so
    // start[i + 1] - start[i] is step i's length in steps) and a gain per step.
    // Counted from the song start like swing, which it replaces. steps = 0 turns it off.
    static constexpr int kMaxGrooveSteps = 64;
    void setGroove(const float* start, const float* gain, int steps);

    // Muted tracks release their notes and stop firing. A solo track mutes all the others.
    bool audible(int track) const { return solo >= 0 ? track == solo : !mute[track]; }

    Pattern patterns[kTracks];
    bool    mute[kTracks] = {};
    int     solo          = -1;
    float   volume[kTracks] = {1.0f, 1.0f, 1.0f, 1.0f}; // track gain, 0..kMaxVolume
    float   level[kTracks]  = {};  // peak output per track since the UI last cleared it
    float   pan[kTracks]    = {};  // -1 left .. 1 right
    float   send[kTracks]   = {};  // into the reverb, 0..1
    float   duck[kTracks]   = {};  // sidechain: how far a kick pushes the track down, 0..1
    bool    drumTrack[kTracks] = {}; // the kick (pad 1) of a drum track drives the ducking
    float   duckTime = 0.35f;      // 0..1: how fast the ducked tracks come back
    float   reverbSize = 0.45f, reverbTone = 0.55f; // what the UI shows; pass them on to `reverb`
    Reverb  reverb;
    static constexpr float kMaxVolume = 1.5f;

    // Called on the audio thread when an audible track wraps back to its first
    // step (not on the very first step after start), before that step fires.
    // Whatever it changes in the pattern plays from this loop on.
    void (*onLoop)(void* user, int track) = nullptr;
    void* loopUser = nullptr;
    int   loops(int track) const { return loops_[track]; } // wraps since start

    // Clip launching: `p` replaces the track's pattern on the next multiple of
    // launchQuant steps (counted from the song start), from its first step.
    // Stopped, it replaces it right away. `slot` is only remembered for the UI.
    void queue(int track, const Pattern& p, int slot);
    void cancelQueue(int track) { queued_[track] = false; }
    int  queuedSlot(int track) const { return queued_[track] ? queuedSlot_[track] : -1; }
    int  launchQuant = 16;
    int  slot[kTracks] = {-1, -1, -1, -1}; // where the playing pattern came from, -1 = nowhere
    long songStep() const { return tick_ < 0 ? 0 : tick_ / kTicksPerStep; } // steps since start

private:
    // What one column of one track is doing between ticks.
    struct Column {
        int     key      = -1; // sounding instrument key
        int     note     = 0;
        float   gain     = 1.0f;
        bool    pending  = false; // a note or OFF waits for its (delayed) tick in this step
        int     pendTick = 0;
        SeqNote pend;
        int     retEvery = 0; // ticks between retriggers, 0 = no roll
        int     retNext  = 0; // tick within the step
        uint8_t arp      = 0; // xy semitone offsets, 0 = no arpeggio
        int     arpAge   = 0; // ticks since the note started
        long    killAt   = -1; // absolute tick
    };

    double samplesPerTick() const;
    void   fireTick(Instrument* const* inst);
    void   startStep(int track);
    void   trigger(Instrument& inst, int track, Column& col, const SeqNote& n);
    void   applyFx(Column& col, Fx fx, int val); // RET / ARP / KIL on a sounding column
    int    newKey(int track);
    void   releaseTrack(Instrument& inst, int track);
    float  random01();

    int    sampleRate_ = 48000;
    float  bpm_        = 120.0f;
    float  swing_      = 0.5f;
    int    grooveSteps_ = 0;
    float  grooveStart_[kMaxGrooveSteps + 1] = {};
    float  grooveGain_[kMaxGrooveSteps]      = {};
    bool   playing_    = false;
    double untilTick_  = 0.0; // samples until the next tick fires
    long   tick_       = -1;  // ticks fired since start
    uint32_t rng_      = 0x9E3779B9u;

    int step_[kTracks]; // step fired most recently, per track
    int loops_[kTracks] = {};

    Pattern queuedPat_[kTracks];
    bool    queued_[kTracks]     = {};
    int     queuedSlot_[kTracks] = {};

    Column cols_[kTracks][SeqStep::kMaxNotes];
    int    nextKey_[kTracks] = {};

    std::vector<float> trackL_, trackR_, sendBuf_;
    float duckHold_ = 0.0f, duckEnv_ = 0.0f; // sidechain envelope: hold jumps on a kick, env follows it
};
