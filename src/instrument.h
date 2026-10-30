#pragma once

// What a track plays: the subtractive Synth or a DrumKit. The sequencer and the
// pads only talk to this interface. render() runs on the audio thread; the
// rest is called with the audio device locked.
class Instrument {
public:
    virtual ~Instrument() = default;

    // `key` identifies who started the note (a pad, or a sequencer slot) so the
    // matching noteOff finds it. `note` is a MIDI note number.
    virtual void noteOn(int key, int note) = 0;
    virtual void noteOff(int key)          = 0;
    virtual void allOff()                  = 0;

    // Writes (not adds) `frames` mono samples.
    virtual void render(float* out, int frames) = 0;
    virtual int  activeVoices() const           = 0;
};
