#include "groove.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

// Value of attribute `attr` in the tag starting at `pos` (up to its '>').
bool attrIn(const std::string& s, size_t pos, const char* attr, double& out) {
    size_t end = s.find('>', pos);
    std::string key = std::string(" ") + attr + "=\"";
    size_t      at  = s.find(key, pos);
    if (at == std::string::npos || at > end) return false;
    out = std::strtod(s.c_str() + at + key.size(), nullptr);
    return true;
}

// <Tag Value="x" /> : the first (or last) one in the file.
bool tagValue(const std::string& s, const char* tag, bool last, double& out) {
    std::string key = std::string("<") + tag + " ";
    size_t      at  = last ? s.rfind(key) : s.find(key);
    return at != std::string::npos && attrIn(s, at, "Value", out);
}

struct Point {
    double grid, time, vel;
};

// Piecewise-linear lookup through points sorted by grid, which wrap every `len` beats.
void lookup(const std::vector<Point>& pts, double len, double x, double& time, double& vel) {
    const size_t n = pts.size();
    for (size_t i = 0; i < n; ++i) {
        const Point& a = pts[i];
        Point        b = i + 1 < n ? pts[i + 1] : Point{pts[0].grid + len, pts[0].time + len, pts[0].vel};
        if (x < a.grid && i == 0) { // before the first point: wrap from the last one
            Point p = pts[n - 1];
            p.grid -= len, p.time -= len;
            double f = (x - p.grid) / (a.grid - p.grid);
            time     = p.time + f * (a.time - p.time);
            vel      = p.vel + f * (a.vel - p.vel);
            return;
        }
        if (x >= a.grid && x < b.grid) {
            double f = (x - a.grid) / (b.grid - a.grid);
            time     = a.time + f * (b.time - a.time);
            vel      = a.vel + f * (b.vel - a.vel);
            return;
        }
    }
    time = x, vel = 1.0;
}

// The beat division the groove was played on: from the name when it says ("16ths",
// "8ths", "8T", "16T"), otherwise from how many notes there are per beat.
double gridFor(const std::string& name, size_t notes, double beats) {
    std::string n = name;
    for (char& c : n) c = char(std::toupper(static_cast<unsigned char>(c)));
    // "16THS" / "8THS" before the triplet marks, which they also contain.
    if (n.find("16TH") != std::string::npos) return 0.25;
    if (n.find("8TH") != std::string::npos) return 0.5;
    if (n.find("16T") != std::string::npos) return 1.0 / 6.0;
    if (n.find("8T") != std::string::npos) return 1.0 / 3.0;
    double perBeat = notes / std::max(beats, 0.25);
    if (perBeat >= 3.5) return 0.25;
    if (perBeat >= 2.5) return 1.0 / 3.0;
    if (perBeat >= 1.5) return 0.5;
    return 1.0;
}

// Newer Live versions save .agr as a binary document (it starts with AB 1E). Its
// notes are stored as 25-byte records after the type name "MidiNoteEvent":
// time (double, beats), duration (double), velocity (float), off velocity
// (float), enabled (byte). The record count is the 32-bit value 7 bytes before the name.
bool readBinaryNotes(const std::string& b, std::vector<Point>& notes, double& timing, double& velocity) {
    auto f64 = [&](size_t at) { double v; std::memcpy(&v, b.data() + at, 8); return v; };
    auto f32 = [&](size_t at) { float v; std::memcpy(&v, b.data() + at, 4); return double(v); };
    auto u32 = [&](size_t at) { uint32_t v; std::memcpy(&v, b.data() + at, 4); return v; };
    auto valid = [&](size_t at) {
        if (at + 25 > b.size()) return false;
        double t = f64(at), d = f64(at + 8), v = f32(at + 16), off = f32(at + 20);
        return t >= 0.0 && t < 1024.0 && d > 0.0 && d < 1024.0 && v >= 0.0 && v <= 127.0 && off >= 0.0 &&
               off <= 127.0 && uint8_t(b[at + 24]) <= 1;
    };
    const std::string name = "MidiNoteEvent";
    for (size_t at = b.find(name); at != std::string::npos; at = b.find(name, at + 1)) {
        if (at < 7) continue;
        uint32_t count = u32(at - 7);
        if (count == 0 || count > 4096) continue;
        for (size_t gap = 0; gap <= 8; ++gap) {
            size_t first = at + name.size() + gap;
            size_t k     = 0;
            while (k < count && valid(first + k * 25)) ++k;
            if (k != count) continue;
            for (k = 0; k < count; ++k) {
                size_t r = first + k * 25;
                if (b[r + 24]) notes.push_back({0.0, f64(r), std::clamp(f32(r + 16) / 127.0, 0.0, 1.0)});
            }
            break;
        }
    }
    // The groove amounts near the end: FF, three zero bytes, the base (32-bit enum),
    // then quantize, timing, random and velocity amounts as floats in 0..100.
    for (size_t at = b.size() >= 29 ? b.size() - 29 : 0; at > 0 && b.size() - at < 4096; --at) {
        if (uint8_t(b[at]) != 0xFF || b[at + 1] || b[at + 2] || b[at + 3] || u32(at + 4) > 16) continue;
        double q = f32(at + 8), t = f32(at + 12), r = f32(at + 16), v = f32(at + 20);
        bool   ok = true;
        for (double x : {q, t, r, v}) ok &= x >= 0.0 && x <= 100.0 && std::fabs(x - std::round(x)) < 1e-3;
        if (ok && t >= 1.0) { // whole percentages, and some timing (anything else is a false match)
            timing   = t / 100.0;
            velocity = v / 100.0;
            break;
        }
    }
    return !notes.empty();
}

} // namespace

std::string fontSafe(const std::string& s) {
    static const std::string kOk = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-.:/*+<>()[]#%=_!? ";
    std::string out;
    for (char ch : s) {
        char c = char(std::toupper(static_cast<unsigned char>(ch)));
        if (c == '&') c = '+';
        out += kOk.find(c) != std::string::npos ? c : ' ';
    }
    return out;
}

bool parseGroove(const std::string& xml, const std::string& name, Groove& g) {
    std::vector<Point> notes;
    double beats = 4.0, timing = 1.0, velocity = 0.0;
    if (xml.size() > 2 && uint8_t(xml[0]) == 0xAB && uint8_t(xml[1]) == 0x1E) {
        if (!readBinaryNotes(xml, notes, timing, velocity)) return false;
        // No loop length to read: whole bars that cover the notes.
        double last = 0.0;
        for (const Point& p : notes) last = std::max(last, p.time);
        beats = std::clamp(std::ceil((last + 0.05) / 4.0) * 4.0, 4.0, Groove::kMaxSteps / 4.0);
    } else {
        if (!tagValue(xml, "LoopEnd", false, beats) || beats <= 0.0) beats = 4.0;
        double start = 0.0;
        tagValue(xml, "LoopStart", false, start);
        beats = std::clamp(beats - start, 0.25, Groove::kMaxSteps / 4.0);
        for (size_t at = xml.find("<MidiNoteEvent "); at != std::string::npos;
             at = xml.find("<MidiNoteEvent ", at + 1)) {
            double t = 0.0, v = 100.0;
            if (!attrIn(xml, at, "Time", t)) continue;
            attrIn(xml, at, "Velocity", v);
            notes.push_back({0.0, t - start, std::clamp(v / 127.0, 0.0, 1.0)});
        }
        double amt = 100.0;
        if (tagValue(xml, "TimingAmount", true, amt)) timing = amt / 100.0;
        amt = 0.0;
        if (tagValue(xml, "VelocityAmount", true, amt)) velocity = amt / 100.0;
    }
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&](const Point& p) { return p.time < -0.01 || p.time >= beats; }),
                notes.end());
    if (notes.empty()) return false;

    // Snap each note to the grid it was played against; the first note on a grid point wins.
    const double grid = gridFor(name, notes.size(), beats);
    std::vector<Point> pts;
    std::sort(notes.begin(), notes.end(), [](const Point& a, const Point& b) { return a.time < b.time; });
    for (Point p : notes) {
        p.grid = std::round(p.time / grid) * grid;
        if (p.grid >= beats - 1e-6) continue; // belongs to the next loop's first point
        bool taken = false;
        for (const Point& q : pts) taken |= std::fabs(q.grid - p.grid) < 1e-6;
        if (!taken) pts.push_back(p);
    }
    if (pts.empty()) return false;
    std::sort(pts.begin(), pts.end(), [](const Point& a, const Point& b) { return a.grid < b.grid; });

    g.name  = fontSafe(name);
    g.steps = std::clamp(int(std::lround(beats * 4.0)), 1, Groove::kMaxSteps);
    for (int s = 0; s < g.steps; ++s) {
        double time, vel;
        lookup(pts, beats, s * 0.25, time, vel);
        g.start[s] = float(time * 4.0);
        g.vel[s]   = float(vel);
    }
    g.start[g.steps] = g.start[0] + g.steps;
    // Never let a step shrink to nothing (or run backwards).
    for (int s = 1; s <= g.steps; ++s) g.start[s] = std::max(g.start[s], g.start[s - 1] + 0.1f);
    float top = *std::max_element(g.vel, g.vel + g.steps);
    for (int s = 0; s < g.steps; ++s) g.vel[s] = top > 0.0f ? g.vel[s] / top : 1.0f;

    g.timing   = float(std::clamp(timing, 0.0, 1.0));
    g.velocity = float(std::clamp(velocity, 0.0, 1.0));
    return true;
}

std::vector<Groove> loadGrooves(const std::string& dir) {
    std::vector<Groove> out;
    std::error_code     ec;
    std::vector<fs::path> files;
    for (auto it = fs::recursive_directory_iterator(fs::u8path(dir), ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        std::string ext = it->path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (it->is_regular_file() && ext == ".agr") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return a.stem() < b.stem(); });
    for (const fs::path& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        Groove g;
        if (parseGroove(ss.str(), f.stem().u8string(), g)) out.push_back(std::move(g));
    }
    return out;
}
