// GENERATE: a popup menu over any screen (X opens it) with the selected track's
// generator settings. Changing a setting rebuilds the pattern on the spot; X
// rolls a new seed and Y mutates while it is open.

#include <algorithm>

#include "app.h"
#include "gfx.h"

namespace {

constexpr int kMenuX = 560;
constexpr int kMenuY = 60;
constexpr int kMenuW = kW - kMenuX - 12;
constexpr int kMenuH = 594;

enum { ACT_SEED, ACT_MUTATE, ACT_COUNT }; // action rows above the settings
const char* kActNames[ACT_COUNT] = {"NEW SEED", "MUTATE"};

enum MelodyField { M_LEN, M_MODE, M_OCT, M_DENSITY, M_RANGE, M_MOTION, M_LENGTH, M_CHORD, M_DYNAMICS, M_SEED, M_COUNT };
const char* kMelodyNames[M_COUNT] = {"LEN", "MODE", "OCT", "DENSITY", "RANGE", "MOTION", "LENGTH", "CHORD", "DYNAMICS", "SEED"};

// Melodic tracks in CHORDS mode (field 1 is MODE in both).
enum ChordField { C_LEN, C_MODE, C_PROG, C_CHANGE, C_OCT, C_COMPLEX, C_VOICING, C_NOTES, C_RHYTHM, C_GATE, C_STRUM,
                  C_DYNAMICS, C_SEED, C_COUNT };
const char* kChordNames[C_COUNT] = {"LEN", "MODE", "PROGRESSION", "CHANGE", "OCT", "COMPLEX", "VOICING", "NOTES",
                                    "RHYTHM", "GATE", "STRUM", "DYNAMICS", "SEED"};
constexpr int kRowStep = 29;

enum DrumField { D_LEN, D_STYLE, D_DENSITY, D_VARIATION, D_DYNAMICS, D_SEED, D_COUNT };
const char* kDrumNames[D_COUNT] = {"LEN", "STYLE", "DENSITY", "VARIATION", "DYNAMICS", "SEED"};

int panelCount(const App& a) {
    return a.isDrum[a.track] ? int(D_COUNT) : a.chordMode[a.track] ? int(C_COUNT) : int(M_COUNT);
}
const char* fieldName(const App& a, int f) {
    return a.isDrum[a.track] ? kDrumNames[f] : a.chordMode[a.track] ? kChordNames[f] : kMelodyNames[f];
}
template <typename E> E stepEnum(E v, int by, int count) { return E(((int(v) + by) % count + count) % count); }

std::string pct(float v) { return fmt("%d%%", int(v * 100.0f + 0.5f)); }

// Value text and bar position (-1 = no bar) of one panel field.
std::string panelValue(const App& a, int f, float& bar) {
    const int t   = a.track;
    const int len = a.seq.patterns[t].length;
    bar           = -1.0f;
    if (a.isDrum[t]) {
        const DrumGen& g = a.drumGen[t];
        switch (f) {
        case D_LEN:       return std::to_string(len);
        case D_STYLE:     return drumStyleName(g.style);
        case D_DENSITY:   bar = g.density; return pct(g.density);
        case D_VARIATION: bar = g.variation; return pct(g.variation);
        case D_DYNAMICS:  bar = g.dynamics; return pct(g.dynamics);
        case D_SEED:      return std::to_string(g.seed);
        }
        return "";
    }
    if (f == M_MODE) return a.chordMode[t] ? "CHORDS" : "MELODY";
    if (a.chordMode[t]) {
        const ChordGen& g = a.chordGen[t];
        switch (f) {
        case C_LEN:      return std::to_string(len);
        case C_PROG:     return chordProgName(g.prog);
        case C_CHANGE:   return fmt("%d STEPS", kChordChanges[g.change]);
        case C_OCT:      return std::to_string(g.octave);
        case C_COMPLEX: {
            bar = g.complex;
            static const char* kSize[] = {"TRIADS", "7THS", "9THS", "13THS", "7 NOTES"};
            return kSize[std::clamp(int(std::lround(g.complex * 4.0f)), 0, 4)];
        }
        case C_VOICING:  return chordVoicingName(g.voicing);
        case C_NOTES:    return chordNotesName(g.notes);
        case C_RHYTHM:   return chordRhythmName(g.rhythm);
        case C_GATE:     bar = g.gate; return pct(g.gate);
        case C_STRUM:    bar = g.strum; return pct(g.strum);
        case C_DYNAMICS: bar = g.dynamics; return pct(g.dynamics);
        case C_SEED:     return std::to_string(g.seed);
        }
        return "";
    }
    const MelodyGen& g = a.melodyGen[t];
    switch (f) {
    case M_LEN:     return std::to_string(len);
    case M_OCT:     return std::to_string(g.octave);
    case M_DENSITY: bar = g.density; return pct(g.density);
    case M_RANGE:   bar = g.range; return pct(g.range);
    case M_MOTION:  bar = g.motion; return pct(g.motion);
    case M_LENGTH:  bar = g.length; return pct(g.length);
    case M_CHORD:   bar = g.chord; return pct(g.chord);
    case M_DYNAMICS: bar = g.dynamics; return pct(g.dynamics);
    case M_SEED:    return std::to_string(g.seed);
    }
    return "";
}

// Changes a generator field and rebuilds the track. Locked.
void editPanel(App& a, int f, int fine, int coarse) {
    const int t   = a.track;
    Pattern&  p   = a.seq.patterns[t];
    auto      add = [](float v, float d) { return std::clamp(v + d, 0.0f, 1.0f); };
    const float d = fine * 0.05f + coarse * 0.2f;
    if (f == 0) { // LEN: only changes the length, keeps the notes
        a.seq.setLength(t, p.length + fine + coarse * 4);
        return;
    }
    if (!a.isDrum[t] && f == M_MODE) {
        a.chordMode[t] = !a.chordMode[t];
        // A progression needs room: four chords of a bar each.
        if (a.chordMode[t] && p.length < 64) {
            a.seq.setLength(t, 64);
            a.message = fmt("T%d: CHORDS, LEN 64  (CHANGE SETS STEPS PER CHORD)", t + 1);
        }
        a.genRow       = std::min(a.genRow, ACT_COUNT + panelCount(a) - 1);
    } else if (!a.isDrum[t] && a.chordMode[t]) {
        ChordGen& g = a.chordGen[t];
        const int by = fine + coarse;
        switch (f) {
        case C_PROG:     g.prog = (g.prog + by + chordProgCount() * 4) % chordProgCount(); break;
        case C_CHANGE:   g.change = std::clamp(g.change + by, 0, 3); break;
        case C_OCT:      g.octave = std::clamp(g.octave + by, 1, 7); break;
        case C_COMPLEX:  g.complex = add(g.complex, fine * 0.05f + coarse * 0.25f); break;
        case C_VOICING:  g.voicing = stepEnum(g.voicing, by, int(ChordVoicing::Count)); break;
        case C_NOTES:    g.notes = stepEnum(g.notes, by, int(ChordNotes::Count)); break;
        case C_RHYTHM:   g.rhythm = stepEnum(g.rhythm, by, int(ChordRhythm::Count)); break;
        case C_GATE:     g.gate = add(g.gate, d); break;
        case C_STRUM:    g.strum = add(g.strum, d); break;
        case C_DYNAMICS: g.dynamics = add(g.dynamics, d); break;
        case C_SEED:     g.seed = uint32_t(std::max<int64_t>(1, int64_t(g.seed) + fine + coarse * 10)); break;
        }
    } else if (a.isDrum[t]) {
        DrumGen& g = a.drumGen[t];
        switch (f) {
        case D_STYLE: {
            int n   = int(DrumStyle::Count);
            g.style = DrumStyle((int(g.style) + fine + coarse + n) % n);
            break;
        }
        case D_DENSITY:   g.density = add(g.density, d); break;
        case D_VARIATION: g.variation = add(g.variation, d); break;
        case D_DYNAMICS:  g.dynamics = add(g.dynamics, d); break;
        case D_SEED:      g.seed = uint32_t(std::max<int64_t>(1, int64_t(g.seed) + fine + coarse * 10)); break;
        }
    } else {
        MelodyGen& g = a.melodyGen[t];
        switch (f) {
        case M_OCT:     g.octave = std::clamp(g.octave + fine + coarse, 1, 7); break;
        case M_DENSITY: g.density = add(g.density, d); break;
        case M_RANGE:   g.range = add(g.range, d); break;
        case M_MOTION:  g.motion = add(g.motion, d); break;
        case M_LENGTH:  g.length = add(g.length, d); break;
        case M_CHORD:   g.chord = add(g.chord, d); break;
        case M_DYNAMICS: g.dynamics = add(g.dynamics, d); break;
        case M_SEED:    g.seed = uint32_t(std::max<int64_t>(1, int64_t(g.seed) + fine + coarse * 10)); break;
        }
    }
    regenerate(a, t);
}

void resetPanel(App& a, int f) {
    const int t = a.track;
    if (f == 0) return;
    if (!a.isDrum[t] && f == M_MODE) return;
    if (!a.isDrum[t] && a.chordMode[t]) {
        const ChordGen def;
        ChordGen&      g    = a.chordGen[t];
        const uint32_t seed = g.seed;
        if (f != C_SEED) g = def, g.seed = seed; // B+A on a chord setting puts all of them back
    } else if (a.isDrum[t]) {
        DrumGen def;
        DrumGen& g = a.drumGen[t];
        switch (f) {
        case D_STYLE:     g.style = def.style; break;
        case D_DENSITY:   g.density = def.density; break;
        case D_VARIATION: g.variation = def.variation; break;
        case D_DYNAMICS:  g.dynamics = def.dynamics; break;
        default: return;
        }
    } else {
        MelodyGen def;
        MelodyGen& g = a.melodyGen[t];
        switch (f) {
        case M_OCT:     g.octave = def.octave; break;
        case M_DENSITY: g.density = def.density; break;
        case M_RANGE:   g.range = def.range; break;
        case M_MOTION:  g.motion = def.motion; break;
        case M_LENGTH:  g.length = def.length; break;
        case M_CHORD:   g.chord = def.chord; break;
        case M_DYNAMICS: g.dynamics = def.dynamics; break;
        default: return;
        }
    }
    regenerate(a, t);
}


} // namespace

int genRows(const App& a) { return ACT_COUNT + panelCount(a); }

void drawGenMenu(App& a) {
    SDL_Renderer* r = a.renderer;
    a.genRow        = std::clamp(a.genRow, 0, genRows(a) - 1);
    fillRect(r, kMenuX - 4, kMenuY - 4, kMenuW + 8, kMenuH + 8, kAccent);
    fillRect(r, kMenuX, kMenuY, kMenuW, kMenuH, kPanel);
    drawText(r, kMenuX + 16, kMenuY + 14, 3, "GENERATE", kAccent);
    drawTextRight(r, kMenuX + kMenuW - 16, kMenuY + 20, 2,
                  fmt("T%d %s", a.track + 1, a.isDrum[a.track] ? "DRUM" : a.chordMode[a.track] ? "CHORDS" : "MELODY"), kDim);
    int y = kMenuY + 64;
    for (int i = 0; i < ACT_COUNT; ++i, y += kRowStep) {
        bool sel = a.genRow == i;
        if (sel) fillRect(r, kMenuX + 8, y - 6, kMenuW - 16, 26, kCursorBg);
        drawText(r, kMenuX + 16, y, 2, std::string("> ") + kActNames[i], sel ? kAccent : kText);
        drawTextRight(r, kMenuX + kMenuW - 16, y, 2, i == ACT_SEED ? "A / X" : "A / Y", kDim);
    }
    y += 10;
    for (int f = 0; f < panelCount(a); ++f, y += kRowStep) {
        bool        sel = a.genRow == ACT_COUNT + f;
        float       bar;
        std::string v    = panelValue(a, f, bar);
        drawField(r, kMenuX + 16, y, fieldName(a, f), v, sel, kMenuX + 172);
        if (bar >= 0.0f) drawBar(r, kMenuX + 300, y + 2, 130, 10, bar, sel ? kAccent : kDim);
    }
    if (!a.isDrum[a.track] && a.chordMode[a.track]) { // the chords in this key, like "A- F C G"
        std::string chords = chordProgText(a.chordGen[a.track].prog, scaleInfo(a));
        drawText(r, kMenuX + 16, kMenuY + kMenuH - 84, 2, chords.empty() ? "CHORDS FROM THE SEED" : chords, kPlayCol);
    }
    drawText(r, kMenuX + 16, kMenuY + kMenuH - 52, 2, "A+PAD: CHANGE  B+A: DEFAULT", kDim);
    drawText(r, kMenuX + 16, kMenuY + kMenuH - 28, 2, "B: CLOSE    SEL+B: UNDO", kDim);
}

void genMove(App& a, int dy) { a.genRow = std::clamp(a.genRow + dy, 0, genRows(a) - 1); }

void genEdit(App& a, int fine, int coarse) {
    if (a.genRow < ACT_COUNT) return;
    AudioLock lock(a.audio);
    if (!a.genEdited) pushUndo(a, a.track); // one undo step for a run of setting changes
    a.genEdited = true;
    editPanel(a, a.genRow - ACT_COUNT, fine, coarse);
}

void genEnter(App& a) {
    if (a.genRow == ACT_SEED) newSeed(a);
    else if (a.genRow == ACT_MUTATE) mutateTrack(a);
    a.genEdited = false;
}

void genClear(App& a) {
    if (a.genRow < ACT_COUNT) return;
    AudioLock lock(a.audio);
    if (!a.genEdited) pushUndo(a, a.track);
    a.genEdited = true;
    resetPanel(a, a.genRow - ACT_COUNT);
}
