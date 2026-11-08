// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Names older SculptForge versions wrote into projects, brush files, configs, packs and scripts. They are read
// once, on load, and mapped to today's names; never shown or written. The old words are not spelled here: each is
// a hash of its lower-case letters, and docs/legal/trademarks.md lists them (tests/test_legacy_names.cpp checks the
// table against that list).
#pragma once

#include "core/Json.h"

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace sf::legacy {

// FNV-1a 64 of the ASCII-lower-cased bytes.
constexpr uint64_t hash(std::string_view s) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (char c : s) {
        h ^= uint8_t(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        h *= 0x100000001B3ull;
    }
    return h;
}

struct Word {
    uint64_t hash;          // of the old word, lower case
    uint8_t length;         // its length
    std::string_view lower; // today's word, written lower case ...
    std::string_view title; // ... and capitalised (UPPER is made from `lower`)
};

// Longest first where one could contain another. W1..W13 in docs/legal/trademarks.md.
inline constexpr Word kWords[] = {
    {0xBCFC81F111856264ull, 11, "sharpcrease", "SharpCrease"}, // W8
    {0xC1D54DDDFE279385ull, 11, "clayblock", "ClayBlock"},     // W9
    {0xEFC9B9E5E56A64CEull, 9, "facegroup", "FaceGroup"},      // W4
    {0x06C84E19C9C3D005ull, 9, "vertexpaint", "VertexPaint"},  // W5
    {0xFB0FEB36194770D0ull, 8, "library", "Library"},          // W7
    {0x50329458AC585F77ull, 7, "part", "Part"},                // W2
    {0x7A3173B83AFDBBD0ull, 7, "forgesphere", "ForgeSphere"},  // W3
    {0x77AF1E52C70C5CDBull, 6, "classic", "Classic"},          // W1
    {0xEB424D5C1B5FCC43ull, 6, "litsphere", "LitSphere"},      // W6
    {0x90F1043555FF3A33ull, 5, "wire", "Wire"},                // W13 (the view.wire action id)
    {0x4265616148F33BECull, 4, "raise", "Raise"},              // W10
    {0xDE377B61A12DA42Dull, 4, "lower", "Lower"},              // W11
    {0xD4F75918FABCC503ull, 3, "bridge", "Bridge"},            // W12
};

// Whole names renamed where the word alone is an everyday one (a user's own brush may hold it): matched only as the
// entire string, ASCII case ignored. N1..N6 in docs/legal/trademarks.md.
struct Name {
    uint64_t hash;        // of the old name, lower case
    uint8_t length;       // its length
    std::string_view now; // today's name
};
inline constexpr Name kNames[] = {
    {0x71FEDAD706F8BDF4ull, 25, "brush.recipe.Stone Carve"}, // N1: the built-in recipe's action id
    {0xA8D1CB6C0FF343FCull, 12, "Stone Carve"},              // N2: its name
    {0x02E5D1FCA5703EA4ull, 6, "triangle_corners"},          // N3: extract's argument
    {0x374EB70D26D039F5ull, 7, "triangle_border"},           // N4: extract's argument
    {0x3160EB71CE86F92Dull, 19, "part.extract_triangle_corners"}, // N5: the Corners action id
    {0xB780FCA1FA84E3DEull, 20, "part.extract_triangle_border"},  // N6: the Border action id
};

// Today's spelling of `s`: a whole old name (kNames) replaced, or every old word in it, keeping its case (lower,
// Capitalised or UPPER). Returns `s` unchanged when it holds none. Only for names SculptForge itself wrote (ids,
// keys, command and value names, archive paths), never for user text or file-system paths.
inline std::string modernize(std::string_view s) {
    for (const Name& n : kNames)
        if (s.size() == n.length && hash(s) == n.hash) return std::string(n.now);
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size();) {
        const Word* hit = nullptr;
        if (std::isalpha(uint8_t(s[i])))
            for (const Word& w : kWords)
                if (i + w.length <= s.size() && hash(s.substr(i, w.length)) == w.hash) {
                    hit = &w;
                    break;
                }
        if (!hit) {
            out += s[i++];
            continue;
        }
        const bool upper = std::isupper(uint8_t(s[i])) != 0;
        bool allUpper = upper;
        for (size_t k = i; allUpper && k < i + hit->length; ++k) allUpper = !std::islower(uint8_t(s[k]));
        if (allUpper)
            for (char c : hit->lower) out += char(std::toupper(uint8_t(c)));
        else
            out += upper ? hit->title : hit->lower;
        i += hit->length;
    }
    return out;
}

// Whether `s` holds an old word (modernize would change it).
inline bool isOld(std::string_view s) { return modernize(s) != s; }

// `j` with every object key modernized (values as they are), for a file an older version wrote.
inline Json modernizeKeys(const Json& j) {
    if (j.isArray()) {
        Json out = Json::array();
        for (const Json& v : j.items()) out.push(modernizeKeys(v));
        return out;
    }
    if (!j.isObject()) return j;
    Json out = Json::object();
    for (const auto& [k, v] : j.members()) out.set(modernize(k), modernizeKeys(v));
    return out;
}

// Brush stored names renamed without a trademark reason (compared squashed and lower-case).
struct Rename {
    std::string_view old, now;
};
inline constexpr Rename kBrushNames[] = {{"snakehook", "Tendril"}};

} // namespace sf::legacy
