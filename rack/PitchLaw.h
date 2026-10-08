#pragma once
// Pitch laws (Jidai Cable Standard v1.1, JCS R4). Framework-free.
//   V/OCT     the rack standard: 0 V = C3 = 130.8128 Hz = MIDI 48, one volt per octave.     note = 48 + 12 V
//   HZ/V LIN  linear: 1 V = C3 = 130.8128 Hz at RONIN 8', double the volts = one octave up. note = 48 + 12 log2 V
// The CV jacks always stay in volts: a law only decides what those volts mean (note names, QUANT, MIDI, cable role).
// The old 55 Hz / MIDI 33 reference is retired everywhere.
#include <cmath>
#include <cstdio>

namespace rack::pitch {

enum class Law { VOct = 0, HzvLin = 1 };

constexpr double kC3Hz    = 130.8128;   // MIDI 48
constexpr double kRefNote = 48.0;
constexpr double kRail    = 5.0;        // JCS R4.4: a pitch source never leaves +-5 V
constexpr double kLinFloor = 1.0 / 32.0;   // HZ/V LIN QUANT: below 2^-5 V the row is treated as silent (0 V)

// Fractional MIDI note for a voltage, or NaN when the law has no note there (HZ/V LIN at or below 0 V).
inline double note(Law law, double volts)
{
    if (law == Law::VOct) return kRefNote + 12.0 * volts;
    return volts > 0.0 ? kRefNote + 12.0 * std::log2(volts) : std::nan("");
}

inline double hz(Law law, double volts)
{
    if (law == Law::VOct) return kC3Hz * std::exp2(volts);
    return volts > 0.0 ? kC3Hz * volts : 0.0;
}

// Nearest MIDI note (JCS R4.5), clamped to 0..127; -1 when there is no note (HZ/V LIN at or below 0 V).
inline int midiNote(Law law, double volts)
{
    const double n = note(law, volts);
    if (std::isnan(n)) return -1;
    const long r = std::lround(n);
    return (int) (r < 0 ? 0 : r > 127 ? 127 : r);
}

// QUANT SEMI: snap to the nearest semitone under the row's law, never past the +5 V rail.
//   V/OCT     round(12 V) / 12
//   HZ/V LIN  2^(round(12 log2 V) / 12) for V >= 2^-5 V, else 0 V (5 V gives note 75, 4.757 V, not 76 at 5.04 V)
inline double quantize(Law law, double volts)
{
    if (law == Law::VOct) {
        double s = std::round(12.0 * volts);
        if (s / 12.0 > kRail) s -= 1.0;
        if (s / 12.0 < -kRail) s += 1.0;
        return s / 12.0;
    }
    if (volts < kLinFloor) return 0.0;
    double s = std::round(12.0 * std::log2(volts));
    if (std::exp2(s / 12.0) > kRail) s -= 1.0;
    return std::exp2(s / 12.0);
}

// "C3", "F#4": the note name for a MIDI note number (C3 = 48, so C-1 = 0).
inline const char* noteName(int midi, char* buf, int size)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (midi < 0) { std::snprintf(buf, (size_t) size, "--"); return buf; }
    std::snprintf(buf, (size_t) size, "%s%d", names[midi % 12], midi / 12 - 1);
    return buf;
}

} // namespace rack::pitch
