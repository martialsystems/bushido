#pragma once
// Pitch laws (Jidai Cable Standard v1.1, JCS R4): a shim over the shared jidai-common header (third_party/jidai-common).
//   V/OCT     the rack standard: 0 V = C3 = 130.8127826502993 Hz = MIDI 48, one volt per octave.     note = 48 + 12 V
//   HZ/V LIN  linear: 1 V = C3 = 130.8127826502993 Hz at RONIN 8', double the volts = one octave up. note = 48 + 12 log2 V
//   C3 is jidai-common's kC3Hz (1.1.1): exactly 440 x 2^(-21/12) as a double, no rounded constant.
// rack::pitch keeps the names BUSHIDO uses (Law, kC3Hz, kRefNote, kRail, kLinFloor, note, hz, midiNote, quantize,
// noteName); the math is jidai::jcs::pitch, identical to the former local copy (same expressions, same rounding:
// MIDI = lround, clamped 0..127, -1 for no note; QUANT steps down one semitone past the +5 V rail; "--" for no note).
#include <jidai/jcs/Pitch.h>

namespace rack::pitch {
using namespace jidai::jcs::pitch;
} // namespace rack::pitch
