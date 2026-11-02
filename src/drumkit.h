#pragma once

#include <string>
#include <vector>

#include "instrument.h"

// A sample loaded from a WAV file: `data` is mono or the left channel, `right`
// is the right channel of a stereo file (empty for mono).
struct Sample {
    std::string        name; // shown on the pad
    std::vector<float> data;
    std::vector<float> right;
    int                rate = 48000;
};

// Up to eight samples, one per pad, loaded from one folder.
struct Kit {
    static constexpr int kPads = 8;
    std::string name;
    std::string dir; // the folder it was loaded from
    Sample      pads[kPads];
};

// Reads a PCM (8/16/24/32-bit) or float WAV, mono or stereo (more channels: the first two).
bool loadWav(const std::string& path, Sample& out, std::string& error);

// "01-Kick_808.wav" -> "KICK_808", the name a sample shows on its pad.
std::string sampleName(const std::string& path);
bool        isWavFile(const std::string& path);

// Each subfolder of `dir` is a kit; its WAV files, sorted by name, fill pads 1-8.
std::vector<Kit> loadKits(const std::string& dir);

// One-shot sample player: pad n plays on MIDI note kBaseNote + n, and notes
// play to the end (or the END point) regardless of note-off.
class DrumKit : public Instrument {
public:
    static constexpr int kBaseNote = 36;
    static constexpr int kVoices   = 16;

    struct PadSettings {
        int   pitch  = 0;    // semitones, -24..+24
        float start  = 0.0f; // 0..1 of the sample
        float end    = 1.0f;
        float volume = 0.8f;
        float pan    = 0.0f; // -1 left .. 1 right
    };

    void init(int sampleRate) { sampleRate_ = sampleRate; }

    // Kits are owned elsewhere and must outlive every DrumKit that points at them.
    void       setKit(const Kit* kit);
    const Kit* kit() const { return kit_; }

    void noteOn(int key, int note, float gain) override;
    void noteOff(int) override {}
    void setPitch(int, int) override {}
    void kill(int key) override;
    void allOff() override;
    void render(float* left, float* right, int frames) override;
    int  activeVoices() const override;

    PadSettings pads[Kit::kPads];

private:
    struct Voice {
        int           key    = -1;
        const Sample* sample = nullptr;
        double        pos    = 0.0;
        double        inc    = 1.0;
        double        end    = 0.0;
        float         gain   = 0.0f;
        float         gainL  = 0.0f, gainR = 0.0f;
        unsigned      age    = 0;
    };

    const Kit* kit_        = nullptr;
    int        sampleRate_ = 48000;
    Voice      voices_[kVoices];
    unsigned   counter_    = 0;
};
