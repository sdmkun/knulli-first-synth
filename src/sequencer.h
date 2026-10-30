#pragma once

#include <cstdint>
#include <vector>

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

// Multi-track step sequencer driven from the audio thread so steps land
// sample-accurately. Each track owns a pattern (with its own length, so tracks
// can run as a polymeter) and plays one Synth. Everything except process() is
// for the UI thread and must be called with the audio device locked.
class Sequencer {
public:
    static constexpr int kTracks    = 4;
    static constexpr int kKeyBase   = 100; // synth keys for sequenced notes; pads use 0..7
    static constexpr int kMaxActive = 16;  // sounding sequenced notes per track

    // Where a recorded note was placed, so its length can be set on release.
    struct RecHandle {
        int    track = -1;
        int    step  = -1;
        int    slot  = -1;
        int    pitch = -1;
        double start = 0.0; // quantized start, in absolute steps
    };

    void init(int sampleRate);

    // Audio thread: renders and mixes all track synths (plus metronome) into
    // `out`, firing steps on time. `synths` holds kTracks synths.
    void process(Synth* synths, float* out, int frames);

    void start();
    void stop(Synth* synths);
    bool playing() const { return playing_; }
    int  currentStep(int track) const { return step_[track]; }

    void  setBpm(float bpm);
    float bpm() const { return bpm_; }
    void  setLength(int track, int steps);

    // Muted tracks stop firing notes (live pads still sound). A solo track
    // mutes all the others.
    bool audible(int track) const { return solo >= 0 ? track == solo : !mute[track]; }

    // Records `pitch` on `track` at the step nearest to what is heard right now.
    RecHandle record(int track, int pitch);
    void      finishRecord(const RecHandle& h);

    // Undo restores a track's pattern from before its last overdub pass (or clear).
    bool canUndo(int track) const { return undo_[track].has; }
    void undo(int track);
    void clearPattern(int track);

    Pattern patterns[kTracks];
    bool    mute[kTracks]  = {};
    int     solo           = -1;
    bool    recording      = false;
    bool    metronome      = true; // clicks while recording
    int     latencyFrames  = 0;    // output buffer length, used to line up recorded notes

private:
    struct Active {
        int key       = -1;
        int remaining = 0; // steps until note-off
    };
    struct Undo {
        Pattern pattern;
        bool    has  = false;
        int     pass = -1;
    };

    double samplesPerStep() const;
    double heardPosition() const; // absolute steps, latency compensated
    void   fireStep(Synth* synths);
    void   releaseTrack(Synth& synth, int track);
    void   snapshotForUndo(int track);
    void   clearFresh(int track);
    void   mixClick(float* out, int frames);

    int    sampleRate_ = 48000;
    float  bpm_        = 120.0f;
    bool   playing_    = false;
    long   absStep_    = -1;  // steps fired since start
    double untilStep_  = 0.0; // samples until the next step fires

    int step_[kTracks];        // step fired most recently, per track
    int passId_[kTracks] = {}; // bumps each time a track's pattern wraps

    Active active_[kTracks][kMaxActive];
    Undo   undo_[kTracks];

    std::vector<float> trackBuf_;

    int   clickPos_    = -1; // samples into the current click, -1 = silent
    bool  clickAccent_ = false;
    float clickPhase_  = 0.0f;
};
