// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The order of the asset lists (brushes, alphas, materials, the Library): A-Z by default, the most recently used
// first, or grouped by pack (then A-Z). Names compare ignoring case, with numbers in number order ("Brush 2"
// before "Brush 10"). Pure functions, so the order can be tested without the UI.
#pragma once

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

enum class ListSort { AZ, Recent, Pack, Count };

inline const char* listSortLabel(ListSort s) {
    switch (s) {
    case ListSort::AZ: return "A-Z";
    case ListSort::Recent: return "Recent";
    case ListSort::Pack: return "Pack";
    default: return "";
    }
}
inline const char* listSortId(ListSort s) { // preferences
    switch (s) {
    case ListSort::Recent: return "recent";
    case ListSort::Pack: return "pack";
    default: return "az";
    }
}
inline ListSort listSortFromId(const std::string& id) {
    return id == "recent" ? ListSort::Recent : id == "pack" ? ListSort::Pack : ListSort::AZ;
}

// Natural, case-insensitive "a < b".
inline bool naturalLess(const std::string& a, const std::string& b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t ei = i, ej = j;
            while (ei < a.size() && a[ei] == '0') ++ei; // leading zeros don't count
            while (ej < b.size() && b[ej] == '0') ++ej;
            size_t ni = ei, nj = ej;
            while (ni < a.size() && std::isdigit(static_cast<unsigned char>(a[ni]))) ++ni;
            while (nj < b.size() && std::isdigit(static_cast<unsigned char>(b[nj]))) ++nj;
            if (ni - ei != nj - ej) return ni - ei < nj - ej;
            const int c = a.compare(ei, ni - ei, b, ej, nj - ej);
            if (c != 0) return c < 0;
            i = ni;
            j = nj;
            continue;
        }
        const int la = std::tolower(ca), lb = std::tolower(cb);
        if (la != lb) return la < lb;
        ++i;
        ++j;
    }
    if ((a.size() - i) != (b.size() - j)) return a.size() - i < b.size() - j;
    return a < b; // equal ignoring case: a stable, deterministic tie-break
}

struct ListSortKey {
    std::string name;
    std::string pack;  // "" = not from a pack (sorts first under Pack)
    int recent = -1;   // 0 = used last, 1 = before that ...; -1 = not used recently
    int packRank = -1; // the pack's place in a fixed order (built-in packs); -1 = A-Z by pack name after those
};

// The indices of `keys` in display order.
inline std::vector<size_t> listSortOrder(const std::vector<ListSortKey>& keys, ListSort mode) {
    std::vector<size_t> order(keys.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    auto byName = [&](size_t a, size_t b) { return naturalLess(keys[a].name, keys[b].name); };
    switch (mode) {
    case ListSort::Recent:
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const int ra = keys[a].recent < 0 ? INT32_MAX : keys[a].recent;
            const int rb = keys[b].recent < 0 ? INT32_MAX : keys[b].recent;
            return ra != rb ? ra < rb : byName(a, b);
        });
        break;
    case ListSort::Pack:
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const ListSortKey &ka = keys[a], &kb = keys[b];
            if (ka.pack.empty() != kb.pack.empty()) return ka.pack.empty();
            const int ra = ka.packRank < 0 ? INT32_MAX : ka.packRank, rb = kb.packRank < 0 ? INT32_MAX : kb.packRank;
            if (ra != rb) return ra < rb;
            if (ka.pack != kb.pack) return naturalLess(ka.pack, kb.pack);
            return byName(a, b);
        });
        break;
    default: std::stable_sort(order.begin(), order.end(), byName); break;
    }
    return order;
}

} // namespace sf
