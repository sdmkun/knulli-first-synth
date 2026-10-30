#pragma once

#include <cstdint>
#include <vector>

#include "instrument.h"

// Oscillator shapes selectable from the UI.
enum class Wave { Sine, Triangle, Saw, Square, Pulse25, Noise, Count };

// What the global LFO modulates.
enum class LfoDest { Pitch, Cutoff, Amp, Count };

const char* waveName(Wave w);
const char* lfoDestName(LfoDest d);

// All values are normalized 0..1 except the enums; Synth maps them to real units.
// Fields after `volume` have defaults so presets can omit them.
struct Params {
    Wave    wave       = Wave::Saw;
    float   cutoff     = 0.7f;
    float   resonance  = 0.2f;
    float   attack     = 0.1f;
    float   release    = 0.3f;
    float   detune     = 0.0f;
    float   volume     = 0.7f;
    float   fenvAmount = 0.0f;
    float   fenvDecay  = 0.3f;
    float   lfoRate    = 0.4f;
    float   lfoDepth   = 0.0f;
    LfoDest lfoDest    = LfoDest::Pitch;
    float   delayTime  = 0.6f;
    float   delayFeedback = 0.4f;
    float   delayMix   = 0.0f;
};

struct Preset {
    const char* name;
    Params      params;
};

extern const Preset kPresets[];
extern const int    kPresetCount;

// Real-unit helpers (shared by DSP and UI so displayed values match the sound).
float cutoffHz(float n);
float attackSec(float n);
float releaseSec(float n);
float detuneCents(float n);
float fenvOctaves(float n);
float fenvDecaySec(float n);
float lfoHz(float n);
float delaySec(float n);
float delayFeedbackGain(float n);

class Synth : public Instrument {
public:
    static constexpr int kVoices = 8;
    static constexpr int kBlock  = 16; // control-rate interval for envelopes/LFO/filter coefficients

    void init(int sampleRate);

    void noteOn(int key, int note) override;
    void noteOff(int key) override;
    void allOff() override;
    void render(float* out, int frames) override;
    int  activeVoices() const override;

    Params params;

private:
    struct Voice {
        int   key    = -1;
        float freq   = 0.0f;
        float phase1 = 0.0f;
        float phase2 = 0.0f;
        float env    = 0.0f;
        float fenv   = 0.0f; // filter envelope, 1 at note-on, decays to 0
        bool  gate   = false;
        float ic1    = 0.0f; // SVF integrator states
        float ic2    = 0.0f;
    };

    float oscillator(Wave w, float phase, float dt);
    float noise();

    Voice    voices_[kVoices];
    float    sampleRate_ = 48000.0f;
    uint32_t rng_        = 0x12345678u;
    float    lfoPhase_   = 0.0f;

    std::vector<float> delayBuf_;
    int      delayWrite_ = 0;
    float    delaySmooth_ = -1.0f; // current delay length in samples, glides toward the target
    int      silentRun_   = 0;     // consecutive silent samples written to the delay line
    bool     delayInUse_  = false;
};
