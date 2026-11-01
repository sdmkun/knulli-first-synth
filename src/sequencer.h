#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

struct SeqStep {
    static constexpr int kMaxNotes = 8; // columns; a drum step can hold the whole kit
    SeqNote notes[kMaxNotes];
    bool    empty() const;               // no notes and no OFFs
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
    static constexpr int kKeys    = 64;  // keys rotate so a released note can ring out under the next one

    void init(int sampleRate);

    // Audio thread: renders and mixes every track's instrument into `out`,
    // firing steps on time. `inst` holds kTracks instruments.
    void process(Instrument* const* inst, float* out, int frames);

    void start();
    void stop(Instrument* const* inst);
    bool playing() const { return playing_; }
    int  currentStep(int track) const { return step_[track]; }

    void  setBpm(float bpm);
    float bpm() const { return bpm_; }
    void  setLength(int track, int steps);

    // Muted tracks release their notes and stop firing. A solo track mutes all the others.
    bool audible(int track) const { return solo >= 0 ? track == solo : !mute[track]; }

    Pattern patterns[kTracks];
    bool    mute[kTracks] = {};
    int     solo          = -1;

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
    int    newKey(int track);
    void   releaseTrack(Instrument& inst, int track);
    float  random01();

    int    sampleRate_ = 48000;
    float  bpm_        = 120.0f;
    bool   playing_    = false;
    double untilTick_  = 0.0; // samples until the next tick fires
    long   tick_       = -1;  // ticks fired since start
    uint32_t rng_      = 0x9E3779B9u;

    int step_[kTracks]; // step fired most recently, per track

    Column cols_[kTracks][SeqStep::kMaxNotes];
    int    nextKey_[kTracks] = {};

    std::vector<float> trackBuf_;
};
