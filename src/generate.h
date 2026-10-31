#pragma once

#include <cstdint>

#include "sequencer.h"

// Pattern generators and mutators. Everything is deterministic for a given
// seed, so moving a setting away and back restores the same pattern.

// Drum notes: pad n plays MIDI note kDrumBase + n (matches DrumKit).
constexpr int kDrumBase = 36;
constexpr int kDrumPads = 8;

enum class DrumStyle { Four, Break, Half, Jungle, Random, Count };
const char* drumStyleName(DrumStyle s);

struct DrumGen {
    DrumStyle style     = DrumStyle::Four;
    float     density   = 0.5f; // 0 = only the backbone hits, 1 = busy
    float     variation = 0.3f; // ghost notes and end-of-bar fills
    uint32_t  seed      = 1;
};

// Replaces every drum row of `p` (its length is kept).
void generateDrums(Pattern& p, const DrumGen& g);

// Replaces one drum row with an evenly spread (Euclidean) rhythm.
void euclidRow(Pattern& p, int pad, int hits, int rotate);
int  rowHits(const Pattern& p, int pad);

struct ScaleInfo {
    const int* steps; // semitone offsets of the scale degrees within an octave
    int        count;
    int        root; // 0 = C
};

struct MelodyGen {
    int      octave  = 4;
    float    density = 0.5f;
    float    range   = 0.4f; // how far the line may wander (about 3 to 14 scale degrees)
    float    motion  = 0.3f; // 0 = stepwise, 1 = leaps
    float    length  = 0.3f; // 0 = staccato, 1 = long notes
    float    chord   = 0.0f; // chance a note becomes a triad
    uint32_t seed    = 1;
};

// Replaces the pattern with a scale-locked line.
void generateMelody(Pattern& p, const MelodyGen& g, const ScaleInfo& s);

// Small changes that keep the character: `amount` 0..1.
void mutateDrums(Pattern& p, uint32_t seed, float amount);
void mutateMelody(Pattern& p, const ScaleInfo& s, uint32_t seed, float amount);

// Nearest scale tone above (dir > 0) or below (dir < 0) `pitch`.
int scaleStep(int pitch, int dir, const ScaleInfo& s);
