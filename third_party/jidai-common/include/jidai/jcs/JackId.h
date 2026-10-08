// Copyright (c) 2026 Martial Systems LLC. All rights reserved.
#pragma once
// Jidai Cable Standard v1.1: canonical jack ids and alias tables (JCS R6). Header-only C++17, no deps.
//
//   Global form   PREFIX#N/SECTION:LABEL     e.g. "RONIN#1/VCO:HZ/V", "ORIGAMI#2/VC:VC 1"
//   File forms    PREFIX/SECTION:LABEL       first instance of that kind (kept rebinding)
//                 SECTION:LABEL              bare, binds to the device that saved it
//   parseJackId (text) -> std::optional<JackId>      JackId::global(), JackId::local()
//   formatJackId (prefix, n, section, label)
//   isKnownPrefix ("SHOGUN")    BUSHIDO, RONIN, SHOGUN, ORIGAMI, RACK
//   AliasTable                  per-device {old local id -> canonical local id}, applied on load before binding;
//                               refuses to reuse an id for a different jack.
//
// Character rules (as enforced here): PREFIX is A-Z, 0-9, '-' or '_', starting with a letter. SECTION and LABEL
// are non-empty, contain no lowercase ASCII letters, no '#' and no ':'. SECTION has no '/'. LABEL MAY contain
// '/' (canonical ids such as VCO:HZ/V and INPUTS:START/STOP already do) and may contain non-ASCII UTF-8 (RONIN's
// "EG 2:OUT \u2212"). The split is at the FIRST '/' before the first ':'. This relaxes the "no '/'" wording of R6 to
// match the ids that R6 itself lists as canonical.

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace jidai::jcs {

enum class JackForm { Global, FirstInstance, Bare };

struct JackId
{
    std::string prefix;   // empty for Bare
    int number = 0;       // N >= 1 for Global, 0 otherwise
    std::string section;
    std::string label;
    JackForm form = JackForm::Bare;

    std::string local() const { return section + ":" + label; }
    std::string global() const { return prefix + "#" + std::to_string (number) + "/" + local(); }   // Global only
    std::string text() const
    {
        if (form == JackForm::Global) return global();
        if (form == JackForm::FirstInstance) return prefix + "/" + local();
        return local();
    }
};

inline constexpr const char* kKnownPrefixes[] = { "BUSHIDO", "RONIN", "SHOGUN", "ORIGAMI", "RACK" };

inline bool isKnownPrefix (std::string_view p)
{
    for (auto* k : kKnownPrefixes)
        if (p == k)
            return true;
    return false;
}

namespace detail {
inline bool validPrefix (std::string_view p)
{
    if (p.empty() || p[0] < 'A' || p[0] > 'Z')
        return false;
    for (char c : p)
        if (! ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    return true;
}
inline bool validPart (std::string_view s, bool allowSlash)
{
    if (s.empty() || s.front() == ' ' || s.back() == ' ')
        return false;
    for (char c : s)
    {
        if (c >= 'a' && c <= 'z') return false;
        if (c == '#' || c == ':') return false;
        if (c == '/' && ! allowSlash) return false;
        if ((unsigned char) c < 0x20) return false;
    }
    return true;
}
}

inline bool isValidSection (std::string_view s) { return detail::validPart (s, false); }
inline bool isValidLabel (std::string_view s) { return detail::validPart (s, true); }
inline bool isValidLocalId (std::string_view s)
{
    const auto colon = s.find (':');
    return colon != std::string_view::npos && isValidSection (s.substr (0, colon)) && isValidLabel (s.substr (colon + 1));
}

inline std::optional<JackId> parseJackId (std::string_view s)
{
    JackId id;
    const auto colon = s.find (':');
    if (colon == std::string_view::npos)
        return std::nullopt;
    const auto slash = s.find ('/');
    std::string_view rest = s;
    if (slash != std::string_view::npos && slash < colon)
    {
        std::string_view head = s.substr (0, slash);
        rest = s.substr (slash + 1);
        const auto hash = head.find ('#');
        if (hash == std::string_view::npos)
        {
            id.form = JackForm::FirstInstance;
            id.prefix = std::string (head);
        }
        else
        {
            id.form = JackForm::Global;
            id.prefix = std::string (head.substr (0, hash));
            const auto num = head.substr (hash + 1);
            if (num.empty() || num.size() > 6 || num[0] == '0')
                return std::nullopt;
            int n = 0;
            for (char c : num)
            {
                if (c < '0' || c > '9')
                    return std::nullopt;
                n = n * 10 + (c - '0');
            }
            id.number = n;
        }
        if (! detail::validPrefix (id.prefix))
            return std::nullopt;
    }
    const auto c2 = rest.find (':');
    id.section = std::string (rest.substr (0, c2));
    id.label = std::string (rest.substr (c2 + 1));
    if (! isValidSection (id.section) || ! isValidLabel (id.label))
        return std::nullopt;
    return id;
}

inline std::string formatJackId (std::string_view prefix, int number, std::string_view section, std::string_view label)
{
    return std::string (prefix) + "#" + std::to_string (number) + "/" + std::string (section) + ":" + std::string (label);
}

// Per-device alias table (R6): old local id -> canonical local id. Ids are never reused for a different jack.
class AliasTable
{
public:
    // False when `oldId` already maps elsewhere, when either id is malformed, or when `oldId` is itself a target.
    bool add (const std::string& oldId, const std::string& canonical)
    {
        if (! isValidLocalId (oldId) || ! isValidLocalId (canonical) || oldId == canonical)
            return false;
        for (auto& [o, c] : map_)
        {
            if (o == oldId) return c == canonical;
            if (c == oldId) return false;
            if (o == canonical) return false;
        }
        map_.push_back ({ oldId, canonical });
        return true;
    }
    // Applies chains (A -> B, B -> C) as well; unknown ids pass through unchanged.
    std::string apply (std::string id) const
    {
        for (size_t guard = 0; guard <= map_.size(); ++guard)
        {
            bool changed = false;
            for (auto& [o, c] : map_)
                if (o == id) { id = c; changed = true; break; }
            if (! changed) break;
        }
        return id;
    }
    size_t size() const { return map_.size(); }

private:
    std::vector<std::pair<std::string, std::string>> map_;
};

} // namespace jidai::jcs
