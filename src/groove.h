#pragma once

#include <string>
#include <vector>

// Grooves read from Ableton .agr files. A groove's notes say where each beat
// division really lands; the sequencer uses that as a timing map over its
// 16th steps (and a velocity curve), looped every `steps` steps.
struct Groove {
    static constexpr int kMaxSteps = 64;
    std::string name;            // from the file name, uppercase for the bitmap font
    int         steps = 16;      // loop length in 16th steps
    float       start[kMaxSteps + 1] = {}; // where each step starts, in steps (start[steps] = start[0] + steps)
    float       vel[kMaxSteps]       = {}; // 0..1 accent of each step
    float       timing   = 1.0f; // the file's TimingAmount (0..1)
    float       velocity = 0.0f; // the file's VelocityAmount (0..1)
};

// Parses one .agr (XML). `name` becomes the groove's name.
bool parseGroove(const std::string& xml, const std::string& name, Groove& out);

// Every .agr under `dir` (subfolders too), sorted by name.
std::vector<Groove> loadGrooves(const std::string& dir);

// Text that the bitmap font can draw: uppercase, '&' as '+', anything unknown as a space.
std::string fontSafe(const std::string& s);
