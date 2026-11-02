// PATTERN: where a track is written. Synth tracks get a tracker with four
// columns of NOTE / VEL / FX; drum tracks get an 8-pad x 16-step grid with the
// cell under the cursor shown in full on the right. Generators live in the
// GENERATE popup (ui_gen.cpp).

#include <algorithm>

#include "app.h"
#include "gfx.h"

namespace {

// ---------------------------------------------------------------- shared

// Steps a note command and resets its value, or changes the value within the command's range.
void editFx(Fx& fx, uint8_t& val, int cmdDir, int valFine, int valCoarse) {
    if (cmdDir) {
        int k = int(Fx::Count);
        fx    = Fx((int(fx) + cmdDir + k) % k);
        val   = uint8_t(fxDefault(fx));
        return;
    }
    if (fx == Fx::None) return;
    int v = val;
    if (fx == Fx::Arp) v = (v & 0xF0) | ((v + valFine) & 0x0F), v = (v + valCoarse * 16) & 0xFF; // y digit, x digit
    else if (fx == Fx::Cha) v += valFine * 5 + valCoarse * 25;
    else v += valFine + valCoarse * 4;
    val = uint8_t(fxClamp(fx, v));
}

// What a command does with its value, for the help line.
std::string fxHelp(Fx fx, int v) {
    switch (fx) {
    case Fx::Ret: return fmt("RET: REPEAT EVERY %d TICKS", v);
    case Fx::Arp: return fmt("ARP: CYCLE +0 +%d +%d SEMITONES", v >> 4, v & 15);
    case Fx::Kil: return fmt("KIL: CUT AFTER %d TICKS", v);
    case Fx::Dly: return fmt("DLY: START %d TICKS LATE", v);
    case Fx::Cha: return fmt("CHA: PLAY %d%% OF THE TIME", v);
    default:      return "FX: NONE";
    }
}

// ---------------------------------------------------------------- synth tracker

// Four note columns (NOTE, VEL) and, at the right edge, the row's two commands
// (command, value), which act on every note of the row. Cursor columns: 0..7 are
// the note columns' NOTE / VEL, 8..11 the commands' CMD / VALUE.
constexpr int kSynthRowY = 102;
constexpr int kSynthRowH = 34;
constexpr int kNoteX     = 76;
constexpr int kNoteColW  = 150;
constexpr int kSub       = 2;                 // cursor sub-columns per note column
constexpr int kSubX[kSub] = {0, 56};          // NOTE, VEL
constexpr int kSubW[kSub] = {52, 24};         // cursor highlight widths (text + margin)
enum { SUB_NOTE, SUB_VEL };
constexpr int kFxX       = 690;
constexpr int kFxColW    = 160;
constexpr int kFxSubX[2] = {0, 48};           // command, value
constexpr int kFxSubW[2] = {36, 24};
constexpr int kNoteCurs  = kSynthCols * kSub; // first cursor column of the commands
constexpr int kSynthCurs = kNoteCurs + SeqStep::kFx * 2;

// Which steps of column `col` have a note sounding: from a note until the next
// note or OFF in the column, wrapping around the end of the pattern.
void soundingSteps(const Pattern& p, int col, bool* out) {
    bool on = false;
    for (int i = 0; i < p.length; ++i) { // state carried in from the end of the pattern
        int8_t e = p.steps[i].notes[col].pitch;
        if (e != SeqNote::kEmpty) on = e >= 0;
    }
    for (int i = 0; i < p.length; ++i) {
        int8_t e = p.steps[i].notes[col].pitch;
        if (e != SeqNote::kEmpty) on = e >= 0;
        out[i] = on;
    }
}

std::string noteCellText(const SeqNote& n, int sub) {
    if (!n.isNote()) {
        if (sub == SUB_NOTE) return n.pitch == SeqNote::kOff ? "OFF" : "---";
        return "-";
    }
    return sub == SUB_NOTE ? noteName(n.pitch) : std::to_string(n.vel);
}

// The row command under the cursor (when it is on one), or null. Locked.
StepFx* cursorFx(App& a) {
    const Cursor& c    = a.synthCur;
    Pattern&      p    = a.seq.patterns[a.track];
    int           step = a.page * kPageSteps + c.row;
    if (c.col < kNoteCurs || step >= p.length) return nullptr;
    return &p.steps[step].fx[(c.col - kNoteCurs) / 2];
}

void drawSynth(App& a, const Pattern& p, int playStep, bool playing) {
    SDL_Renderer* r = a.renderer;
    const Cursor& c = a.synthCur;
    bool held[kSynthCols][Pattern::kMaxSteps];
    for (int k = 0; k < kSynthCols; ++k) {
        int x = kNoteX + k * kNoteColW;
        drawText(r, x, 74, 2, fmt("N%d", k + 1), kDim);
        drawText(r, x + kSubX[SUB_VEL], 74, 2, "V", kDim);
        soundingSteps(p, k, held[k]);
    }
    for (int f = 0; f < SeqStep::kFx; ++f) drawText(r, kFxX + f * kFxColW, 74, 2, fmt("FX%d", f + 1), kDim);
    for (int row = 0; row < kPageSteps; ++row) {
        int  step   = a.page * kPageSteps + row;
        int  y      = kSynthRowY + row * kSynthRowH;
        bool inside = step < p.length;
        SDL_Color bg = !inside ? kBg : row % 4 == 0 ? kPanel2 : kPanel;
        if (inside && playing && playStep == step) bg = {44, 84, 60, 255};
        fillRect(r, 16, y, kW - 32, kSynthRowH - 3, bg);
        drawText(r, 24, y + 9, 2, fmt("%02d", step), row % 4 == 0 ? kText : kDim);
        if (!inside) continue;
        for (int k = 0; k < kSynthCols; ++k) {
            const SeqNote& n = p.steps[step].notes[k];
            // A bar left of the column shows how long each note rings.
            if (held[k][step]) fillRect(r, kNoteX + k * kNoteColW - 10, y, 4, kSynthRowH - 3, kLine);
            for (int sub = 0; sub < kSub; ++sub) {
                int  x   = kNoteX + k * kNoteColW + kSubX[sub];
                bool cur = c.row == row && c.col == k * kSub + sub;
                if (cur) fillRect(r, x - 6, y + 2, kSubW[sub] + 12, kSynthRowH - 7, kCursorBg);
                SDL_Color col = cur ? kAccent : n.isNote() ? kText
                              : n.pitch == SeqNote::kOff && sub == SUB_NOTE ? kDim : kLine;
                drawText(r, x, y + 9, 2, noteCellText(n, sub), col);
            }
        }
        fillRect(r, kFxX - 16, y, 2, kSynthRowH - 3, kLine); // the commands belong to the whole row
        for (int f = 0; f < SeqStep::kFx; ++f) {
            const StepFx& fx = p.steps[step].fx[f];
            for (int sub = 0; sub < 2; ++sub) {
                int  x   = kFxX + f * kFxColW + kFxSubX[sub];
                bool cur = c.row == row && c.col == kNoteCurs + f * 2 + sub;
                if (cur) fillRect(r, x - 6, y + 2, kFxSubW[sub] + 12, kSynthRowH - 7, kCursorBg);
                std::string text = sub == 0 ? fxName(fx.fx) : fxValueText(fx.fx, fx.val);
                drawText(r, x, y + 9, 2, text, cur ? kAccent : fx.fx != Fx::None ? kScopeC : kLine);
            }
        }
    }
    if (c.col >= kNoteCurs && c.row < kPageSteps) {
        int step = a.page * kPageSteps + c.row;
        if (step < p.length) {
            const StepFx& fx = p.steps[step].fx[(c.col - kNoteCurs) / 2];
            a.message = fx.fx == Fx::None ? "FX: A PUTS THE LAST ONE (" + std::string(fxName(a.lastFx[a.track].fx)) +
                                                " " + fxValueText(a.lastFx[a.track].fx, a.lastFx[a.track].val) +
                                                ")  A+UP/DN: LIST"
                                          : fxHelp(fx.fx, fx.val) + "  (WHOLE ROW)";
        }
    }
}

void editSynth(App& a, Pattern& p, int fine, int coarse) {
    const int     t    = a.track;
    const Cursor& c    = a.synthCur;
    int           step = a.page * kPageSteps + c.row;
    if (step >= p.length) return;
    if (StepFx* fx = cursorFx(a)) {
        if ((c.col - kNoteCurs) % 2 == 0) editFx(fx->fx, fx->val, fine, 0, 0); // up / down opens the list (see edit())
        else editFx(fx->fx, fx->val, 0, fine, coarse);
        if (fx->fx != Fx::None) a.lastFx[t] = *fx;
        return;
    }
    SeqNote& n = p.steps[step].notes[c.col / kSub];
    if (!n.isNote()) return;
    if (c.col % kSub == SUB_NOTE) {
        int pitch = n.pitch;
        if (fine) pitch = scaleStep(pitch, fine, scaleInfo(a));
        pitch   = std::clamp(pitch + coarse * 12, 0, 127);
        n.pitch = int8_t(pitch);
        a.lastNote[t] = pitch;
    } else {
        n.vel        = uint8_t(std::clamp(n.vel + fine + coarse, 1, kMaxVel));
        a.lastVel[t] = n.vel;
    }
    preview(a, t, n.pitch, n.vel);
}

// ---------------------------------------------------------------- drum grid

constexpr int kDrumRowY  = 80;
constexpr int kDrumRowH  = 66;
constexpr int kDrumCellX = 208;
constexpr int kDrumCellW = 26;
constexpr int kInfoX     = 640;

// The hit under the drum cursor, or null.
SeqNote* drumHit(App& a, Pattern& p) {
    const Cursor& c    = a.drumCur;
    int           step = a.page * kPageSteps + c.col;
    if (c.col < 0 || step >= p.length) return nullptr;
    int slot = p.steps[step].find(kDrumBase + c.row);
    return slot >= 0 ? &p.steps[step].notes[slot] : nullptr;
}

void drawDrumInfo(App& a, Pattern& p) {
    SDL_Renderer* r = a.renderer;
    const Cursor& c = a.drumCur;
    const int     x = kInfoX;
    fillRect(r, x, kDrumRowY, kW - x - 16, 8 * kDrumRowH - 6, kPanel);
    std::string pad = padName(a, a.track, c.row);
    if (c.col < 0) {
        drawText(r, x + 16, kDrumRowY + 14, 3, pad, kAccent);
        drawText(r, x + 16, kDrumRowY + 60, 2, "EUCLID ROW", kText);
        drawText(r, x + 16, kDrumRowY + 92, 2, "A+LEFT/RIGHT: HITS", kDim);
        drawText(r, x + 16, kDrumRowY + 116, 2, "A+UP/DOWN: ROTATE", kDim);
        drawText(r, x + 16, kDrumRowY + 140, 2, "A: PLAY   B+A: CLEAR ROW", kDim);
        return;
    }
    int step = a.page * kPageSteps + c.col;
    drawText(r, x + 16, kDrumRowY + 14, 3, fmt("%s  %02d", pad.c_str(), step), kAccent);
    SeqNote* n = drumHit(a, p);
    int      y = kDrumRowY + 64;
    if (!n) {
        drawText(r, x + 16, y, 2, "NO HIT  (A: PUT ONE)", kDim);
    } else {
        drawText(r, x + 16, y, 2, "VEL", kDim);
        drawText(r, x + 110, y, 2, std::to_string(n->vel), kText);
        drawBar(r, x + 150, y + 2, 150, 10, float(n->vel) / kMaxVel, kScopeC);
        drawText(r, x + 16, y + 36, 2, "FX", kDim);
        drawText(r, x + 110, y + 36, 2, std::string(fxName(n->fx)) + " " + fxValueText(n->fx, n->fxVal),
                 n->fx == Fx::None ? kLine : kScopeC);
        std::string help = fxHelp(n->fx, n->fxVal);
        size_t      cut  = help.find(':');
        drawText(r, x + 16, y + 64, 2, help.substr(cut + 2), kDim);
    }
    y = kDrumRowY + 200;
    const char* lines[] = {"A: PUT / PLAY", "A+LEFT/RIGHT: VELOCITY", "A+UP/DOWN: FX LIST", "B+UP/DOWN: FX VALUE", "B+A: DELETE", "",
                           "RET ROLL   ARP ARPEGGIO", "KIL CUT    DLY DELAY", "CHA CHANCE", "12 TICKS = 1 STEP"};
    for (const char* l : lines) {
        drawText(r, x + 16, y, 2, l, kDim);
        y += 26;
    }
}

void drawDrums(App& a, Pattern& p, int playStep, bool playing) {
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
            int  slot   = inside ? p.steps[step].find(kDrumBase + pad) : -1;
            bool cur    = curRow && c.col == i;
            SDL_Color bg = !inside ? kBg : i % 4 == 0 ? kPanel2 : kPanel;
            if (inside && playing && playStep == step) bg = {44, 84, 60, 255};
            fillRect(r, x, y, kDrumCellW - 3, kDrumRowH - 6, bg);
            if (slot >= 0) { // height follows velocity; a command shows as its initial on top
                const SeqNote& n    = p.steps[step].notes[slot];
                int            full = kDrumRowH - 22;
                int            h    = std::max(4, full * n.vel / kMaxVel);
                fillRect(r, x + 3, y + 12 + full - h, kDrumCellW - 9, h, a.seq.audible(t) ? kScopeC : kDim);
                if (n.fx != Fx::None) drawText(r, x + 8, y + 2, 1, std::string(1, fxName(n.fx)[0]), kAccent);
            }
            if (cur) {
                strokeRect(r, x, y, kDrumCellW - 3, kDrumRowH - 6, kAccent);
                strokeRect(r, x + 1, y + 1, kDrumCellW - 5, kDrumRowH - 8, kAccent);
            }
        }
    }
    drawDrumInfo(a, p);
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
    if (a.isDrum[a.track]) drawDrums(a, p, playStep, playing);
    else drawSynth(a, p, playStep, playing);
}

void move(App& a, int dx, int dy) {
    if (a.isDrum[a.track]) {
        Cursor& c = a.drumCur;
        c.col     = std::clamp(c.col + dx, -1, kPageSteps - 1);
        c.row     = std::clamp(c.row + dy, 0, kDrumPads - 1);
    } else {
        Cursor& c = a.synthCur;
        c.col     = std::clamp(c.col + dx, 0, kSynthCurs - 1);
        c.row     = std::clamp(c.row + dy, 0, kPageSteps - 1);
    }
}

// What the command list edits: the drum hit's own command, or the row command under the synth cursor. Locked.
FxRef drumFxRef(App& a) {
    SeqNote* n = drumHit(a, a.seq.patterns[a.track]);
    return n ? FxRef{&n->fx, &n->fxVal} : FxRef{};
}
FxRef synthFxRef(App& a) {
    StepFx* f = cursorFx(a);
    return f ? FxRef{&f->fx, &f->val} : FxRef{};
}

void edit(App& a, int fine, int coarse) {
    const int t = a.track;
    // A + up / down on a note command opens the list of commands (M8 style).
    if (coarse && !fine) {
        if (!a.isDrum[t] && a.synthCur.col >= kNoteCurs && (a.synthCur.col - kNoteCurs) % 2 == 0) {
            openFxPopup(a, synthFxRef);
            return;
        }
        if (a.isDrum[t] && a.drumCur.col >= 0) { // on a drum hit; left / right stays velocity
            openFxPopup(a, drumFxRef);
            return;
        }
    }
    AudioLock lock(a.audio);
    Pattern&  p = a.seq.patterns[t];
    if (!a.isDrum[t]) {
        editSynth(a, p, fine, coarse);
        return;
    }
    const Cursor& c = a.drumCur;
    if (c.col < 0) { // Euclid: left / right = hits, up / down = rotate
        int& rot = a.euclidRot[t][c.row];
        rot      = ((rot + coarse) % p.length + p.length) % p.length;
        euclidRow(p, c.row, rowHits(p, c.row) + fine, rot);
    } else if (SeqNote* n = drumHit(a, p)) {
        n->vel       = uint8_t(std::clamp(n->vel + fine + coarse, 1, kMaxVel));
        a.lastVel[t] = n->vel;
        preview(a, t, n->pitch, n->vel);
    }
}

// B + D-pad on a drum hit: left / right opens the command list, up / down changes its value.
void option(App& a, int fine, int coarse) {
    const int t = a.track;
    if (!a.isDrum[t]) return;
    if (fine) {
        openFxPopup(a, drumFxRef);
        return;
    }
    AudioLock lock(a.audio);
    if (SeqNote* n = drumHit(a, a.seq.patterns[t])) editFx(n->fx, n->fxVal, 0, 0, coarse);
}

void enter(App& a) {
    const int t = a.track;
    AudioLock lock(a.audio);
    Pattern&  p = a.seq.patterns[t];
    if (a.isDrum[t]) {
        const Cursor& c = a.drumCur;
        if (c.col < 0) {
            preview(a, t, kDrumBase + c.row);
        } else if (SeqNote* n = drumHit(a, p)) {
            preview(a, t, n->pitch, n->vel); // A never removes a hit; B + A does
        } else if (a.page * kPageSteps + c.col < p.length) {
            pushUndo(a, t);
            p.steps[a.page * kPageSteps + c.col].add(kDrumBase + c.row, a.lastVel[t]);
            preview(a, t, kDrumBase + c.row, a.lastVel[t]);
        }
        return;
    }
    const Cursor& c    = a.synthCur;
    int           step = a.page * kPageSteps + c.row;
    if (step >= p.length) return;
    if (StepFx* fx = cursorFx(a)) { // an empty command takes the last one used
        if (fx->fx == Fx::None) {
            pushUndo(a, t);
            *fx = a.lastFx[t];
        }
        return;
    }
    SeqNote& n = p.steps[step].notes[c.col / kSub];
    if (!n.isNote()) {
        pushUndo(a, t);
        n = {int8_t(a.lastNote[t]), uint8_t(a.lastVel[t]), Fx::None, 0}; // drop in the last note used
    }
    a.lastNote[t] = n.pitch;
    a.lastVel[t]  = n.vel;
    preview(a, t, n.pitch, n.vel);
}

void clear(App& a) {
    const int t = a.track;
    AudioLock lock(a.audio);
    Pattern&  p = a.seq.patterns[t];
    pushUndo(a, t);
    if (a.isDrum[t]) {
        const Cursor& c = a.drumCur;
        if (c.col < 0) euclidRow(p, c.row, 0, 0);
        else if (SeqNote* n = drumHit(a, p)) *n = SeqNote{};
        return;
    }
    const Cursor& c    = a.synthCur;
    int           step = a.page * kPageSteps + c.row;
    if (step >= p.length) return;
    if (StepFx* fx = cursorFx(a)) {
        *fx = StepFx{};
        return;
    }
    SeqNote& n = p.steps[step].notes[c.col / kSub];
    if (c.col % kSub == SUB_VEL) { // back to the default
        if (n.isNote()) n.vel = kDefaultVel;
        return;
    }
    // NOTE: a note goes, an empty cell becomes OFF, an OFF goes.
    if (n.pitch == SeqNote::kEmpty) n.pitch = SeqNote::kOff;
    else n = SeqNote{};
}

} // namespace

const ScreenOps kPatternScreen = {
    "PATTERN", draw, move, edit, enter, clear, option,
    "A:PUT  A+PAD:CHANGE  A+UP/DN ON FX:LIST  B+A:DEL/OFF  X:GEN  Y+PAD:SCREEN",
};
