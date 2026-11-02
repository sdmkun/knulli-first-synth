#include "reverb.h"

#include <algorithm>
#include <cmath>

namespace {
// Mutually prime-ish lengths in ms, so the echoes do not line up.
const float kLineMs[8] = {31.7f, 37.9f, 41.3f, 47.1f, 53.9f, 59.3f, 67.7f, 73.1f};
const float kDiffMs[4] = {4.7f, 3.6f, 12.7f, 9.3f};
constexpr float kDiffGain = 0.62f;

float decaySeconds(float size) { return 0.4f * std::pow(20.0f, std::clamp(size, 0.0f, 1.0f)); }
} // namespace

void Reverb::init(int sampleRate) {
    sampleRate_ = sampleRate;
    for (int i = 0; i < kLines; ++i) lines_[i].buf.assign(size_t(kLineMs[i] * 0.001f * sampleRate) + 1, 0.0f);
    for (int i = 0; i < kDiff; ++i) diff_[i].buf.assign(size_t(kDiffMs[i] * 0.001f * sampleRate) + 1, 0.0f);
    clear();
}

void Reverb::clear() {
    for (Line& l : lines_) std::fill(l.buf.begin(), l.buf.end(), 0.0f), l.pos = 0;
    for (Line& l : diff_) std::fill(l.buf.begin(), l.buf.end(), 0.0f), l.pos = 0;
    std::fill(std::begin(lp_), std::end(lp_), 0.0f);
    quiet_ = tail_;
}

void Reverb::update() {
    if (size_ == shownSize_ && tone_ == shownTone_) return;
    shownSize_ = size_;
    shownTone_ = tone_;
    const float rt60 = decaySeconds(size_);
    for (int i = 0; i < kLines; ++i) {
        const float len = float(lines_[i].buf.size());
        gain_[i]        = std::pow(10.0f, -3.0f * len / (rt60 * float(sampleRate_)));
    }
    // One-pole low-pass in each loop: tone 0 rolls off near 1.5 kHz, 1 hardly at all.
    const float fc = 1500.0f * std::pow(10.0f, std::clamp(tone_, 0.0f, 1.0f));
    damp_          = std::exp(-6.2831853f * fc / float(sampleRate_));
    tail_          = int(rt60 * float(sampleRate_)) + sampleRate_ / 10;
}

void Reverb::process(const float* in, float* left, float* right, int frames) {
    update();
    bool any = false;
    for (int i = 0; i < frames && !any; ++i) any = in[i] != 0.0f;
    if (!any && quiet_ >= tail_) return;

    for (int i = 0; i < frames; ++i) {
        float x = in[i];
        quiet_  = x != 0.0f ? 0 : quiet_ + 1;

        // Diffusion: Schroeder allpasses smear the input into a dense cloud.
        for (Line& d : diff_) {
            float& slot = d.buf[d.pos];
            float  del  = slot;
            float  v    = x + kDiffGain * del;
            slot        = v;
            x           = del - kDiffGain * v;
            if (++d.pos == int(d.buf.size())) d.pos = 0;
        }

        // FDN: read every line, damp, mix through the Householder matrix, write back.
        float out[kLines];
        float sum = 0.0f;
        for (int l = 0; l < kLines; ++l) {
            float v = lines_[l].buf[lines_[l].pos] * gain_[l];
            lp_[l]  = v + (lp_[l] - v) * damp_;
            out[l]  = lp_[l];
            sum += out[l];
        }
        const float h = sum * (2.0f / kLines);
        for (int l = 0; l < kLines; ++l) {
            Line& ln          = lines_[l];
            ln.buf[ln.pos]    = out[l] - h + (l & 1 ? -x : x) * 0.35f;
            if (++ln.pos == int(ln.buf.size())) ln.pos = 0;
        }
        const float wl = out[0] + out[2] + out[4] + out[6];
        const float wr = out[1] + out[3] + out[5] + out[7];
        left[i] += wl * 0.5f;
        right[i] += wr * 0.5f;
    }
}
