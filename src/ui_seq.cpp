// SEQ: all four tracks side by side, one row per step, like a tracker.
// The row above the grid holds the song-wide BPM / KEY / SCALE.

#include <algorithm>

#include "app.h"
#include "gfx.h"

namespace {

enum { F_BPM, F_KEY, F_SCALE, F_COUNT };

constexpr int kGridX  = 76;
constexpr int kColW   = 236;
constexpr int kTitleY = 104;
constexpr int kRowY   = 160;
constexpr int kRowH   = 30;

// "C4 E4 G4+" for a synth step; one letter per drum pad ("K.H.....") for a drum step.
std::string cellText(const App& a, int t, const SeqStep& st) {
    if (a.isDrum[t]) {
        std::string s(kDrumPads, '.');
        for (int pad = 0; pad < kDrumPads; ++pad)
            if (st.find(kDrumBase + pad) >= 0) s[pad] = padName(a, t, pad)[0];
        return s;
    }
    SeqNote notes[SeqStep::kMaxNotes];
    int     n = st.sorted(notes);
    if (n == 0) return "---";
    std::string s;
    for (int i = 0; i < std::min(n, 3); ++i) s += (i ? " " : "") + noteName(notes[i].pitch);
    if (n > 3) s += "+";
    return s;
}

void draw(App& a) {
    SDL_Renderer* r = a.renderer;
    Pattern pats[kTracks];
    int     steps[kTracks];
    bool    playing;
    {
        AudioLock lock(a.audio);
        for (int t = 0; t < kTracks; ++t) {
            pats[t]  = a.seq.patterns[t];
            steps[t] = a.seq.currentStep(t);
        }
        playing = a.seq.playing();
    }
    const Cursor& c = a.seqCur;

    // Song-wide fields.
    const std::string values[F_COUNT] = {fmt("%d", int(a.seq.bpm() + 0.5f)), kNoteNames[a.root],
                                         kScales[a.scale].name};
    const char*       labels[F_COUNT] = {"BPM", "KEY", "SCALE"};
    const int         fx[F_COUNT]     = {24, 220, 400};
    for (int f = 0; f < F_COUNT; ++f) drawField(r, fx[f], 70, labels[f], values[f], c.row < 0 && c.col == f, fx[f] + 64);

    // Track titles.
    for (int t = 0; t < kTracks; ++t) {
        int  x   = kGridX + t * kColW;
        bool sel = t == a.track;
        fillRect(r, x, kTitleY, kColW - 6, 48, sel ? kPanel2 : kPanel);
        if (sel) fillRect(r, x, kTitleY + 46, kColW - 6, 2, kAccent);
        std::string name = fmt("%d %s", t + 1, trackName(a, t).c_str());
        if (name.size() > 18) name.resize(18);
        drawText(r, x + 6, kTitleY + 6, 2, name, sel ? kAccent : kText);
        drawText(r, x + 6, kTitleY + 26, 2, fmt("%s LEN %d", a.isDrum[t] ? "DRUM" : "SYNTH", pats[t].length), kDim);
        if (a.seq.solo == t) drawTextRight(r, x + kColW - 12, kTitleY + 26, 2, "SOLO", kAccent);
        else if (a.seq.mute[t]) drawTextRight(r, x + kColW - 12, kTitleY + 26, 2, "MUTE", kRecCol);
    }

    // Grid: the visible page of every track.
    for (int row = 0; row < kPageSteps; ++row) {
        int step = a.page * kPageSteps + row;
        int y    = kRowY + row * kRowH;
        drawText(r, 24, y + 8, 2, fmt("%02d", step), row % 4 == 0 ? kText : kDim);
        for (int t = 0; t < kTracks; ++t) {
            int  x      = kGridX + t * kColW;
            bool inside = step < pats[t].length;
            SDL_Color bg = !inside ? kBg : row % 4 == 0 ? kPanel2 : kPanel;
            if (inside && playing && steps[t] == step) bg = {44, 84, 60, 255};
            bool cur = c.row == row && c.col == t;
            if (cur) bg = kCursorBg;
            fillRect(r, x, y, kColW - 6, kRowH - 3, bg);
            if (!inside) continue;
            const SeqStep& st   = pats[t].steps[step];
            bool           any  = !st.empty();
            SDL_Color      col  = !a.seq.audible(t) ? kLine : any ? (cur ? kAccent : kText) : kLine;
            drawText(r, x + 8, y + 7, 2, cellText(a, t, st), col);
        }
    }
}

void move(App& a, int dx, int dy) {
    Cursor& c = a.seqCur;
    if (dy) {
        int row = std::clamp(c.row + dy, -1, kPageSteps - 1);
        if (row < 0 && c.row >= 0) c.col = 0;       // into the field row
        if (row >= 0 && c.row < 0) c.col = a.track; // back onto the grid
        c.row = row;
    }
    if (dx) c.col = std::clamp(c.col + dx, 0, c.row < 0 ? F_COUNT - 1 : kTracks - 1);
    if (c.row >= 0) a.track = c.col;
}

void edit(App& a, int fine, int coarse) {
    Cursor& c = a.seqCur;
    if (c.row >= 0) return;
    AudioLock lock(a.audio);
    switch (c.col) {
    case F_BPM:   a.seq.setBpm(a.seq.bpm() + fine + coarse * 10); break;
    case F_KEY:   a.root = (a.root + fine + coarse + 12) % 12; break;
    case F_SCALE: a.scale = (a.scale + fine + coarse + kScaleCount) % kScaleCount; break;
    }
}

// A on a cell opens that track's pattern at that step.
void enter(App& a) {
    const Cursor& c = a.seqCur;
    if (c.row < 0) return;
    a.track          = c.col;
    a.screen         = SCR_PATTERN;
    a.synthCur.row   = c.row;
    a.synthCur.col   = 0;
    a.drumCur.col    = c.row;
}

void clear(App& a) {
    const Cursor& c = a.seqCur;
    if (c.row < 0) return;
    int step = a.page * kPageSteps + c.row;
    AudioLock lock(a.audio);
    if (step >= a.seq.patterns[c.col].length) return;
    pushUndo(a, c.col);
    a.seq.patterns[c.col].steps[step] = SeqStep{};
}

} // namespace

const ScreenOps kSeqScreen = {
    "SEQ", draw, move, edit, enter, clear,
    "A:OPEN STEP  A+PAD:EDIT  B+A:CLEAR  X:GEN Y:MUTATE  SEL+<>:SCREEN  L3/R3:MUTE/SOLO",
};
