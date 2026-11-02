#include "synth.h"

#include <algorithm>
#include <cmath>

#include "wavetable.h"

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

// Phases only ever move forward by less than one cycle, so a compare beats floor().
float wrap01(float x) { return x >= 1.0f ? x - 1.0f : x; }

// Sine from a table with linear interpolation; t in [0, 1).
constexpr int kSineSize = 1024;
struct SineTable {
    float v[kSineSize + 1];
    SineTable() {
        for (int i = 0; i <= kSineSize; ++i) v[i] = std::sin(2.0f * kPi * float(i) / kSineSize);
    }
};
const SineTable kSine;

float fastSin(float t) {
    float x    = t * kSineSize;
    int   i    = static_cast<int>(x);
    float frac = x - float(i);
    return kSine.v[i] + (kSine.v[i + 1] - kSine.v[i]) * frac;
}

// Rational tanh approximation, exact enough for a soft limiter.
float softClip(float x) {
    x = std::clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

float midiHz(float note) { return 440.0f * std::exp2((note - 69.0f) / 12.0f); }

// Multiplier that takes an exponential decay to -60 dB in `sec`, applied every `step` samples.
float decayMul(float sec, float sr, int step) { return std::exp(std::log(0.001f) * step / (sec * sr)); }

// ---- presets

Params brightSaw() {
    Params p;
    p.cutoff = 0.85f, p.resonance = 0.2f, p.ampA = 0.05f, p.ampR = 0.3f, p.detune = 0.3f;
    p.width = 0.6f, p.chorus = 0.2f;
    return p;
}
Params softSine() {
    Params p;
    p.wave = int(Wave::Sine), p.cutoff = 1.0f, p.resonance = 0.0f, p.ampA = 0.25f, p.ampR = 0.55f, p.volume = 0.8f;
    return p;
}
Params acidSquare() {
    Params p;
    p.wave = int(Wave::Square), p.cutoff = 0.22f, p.resonance = 0.88f, p.drive = 0.3f, p.ampA = 0.0f, p.ampR = 0.25f,
    p.volume = 0.65f;
    p.envDest = int(ModDest::Cutoff), p.envAmt = 0.55f, p.envD = 0.39f;
    return p;
}
Params dubPluck() {
    Params p;
    p.cutoff = 0.3f, p.resonance = 0.6f, p.ampA = 0.0f, p.ampR = 0.35f;
    p.envDest = int(ModDest::Cutoff), p.envAmt = 0.6f, p.envD = 0.31f;
    p.delayTime = 0.72f, p.delayFeedback = 0.6f, p.delayMix = 0.45f;
    return p;
}
Params vibratoLead() {
    Params p;
    p.wave = int(Wave::Square), p.pos = 0.556f, p.cutoff = 0.7f, p.resonance = 0.3f, p.ampA = 0.2f, p.ampR = 0.35f,
    p.detune = 0.1f;
    p.lfo1Dest = int(ModDest::Pitch), p.lfo1Rate = 0.75f, p.lfo1Amt = 0.05f;
    p.delayTime = 0.55f, p.delayFeedback = 0.3f, p.delayMix = 0.2f;
    return p;
}
Params hollowPulse() {
    Params p;
    p.wave = int(Wave::Square), p.pos = 0.556f, p.cutoff = 0.6f, p.resonance = 0.4f, p.ampA = 0.1f, p.ampR = 0.45f,
    p.detune = 0.15f;
    return p;
}
Params warmPad() {
    Params p;
    p.cutoff = 0.48f, p.resonance = 0.25f, p.ampA = 0.8f, p.ampR = 0.85f, p.detune = 0.65f, p.volume = 0.65f;
    p.unison = 4, p.spread = 0.35f, p.width = 0.8f, p.chorus = 0.3f;
    p.lfo1Dest = int(ModDest::Cutoff), p.lfo1Rate = 0.25f, p.lfo1Amt = 0.18f;
    p.delayTime = 0.8f, p.delayFeedback = 0.4f, p.delayMix = 0.3f;
    return p;
}
Params woodFlute() {
    Params p;
    p.wave = int(Wave::Triangle), p.cutoff = 0.7f, p.resonance = 0.1f, p.ampA = 0.4f, p.ampR = 0.35f, p.detune = 0.05f,
    p.noise = 0.08f, p.volume = 0.85f;
    return p;
}
Params noiseWind() {
    Params p;
    p.wave = int(Wave::Noise), p.filter = int(FilterType::BandPass), p.cutoff = 0.45f, p.resonance = 0.85f,
    p.ampA = 0.6f, p.ampR = 0.8f, p.volume = 0.6f;
    p.lfo1Dest = int(ModDest::Cutoff), p.lfo1Rate = 0.3f, p.lfo1Amt = 0.25f;
    return p;
}
Params vowelPad() {
    Params p;
    p.wave = int(Wave::WtVowel), p.cutoff = 0.8f, p.resonance = 0.1f, p.ampA = 0.6f, p.ampR = 0.8f, p.detune = 0.4f;
    p.lfo2Dest = int(ModDest::Pos), p.lfo2Rate = 0.25f, p.lfo2Amt = 0.5f;
    p.delayMix = 0.25f, p.width = 0.6f, p.chorus = 0.3f;
    return p;
}
Params syncLead() {
    Params p;
    p.wave = int(Wave::WtSync), p.pos = 0.15f, p.cutoff = 0.9f, p.ampA = 0.02f, p.ampR = 0.3f, p.detune = 0.2f;
    p.envDest = int(ModDest::Pos), p.envAmt = 0.7f, p.envD = 0.45f, p.envS = 0.2f;
    p.lfo1Dest = int(ModDest::Pitch), p.lfo1Rate = 0.7f, p.lfo1Amt = 0.03f;
    return p;
}
Params foldBass() {
    Params p;
    p.wave = int(Wave::WtFold), p.pos = 0.25f, p.sub = 0.5f, p.cutoff = 0.55f, p.resonance = 0.3f;
    p.ampA = 0.0f, p.ampD = 0.5f, p.ampS = 0.6f, p.ampR = 0.2f;
    p.envDest = int(ModDest::Pos), p.envAmt = 0.5f, p.envD = 0.35f;
    return p;
}
Params digiKeys() {
    Params p;
    p.wave = int(Wave::WtDigi), p.pos = 0.5f, p.cutoff = 0.8f, p.ampA = 0.0f, p.ampD = 0.55f, p.ampS = 0.3f, p.ampR = 0.4f;
    p.delayTime = 0.5f, p.delayFeedback = 0.35f, p.delayMix = 0.25f;
    return p;
}
Params drawbarOrgan() {
    Params p;
    p.wave = int(Wave::WtHarm), p.pos = 0.6f, p.cutoff = 1.0f, p.resonance = 0.0f, p.ampA = 0.02f, p.ampR = 0.15f;
    p.lfo1Dest = int(ModDest::Amp), p.lfo1Rate = 0.65f, p.lfo1Amt = 0.15f;
    return p;
}
Params morphPluck() {
    Params p;
    p.wave = int(Wave::WtMorph), p.pos = 0.7f, p.cutoff = 0.4f, p.resonance = 0.35f;
    p.ampA = 0.0f, p.ampD = 0.5f, p.ampS = 0.0f, p.ampR = 0.4f;
    p.envDest = int(ModDest::Cutoff), p.envAmt = 0.5f, p.envD = 0.3f;
    return p;
}
// Wide, thick sounds for electro and big-room leads and chords.
Params superSaw() {
    Params p;
    p.unison = 6, p.spread = 0.45f, p.width = 0.9f, p.chorus = 0.15f;
    p.cutoff = 0.9f, p.resonance = 0.1f, p.ampA = 0.01f, p.ampR = 0.35f, p.volume = 0.6f;
    p.delayTime = 0.55f, p.delayFeedback = 0.3f, p.delayMix = 0.15f;
    return p;
}
Params sawStab() {
    Params p;
    p.unison = 4, p.spread = 0.3f, p.width = 0.7f;
    p.cutoff = 0.35f, p.resonance = 0.3f, p.ampA = 0.0f, p.ampD = 0.35f, p.ampS = 0.0f, p.ampR = 0.25f;
    p.envDest = int(ModDest::Cutoff), p.envAmt = 0.6f, p.envD = 0.3f;
    return p;
}
Params reeseBass() {
    Params p;
    p.unison = 1, p.spread = 0.25f, p.width = 0.2f, p.sub = 0.3f, p.drive = 0.3f;
    p.cutoff = 0.35f, p.resonance = 0.25f, p.ampA = 0.0f, p.ampR = 0.15f;
    p.lfo1Dest = int(ModDest::Cutoff), p.lfo1Rate = 0.15f, p.lfo1Amt = 0.15f;
    return p;
}
Params widePluck() {
    Params p;
    p.unison = 2, p.spread = 0.2f, p.width = 0.8f, p.chorus = 0.2f;
    p.cutoff = 0.3f, p.resonance = 0.3f, p.ampA = 0.0f, p.ampD = 0.45f, p.ampS = 0.0f, p.ampR = 0.35f;
    p.envDest = int(ModDest::Cutoff), p.envAmt = 0.55f, p.envD = 0.28f;
    p.delayTime = 0.5f, p.delayFeedback = 0.35f, p.delayMix = 0.25f;
    return p;
}
Params sawPad() {
    Params p;
    p.unison = 6, p.spread = 0.3f, p.width = 1.0f, p.chorus = 0.35f, p.volume = 0.6f;
    p.cutoff = 0.5f, p.resonance = 0.15f, p.ampA = 0.7f, p.ampR = 0.8f;
    p.lfo1Dest = int(ModDest::Cutoff), p.lfo1Rate = 0.2f, p.lfo1Amt = 0.1f;
    return p;
}
Params pulseLead() {
    Params p;
    p.wave = int(Wave::Square), p.pos = 0.3f, p.unison = 2, p.spread = 0.15f, p.width = 0.5f;
    p.cutoff = 0.8f, p.resonance = 0.15f, p.ampA = 0.01f, p.ampR = 0.25f;
    p.lfo1Dest = int(ModDest::Pitch), p.lfo1Rate = 0.72f, p.lfo1Amt = 0.02f;
    p.delayTime = 0.5f, p.delayFeedback = 0.3f, p.delayMix = 0.2f;
    return p;
}
} // namespace

const Preset kPresets[] = {
    {"BRIGHT SAW", brightSaw()},     {"SOFT SINE", softSine()},     {"ACID SQUARE", acidSquare()},
    {"DUB PLUCK", dubPluck()},       {"VIBRATO LEAD", vibratoLead()}, {"HOLLOW PULSE", hollowPulse()},
    {"WARM PAD", warmPad()},         {"WOOD FLUTE", woodFlute()},   {"NOISE WIND", noiseWind()},
    {"VOWEL PAD", vowelPad()},       {"SYNC LEAD", syncLead()},     {"FOLD BASS", foldBass()},
    {"DIGI KEYS", digiKeys()},       {"DRAWBAR ORGAN", drawbarOrgan()}, {"MORPH PLUCK", morphPluck()},
    {"SUPERSAW", superSaw()},        {"SAW STAB", sawStab()},       {"REESE BASS", reeseBass()},
    {"WIDE PLUCK", widePluck()},     {"SAW PAD", sawPad()},         {"PULSE LEAD", pulseLead()},
};
const int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

const char* waveName(int w) {
    static const char* kNames[] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "NOISE", "WT MORPH", "WT PULSE",
                                   "WT SYNC", "WT VOWEL", "WT FOLD", "WT HARM", "WT DIGI"};
    return kNames[std::clamp(w, 0, int(Wave::Count) - 1)];
}
const char* filterName(int f) {
    static const char* kNames[] = {"LOWPASS", "HIGHPASS", "BANDPASS", "OFF"};
    return kNames[std::clamp(f, 0, int(FilterType::Count) - 1)];
}
const char* lfoShapeName(int s) {
    static const char* kNames[] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "S+H"};
    return kNames[std::clamp(s, 0, int(LfoShape::Count) - 1)];
}
const char* modDestName(int d) {
    static const char* kNames[] = {"OFF", "PITCH", "CUTOFF", "RESO", "POS", "AMP"};
    return kNames[std::clamp(d, 0, int(ModDest::Count) - 1)];
}
bool isWavetable(int w) { return w >= int(Wave::WtMorph); }

float cutoffHz(float n)          { return 40.0f * std::pow(450.0f, n); }   // 40 Hz .. 18 kHz
float attackSec(float n)         { return 0.001f * std::pow(3000.0f, n); } // 1 ms .. 3 s
float decaySec(float n)          { return 0.005f * std::pow(1000.0f, n); } // 5 ms .. 5 s
float releaseSec(float n)        { return 0.01f * std::pow(500.0f, n); }   // 10 ms .. 5 s
float detuneCents(float n)       { return n * 30.0f; }
float lfoHz(float n)             { return 0.05f * std::pow(400.0f, n); }   // 0.05 .. 20 Hz
float delaySec(float n)          { return 0.01f * std::pow(100.0f, n); }   // 10 ms .. 1 s
float delayFeedbackGain(float n) { return n * 0.9f; }
float pulseWidth(float pos)      { return 0.5f - 0.45f * pos; }            // 50% .. 5%
float spreadCents(float n)       { return n * 50.0f; }

void Synth::init(int sampleRate) {
    sampleRate_ = static_cast<float>(sampleRate);
    for (Voice& v : voices_) v = Voice{};
    delayBuf_.assign(static_cast<size_t>(sampleRate_ * delaySec(1.0f)) + 4, 0.0f);
    delayWrite_  = 0;
    delaySmooth_ = -1.0f;
    silentRun_   = 0;
    chorusBuf_.assign(static_cast<size_t>(sampleRate_ * 0.03f) + 4, 0.0f);
    chorusWrite_ = 0;
    wt::build();
}

void Synth::noteOn(int key, int note, float gain) {
    Voice* target = nullptr;
    for (Voice& v : voices_)
        if (v.key == key) target = &v;
    if (!target)
        for (Voice& v : voices_)
            if (v.ampStage == Idle) { target = &v; break; }
    if (!target) {
        // Steal the quietest voice, preferring ones already released.
        float best = 1e9f;
        for (Voice& v : voices_) {
            float score = v.amp + (v.gate ? 1.0f : 0.0f);
            if (score < best) { best = score; target = &v; }
        }
    }
    Voice& v   = *target;
    bool   was = v.ampStage != Idle;
    v.key      = key;
    v.note     = float(note);
    v.gain     = gain;
    v.gate     = true;
    v.killed   = false;
    v.ampStage = Attack; // from the current level, so a stolen voice doesn't click
    v.envStage = Attack;
    v.env      = 0.0f;
    const int trig[2] = {params.lfo1Trig, params.lfo2Trig};
    for (int i = 0; i < 2; ++i) v.lfoPhase[i] = trig[i] ? 0.0f : freePhase_[i];
    if (!was) {
        v.ic1 = v.ic2 = v.ir1 = v.ir2 = 0.0f;
        // Unison oscillators start at scattered phases, or they would sound as one at first.
        for (int i = 1; i < kMaxUnison; ++i) v.ph[i] = 0.5f + 0.49f * noise();
    }
}

void Synth::noteOff(int key) {
    for (Voice& v : voices_)
        if (v.key == key) v.gate = false;
}

void Synth::setPitch(int key, int note) {
    for (Voice& v : voices_)
        if (v.key == key) v.note = float(note);
}

void Synth::kill(int key) {
    for (Voice& v : voices_) {
        if (v.key != key) continue;
        v.gate   = false;
        v.killed = true;
    }
}

void Synth::allOff() {
    for (Voice& v : voices_) v.gate = false;
}

int Synth::activeVoices() const {
    int n = 0;
    for (const Voice& v : voices_)
        if (v.ampStage != Idle) ++n;
    return n;
}

float Synth::noise() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_) * (2.0f / 4294967295.0f) - 1.0f;
}

float Synth::classic(int w, float t, float dt, float pw) {
    switch (Wave(w)) {
    case Wave::Sine:
        return fastSin(t);
    case Wave::Triangle:
        return 1.0f - 4.0f * std::fabs(t - 0.5f);
    case Wave::Saw:
        return 2.0f * t - 1.0f - polyBlep(t, dt);
    case Wave::Square: {
        float s = (t < pw ? 1.0f : -1.0f) + polyBlep(t, dt) - polyBlep(wrap01(t + 1.0f - pw), dt);
        return s - (2.0f * pw - 1.0f); // remove the DC a narrow pulse carries
    }
    case Wave::Noise:
        return noise();
    default:
        return 0.0f;
    }
}

// Advances LFO i of a voice by one block and returns its value (-1..1).
float Synth::lfoValue(Voice& v, int i, int shape, float inc) {
    float& ph   = v.lfoPhase[i];
    float  next = ph + inc;
    if (next >= 1.0f) {
        next -= std::floor(next);
        v.lfoHold[i] = noise();
    }
    ph = next;
    switch (LfoShape(shape)) {
    case LfoShape::Sine:       return fastSin(ph);
    case LfoShape::Triangle:   return 1.0f - 4.0f * std::fabs(ph - 0.5f);
    case LfoShape::Saw:        return 1.0f - 2.0f * ph; // falling, like a decaying ramp
    case LfoShape::Square:     return ph < 0.5f ? 1.0f : -1.0f;
    case LfoShape::SampleHold: return v.lfoHold[i];
    default:                   return 0.0f;
    }
}

void Synth::render(float* left, float* right, int frames) {
    const Params p  = params;
    const float  sr = sampleRate_;

    // Amp envelope (per sample) and mod envelope (per block) coefficients.
    const float ampAtk  = 1.0f / (attackSec(p.ampA) * sr);
    const float ampDec  = decayMul(decaySec(p.ampD), sr, 1);
    const float ampRel  = decayMul(releaseSec(p.ampR), sr, 1);
    const float killRel = decayMul(0.005f, sr, 1);
    const float envAtk  = kBlock / (attackSec(p.envA) * sr);
    const float envDec  = decayMul(decaySec(p.envD), sr, kBlock);
    const float envRel  = decayMul(releaseSec(p.envR), sr, kBlock);

    const float baseCutoff = cutoffHz(p.cutoff);
    const float driveGain  = 1.0f + p.drive * 9.0f;
    const float gain       = 0.3f * p.volume;
    const float lfoInc[2]  = {lfoHz(p.lfo1Rate) * kBlock / sr, lfoHz(p.lfo2Rate) * kBlock / sr};
    const int   wave       = std::clamp(p.wave, 0, int(Wave::Count) - 1);
    const bool  table      = isWavetable(wave);
    const int   tableIndex = wave - int(Wave::WtMorph);
    const auto  filter     = FilterType(p.filter);

    // The oscillator stack: its pitch ratios and where each sits in the stereo field.
    // UNISON 1 is the main oscillator plus DETUNE's copy; more spreads a stack across SPREAD.
    const int   uni   = std::clamp(p.unison, 0, kMaxUnison - 1) + 1;
    const float width = std::clamp(p.width, 0.0f, 1.0f);
    int   nOsc = 1;
    float ratio[kMaxUnison] = {1.0f}, gl[kMaxUnison] = {1.0f}, gr[kMaxUnison] = {1.0f};
    auto  place = [&](int i, float pan, float g) { // balance pan law: the centre keeps full level
        gl[i] = g * std::min(1.0f, 1.0f - pan);
        gr[i] = g * std::min(1.0f, 1.0f + pan);
    };
    if (uni == 1) {
        if (p.detune > 0.0f) {
            nOsc     = 2;
            ratio[1] = std::exp2(detuneCents(p.detune) / 1200.0f);
            place(0, -width, 0.5f);
            place(1, width, 0.5f);
        }
    } else {
        nOsc          = uni;
        const float g = 1.3f / std::sqrt(float(uni));
        for (int i = 0; i < uni; ++i) {
            const float off = -1.0f + 2.0f * float(i) / float(uni - 1);
            ratio[i]        = std::exp2(off * spreadCents(p.spread) / 1200.0f);
            place(i, (i % 2 ? -1.0f : 1.0f) * std::fabs(off) * width, g);
        }
    }
    bool stereo = false;
    float maxRatio = 1.0f;
    for (int i = 0; i < nOsc; ++i) {
        stereo   = stereo || gl[i] != gr[i];
        maxRatio = std::max(maxRatio, ratio[i]);
    }

    const int   delaySize   = static_cast<int>(delayBuf_.size());
    const float delayTarget = std::min(delaySec(p.delayTime) * sr, float(delaySize - 2));
    const float feedback    = delayFeedbackGain(p.delayFeedback);
    if (delaySmooth_ < 0.0f) delaySmooth_ = delayTarget;

    // Free-running LFO phases, picked up by new notes in FREE mode.
    for (int i = 0; i < 2; ++i) {
        freePhase_[i] += lfoInc[i] * frames / kBlock;
        freePhase_[i] -= std::floor(freePhase_[i]);
    }

    // With DLY MIX at zero the delay is inaudible, so skip it and empty the line
    // so stale echoes don't come back when it is turned up again.
    const bool useDelay = p.delayMix > 0.0f;
    if (!useDelay) {
        if (delayInUse_) std::fill(delayBuf_.begin(), delayBuf_.end(), 0.0f);
        delayInUse_ = false;
        silentRun_  = delaySize;
    }
    delayInUse_ = useDelay;

    // Idle: no voices and the delay line has held only silence for a full lap.
    if (silentRun_ >= delaySize && activeVoices() == 0) {
        std::fill(left, left + frames, 0.0f);
        std::fill(right, right + frames, 0.0f);
        return;
    }

    // Chorus: two taps around 8 ms swinging +-3 ms in quadrature, one per side.
    const bool  useChorus  = p.chorus > 0.0f;
    const int   chorusSize = static_cast<int>(chorusBuf_.size());
    const float chorusInc  = 0.35f / sr;
    const float chorusBase = 0.008f * sr, chorusDepth = 0.003f * sr;

    for (int start = 0; start < frames; start += kBlock) {
        const int n = std::min(kBlock, frames - start);

        float mixL[kBlock] = {}, mixR[kBlock] = {};
        for (Voice& v : voices_) {
            if (v.ampStage == Idle) continue;

            // ---- control rate: mod envelope, LFOs, destinations
            if (!v.gate && v.envStage != Idle) v.envStage = Release;
            switch (v.envStage) {
            case Attack:
                v.env += envAtk;
                if (v.env >= 1.0f) v.env = 1.0f, v.envStage = Decay;
                break;
            case Decay:
                v.env = p.envS + (v.env - p.envS) * envDec;
                if (std::fabs(v.env - p.envS) < 1e-4f) v.env = p.envS; // settle instead of creeping into denormals
                break;
            case Release:
                v.env *= envRel;
                if (v.env < 1e-4f) v.env = 0.0f, v.envStage = Idle;
                break;
            default: break;
            }
            float mod[int(ModDest::Count)] = {};
            mod[std::clamp(p.envDest, 0, int(ModDest::Count) - 1)] += v.env * p.envAmt;
            mod[std::clamp(p.lfo1Dest, 0, int(ModDest::Count) - 1)] += lfoValue(v, 0, p.lfo1Shape, lfoInc[0]) * p.lfo1Amt;
            mod[std::clamp(p.lfo2Dest, 0, int(ModDest::Count) - 1)] += lfoValue(v, 1, p.lfo2Shape, lfoInc[1]) * p.lfo2Amt;

            const float freq = midiHz(v.note + p.transpose + mod[int(ModDest::Pitch)] * kModPitchSemis);
            const float dt1  = std::min(freq / sr, 0.49f);
            float       dto[kMaxUnison];
            for (int o = 0; o < nOsc; ++o) dto[o] = std::min(dt1 * ratio[o], 0.49f);
            const float dtS  = dt1 * 0.5f;
            const float pos  = std::clamp(p.pos + mod[int(ModDest::Pos)], 0.0f, 1.0f);
            const float pw   = pulseWidth(pos);
            const float ampM = std::clamp(1.0f + mod[int(ModDest::Amp)], 0.0f, 2.0f) * v.gain;

            // Wavetable frames for this block, at the band limit this pitch needs.
            const float* fa = nullptr;
            const float* fb = nullptr;
            float        ff = 0.0f;
            if (table) {
                int   mip = wt::mipFor(freq * maxRatio, sr);
                float fp  = pos * (wt::kFrames - 1);
                int   f0  = std::min(int(fp), wt::kFrames - 2);
                ff        = fp - f0;
                fa        = wt::frame(tableIndex, f0, mip);
                fb        = wt::frame(tableIndex, f0 + 1, mip);
            }
            auto readTable = [&](float ph) {
                float x  = ph * wt::kSize;
                int   i  = int(x);
                float fr = x - i;
                float a  = fa[i] + (fa[i + 1] - fa[i]) * fr;
                float b  = fb[i] + (fb[i + 1] - fb[i]) * fr;
                return a + (b - a) * ff;
            };

            // Topology-preserving-transform state variable filter, coefficients per block.
            const float fc = std::clamp(baseCutoff * std::exp2(mod[int(ModDest::Cutoff)] * kModCutoffOct), 20.0f, sr * 0.45f);
            const float res = std::clamp(p.resonance + mod[int(ModDest::Reso)], 0.0f, 1.0f);
            const float k   = std::max(2.0f - 2.0f * res, 0.05f);
            const float g   = std::tan(kPi * fc / sr);
            const float a1  = 1.0f / (1.0f + g * (g + k));
            const float a2  = g * a1;
            const float a3  = g * a2;
            auto svf = [&](float x, float& ic1, float& ic2) {
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                return filter == FilterType::LowPass ? v2 : filter == FilterType::BandPass ? v1 : x - k * v1 - v2;
            };

            for (int i = 0; i < n; ++i) {
                // Amp envelope.
                if (!v.gate && v.ampStage != Release) v.ampStage = Release;
                if (v.ampStage == Attack) {
                    v.amp += ampAtk;
                    if (v.amp >= 1.0f) v.amp = 1.0f, v.ampStage = Decay;
                } else if (v.ampStage == Decay) {
                    v.amp = p.ampS + (v.amp - p.ampS) * ampDec;
                    if (std::fabs(v.amp - p.ampS) < 1e-5f) v.amp = p.ampS;
                    if (v.amp < 0.0005f) v.ampStage = Release; // decayed to silence: free the voice
                } else {
                    v.amp *= v.killed ? killRel : ampRel;
                    if (v.amp < 0.0005f) {
                        v.amp      = 0.0f;
                        v.ampStage = Idle;
                        v.envStage = Idle;
                        v.key      = -1;
                        v.ic1 = v.ic2 = v.ir1 = v.ir2 = 0.0f;
                        break;
                    }
                }

                // Oscillators.
                float sL = 0.0f, sR = 0.0f;
                for (int o = 0; o < nOsc; ++o) {
                    const float s = table ? readTable(v.ph[o]) : classic(wave, v.ph[o], dto[o], pw);
                    sL += s * gl[o];
                    sR += s * gr[o];
                    v.ph[o] = wrap01(v.ph[o] + dto[o]);
                }
                float centre = 0.0f;
                if (p.sub > 0.0f) {
                    float sq = (v.phaseSub < 0.5f ? 1.0f : -1.0f) + polyBlep(v.phaseSub, dtS) -
                               polyBlep(wrap01(v.phaseSub + 0.5f), dtS);
                    centre += sq * p.sub * 0.7f;
                }
                v.phaseSub = wrap01(v.phaseSub + dtS);
                if (p.noise > 0.0f) centre += noise() * p.noise;
                sL += centre;
                if (p.drive > 0.0f) sL = softClip(sL * driveGain);

                // Filter: one for a mono stack, one per side for a stereo one.
                const float amp = v.amp * ampM;
                float       yL  = filter != FilterType::Off ? svf(sL, v.ic1, v.ic2) : sL;
                mixL[i] += yL * amp;
                if (!stereo) {
                    mixR[i] += yL * amp;
                    continue;
                }
                sR += centre;
                if (p.drive > 0.0f) sR = softClip(sR * driveGain);
                mixR[i] += (filter != FilterType::Off ? svf(sR, v.ir1, v.ir2) : sR) * amp;
            }
        }

        for (int i = 0; i < n; ++i) {
            float l = mixL[i] * gain, r = mixR[i] * gain;

            if (useDelay) {
                // Feedback delay; the length glides so turning DELAY TIME bends pitch like tape.
                delaySmooth_ += (delayTarget - delaySmooth_) * 0.0005f;
                float rp = float(delayWrite_) - delaySmooth_;
                if (rp < 0.0f) rp += float(delaySize);
                const int   i0   = static_cast<int>(rp);
                const int   i1   = i0 + 1 == delaySize ? 0 : i0 + 1;
                const float frac = rp - float(i0);
                const float wet  = delayBuf_[i0] + (delayBuf_[i1] - delayBuf_[i0]) * frac;
                delayBuf_[delayWrite_] = 0.5f * (l + r) + wet * feedback;
                silentRun_ = std::fabs(delayBuf_[delayWrite_]) > 1e-5f ? 0 : silentRun_ + 1;
                if (++delayWrite_ == delaySize) delayWrite_ = 0;
                l += wet * p.delayMix;
                r += wet * p.delayMix;
            }

            if (useChorus) {
                chorusBuf_[chorusWrite_] = 0.5f * (l + r);
                chorusPhase_ += chorusInc;
                if (chorusPhase_ >= 1.0f) chorusPhase_ -= 1.0f;
                auto tap = [&](float ph) {
                    float rp = float(chorusWrite_) - (chorusBase + chorusDepth * fastSin(ph));
                    if (rp < 0.0f) rp += float(chorusSize);
                    const int   i0   = static_cast<int>(rp);
                    const int   i1   = i0 + 1 == chorusSize ? 0 : i0 + 1;
                    const float frac = rp - float(i0);
                    return chorusBuf_[i0] + (chorusBuf_[i1] - chorusBuf_[i0]) * frac;
                };
                const float cl = tap(chorusPhase_), cr = tap(wrap01(chorusPhase_ + 0.25f));
                if (++chorusWrite_ == chorusSize) chorusWrite_ = 0;
                l += (cl - 0.3f * cr) * p.chorus;
                r += (cr - 0.3f * cl) * p.chorus;
            }

            left[start + i]  = softClip(l); // gentle limiter for chords / resonance
            right[start + i] = softClip(r);
        }
    }
}
