// INSTR: the sound of the selected track. Synth tracks list the preset and every
// synth parameter; drum tracks list the kit and a table of per-pad settings,
// with the selected pad's waveform and its START / END points on the right.

#include <algorithm>
#include <cmath>

#include "app.h"
#include "gfx.h"

namespace {

enum { ROW_TYPE, ROW_SOURCE, ROW_FIRST }; // TYPE, PRESET / KIT, then parameters or pads

constexpr int kTop     = 72;
constexpr int kRightX  = 640;
constexpr int kRightW  = kW - kRightX - 16;

int rowCount(const App& a) { return ROW_FIRST + (a.isDrum[a.track] ? kDrumPads : int(P_COUNT)); }

// Keeps the cursor valid after the track or its type changed.
Cursor& cursor(App& a) {
    Cursor& c = a.instCur;
    c.row     = std::clamp(c.row, 0, rowCount(a) - 1);
    c.col     = a.isDrum[a.track] && c.row >= ROW_FIRST ? std::clamp(c.col, 0, 3) : 0;
    return c;
}

// ---------------------------------------------------------------- synth

constexpr int kSynthRowH = 34;

void drawSynth(App& a, const Cursor& c) {
    SDL_Renderer* r = a.renderer;
    const int     t = a.track;
    Params        p;
    {
        AudioLock lock(a.audio);
        p = a.synths[t].params;
    }
    drawField(r, 40, kTop, "TYPE", "SYNTH", c.row == ROW_TYPE, 230);
    drawField(r, 40, kTop + kSynthRowH, "PRESET", trackName(a, t), c.row == ROW_SOURCE, 230);
    for (int id = 0; id < P_COUNT; ++id) {
        int  row = ROW_FIRST + id;
        int  y   = kTop + row * kSynthRowH;
        bool sel = c.row == row;
        drawField(r, 40, y, kParamNames[id], synthParamText(p, id), sel, 230);
        drawBar(r, 430, y + 3, 150, 10, synthParamNorm(p, id), sel ? kAccent : kDim);
    }

    fillRect(r, kRightX, kTop - 6, kRightW, 300, kPanel);
    drawText(r, kRightX + 12, kTop + 4, 2, "OUTPUT", kDim);
    drawScope(a, kRightX + 8, kTop + 30, kRightW - 16, 256);
    int y = kTop + 320;
    drawText(r, kRightX + 12, y, 2, fmt("A: PLAY %s", noteName(a.lastNote[t]).c_str()), kDim);
    drawText(r, kRightX + 12, y + 24, 2, "A+UP/DN: COARSE", kDim);
    drawText(r, kRightX + 12, y + 48, 2, "B+A: BACK TO PRESET", kDim);
}

void editSynth(App& a, const Cursor& c, int fine, int coarse) {
    const int t = a.track;
    if (c.row == ROW_SOURCE) {
        loadPreset(a, t, a.preset[t] + fine + coarse);
        return;
    }
    AudioLock lock(a.audio);
    adjustSynthParam(a.synths[t].params, c.row - ROW_FIRST, fine, coarse);
    a.edited[t] = true;
}

// ---------------------------------------------------------------- drums

constexpr int kPadRowY = kTop + 96;
constexpr int kPadRowH = 52;
const char*   kPadCols[4] = {"PITCH", "START", "END", "VOL"};
constexpr int kPadColX[4] = {230, 330, 430, 530};

std::string padValue(const DrumKit::PadSettings& s, int col) {
    switch (col) {
    case 0:  return fmt("%+d", s.pitch);
    case 1:  return fmt("%d%%", int(s.start * 100.0f + 0.5f));
    case 2:  return fmt("%d%%", int(s.end * 100.0f + 0.5f));
    default: return fmt("%d%%", int(s.volume * 100.0f + 0.5f));
    }
}

// Min / max outline of the sample, with the playable part between START and END lit.
void drawWave(App& a, const Sample& smp, const DrumKit::PadSettings& s, int x, int y, int w, int h) {
    SDL_Renderer* r = a.renderer;
    const int     n = int(smp.data.size());
    if (n == 0) {
        drawTextCentered(r, x + w / 2, y + h / 2 - 8, 2, "NO SAMPLE", kDim);
        return;
    }
    const int on = int(s.start * w), off = int(s.end * w);
    for (int i = 0; i < w; ++i) {
        int   from = int(int64_t(i) * n / w), to = std::max(from + 1, int(int64_t(i + 1) * n / w));
        float lo = 0.0f, hi = 0.0f;
        for (int k = from; k < to && k < n; ++k) {
            lo = std::min(lo, smp.data[k]);
            hi = std::max(hi, smp.data[k]);
        }
        SDL_Color col = i >= on && i <= off ? kScopeC : kLine;
        SDL_SetRenderDrawColor(r, col.r, col.g, col.b, 255);
        SDL_RenderDrawLine(r, x + i, y + h / 2 - int(hi * (h / 2 - 2)), x + i, y + h / 2 - int(lo * (h / 2 - 2)));
    }
    fillRect(r, x + on, y, 2, h, kPlayCol);
    fillRect(r, x + std::min(off, w - 2), y, 2, h, kRecCol);
}

void drawDrums(App& a, const Cursor& c) {
    SDL_Renderer* r = a.renderer;
    const int     t = a.track;
    DrumKit::PadSettings pads[kDrumPads];
    {
        AudioLock lock(a.audio);
        std::copy(a.drums[t].pads, a.drums[t].pads + kDrumPads, pads);
    }
    drawField(r, 40, kTop, "TYPE", "DRUM", c.row == ROW_TYPE, 230);
    std::string kit = a.kits.empty() ? "-" : a.kits[a.kit[t]].name;
    drawField(r, 40, kTop + 34, "KIT", kit, c.row == ROW_SOURCE, 230);

    drawText(r, 40, kPadRowY - 26, 2, "PAD", kDim);
    for (int col = 0; col < 4; ++col) drawText(r, kPadColX[col], kPadRowY - 26, 2, kPadCols[col], kDim);
    for (int pad = 0; pad < kDrumPads; ++pad) {
        int  row    = ROW_FIRST + pad;
        int  y      = kPadRowY + pad * kPadRowH;
        bool curRow = c.row == row;
        fillRect(r, 24, y, 600, kPadRowH - 6, curRow ? kPanel2 : kPanel);
        std::string name = fmt("%d %s", pad + 1, padName(a, t, pad).c_str());
        if (name.size() > 13) name.resize(13);
        drawText(r, 36, y + 15, 2, name, curRow ? kText : kDim);
        for (int col = 0; col < 4; ++col) {
            bool        cur = curRow && c.col == col;
            std::string v   = padValue(pads[pad], col);
            if (cur) fillRect(r, kPadColX[col] - 8, y + 6, 88, kPadRowH - 18, kCursorBg);
            drawText(r, kPadColX[col], y + 15, 2, v, cur ? kAccent : kText);
        }
    }

    // The selected pad's sample (or pad 1 while on TYPE / KIT).
    int pad = c.row >= ROW_FIRST ? c.row - ROW_FIRST : 0;
    fillRect(r, kRightX, kTop - 6, kRightW, 300, kPanel);
    drawText(r, kRightX + 12, kTop + 4, 2, fmt("PAD %d %s", pad + 1, padName(a, t, pad).c_str()), kDim);
    if (!a.kits.empty()) {
        const Sample& smp = a.kits[a.kit[t]].pads[pad];
        drawWave(a, smp, pads[pad], kRightX + 8, kTop + 30, kRightW - 16, 200);
        if (!smp.data.empty())
            drawText(r, kRightX + 12, kTop + 246, 2,
                     fmt("%dMS  %dHZ", int(smp.data.size() * 1000.0 / smp.rate), smp.rate), kDim);
    }
    int y = kTop + 320;
    drawText(r, kRightX + 12, y, 2, "A: PLAY PAD", kDim);
    drawText(r, kRightX + 12, y + 24, 2, "A+UP/DN: COARSE", kDim);
    drawText(r, kRightX + 12, y + 48, 2, "B+A: RESET", kDim);
    drawText(r, kRightX + 12, y + 72, 2, "SAMPLES/KITS/<KIT>/*.WAV", kDim);
}

void editDrums(App& a, const Cursor& c, int fine, int coarse) {
    const int t = a.track;
    if (c.row == ROW_SOURCE) {
        loadKit(a, t, a.kit[t] + fine + coarse);
        return;
    }
    AudioLock lock(a.audio);
    DrumKit::PadSettings& s = a.drums[t].pads[c.row - ROW_FIRST];
    switch (c.col) {
    case 0: s.pitch = std::clamp(s.pitch + fine + coarse * 12, -24, 24); break;
    case 1: s.start = std::clamp(s.start + fine * 0.01f + coarse * 0.1f, 0.0f, s.end - 0.01f); break;
    case 2: s.end = std::clamp(s.end + fine * 0.01f + coarse * 0.1f, s.start + 0.01f, 1.0f); break;
    case 3: s.volume = std::clamp(s.volume + fine * 0.02f + coarse * 0.1f, 0.0f, 1.0f); break;
    }
}

// ---------------------------------------------------------------- screen ops

void draw(App& a) {
    const Cursor& c = cursor(a);
    if (a.isDrum[a.track]) drawDrums(a, c);
    else drawSynth(a, c);
}

void move(App& a, int dx, int dy) {
    Cursor& c = cursor(a);
    c.row     = std::clamp(c.row + dy, 0, rowCount(a) - 1);
    if (a.isDrum[a.track] && c.row >= ROW_FIRST) c.col = std::clamp(c.col + dx, 0, 3);
}

void edit(App& a, int fine, int coarse) {
    const Cursor& c = cursor(a);
    if (c.row == ROW_TYPE) {
        setTrackType(a, a.track, !a.isDrum[a.track]);
        if (a.isDrum[a.track]) a.message = "TRACK IS NOW DRUMS (SEL+B: UNDO PATTERN)";
        return;
    }
    if (a.isDrum[a.track]) editDrums(a, c, fine, coarse);
    else editSynth(a, c, fine, coarse);
}

void enter(App& a) {
    const Cursor& c = cursor(a);
    const int     t = a.track;
    AudioLock     lock(a.audio);
    if (!a.isDrum[t]) preview(a, t, a.lastNote[t]);
    else preview(a, t, kDrumBase + (c.row >= ROW_FIRST ? c.row - ROW_FIRST : 0));
}

void clear(App& a) {
    const Cursor& c = cursor(a);
    const int     t = a.track;
    if (c.row < ROW_FIRST) return;
    AudioLock lock(a.audio);
    if (a.isDrum[t]) {
        a.drums[t].pads[c.row - ROW_FIRST] = DrumKit::PadSettings{};
    } else {
        resetSynthParam(a.synths[t].params, kPresets[a.preset[t]].params, c.row - ROW_FIRST);
        a.edited[t] = true;
    }
}

} // namespace

const ScreenOps kInstrScreen = {
    "INSTR", draw, move, edit, enter, clear,
    "A:PLAY  A+PAD:EDIT  B+A:RESET  L1/R1:TRACK  SEL+<>:SCREEN  START:PLAY",
};
