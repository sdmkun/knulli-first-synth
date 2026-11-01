#pragma once

// Built-in wavetables. Each table has kFrames single-cycle frames that the
// synth's POS parameter morphs through. Every frame is stored band-limited at
// kMips levels (level m keeps harmonics up to 512 >> m) so high notes don't alias.

namespace wt {

constexpr int kTables = 7;
constexpr int kFrames = 8;
constexpr int kSize   = 1024; // samples per frame
constexpr int kMips   = 9;

// Tables are built on first use (a few milliseconds) and never change afterwards.
const char* tableName(int table);

// One frame at one band-limit level; kSize + 1 samples (the last repeats the first).
const float* frame(int table, int frame, int mip);

// The mip level to use for a note of `freq` Hz at `sampleRate`.
int mipFor(float freq, float sampleRate);

// Reads a table at `phase` (0..1) and `pos` (0..1 across the frames), interpolating both ways.
float read(int table, int mip, float pos, float phase);

void build(); // optional: build now instead of on first use

} // namespace wt
