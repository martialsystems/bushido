// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
// Unit tests for jidai-common (Jidai Cable Standard v1.1 helpers). No framework. Built as C++17.

#include "jidai/CableStandard.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace jidai::jcs;

namespace {

int checks = 0, failures = 0;
std::string group;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL [%s] %s\n", group.c_str(), what.c_str());
    }
}

bool near (double a, double b, double tol) { return std::fabs (a - b) <= tol; }

// xorshift32 + Box-Muller: a deterministic Gaussian for the noisy-ramp test (the audit used numpy, sigma 50 mV).
struct Noise
{
    std::uint32_t s = 0x9E3779B9u;
    double uniform()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return ((double) s + 1.0) / 4294967297.0;
    }
    double gauss() { return std::sqrt (-2.0 * std::log (uniform())) * std::cos (6.283185307179586 * uniform()); }
};

// The audit's ramp (verify_crossunit.py section 5): 2 Hz sine * 2.5 + 0.8 V, clipped to +-5 V, plus 50 mV noise.
std::vector<float> noisyRamp()
{
    Noise n;
    std::vector<float> v (48000);
    for (size_t i = 0; i < v.size(); ++i)
    {
        double x = std::sin (6.283185307179586 * 2.0 * (double) i / 48000.0) * 2.5 + 0.8;
        x = x > 5.0 ? 5.0 : (x < -5.0 ? -5.0 : x);
        v[i] = (float) (x + 0.05 * n.gauss());
    }
    return v;
}

// ---------------------------------------------------------------- Detect.h
void testSchmitt()
{
    group = "Schmitt R3";
    Schmitt s;
    check (s.process (0.999f) == Edge::None && ! s.high, "0.999 V stays low");
    check (s.process (1.0f) == Edge::None && ! s.high, "exactly 1.0 V stays low (high is strictly above 1.0)");
    check (s.process (1.0001f) == Edge::Rising && s.high, "1.0001 V rises on that sample");
    check (s.process (3.0f) == Edge::None, "a rising edge counts once");
    check (s.process (0.6f) == Edge::None && s.high, "0.6 V stays high (hysteresis)");
    check (s.process (0.5f) == Edge::None && s.high, "exactly 0.5 V stays high (low is strictly below 0.5)");
    check (s.process (0.4999f) == Edge::Falling && ! s.high, "0.4999 V falls");
    check (s.process (0.9f) == Edge::None, "0.9 V after falling: still low");
    check (s.rising (5.0f), "rising() helper");
    s.reset();
    check (! s.high, "reset low");
    check (kTrigHigh == 1.0f && kTrigLow == 0.5f, "thresholds are 1.0 / 0.5 V");

    // Hysteresis rejects chatter: the audit's noisy ramp has exactly 2 real crossings in 1 s.
    const auto ramp = noisyRamp();
    Schmitt d;
    d.reset (ramp[0] > kTrigHigh);
    int edges = 0;
    for (float v : ramp) edges += d.rising (v) ? 1 : 0;
    check (edges == 2, "noisy 2 Hz ramp: exactly 2 rising edges, got " + std::to_string (edges));
    // The old no-hysteresis S&H rule (>= 1.0 V) on the same ramp chatters (JCS X6).
    int plain = 0;
    bool prev = ramp[0] >= 1.0f;
    for (float v : ramp) { const bool now = v >= 1.0f; plain += (now && ! prev) ? 1 : 0; prev = now; }
    check (plain > 20, "the old >= 1.0 V rule chatters on the same ramp: " + std::to_string (plain) + " edges");
}

void testStrig()
{
    group = "S-trig R3s";
    StrigDetector s;
    check (s.process (5.0f) == Edge::None && ! s.held, "+5 V is released");
    check (s.process (1.0f) == Edge::None && ! s.held, "exactly 1.0 V is not held yet");
    check (s.process (0.99f) == Edge::Rising && s.held, "0.99 V becomes held");
    check (s.process (1.5f) == Edge::None && s.held, "exactly 1.5 V still held");
    check (s.process (1.51f) == Edge::Falling && ! s.held, "1.51 V releases");
    check (strigVoltsFor (true) == 0.0f && strigVoltsFor (false) == 5.0f, "gate high -> 0 V held, low -> +5 V released");
    check (kStrigRest == 5.0f, "unpatched S-trig rests at +5 V");
    const auto ramp = noisyRamp();
    StrigDetector d;
    d.reset (ramp[0] < kStrigHeld);
    int held = 0;
    for (float v : ramp) held += d.process (-v + 1.3f) == Edge::Rising ? 1 : 0;   // mirrored ramp around the window
    check (held == 2, "S-trig on a noisy mirrored ramp: 2 held edges, got " + std::to_string (held));
}

void testGates()
{
    group = "gates R2";
    check (gateVolts (true) == 5.0f && gateVolts (false) == 0.0f, "gates are 0 / 5 V");
    check (triggerPulseSamples (48000.0) == 48, "1 ms at 48 kHz = 48 samples");
    check (triggerPulseSamples (44100.0) == 44, "1 ms at 44.1 kHz = 44 samples");
    check (triggerPulseSamples (96000.0) == 96, "1 ms at 96 kHz = 96 samples");
    check (triggerPulseSamples (100.0) == 1, "never below 1 sample");
}

// ---------------------------------------------------------------- Volts.h
void testVolts()
{
    group = "volts R1/R4.4/R15/R16";
    check (hostToVolts (1.0f) == 5.0f && hostToVolts (-0.2f) == -1.0f, "host 1.0 = 5 V");
    check (near (voltsToHost (5.0f), 1.0, 1e-7) && near (voltsToHost (2.5f), 0.5, 1e-7), "5 V = host 1.0");
    bool over = false;
    check (clampRail (4.999f, over) == 4.999f && ! over, "inside the rail: unchanged, no flag");
    check (clampRail (5.0f, over) == 5.0f && ! over, "exactly 5 V: no flag");
    check (clampRail (5.0001f, over) == 5.0f && over, "5.0001 V stops at exactly 5 V and flags");
    over = false;
    check (clampRail (-7.0f, over) == -5.0f && over, "-7 V stops at exactly -5 V and flags");
    over = false;
    check (clampRail (std::nanf (""), over) == 0.0f && over, "NaN never reaches a cable");

    OverRangeLed led;
    led.prepare (48000.0);
    bool lit = false;
    for (int i = 0; i < 480; ++i) lit = led.process (6.0f);
    check (! lit, "6 V for exactly 10 ms (480 samples): not lit yet");
    check (led.process (6.0f), "6 V for more than 10 ms: lit");
    check (! led.process (5.0f), "back inside: dark");
    for (int i = 0; i < 2000; ++i) lit = led.process (5.5f);
    check (! lit, "exactly 5.5 V is not over range");
    for (int i = 0; i < 2000; ++i) lit = led.process (i % 400 == 0 ? 0.0f : -6.0f);
    check (! lit, "a dip back inside every 400 samples keeps it dark");

    PitchRailFlag p;
    check (p.process (6.0f) == 5.0f && p.latched, "pitch rail clamps and latches");
    check (p.process (1.0f) == 1.0f && p.latched, "the flag stays latched until the next note");
    p.noteOn();
    check (! p.latched, "noteOn clears the latch");

    check (! kR16Enabled, "R16 is not enabled (pending user decision)");
    check (r16BoundaryGain (AudioLevel::ModularHalfLevel, true, true) == 1.0f && r16BoundaryGain (AudioLevel::ModularHalfLevel, true, false) == 1.0f
               && r16BoundaryGain (AudioLevel::Jidai5V, true, true) == 1.0f, "R16 hook is identity for every level");
}

// ---------------------------------------------------------------- Pitch.h
void testPitch()
{
    group = "pitch R4";
    using namespace pitch;
    check (near (kC3Hz, 440.0 * std::exp2 (-21.0 / 12.0), 1e-12) && near (kC3Hz, 55.0 * std::exp2 (15.0 / 12.0), 1e-12), "C3 is exactly A440 minus 21 semitones (130.8127826502993 Hz)");
    check (near (kC3Hz, 130.8127826502993, 0.0), "C3 constant is the exact double, not the rounded 130.8128");
    check (near (hz (Law::VOct, 0.0), kC3Hz, 0.0), "0 V = C3");
    check (near (hz (Law::VOct, 1.0), 2.0 * kC3Hz, 1e-12), "+1 V = one octave up");
    check (near (hz (Law::VOct, -1.0), 0.5 * kC3Hz, 1e-12), "-1 V = one octave down");
    check (near (hz (Law::VOct, 1.0 / 12.0) / hz (Law::VOct, 0.0), std::exp2 (1.0 / 12.0), 1e-12), "1/12 V = one semitone");
    const int notes[] = { 24, 36, 48, 60, 72, 84, 108 };
    const double volts[] = { -2, -1, 0, 1, 2, 3, 5 };
    for (int i = 0; i < 7; ++i)
    {
        check (near (voltsForNote (Law::VOct, notes[i]), volts[i], 1e-12), "note " + std::to_string (notes[i]) + " -> " + std::to_string (volts[i]) + " V");
        check (midiNote (Law::VOct, volts[i]) == notes[i], "V/OCT volts -> note " + std::to_string (notes[i]));
    }
    check (near (note (Law::VOct, 0.25), 51.0, 1e-12), "fractional note");
    check (near (hzToVolts (Law::VOct, 4.0 * kC3Hz), 2.0, 1e-12), "C5 = +2 V");
    // HZ/V LIN: a different role, 1 V = C3.
    check (near (hz (Law::HzvLin, 1.0), kC3Hz, 0.0) && near (hz (Law::HzvLin, 2.0), 2.0 * kC3Hz, 1e-12), "HZ/V LIN: 1 V = C3, 2 V = C4");
    check (hz (Law::HzvLin, 0.0) == 0.0 && midiNote (Law::HzvLin, 0.0) == -1 && std::isnan (note (Law::HzvLin, -1.0)), "HZ/V LIN has no note at or below 0 V");
    check (midiNote (Law::HzvLin, 1.0) == 48 && midiNote (Law::HzvLin, 4.0) == 72, "HZ/V LIN MIDI: 1 V -> 48, 4 V -> 72");
    check (near (voltsForNote (Law::HzvLin, 60), 2.0, 1e-12), "HZ/V LIN note 60 = 2 V");
    check (near (hz (Law::HzvLin, 5.0), 5.0 * kC3Hz, 1e-12), "HZ/V LIN +5 V = 654.06 Hz");
    check (near (roninHzvLinHz (0.01), kC3Hz * 0.05, 1e-12), "RONIN's linear input floors at 0.05 V");
    check (midiNote (Law::VOct, 9.0) == 127 && midiNote (Law::VOct, -9.0) == 0, "MIDI clamps to 0..127");
    // quantize, the BUSHIDO QUANT SEMI rule
    check (near (quantize (Law::VOct, 0.04), 0.0, 1e-12) && near (quantize (Law::VOct, 0.05), 1.0 / 12.0, 1e-12), "V/OCT quantize to semitones");
    check (quantize (Law::VOct, 5.04) <= 5.0, "V/OCT quantize never passes +5 V");
    check (near (quantize (Law::HzvLin, 5.0), std::exp2 (27.0 / 12.0), 1e-12), "HZ/V LIN 5 V quantizes to note 75 (4.757 V)");
    check (quantize (Law::HzvLin, 0.02) == 0.0, "HZ/V LIN below 2^-5 V is silent");
    // Tuning is not the volt law (R4.6).
    check (near (voiceHz (0.0), kC3Hz, 1e-12), "voice at home tuning plays the law");
    check (near (voiceHz (0.0, 0.0, 442.0), kC3Hz * 442.0 / 440.0, 1e-12), "A4 442 moves the voice");
    check (near (voiceHz (1.0, 100.0), 2.0 * kC3Hz * std::exp2 (1.0 / 12.0), 1e-12), "+100 cents fine moves the voice a semitone");
    check (near (voltsForNote (Law::VOct, 60), 1.0, 0.0), "...while the volts for note 60 stay 1 V");
    check (near (voiceHz (0.0, 0.0, 440.0, -1), 0.5 * kC3Hz, 1e-12), "an OCT switch moves the voice, not the law");
    // lin55 migration converter (SHOGUN v2 HZ/V, old BUSHIDO MIDI): exact.
    double worst = 0.0;
    for (int n = 24; n <= 96; ++n)
    {
        const double f = 440.0 * std::exp2 ((n - 69) / 12.0);
        const double oldV = f / 55.0;
        worst = std::fmax (worst, std::fabs (lin55ToVoct (oldV) - (n - 48) / 12.0));
    }
    check (worst < 1e-12, "lin55 -> V/OCT matches (note-48)/12 for notes 24..96 exactly, worst " + std::to_string (worst));
    check (near (lin55ToVoct (1.0), -1.25, 0.0) && near (lin55ToVoct (4.0), 0.75, 0.0), "lin55: V' = log2 V - 1.25 exactly (1 V -> -1.25 V)");
    check (near (std::log2 (kC3Hz / 55.0), kLin55Octaves, 1e-14), "log2(C3/55) = 1.25 with the exact C3");
    bool over = false;
    check (clampPitch (5.2, over) == 5.0 && over, "pitch rail +5 V");
    char buf[8];
    check (std::string (noteName (48, buf, 8)) == "C3" && std::string (noteName (69, buf, 8)) == "A4" && std::string (noteName (-1, buf, 8)) == "--", "note names: 48 = C3, 69 = A4");
}

// ---------------------------------------------------------------- Roles.h
double srgbLuminance (std::uint32_t rgb)
{
    auto lin = [] (double c) { c /= 255.0; return c <= 0.04045 ? c / 12.92 : std::pow ((c + 0.055) / 1.055, 2.4); };
    return 0.2126 * lin ((rgb >> 16) & 0xff) + 0.7152 * lin ((rgb >> 8) & 0xff) + 0.0722 * lin (rgb & 0xff);
}

void testRoles()
{
    group = "roles R14";
    const std::uint32_t rgb[] = { 0xb129b1, 0xe53b2f, 0x6590f3, 0x2ec554, 0x5cd5ed, 0xf7e77d };
    const char* names[] = { "S-TRIG", "AUDIO", "V/OCT", "GATE/CLK", "HZ/V LIN", "CV" };
    double prevL = -1.0;
    for (int i = 0; i < kRoleCount; ++i)
    {
        const auto& r = roleInfo ((Role) i);
        check (r.rgb == rgb[i] && std::string (r.name) == names[i], std::string ("role table entry ") + names[i]);
        check (roleArgb ((Role) i) == (0xff000000u | rgb[i]), std::string ("ARGB for ") + names[i]);
        const double L = srgbLuminance (r.rgb);
        check (near (L, r.luminance, 0.0015), std::string ("tabled luminance matches sRGB for ") + names[i] + ": " + std::to_string (L));
        if (prevL >= 0.0)
            check ((L + 0.05) / (prevL + 0.05) >= 1.30, std::string ("luminance step >= 1.31 into ") + names[i]);
        prevL = L;
        Role back = Role::CV;
        check (roleFromName (names[i], back) && back == (Role) i, std::string ("roleFromName ") + names[i]);
        check (std::string (r.glyph).size() >= 2, std::string ("glyph present for ") + names[i]);
    }
    check (std::string (roleInfo (Role::VOct).glyph) == "\xe2\x99\xaa" && std::string (roleInfo (Role::HzvLin).glyph) == "\xc6\x92", "V/OCT and HZ/V LIN glyphs differ");
    Role dummy;
    check (! roleFromName ("PINK", dummy), "unknown role name");
    check (cableBadge (Role::VOct, Role::HzvLin) == Badge::PitchLaw && cableBadge (Role::HzvLin, Role::VOct) == Badge::PitchLaw, "V/OCT <-> HZ/V LIN gets the not-equal badge");
    check (cableBadge (Role::Audio, Role::GateClk) == Badge::AudioIntoClock, "audio into a clock gets a badge");
    check (cableBadge (Role::GateClk, Role::STrig) == Badge::GateToStrig, "gate into S-trig is marked as converted");
    check (cableBadge (Role::VOct, Role::VOct) == Badge::None && cableBadge (Role::CV, Role::Audio) == Badge::None, "matching roles: no badge");
    check (std::string (badgeText (Badge::PitchLaw)).find ("not octaves") != std::string::npos, "badge text");
}

// ---------------------------------------------------------------- JackId.h
void testJackIds()
{
    group = "jack ids R6";
    auto g = parseJackId ("RONIN#1/VCO:HZ/V");
    check (g && g->form == JackForm::Global && g->prefix == "RONIN" && g->number == 1 && g->section == "VCO" && g->label == "HZ/V",
           "global id with a slash in the label splits at the first '/'");
    check (g && g->global() == "RONIN#1/VCO:HZ/V" && g->local() == "VCO:HZ/V" && g->text() == "RONIN#1/VCO:HZ/V", "round trip");
    auto f = parseJackId ("BUSHIDO/INPUTS:START/STOP");
    check (f && f->form == JackForm::FirstInstance && f->prefix == "BUSHIDO" && f->label == "START/STOP", "first-instance form");
    auto b = parseJackId ("VCO:HZ/V");
    check (b && b->form == JackForm::Bare && b->section == "VCO" && b->label == "HZ/V" && b->prefix.empty(), "bare id keeps HZ/V as its label");
    auto o = parseJackId ("ORIGAMI#12/VC:VC 1");
    check (o && o->number == 12 && o->label == "VC 1", "multi-digit instance and spaces");
    auto u = parseJackId ("RONIN#1/EG 2:OUT \xe2\x88\x92");
    check (u && u->label == "OUT \xe2\x88\x92", "non-ASCII label (RONIN EG 2:OUT minus) is accepted");
    check (parseJackId ("SHOGUN#1/BD1:FOLD VC").has_value() && parseJackId ("RACK#1/MAIN:OUT L").has_value(), "SHOGUN and RACK ids");
    check (! parseJackId ("RONIN#0/VCO:SAW"), "instance 0 rejected");
    check (! parseJackId ("RONIN#01/VCO:SAW"), "leading zero rejected");
    check (! parseJackId ("RONIN#/VCO:SAW"), "empty instance rejected");
    check (! parseJackId ("ronin#1/VCO:SAW"), "lowercase prefix rejected");
    check (! parseJackId ("RONIN#1/vco:SAW"), "lowercase section rejected");
    check (! parseJackId ("RONIN#1/VCO:saw"), "lowercase label rejected");
    check (! parseJackId ("RONIN#1/VCO"), "missing colon rejected");
    check (! parseJackId ("RONIN#1/:SAW") && ! parseJackId ("RONIN#1/VCO:"), "empty section or label rejected");
    check (! parseJackId ("RONIN#1/VCO:SAW#2"), "'#' in a label rejected");
    check (! parseJackId ("RONIN#1/VCO:A:B"), "second ':' rejected");
    check (! parseJackId ("RONIN#1/ VCO:SAW"), "leading space rejected");
    check (isKnownPrefix ("ORIGAMI") && isKnownPrefix ("RACK") && ! isKnownPrefix ("ACME"), "known neutral prefixes");
    check (formatJackId ("ORIGAMI", 1, "HOST", "IN L") == "ORIGAMI#1/HOST:IN L", "formatJackId");
    check (isValidLocalId ("INPUTS:START/STOP") && ! isValidLocalId ("IN/PUTS:START"), "slash allowed in label, not in section");

    AliasTable t;
    check (t.add ("VCO:HZ/V OLD", "VCO:HZ/V"), "alias added");
    check (t.add ("VCO:HZ/V OLD", "VCO:HZ/V"), "same alias again is fine");
    check (! t.add ("VCO:HZ/V OLD", "VCO:SAW"), "an old id is never reused for a different jack");
    check (! t.add ("VCO:HZ/V", "VCO:X"), "a canonical target cannot become an old id");
    check (! t.add ("bad", "VCO:SAW"), "malformed id refused");
    check (t.apply ("VCO:HZ/V OLD") == "VCO:HZ/V" && t.apply ("VCO:SAW") == "VCO:SAW", "apply maps old ids, passes others");
    AliasTable chain;
    chain.add ("A:ONE", "A:TWO");
    chain.add ("A:TWO X", "A:THREE");
    check (chain.apply ("A:ONE") == "A:TWO", "apply does not invent chains");
    check (chain.size() == 2, "alias count");
    check (! chain.add ("A:TWO", "A:FOUR") && ! chain.add ("A:ZERO", "A:ONE"), "aliases never chain (one hop)");

    // Input laws: a unit declares a rename and its conversion together (SHOGUN LEAD:HZ/V -> LEAD:NOTE, lin55).
    AliasTable laws;
    check (laws.add ("LEAD:HZ/V", "LEAD:NOTE", AliasLaw::Lin55ToVoct) && laws.add ("BASS:HZ/V", "BASS:NOTE", AliasLaw::Lin55ToVoct), "aliases with an input law");
    check (! laws.add ("LEAD:HZ/V", "LEAD:NOTE"), "the same alias with a different law is refused");
    check (laws.add ("LEAD:HZ/V", "LEAD:NOTE", AliasLaw::Lin55ToVoct), "the same alias with the same law is fine");
    const auto r = laws.resolve ("LEAD:HZ/V");
    check (r.aliased && r.id == "LEAD:NOTE" && r.conversion.law == AliasLaw::Lin55ToVoct, "resolve gives the canonical id and its law");
    check (near (r.conversion.convert (2.0), std::log2 (2.0) - 1.25, 0.0) && near (r.conversion.convert (2.0), pitch::lin55ToVoct (2.0), 0.0),
           "Lin55ToVoct: V' = log2 V - 1.25");
    const double floorV = std::log2 (1.0e-3) - 1.25;
    check (near (r.conversion.convert (0.0), floorV, 0.0) && near (pitch::lin55ToVoct (-3.0), floorV, 0.0)
               && near (pitch::lin55ToVoct (std::nan ("")), floorV, 0.0) && near (pitch::lin55ToVoct (5.0e-4), floorV, 0.0),
           "Lin55ToVoct (1.1.2): V floored at 1 mV: 0 V, negative, NaN and 0.5 mV give log2(1e-3) - 1.25 = -11.216 V");
    check (near (pitch::lin55ToVoct (2.0e-3), std::log2 (2.0e-3) - 1.25, 0.0) && near (pitch::lin55ToVoct (1.0e-3), floorV, 0.0),
           "Lin55ToVoct: above the floor unchanged (2 mV), at it exact (1 mV)");
    const auto pass = laws.resolve ("LEAD:GATE");
    check (! pass.aliased && pass.id == "LEAD:GATE" && pass.conversion.identity() && near (pass.conversion.convert (3.3), 3.3, 0.0), "unknown ids pass through with the identity law");
    AliasTable more;
    more.add ("VCO:LIN", "VCO:VOCT", AliasLaw::HzvLinToVoct);
    more.add ("VCO:OLD VOCT", "VCO:HZV", AliasLaw::VoctToHzvLin);
    more.add ("MIX:IN OLD", "MIX:IN", AliasConversion::linear (2.0, -1.0));
    check (near (more.resolve ("VCO:LIN").conversion.convert (4.0), 2.0, 0.0), "HzvLinToVoct: 4 V (C5) -> +2 V");
    check (near (more.resolve ("VCO:OLD VOCT").conversion.convert (2.0), 4.0, 0.0), "VoctToHzvLin: +2 V -> 4 V");
    check (near (more.resolve ("MIX:IN OLD").conversion.convert (1.5), 2.0, 0.0), "Scale: 2 V - 1");
}

// ---------------------------------------------------------------- State.h
struct Doc { std::vector<std::string> log; int value = 0; };

void testState()
{
    group = "state R7";
    MigrationChain<Doc> chain (3);
    chain.add (0, [] (Doc& d, std::vector<std::string>& n) { d.log.push_back ("0>1"); d.value += 1; n.push_back ("m1"); return true; })
         .add (1, [] (Doc& d, std::vector<std::string>&) { d.log.push_back ("1>2"); d.value *= 10; return true; })
         .add (2, [] (Doc& d, std::vector<std::string>&) { d.log.push_back ("2>3"); d.value += 5; return true; });
    Doc d;
    int fmt = 0;
    auto r = chain.run (d, fmt);
    check (r.status == LoadStatus::Migrated && fmt == 3 && r.fromFormat == 0 && r.toFormat == 3, "v0 migrates to v3");
    check (d.log.size() == 3 && d.log[0] == "0>1" && d.log[2] == "2>3" && d.value == 15, "steps run in order, once each");
    check (r.notes.size() == 1 && r.notes[0] == "m1", "notes collected");
    Doc cur;
    fmt = 3;
    r = chain.run (cur, fmt);
    check (r.status == LoadStatus::Current && cur.log.empty(), "current format: no steps");
    fmt = 7;
    r = chain.run (cur, fmt);
    check (r.status == LoadStatus::FutureReadOnly && r.readOnly() && r.ok() && fmt == 7, "future format loads read-only, not rejected");
    MigrationChain<Doc> gap (3);
    gap.add (0, [] (Doc&, std::vector<std::string>&) { return true; });
    Doc g;
    fmt = 0;
    r = gap.run (g, fmt);
    check (r.status == LoadStatus::Failed && ! r.ok() && fmt == 1, "a gap in the chain fails at the last good format");
    StateHeader h { 1, "ORIGAMI" };
    check (h.format == 1 && h.unit == "ORIGAMI", "state header");
}

// ---------------------------------------------------------------- Graph.h
void testGraph()
{
    group = "graph R9/R11";
    // JCS X2 patch: VCO(0) SAW -> RING(1) A, RING OUT -> VCO FM (loop 1, older), VCO TRI -> INV(2), INV OUT -> VCO PWM (loop 2).
    const std::vector<GraphEdge> cables { { 0, 1 }, { 1, 0 }, { 0, 2 }, { 2, 0 } };
    const auto fb = classifyFeedback (3, {}, cables);
    check (fb == std::vector<char> ({ 0, 1, 0, 1 }), "two loops: both closing cables are feedback (one delayed cable per loop)");
    const auto order = runOrder (3, {}, cables, fb);
    std::vector<int> count (3, 0);
    for (int u : order) ++count[(size_t) u];
    check (order.size() == 3 && count == std::vector<int> ({ 1, 1, 1 }), "each node runs exactly once per sample");
    check (order[0] == 0, "VCO first, its loops come back delayed");
    // Patch order matters: the newest cable of a loop is the delayed one.
    const auto fb2 = classifyFeedback (2, {}, { { 1, 0 }, { 0, 1 } });
    check (fb2 == std::vector<char> ({ 0, 1 }), "the newer cable of a loop is the delayed one");
    check (classifyFeedback (1, {}, { { 0, 0 } })[0] == 1, "a self-patch is feedback");
    // Fixed (in-device) edges count as kept from the start and are never delayed.
    const auto fb3 = classifyFeedback (3, { { 0, 1 } }, { { 1, 2 }, { 2, 0 } });
    check (fb3 == std::vector<char> ({ 0, 1 }), "a cable closing a loop through a fixed edge is feedback");
    const auto ord3 = runOrder (3, { { 0, 1 } }, { { 1, 2 }, { 2, 0 } }, fb3);
    check (ord3 == std::vector<int> ({ 0, 1, 2 }), "fixed edges order the run");
    check (classifyFeedback (2, {}, { { 0, 5 } })[0] == 0, "out-of-range edges are ignored");

    // JCS X5: SHOGUN(0, 23) -> RONIN(1, 0); both feed the host. Path latency: 23 and 23, not 46.
    const auto P = pathLatency ({ 23, 0 }, { { 0, 1 } });
    check (P == std::vector<int> ({ 23, 23 }), "SHOGUN -> RONIN: path latency 23 / 23 (per-device rule gave 46)");
    const int maxP = std::max (P[0], P[1]);
    check (maxP - P[0] == 0 && maxP - P[1] == 0, "no compensation needed on either host contribution");
    const int perDevice = 23 + (23 - 0);
    check (perDevice == 46 && maxP == 23, "the old per-device rule double-counts (46), the path rule does not (23)");
    // Chain and fan-in.
    const auto P2 = pathLatency ({ 23, 23, 0, 0 }, { { 0, 1 }, { 1, 2 }, { 3, 2 } });
    check (P2 == std::vector<int> ({ 23, 46, 46, 0 }), "23 -> 23 -> 0 is 46; a device with no audio inputs keeps its own latency");
    const auto skew = arrivalSkew (P2, { { 0, 1 }, { 1, 2 }, { 3, 2 } });
    check (skew == std::vector<int> ({ 0, 0, 46 }), "the early input of a device gets a delta-46 badge");
    const auto P3 = pathLatency ({ 23, 0 }, { { 0, 1 }, { 1, 0 } });
    check (P3 == std::vector<int> ({ 23, 23 }), "a feedback audio cable is ignored by the path latency");
}

}

int main()
{
    testSchmitt();
    testStrig();
    testGates();
    testVolts();
    testPitch();
    testRoles();
    testJackIds();
    testState();
    testGraph();
    std::printf ("%d checks, %d failed\n", checks, failures);
    std::printf (failures == 0 ? "JIDAI COMMON TESTS PASS\n" : "JIDAI COMMON TESTS FAIL\n");
    return failures == 0 ? 0 : 1;
}
