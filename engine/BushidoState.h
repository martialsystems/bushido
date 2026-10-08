#pragma once
// BUSHIDO state format and migration (JCS R6, R7). Framework-free: the plugin and the rack both call it.
//   format 0  unversioned v1 states (no `format` attribute)
//   format 1  this redesign: tab parameters (EXT SOURCE, SETTLE, TRIG MODE, LAW, QUANT, MIDI CH, VEL)
// Loading runs migrate() before the parameters and cables are bound. Unknown future formats load read-only.
#include "BushidoModule.h"
#include <jidai/jcs/JackId.h>
#include <map>
#include <string>
#include <vector>
#include <utility>

namespace bushido {

constexpr int kFormat = 1;

// Jack ids are read exactly as stored (JCS R6): there are no prefix aliases, so an id with an unknown prefix simply binds to nothing.

// "BUSHIDO#1/OUTPUTS:CV A" -> { "BUSHIDO", "OUTPUTS:CV A" }; a bare "OUTPUTS:CV A" or "INPUTS:START/STOP" has an empty device.
// The prefix ends at the first '/' before the first ':' (jidai::jcs::parseJackId); labels may contain '/'. An id the shared
// parser rejects (lowercase, a bad instance number) is split by the same rule, so a hand-edited file still loads.
inline std::pair<std::string, std::string> splitJack(const std::string& gid)
{
    if (const auto j = jidai::jcs::parseJackId(gid)) return { j->prefix, j->local() };
    const auto colon = gid.find(':'), slash = gid.find('/');
    if (slash == std::string::npos || slash > colon) return { "", gid };
    std::string dev = gid.substr(0, slash); const auto hash = dev.find('#'); if (hash != std::string::npos) dev = dev.substr(0, hash);
    return { dev, gid.substr(slash + 1) };
}

struct MigrationReport {
    int fromFormat = kFormat;
    bool readOnly = false;                    // a future format: loaded as-is, not saved over
    std::vector<std::string> lines;           // shown on the SETUP tab
    rack::pitch::Law law[2] = { rack::pitch::Law::VOct, rack::pitch::Law::VOct };
    bool lawMismatch[2] = { false, false };   // the row is cabled to both a LIN and a V/OCT input: that V/OCT cable shows the badge
};

// params: parameter id -> normalised value, as stored. cables: pairs of global jack ids (any order, any prefix form).
// `self` is this instance's rack prefix ("BUSHIDO#1"), or "" outside the rack: only cables from this instance count.
inline MigrationReport migrate(int fromFormat, std::map<std::string, float>& params,
                               const std::vector<std::pair<std::string, std::string>>& cables, const std::string& self = "")
{
    MigrationReport r; r.fromFormat = fromFormat;
    if (fromFormat > kFormat) { r.readOnly = true; r.lines.push_back("Saved by a newer BUSHIDO (format " + std::to_string(fromFormat) + "): loaded read-only"); return r; }
    if (fromFormat >= kFormat) return r;

    // 0 -> 1. Patterns, knob volts, PORTA and SOURCE are untouched, so every cable carries exactly the same volts.
    params["CLOCK:SETTLE"] = 1.0f;            r.lines.push_back("SETTLE = VINTAGE (0.6 ms, as before)");
    params["CLOCK:TRIG MODE"] = 0.0f;         r.lines.push_back("TRIG MODE = STEP");
    params["CLOCK:EXT SOURCE"] = 0.0f;        // SOURCE stays as stored; never flipped to HOST
    const char* cvJack[2] = { "OUTPUTS:CV A", "OUTPUTS:CV B" };
    auto mine = [&](const std::pair<std::string, std::string>& end, const std::string& raw) {
        if (end.first.empty()) return true;                                     // bare id: this unit's own file
        if (end.first != "BUSHIDO") return false;
        if (self.empty()) return true;                                          // outside the rack: every BUSHIDO end is this one
        return raw.rfind(self + "/", 0) == 0 || (self == "BUSHIDO#1" && raw.rfind("BUSHIDO/", 0) == 0);   // "BUSHIDO/..." binds to the first instance
    };
    for (int row = 0; row < 2; ++row) {
        bool toLin = false, toVoct = false;
        for (auto& [a, b] : cables) {
            const auto ea = splitJack(a), eb = splitJack(b);
            for (int side = 0; side < 2; ++side) {
                const auto& src = side == 0 ? ea : eb; const auto& dst = side == 0 ? eb : ea; const auto& srcRaw = side == 0 ? a : b;
                if (src.second != cvJack[row] || ! mine(src, srcRaw)) continue;
                if (dst.first == "RONIN" && dst.second == "VCO:HZ/V") toLin = true;
                else if ((dst.first == "RONIN" && dst.second == "VCO:V/OCT") || (dst.first == "SHOGUN" && dst.second.find(":NOTE") != std::string::npos)) toVoct = true;
            }
        }
        r.law[row] = toLin ? rack::pitch::Law::HzvLin : rack::pitch::Law::VOct;
        r.lawMismatch[row] = toLin && toVoct;
        params[row == 0 ? "STEPS:LAW A" : "STEPS:LAW B"] = toLin ? 1.0f : 0.0f;
        r.lines.push_back(std::string("Row ") + (row == 0 ? "A" : "B") + " PITCH LAW = " + (toLin ? "HZ/V LIN (cabled to RONIN VCO:HZ/V)" : "V/OCT")
                          + (r.lawMismatch[row] ? "; its V/OCT cable shows the mismatch badge" : ""));
    }
    return r;
}

} // namespace bushido
