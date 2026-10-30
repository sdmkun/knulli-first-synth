#include "synth.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979f;

// PolyBLEP residual to band-limit the discontinuities of saw/square waves.
float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

float wrap01(float x) { return x - std::floor(x); }
} // namespace

const char* waveName(Wave w) {
    switch (w) {
    case Wave::Sine:     return "SINE";
    case Wave::Triangle: return "TRIANGLE";
    case Wave::Saw:      return "SAW";
    case Wave::Square:   return "SQUARE";
    case Wave::Pulse25:  return "PULSE 25%";
    case Wave::Noise:    return "NOISE";
    default:             return "?";
    }
}

const char* lfoDestName(LfoDest d) {
    switch (d) {
    case LfoDest::Pitch:  return "PITCH";
    case LfoDest::Cutoff: return "CUTOFF";
    case LfoDest::Amp:    return "AMP";
    default:              return "?";
    }
}

// Columns: wave, cutoff, reso, attack, release, detune, volume,
//          fenv amount, fenv decay, lfo rate, lfo depth, lfo dest,
//          delay time, delay feedback, delay mix (omitted columns use Params defaults)
const Preset kPresets[] = {
    {"BRIGHT SAW",   {Wave::Saw,      0.85f, 0.20f, 0.05f, 0.30f, 0.30f, 0.70f}},
    {"SOFT SINE",    {Wave::Sine,     1.00f, 0.00f, 0.25f, 0.55f, 0.00f, 0.80f}},
    {"ACID SQUARE",  {Wave::Square,   0.22f, 0.88f, 0.00f, 0.25f, 0.00f, 0.65f,
                      0.55f, 0.35f}},
    {"DUB PLUCK",    {Wave::Saw,      0.30f, 0.60f, 0.00f, 0.35f, 0.00f, 0.70f,
                      0.60f, 0.25f, 0.40f, 0.00f, LfoDest::Pitch,
                      0.72f, 0.60f, 0.45f}},
    {"VIBRATO LEAD", {Wave::Pulse25,  0.70f, 0.30f, 0.20f, 0.35f, 0.10f, 0.70f,
                      0.00f, 0.30f, 0.75f, 0.30f, LfoDest::Pitch,
                      0.55f, 0.30f, 0.20f}},
    {"HOLLOW PULSE", {Wave::Pulse25,  0.60f, 0.40f, 0.10f, 0.45f, 0.15f, 0.70f}},
    {"WARM PAD",     {Wave::Saw,      0.48f, 0.25f, 0.80f, 0.85f, 0.65f, 0.65f,
                      0.00f, 0.30f, 0.25f, 0.35f, LfoDest::Cutoff,
                      0.80f, 0.40f, 0.30f}},
    {"WOOD FLUTE",   {Wave::Triangle, 0.70f, 0.10f, 0.40f, 0.35f, 0.05f, 0.85f}},
    {"NOISE WIND",   {Wave::Noise,    0.35f, 0.92f, 0.60f, 0.80f, 0.00f, 0.60f,
                      0.00f, 0.30f, 0.30f, 0.50f, LfoDest::Cutoff}},
};
const int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

float cutoffHz(float n)          { return 40.0f * std::pow(450.0f, n); }   // 40 Hz .. 18 kHz
float attackSec(float n)         { return 0.001f * std::pow(3000.0f, n); } // 1 ms .. 3 s
float releaseSec(float n)        { return 0.01f * std::pow(500.0f, n); }   // 10 ms .. 5 s
float detuneCents(float n)       { return n * 30.0f; }
float fenvOctaves(float n)       { return n * 6.0f; }
float fenvDecaySec(float n)      { return 0.01f * std::pow(300.0f, n); }   // 10 ms .. 3 s
float lfoHz(float n)             { return 0.05f * std::pow(400.0f, n); }   // 0.05 .. 20 Hz
float delaySec(float n)          { return 0.01f * std::pow(100.0f, n); }   // 10 ms .. 1 s
float delayFeedbackGain(float n) { return n * 0.9f; }

void Synth::init(int sampleRate) {
    sampleRate_ = static_cast<float>(sampleRate);
    for (Voice& v : voices_) v = Voice{};
    delayBuf_.assign(static_cast<size_t>(sampleRate_ * delaySec(1.0f)) + 4, 0.0f);
    delayWrite_  = 0;
    delaySmooth_ = -1.0f;
    silentRun_   = 0;
}

void Synth::noteOn(int key, float freq) {
    Voice* target = nullptr;
    for (Voice& v : voices_)
        if (v.key == key) target = &v;
    if (!target)
        for (Voice& v : voices_)
            if (!v.gate && v.env <= 0.0f) { target = &v; break; }
    if (!target) {
        // Steal the quietest voice, preferring ones already released.
        float best = 1e9f;
        for (Voice& v : voices_) {
            float score = v.env + (v.gate ? 1.0f : 0.0f);
            if (score < best) { best = score; target = &v; }
        }
    }
    target->key  = key;
    target->freq = freq;
    target->gate = true;
    target->fenv = 1.0f;
}

void Synth::noteOff(int key) {
    for (Voice& v : voices_)
        if (v.key == key) v.gate = false;
}

void Synth::allOff() {
    for (Voice& v : voices_) v.gate = false;
}

int Synth::activeVoices() const {
    int n = 0;
    for (const Voice& v : voices_)
        if (v.gate || v.env > 0.0f) ++n;
    return n;
}

float Synth::noise() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_) * (2.0f / 4294967295.0f) - 1.0f;
}

float Synth::oscillator(Wave w, float t, float dt) {
    switch (w) {
    case Wave::Sine:
        return std::sin(2.0f * kPi * t);
    case Wave::Triangle:
        return 1.0f - 4.0f * std::fabs(t - 0.5f);
    case Wave::Saw:
        return 2.0f * t - 1.0f - polyBlep(t, dt);
    case Wave::Square:
        return (t < 0.5f ? 1.0f : -1.0f) + polyBlep(t, dt) - polyBlep(wrap01(t + 0.5f), dt);
    case Wave::Pulse25:
        return (t < 0.25f ? 1.0f : -1.0f) + polyBlep(t, dt) - polyBlep(wrap01(t + 0.75f), dt);
    case Wave::Noise:
        return noise();
    default:
        return 0.0f;
    }
}

void Synth::render(float* out, int frames) {
    const Params p  = params;
    const float  sr = sampleRate_;

    const float k          = std::max(2.0f - 2.0f * p.resonance, 0.05f);
    const float baseCutoff = cutoffHz(p.cutoff);
    const float attackStep = 1.0f / (attackSec(p.attack) * sr);
    // Exponential decays reaching -60 dB after the given time.
    const float releaseMul   = std::exp(std::log(0.001f) / (releaseSec(p.release) * sr));
    const float fenvBlockMul = std::exp(std::log(0.001f) * kBlock / (fenvDecaySec(p.fenvDecay) * sr));
    const float fenvOct      = fenvOctaves(p.fenvAmount);
    const float detuneMul    = std::pow(2.0f, detuneCents(p.detune) / 1200.0f);
    const float gain         = 0.3f * p.volume;
    const float lfoInc       = lfoHz(p.lfoRate) / sr;

    const int   delaySize   = static_cast<int>(delayBuf_.size());
    const float delayTarget = std::min(delaySec(p.delayTime) * sr, float(delaySize - 2));
    const float feedback    = delayFeedbackGain(p.delayFeedback);
    if (delaySmooth_ < 0.0f) delaySmooth_ = delayTarget;

    // Idle: no voices and the delay line has held only silence for a full lap.
    if (silentRun_ >= delaySize && activeVoices() == 0) {
        std::fill(out, out + frames, 0.0f);
        lfoPhase_ = wrap01(lfoPhase_ + lfoInc * frames);
        return;
    }

    for (int start = 0; start < frames; start += kBlock) {
        const int n = std::min(kBlock, frames - start);

        // Global LFO, evaluated once per block.
        const float lfo = std::sin(2.0f * kPi * lfoPhase_);
        lfoPhase_       = wrap01(lfoPhase_ + lfoInc * n);
        float pitchMul = 1.0f, cutoffMul = 1.0f, ampMul = 1.0f;
        switch (p.lfoDest) {
        case LfoDest::Pitch:  pitchMul  = std::pow(2.0f, p.lfoDepth * 2.0f * lfo / 12.0f); break; // +-2 semitones
        case LfoDest::Cutoff: cutoffMul = std::pow(2.0f, p.lfoDepth * 3.0f * lfo); break;         // +-3 octaves
        case LfoDest::Amp:    ampMul    = 1.0f - p.lfoDepth * 0.5f * (1.0f - lfo); break;
        default: break;
        }

        float mix[kBlock] = {};
        for (Voice& v : voices_) {
            if (!v.gate && v.env <= 0.0f) continue;

            // Topology-preserving-transform state variable filter (lowpass output),
            // coefficients refreshed per block so the filter envelope and LFO can move it.
            const float fc = std::min(baseCutoff * cutoffMul * std::exp2(fenvOct * v.fenv), sr * 0.45f);
            const float g  = std::tan(kPi * fc / sr);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;

            const float dt1 = v.freq * pitchMul / sr;
            const float dt2 = dt1 * detuneMul;

            for (int i = 0; i < n; ++i) {
                if (v.gate) {
                    v.env = std::min(v.env + attackStep, 1.0f);
                } else {
                    v.env *= releaseMul;
                    if (v.env < 0.0005f) {
                        v.env = 0.0f;
                        v.key = -1;
                        v.ic1 = v.ic2 = 0.0f;
                        break;
                    }
                }

                float s = oscillator(p.wave, v.phase1, dt1);
                if (p.detune > 0.0f) s = 0.5f * (s + oscillator(p.wave, v.phase2, dt2));
                v.phase1 = wrap01(v.phase1 + dt1);
                v.phase2 = wrap01(v.phase2 + dt2);

                const float v3 = s - v.ic2;
                const float v1 = a1 * v.ic1 + a2 * v3;
                const float v2 = v.ic2 + a2 * v.ic1 + a3 * v3;
                v.ic1 = 2.0f * v1 - v.ic1;
                v.ic2 = 2.0f * v2 - v.ic2;

                mix[i] += v2 * v.env;
            }
            v.fenv *= fenvBlockMul;
        }

        for (int i = 0; i < n; ++i) {
            const float dry = mix[i] * gain * ampMul;

            // Feedback delay; the length glides so turning DELAY TIME bends pitch like tape.
            delaySmooth_ += (delayTarget - delaySmooth_) * 0.0005f;
            float rp = float(delayWrite_) - delaySmooth_;
            if (rp < 0.0f) rp += float(delaySize);
            const int   i0   = static_cast<int>(rp);
            const int   i1   = (i0 + 1) % delaySize;
            const float frac = rp - float(i0);
            const float wet  = delayBuf_[i0] + (delayBuf_[i1] - delayBuf_[i0]) * frac;
            delayBuf_[delayWrite_] = dry + wet * feedback;
            silentRun_ = std::fabs(delayBuf_[delayWrite_]) > 1e-5f ? 0 : silentRun_ + 1;
            delayWrite_ = (delayWrite_ + 1) % delaySize;

            const float y = std::tanh(dry + wet * p.delayMix); // gentle limiter for chords / resonance
            out[start + i] = y;
        }
    }
}
