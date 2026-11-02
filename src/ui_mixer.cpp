// MIXER: the four tracks' volumes with level meters, and each track's EVOLVE
// settings (keep mutating while it plays). Reached from SEQ with SELECT + down.

#include <algorithm>
#include <cmath>

#include "app.h"
#include "gfx.h"

namespace {

enum { R_VOLUME, R_EVOLVE, R_AMOUNT, R_MODE, R_GROOVE, R_COUNT }; // R_GROOVE spans all tracks

constexpr int kGridX   = 76;
constexpr int kColW    = 236;
constexpr int kTitleY  = 70;
constexpr int kFaderY  = 136;
constexpr int kFaderH  = 240;
constexpr int kFieldY  = 420;
constexpr int kGrooveY = 570;
enum { G_NAME, G_TIMING, G_VEL, G_COUNT }; // fields of the groove row
constexpr int kFieldH  = 40;

Cursor& cursor(App& a) {
    Cursor& c = a.mixCur;
    c.col     = a.track; // the column follows the selected track, so L1 / R1 move it too
    c.row     = std::clamp(c.row, 0, R_COUNT - 1);
    return c;
}

std::string everyText(int every) { return every ? fmt("%d LOOP%s", every, every > 1 ? "S" : "") : "OFF"; }

// Level in dB below full scale mapped onto 0..1 (-48 dB at the bottom).
float meterNorm(float peak) {
    if (peak <= 0.0f) return 0.0f;
    return std::clamp(1.0f + 20.0f * std::log10(peak) / 48.0f, 0.0f, 1.0f);
}

void draw(App& a) {
    SDL_Renderer* r = a.renderer;
    const Cursor& c = cursor(a);
    float    vol[kTracks], peak[kTracks];
    Evolve   evo[kTracks];
    int      loops[kTracks];
    bool     playing;
    {
        AudioLock lock(a.audio);
        for (int t = 0; t < kTracks; ++t) {
            vol[t]          = a.seq.volume[t];
            peak[t]         = a.seq.level[t];
            a.seq.level[t]  = 0.0f;
            loops[t]        = a.seq.loops(t);
            evo[t].every    = a.evolve[t].every;
            evo[t].amount   = a.evolve[t].amount;
            evo[t].vary     = a.evolve[t].vary;
            evo[t].count    = a.evolve[t].count;
        }
        playing = a.seq.playing();
    }

    for (int t = 0; t < kTracks; ++t) {
        // Meters fall about 20 dB a second.
        a.meter[t] = std::max(meterNorm(peak[t]), a.meter[t] - 0.007f);

        const int  x   = kGridX + t * kColW;
        const bool sel = t == c.col && c.row != R_GROOVE;
        fillRect(r, x, kTitleY, kColW - 6, 48, sel ? kPanel2 : kPanel);
        if (sel) fillRect(r, x, kTitleY + 46, kColW - 6, 2, kAccent);
        std::string name = fmt("%d %s", t + 1, trackName(a, t).c_str());
        if (name.size() > 18) name.resize(18);
        drawText(r, x + 6, kTitleY + 6, 2, name, sel ? kAccent : kText);
        const char* state = a.seq.solo == t ? "SOLO" : a.seq.mute[t] ? "MUTE" : a.seq.audible(t) ? "" : "OFF";
        drawText(r, x + 6, kTitleY + 26, 2, state, a.seq.solo == t ? kAccent : kRecCol);
        if (playing) drawTextRight(r, x + kColW - 12, kTitleY + 26, 2, fmt("LOOP %d", loops[t]), kDim);

        // Fader (left) and meter (right). The tick marks 100%.
        const bool fsel = sel && c.row == R_VOLUME;
        const int  fx = x + 40, fw = 56, mx = x + 120, mw = 28;
        const int  bottom = kFaderY + kFaderH;
        fillRect(r, fx, kFaderY, fw, kFaderH, kPanel);
        int fh = int(vol[t] / Sequencer::kMaxVolume * kFaderH);
        fillRect(r, fx, bottom - fh, fw, fh, fsel ? kAccent : kDim);
        int unity = bottom - int(1.0f / Sequencer::kMaxVolume * kFaderH);
        fillRect(r, fx - 8, unity, fw + 16, 2, kText);
        fillRect(r, mx, kFaderY, mw, kFaderH, kPanel);
        int mh = int(a.meter[t] * kFaderH);
        SDL_Color mc = a.meter[t] > 0.97f ? kRecCol : a.meter[t] > 0.85f ? kAccent : kPlayCol;
        if (!a.seq.audible(t)) mc = kLine;
        fillRect(r, mx, bottom - mh, mw, mh, mc);
        if (fsel) fillRect(r, x + 4, bottom + 8, kColW - 14, 26, kCursorBg);
        drawTextCentered(r, x + (kColW - 6) / 2, bottom + 14, 2, fmt("%d%%", int(std::lround(vol[t] * 100.0f))),
                         fsel ? kAccent : kText);

        // EVOLVE settings.
        const std::string values[R_MODE + 1] = {"", everyText(kEvolveEvery[evo[t].every]),
                                             fmt("%d%%", int(std::lround(evo[t].amount * 100.0f))),
                                             evo[t].vary ? "VARY" : "DRIFT"};
        const char*       labels[R_MODE + 1] = {"", "EVOLVE", "AMOUNT", "MODE"};
        for (int row = R_EVOLVE; row <= R_MODE; ++row)
            drawField(r, x + 12, kFieldY + (row - 1) * kFieldH, labels[row], values[row], sel && c.row == row,
                      x + 100);
        int every = kEvolveEvery[evo[t].every];
        if (every > 1 && playing) // how far it is to the next change
            drawBar(r, x + 12, kFieldY + 3 * kFieldH + 2, kColW - 30, 6, float(evo[t].count) / every, kPlayCol);
    }
    // Groove (song-wide): which one, and how much of its timing and accents.
    fillRect(r, 16, kGrooveY - 8, kW - 32, 34, kPanel);
    std::string name = grooveName(a);
    if (name.size() > 30) name.resize(30);
    const bool grow = c.row == R_GROOVE, on = a.groove >= 0;
    drawField(r, 28, kGrooveY, "GROOVE", name, grow && a.mixGrooveCol == G_NAME, 120);
    drawField(r, 530, kGrooveY, "TIMING", on ? fmt("%d%%", int(std::lround(a.grooveTiming * 100.0f))) : "-",
              grow && a.mixGrooveCol == G_TIMING, 618);
    drawField(r, 720, kGrooveY, "VELOCITY", on ? fmt("%d%%", int(std::lround(a.grooveVelocity * 100.0f))) : "-",
              grow && a.mixGrooveCol == G_VEL, 832);
    drawText(r, 24, kGrooveY + 36, 2, "EVOLVE: MUTATE EVERY N LOOPS.  VARY: AROUND YOUR PATTERN  DRIFT: AWAY", kDim);
}

void move(App& a, int dx, int dy) {
    Cursor& c = cursor(a);
    if (c.row == R_GROOVE) a.mixGrooveCol = std::clamp(a.mixGrooveCol + dx, 0, G_COUNT - 1);
    else a.track = std::clamp(a.track + dx, 0, kTracks - 1);
    c.row += dy;
    cursor(a);
}

void edit(App& a, int fine, int coarse) {
    const Cursor& c = cursor(a);
    AudioLock lock(a.audio);
    Evolve& e = a.evolve[c.col];
    switch (c.row) {
    case R_VOLUME: {
        float& v = a.seq.volume[c.col];
        v        = std::clamp(v + fine * 0.01f + coarse * 0.1f, 0.0f, Sequencer::kMaxVolume);
        break;
    }
    case R_EVOLVE:
        e.every = std::clamp(e.every + fine + coarse, 0, Evolve::kChoices - 1);
        e.count = 0;
        break;
    case R_AMOUNT: e.amount = std::clamp(e.amount + fine * 0.01f + coarse * 0.1f, 0.0f, 1.0f); break;
    case R_MODE:   e.vary = !e.vary, e.hasBase = false; break;
    case R_GROOVE:
        if (a.mixGrooveCol == G_NAME) {
            const int n = int(a.grooves.size()) + 1;
            a.groove    = (a.groove + 1 + fine + coarse * 10 + n * 10) % n - 1;
            if (a.groove >= 0) {
                a.grooveTiming   = a.grooves[a.groove].timing;
                a.grooveVelocity = a.grooves[a.groove].velocity;
            }
        } else {
            float& v = a.mixGrooveCol == G_TIMING ? a.grooveTiming : a.grooveVelocity;
            v        = std::clamp(v + fine * 0.01f + coarse * 0.1f, 0.0f, 1.0f);
        }
        applyGroove(a);
        break;
    }
}

void enter(App& a) {
    const Cursor& c = cursor(a);
    if (c.row == R_GROOVE) {
        if (a.mixGrooveCol == G_NAME) openGroovePopup(a);
        return;
    }
    AudioLock lock(a.audio);
    if (c.row == R_MODE) {
        a.evolve[c.col].vary    = !a.evolve[c.col].vary;
        a.evolve[c.col].hasBase = false;
    }
}

void clear(App& a) {
    const Cursor& c = cursor(a);
    AudioLock lock(a.audio);
    Evolve& e = a.evolve[c.col];
    switch (c.row) {
    case R_VOLUME: a.seq.volume[c.col] = 1.0f; break;
    case R_EVOLVE: e.every = 0, e.count = 0; break;
    case R_AMOUNT: e.amount = Evolve{}.amount; break;
    case R_MODE:   e.vary = Evolve{}.vary, e.hasBase = false; break;
    case R_GROOVE:
        if (a.mixGrooveCol == G_NAME) a.groove = -1;
        else if (a.groove >= 0 && a.mixGrooveCol == G_TIMING) a.grooveTiming = a.grooves[a.groove].timing;
        else if (a.groove >= 0) a.grooveVelocity = a.grooves[a.groove].velocity;
        applyGroove(a);
        break;
    }
}

} // namespace

const ScreenOps kMixerScreen = {
    "MIXER", draw, move, edit, enter, clear, nullptr,
    "A+PAD:EDIT  B+A:RESET  L3/R3:MUTE/SOLO  Y:MUTATE  SEL+UP:SEQ",
};
