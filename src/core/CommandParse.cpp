// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/CommandParse.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace sf::cmd {

namespace {

using U32 = std::u32string;

// ---- text ------------------------------------------------------------------------------------------------------

U32 decode(std::string_view s) {
    U32 out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp = c;
        size_t n = 1;
        if (c >= 0xF0) { cp = c & 0x07; n = 4; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 3; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 2; }
        if (n > 1) {
            if (i + n > s.size()) break;
            for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        }
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string encode(const U32& s) {
    std::string out;
    for (char32_t cp : s) {
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
        else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    }
    return out;
}

// Case, accents and full-width forms folded away, so "Simetría", "simetria" and "ＳＩＭＥＴＲＩＡ" are one word.
char32_t fold(char32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xFF10 && c <= 0xFF19) return c - 0xFF10 + '0';
    if (c >= 0xFF21 && c <= 0xFF3A) return c - 0xFF21 + 'a';
    if (c >= 0xFF41 && c <= 0xFF5A) return c - 0xFF41 + 'a';
    if (c == 0xFF05) return '%';
    if (c == 0xFF0E) return '.';
    switch (c) {
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: return 'a';
    case 0xC8: case 0xC9: case 0xCA: case 0xCB: case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: return 'o';
    case 0xD9: case 0xDA: case 0xDB: case 0xDC: case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
    case 0xD1: case 0xF1: return 'n';
    case 0xC7: case 0xE7: return 'c';
    default: return c;
    }
}

bool isDigit(char32_t c) { return c >= '0' && c <= '9'; }

// Spaces and punctuation of any script. '%' and '.' are read by the number parser.
bool isSeparator(char32_t c) {
    if (c < 0x80) return !(std::isalnum(static_cast<int>(c)) || c == '%' || c == '.');
    if (c == 0x3000 || (c >= 0x2000 && c <= 0x206F) || (c >= 0x3001 && c <= 0x303F)) return true;
    if (c == 0xFF0C || c == 0xFF1B || c == 0xFF1A || c == 0xFF01 || c == 0xFF1F || c == 0xFF08 || c == 0xFF09 || c == 0xFF0F) return true;
    return c == 0xA0 || c == 0xB7 || c == 0xD7;
}

bool isCjk(char32_t c) { return c >= 0x2E80; }

struct Token {
    U32 text;
    bool number = false;
    double value = 0.0;
    Unit unit = Unit::None;
};

std::vector<Token> tokenize(std::string_view text) {
    U32 s = decode(text);
    for (auto& c : s) c = fold(c);
    std::vector<Token> out;
    for (size_t i = 0; i < s.size();) {
        const char32_t c = s[i];
        const bool startsNumber = isDigit(c) || (c == '.' && i + 1 < s.size() && isDigit(s[i + 1]));
        if (startsNumber) {
            size_t j = i;
            std::string digits;
            bool dot = false;
            // A comma between digits is a decimal comma ("1,5m"), a full stop the usual point.
            while (j < s.size() && (isDigit(s[j]) || ((s[j] == '.' || s[j] == ',') && !dot && (j == i ? s[j] == '.' : isDigit(s[j - 1])) && j + 1 < s.size() && isDigit(s[j + 1])))) {
                dot = dot || s[j] == '.' || s[j] == ',';
                digits += s[j] == ',' ? '.' : char(s[j]);
                ++j;
            }
            Token t;
            t.number = true;
            { // by hand, not atof: the point is a point whatever the user's locale says
                double whole = 0.0, frac = 0.0, scale = 1.0;
                bool after = false;
                for (char d : digits) {
                    if (d == '.') after = true;
                    else if (!after) whole = whole * 10.0 + (d - '0');
                    else frac = frac * 10.0 + (d - '0'), scale *= 10.0;
                }
                t.value = whole + frac / scale;
            }
            t.text = s.substr(i, j - i);
            if (j < s.size() && s[j] == '%') { t.unit = Unit::Percent; ++j; }
            else {
                size_t k = j;
                U32 suffix;
                while (k < s.size() && !isSeparator(s[k]) && !isDigit(s[k]) && s[k] != '%' && suffix.size() < 8) suffix += s[k++];
                const bool wordEnds = k >= s.size() || isSeparator(s[k]) || isDigit(s[k]);
                if (wordEnds && suffix == U"px") { t.unit = Unit::Px; j = k; }
                else if (wordEnds && suffix == U"k") { t.unit = Unit::Triangles; t.value *= 1e3; j = k; }
                else if (wordEnds && suffix == U"m") { t.unit = Unit::Triangles; t.value *= 1e6; j = k; }
            }
            out.push_back(std::move(t));
            i = j;
            continue;
        }
        if (isSeparator(c)) { ++i; continue; }
        size_t j = i;
        while (j < s.size() && !isSeparator(s[j]) && !isDigit(s[j]) && !(s[j] == '.' && j + 1 < s.size() && isDigit(s[j + 1])) &&
               !(j > i && s[j] == '%') && !(j > i && isCjk(s[j]) != isCjk(s[j - 1]))) // "对称x": two words
            ++j;
        if (j == i) { ++i; continue; }
        Token t;
        t.text = s.substr(i, j - i);
        // "200 k", "30 %", "40 px": a unit word right after a bare number.
        if (!out.empty() && out.back().number && out.back().unit == Unit::None) {
            Token& n = out.back();
            if (t.text == U"%" || t.text == U"percent") { n.unit = Unit::Percent; i = j; continue; }
            if (t.text == U"px" || t.text == U"pixels" || t.text == U"pixel") { n.unit = Unit::Px; i = j; continue; }
            if (t.text == U"k") { n.unit = Unit::Triangles; n.value *= 1e3; i = j; continue; }
            if (t.text == U"m") { n.unit = Unit::Triangles; n.value *= 1e6; i = j; continue; }
        }
        out.push_back(std::move(t));
        i = j;
    }
    return out;
}

std::vector<U32> wordsOf(std::string_view text) {
    std::vector<U32> out;
    for (auto& t : tokenize(text)) out.push_back(t.number ? t.text : std::move(t.text));
    return out;
}

// ---- matching --------------------------------------------------------------------------------------------------

int osaDistance(const U32& a, const U32& b) {
    const size_t n = a.size(), m = b.size();
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1));
    for (size_t i = 0; i <= n; ++i) d[i][0] = int(i);
    for (size_t j = 0; j <= m; ++j) d[0][j] = int(j);
    for (size_t i = 1; i <= n; ++i)
        for (size_t j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    return d[n][m];
}

bool hasCjk(const U32& w) { return std::any_of(w.begin(), w.end(), isCjk); }

// How many letters may be wrong in a word of that length: none in short words, where a typo is another word.
int tolerance(const U32& w) {
    const size_t n = w.size();
    if (hasCjk(w)) return n >= 4 ? 1 : 0;
    return n <= 3 ? 0 : n <= 6 ? 1 : 2;
}

// 0 = not at all, 1 = the same word. A start of the word counts ("ortho" for "orthographic"), a slip of the keys too.
double wordScore(const U32& q, const U32& n) {
    if (q.empty() || n.empty()) return 0.0;
    if (q == n) return 1.0;
    const bool cjk = hasCjk(n);
    if (q.size() >= (cjk ? 1 : 2) && n.size() > q.size() && n.compare(0, q.size(), q) == 0)
        return std::min(0.93, 0.78 + 0.15 * double(q.size()) / double(n.size()));
    if (cjk && q.size() >= 2 && n.find(q) != U32::npos) return 0.8;
    if (q.size() == 3 && n.size() >= 4 && !cjk && osaDistance(q, n) == 1) return 0.66; // "hed" for "head": a letter missing
    const int tol = tolerance(q);
    if (tol > 0) {
        const int d = osaDistance(q, n);
        if (d <= tol) return 0.85 - 0.12 * d;
        if (n.size() > q.size() && q.size() >= 4) { // a typo inside the start of a longer word
            const int dp = osaDistance(q, n.substr(0, q.size()));
            if (dp <= tol) return 0.72 - 0.1 * dp;
        }
    }
    return 0.0;
}

// How well QWORDS read as the NAME's words, 0..1: every query word must find a word of the name.
double nameScore(const std::vector<U32>& q, const std::vector<U32>& name) {
    if (q.empty() || name.empty()) return 0.0;
    double sum = 0.0;
    std::set<size_t> covered;
    bool all = true;
    for (const auto& w : q) {
        double best = 0.0;
        size_t at = 0;
        for (size_t i = 0; i < name.size(); ++i)
            if (const double s = wordScore(w, name[i]); s > best) best = s, at = i;
        if (best <= 0.0) { all = false; break; }
        sum += best;
        covered.insert(at);
    }
    double scored = 0.0;
    if (all) scored = (sum / double(q.size())) * (0.75 + 0.25 * double(covered.size()) / double(name.size()));
    if (!all || scored < 0.999) { // "quadremesh" for "Quad Remesh": the words run together or apart
        U32 a, b;
        for (const auto& w : q) a += w;
        for (const auto& w : name) b += w;
        scored = std::max(scored, wordScore(a, b) * 0.97);
    }
    return scored;
}

double bestNameScore(const std::vector<U32>& q, const std::vector<std::vector<U32>>& names) {
    double best = 0.0;
    for (const auto& n : names) best = std::max(best, nameScore(q, n));
    return best;
}

const std::set<std::u32string>& stopWords() {
    static const std::set<std::u32string> w = {U"to", U"the", U"a", U"an", U"at", U"by", U"with", U"of", U"please", U"my",
                                               U"set", U"use", U"select", U"pick", U"choose", U"switch", U"make", U"go", U"turn"};
    return w;
}

std::vector<U32> withoutStopWords(const std::vector<U32>& words) {
    std::vector<U32> out;
    for (const auto& w : words)
        if (!stopWords().count(w)) out.push_back(w);
    return out.empty() ? words : out;
}

bool oneOf(const U32& w, std::initializer_list<const char32_t*> set) {
    for (const char32_t* s : set)
        if (w == s) return true;
    return false;
}

constexpr double kMinScore = 0.6;
constexpr double kMinTriangles = 1000.0, kMaxTriangles = 50e6;
constexpr double kTrianglesFrom = kMinTriangles; // "remesh 300" is voxels, "remesh 20000" triangles

double clampInto(double v, double lo, double hi, bool& clamped) {
    if (hi < lo) return v;
    const double c = std::clamp(v, lo, hi);
    clamped = clamped || c != v;
    return c;
}

struct Parsed {
    std::vector<Token> tokens;
    std::vector<U32> words; // the words, in order
    int numbers = 0;
};

// ---- one entry -------------------------------------------------------------------------------------------------

bool readBrush(const Entry& e, const Parsed& p, const std::vector<std::vector<U32>>& names, Interpretation& r) {
    std::vector<U32> words;
    for (size_t i = 0; i < p.tokens.size(); ++i) {
        const Token& t = p.tokens[i];
        if (!t.number) {
            const bool isLabel = oneOf(t.text, {U"size", U"radius", U"width", U"intensity", U"strength", U"pressure", U"opacity"});
            const bool nextNumber = i + 1 < p.tokens.size() && p.tokens[i + 1].number;
            const bool prevNumber = i > 0 && p.tokens[i - 1].number;
            if (isLabel && (nextNumber || prevNumber)) continue; // "size 40" / "40 size": the label of the number beside it
            words.push_back(t.text);
            continue;
        }
        if (t.unit == Unit::Triangles) return false;
        bool intensity = t.unit == Unit::Percent;
        if (i > 0 && !p.tokens[i - 1].number && oneOf(p.tokens[i - 1].text, {U"intensity", U"strength", U"pressure", U"opacity"})) intensity = true;
        const bool labelBefore = i > 0 && !p.tokens[i - 1].number && oneOf(p.tokens[i - 1].text, {U"size", U"radius", U"width"});
        if (!labelBefore && i + 1 < p.tokens.size() && !p.tokens[i + 1].number && oneOf(p.tokens[i + 1].text, {U"intensity", U"strength", U"pressure", U"opacity"})) intensity = true;
        if (intensity) {
            if (r.hasIntensity) return false;
            r.hasIntensity = true;
            r.intensity = clampInto(t.value, 0.0, 100.0, r.clamped);
        } else {
            if (r.hasSize) return false;
            r.hasSize = true;
            r.size = clampInto(t.value, e.lo, e.hi, r.clamped);
        }
    }
    words = withoutStopWords(words);
    if (words.size() > 1) words.erase(std::remove(words.begin(), words.end(), U32(U"brush")), words.end()); // "the clay brush"
    if (words.empty()) return false;
    r.score = 100.0 * bestNameScore(words, names);
    return r.score >= 100.0 * kMinScore;
}

bool readValue(const Entry& e, const Parsed& p, const std::vector<std::vector<U32>>& names, Interpretation& r) {
    if (p.numbers > 1) return false;
    std::vector<U32> words;
    for (const auto& t : p.tokens) {
        if (!t.number) { words.push_back(t.text); continue; }
        if (t.unit == Unit::Triangles) return false;
        r.hasNumber = true;
        r.unit = t.unit;
        r.number = clampInto(t.value, e.lo, e.hi, r.clamped);
    }
    words = withoutStopWords(words);
    r.needsNumber = !r.hasNumber;
    r.score = 100.0 * bestNameScore(words, names);
    return r.score >= 100.0 * kMinScore;
}

bool readSwitch(const Parsed& p, const std::vector<std::vector<U32>>& names, Interpretation& r) {
    if (p.numbers > 0) return false;
    double best = 0.0;
    for (const bool takeAxis : {true, false}) {
        std::vector<U32> words;
        int state = -1;
        bool stated = false, bad = false;
        char axis = 0;
        for (const auto& w : p.words) {
            int s = -2;
            if (oneOf(w, {U"on", U"enable", U"enabled", U"yes", U"true", U"activar", U"activado", U"si", U"开", U"開", U"打开", U"開啟", U"开启"})) s = 1;
            else if (oneOf(w, {U"off", U"disable", U"disabled", U"no", U"false", U"desactivar", U"desactivado", U"关", U"關", U"关闭", U"關閉"})) s = 0;
            else if (oneOf(w, {U"flip", U"toggle", U"alternar", U"切换", U"切換"})) s = -1;
            if (s != -2) {
                if (stated && state != s) bad = true;
                state = s, stated = true;
                continue;
            }
            if (takeAxis && w.size() == 1 && (w[0] == 'x' || w[0] == 'y' || w[0] == 'z')) {
                if (axis && axis != char(w[0])) bad = true;
                axis = char(w[0]);
                continue;
            }
            words.push_back(w);
        }
        words = withoutStopWords(words);
        if (bad || words.empty()) continue;
        if (const double s = bestNameScore(words, names); s > best) {
            best = s;
            r.state = state;
            r.axis = axis;
        }
    }
    r.score = 100.0 * best;
    return r.score >= 100.0 * kMinScore;
}

bool readAmount(const Entry& e, const Parsed& p, const std::vector<std::vector<U32>>& names, Interpretation& r) {
    if (p.numbers > 1) return false;
    std::vector<U32> words;
    for (const auto& t : p.tokens) {
        if (!t.number) { words.push_back(t.text); continue; }
        if (t.unit == Unit::Percent) return false;
        r.hasNumber = true;
        r.unit = (t.unit == Unit::Triangles || (t.unit == Unit::None && t.value >= kTrianglesFrom)) ? Unit::Triangles : Unit::Px;
        r.number = r.unit == Unit::Triangles ? clampInto(t.value, kMinTriangles, kMaxTriangles, r.clamped)
                                             : clampInto(t.value, e.lo, e.hi, r.clamped);
    }
    words = withoutStopWords(words);
    r.score = 100.0 * bestNameScore(words, names);
    return r.score >= 100.0 * kMinScore;
}

bool readPlain(const Parsed& p, const std::vector<std::vector<U32>>& names, Interpretation& r) {
    if (p.numbers > 0) return false;
    r.score = 100.0 * bestNameScore(withoutStopWords(p.words), names);
    return r.score >= 100.0 * kMinScore;
}

bool readVisibility(const Catalog& catalog, const Parsed& p, const std::vector<std::vector<U32>>& names, Interpretation& r) {
    if (p.numbers > 0 || p.words.empty()) return false;
    const double verb = bestNameScore({p.words[0]}, names);
    if (verb < kMinScore) return false;
    std::vector<U32> phrase;
    for (size_t i = 1; i < p.words.size(); ++i) {
        const U32& w = p.words[i];
        if (stopWords().count(w) || oneOf(w, {U"parts", U"part", U"else"})) continue;
        if (oneOf(w, {U"all", U"everything", U"every", U"whole", U"todo", U"todos", U"todas", U"全部", U"所有", U"一切"})) r.all = true;
        else if (oneOf(w, {U"but", U"except", U"besides", U"without", U"excluding", U"menos", U"excepto", U"salvo", U"除了", U"除"})) r.except = true;
        else if (w == U"only" || w == U"solo" || w == U"只" || w == U"仅" || w == U"僅") r.only = true;
        else phrase.push_back(w);
    }
    r.score = 100.0 * verb;
    for (const auto& w : phrase) r.target += (r.target.empty() ? "" : " ") + encode(w);
    if (phrase.empty()) {
        if (r.except) return false;                   // "hide all but" and then nothing
        if (!r.all) { r.needsTarget = true; return p.words.size() == 1; } // only the verb: say what is missing
        return true;
    }
    if (r.all && !r.except) return false;             // "hide all head": not a sentence
    double best = 0.0;
    std::vector<std::pair<double, size_t>> hits;
    for (size_t i = 0; i < catalog.parts.size(); ++i) {
        const double s = nameScore(phrase, wordsOf(catalog.parts[i]));
        if (s >= kMinScore) hits.emplace_back(s, i), best = std::max(best, s);
    }
    for (const auto& [s, i] : hits)
        if (s >= best - 0.05) r.parts.push_back(i);
    return !r.parts.empty();
}

std::string formatNumber(double v) {
    char buf[32];
    if (std::abs(v - std::round(v)) < 0.05) std::snprintf(buf, sizeof buf, "%.0f", v);
    else std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

std::string writeNumber(double v, Unit unit) {
    switch (unit) {
    case Unit::Percent: return formatNumber(v) + "%";
    case Unit::Px: return formatNumber(v) + "px";
    case Unit::Triangles:
        if (v >= 1e6) return formatNumber(v / 1e6) + "m";
        if (v >= 1e3) return formatNumber(v / 1e3) + "k";
        return formatNumber(v);
    default: return formatNumber(v);
    }
}

} // namespace

bool parseNumber(std::string_view text, double& value, Unit& unit) {
    const auto t = tokenize(text);
    if (t.size() != 1 || !t[0].number) return false;
    value = t[0].value;
    unit = t[0].unit;
    return true;
}

int editDistance(std::string_view a, std::string_view b) {
    U32 x = decode(a), y = decode(b);
    for (auto& c : x) c = fold(c);
    for (auto& c : y) c = fold(c);
    return osaDistance(x, y);
}

std::vector<Interpretation> interpret(const Catalog& catalog, std::string_view query, size_t limit) {
    Parsed p;
    p.tokens = tokenize(query);
    for (const auto& t : p.tokens) {
        if (t.number) ++p.numbers;
        else p.words.push_back(t.text);
    }
    std::vector<Interpretation> out;
    if (p.tokens.empty()) return out;
    for (size_t i = 0; i < catalog.entries.size(); ++i) {
        const Entry& e = catalog.entries[i];
        std::vector<std::vector<U32>> names;
        names.reserve(e.names.size() + 1);
        names.push_back(wordsOf(e.label));
        for (const auto& n : e.names) names.push_back(wordsOf(n));
        Interpretation r;
        r.entry = i;
        bool ok = false;
        switch (e.role) {
        case Role::Plain: ok = readPlain(p, names, r); break;
        case Role::Brush: ok = readBrush(e, p, names, r); break;
        case Role::Value: ok = readValue(e, p, names, r); break;
        case Role::Switch: ok = readSwitch(p, names, r); break;
        case Role::Amount: ok = readAmount(e, p, names, r); break;
        case Role::Visibility: ok = readVisibility(catalog, p, names, r); break;
        }
        if (!ok) continue;
        r.exact = r.score >= 99.9;
        out.push_back(std::move(r));
    }
    std::stable_sort(out.begin(), out.end(), [&](const Interpretation& a, const Interpretation& b) {
        if (std::abs(a.score - b.score) > 1e-6) return a.score > b.score;
        if (a.parametrised() != b.parametrised()) return a.parametrised();
        return catalog.entries[a.entry].priority > catalog.entries[b.entry].priority;
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

std::vector<std::string> synonymNames(std::string_view id) {
    static const std::map<std::string, std::string, std::less<>> table = {
        {"view.ortho", "orthographic, flat view, no perspective, parallel view"},
        {"view.orbit_lock_y", "turntable, spin lock, lock vertical"},
        {"view.wireframe", "wireframe, wires, mesh lines, polygons"},
        {"view.xray", "xray, see through, transparent"},
        {"view.frame", "fit, focus, center, zoom to fit"},
        {"view.reset", "home, default view"},
        {"geometry.remesh", "remesh, rebuild, even triangles, voxel"},
        {"geometry.divide", "subdivide, more detail, higher resolution"},
        {"@symmetry", "mirror, mirroring, symmetric, symmetrical, symmetry"},
        {"@size", "size, brush size, radius, width"},
        {"@intensity", "intensity, strength, pressure, opacity"},
        {"@hide", "hide, conceal, invisible"},
        {"@show", "show, unhide, reveal, visible"},
        {"@isolate", "isolate, solo, focus on"},
    };
    std::vector<std::string> out;
    const auto it = table.find(id);
    if (it == table.end()) return out;
    std::string_view rest = it->second;
    while (!rest.empty()) {
        const size_t comma = rest.find(',');
        out.emplace_back(rest.substr(0, comma));
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
    }
    return out;
}

std::string canonicalLine(const Catalog& catalog, const Interpretation& r) {
    const Entry& e = catalog.entries[r.entry];
    std::string line = e.label;
    switch (e.role) {
    case Role::Brush:
        if (r.hasSize) line += " " + writeNumber(r.size, Unit::None);
        if (r.hasIntensity) line += " " + writeNumber(r.intensity, Unit::Percent);
        break;
    case Role::Value:
    case Role::Amount:
        if (r.hasNumber) line += " " + writeNumber(r.number, r.unit);
        break;
    case Role::Switch:
        if (r.axis) line += std::string(" ") + char(std::toupper(static_cast<unsigned char>(r.axis)));
        if (r.state == 1) line += " on";
        else if (r.state == 0) line += " off";
        break;
    case Role::Visibility:
        if (r.only) line += " only";
        if (r.all) line += " all";
        if (r.except) line += " but";
        if (r.parts.size() == 1 && r.parts[0] < catalog.parts.size()) line += " " + catalog.parts[r.parts[0]]; // the Part as named
        else if (!r.target.empty()) line += " " + r.target;                                                   // several: as typed
        break;
    case Role::Plain: break;
    }
    return line;
}

void CommandHistory::push(std::string line) {
    cursor_ = -1;
    if (line.empty()) return;
    lines_.erase(std::remove(lines_.begin(), lines_.end(), line), lines_.end());
    lines_.insert(lines_.begin(), std::move(line));
    if (lines_.size() > kMax) lines_.resize(kMax);
}

std::string CommandHistory::older() {
    if (lines_.empty()) return {};
    if (cursor_ + 1 < int(lines_.size())) ++cursor_;
    return lines_[size_t(std::max(cursor_, 0))];
}

std::string CommandHistory::newer() {
    if (cursor_ <= 0) { cursor_ = -1; return {}; }
    return lines_[size_t(--cursor_)];
}

std::string CommandHistory::toText() const {
    std::string out;
    for (const auto& l : lines_) out += l + "\n";
    return out;
}

void CommandHistory::fromText(std::string_view text) {
    lines_.clear();
    cursor_ = -1;
    while (!text.empty() && lines_.size() < kMax) {
        const size_t nl = text.find('\n');
        std::string l(text.substr(0, nl));
        while (!l.empty() && (l.back() == '\r' || l.back() == ' ')) l.pop_back();
        if (!l.empty() && std::find(lines_.begin(), lines_.end(), l) == lines_.end()) lines_.push_back(std::move(l));
        text = nl == std::string_view::npos ? std::string_view{} : text.substr(nl + 1);
    }
}

} // namespace sf::cmd
