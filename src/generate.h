#pragma once

#include <cstdint>
#include <string>

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
    float     dynamics  = 0.5f; // 0 = every hit at the default velocity, 1 = strong accents and quiet ghosts
    uint32_t  seed      = 1;
};

// Replaces every drum row of `p` (its length is kept).
void generateDrums(Pattern& p, const DrumGen& g);

// Replaces one drum row with an evenly spread (Euclidean) rhythm; hits already there keep their velocity.
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
    float    length  = 0.3f; // 0 = staccato, 1 = long notes (placed as OFFs; a note never outlasts the next)
    float    chord   = 0.0f; // chance a note becomes a triad
    float    dynamics = 0.4f; // how much velocity follows the beat (and varies)
    uint32_t seed    = 1;
};

// Replaces the pattern with a scale-locked line.
void generateMelody(Pattern& p, const MelodyGen& g, const ScaleInfo& s);

// Chords in the key, a bit like Ableton's Expressive Chords: a progression of
// scale degrees, stacked in thirds with extensions as COMPLEX goes up, voiced
// close / open / spread and moved by the nearest inversion, then played in a rhythm.
enum class ChordRhythm { Hold, Beats, Offbeat, Stabs, Eighths, Random, Count };
enum class ChordVoicing { Close, Open, Spread, Count };
enum class ChordNotes { Full, FullBass, Root, Count }; // ROOT: one note a chord, for chord-stab samples
const char* chordRhythmName(ChordRhythm r);
const char* chordVoicingName(ChordVoicing v);
const char* chordNotesName(ChordNotes n);
const char* chordProgName(int prog);
int         chordProgCount();
std::string chordProgText(int prog, const ScaleInfo& s); // "A- F C G" in the key (empty for RANDOM)
constexpr int kChordChanges[] = {4, 8, 16, 32}; // steps per chord

struct ChordGen {
    int          prog     = 0;
    int          change   = 2;    // index into kChordChanges
    int          octave   = 4;
    float        complex  = 0.4f; // 0 = triads .. 1 = seven-note chords and suspensions
    ChordVoicing voicing  = ChordVoicing::Close;
    ChordNotes   notes    = ChordNotes::Full;
    ChordRhythm  rhythm   = ChordRhythm::Hold;
    float        gate     = 0.9f; // how much of the time to the next hit a chord sounds
    float        strum    = 0.0f; // spreads the notes of each chord over up to a step, low to high
    float        dynamics = 0.4f;
    uint32_t     seed     = 1;
};

void generateChords(Pattern& p, const ChordGen& g, const ScaleInfo& s);
// Keeps the chords and changes how they are played: accents, a dropped hit, an added one.
void mutateChords(Pattern& p, uint32_t seed, float amount);

// Small changes that keep the character: `amount` 0..1.
void mutateDrums(Pattern& p, uint32_t seed, float amount);
void mutateMelody(Pattern& p, const ScaleInfo& s, uint32_t seed, float amount);

// Nearest scale tone above (dir > 0) or below (dir < 0) `pitch`.
int scaleStep(int pitch, int dir, const ScaleInfo& s);
