#include "sequencer.h"

#include <algorithm>
#include <cmath>

#include "instrument.h"

namespace {
constexpr float kPi = 3.14159265358979f;

// Cheap tanh-like soft clip for the master bus.
float softClip(float x) {
    x = std::clamp(x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}
} // namespace

bool SeqStep::empty() const {
    for (const SeqNote& n : notes)
        if (n.pitch >= 0) return false;
    return true;
}

void Pattern::clear() {
    for (SeqStep& s : steps) s = SeqStep{};
}

void Sequencer::init(int sampleRate) {
    sampleRate_ = sampleRate;
    trackBuf_.assign(1024, 0.0f);
    std::fill(std::begin(step_), std::end(step_), -1);
}

double Sequencer::samplesPerStep() const { return sampleRate_ * 60.0 / bpm_ / 4.0; }

void Sequencer::setBpm(float bpm) { bpm_ = std::clamp(bpm, 40.0f, 240.0f); }

void Sequencer::setLength(int track, int steps) {
    patterns[track].length = std::clamp(steps, 1, Pattern::kMaxSteps);
}

void Sequencer::start() {
    playing_ = true;
    std::fill(std::begin(step_), std::end(step_), -1);
    absStep_   = -1;
    untilStep_ = 0.0; // fire step 0 on the next sample
}

void Sequencer::stop(Instrument* const* inst) {
    playing_ = false;
    std::fill(std::begin(step_), std::end(step_), -1);
    for (int t = 0; t < kTracks; ++t) {
        releaseTrack(*inst[t], t);
        clearFresh(t);
    }
}

void Sequencer::releaseTrack(Instrument& inst, int track) {
    for (Active& a : active_[track]) {
        if (a.key >= 0) inst.noteOff(a.key);
        a = Active{};
    }
}

void Sequencer::clearFresh(int track) {
    for (SeqStep& s : patterns[track].steps)
        for (SeqNote& n : s.notes) n.fresh = false;
}

void Sequencer::fireStep(Instrument* const* inst) {
    ++absStep_;
    for (int t = 0; t < kTracks; ++t) {
        Pattern&    pat   = patterns[t];
        Instrument& track = *inst[t];
        step_[t]          = (step_[t] + 1) % pat.length;
        if (step_[t] == 0) ++passId_[t];

        if (!audible(t)) {
            releaseTrack(track, t);
            clearFresh(t);
            continue;
        }

        Active* active = active_[t];
        for (int i = 0; i < kMaxActive; ++i) {
            if (active[i].key >= 0 && --active[i].remaining <= 0) {
                track.noteOff(active[i].key);
                active[i] = Active{};
            }
        }

        for (SeqNote& n : pat.steps[step_[t]].notes) {
            if (n.pitch < 0) continue;
            if (n.fresh) { n.fresh = false; continue; }
            // Free slot, else steal the one closest to ending.
            int slot = 0;
            for (int i = 0; i < kMaxActive; ++i) {
                if (active[i].key < 0) { slot = i; break; }
                if (active[i].remaining < active[slot].remaining) slot = i;
            }
            if (active[slot].key >= 0) track.noteOff(active[slot].key);
            active[slot] = {kKeyBase + slot, n.len};
            track.noteOn(kKeyBase + slot, n.pitch);
        }
    }

    // The click follows the global beat so it stays steady under polymeters.
    if (recording && metronome && absStep_ % 4 == 0) {
        clickPos_    = 0;
        clickAccent_ = absStep_ % 16 == 0;
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
        mixClick(o, n);

        if (playing_) untilStep_ -= n;
        done += n;
    }
}

double Sequencer::heardPosition() const {
    const double sps = samplesPerStep();
    return double(absStep_) + (1.0 - untilStep_ / sps) - latencyFrames / sps;
}

void Sequencer::snapshotForUndo(int track) {
    Undo& u = undo_[track];
    if (u.has && u.pass == passId_[track]) return; // one undo level per overdub pass
    u.pattern = patterns[track];
    u.has     = true;
    u.pass    = passId_[track];
}

Sequencer::RecHandle Sequencer::record(int track, int pitch) {
    RecHandle h;
    if (!playing_ || step_[track] < 0 || pitch < 0 || pitch > 127) return h;

    const long nearest = std::lround(heardPosition());
    const int  offset  = int(nearest - absStep_); // -1, 0 or +1
    const int  len     = patterns[track].length;
    const int  step    = ((step_[track] + offset) % len + len) % len;

    snapshotForUndo(track);
    SeqStep& s    = patterns[track].steps[step];
    int      slot = -1;
    for (int i = 0; i < SeqStep::kMaxNotes && slot < 0; ++i)
        if (s.notes[i].pitch == pitch) slot = i; // same pitch again: overwrite
    for (int i = 0; i < SeqStep::kMaxNotes && slot < 0; ++i)
        if (s.notes[i].pitch < 0) slot = i;
    if (slot < 0) {
        h.full = true;
        return h;
    }

    // Not fired yet this pass, but already heard live: skip it once.
    s.notes[slot] = {int8_t(pitch), 1, offset > 0};
    h.track = track;
    h.step  = step;
    h.slot  = slot;
    h.pitch = pitch;
    h.start = double(nearest);
    return h;
}

void Sequencer::finishRecord(const RecHandle& h) {
    if (h.track < 0) return;
    Pattern& pat = patterns[h.track];
    if (h.step >= pat.length) return;
    SeqNote& n = pat.steps[h.step].notes[h.slot];
    if (n.pitch != h.pitch) return; // undone or cleared meanwhile
    long steps = std::lround(heardPosition() - h.start);
    n.len      = uint8_t(std::clamp<long>(steps, 1, pat.length));
}

void Sequencer::undo(int track) {
    Undo& u = undo_[track];
    if (!u.has) return;
    patterns[track] = u.pattern;
    u.has           = false;
    clearFresh(track);
}

void Sequencer::clearPattern(int track) {
    Undo& u   = undo_[track];
    u.pattern = patterns[track];
    u.has     = true;
    u.pass    = -1; // the next recording takes its own snapshot
    int len   = patterns[track].length;
    patterns[track].clear();
    patterns[track].length = len;
}
