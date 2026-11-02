// The list popup and the lists it is used for: note commands (A + up / down on
// an FX), grooves (A on GROOVE) and the sample browser (A + up / down on a pad's sample).

#include <algorithm>
#include <filesystem>
#include <memory>

#include "app.h"
#include "gfx.h"

namespace fs = std::filesystem;

namespace {

constexpr int kX     = 180;
constexpr int kY     = 64;
constexpr int kWidth = kW - 2 * kX;
constexpr int kRowH  = 26;
constexpr int kRows  = 19; // visible at once

void clampSel(Popup& p) {
    const int n = int(p.items.size());
    p.sel       = n ? std::clamp(p.sel, 0, n - 1) : 0;
    if (p.sel < p.top) p.top = p.sel;
    if (p.sel >= p.top + kRows) p.top = p.sel - kRows + 1;
    p.top = std::clamp(p.top, 0, std::max(0, n - kRows));
}

void moveSel(App& a, int by) {
    Popup& p   = a.popup;
    int    old = p.sel;
    p.sel += by;
    clampSel(p);
    if (p.sel != old && p.moved) {
        auto f = p.moved; // the callback may change the popup, so call a copy
        f(a, p.sel);
    }
}

// ---------------------------------------------------------------- note commands

const char* fxLongName(Fx fx) {
    switch (fx) {
    case Fx::Ret: return "ROLL: RETRIGGER EVERY N TICKS";
    case Fx::Arp: return "ARPEGGIO: +0 +X +Y SEMITONES";
    case Fx::Kil: return "CUT THE NOTE AFTER N TICKS";
    case Fx::Dly: return "START THE NOTE N TICKS LATE";
    case Fx::Cha: return "PLAY ONLY N% OF THE TIME";
    default:      return "NO COMMAND";
    }
}

// ---------------------------------------------------------------- sample browser

struct Browser {
    std::string root, dir;
    std::vector<std::string> paths; // per item; empty for "..", ending in '/' for folders
    int track = 0, pad = 0;
};

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

void listDir(App& a, Browser& b, const std::string& selectName = "") {
    std::vector<fs::path> dirs, files;
    std::error_code       ec;
    for (const auto& e : fs::directory_iterator(fs::u8path(b.dir), ec)) {
        if (e.is_directory(ec)) dirs.push_back(e.path());
        else if (e.is_regular_file(ec) && isWavFile(e.path().u8string())) files.push_back(e.path());
    }
    auto byName = [](const fs::path& x, const fs::path& y) { return upper(x.filename().u8string()) < upper(y.filename().u8string()); };
    std::sort(dirs.begin(), dirs.end(), byName);
    std::sort(files.begin(), files.end(), byName);

    Popup& p = a.popup;
    p.items.clear();
    p.notes.clear();
    b.paths.clear();
    if (b.dir != b.root) {
        p.items.push_back("../");
        p.notes.push_back("UP");
        b.paths.push_back("");
    }
    for (const fs::path& d : dirs) {
        p.items.push_back(fontSafe(d.filename().u8string()) + "/");
        p.notes.push_back("");
        b.paths.push_back(d.u8string() + "/");
    }
    for (const fs::path& f : files) {
        p.items.push_back(fontSafe(f.filename().u8string()));
        p.notes.push_back(fmt("%dK", int(fs::file_size(f, ec) / 1024)));
        b.paths.push_back(f.u8string());
    }
    std::string rel = fs::u8path(b.dir).lexically_relative(fs::u8path(b.root)).u8string();
    p.title         = fmt("PAD %d SAMPLE  SAMPLES/%s", b.pad + 1, rel == "." ? "" : fontSafe(rel).c_str());
    p.sel = p.top = 0;
    for (size_t i = 0; i < p.items.size(); ++i)
        if (!selectName.empty() && p.items[i] == selectName) p.sel = int(i);
    clampSel(p);
    if (p.items.empty()) {
        p.items.push_back("(NO WAV FILES HERE)");
        p.notes.push_back("");
        b.paths.push_back("");
    }
}

void goUp(App& a, Browser& b) {
    if (b.dir == b.root) return;
    fs::path    cur  = fs::u8path(b.dir);
    std::string name = fontSafe(cur.filename().u8string()) + "/";
    b.dir            = cur.parent_path().u8string();
    listDir(a, b, name);
}

void open(App& a, Browser& b, int i) {
    if (i < 0 || i >= int(b.paths.size())) return;
    const std::string& path = b.paths[i];
    if (path.empty()) {
        if (a.popup.items[i] == "../") goUp(a, b);
        return;
    }
    if (path.back() == '/') {
        b.dir = path.substr(0, path.size() - 1);
        listDir(a, b);
        return;
    }
    if (setPadSample(a, b.track, b.pad, path)) {
        AudioLock lock(a.audio);
        preview(a, b.track, kDrumBase + b.pad);
        a.message = fmt("PAD %d: %s", b.pad + 1, padName(a, b.track, b.pad).c_str());
    }
}

} // namespace

void openPopup(App& a, Popup p) {
    a.popup      = std::move(p);
    a.popup.open = true;
    a.genOpen    = false;
    clampSel(a.popup);
}

bool popupPress(App& a, Btn b) {
    Popup& p = a.popup;
    if (!p.open) return false;
    switch (b) {
    case BTN_UP:   moveSel(a, -1); return true;
    case BTN_DOWN: moveSel(a, 1); return true;
    case BTN_L2:   moveSel(a, -kRows); return true;
    case BTN_R2:   moveSel(a, kRows); return true;
    case BTN_LEFT:
        if (p.left) { auto f = p.left; f(a, p.sel); }
        else moveSel(a, -kRows);
        return true;
    case BTN_RIGHT:
        if (p.right) { auto f = p.right; f(a, p.sel); }
        else moveSel(a, kRows);
        return true;
    case BTN_A:
        if (p.pick && !p.items.empty()) { auto f = p.pick; f(a, p.sel); }
        return true;
    case BTN_B:
        if (p.cancel) { auto f = p.cancel; f(a); }
        p.open = false;
        return true;
    case BTN_START: return false; // play / stop still works
    default:        return true;  // everything else waits until the popup is closed
    }
}

void drawPopup(App& a) {
    const Popup& p = a.popup;
    if (!p.open) return;
    SDL_Renderer* r = a.renderer;
    const int     n = int(p.items.size());
    const int     h = 52 + std::max(4, std::min(n, kRows)) * kRowH + 36; // short lists get a short box
    fillRect(r, kX - 4, kY - 4, kWidth + 8, h + 8, kAccent);
    fillRect(r, kX, kY, kWidth, h, kBg);
    std::string title = p.title;
    if (title.size() > 52) title.resize(52);
    drawText(r, kX + 16, kY + 14, 2, title, kAccent);
    if (n > kRows) drawTextRight(r, kX + kWidth - 16, kY + 14, 2, fmt("%d/%d", p.sel + 1, n), kDim);
    for (int i = 0; i < kRows && p.top + i < n; ++i) {
        const int idx = p.top + i, y = kY + 48 + i * kRowH;
        const bool sel = idx == p.sel;
        if (sel) fillRect(r, kX + 8, y - 4, kWidth - 30, kRowH - 2, kCursorBg);
        std::string item = p.items[idx];
        if (item.size() > 40) item.resize(40);
        drawText(r, kX + 16, y, 2, item, sel ? kAccent : kText);
        if (idx < int(p.notes.size()) && !p.notes[idx].empty())
            drawTextRight(r, kX + kWidth - 32, y, 2, p.notes[idx], sel ? kAccent : kDim);
    }
    if (n > kRows) { // scroll bar
        const int trackH = kRows * kRowH, barH = std::max(20, trackH * kRows / n);
        const int barY   = kY + 44 + (trackH - barH) * p.top / std::max(1, n - kRows);
        fillRect(r, kX + kWidth - 14, kY + 44, 6, trackH, kPanel);
        fillRect(r, kX + kWidth - 14, barY, 6, barH, kDim);
    }
    drawText(r, kX + 16, kY + h - 28, 2, p.footer.empty() ? "A:SELECT  B:CLOSE  L2/R2:PAGE" : p.footer, kDim);
}

void openFxPopup(App& a, FxRef (*target)(App&)) {
    Fx current = Fx::None;
    {
        AudioLock lock(a.audio);
        FxRef     ref = target(a);
        if (!ref.fx) return;
        current = *ref.fx;
    }
    Popup p;
    p.title = "NOTE COMMAND";
    for (int i = 0; i < int(Fx::Count); ++i) {
        p.items.push_back(fmt("%s  %s", fxName(Fx(i)), fxLongName(Fx(i))));
        p.notes.push_back(Fx(i) == Fx::None ? "" : fxValueText(Fx(i), fxDefault(Fx(i))));
    }
    p.sel  = int(current);
    p.pick = [target](App& app, int i) {
        AudioLock lock(app.audio);
        if (FxRef ref = target(app); ref.fx && *ref.fx != Fx(i)) {
            *ref.fx  = Fx(i);
            *ref.val = uint8_t(fxDefault(Fx(i)));
        }
        if (Fx(i) != Fx::None) app.lastFx[app.track] = {Fx(i), uint8_t(fxDefault(Fx(i)))};
        app.popup.open = false;
    };
    p.footer = "A:SET  B:CANCEL  (VALUE SHOWN IS THE DEFAULT)";
    openPopup(a, std::move(p));
}

void openGroovePopup(App& a) {
    if (a.grooves.empty()) {
        a.message = "NO GROOVES: PUT .AGR FILES IN GROOVES/";
        return;
    }
    Popup p;
    p.title = "GROOVE";
    p.items.push_back("NONE (SWING)");
    p.notes.push_back("");
    for (const Groove& g : a.grooves) {
        p.items.push_back(g.name);
        p.notes.push_back(g.steps % 16 == 0 ? fmt("%d BAR", g.steps / 16) : fmt("%d ST", g.steps));
    }
    p.sel = a.groove + 1;
    // Moving through the list plays each groove at once; B goes back to the one before.
    const int   oldGroove = a.groove;
    const float oldTiming = a.grooveTiming, oldVel = a.grooveVelocity;
    auto choose = [](App& app, int i) {
        AudioLock lock(app.audio);
        app.groove = i - 1;
        if (app.groove >= 0) {
            app.grooveTiming   = app.grooves[app.groove].timing;
            app.grooveVelocity = app.grooves[app.groove].velocity;
        }
        applyGroove(app);
    };
    p.moved  = choose;
    p.pick   = [choose](App& app, int i) {
        choose(app, i);
        app.popup.open = false;
        app.message    = "GROOVE: " + grooveName(app);
    };
    p.cancel = [oldGroove, oldTiming, oldVel](App& app) {
        AudioLock lock(app.audio);
        app.groove         = oldGroove;
        app.grooveTiming   = oldTiming;
        app.grooveVelocity = oldVel;
        applyGroove(app);
    };
    p.footer = "UP/DN: TRY  A:KEEP  B:CANCEL  L2/R2:PAGE";
    openPopup(a, std::move(p));
}

void openSampleBrowser(App& a, int t, int pad) {
    std::error_code ec;
    if (a.samplesDir.empty() || !fs::is_directory(fs::u8path(a.samplesDir), ec)) {
        a.message = "NO SAMPLES FOLDER";
        return;
    }
    auto b   = std::make_shared<Browser>();
    b->root  = fs::u8path(a.samplesDir).lexically_normal().u8string();
    if (!b->root.empty() && (b->root.back() == '/' || b->root.back() == '\\')) b->root.pop_back();
    b->dir   = b->root;
    // Start in the folder of the kit the track plays, when it lies under samples/.
    if (const Kit* k = trackKit(a, t); k && !k->dir.empty()) {
        std::string kd  = fs::u8path(k->dir).lexically_normal().u8string();
        std::string rel = fs::u8path(kd).lexically_relative(fs::u8path(b->root)).u8string();
        if (!rel.empty() && rel.rfind("..", 0) != 0 && fs::is_directory(fs::u8path(kd), ec)) b->dir = kd;
    }
    b->track = t;
    b->pad   = pad;
    Popup p;
    p.pick   = [b](App& app, int i) { open(app, *b, i); };
    p.right  = [b](App& app, int i) {
        if (i < int(b->paths.size()) && !b->paths[i].empty() && b->paths[i].back() == '/') open(app, *b, i);
    };
    p.left   = [b](App& app, int) { goUp(app, *b); };
    p.footer = "A:LOAD+PLAY  LEFT/RIGHT:FOLDER  B:CLOSE";
    openPopup(a, std::move(p));
    listDir(a, *b);
}
