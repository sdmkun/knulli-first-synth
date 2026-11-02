// SESSION (home): the clip bank, laid out like Ableton's session view. Each
// track is a column; the top row is what each track is playing now (LIVE), the
// rows below are clips kept from it. Every cell shows its notes as a tiny piano
// roll, and the playing ones show the play position sweeping left to right.
//
//   A on an empty slot   keeps the track's live pattern there
//   A on a clip          plays it from the next bar (or right away when stopped)
//   A on a scene button  plays the whole row; on an empty row, keeps all four
//   B + A                deletes a clip (on a scene button: the whole row)
//   SELECT + A           (any screen) keeps the current track in its first free slot

#include <algorithm>

#include "app.h"
#include "gfx.h"

namespace {

constexpr int kGridX  = 76;
constexpr int kColW   = 236;
constexpr int kCellW  = kColW - 6;
constexpr int kTitleY = 58;
constexpr int kLiveY  = 106;
constexpr int kLiveH  = 72;
constexpr int kSlotY  = 188;
constexpr int kSlotH  = 52;
constexpr int kSlotGap = 6;

const SDL_Color kTrackCol[kTracks] = {
    {255, 150, 70, 255}, {90, 200, 255, 255}, {240, 110, 200, 255}, {150, 230, 100, 255}};

SDL_Color shade(SDL_Color c, float k) {
    return {Uint8(c.r * k), Uint8(c.g * k), Uint8(c.b * k), 255};
}

// A pattern as a piano roll in (x, y, w, h). Synth notes last until the next
// note or OFF in their column; drum hits are short ticks on eight lanes.
void drawRoll(SDL_Renderer* r, const Pattern& p, bool drum, int x, int y, int w, int h, SDL_Color c) {
    const float stepW = float(w) / float(p.length);
    if (drum) {
        const float laneH = float(h) / kDrumPads;
        const int   hitW  = std::max(2, int(stepW * 0.6f));
        for (int i = 0; i < p.length; ++i)
            for (const SeqNote& n : p.steps[i].notes) {
                if (!n.isNote()) continue;
                int lane = std::clamp(n.pitch - kDrumBase, 0, kDrumPads - 1);
                int ly   = y + h - int((lane + 1) * laneH);
                fillRect(r, x + int(i * stepW), ly + 1, hitW, std::max(2, int(laneH) - 1),
                         shade(c, 0.55f + 0.45f * velocityGain(n.vel) / velocityGain(kMaxVel)));
            }
        return;
    }
    int lo = 127, hi = 0;
    for (int i = 0; i < p.length; ++i)
        for (const SeqNote& n : p.steps[i].notes)
            if (n.isNote()) lo = std::min<int>(lo, n.pitch), hi = std::max<int>(hi, n.pitch);
    if (lo > hi) return;
    if (hi - lo < 12) { // at least an octave tall, so a few notes do not fill the cell
        int mid = (lo + hi) / 2;
        lo      = mid - 6;
        hi      = lo + 12;
    }
    const float rowH  = float(h) / float(hi - lo + 1);
    const int   noteH = std::clamp(int(rowH), 2, 6);
    for (int col = 0; col < SeqStep::kMaxNotes; ++col) {
        int start = -1, pitch = 0, vel = kDefaultVel;
        for (int i = 0; i <= p.length; ++i) {
            const SeqNote* n = i < p.length ? &p.steps[i].notes[col] : nullptr;
            if (n && n->pitch == SeqNote::kEmpty) continue;
            if (start >= 0) {
                int nx = x + int(start * stepW), nw = std::max(2, int((i - start) * stepW) - 1);
                int ny = y + h - int((pitch - lo + 1) * rowH) + (int(rowH) - noteH) / 2;
                fillRect(r, nx, ny, nw, noteH, shade(c, 0.55f + 0.45f * velocityGain(vel) / velocityGain(kMaxVel)));
            }
            start = n && n->isNote() ? i : -1;
            if (n) pitch = n->pitch, vel = n->vel;
        }
    }
}

// The swept part of a playing cell and the play line.
void drawPlayhead(SDL_Renderer* r, int x, int y, int w, int h, int step, int length, SDL_Color c) {
    if (step < 0 || length <= 0) return;
    int px = x + int(float(w) * float(step + 1) / float(length));
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 40);
    SDL_Rect done{x, y, px - x, h};
    SDL_RenderFillRect(r, &done);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    fillRect(r, std::min(px, x + w - 2), y, 2, h, kText);
}

bool rowUsed(const App& a, int s) {
    for (int t = 0; t < kTracks; ++t)
        if (a.clips[t][s].used) return true;
    return false;
}

int slotY(int s) { return kSlotY + s * (kSlotH + kSlotGap); }

void draw(App& a) {
    SDL_Renderer* r = a.renderer;
    Pattern live[kTracks];
    int     step[kTracks], from[kTracks], queued[kTracks];
    bool    same[kTracks], playing;
    long    songStep;
    {
        AudioLock lock(a.audio);
        for (int t = 0; t < kTracks; ++t) {
            live[t]   = a.seq.patterns[t];
            step[t]   = a.seq.currentStep(t);
            from[t]   = a.seq.slot[t];
            queued[t] = a.seq.queuedSlot(t);
            same[t]   = clipMatchesLive(a, t, from[t]);
        }
        playing  = a.seq.playing();
        songStep = a.seq.songStep();
    }
    const Cursor& c     = a.sessCur;
    const bool    blink = (SDL_GetTicks() / 200) % 2 == 0;

    // Track titles.
    for (int t = 0; t < kTracks; ++t) {
        int  x   = kGridX + t * kColW;
        bool sel = t == a.track;
        fillRect(r, x, kTitleY, kCellW, 40, sel ? kPanel2 : kPanel);
        fillRect(r, x, kTitleY, 4, 40, kTrackCol[t]);
        std::string name = fmt("%d %s", t + 1, trackName(a, t).c_str());
        if (name.size() > 17) name.resize(17);
        drawText(r, x + 10, kTitleY + 4, 2, name, sel ? kAccent : kText);
        const char* state = a.seq.solo == t ? "SOLO" : a.seq.mute[t] ? "MUTE" : kEvolveEvery[a.evolve[t].every] ? "EVO" : "";
        SDL_Color   sc    = a.seq.solo == t ? kAccent : a.seq.mute[t] ? kRecCol : kPlayCol;
        drawText(r, x + 10, kTitleY + 22, 2, fmt("%s %d", a.isDrum[t] ? "DRUM" : "SYNTH", live[t].length), kDim);
        drawTextRight(r, x + kCellW - 8, kTitleY + 22, 2, state, sc);
    }

    // Scene buttons on the left.
    for (int row = 0; row <= kSlots; ++row) {
        int  y   = row == 0 ? kLiveY : slotY(row - 1);
        int  h   = row == 0 ? kLiveH : kSlotH;
        bool cur = c.row == row && c.col < 0;
        bool any = row == 0 || rowUsed(a, row - 1);
        fillRect(r, 12, y, 56, h, cur ? kCursorBg : kPanel);
        drawTextCentered(r, 40, y + h / 2 - 7, 2, row == 0 ? "LIVE" : fmt("%d", row), cur ? kAccent : any ? kText : kDim);
    }

    // LIVE: what each track plays now.
    for (int t = 0; t < kTracks; ++t) {
        int       x   = kGridX + t * kColW;
        bool      cur = c.row == 0 && c.col == t;
        SDL_Color tc  = a.seq.audible(t) ? kTrackCol[t] : kLine;
        fillRect(r, x, kLiveY, kCellW, kLiveH, cur ? kCursorBg : kPanel2);
        std::string src = from[t] < 0 ? "NEW" : fmt("FROM %d%s", from[t] + 1, same[t] ? "" : " *");
        drawText(r, x + 8, kLiveY + 5, 2, src, from[t] >= 0 && same[t] ? kText : kDim);
        if (queued[t] >= 0 && blink) drawTextRight(r, x + kCellW - 8, kLiveY + 5, 2, fmt("> %d", queued[t] + 1), kAccent);
        drawRoll(r, live[t], a.isDrum[t], x + 6, kLiveY + 24, kCellW - 12, kLiveH - 30, tc);
        if (playing && a.seq.audible(t)) drawPlayhead(r, x, kLiveY, kCellW, kLiveH, step[t], live[t].length, tc);
        if (cur) strokeRect(r, x, kLiveY, kCellW, kLiveH, kAccent);
    }

    // The clip bank.
    for (int s = 0; s < kSlots; ++s) {
        int y = slotY(s);
        for (int t = 0; t < kTracks; ++t) {
            int         x    = kGridX + t * kColW;
            const Clip& clip = a.clips[t][s];
            bool        cur  = c.row == s + 1 && c.col == t;
            bool        src  = from[t] == s;
            if (!clip.used) {
                fillRect(r, x, y, kCellW, kSlotH, cur ? kCursorBg : kBg);
                strokeRect(r, x, y, kCellW, kSlotH, cur ? kAccent : kPanel2);
                if (cur) drawTextCentered(r, x + kCellW / 2, y + kSlotH / 2 - 7, 2, "A: KEEP HERE", kAccent);
                continue;
            }
            SDL_Color tc = kTrackCol[t];
            fillRect(r, x, y, kCellW, kSlotH, cur ? kCursorBg : src ? shade(tc, 0.28f) : kPanel);
            fillRect(r, x, y, 4, kSlotH, src ? tc : shade(tc, 0.5f));
            drawRoll(r, clip.pat, clip.drum, x + 10, y + 4, kCellW - 16, kSlotH - 8, tc);
            if (src && same[t] && playing && a.seq.audible(t))
                drawPlayhead(r, x, y, kCellW, kSlotH, step[t], clip.pat.length, tc);
            if (queued[t] == s && blink) strokeRect(r, x, y, kCellW, kSlotH, kAccent);
            else if (cur) strokeRect(r, x, y, kCellW, kSlotH, kAccent);
        }
    }

    // Bar counter, so a queued launch can be seen coming.
    if (playing) {
        int q   = std::max(1, a.seq.launchQuant);
        int pos = int(songStep % q);
        for (int i = 0; i < 4; ++i) {
            bool on = pos * 4 / q == i;
            fillRect(r, 12 + i * 14, kTitleY + 14, 10, 10, on ? kAccent : kPanel2);
        }
    }
}

void move(App& a, int dx, int dy) {
    Cursor& c = a.sessCur;
    c.row     = std::clamp(c.row + dy, 0, kSlots);
    c.col     = std::clamp(c.col + dx, -1, kTracks - 1);
    if (c.col >= 0) a.track = c.col;
}

void edit(App&, int, int) {}

// Keeps all four live patterns in row `s`.
void keepRow(App& a, int s) {
    for (int t = 0; t < kTracks; ++t) keepClip(a, t, s);
    a.message = fmt("ALL TRACKS KEPT IN ROW %d", s + 1);
}

void enter(App& a) {
    const Cursor& c = a.sessCur;
    if (c.row == 0) {
        if (c.col >= 0) { // open the pattern
            a.track        = c.col;
            a.screen       = SCR_PATTERN;
            a.synthCur.row = 0;
            a.drumCur.col  = 0;
            return;
        }
        for (int s = 0; s < kSlots; ++s)
            if (!rowUsed(a, s)) {
                keepRow(a, s);
                saveProject(a, true);
                return;
            }
        a.message = "NO EMPTY ROW  (B+A ON A ROW NUMBER DELETES IT)";
        return;
    }
    const int s = c.row - 1;
    if (c.col < 0) {
        if (!rowUsed(a, s)) {
            keepRow(a, s);
            saveProject(a, true);
            return;
        }
        for (int t = 0; t < kTracks; ++t)
            if (a.clips[t][s].used) launchClip(a, t, s);
        a.message = fmt("ROW %d: PLAYS FROM THE NEXT BAR", s + 1);
        return;
    }
    const int t = c.col;
    if (!a.clips[t][s].used) {
        if (keepClip(a, t, s) >= 0) saveProject(a, true);
        return;
    }
    launchClip(a, t, s);
    if (a.clips[t][s].drum == a.isDrum[t]) // otherwise launchClip said why not
        a.message = fmt("T%d: SLOT %d PLAYS FROM THE NEXT BAR  (SEL+B: BACK)", t + 1, s + 1);
}

void deleteClip(App& a, int t, int s) {
    a.clips[t][s] = Clip{};
    AudioLock lock(a.audio);
    if (a.seq.slot[t] == s) a.seq.slot[t] = -1;
    if (a.seq.queuedSlot(t) == s) a.seq.cancelQueue(t);
}

void clear(App& a) {
    const Cursor& c = a.sessCur;
    if (c.row == 0) return;
    const int s = c.row - 1;
    if (c.col < 0) {
        if (!rowUsed(a, s)) return;
        for (int t = 0; t < kTracks; ++t) deleteClip(a, t, s);
        a.message = fmt("ROW %d DELETED", s + 1);
    } else {
        if (!a.clips[c.col][s].used) return;
        deleteClip(a, c.col, s);
        a.message = fmt("T%d: SLOT %d DELETED", c.col + 1, s + 1);
    }
    saveProject(a, true);
}

} // namespace

const ScreenOps kSessionScreen = {
    "SESSION", draw, move, edit, enter, clear, nullptr,
    "A:PLAY CLIP / KEEP  B+A:DELETE  SEL+A:KEEP  X:GEN  L3/R3:MUTE/SOLO  Y+DN:MIXER",
};
