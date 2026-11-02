// The project file: everything needed to come back to where you left off —
// tempo and key, each track's sound, settings and live pattern, and the clip bank.
// It is JSON so it can be read and changed by hand (or by Claude) on the PC.

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "app.h"
#include "gfx.h"
#include "json.h"

namespace fs = std::filesystem;

namespace {

constexpr int    kVersion     = 1;
constexpr Uint32 kCheckEvery  = 5000; // ms between autosave checks

// Every synth parameter by the name it has in the file.
struct FloatParam { const char* key; float Params::*f; };
struct IntParam   { const char* key; int Params::*i; };
const FloatParam kFloats[] = {
    {"pos", &Params::pos}, {"detune", &Params::detune}, {"sub", &Params::sub}, {"noise", &Params::noise},
    {"cutoff", &Params::cutoff}, {"resonance", &Params::resonance}, {"drive", &Params::drive},
    {"volume", &Params::volume}, {"delayTime", &Params::delayTime}, {"delayFeedback", &Params::delayFeedback},
    {"delayMix", &Params::delayMix}, {"ampA", &Params::ampA}, {"ampD", &Params::ampD}, {"ampS", &Params::ampS},
    {"ampR", &Params::ampR}, {"envA", &Params::envA}, {"envD", &Params::envD}, {"envS", &Params::envS},
    {"envR", &Params::envR}, {"envAmt", &Params::envAmt}, {"lfo1Rate", &Params::lfo1Rate},
    {"lfo1Amt", &Params::lfo1Amt}, {"lfo2Rate", &Params::lfo2Rate}, {"lfo2Amt", &Params::lfo2Amt},
    {"spread", &Params::spread}, {"width", &Params::width}, {"chorus", &Params::chorus},
};
const IntParam kInts[] = {
    {"wave", &Params::wave}, {"transpose", &Params::transpose}, {"filter", &Params::filter},
    {"envDest", &Params::envDest}, {"lfo1Shape", &Params::lfo1Shape}, {"lfo1Dest", &Params::lfo1Dest},
    {"lfo1Trig", &Params::lfo1Trig}, {"lfo2Shape", &Params::lfo2Shape}, {"lfo2Dest", &Params::lfo2Dest},
    {"lfo2Trig", &Params::lfo2Trig}, {"unison", &Params::unison},
};

// ---------------------------------------------------------------- patterns

// {"length": 16, "steps": [[step, [[column, pitch, vel, fx, fxValue], ...], [[fx, value], [fx, value]]], ...]}
// Only steps with something in them are written. Pitch -2 is OFF.
Json patternJson(const Pattern& p) {
    Json j = Json::object();
    j.set("length", p.length);
    Json& steps = j.set("steps", Json::array());
    for (int i = 0; i < Pattern::kMaxSteps; ++i) {
        const SeqStep& st  = p.steps[i];
        bool           any = !st.empty();
        for (const StepFx& f : st.fx) any = any || f.fx != Fx::None;
        if (!any) continue;
        Json row = Json::array();
        row.push(i);
        Json notes = Json::array();
        for (int c = 0; c < SeqStep::kMaxNotes; ++c) {
            const SeqNote& n = st.notes[c];
            if (n.pitch == SeqNote::kEmpty) continue;
            Json note = Json::array();
            for (int v : {c, int(n.pitch), int(n.vel), int(n.fx), int(n.fxVal)}) note.push(v);
            notes.push(std::move(note));
        }
        row.push(std::move(notes));
        Json fx = Json::array();
        for (const StepFx& f : st.fx) {
            Json one = Json::array();
            one.push(int(f.fx));
            one.push(int(f.val));
            fx.push(std::move(one));
        }
        row.push(std::move(fx));
        steps.push(std::move(row));
    }
    return j;
}

void readPattern(const Json& j, Pattern& p) {
    p.clear();
    p.length = std::clamp(j["length"].integer(16), 1, Pattern::kMaxSteps);
    const Json& steps = j["steps"];
    for (size_t k = 0; k < steps.size(); ++k) {
        const Json& row = steps[k];
        int         i   = row[0].integer(-1);
        if (i < 0 || i >= Pattern::kMaxSteps) continue;
        SeqStep&    st    = p.steps[i];
        const Json& notes = row[1];
        for (size_t n = 0; n < notes.size(); ++n) {
            const Json& e = notes[n];
            int         c = e[0].integer(-1);
            if (c < 0 || c >= SeqStep::kMaxNotes) continue;
            SeqNote& sn = st.notes[c];
            int      pitch = e[1].integer(SeqNote::kEmpty);
            if (pitch != SeqNote::kOff && (pitch < 0 || pitch > 127)) continue;
            sn.pitch = int8_t(pitch);
            sn.vel   = uint8_t(std::clamp(e[2].integer(kDefaultVel), 1, kMaxVel));
            int fx   = std::clamp(e[3].integer(0), 0, int(Fx::Count) - 1);
            sn.fx    = Fx(fx);
            sn.fxVal = uint8_t(fxClamp(sn.fx, e[4].integer(0)));
        }
        const Json& fx = row[2];
        for (int f = 0; f < SeqStep::kFx && f < int(fx.size()); ++f) {
            int cmd     = std::clamp(fx[f][0].integer(0), 0, int(Fx::Count) - 1);
            st.fx[f].fx  = Fx(cmd);
            st.fx[f].val = uint8_t(fxClamp(st.fx[f].fx, fx[f][1].integer(0)));
        }
    }
}

// ---------------------------------------------------------------- paths

std::string relativeTo(const std::string& path, const std::string& base) {
    if (base.empty()) return path;
    std::error_code ec;
    fs::path rel = fs::relative(fs::path(path), fs::path(base), ec);
    if (ec || rel.empty() || *rel.begin() == "..") return path;
    return rel.generic_string();
}

std::string resolve(const std::string& path, const std::string& base) {
    fs::path p(path);
    if (p.is_absolute() || base.empty()) return path;
    return (fs::path(base) / p).string();
}

// ---------------------------------------------------------------- whole project

Json projectJson(App& a) {
    Pattern live[kTracks];
    int     slot[kTracks];
    bool    mute[kTracks];
    float   volume[kTracks], pan[kTracks], send[kTracks], duck[kTracks];
    int     solo;
    float   bpm, swing, revSize, revTone, duckTime;
    {
        AudioLock lock(a.audio);
        for (int t = 0; t < kTracks; ++t) {
            live[t]   = a.seq.patterns[t];
            slot[t]   = a.seq.slot[t];
            mute[t]   = a.seq.mute[t];
            volume[t] = a.seq.volume[t];
            pan[t]    = a.seq.pan[t];
            send[t]   = a.seq.send[t];
            duck[t]   = a.seq.duck[t];
        }
        revSize  = a.seq.reverbSize;
        revTone  = a.seq.reverbTone;
        duckTime = a.seq.duckTime;
        solo  = a.seq.solo;
        bpm   = a.seq.bpm();
        swing = a.seq.swing();
    }

    Json j = Json::object();
    j.set("app", "knulli-first-synth");
    j.set("version", kVersion);
    j.set("bpm", bpm);
    j.set("root", a.root);
    j.set("scale", kScales[a.scale].name);
    j.set("swing", swing);
    j.set("groove", a.groove >= 0 ? grooveName(a) : "");
    j.set("grooveTiming", a.grooveTiming);
    j.set("grooveVelocity", a.grooveVelocity);
    j.set("solo", solo);
    j.set("reverbSize", revSize);
    j.set("reverbTone", revTone);
    j.set("duckRelease", duckTime);

    Json& tracks = j.set("tracks", Json::array());
    for (int t = 0; t < kTracks; ++t) {
        Json tr = Json::object();
        tr.set("type", a.isDrum[t] ? "drum" : "synth");
        tr.set("volume", volume[t]);
        tr.set("mute", mute[t]);
        tr.set("pan", pan[t]);
        tr.set("reverb", send[t]);
        tr.set("duck", duck[t]);

        Json& syn = tr.set("synth", Json::object());
        syn.set("preset", kPresets[a.preset[t]].name);
        syn.set("edited", a.edited[t]);
        const Params& p = a.synths[t].params;
        for (const IntParam& ip : kInts) syn.set(ip.key, p.*ip.i);
        for (const FloatParam& fp : kFloats) syn.set(fp.key, p.*fp.f);

        Json& drum = tr.set("drum", Json::object());
        drum.set("kit", !a.kits.empty() ? a.kits[a.kit[t]].name : "");
        Json& pads = drum.set("pads", Json::array());
        for (int i = 0; i < Kit::kPads; ++i) {
            const DrumKit::PadSettings& ps = a.drums[t].pads[i];
            Json pad = Json::object();
            pad.set("sample", a.usesOwnKit[t] && !a.padPath[t][i].empty() ? relativeTo(a.padPath[t][i], a.samplesDir) : "");
            pad.set("pitch", ps.pitch);
            pad.set("start", ps.start);
            pad.set("end", ps.end);
            pad.set("volume", ps.volume);
            pad.set("pan", ps.pan);
            pads.push(std::move(pad));
        }

        const DrumGen& dg = a.drumGen[t];
        Json& gd = tr.set("drumGen", Json::object());
        gd.set("style", int(dg.style));
        gd.set("density", dg.density);
        gd.set("variation", dg.variation);
        gd.set("dynamics", dg.dynamics);
        gd.set("seed", dg.seed);
        const MelodyGen& mg = a.melodyGen[t];
        Json& gm = tr.set("melodyGen", Json::object());
        gm.set("octave", mg.octave);
        gm.set("density", mg.density);
        gm.set("range", mg.range);
        gm.set("motion", mg.motion);
        gm.set("length", mg.length);
        gm.set("chord", mg.chord);
        gm.set("dynamics", mg.dynamics);
        gm.set("seed", mg.seed);

        const Evolve& ev = a.evolve[t];
        Json& evo = tr.set("evolve", Json::object());
        evo.set("every", kEvolveEvery[ev.every]);
        evo.set("amount", ev.amount);
        evo.set("mode", ev.vary ? "vary" : "drift");

        tr.set("slot", slot[t]);
        tr.set("pattern", patternJson(live[t]));
        Json& clips = tr.set("clips", Json::array());
        for (int s = 0; s < kSlots; ++s) {
            const Clip& c = a.clips[t][s];
            if (!c.used) {
                clips.push(Json());
                continue;
            }
            Json cj = Json::object();
            cj.set("drum", c.drum);
            cj.set("pattern", patternJson(c.pat));
            clips.push(std::move(cj));
        }
        tracks.push(std::move(tr));
    }
    return j;
}

void applyProject(App& a, const Json& j) {
    {
        AudioLock lock(a.audio);
        a.seq.setBpm(float(j["bpm"].num(a.seq.bpm())));
        a.seq.setSwing(float(j["swing"].num(a.seq.swing())));
        a.seq.solo = std::clamp(j["solo"].integer(-1), -1, kTracks - 1);
        a.seq.reverbSize = std::clamp(float(j["reverbSize"].num(a.seq.reverbSize)), 0.0f, 1.0f);
        a.seq.reverbTone = std::clamp(float(j["reverbTone"].num(a.seq.reverbTone)), 0.0f, 1.0f);
        a.seq.duckTime   = std::clamp(float(j["duckRelease"].num(a.seq.duckTime)), 0.0f, 1.0f);
        a.seq.reverb.setSize(a.seq.reverbSize);
        a.seq.reverb.setTone(a.seq.reverbTone);
    }
    a.root = std::clamp(j["root"].integer(a.root), 0, 11);
    std::string scale = j["scale"].str();
    for (int s = 0; s < kScaleCount; ++s)
        if (scale == kScales[s].name) a.scale = s;

    std::string groove = j["groove"].str();
    a.groove           = -1;
    for (int g = 0; g < int(a.grooves.size()); ++g)
        if (a.grooves[g].name == groove) a.groove = g;
    a.grooveTiming   = float(j["grooveTiming"].num(a.grooveTiming));
    a.grooveVelocity = float(j["grooveVelocity"].num(a.grooveVelocity));
    {
        AudioLock lock(a.audio);
        applyGroove(a);
    }

    const Json& tracks = j["tracks"];
    for (int t = 0; t < kTracks && t < int(tracks.size()); ++t) {
        const Json& tr = tracks[t];
        setTrackType(a, t, tr["type"].str() == "drum");

        const Json& syn    = tr["synth"];
        std::string preset = syn["preset"].str();
        for (int i = 0; i < kPresetCount; ++i)
            if (preset == kPresets[i].name) a.preset[t] = i;
        {
            AudioLock lock(a.audio);
            Params& p = a.synths[t].params;
            p         = kPresets[a.preset[t]].params;
            for (const IntParam& ip : kInts) p.*ip.i = syn[ip.key].integer(p.*ip.i);
            for (const FloatParam& fp : kFloats) p.*fp.f = float(syn[fp.key].num(p.*fp.f));
            p.wave      = std::clamp(p.wave, 0, int(Wave::Count) - 1);
            p.filter    = std::clamp(p.filter, 0, int(FilterType::Count) - 1);
            p.transpose = std::clamp(p.transpose, -24, 24);
            p.unison    = std::clamp(p.unison, 0, kMaxUnison - 1);
            for (int* e : {&p.envDest, &p.lfo1Dest, &p.lfo2Dest}) *e = std::clamp(*e, 0, int(ModDest::Count) - 1);
            for (int* e : {&p.lfo1Shape, &p.lfo2Shape}) *e = std::clamp(*e, 0, int(LfoShape::Count) - 1);
        }
        a.edited[t] = syn["edited"].boolean(false);

        const Json& drum = tr["drum"];
        std::string kit  = drum["kit"].str();
        for (int k = 0; k < int(a.kits.size()); ++k)
            if (a.kits[k].name == kit) loadKit(a, t, k);
        const Json& pads = drum["pads"];
        for (int i = 0; i < Kit::kPads && i < int(pads.size()); ++i) {
            const Json& pj = pads[i];
            {
                AudioLock lock(a.audio);
                DrumKit::PadSettings& ps = a.drums[t].pads[i];
                ps.pitch  = std::clamp(pj["pitch"].integer(ps.pitch), -24, 24);
                ps.start  = std::clamp(float(pj["start"].num(ps.start)), 0.0f, 1.0f);
                ps.end    = std::clamp(float(pj["end"].num(ps.end)), 0.0f, 1.0f);
                ps.volume = std::clamp(float(pj["volume"].num(ps.volume)), 0.0f, 1.0f);
                ps.pan    = std::clamp(float(pj["pan"].num(0.0)), -1.0f, 1.0f);
            }
            std::string sample = pj["sample"].str();
            if (!sample.empty()) setPadSample(a, t, i, resolve(sample, a.samplesDir));
        }

        DrumGen&    dg = a.drumGen[t];
        const Json& gd = tr["drumGen"];
        dg.style       = DrumStyle(std::clamp(gd["style"].integer(int(dg.style)), 0, int(DrumStyle::Count) - 1));
        dg.density     = float(gd["density"].num(dg.density));
        dg.variation   = float(gd["variation"].num(dg.variation));
        dg.dynamics    = float(gd["dynamics"].num(dg.dynamics));
        dg.seed        = uint32_t(gd["seed"].num(dg.seed));
        MelodyGen&  mg = a.melodyGen[t];
        const Json& gm = tr["melodyGen"];
        mg.octave      = std::clamp(gm["octave"].integer(mg.octave), 1, 7);
        mg.density     = float(gm["density"].num(mg.density));
        mg.range       = float(gm["range"].num(mg.range));
        mg.motion      = float(gm["motion"].num(mg.motion));
        mg.length      = float(gm["length"].num(mg.length));
        mg.chord       = float(gm["chord"].num(mg.chord));
        mg.dynamics    = float(gm["dynamics"].num(mg.dynamics));
        mg.seed        = uint32_t(gm["seed"].num(mg.seed));

        const Json& evo = tr["evolve"];
        Evolve&     ev  = a.evolve[t];
        int every       = evo["every"].integer(0);
        ev.every        = 0;
        for (int i = 0; i < Evolve::kChoices; ++i)
            if (kEvolveEvery[i] == every) ev.every = i;
        ev.amount  = std::clamp(float(evo["amount"].num(ev.amount)), 0.0f, 1.0f);
        ev.vary    = evo["mode"].str("vary") != "drift";
        ev.hasBase = false;

        const Json& clips = tr["clips"];
        for (int s = 0; s < kSlots; ++s) {
            Clip&       c  = a.clips[t][s];
            const Json& cj = clips[size_t(s)];
            c.used         = cj.type == Json::Obj;
            if (!c.used) continue;
            c.drum = cj["drum"].boolean(false);
            readPattern(cj["pattern"], c.pat);
        }

        AudioLock lock(a.audio);
        if (tr.has("pattern")) readPattern(tr["pattern"], a.seq.patterns[t]);
        a.seq.slot[t]   = std::clamp(tr["slot"].integer(-1), -1, kSlots - 1);
        a.seq.volume[t] = std::clamp(float(tr["volume"].num(1.0)), 0.0f, Sequencer::kMaxVolume);
        a.seq.mute[t]   = tr["mute"].boolean(false);
        a.seq.pan[t]    = std::clamp(float(tr["pan"].num(0.0)), -1.0f, 1.0f);
        a.seq.send[t]   = std::clamp(float(tr["reverb"].num(0.0)), 0.0f, 1.0f);
        a.seq.duck[t]   = std::clamp(float(tr["duck"].num(0.0)), 0.0f, 1.0f);
        a.hasUndo[t]    = false;
    }
}

bool writeFile(const std::string& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        if (!f.good()) return false;
    }
    fs::rename(tmp, path, ec);
    if (ec) { // some platforms will not rename over an existing file
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
    }
    return !ec;
}

} // namespace

bool saveProject(App& a, bool force) {
    if (a.projectPath.empty()) return false;
    std::string text = projectJson(a).dump(2) + "\n";
    if (!force && text == a.savedText) return false;
    if (!writeFile(a.projectPath, text)) {
        a.message = "CANNOT SAVE THE PROJECT";
        SDL_Log("cannot write %s", a.projectPath.c_str());
        return false;
    }
    a.savedText = std::move(text);
    return true;
}

bool loadProject(App& a) {
    std::ifstream f(a.projectPath, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    Json        j;
    std::string error;
    if (!parseJson(ss.str(), j, error)) {
        SDL_Log("project %s: %s", a.projectPath.c_str(), error.c_str());
        a.message = "PROJECT FILE IS BROKEN: " + fontSafe(error);
        // Keep the broken file for a look on the PC; the next save starts a new one.
        std::error_code ec;
        fs::rename(a.projectPath, a.projectPath + ".broken", ec);
        return false;
    }
    applyProject(a, j);
    a.savedText = projectJson(a).dump(2) + "\n";
    SDL_Log("project loaded: %s", a.projectPath.c_str());
    return true;
}

void autosave(App& a) {
    Uint32 now = SDL_GetTicks();
    if (!SDL_TICKS_PASSED(now, a.saveCheckAt)) return;
    a.saveCheckAt = now + kCheckEvery;
    saveProject(a);
}
