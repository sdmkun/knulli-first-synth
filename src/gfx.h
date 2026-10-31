#pragma once

#include <SDL.h>

#include <string>

// Drawing helpers on top of SDL_Renderer, using the built-in 5x7 font.
// Text is drawn in cells of 6x8 pixels times `scale`.

constexpr int kW = 1024; // TrimUI Brick native resolution
constexpr int kH = 768;

const SDL_Color kBg      = {14, 16, 26, 255};
const SDL_Color kPanel   = {26, 30, 46, 255};
const SDL_Color kPanel2  = {34, 39, 58, 255}; // beat rows, header strips
const SDL_Color kLine    = {58, 64, 92, 255};
const SDL_Color kText    = {220, 224, 236, 255};
const SDL_Color kDim     = {110, 118, 150, 255};
const SDL_Color kAccent  = {255, 176, 64, 255};
const SDL_Color kPlayCol = {96, 220, 140, 255};
const SDL_Color kScopeC  = {90, 210, 255, 255};
const SDL_Color kRecCol  = {255, 80, 80, 255};
const SDL_Color kCursorBg = {70, 54, 28, 255};

void        initFont();
std::string fmt(const char* f, ...);

int  textWidth(const std::string& s, int scale);
void drawText(SDL_Renderer* r, int x, int y, int scale, const std::string& s, SDL_Color c);
void drawTextCentered(SDL_Renderer* r, int cx, int y, int scale, const std::string& s, SDL_Color c);
void drawTextRight(SDL_Renderer* r, int rx, int y, int scale, const std::string& s, SDL_Color c);
void fillRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c);
void strokeRect(SDL_Renderer* r, int x, int y, int w, int h, SDL_Color c);
// Horizontal bar showing `norm` (0..1) of its width.
void drawBar(SDL_Renderer* r, int x, int y, int w, int h, float norm, SDL_Color c);
