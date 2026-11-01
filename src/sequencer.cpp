#include "sequencer.h"

#include <algorithm>
#include <cmath>

#include "instrument.h"

namespace {
// Cheap tanh-like soft clip for the master bus.
float softClip(float x) {
    x = std::clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
} // namespace

float velocityGain(int vel) {
    // Roughly 3 dB apart below the default, gentler accents above it.
    static const float kGain[kMaxVel + 1] = {0.0f, 0.16f, 0.25f, 0.36f, 0.5f, 0.7f, 1.0f, 1.2f, 1.4f};
    return kGain[std::clamp(vel, 1, kMaxVel)];
}

bool SeqStep::empty() const {
    for (const SeqNote& n : notes)
        if (n.pitch != SeqNote::kEmpty) return false;
    return true;
}

int SeqStep::count() const {
    int n = 0;
    for (const SeqNote& s : notes)
        if (s.isNote()) ++n;
    return n;
}

bool SeqStep::hasOff() const {
    for (const SeqNote& s : notes)
        if (s.pitch == SeqNote::kOff) return true;
    return false;
}

int SeqStep::find(int pitch) const {
    for (int i = 0; i < kMaxNotes; ++i)
        if (notes[i].pitch == pitch) return i;
    return -1;
}

bool SeqStep::add(int pitch, int vel) {
    int slot = find(pitch);
    if (slot < 0) slot = find(SeqNote::kEmpty);
    if (slot < 0 || pitch < 0 || pitch > 127) return false;
    notes[slot] = {int8_t(pitch), uint8_t(std::clamp(vel, 1, kMaxVel))};
    return true;
}

void SeqStep::remove(int pitch) {
    int slot = find(pitch);
    if (slot >= 0) notes[slot] = SeqNote{};
}

int SeqStep::sorted(SeqNote* out) const {
    int n = 0;
    for (const SeqNote& s : notes)
        if (s.isNote()) out[n++] = s;
    std::sort(out, out + n, [](const SeqNote& a, const SeqNote& b) { return a.pitch < b.pitch; });
    return n;
}

void Pattern::clear() {
    for (SeqStep& s : steps) s = SeqStep{};
}

void Sequencer::init(int sampleRate) {
    sampleRate_ = sampleRate;
    trackBuf_.assign(1024, 0.0f);
    std::fill(std::begin(step_), std::end(step_), -1);
    for (auto& cols : sounding_) std::fill(std::begin(cols), std::end(cols), -1);
}

double Sequencer::samplesPerStep() const { return sampleRate_ * 60.0 / bpm_ / 4.0; }

void Sequencer::setBpm(float bpm) { bpm_ = std::clamp(bpm, 40.0f, 240.0f); }

void Sequencer::setLength(int track, int steps) {
    patterns[track].length = std::clamp(steps, 1, Pattern::kMaxSteps);
}

void Sequencer::start() {
    playing_ = true;
    std::fill(std::begin(step_), std::end(step_), -1);
    untilStep_ = 0.0; // fire step 0 on the next sample
}

void Sequencer::stop(Instrument* const* inst) {
    playing_ = false;
    std::fill(std::begin(step_), std::end(step_), -1);
    for (int t = 0; t < kTracks; ++t) releaseTrack(*inst[t], t);
}

void Sequencer::releaseTrack(Instrument& inst, int track) {
    for (int& key : sounding_[track]) {
        if (key >= 0) inst.noteOff(key);
        key = -1;
    }
}

void Sequencer::fireStep(Instrument* const* inst) {
    for (int t = 0; t < kTracks; ++t) {
        Pattern&    pat   = patterns[t];
        Instrument& track = *inst[t];
        step_[t]          = (step_[t] + 1) % pat.length;

        if (!audible(t)) {
            releaseTrack(track, t);
            continue;
        }

        // A note or OFF ends whatever its column was holding; a note then takes the column.
        const SeqStep& st = pat.steps[step_[t]];
        for (int c = 0; c < SeqStep::kMaxNotes; ++c) {
            const SeqNote& n = st.notes[c];
            if (n.pitch == SeqNote::kEmpty) continue;
            int& key = sounding_[t][c];
            if (key >= 0) track.noteOff(key);
            key = -1;
            if (!n.isNote()) continue;
            key         = kKeyBase + nextKey_[t];
            nextKey_[t] = (nextKey_[t] + 1) % kKeys;
            track.noteOn(key, n.pitch, velocityGain(n.vel));
        }
    }
}

void Sequencer::process(Instrument* const* inst, float* out, int frames) {
    int done = 0;
    while (done < frames) {
        if (playing_ && untilStep_ <= 0.0) {
            fireStep(inst);
            untilStep_ += samplesPerStep();
        }
        int n = std::min(frames - done, int(trackBuf_.size()));
        if (playing_) n = std::clamp(int(std::ceil(untilStep_)), 1, n);

        float* o = out + done;
        std::fill(o, o + n, 0.0f);
        for (int t = 0; t < kTracks; ++t) {
            inst[t]->render(trackBuf_.data(), n);
            for (int i = 0; i < n; ++i) o[i] += trackBuf_[i];
        }
        for (int i = 0; i < n; ++i) o[i] = softClip(o[i] * 0.8f);

        if (playing_) untilStep_ -= n;
        done += n;
    }
}
