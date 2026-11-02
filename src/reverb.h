#pragma once

#include <vector>

// A small stereo reverb for the send bus: four allpasses diffuse the input,
// then eight delay lines feed back through a Householder matrix with a
// low-pass in each loop. Cheap enough for the Brick (about 30 operations a sample).
class Reverb {
public:
    void init(int sampleRate);

    // size 0..1: decay time 0.4 s .. 8 s. tone 0..1: dark .. bright.
    void setSize(float size) { size_ = size; }
    void setTone(float tone) { tone_ = tone; }
    void clear();

    // Adds the reverb of mono `in` to `left` / `right`. Skips the work while
    // the input is silent and the tail has died away.
    void process(const float* in, float* left, float* right, int frames);

private:
    static constexpr int kLines = 8;
    static constexpr int kDiff  = 4;

    struct Line {
        std::vector<float> buf;
        int                pos = 0;
    };

    void update();

    Line  lines_[kLines];
    Line  diff_[kDiff];
    float gain_[kLines]  = {};
    float lp_[kLines]    = {};
    float damp_          = 0.3f;
    float size_          = 0.5f, tone_ = 0.6f;
    float shownSize_     = -1.0f, shownTone_ = -1.0f;
    int   sampleRate_    = 48000;
    int   quiet_         = 0; // samples since the input or output last had any level
    int   tail_          = 0; // after this many quiet samples the reverb stops computing
};
