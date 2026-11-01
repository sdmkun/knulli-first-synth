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

enum MelodyField { M_LEN, M_OCT, M_DENSITY, M_RANGE, M_MOTION, M_LENGTH, M_CHORD, M_DYNAMICS, M_SEED, M_COUNT };
const char* kMelodyNames[M_COUNT] = {"LEN", "OCT", "DENSITY", "RANGE", "MOTION", "LENGTH", "CHORD", "DYNAMICS", "SEED"};

enum DrumField { D_LEN, D_STYLE, D_DENSITY, D_VARIATION, D_DYNAMICS, D_SEED, D_COUNT };
const char* kDrumNames[D_COUNT] = {"LEN", "STYLE", "DENSITY", "VARIATION", "DYNAMICS", "SEED"};

int panelCount(const App& a) { return a.isDrum[a.track] ? int(D_COUNT) : int(M_COUNT); }

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
    if (a.isDrum[t]) {
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
    if (a.isDrum[t]) {
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
    drawTextRight(r, kMenuX + kMenuW - 16, kMenuY + 20, 2, fmt("T%d %s", a.track + 1, a.isDrum[a.track] ? "DRUM" : "MELODY"), kDim);
    int y = kMenuY + 64;
    for (int i = 0; i < ACT_COUNT; ++i, y += 40) {
        bool sel = a.genRow == i;
        if (sel) fillRect(r, kMenuX + 8, y - 6, kMenuW - 16, 26, kCursorBg);
        drawText(r, kMenuX + 16, y, 2, std::string("> ") + kActNames[i], sel ? kAccent : kText);
        drawTextRight(r, kMenuX + kMenuW - 16, y, 2, i == ACT_SEED ? "A / X" : "A / Y", kDim);
    }
    y += 10;
    for (int f = 0; f < panelCount(a); ++f, y += 40) {
        bool        sel = a.genRow == ACT_COUNT + f;
        float       bar;
        std::string v    = panelValue(a, f, bar);
        const char* name = a.isDrum[a.track] ? kDrumNames[f] : kMelodyNames[f];
        drawField(r, kMenuX + 16, y, name, v, sel, kMenuX + 150);
        if (bar >= 0.0f) drawBar(r, kMenuX + 260, y + 2, 150, 10, bar, sel ? kAccent : kDim);
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
