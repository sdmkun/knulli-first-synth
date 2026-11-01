#include "wavetable.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace wt {
namespace {

constexpr float kPi = 3.14159265358979f;
using Cpx = std::complex<float>;

// In-place iterative radix-2 FFT; `inverse` scales by 1/N.
void fft(std::vector<Cpx>& a, bool inverse) {
    const int n = int(a.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = 2.0f * kPi / len * (inverse ? 1.0f : -1.0f);
        Cpx   wl(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            Cpx w(1.0f, 0.0f);
            for (int k = 0; k < len / 2; ++k) {
                Cpx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k]           = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (Cpx& c : a) c /= float(n);
}

float frac(float x) { return x - std::floor(x); }

// ---- frame shapes: x = phase 0..1, p = position across the table 0..1

float morph(float x, float p) { // sine -> triangle -> saw -> square
    float sine = std::sin(2.0f * kPi * x);
    float tri  = 1.0f - 4.0f * std::fabs(frac(x + 0.25f) - 0.5f);
    float saw  = 2.0f * frac(x + 0.5f) - 1.0f;
    float sq   = x < 0.5f ? 1.0f : -1.0f;
    float s    = p * 3.0f;
    if (s < 1.0f) return sine + (tri - sine) * s;
    if (s < 2.0f) return tri + (saw - tri) * (s - 1.0f);
    return saw + (sq - saw) * (s - 2.0f);
}

float pulse(float x, float p) { return x < 0.5f - 0.46f * p ? 1.0f : -1.0f; }

float sync(float x, float p) { return 2.0f * frac(x * (1.0f + 4.0f * p)) - 1.0f; } // hard-synced saw

float fold(float x, float p) { return std::sin(kPi * 0.5f * (1.0f + 6.0f * p) * std::sin(2.0f * kPi * x)); }

float digi(float x, float p) { // a sine with fewer samples and levels as p grows
    float steps  = std::round(64.0f * std::pow(1.0f / 8.0f, p)); // 64 .. 8 samples per cycle
    float levels = std::round(std::pow(2.0f, 5.0f - 3.5f * p));  // 32 .. ~3 levels
    float s      = std::sin(2.0f * kPi * std::floor(x * steps) / steps);
    return std::round(s * levels) / levels;
}

// ---- spectral tables: amplitude of harmonic h at position p

float vowel(int h, float p) {
    // Formants (Hz) of A, E, I, O, U, assuming a 110 Hz voice.
    static const float kF[5][3] = {{800, 1150, 2900}, {400, 1700, 2600}, {300, 2200, 3000}, {450, 800, 2830}, {325, 700, 2530}};
    static const float kGain[3] = {1.0f, 0.5f, 0.25f};
    float s = p * 4.0f;
    int   a = std::min(int(s), 3);
    float t = s - a;
    float f = h * 110.0f, amp = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float fc = kF[a][i] + (kF[a + 1][i] - kF[a][i]) * t;
        float d  = (f - fc) / (80.0f + fc * 0.08f);
        amp += kGain[i] * std::exp(-d * d);
    }
    return amp / std::sqrt(float(h));
}

float harm(int h, float p) { // drawbars opening upwards
    float top = 1.0f + p * 15.0f;
    if (h > top + 1.0f) return 0.0f;
    float w = std::min(1.0f, top + 1.0f - h);
    return w / std::pow(float(h), 0.7f);
}

struct Def {
    const char* name;
    float (*shape)(float x, float p);    // time-domain frame, or
    float (*spectrum)(int h, float p);   // harmonic amplitudes
};
const Def kDefs[kTables] = {
    {"MORPH", morph, nullptr}, {"PULSE", pulse, nullptr}, {"SYNC", sync, nullptr},   {"VOWEL", nullptr, vowel},
    {"FOLD", fold, nullptr},   {"HARM", nullptr, harm},   {"DIGI", digi, nullptr},
};

struct Tables {
    std::vector<float> data; // [table][frame][mip][kSize + 1]
    Tables() {
        data.resize(size_t(kTables) * kFrames * kMips * (kSize + 1));
        std::vector<Cpx> spec(kSize), tmp(kSize);
        for (int t = 0; t < kTables; ++t) {
            for (int f = 0; f < kFrames; ++f) {
                float p = float(f) / (kFrames - 1);
                if (kDefs[t].shape) {
                    for (int i = 0; i < kSize; ++i) spec[i] = Cpx(kDefs[t].shape(float(i) / kSize, p), 0.0f);
                    fft(spec, false);
                } else {
                    std::fill(spec.begin(), spec.end(), Cpx());
                    for (int h = 1; h < kSize / 2; ++h) { // sine phases: X[h] = -i A N / 2
                        float a = kDefs[t].spectrum(h, p) * kSize * 0.5f;
                        spec[h] = Cpx(0.0f, -a);
                        spec[kSize - h] = Cpx(0.0f, a);
                    }
                }
                spec[0] = Cpx();
                float norm = 0.0f;
                for (int m = 0; m < kMips; ++m) {
                    int maxH = (kSize / 2) >> m;
                    for (int k = 0; k < kSize; ++k) {
                        int h  = k <= kSize / 2 ? k : kSize - k;
                        tmp[k] = h < maxH ? spec[k] : Cpx();
                    }
                    fft(tmp, true);
                    float* out = &data[((size_t(t) * kFrames + f) * kMips + m) * (kSize + 1)];
                    for (int i = 0; i < kSize; ++i) out[i] = tmp[i].real();
                    if (m == 0) // every level of a frame shares the full-band level's scaling
                        for (int i = 0; i < kSize; ++i) norm = std::max(norm, std::fabs(out[i]));
                    float g = norm > 1e-6f ? 1.0f / norm : 0.0f;
                    for (int i = 0; i < kSize; ++i) out[i] *= g;
                    out[kSize] = out[0];
                }
            }
        }
    }
};

const Tables& tables() {
    static const Tables t;
    return t;
}

} // namespace

const char* tableName(int t) { return kDefs[std::clamp(t, 0, kTables - 1)].name; }

void build() { tables(); }

const float* frame(int t, int f, int m) {
    return &tables().data[((size_t(t) * kFrames + f) * kMips + m) * (kSize + 1)];
}

int mipFor(float freq, float sampleRate) {
    float allowed = sampleRate * 0.5f / std::max(freq, 1.0f); // harmonics that fit below Nyquist
    int   m       = 0;
    while (m < kMips - 1 && float((kSize / 2) >> m) > allowed) ++m;
    return m;
}

float read(int t, int m, float pos, float phase) {
    float fp = pos * (kFrames - 1);
    int   f0 = std::min(int(fp), kFrames - 2);
    float ff = fp - f0;
    float x  = phase * kSize;
    int   i  = int(x);
    float fr = x - i;
    const float* a = frame(t, f0, m);
    const float* b = frame(t, f0 + 1, m);
    float sa = a[i] + (a[i + 1] - a[i]) * fr;
    float sb = b[i] + (b[i + 1] - b[i]) * fr;
    return sa + (sb - sa) * ff;
}

} // namespace wt
