#include "drumkit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

// "01-Kick_808.wav" -> "KICK_808": drop the extension and any leading number
// used for ordering, uppercase for the bitmap font.
std::string displayName(const fs::path& p) {
    std::string s = p.stem().string();
    size_t      i = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
    while (i < s.size() && (s[i] == '-' || s[i] == '_' || s[i] == ' ' || s[i] == '.')) ++i;
    if (i == s.size()) i = 0;
    s = s.substr(i);
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool isWav(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".wav";
}

} // namespace

bool loadWav(const std::string& path, Sample& out, std::string& error) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) { error = "cannot open"; return false; }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 12 || std::memcmp(buf.data(), "RIFF", 4) != 0 || std::memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file";
        return false;
    }

    int            format = 0, channels = 0, rate = 0, bits = 0;
    const uint8_t* data   = nullptr;
    size_t         bytes  = 0;
    for (size_t pos = 12; pos + 8 <= buf.size();) {
        const uint8_t* chunk = buf.data() + pos;
        size_t         size  = le32(chunk + 4);
        size_t         avail = std::min(size, buf.size() - pos - 8);
        if (std::memcmp(chunk, "fmt ", 4) == 0 && avail >= 16) {
            format   = le16(chunk + 8);
            channels = le16(chunk + 10);
            rate     = int(le32(chunk + 12));
            bits     = le16(chunk + 22);
            if (format == 0xFFFE && avail >= 26) format = le16(chunk + 32); // WAVE_FORMAT_EXTENSIBLE sub-format
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data  = chunk + 8;
            bytes = avail;
        }
        pos += 8 + size + (size & 1);
    }
    if (!data || channels <= 0 || rate <= 0) { error = "missing fmt or data chunk"; return false; }
    if (!((format == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32))) {
        error = "unsupported format " + std::to_string(format) + "/" + std::to_string(bits) + "bit";
        return false;
    }

    const int    bps    = bits / 8;
    const size_t frames = bytes / size_t(bps * channels);
    out.data.assign(frames, 0.0f);
    out.rate = rate;
    for (size_t i = 0; i < frames; ++i) {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c) {
            const uint8_t* p = data + (i * channels + c) * bps;
            float          v = 0.0f;
            if (format == 3) {
                std::memcpy(&v, p, 4);
            } else if (bits == 8) {
                v = (int(p[0]) - 128) / 128.0f;
            } else if (bits == 16) {
                v = int16_t(le16(p)) / 32768.0f;
            } else if (bits == 24) {
                int32_t x = int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) >> 8;
                v         = x / 8388608.0f;
            } else {
                v = int32_t(le32(p)) / 2147483648.0f;
            }
            sum += v;
        }
        out.data[i] = sum / channels;
    }
    return true;
}

std::vector<Kit> loadKits(const std::string& dir) {
    std::vector<Kit> kits;
    std::error_code  ec;
    std::vector<fs::path> folders;
    for (const auto& e : fs::directory_iterator(fs::u8path(dir), ec))
        if (e.is_directory()) folders.push_back(e.path());
    std::sort(folders.begin(), folders.end());

    for (const fs::path& folder : folders) {
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(folder, ec))
            if (e.is_regular_file() && isWav(e.path())) files.push_back(e.path());
        std::sort(files.begin(), files.end());

        Kit kit;
        kit.name   = displayName(folder);
        int loaded = 0;
        for (const fs::path& file : files) {
            if (loaded == Kit::kPads) break;
            std::string error;
            Sample&     s = kit.pads[loaded];
            if (loadWav(file.u8string(), s, error)) {
                s.name = displayName(file);
                ++loaded;
            } else {
                s = Sample{};
                std::fprintf(stderr, "skip %s: %s\n", file.u8string().c_str(), error.c_str());
            }
        }
        if (loaded > 0) kits.push_back(std::move(kit));
    }
    return kits;
}

void DrumKit::setKit(const Kit* kit) {
    allOff();
    kit_ = kit;
}

void DrumKit::noteOn(int, int note, float gain) {
    const int pad = note - kBaseNote;
    if (!kit_ || pad < 0 || pad >= Kit::kPads) return;
    const Sample& s = kit_->pads[pad];
    if (s.data.empty()) return;

    // Free voice, else steal the oldest.
    Voice* v = &voices_[0];
    for (Voice& c : voices_) {
        if (!c.sample) { v = &c; break; }
        if (c.age < v->age) v = &c;
    }
    const PadSettings& ps  = pads[pad];
    const double       len = double(s.data.size());
    v->sample = &s;
    v->pos    = std::floor(std::clamp(ps.start, 0.0f, 1.0f) * len);
    v->end    = std::max(v->pos + 1.0, std::clamp(ps.end, 0.0f, 1.0f) * len);
    v->end    = std::min(v->end, len - 1.0);
    v->inc    = double(s.rate) / sampleRate_ * std::pow(2.0, ps.pitch / 12.0);
    v->gain   = ps.volume * ps.volume * gain;
    v->age    = ++counter_;
}

void DrumKit::allOff() {
    for (Voice& v : voices_) v = Voice{};
}

int DrumKit::activeVoices() const {
    int n = 0;
    for (const Voice& v : voices_)
        if (v.sample) ++n;
    return n;
}

void DrumKit::render(float* out, int frames) {
    std::fill(out, out + frames, 0.0f);
    for (Voice& v : voices_) {
        if (!v.sample) continue;
        const float*  d    = v.sample->data.data();
        const double  fade = 64.0 * v.inc; // short fade before END so cut samples don't click
        for (int i = 0; i < frames; ++i) {
            if (v.pos >= v.end) { v = Voice{}; break; }
            const int   i0   = int(v.pos);
            const float frac = float(v.pos - i0);
            float       s    = d[i0] + (d[i0 + 1] - d[i0]) * frac;
            const double left = v.end - v.pos;
            if (left < fade) s *= float(left / fade);
            out[i] += s * v.gain;
            v.pos += v.inc;
        }
    }
}
