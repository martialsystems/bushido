#pragma once
// MIDI convenience out (framework-free so it can be tested without JUCE). It is a convenience, not the patch:
// the CV jacks carry the music. A gate rise on a jack pair sends a note-on, a gate fall the matching note-off.
//   note     from the jack's TARGET volts at the rise (never the slewed CV, so PORTA cannot change it) under the
//            jack's PITCH LAW: V/OCT round(48 + 12 V), HZ/V LIN round(48 + 12 log2 V), no note at or below 0 V (JCS R4.5).
//   channel  MIDI tab, CH A / CH B (1 and 2 by default).
//   velocity 100, or FROM C: round(1 + 126 C / 5) from row C when C MODE = CV.
#include "BushidoModule.h"
#include <cmath>

class BushidoMidiOut {
public:
    struct Msg { long long sample; int channel; int note; int velocity; bool on; };

    // Turns the engine's gate events into note messages. emit(Msg) is called in order.
    template <typename Emit>
    void handle(const BushidoModule& m, const BushidoModule::GateEvent* ev, int count, Emit&& emit)
    {
        for (int e = 0; e < count; ++e) {
            const auto& g = ev[e]; const int j = g.jack;
            if (g.on) {
                if (note[j] >= 0) emit(Msg { g.sample, chan[j], note[j], 0, false });
                note[j] = rack::pitch::midiNote(m.law(j), g.target);
                chan[j] = channel(m, j);
                if (note[j] >= 0) emit(Msg { g.sample, chan[j], note[j], velocity(m, j, g.cvC), true });
            } else if (note[j] >= 0) { emit(Msg { g.sample, chan[j], note[j], 0, false }); note[j] = -1; }
        }
    }
    // All notes off (bypass, transport stop, release).
    template <typename Emit>
    void allOff(long long sample, Emit&& emit)
    { for (int j = 0; j < 2; ++j) if (note[j] >= 0) { emit(Msg { sample, chan[j], note[j], 0, false }); note[j] = -1; } }

    static int channel(const BushidoModule& m, int jack)
    { return 1 + (int) std::lround(m.getParam(jack == 0 ? BushidoModule::MIDI_CH_A : BushidoModule::MIDI_CH_B) * 15.0f); }
    static int velocity(const BushidoModule& m, int jack, float cvC)
    {
        const bool fromC = m.getParam(jack == 0 ? BushidoModule::VEL_A : BushidoModule::VEL_B) > 0.5f && m.getParam(BushidoModule::C_MODE) < 0.5f;
        if (! fromC) return 100;
        const long v = std::lround(1.0 + 126.0 * (double) cvC / 5.0);
        return (int) (v < 1 ? 1 : v > 127 ? 127 : v);
    }

private:
    int note[2] = { -1, -1 }, chan[2] = { 1, 2 };
};
