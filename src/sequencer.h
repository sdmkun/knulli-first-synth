#pragma once

#include <cstdint>

class Synth;

// One note on a step. Lengths are in steps; the note is released when they run out.
struct SeqNote {
    int8_t  pitch = -1; // MIDI note, -1 = empty slot
    uint8_t len   = 1;
    bool    fresh = false; // recorded just ahead of the playhead: already heard live, skip once
};

struct SeqStep {
    static constexpr int kMaxNotes = 4;
    SeqNote notes[kMaxNotes];
    bool    empty() const;
};

struct Pattern {
    static constexpr int kMaxSteps = 64;
    int     length = 16; // in 1/16 steps
    SeqStep steps[kMaxSteps];
    void    clear();
};

// Step sequencer driven from the audio thread so steps land sample-accurately.
// Everything except process() is for the UI thread and must be called with the
// audio device locked.
class Sequencer {
public:
    static constexpr int kKeyBase   = 100; // synth keys for sequenced notes; pads use 0..7
    static constexpr int kMaxActive = 16;

    // Where a recorded note was placed, so its length can be set on release.
    struct RecHandle {
        int    step  = -1;
        int    slot  = -1;
        int    pitch = -1;
        double start = 0.0; // quantized start, in absolute steps
    };

    void init(int sampleRate);

    // Audio thread: renders the synth (plus metronome) and fires steps on time.
    void process(Synth& synth, float* out, int frames);

    void start();
    void stop(Synth& synth);
    bool playing() const { return playing_; }
    int  currentStep() const { return step_; }

    void  setBpm(float bpm);
    float bpm() const { return bpm_; }
    void  setLength(int steps);

    // Records `pitch` at the step nearest to what is being heard right now.
    RecHandle record(int pitch);
    void      finishRecord(const RecHandle& h);

    // Undo restores the pattern from before the last overdub pass (or clear).
    bool canUndo() const { return hasUndo_; }
    void undo();
    void clearPattern();

    Pattern pattern;
    bool    recording     = false;
    bool    metronome     = true; // clicks while recording
    int     latencyFrames = 0;    // output buffer length, used to line up recorded notes

private:
    struct Active {
        int key       = -1;
        int remaining = 0; // steps until note-off
    };

    double samplesPerStep() const;
    double heardPosition() const; // absolute steps, latency compensated
    void   fireStep(Synth& synth);
    void   releaseAll(Synth& synth);
    void   snapshotForUndo();
    void   clearFresh();
    void   mixClick(float* out, int frames);

    int    sampleRate_ = 48000;
    float  bpm_        = 120.0f;
    bool   playing_    = false;
    int    step_       = -1;  // step fired most recently
    long   absStep_    = -1;  // steps fired since start
    double untilStep_  = 0.0; // samples until the next step fires
    int    passId_     = 0;   // bumps each time the pattern wraps

    Active active_[kMaxActive];

    Pattern undo_;
    bool    hasUndo_  = false;
    int     undoPass_ = -1;

    int   clickPos_    = -1; // samples into the current click, -1 = silent
    bool  clickAccent_ = false;
    float clickPhase_  = 0.0f;
};
