#pragma once
// Hz/V pitch law, as on Korg MS-series VCOs: frequency is proportional to the control voltage,
// so doubling the voltage raises the pitch one octave. 1 V = A1 = 55 Hz (MIDI note 33).
// Framework-free. Used only for the plugin's MIDI convenience output; the CV jacks stay in volts.
#include <cmath>

namespace rack::hzv {

constexpr float kHzPerVolt = 55.0f;     // 1 V -> 55 Hz
constexpr float kRefNote   = 33.0f;     // MIDI note of 55 Hz (A1)

inline float hz(float volts) { return volts > 0.0f ? volts * kHzPerVolt : 0.0f; }

// Nearest MIDI note for a Hz/V voltage, or -1 when the voltage is at or below 0 V (a Hz/V VCO is silent there).
// Notes below 0 clamp to 0 and above 127 clamp to 127.
inline int midiNote(float volts)
{
    if (! (volts > 0.0f)) return -1;
    const long n = std::lround(kRefNote + 12.0 * std::log2((double) volts));
    return (int) (n < 0 ? 0 : n > 127 ? 127 : n);
}

} // namespace rack::hzv
