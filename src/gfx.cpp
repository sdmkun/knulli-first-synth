#include "gfx.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "font.h"

namespace {
const Glyph* gGlyphs[128] = {};
}

void initFont() {
    for (const Glyph& g : kGlyphs) gGlyphs[int(g.c)] = &g;
}

std::string fmt(const char* f, ...) {
    char    buf[256];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

int textWidth(const std::string& s, int scale) { return s.empty() ? 0 : int(s.size()) * 6 * scale - scale; }

void drawText(SDL_Renderer* r, int x, int y, int scale, const std::string& s, SDL_Color c) {
    std::vector<SDL_Rect> rects;
    for (char ch : s) {
        int          code = (ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
        const Glyph* g    = (code > 0 && code < 128) ? gGlyphs[code] : nullptr;
        if (g)
            for (int row = 0; row < 7; ++row)
                for (int col = 0; col < 5; ++col)
                    if (g->rows[row][col] == '1') rects.push_back({x + col * scale, y + row * scale, scale, scale});
        x += 6 * scale;
    }
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    if (!rects.empty()) SDL_RenderFillRects(r, rects.data(), int(rects.size()));
}

void drawTextCentered(SDL_Renderer* r, int cx, int y, int scale, const std::string& s, SDL_Color c) {
    drawText(r, cx - textWidth(s, scale) / 2, y, scale, s, c);
}

void drawTextRight(SDL_Renderer* r, int rx, int y, int scale, const std::string& s, SDL_Color c) {
    drawText(r, rx - textWidth(s, scale), y, scale, s, c);
}

void fillRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

void strokeRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderDrawRect(r, &rc);
}

void drawBar(SDL_Renderer* r, int x, int y, int w, int h, float norm, SDL_Color c) {
    fillRect(r, x, y, w, h, kLine);
    fillRect(r, x, y, int(w * std::clamp(norm, 0.0f, 1.0f)), h, c);
}
