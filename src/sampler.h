#pragma once

#include <string>

#include "drumkit.h"
#include "instrument.h"

// A one-sample instrument: the sample is played at the pitch of each note
// (ROOT is the note it was recorded at), polyphonically, through an
// attack / release envelope and a low-pass filter. For stabs and one-shot
// synth samples, and chords made of them.
class Sampler : public Instrument {
public:
    static constexpr int kVoices = 16;

    struct Settings {
        int   root    = 60;   // MIDI note the sample plays at its own speed
        int   fine    = 0;    // cents, -50..50
        float start   = 0.0f; // 0..1 of the sample
        float attack  = 0.0f; // 0..1 (see attackSec)
        float release = 0.3f; // 0..1 (see releaseSec)
        bool  gate    = true; // true: a note's end releases it; false: it plays to the end of the sample
        float cutoff  = 1.0f; // 0..1, 1 = open
        float reso    = 0.1f;
        float volume  = 0.8f;
    };

    void init(int sampleRate) { sampleRate_ = float(sampleRate); }

    // The sample is owned elsewhere and must outlive this (or be cleared with nullptr first).
    void          setSample(const Sample* s);
    const Sample* sample() const { return sample_; }

    void noteOn(int key, int note, float gain) override;
    void noteOff(int key) override;
    void setPitch(int key, int note) override;
    void kill(int key) override;
    void allOff() override;
    void render(float* left, float* right, int frames) override;
    int  activeVoices() const override;

    Settings settings;

private:
    struct Voice {
        int    key   = -1;
        bool   on    = false;
        bool   gate  = false;
        bool   fast  = false; // KIL: fades in a few ms
        int    note  = 60;
        double pos   = 0.0, inc = 1.0;
        float  gain  = 1.0f;
        float  env   = 0.0f;
        bool   rising = true;
        float  l1 = 0, l2 = 0, r1 = 0, r2 = 0; // filter state per side
        unsigned age = 0;
    };
    double rate(int note) const;

    const Sample* sample_     = nullptr;
    float         sampleRate_ = 48000.0f;
    Voice         voices_[kVoices];
    unsigned      counter_    = 0;
};

// The note a sample was recorded at, from its file name ("Stab_F#3.wav",
// "chord Am.wav"); -1 when the name does not say. A note without an octave is put in octave 4.
int rootFromName(const std::string& path);
