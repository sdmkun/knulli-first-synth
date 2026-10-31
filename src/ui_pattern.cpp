// PATTERN: where a track is written. Synth tracks get a tracker with four
// note / length column pairs; drum tracks get an 8-pad x 16-step grid. The
// panel on the right holds the track's generator settings: changing one
// rebuilds the pattern on the spot.

#include <algorithm>

#include "app.h"
#include "gfx.h"

namespace {

constexpr int kPanelX = 640;
constexpr int kTop    = 66;

// ---------------------------------------------------------------- generator panels

enum MelodyField { M_LEN, M_OCT, M_DENSITY, M_RANGE, M_MOTION, M_LENGTH, M_CHORD, M_SEED, M_COUNT };
const char* kMelodyNames[M_COUNT] = {"LEN", "OCT", "DENSITY", "RANGE", "MOTION", "LENGTH", "CHORD", "SEED"};

enum DrumField { D_LEN, D_STYLE, D_DENSITY, D_VARIATION, D_SEED, D_COUNT };
const char* kDrumNames[D_COUNT] = {"LEN", "STYLE", "DENSITY", "VARIATION", "SEED"};

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
    case M_SEED:    return std::to_string(g.seed);
    }
    return "";
}

void drawPanel(App& a, int selRow) {
    SDL_Renderer* r = a.renderer;
    fillRect(r, kPanelX, kTop, kW - kPanelX - 16, 584, kPanel);
    drawText(r, kPanelX + 16, kTop + 14, 3, "GENERATE", kAccent);
    const int n = panelCount(a);
    for (int f = 0; f < n; ++f) {
        int         y = kTop + 64 + f * 44;
        float       bar;
        std::string v = panelValue(a, f, bar);
        const char* name = a.isDrum[a.track] ? kDrumNames[f] : kMelodyNames[f];
        drawField(r, kPanelX + 16, y, name, v, f == selRow, kPanelX + 150);
        if (bar >= 0.0f) drawBar(r, kPanelX + 250, y + 2, 100, 10, bar, f == selRow ? kAccent : kDim);
    }
    int y = kTop + 64 + n * 44 + 20;
    drawText(r, kPanelX + 16, y, 2, "X: NEW SEED", kDim);
    drawText(r, kPanelX + 16, y + 24, 2, "Y: MUTATE", kDim);
    drawText(r, kPanelX + 16, y + 48, 2, "SEL+B: UNDO / REDO", kDim);
    drawText(r, kPanelX + 16, y + 72, 2, "SEL+X: CLEAR TRACK", kDim);
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
        default: return;
        }
    }
    regenerate(a, t);
}

// ---------------------------------------------------------------- synth tracker

constexpr int kSynthRowY = 102;
constexpr int kSynthRowH = 34;
constexpr int kNoteX     = 76;
constexpr int kNoteColW  = 136;
constexpr int kPanelCol  = kSynthCols * 2; // cursor column of the generator panel

void drawSynth(App& a, const Pattern& p, int playStep, bool playing) {
    SDL_Renderer* r = a.renderer;
    const Cursor& c = a.synthCur;
    for (int k = 0; k < kSynthCols; ++k) {
        drawText(r, kNoteX + k * kNoteColW, 74, 2, fmt("NOTE%d", k + 1), kDim);
        drawText(r, kNoteX + k * kNoteColW + 76, 74, 2, "LEN", kDim);
    }
    for (int row = 0; row < kPageSteps; ++row) {
        int step = a.page * kPageSteps + row;
        int y    = kSynthRowY + row * kSynthRowH;
        bool inside = step < p.length;
        SDL_Color bg = !inside ? kBg : row % 4 == 0 ? kPanel2 : kPanel;
        if (inside && playing && playStep == step) bg = {44, 84, 60, 255};
        fillRect(r, 16, y, kPanelX - 28, kSynthRowH - 3, bg);
        drawText(r, 24, y + 9, 2, fmt("%02d", step), row % 4 == 0 ? kText : kDim);
        if (!inside) continue;
        for (int k = 0; k < kSynthCols; ++k) {
            const SeqNote& n  = p.steps[step].notes[k];
            for (int part = 0; part < 2; ++part) {
                int  x   = kNoteX + k * kNoteColW + part * 76;
                bool cur = c.row == row && c.col == k * 2 + part;
                if (cur) fillRect(r, x - 6, y + 2, part ? 44 : 64, kSynthRowH - 7, kCursorBg);
                std::string s = n.pitch < 0 ? (part ? "--" : "---")
                                            : (part ? fmt("%02d", n.len) : noteName(n.pitch));
                drawText(r, x, y + 9, 2, s, cur ? kAccent : n.pitch < 0 ? kLine : kText);
            }
        }
    }
}

// ---------------------------------------------------------------- drum grid

constexpr int kDrumRowY  = 80;
constexpr int kDrumRowH  = 66;
constexpr int kDrumCellX = 208;
constexpr int kDrumCellW = 26;
constexpr int kDrumPanelCol = kPageSteps; // cursor column of the generator panel

void drawDrums(App& a, const Pattern& p, int playStep, bool playing) {
    SDL_Renderer* r = a.renderer;
    const Cursor& c = a.drumCur;
    const int     t = a.track;
    for (int pad = 0; pad < kDrumPads; ++pad) {
        int  y      = kDrumRowY + pad * kDrumRowH;
        bool curRow = c.row == pad;
        bool label  = curRow && c.col < 0;
        fillRect(r, 16, y, 184, kDrumRowH - 6, label ? kCursorBg : kPanel);
        std::string name = padName(a, t, pad);
        if (name.size() > 12) name.resize(12);
        drawText(r, 26, y + 10, 2, name, label ? kAccent : curRow ? kText : kDim);
        drawText(r, 26, y + 34, 2, fmt("E%d R%d", rowHits(p, pad), a.euclidRot[t][pad]), kLine);

        for (int i = 0; i < kPageSteps; ++i) {
            int  step   = a.page * kPageSteps + i;
            int  x      = kDrumCellX + i * kDrumCellW;
            bool inside = step < p.length;
            bool hit    = inside && p.steps[step].find(kDrumBase + pad) >= 0;
            bool cur    = curRow && c.col == i;
            SDL_Color bg = !inside ? kBg : i % 4 == 0 ? kPanel2 : kPanel;
            if (inside && playing && playStep == step) bg = {44, 84, 60, 255};
            fillRect(r, x, y, kDrumCellW - 3, kDrumRowH - 6, bg);
            if (hit) fillRect(r, x + 3, y + 8, kDrumCellW - 9, kDrumRowH - 22, a.seq.audible(t) ? kScopeC : kDim);
            if (cur) {
                strokeRect(r, x, y, kDrumCellW - 3, kDrumRowH - 6, kAccent);
                strokeRect(r, x + 1, y + 1, kDrumCellW - 5, kDrumRowH - 8, kAccent);
            }
        }
    }
}

// ---------------------------------------------------------------- screen ops

void draw(App& a) {
    Pattern p;
    int     playStep;
    bool    playing;
    {
        AudioLock lock(a.audio);
        p        = a.seq.patterns[a.track];
        playStep = a.seq.currentStep(a.track);
        playing  = a.seq.playing();
    }
    if (a.isDrum[a.track]) {
        drawDrums(a, p, playStep, playing);
        drawPanel(a, a.drumCur.col == kDrumPanelCol ? a.drumCur.row : -1);
    } else {
        drawSynth(a, p, playStep, playing);
        drawPanel(a, a.synthCur.col == kPanelCol ? a.synthCur.row : -1);
    }
}

void move(App& a, int dx, int dy) {
    if (a.isDrum[a.track]) {
        Cursor& c = a.drumCur;
        c.col     = std::clamp(c.col + dx, -1, kDrumPanelCol);
        int rows  = c.col == kDrumPanelCol ? D_COUNT : kDrumPads;
        c.row     = std::clamp(c.row + dy, 0, rows - 1);
    } else {
        Cursor& c = a.synthCur;
        c.col     = std::clamp(c.col + dx, 0, kPanelCol);
        int rows  = c.col == kPanelCol ? M_COUNT : kPageSteps;
        c.row     = std::clamp(c.row + dy, 0, rows - 1);
    }
}

void edit(App& a, int fine, int coarse) {
    const int t = a.track;
    AudioLock lock(a.audio);
    Pattern&  p = a.seq.patterns[t];
    if (a.isDrum[t]) {
        const Cursor& c = a.drumCur;
        if (c.col == kDrumPanelCol) {
            editPanel(a, c.row, fine, coarse);
        } else if (c.col < 0) { // Euclid: left / right = hits, up / down = rotate
            int& rot = a.euclidRot[t][c.row];
            rot      = ((rot + coarse) % p.length + p.length) % p.length;
            euclidRow(p, c.row, rowHits(p, c.row) + fine, rot);
        }
        return;
    }
    const Cursor& c = a.synthCur;
    if (c.col == kPanelCol) {
        editPanel(a, c.row, fine, coarse);
        return;
    }
    int step = a.page * kPageSteps + c.row;
    if (step >= p.length) return;
    SeqNote& n = p.steps[step].notes[c.col / 2];
    if (n.pitch < 0) return;
    if (c.col % 2 == 0) {
        int pitch = n.pitch;
        if (fine) pitch = scaleStep(pitch, fine, scaleInfo(a));
        pitch   = std::clamp(pitch + coarse * 12, 0, 127);
        n.pitch = int8_t(pitch);
        a.lastNote[t] = pitch;
        preview(a, t, pitch);
    } else {
        n.len = uint8_t(std::clamp(n.len + fine + coarse * 4, 1, Pattern::kMaxSteps));
    }
}

void enter(App& a) {
    const int t = a.track;
    AudioLock lock(a.audio);
    Pattern&  p = a.seq.patterns[t];
    pushUndo(a, t); // A starts an edit gesture; undo returns to before it
    if (a.isDrum[t]) {
        const Cursor& c = a.drumCur;
        if (c.col < 0) {
            preview(a, t, kDrumBase + c.row);
        } else if (c.col < kDrumPanelCol) {
            int step = a.page * kPageSteps + c.col;
            if (step >= p.length) return;
            SeqStep& st = p.steps[step];
            if (st.find(kDrumBase + c.row) >= 0) {
                st.remove(kDrumBase + c.row);
            } else {
                st.add(kDrumBase + c.row, 1);
                preview(a, t, kDrumBase + c.row);
            }
        }
        return;
    }
    const Cursor& c = a.synthCur;
    if (c.col == kPanelCol) return;
    int step = a.page * kPageSteps + c.row;
    if (step >= p.length) return;
    SeqNote& n = p.steps[step].notes[c.col / 2];
    if (n.pitch < 0) n = {int8_t(a.lastNote[t]), 1, false}; // drop in the last note used
    a.lastNote[t] = n.pitch;
    preview(a, t, n.pitch);
}

void clear(App& a) {
    const int t = a.track;
    AudioLock lock(a.audio);
    Pattern&  p = a.seq.patterns[t];
    pushUndo(a, t);
    if (a.isDrum[t]) {
        const Cursor& c = a.drumCur;
        if (c.col == kDrumPanelCol) resetPanel(a, c.row);
        else if (c.col < 0) euclidRow(p, c.row, 0, 0);
        else if (a.page * kPageSteps + c.col < p.length) p.steps[a.page * kPageSteps + c.col].remove(kDrumBase + c.row);
        return;
    }
    const Cursor& c = a.synthCur;
    if (c.col == kPanelCol) {
        resetPanel(a, c.row);
        return;
    }
    int step = a.page * kPageSteps + c.row;
    if (step < p.length) p.steps[step].notes[c.col / 2] = SeqNote{};
}

} // namespace

const ScreenOps kPatternScreen = {
    "PATTERN", draw, move, edit, enter, clear,
    "A:PUT/HIT  A+PAD:CHANGE  B+A:DELETE  X:GEN Y:MUTATE  L1/R1:TRACK  L2/R2:PAGE",
};
