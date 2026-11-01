// Knulli First Synth - a small 4-track groovebox for the TrimUI Brick (Knulli).
// SDL2 only: video via SDL_Renderer, audio via SDL audio callback.
//
// This file owns the audio callback, input mapping and the main loop; the
// screens live in ui_*.cpp and share the state in app.h.

#include <SDL.h>

#include <algorithm>
#include <cstdlib>
#include <string>

#include "app.h"
#include "gfx.h"

namespace {

// Fallback for joysticks SDL has no GameController mapping for. Indices are a
// guess; unmapped presses are shown on the message line so this can be corrected on device.
const Btn kRawJoyButtons[] = {BTN_A, BTN_B, BTN_X, BTN_Y, BTN_L1, BTN_R1, BTN_SELECT, BTN_START, BTN_MENU, BTN_L2, BTN_R2};

constexpr Uint32 kRepeatDelay = 300; // ms before a held D-pad direction repeats
constexpr Uint32 kRepeatRate  = 50;

// ---------------------------------------------------------------- audio

void audioCallback(void* userdata, Uint8* stream, int len) {
    const Uint64 start  = SDL_GetPerformanceCounter();
    App&         a      = *static_cast<App*>(userdata);
    float*       out    = reinterpret_cast<float*>(stream);
    const int    total  = len / int(sizeof(float) * a.channels);
    int          frames = total;
    while (frames > 0) {
        int n = std::min(frames, int(a.mono.size()));
        a.seq.process(a.inst, a.mono.data(), n);
        for (int i = 0; i < n; ++i) {
            for (int c = 0; c < a.channels; ++c) *out++ = a.mono[i];
            a.scope[a.scopePos] = a.mono[i];
            a.scopePos          = (a.scopePos + 1) % 2048;
        }
        frames -= n;
    }

    const double elapsed = double(SDL_GetPerformanceCounter() - start) / double(SDL_GetPerformanceFrequency());
    const float  load    = float(elapsed * a.sampleRate / total);
    a.dspLoad.store(a.dspLoad.load(std::memory_order_relaxed) * 0.9f + load * 0.1f, std::memory_order_relaxed);
    if (load > a.dspPeak.load(std::memory_order_relaxed)) a.dspPeak.store(load, std::memory_order_relaxed);
}

// ---------------------------------------------------------------- actions

int pageCount(const App& a) {
    int len = 0;
    if (a.screen == SCR_SEQ) {
        for (int t = 0; t < kTracks; ++t) len = std::max(len, a.seq.patterns[t].length);
    } else {
        len = a.seq.patterns[a.track].length;
    }
    return std::max(1, (len + kPageSteps - 1) / kPageSteps);
}

void selectTrack(App& a, int t) {
    a.track = std::clamp(t, 0, kTracks - 1);
    if (a.seqCur.row >= 0) a.seqCur.col = a.track;
}

void togglePlay(App& a) {
    AudioLock lock(a.audio);
    if (a.seq.playing()) a.seq.stop(a.inst);
    else a.seq.start();
}

// X: a fresh seed for the current track's generator.
void newSeed(App& a) {
    const int t    = a.track;
    uint32_t  seed = (SDL_GetTicks() * 2654435761u) % 9999 + 1;
    AudioLock lock(a.audio);
    pushUndo(a, t);
    if (a.isDrum[t]) a.drumGen[t].seed = seed;
    else a.melodyGen[t].seed = seed;
    regenerate(a, t);
    a.message = fmt("T%d: NEW PATTERN, SEED %u  (SEL+B: UNDO)", t + 1, seed);
}

void mutateTrack(App& a) {
    AudioLock lock(a.audio);
    pushUndo(a, a.track);
    mutate(a, a.track);
    a.message = fmt("T%d: MUTATED  (SEL+B: UNDO)", a.track + 1);
}

void onSelectCombo(App& a, Btn b) {
    switch (b) {
    case BTN_LEFT:
    case BTN_RIGHT:
        a.screen = Screen(std::clamp(int(a.screen) + (b == BTN_LEFT ? -1 : 1), 0, SCR_COUNT - 1));
        break;
    case BTN_B: {
        AudioLock lock(a.audio);
        if (!a.hasUndo[a.track]) {
            a.message = "NOTHING TO UNDO";
            break;
        }
        toggleUndo(a, a.track);
        a.message = fmt("T%d: UNDO / REDO", a.track + 1);
        break;
    }
    case BTN_X: {
        AudioLock lock(a.audio);
        pushUndo(a, a.track);
        Pattern& p   = a.seq.patterns[a.track];
        int      len = p.length;
        p.clear();
        p.length  = len;
        a.message = fmt("T%d: CLEARED  (SEL+B: UNDO)", a.track + 1);
        break;
    }
    case BTN_START: a.quit = true; break;
    default: break;
    }
}

// D-pad: moves the cursor, or with A held edits what is under it.
void onDirection(App& a, Btn b) {
    const ScreenOps& ops = screenOps(a.screen);
    int dx = b == BTN_LEFT ? -1 : b == BTN_RIGHT ? 1 : 0;
    int dy = b == BTN_UP ? -1 : b == BTN_DOWN ? 1 : 0;
    if (a.held[BTN_A] && a.aScreen == a.screen) ops.edit(a, dx, -dy);
    else ops.move(a, dx, dy);
}

void onPress(App& a, Btn b) {
    a.held[b]     = true;
    a.repeatAt[b] = SDL_GetTicks() + kRepeatDelay;
    if (a.held[BTN_SELECT] && b != BTN_SELECT) {
        onSelectCombo(a, b);
        return;
    }
    const ScreenOps& ops = screenOps(a.screen);
    switch (b) {
    case BTN_UP: case BTN_DOWN: case BTN_LEFT: case BTN_RIGHT:
        onDirection(a, b);
        break;
    case BTN_A:
        a.aScreen = a.screen;
        if (a.held[BTN_B]) ops.clear(a);
        else ops.enter(a);
        break;
    case BTN_X:     newSeed(a); break;
    case BTN_Y:     mutateTrack(a); break;
    case BTN_START: togglePlay(a); break;
    case BTN_L1:    selectTrack(a, a.track - 1); break;
    case BTN_R1:    selectTrack(a, a.track + 1); break;
    case BTN_L2:    a.page = std::max(0, a.page - 1); break;
    case BTN_R2:    a.page = std::min(pageCount(a) - 1, a.page + 1); break;
    case BTN_L3: {
        AudioLock lock(a.audio);
        a.seq.mute[a.track] = !a.seq.mute[a.track];
        break;
    }
    case BTN_R3: {
        AudioLock lock(a.audio);
        a.seq.solo = a.seq.solo == a.track ? -1 : a.track;
        break;
    }
    default: break;
    }
}

void setButton(App& a, Btn b, bool down) {
    if (down) {
        if (!a.held[b]) onPress(a, b);
    } else {
        a.held[b] = false;
    }
}

void handleRepeat(App& a) {
    if (a.held[BTN_SELECT]) return;
    Uint32 now = SDL_GetTicks();
    for (Btn b : {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT}) {
        if (a.held[b] && SDL_TICKS_PASSED(now, a.repeatAt[b])) {
            onDirection(a, b);
            a.repeatAt[b] = now + kRepeatRate;
        }
    }
}

// ---------------------------------------------------------------- input

int keyToBtn(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP:    case SDL_SCANCODE_W: return BTN_UP;
    case SDL_SCANCODE_DOWN:  case SDL_SCANCODE_S: return BTN_DOWN;
    case SDL_SCANCODE_LEFT:  case SDL_SCANCODE_A: return BTN_LEFT;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: return BTN_RIGHT;
    case SDL_SCANCODE_K: return BTN_A;
    case SDL_SCANCODE_L: return BTN_B;
    case SDL_SCANCODE_J: return BTN_X;
    case SDL_SCANCODE_I: return BTN_Y;
    case SDL_SCANCODE_Q: return BTN_L1;
    case SDL_SCANCODE_E: return BTN_R1;
    case SDL_SCANCODE_Z: return BTN_L2;
    case SDL_SCANCODE_C: return BTN_R2;
    case SDL_SCANCODE_TAB: case SDL_SCANCODE_BACKSPACE: return BTN_SELECT;
    case SDL_SCANCODE_RETURN: return BTN_START;
    case SDL_SCANCODE_T: return BTN_L3;
    case SDL_SCANCODE_R: return BTN_R3;
    default: return -1;
    }
}

int controllerToBtn(Uint8 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return BTN_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return BTN_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return BTN_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return BTN_RIGHT;
    case SDL_CONTROLLER_BUTTON_A:             return BTN_A;
    case SDL_CONTROLLER_BUTTON_B:             return BTN_B;
    case SDL_CONTROLLER_BUTTON_X:             return BTN_X;
    case SDL_CONTROLLER_BUTTON_Y:             return BTN_Y;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return BTN_L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return BTN_R1;
    case SDL_CONTROLLER_BUTTON_BACK:          return BTN_SELECT;
    case SDL_CONTROLLER_BUTTON_START:         return BTN_START;
    case SDL_CONTROLLER_BUTTON_GUIDE:         return BTN_MENU;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return BTN_L3;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return BTN_R3;
    default: return -1;
    }
}

void openDevice(App& a, int index) {
    if (SDL_IsGameController(index)) {
        SDL_GameController* gc = SDL_GameControllerOpen(index);
        if (!gc) return;
        SDL_Joystick* js = SDL_GameControllerGetJoystick(gc);
        a.controllerIds.insert(SDL_JoystickInstanceID(js));
        char guid[64];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), guid, sizeof(guid));
        SDL_Log("controller: %s guid=%s", SDL_GameControllerName(gc), guid);
        a.message = fmt("CONNECTED PAD: %s", SDL_GameControllerName(gc));
    } else {
        SDL_Joystick* js = SDL_JoystickOpen(index);
        if (!js) return;
        char guid[64];
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(js), guid, sizeof(guid));
        SDL_Log("raw joystick: %s guid=%s buttons=%d axes=%d hats=%d", SDL_JoystickName(js), guid,
                SDL_JoystickNumButtons(js), SDL_JoystickNumAxes(js), SDL_JoystickNumHats(js));
        a.message = fmt("CONNECTED RAW JOY: %s", SDL_JoystickName(js));
    }
}

void handleEvent(App& a, const SDL_Event& e) {
    switch (e.type) {
    case SDL_QUIT:
        a.quit = true;
        break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        if (e.key.repeat) break;
        if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { a.quit = true; break; }
        int b = keyToBtn(e.key.keysym.scancode);
        if (b >= 0) setButton(a, Btn(b), e.type == SDL_KEYDOWN);
        break;
    }
    case SDL_JOYDEVICEADDED:
        openDevice(a, e.jdevice.which);
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        int b = controllerToBtn(e.cbutton.button);
        if (b >= 0) setButton(a, Btn(b), e.type == SDL_CONTROLLERBUTTONDOWN);
        else if (e.type == SDL_CONTROLLERBUTTONDOWN)
            a.message = fmt("UNMAPPED PAD BUTTON %s",
                            SDL_GameControllerGetStringForButton(SDL_GameControllerButton(e.cbutton.button)));
        break;
    }
    case SDL_CONTROLLERAXISMOTION: {
        int t = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 0
              : e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? 1 : -1;
        if (t < 0) break;
        bool down = a.triggerDown[t] ? e.caxis.value > 8000 : e.caxis.value > 16000;
        if (down != a.triggerDown[t]) {
            a.triggerDown[t] = down;
            setButton(a, t == 0 ? BTN_L2 : BTN_R2, down);
        }
        break;
    }
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        if (a.controllerIds.count(e.jbutton.which)) break;
        int n = sizeof(kRawJoyButtons) / sizeof(kRawJoyButtons[0]);
        int b = e.jbutton.button < n ? kRawJoyButtons[e.jbutton.button] : -1;
        if (e.type == SDL_JOYBUTTONDOWN)
            a.message = fmt("RAW JOY BUTTON %d -> %s", e.jbutton.button, b >= 0 ? kBtnNames[b] : "-");
        if (b >= 0) setButton(a, Btn(b), e.type == SDL_JOYBUTTONDOWN);
        break;
    }
    case SDL_JOYHATMOTION: {
        if (a.controllerIds.count(e.jhat.which) || e.jhat.hat >= 8) break;
        Uint8 prev = a.hatState[e.jhat.hat];
        Uint8 now  = e.jhat.value;
        a.hatState[e.jhat.hat] = now;
        const struct { Uint8 bit; Btn btn; } dirs[] = {
            {SDL_HAT_UP, BTN_UP}, {SDL_HAT_DOWN, BTN_DOWN}, {SDL_HAT_LEFT, BTN_LEFT}, {SDL_HAT_RIGHT, BTN_RIGHT}};
        for (const auto& d : dirs)
            if ((prev ^ now) & d.bit) setButton(a, d.btn, (now & d.bit) != 0);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------- frame

void draw(App& a) {
    SDL_Renderer* r = a.renderer;
    SDL_SetRenderDrawColor(r, kBg.r, kBg.g, kBg.b, 255);
    SDL_RenderClear(r);
    screenOps(a.screen).draw(a);
    drawChrome(a);
    SDL_RenderPresent(r);
}

void releasePreview(App& a) {
    if (a.previewKey < 0 || !SDL_TICKS_PASSED(SDL_GetTicks(), a.previewOffAt)) return;
    AudioLock lock(a.audio);
    a.inst[a.previewTrack]->noteOff(a.previewKey);
    a.previewKey = -1;
}

// Something to hear on the first START: a bass line on track 2 and a beat on track 4.
void makeDefaultSong(App& a) {
    AudioLock lock(a.audio);
    a.melodyGen[1].octave  = 2;
    a.melodyGen[1].density = 0.6f;
    a.melodyGen[1].length  = 0.2f;
    regenerate(a, 1);
    if (a.isDrum[kTracks - 1]) regenerate(a, kTracks - 1);
    for (bool& h : a.hasUndo) h = false;
}

} // namespace

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    App a;
    initFont();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    SDL_version v;
    SDL_GetVersion(&v);
    SDL_Log("SDL %d.%d.%d video=%s audio=%s", v.major, v.minor, v.patch, SDL_GetCurrentVideoDriver(),
            SDL_GetCurrentAudioDriver());

    int added = SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
    if (added > 0) SDL_Log("loaded %d controller mappings", added);

#if defined(_WIN32) || defined(__APPLE__)
    bool windowed = true;
#else
    bool windowed = std::getenv("KSYNTH_WINDOWED") != nullptr;
#endif
    Uint32 wflags = windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP;
    a.window = SDL_CreateWindow("Knulli First Synth", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kW, kH, wflags);
    if (!a.window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }
    a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!a.renderer) a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_SOFTWARE);
    if (!a.renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        return 1;
    }
    SDL_RendererInfo ri;
    SDL_GetRendererInfo(a.renderer, &ri);
    SDL_Log("renderer=%s", ri.name);
    SDL_RenderSetLogicalSize(a.renderer, kW, kH);
    if (!windowed) SDL_ShowCursor(SDL_DISABLE);

    SDL_AudioSpec want{}, have{};
    want.freq     = 48000;
    want.format   = AUDIO_F32SYS;
    want.channels = 2;
    want.samples  = 512;
    want.callback = audioCallback;
    want.userdata = &a;
    a.audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
                                  SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE |
                                      SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (a.audio) {
        a.channels   = have.channels;
        a.sampleRate = have.freq;
        a.mono.resize(std::max<int>(have.samples, 512));
        for (Synth& s : a.synths) s.init(have.freq);
        for (DrumKit& d : a.drums) d.init(have.freq);
        a.seq.init(have.freq);
        SDL_Log("audio: %d Hz, %d ch, %d samples", have.freq, have.channels, have.samples);
    } else {
        SDL_Log("SDL_OpenAudioDevice failed: %s", SDL_GetError());
        for (Synth& s : a.synths) s.init(48000);
        for (DrumKit& d : a.drums) d.init(48000);
        a.seq.init(48000);
    }

    // Drum kits live in samples/kits/<kit>/*.wav next to the binary (on the device),
    // or under the working directory (running from the repo on PC).
    {
        char*       base = SDL_GetBasePath();
        std::string dir  = std::string(base ? base : "") + "samples/kits";
        SDL_free(base);
        a.kits = loadKits(dir);
        if (a.kits.empty()) a.kits = loadKits("samples/kits");
        SDL_Log("drum kits: %d", int(a.kits.size()));
    }
    for (int t = 0; t < kTracks; ++t) {
        a.inst[t] = &a.synths[t];
        loadPreset(a, t, a.preset[t]);
        loadKit(a, t, 0);
    }
    setTrackType(a, kTracks - 1, true); // the last track starts as drums when a kit is available
    makeDefaultSong(a);
    a.message = "START: PLAY   X: NEW PATTERN   SEL+LEFT/RIGHT: SCREENS";
    if (a.audio) SDL_PauseAudioDevice(a.audio, 0);

    while (!a.quit) {
        Uint32    frameStart = SDL_GetTicks();
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(a, e);
        handleRepeat(a);
        releasePreview(a);
        a.page = std::min(a.page, pageCount(a) - 1);
        draw(a);
        Uint32 elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < 16) SDL_Delay(16 - elapsed); // in case vsync is unavailable
    }

    if (a.audio) SDL_CloseAudioDevice(a.audio);
    SDL_DestroyRenderer(a.renderer);
    SDL_DestroyWindow(a.window);
    SDL_Quit();
    return 0;
}
