#include "sampler.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "synth.h" // attackSec, releaseSec, cutoffHz: the same curves as the synth

void Sampler::setSample(const Sample* s) {
    allOff();
    for (Voice& v : voices_) v = Voice{};
    sample_ = s;
}

double Sampler::rate(int note) const {
    if (!sample_) return 1.0;
    const double semis = note - settings.root + settings.fine / 100.0;
    return double(sample_->rate) / sampleRate_ * std::pow(2.0, semis / 12.0);
}

void Sampler::noteOn(int key, int note, float gain) {
    if (!sample_ || sample_->data.size() < 2) return;
    Voice* v = nullptr;
    for (Voice& c : voices_)
        if (!c.on) { v = &c; break; }
    if (!v) { // steal the oldest, preferring released ones
        v = &voices_[0];
        for (Voice& c : voices_) {
            bool better = (!c.gate && v->gate) || (c.gate == v->gate && c.age < v->age);
            if (better) v = &c;
        }
    }
    const bool was = v->on;
    v->key    = key;
    v->on     = true;
    v->gate   = true;
    v->fast   = false;
    v->note   = note;
    v->pos    = std::floor(std::clamp(settings.start, 0.0f, 0.99f) * double(sample_->data.size() - 1));
    v->inc    = rate(note);
    v->gain   = gain;
    v->rising = true;
    if (!was) v->env = 0.0f, v->l1 = v->l2 = v->r1 = v->r2 = 0.0f;
    v->age = ++counter_;
}

void Sampler::noteOff(int key) {
    for (Voice& v : voices_)
        if (v.on && v.key == key) v.gate = false;
}

void Sampler::setPitch(int key, int note) {
    for (Voice& v : voices_)
        if (v.on && v.key == key) v.note = note, v.inc = rate(note);
}

void Sampler::kill(int key) {
    for (Voice& v : voices_)
        if (v.on && v.key == key) v.gate = false, v.fast = true;
}

void Sampler::allOff() {
    for (Voice& v : voices_) v.gate = false;
}

int Sampler::activeVoices() const {
    int n = 0;
    for (const Voice& v : voices_)
        if (v.on) ++n;
    return n;
}

void Sampler::render(float* left, float* right, int frames) {
    std::fill(left, left + frames, 0.0f);
    std::fill(right, right + frames, 0.0f);
    if (!sample_) return;
    const Settings s    = settings;
    const float    sr   = sampleRate_;
    const float    atk  = 1.0f / (attackSec(s.attack) * sr);
    const float    rel  = std::exp(std::log(0.001f) / (releaseSec(s.release) * sr));
    const float    kill = std::exp(std::log(0.001f) / (0.005f * sr));
    const bool     filt = s.cutoff < 0.999f;
    const float    fc   = std::min(cutoffHz(s.cutoff), sr * 0.45f);
    const float    k    = std::max(2.0f - 2.0f * s.reso, 0.05f);
    const float    g    = std::tan(3.14159265f * fc / sr);
    const float    a1   = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float    vol  = s.volume * s.volume;
    const float*   dl   = sample_->data.data();
    const float*   dr   = sample_->right.empty() ? dl : sample_->right.data();
    const double   last = double(sample_->data.size() - 1);

    auto lp = [&](float x, float& ic1, float& ic2) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return v2;
    };

    for (Voice& v : voices_) {
        if (!v.on) continue;
        const bool gated = s.gate;
        for (int i = 0; i < frames; ++i) {
            // Envelope: attack to 1, hold while gated (or always, in one-shot mode), then release.
            if (v.rising) {
                v.env += atk;
                if (v.env >= 1.0f) v.env = 1.0f, v.rising = false;
            }
            if (v.fast) v.env *= kill;
            else if (gated && !v.gate) v.env *= rel, v.rising = false;
            const double rest = last - v.pos;
            if (v.env < 0.0005f && !v.rising) { v.on = false; break; }
            if (rest <= 0.0) { v.on = false; break; }

            const int   i0 = int(v.pos);
            const float fr = float(v.pos - i0);
            float       l  = dl[i0] + (dl[i0 + 1] - dl[i0]) * fr;
            float       r  = dr[i0] + (dr[i0 + 1] - dr[i0]) * fr;
            float       e  = v.env * v.gain * vol;
            if (rest < 64.0 * v.inc) e *= float(rest / (64.0 * v.inc)); // no click at the very end
            if (filt) {
                l = lp(l, v.l1, v.l2);
                r = lp(r, v.r1, v.r2);
            }
            left[i] += l * e;
            right[i] += r * e;
            v.pos += v.inc;
        }
    }
}

int rootFromName(const std::string& path) {
    // The stem, in upper case.
    size_t slash = path.find_last_of("/\\");
    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name.resize(dot);
    for (char& c : name) c = char(std::toupper(static_cast<unsigned char>(c)));

    // A note is a letter A-G standing alone (or followed by #, B, M, MIN, MAJ, a digit), not inside a word.
    static const int kPc[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    int found = -1;
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (c < 'A' || c > 'G') continue;
        if (i > 0 && std::isalnum(static_cast<unsigned char>(name[i - 1]))) continue;
        size_t j  = i + 1;
        int    pc = kPc[c - 'A'];
        if (j < name.size() && (name[j] == '#' || name[j] == 'S')) {
            // "S" only counts as sharp when something note-like follows ("CS3", "C_SHARP" are rare; "CS" alone too)
            if (name[j] == '#' || (j + 1 < name.size() && std::isdigit(static_cast<unsigned char>(name[j + 1])))) ++pc, ++j;
        } else if (j < name.size() && name[j] == 'B' && (j + 1 >= name.size() || !std::isalpha(static_cast<unsigned char>(name[j + 1])))) {
            --pc, ++j;
        }
        // Chord quality after the note is fine ("AM", "AMIN", "CMAJ7"); anything else wordy is not a note.
        size_t q = j;
        for (const char* tag : {"MIN", "MAJ", "M", "DIM", "AUG", "SUS"}) {
            size_t n = std::char_traits<char>::length(tag);
            if (name.compare(q, n, tag) == 0) { q += n; break; }
        }
        int octave = 4;
        bool hasOct = false;
        size_t o = q;
        if (q > j) o = name.size(); // after a chord quality, digits are an extension ("CMAJ7"), not an octave
        if (o < name.size() && std::isdigit(static_cast<unsigned char>(name[o]))) {
            octave = name[o] - '0';
            hasOct = true;
            ++o;
        }
        size_t endAt = hasOct ? o : std::min(q, name.size());
        while (endAt < name.size() && std::isdigit(static_cast<unsigned char>(name[endAt]))) ++endAt; // "CMAJ7"
        if (endAt < name.size() && std::isalpha(static_cast<unsigned char>(name[endAt]))) continue; // part of a word
        found = std::clamp(12 * (octave + 1) + pc, 0, 127); // pc may be -1 (Cb) or 12 (B#)
        // Prefer the last match: names often start with a word like "BASS" and end with the note.
    }
    return found;
}
