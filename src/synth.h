#pragma once

#include <cstdint>
#include <vector>

#include "instrument.h"

// Oscillator shapes: classic band-limited waves, then the wavetables (wavetable.h).
enum class Wave { Sine, Triangle, Saw, Square, Noise, WtMorph, WtPulse, WtSync, WtVowel, WtFold, WtHarm, WtDigi, Count };
enum class FilterType { LowPass, HighPass, BandPass, Off, Count };
enum class LfoShape { Sine, Triangle, Saw, Square, SampleHold, Count };
// What an envelope or LFO modulates.
enum class ModDest { Off, Pitch, Cutoff, Reso, Pos, Amp, Count };

const char* waveName(int w);
const char* filterName(int f);
const char* lfoShapeName(int s);
const char* modDestName(int d);
bool        isWavetable(int w);

// All values are normalized 0..1 unless noted; Synth maps them to real units.
// Enums are stored as ints so the UI can treat every parameter alike.
struct Params {
    // Oscillator
    int   wave      = int(Wave::Saw);
    float pos       = 0.0f; // wavetable position, or pulse width for SQUARE
    int   transpose = 0;    // semitones, -24..+24
    float detune    = 0.0f; // second oscillator, 0..30 cents
    float sub       = 0.0f; // square one octave down
    float noise     = 0.0f;
    // Filter and amp
    int   filter    = int(FilterType::LowPass);
    float cutoff    = 0.7f;
    float resonance = 0.2f;
    float drive     = 0.0f; // saturation before the filter
    float volume    = 0.7f;
    // Delay
    float delayTime     = 0.6f;
    float delayFeedback = 0.4f;
    float delayMix      = 0.0f;
    // Amp envelope
    float ampA = 0.05f, ampD = 0.4f, ampS = 1.0f, ampR = 0.3f;
    // Mod envelope; amounts are -1..1
    float envA = 0.0f, envD = 0.3f, envS = 0.0f, envR = 0.3f;
    int   envDest = int(ModDest::Off);
    float envAmt  = 0.0f;
    // LFOs
    int   lfo1Shape = int(LfoShape::Sine), lfo1Dest = int(ModDest::Off), lfo1Trig = 0; // trig: 0 free, 1 restarts per note
    float lfo1Rate  = 0.4f, lfo1Amt = 0.0f;
    int   lfo2Shape = int(LfoShape::Triangle), lfo2Dest = int(ModDest::Off), lfo2Trig = 0;
    float lfo2Rate  = 0.2f, lfo2Amt = 0.0f;
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
float decaySec(float n);
float releaseSec(float n);
float detuneCents(float n);
float lfoHz(float n);
float delaySec(float n);
float delayFeedbackGain(float n);
float pulseWidth(float pos);
// Full-scale modulation per destination.
constexpr float kModPitchSemis  = 12.0f;
constexpr float kModCutoffOct   = 6.0f;

// Subtractive / wavetable polysynth: one oscillator (plus a detuned copy, a sub
// and noise), drive, a state-variable filter, ADSR amp envelope, a mod envelope,
// two LFOs and a feedback delay.
class Synth : public Instrument {
public:
    static constexpr int kVoices = 8;
    static constexpr int kBlock  = 16; // control-rate interval for envelopes/LFO/filter coefficients

    void init(int sampleRate);

    void noteOn(int key, int note, float gain) override;
    void noteOff(int key) override;
    void setPitch(int key, int note) override;
    void kill(int key) override;
    void allOff() override;
    void render(float* out, int frames) override;
    int  activeVoices() const override;

    Params params;

private:
    enum Stage { Attack, Decay, Release, Idle };
    struct Voice {
        int   key    = -1;
        float note   = 60.0f;
        float gain   = 1.0f; // velocity
        bool  gate   = false;
        bool  killed = false; // released by KIL: fades in a few ms instead of RELEASE
        float phase1 = 0.0f, phase2 = 0.0f, phaseSub = 0.0f;
        Stage ampStage = Idle;
        float amp      = 0.0f;
        Stage envStage = Idle;
        float env      = 0.0f;
        float lfoPhase[2] = {};
        float lfoHold[2]  = {}; // sample & hold value
        float ic1 = 0.0f, ic2 = 0.0f; // SVF integrator states
    };

    float classic(int wave, float phase, float dt, float pw);
    float noise();
    float lfoValue(Voice& v, int i, int shape, float inc);

    Voice    voices_[kVoices];
    float    sampleRate_ = 48000.0f;
    uint32_t rng_        = 0x12345678u;
    float    freePhase_[2] = {}; // free-running LFO phases new notes pick up

    std::vector<float> delayBuf_;
    int   delayWrite_  = 0;
    float delaySmooth_ = -1.0f; // current delay length in samples, glides toward the target
    int   silentRun_   = 0;     // consecutive silent samples written to the delay line
    bool  delayInUse_  = false;
};
