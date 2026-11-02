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

const char* fxName(Fx fx) {
    static const char* kNames[] = {"---", "RET", "ARP", "KIL", "DLY", "CHA"};
    return kNames[std::min(int(fx), int(Fx::Count) - 1)];
}

int fxDefault(Fx fx) {
    switch (fx) {
    case Fx::Ret: return 3;    // a 64th-note roll
    case Fx::Arp: return 0x37; // minor triad
    case Fx::Kil: return 6;    // half a step
    case Fx::Dly: return 3;
    case Fx::Cha: return 50;
    default:      return 0;
    }
}

int fxClamp(Fx fx, int v) {
    switch (fx) {
    case Fx::Ret: return std::clamp(v, 1, kTicksPerStep - 1);
    case Fx::Arp: return std::clamp(v, 0, 0xFF);
    case Fx::Kil: return std::clamp(v, 1, 99);
    case Fx::Dly: return std::clamp(v, 1, kTicksPerStep - 1);
    case Fx::Cha: return std::clamp(v, 0, 99);
    default:      return 0;
    }
}

std::string fxValueText(Fx fx, int v) {
    static const char* kHex = "0123456789ABCDEF";
    if (fx == Fx::None) return "--";
    if (fx == Fx::Arp) return {kHex[(v >> 4) & 15], kHex[v & 15]};
    return {char('0' + v / 10 % 10), char('0' + v % 10)};
}

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
}

// The tick just fired decides: ticks of an even step stretch by 2 * swing, those
// of an odd step shrink by the same amount, so each pair of steps keeps its length.
// A groove sets each step's length from its table instead.
double Sequencer::samplesPerTick() const {
    const double straight = sampleRate_ * 60.0 / bpm_ / 4.0 / kTicksPerStep;
    const long   step     = tick_ >= 0 ? tick_ / kTicksPerStep : 0;
    if (grooveSteps_ > 0) {
        const int g = int(step % grooveSteps_);
        return straight * (grooveStart_[g + 1] - grooveStart_[g]);
    }
    return straight * 2.0 * (step % 2 == 1 ? 1.0 - swing_ : swing_);
}

void Sequencer::setGroove(const float* start, const float* gain, int steps) {
    grooveSteps_ = std::clamp(steps, 0, kMaxGrooveSteps);
    for (int i = 0; i < grooveSteps_; ++i) {
        grooveGain_[i]  = gain[i];
        grooveStart_[i] = start[i];
    }
    if (grooveSteps_) grooveStart_[grooveSteps_] = start[grooveSteps_];
}

void Sequencer::setBpm(float bpm) { bpm_ = std::clamp(bpm, 40.0f, 240.0f); }

void Sequencer::setSwing(float s) { swing_ = std::clamp(s, 0.5f, 0.75f); }

void Sequencer::setLength(int track, int steps) {
    patterns[track].length = std::clamp(steps, 1, Pattern::kMaxSteps);
}

void Sequencer::queue(int track, const Pattern& p, int slotIndex) {
    if (!playing_) {
        patterns[track] = p;
        slot[track]     = slotIndex;
        queued_[track]  = false;
        return;
    }
    queuedPat_[track]  = p;
    queuedSlot_[track] = slotIndex;
    queued_[track]     = true;
}

void Sequencer::start() {
    playing_ = true;
    std::fill(std::begin(step_), std::end(step_), -1);
    std::fill(std::begin(loops_), std::end(loops_), 0);
    tick_      = -1;
    untilTick_ = 0.0; // fire step 0 on the next sample
}

void Sequencer::stop(Instrument* const* inst) {
    playing_ = false;
    std::fill(std::begin(step_), std::end(step_), -1);
    for (int t = 0; t < kTracks; ++t) releaseTrack(*inst[t], t);
}

void Sequencer::releaseTrack(Instrument& inst, int track) {
    for (Column& c : cols_[track]) {
        if (c.key >= 0) inst.noteOff(c.key);
        c = Column{};
    }
}

float Sequencer::random01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ >> 8) * (1.0f / 16777216.0f);
}

int Sequencer::newKey(int track) {
    int key         = kKeyBase + nextKey_[track];
    nextKey_[track] = (nextKey_[track] + 1) % kKeys;
    return key;
}

// Queues each column's entry of the new step at its tick (DLY moves it later; CHA
// may drop it). The row's own commands: DLY and CHA act on all its notes at once
// (one chance roll, so a chord stays whole); RET, ARP and KIL go to each note as
// it starts, or right away to the notes still sounding in columns the row leaves empty.
void Sequencer::startStep(int track) {
    const SeqStep& st     = patterns[track].steps[step_[track]];
    int            rowDly = 0;
    bool           rowOn  = true;
    for (const StepFx& f : st.fx) {
        if (f.fx == Fx::Dly) rowDly = std::min<int>(f.val, kTicksPerStep - 1);
        if (f.fx == Fx::Cha && random01() * 100.0f >= f.val) rowOn = false;
    }
    for (int c = 0; c < SeqStep::kMaxNotes; ++c) {
        Column&        col = cols_[track][c];
        const SeqNote& n   = st.notes[c];
        col.retEvery       = 0; // rolls last one step
        col.pending        = false;
        if (n.pitch == SeqNote::kEmpty) {
            if (col.key >= 0)
                for (const StepFx& f : st.fx) applyFx(col, f.fx, f.val);
            continue;
        }
        if (n.isNote() && (!rowOn || (n.fx == Fx::Cha && random01() * 100.0f >= n.fxVal))) continue;
        col.pending  = true;
        col.pend     = n;
        col.pendTick = n.isNote() && n.fx == Fx::Dly ? std::min<int>(n.fxVal, kTicksPerStep - 1) : rowDly;
    }
}

void Sequencer::applyFx(Column& col, Fx fx, int val) {
    const int tickInStep = int(tick_ % kTicksPerStep);
    switch (fx) {
    case Fx::Ret:
        col.retEvery = std::max(1, val);
        col.retNext  = tickInStep + col.retEvery;
        break;
    case Fx::Arp: // ARP 00 stops an arpeggio
        col.arp    = uint8_t(val);
        col.arpAge = 0;
        break;
    case Fx::Kil: col.killAt = tick_ + std::max(1, val); break;
    default: break;
    }
}

// A note or OFF ends whatever its column was holding; a note then takes the column.
void Sequencer::trigger(Instrument& inst, int track, Column& col, const SeqNote& n) {
    if (col.key >= 0) inst.noteOff(col.key);
    col.key      = -1;
    col.arp      = 0;
    col.killAt   = -1;
    col.retEvery = 0;
    if (!n.isNote()) return;
    col.key  = newKey(track);
    col.note = n.pitch;
    col.gain = velocityGain(n.vel);
    if (grooveSteps_ > 0) col.gain *= grooveGain_[(tick_ / kTicksPerStep) % grooveSteps_];
    inst.noteOn(col.key, col.note, col.gain);
    applyFx(col, n.fx, n.fxVal);
    for (const StepFx& f : patterns[track].steps[step_[track]].fx) applyFx(col, f.fx, f.val);
}

void Sequencer::fireTick(Instrument* const* inst) {
    ++tick_;
    const int tickInStep = int(tick_ % kTicksPerStep);
    for (int t = 0; t < kTracks; ++t) {
        Instrument& track = *inst[t];
        if (tickInStep == 0 && queued_[t] && songStep() % std::max(1, launchQuant) == 0) {
            releaseTrack(track, t);
            patterns[t] = queuedPat_[t];
            slot[t]     = queuedSlot_[t];
            queued_[t]  = false;
            step_[t]    = -1; // the new pattern starts from its first step
        }
        if (tickInStep == 0) {
            const bool first = step_[t] < 0;
            step_[t]         = (step_[t] + 1) % patterns[t].length;
            if (!audible(t)) {
                releaseTrack(track, t);
                continue;
            }
            if (step_[t] == 0 && !first) {
                ++loops_[t];
                if (onLoop) onLoop(loopUser, t);
            }
            startStep(t);
        }
        if (!audible(t)) continue;

        for (Column& col : cols_[t]) {
            if (col.pending && col.pendTick == tickInStep) {
                col.pending = false;
                trigger(track, t, col, col.pend);
                continue;
            }
            if (col.key < 0) continue;
            if (col.killAt >= 0 && tick_ >= col.killAt) {
                track.kill(col.key);
                col.key = -1;
                continue;
            }
            if (col.retEvery > 0 && col.retNext == tickInStep) {
                track.noteOff(col.key);
                col.key = newKey(t);
                track.noteOn(col.key, col.note, col.gain);
                col.retNext += col.retEvery;
            }
            if (col.arp && ++col.arpAge % kArpTicks == 0) {
                const int offsets[3] = {0, col.arp >> 4, col.arp & 15};
                track.setPitch(col.key, col.note + offsets[col.arpAge / kArpTicks % 3]);
            }
        }
    }
}

void Sequencer::process(Instrument* const* inst, float* out, int frames) {
    int done = 0;
    while (done < frames) {
        if (playing_ && untilTick_ <= 0.0) {
            fireTick(inst);
            untilTick_ += samplesPerTick();
        }
        int n = std::min(frames - done, int(trackBuf_.size()));
        if (playing_) n = std::clamp(int(std::ceil(untilTick_)), 1, n);

        float* o = out + done;
        std::fill(o, o + n, 0.0f);
        for (int t = 0; t < kTracks; ++t) {
            inst[t]->render(trackBuf_.data(), n);
            const float g    = volume[t];
            float       peak = level[t];
            for (int i = 0; i < n; ++i) {
                const float v = trackBuf_[i] * g;
                o[i] += v;
                peak = std::max(peak, std::fabs(v));
            }
            level[t] = peak;
        }
        for (int i = 0; i < n; ++i) o[i] = softClip(o[i] * 0.8f);

        if (playing_) untilTick_ -= n;
        done += n;
    }
}
