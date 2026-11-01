// MODS: the selected synth track's envelopes and LFOs, in four blocks
// (AMP ENV, ENV 2 / LFO 1, LFO 2). Reached from INSTR with SELECT + up.

#include <algorithm>
#include <vector>

#include "app.h"
#include "gfx.h"

namespace {

struct Block {
    const char* title;
    int         first, count; // ParamId range
    int         x, y;
};
const Block kBlocks[4] = {
    {"AMP ENV", P_AMP_A, 4, 24, 70},
    {"ENV 2", P_ENV_A, 6, 524, 70},
    {"LFO 1", P_L1_SHAPE, 5, 24, 360},
    {"LFO 2", P_L2_SHAPE, 5, 524, 360},
};
constexpr int kBlockW = 476;
constexpr int kBlockH = 280;
constexpr int kRowH   = 34;

Cursor& cursor(App& a) {
    Cursor& c = a.modsCur;
    c.col     = std::clamp(c.col, 0, 3);
    c.row     = std::clamp(c.row, 0, kBlocks[c.col].count - 1);
    return c;
}

// The shape of an attack / decay / sustain / release envelope, for the AMP and ENV 2 blocks.
void drawAdsr(SDL_Renderer* r, float a, float d, float s, float rel, int x, int y, int w, int h) {
    // Time-ish widths from the normalized settings, so the picture follows the knobs.
    float wa = 0.05f + a, wd = 0.05f + d, ws = 0.4f, wr = 0.05f + rel;
    float k  = w / (wa + wd + ws + wr);
    int   x1 = x + int(wa * k), x2 = x1 + int(wd * k), x3 = x2 + int(ws * k), x4 = x + w;
    int   top = y, bot = y + h, sus = bot - int(s * h);
    SDL_Point pts[5] = {{x, bot}, {x1, top}, {x2, sus}, {x3, sus}, {x4, bot}};
    SDL_SetRenderDrawColor(r, kScopeC.r, kScopeC.g, kScopeC.b, 255);
    SDL_RenderDrawLines(r, pts, 5);
}

void draw(App& a) {
    SDL_Renderer* r = a.renderer;
    if (a.isDrum[a.track]) {
        drawTextCentered(r, kW / 2, 300, 3, "DRUM TRACK", kDim);
        drawTextCentered(r, kW / 2, 340, 2, "ENVELOPES AND LFOS ARE FOR SYNTH TRACKS", kDim);
        return;
    }
    const Cursor& c = cursor(a);
    Params        p;
    {
        AudioLock lock(a.audio);
        p = a.synths[a.track].params;
    }
    for (int b = 0; b < 4; ++b) {
        const Block& bl = kBlocks[b];
        fillRect(r, bl.x - 8, bl.y - 8, kBlockW, kBlockH, kPanel);
        drawText(r, bl.x + 4, bl.y + 2, 3, bl.title, c.col == b ? kAccent : kText);
        for (int i = 0; i < bl.count; ++i)
            drawParam(r, p, bl.first + i, bl.x + 4, bl.y + 44 + i * kRowH, bl.x + 134, bl.x + 300, 150,
                      c.col == b && c.row == i);
    }
    drawAdsr(r, p.ampA, p.ampD, p.ampS, p.ampR, kBlocks[0].x + 4, kBlocks[0].y + 44 + 4 * kRowH + 16, 440, 60);
}

void move(App& a, int dx, int dy) {
    if (a.isDrum[a.track]) return;
    Cursor& c = cursor(a);
    if (dx) {
        int col = c.col % 2 + dx;
        if (col >= 0 && col <= 1) c.col = c.col / 2 * 2 + col;
    }
    if (dy) {
        int row = c.row + dy;
        if (row < 0 && c.col >= 2) { // up into the block above
            c.col -= 2;
            row = kBlocks[c.col].count - 1;
        } else if (row >= kBlocks[c.col].count && c.col < 2) { // down into the block below
            c.col += 2;
            row = 0;
        }
        c.row = row;
    }
    cursor(a);
}

int paramAt(App& a) {
    const Cursor& c = cursor(a);
    return kBlocks[c.col].first + c.row;
}

void edit(App& a, int fine, int coarse) {
    if (a.isDrum[a.track]) return;
    int       id = paramAt(a);
    AudioLock lock(a.audio);
    adjustSynthParam(a.synths[a.track].params, id, fine, coarse);
    a.edited[a.track] = true;
}

void enter(App& a) {
    if (a.isDrum[a.track]) return;
    AudioLock lock(a.audio);
    preview(a, a.track, a.lastNote[a.track]);
}

void clear(App& a) {
    if (a.isDrum[a.track]) return;
    int       id = paramAt(a);
    AudioLock lock(a.audio);
    resetSynthParam(a.synths[a.track].params, kPresets[a.preset[a.track]].params, id);
    a.edited[a.track] = true;
}

} // namespace

const ScreenOps kModsScreen = {
    "MODS", draw, move, edit, enter, clear, nullptr,
    "A:PLAY  A+PAD:EDIT  B+A:RESET  L1/R1:TRACK  SEL+DOWN:INSTR",
};
