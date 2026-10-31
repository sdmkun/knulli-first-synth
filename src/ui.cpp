// Screen chrome shared by all screens: header with the screen map, transport
// strip with the master scope, and the footer with hints and messages.

#include <algorithm>
#include <vector>

#include "app.h"
#include "gfx.h"

const ScreenOps& screenOps(Screen s) {
    switch (s) {
    case SCR_PATTERN: return kPatternScreen;
    case SCR_INSTR:   return kInstrScreen;
    default:          return kSeqScreen;
    }
}

void drawField(SDL_Renderer* r, int x, int y, const std::string& label, const std::string& value, bool selected,
               int valueX) {
    if (selected) fillRect(r, x - 8, y - 6, valueX - x + 8 + textWidth(value, 2) + 16, 26, kCursorBg);
    drawText(r, x, y, 2, label, selected ? kAccent : kDim);
    drawText(r, valueX, y, 2, value, selected ? kAccent : kText);
}

// Master output, triggered on a rising zero crossing so periodic waves stand still.
void drawScope(App& a, int x, int y, int w, int h) {
    SDL_Renderer* r = a.renderer;
    SDL_SetRenderDrawColor(r, kLine.r, kLine.g, kLine.b, 255);
    SDL_RenderDrawLine(r, x, y + h / 2, x + w - 1, y + h / 2);

    constexpr int kSize = sizeof(a.scope) / sizeof(a.scope[0]);
    static float  buf[kSize];
    {
        AudioLock lock(a.audio);
        for (int i = 0; i < kSize; ++i) buf[i] = a.scope[(a.scopePos + i) % kSize];
    }
    const int span  = std::min(w * 2, kSize - 1);
    int       start = 0;
    for (int i = 1; i < kSize - span; ++i)
        if (buf[i - 1] < 0.0f && buf[i] >= 0.0f) { start = i; break; }

    std::vector<SDL_Point> pts(w);
    for (int i = 0; i < w; ++i) {
        float s = std::clamp(buf[std::min(start + i * 2, kSize - 1)], -1.0f, 1.0f);
        pts[i]  = {x + i, y + h / 2 - int(s * (h / 2 - 2))};
    }
    SDL_SetRenderDrawColor(r, kScopeC.r, kScopeC.g, kScopeC.b, 255);
    SDL_RenderDrawLines(r, pts.data(), w);
}

void drawChrome(App& a) {
    SDL_Renderer* r = a.renderer;

    // Header: screen map, current track, DSP load.
    fillRect(r, 0, 0, kW, 52, kPanel);
    int x = 16;
    for (int s = 0; s < SCR_COUNT; ++s) {
        const char* name = screenOps(Screen(s)).name;
        int         w    = textWidth(name, 3);
        if (s == a.screen) fillRect(r, x - 6, 8, w + 12, 36, kAccent);
        drawText(r, x, 15, 3, name, s == a.screen ? kBg : kDim);
        x += w + 26;
    }
    std::string tn = fmt("T%d %s", a.track + 1, trackName(a, a.track).c_str());
    if (tn.size() > 17) tn.resize(17);
    drawText(r, 400, 15, 3, tn, kText);

    Uint32 now = SDL_GetTicks();
    if (SDL_TICKS_PASSED(now, a.peakResetAt)) {
        a.shownPeak   = a.dspPeak.exchange(0.0f);
        a.peakResetAt = now + 1000;
    }
    float     load = a.dspLoad.load() * 100.0f;
    float     peak = a.shownPeak * 100.0f;
    SDL_Color lc   = peak >= 80.0f ? kRecCol : peak >= 50.0f ? kAccent : kPlayCol;
    drawText(r, 760, 8, 2, fmt("DSP  %4.1f%%", load), lc);
    drawText(r, 760, 30, 2, fmt("PEAK %4.1f%%", peak), lc);
    drawBar(r, 900, 11, 108, 8, load / 100.0f, lc);
    drawBar(r, 900, 33, 108, 8, peak / 100.0f, lc);

    // Transport strip: state, tempo, key, page, mute / solo, scope.
    const int ty = 660, th = 44;
    fillRect(r, 0, ty, kW, th, kPanel);
    bool playing;
    {
        AudioLock lock(a.audio);
        playing = a.seq.playing();
    }
    drawText(r, 16, ty + 11, 3, playing ? "PLAY" : "STOP", playing ? kPlayCol : kDim);
    drawText(r, 106, ty + 15, 2, fmt("%d BPM", int(a.seq.bpm() + 0.5f)), kText);
    drawText(r, 206, ty + 15, 2, fmt("%s %s", kNoteNames[a.root], kScales[a.scale].name), kText);
    int pages = 1;
    if (a.screen == SCR_SEQ) {
        for (int t = 0; t < kTracks; ++t) pages = std::max(pages, (a.seq.patterns[t].length + kPageSteps - 1) / kPageSteps);
    } else {
        pages = (a.seq.patterns[a.track].length + kPageSteps - 1) / kPageSteps;
    }
    drawText(r, 340, ty + 15, 2, fmt("PG %d/%d", a.page + 1, pages), pages > 1 ? kText : kDim);
    for (int t = 0; t < kTracks; ++t) {
        int       bx  = 440 + t * 34;
        bool      aud = a.seq.audible(t);
        SDL_Color c   = a.seq.solo == t ? kAccent : aud ? kPanel2 : kRecCol;
        fillRect(r, bx, ty + 8, 28, 28, c);
        drawTextCentered(r, bx + 14, ty + 15, 2, std::to_string(t + 1), aud && a.seq.solo != t ? kText : kBg);
    }
    drawScope(a, 590, ty + 4, kW - 590 - 12, th - 8);

    // Footer: what the buttons do here, and the last message.
    fillRect(r, 0, kH - 60, kW, 60, kBg);
    drawText(r, 16, kH - 52, 2, screenOps(a.screen).hints, kDim);
    std::string msg = a.message;
    if (msg.size() > 84) msg.resize(84);
    drawText(r, 16, kH - 26, 2, msg, kText);
}
