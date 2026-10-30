#include "sequencer.h"

#include <algorithm>
#include <cmath>

#include "synth.h"

namespace {
constexpr float kPi = 3.14159265358979f;

float midiToHz(int m) { return 440.0f * std::pow(2.0f, (m - 69) / 12.0f); }
} // namespace

bool SeqStep::empty() const {
    for (const SeqNote& n : notes)
        if (n.pitch >= 0) return false;
    return true;
}

void Pattern::clear() {
    for (SeqStep& s : steps) s = SeqStep{};
}

void Sequencer::init(int sampleRate) { sampleRate_ = sampleRate; }

double Sequencer::samplesPerStep() const { return sampleRate_ * 60.0 / bpm_ / 4.0; }

void Sequencer::setBpm(float bpm) { bpm_ = std::clamp(bpm, 40.0f, 240.0f); }

void Sequencer::setLength(int steps) { pattern.length = std::clamp(steps, 1, Pattern::kMaxSteps); }

void Sequencer::start() {
    playing_   = true;
    step_      = -1;
    absStep_   = -1;
    untilStep_ = 0.0; // fire step 0 on the next sample
}

void Sequencer::stop(Synth& synth) {
    playing_ = false;
    step_    = -1;
    releaseAll(synth);
    clearFresh();
}

void Sequencer::releaseAll(Synth& synth) {
    for (Active& a : active_) {
        if (a.key >= 0) synth.noteOff(a.key);
        a = Active{};
    }
}

void Sequencer::clearFresh() {
    for (SeqStep& s : pattern.steps)
        for (SeqNote& n : s.notes) n.fresh = false;
}

void Sequencer::fireStep(Synth& synth) {
    step_ = (step_ + 1) % pattern.length;
    ++absStep_;
    if (step_ == 0) ++passId_;

    for (Active& a : active_) {
        if (a.key >= 0 && --a.remaining <= 0) {
            synth.noteOff(a.key);
            a = Active{};
        }
    }

    for (SeqNote& n : pattern.steps[step_].notes) {
        if (n.pitch < 0) continue;
        if (n.fresh) { n.fresh = false; continue; }
        // Free slot, else steal the one closest to ending.
        int slot = 0;
        for (int i = 0; i < kMaxActive; ++i) {
            if (active_[i].key < 0) { slot = i; break; }
            if (active_[i].remaining < active_[slot].remaining) slot = i;
        }
        if (active_[slot].key >= 0) synth.noteOff(active_[slot].key);
        active_[slot] = {kKeyBase + slot, n.len};
        synth.noteOn(kKeyBase + slot, midiToHz(n.pitch));
    }

    if (recording && metronome && step_ % 4 == 0) {
        clickPos_    = 0;
        clickAccent_ = step_ == 0;
        clickPhase_  = 0.0f;
    }
}

void Sequencer::mixClick(float* out, int frames) {
    if (clickPos_ < 0) return;
    const int   len = sampleRate_ / 30; // ~33 ms
    const float inc = (clickAccent_ ? 1600.0f : 1000.0f) / sampleRate_;
    for (int i = 0; i < frames && clickPos_ < len; ++i, ++clickPos_) {
        float env = 1.0f - float(clickPos_) / float(len);
        out[i] += std::sin(2.0f * kPi * clickPhase_) * env * env * 0.25f;
        clickPhase_ += inc;
        if (clickPhase_ >= 1.0f) clickPhase_ -= 1.0f;
    }
    if (clickPos_ >= len) clickPos_ = -1;
}

void Sequencer::process(Synth& synth, float* out, int frames) {
    int done = 0;
    while (done < frames) {
        if (playing_ && untilStep_ <= 0.0) {
            fireStep(synth);
            untilStep_ += samplesPerStep();
        }
        int n = frames - done;
        if (playing_) n = std::clamp(int(std::ceil(untilStep_)), 1, n);
        synth.render(out + done, n);
        mixClick(out + done, n);
        if (playing_) untilStep_ -= n;
        done += n;
    }
}

double Sequencer::heardPosition() const {
    const double sps = samplesPerStep();
    return double(absStep_) + (1.0 - untilStep_ / sps) - latencyFrames / sps;
}

void Sequencer::snapshotForUndo() {
    if (undoPass_ == passId_ && hasUndo_) return; // one undo level per overdub pass
    undo_     = pattern;
    hasUndo_  = true;
    undoPass_ = passId_;
}

Sequencer::RecHandle Sequencer::record(int pitch) {
    RecHandle h;
    if (!playing_ || step_ < 0 || pitch < 0 || pitch > 127) return h;

    const double pos     = heardPosition();
    const long   nearest = std::lround(pos);
    const int    offset  = int(nearest - absStep_); // -1, 0 or +1
    const int    len     = pattern.length;
    const int    step    = ((step_ + offset) % len + len) % len;

    snapshotForUndo();
    SeqStep& s    = pattern.steps[step];
    int      slot = -1;
    for (int i = 0; i < SeqStep::kMaxNotes && slot < 0; ++i)
        if (s.notes[i].pitch == pitch) slot = i; // same pitch again: overwrite
    for (int i = 0; i < SeqStep::kMaxNotes && slot < 0; ++i)
        if (s.notes[i].pitch < 0) slot = i;
    if (slot < 0) return h; // step full

    // Not fired yet this pass, but already heard live: skip it once.
    s.notes[slot] = {int8_t(pitch), 1, offset > 0};
    h.step  = step;
    h.slot  = slot;
    h.pitch = pitch;
    h.start = double(nearest);
    return h;
}

void Sequencer::finishRecord(const RecHandle& h) {
    if (h.step < 0 || h.step >= pattern.length) return;
    SeqNote& n = pattern.steps[h.step].notes[h.slot];
    if (n.pitch != h.pitch) return; // undone or cleared meanwhile
    long steps = std::lround(heardPosition() - h.start);
    n.len      = uint8_t(std::clamp<long>(steps, 1, pattern.length));
}

void Sequencer::undo() {
    if (!hasUndo_) return;
    pattern  = undo_;
    hasUndo_ = false;
    clearFresh();
}

void Sequencer::clearPattern() {
    undo_     = pattern;
    hasUndo_  = true;
    undoPass_ = -1; // the next recording takes its own snapshot
    int len   = pattern.length;
    pattern.clear();
    pattern.length = len;
}
