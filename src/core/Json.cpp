// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sf {

namespace {

const Json kNull;
const std::string kEmpty;

struct Parser {
    explicit Parser(const std::string& text) : s(text) {}
    const std::string& s;
    size_t i = 0;
    std::string error;
    int depth = 0;
    bool strict = false; // parseStrict: duplicate keys and numbers outside JSON's grammar are errors

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    }
    bool fail(const std::string& msg) {
        if (error.empty()) error = msg + " at offset " + std::to_string(i);
        return false;
    }
    bool literal(const char* lit) {
        size_t n = std::char_traits<char>::length(lit);
        if (s.compare(i, n, lit) != 0) return fail("Invalid literal");
        i += n;
        return true;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v) {
        if (i + 4 > s.size()) return fail("Truncated escape");
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else return fail("Bad hex escape");
        }
        return true;
    }
    bool string(std::string& out) {
        ++i; // opening quote
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp;
                if (!hex4(cp)) return false;
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    i += 2;
                    unsigned lo;
                    if (!hex4(lo)) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(out, cp);
                break;
            }
            default: return fail("Bad escape");
            }
        }
        return fail("Unterminated string");
    }
    bool value(Json& out) {
        if (++depth > 256) return fail("Nesting too deep");
        ws();
        if (i >= s.size()) return fail("Unexpected end");
        char c = s[i];
        bool ok = true;
        if (c == '{') {
            out = Json::object();
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') {
                ++i;
            } else {
                while (true) {
                    ws();
                    if (i >= s.size() || s[i] != '"') { ok = fail("Expected key"); break; }
                    std::string key;
                    if (!string(key)) { ok = false; break; }
                    if (strict && out.has(key)) { ok = fail("Duplicate key \"" + key + "\""); break; }
                    ws();
                    if (i >= s.size() || s[i] != ':') { ok = fail("Expected ':'"); break; }
                    ++i;
                    Json v;
                    if (!value(v)) { ok = false; break; }
                    out.set(key, std::move(v));
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == '}') { ++i; break; }
                    ok = fail("Expected ',' or '}'");
                    break;
                }
            }
        } else if (c == '[') {
            out = Json::array();
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') {
                ++i;
            } else {
                while (true) {
                    Json v;
                    if (!value(v)) { ok = false; break; }
                    out.push(std::move(v));
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == ']') { ++i; break; }
                    ok = fail("Expected ',' or ']'");
                    break;
                }
            }
        } else if (c == '"') {
            std::string str;
            ok = string(str);
            out = Json(std::move(str));
        } else if (c == 't') {
            ok = literal("true");
            out = Json(true);
        } else if (c == 'f') {
            ok = literal("false");
            out = Json(false);
        } else if (c == 'n') {
            ok = literal("null");
            out = Json();
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const char* start = s.c_str() + i;
            char* end = nullptr;
            double d = std::strtod(start, &end);
            if (end == start) ok = fail("Bad number");
            // strtod also takes hex, "inf", "nan" and a leading '+': not JSON.
            else if (strict) {
                for (const char* q = start; q < end; ++q)
                    if (!std::strchr("0123456789+-.eE", *q)) { ok = fail("Bad number"); break; }
                if (ok && !std::isfinite(d)) ok = fail("Number out of range");
            }
            i += size_t(end - start);
            out = Json(d);
        } else {
            ok = fail("Unexpected character");
        }
        --depth;
        return ok;
    }
};

void newline(std::string& out, bool pretty, int indent) {
    if (!pretty) return;
    out += '\n';
    out.append(size_t(indent) * 2, ' ');
}

} // namespace

std::string jsonQuote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    return out + "\"";
}

const std::string& Json::asString() const { return type_ == Type::String ? string_ : kEmpty; }

size_t Json::size() const { return type_ == Type::Array ? array_.size() : type_ == Type::Object ? object_.size() : 0; }

const Json& Json::operator[](size_t i) const { return type_ == Type::Array && i < array_.size() ? array_[i] : kNull; }

Json& Json::push(Json v) {
    if (type_ != Type::Array) *this = array();
    array_.push_back(std::move(v));
    return array_.back();
}

const Json& Json::operator[](const std::string& key) const {
    if (type_ == Type::Object)
        for (const auto& [k, v] : object_)
            if (k == key) return v;
    return kNull;
}

Json& Json::set(const std::string& key, Json v) {
    if (type_ != Type::Object) *this = object();
    for (auto& [k, existing] : object_) {
        if (k == key) {
            existing = std::move(v);
            return existing;
        }
    }
    object_.emplace_back(key, std::move(v));
    return object_.back().second;
}

bool Json::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (const auto& [k, v] : object_)
        if (k == key) return true;
    return false;
}

void Json::dumpTo(std::string& out, bool pretty, int indent) const {
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
        if (!std::isfinite(number_)) {
            out += "null";
        } else if (number_ == std::floor(number_) && std::abs(number_) < 9007199254740992.0) { // 2^53: exact in a double
            out += std::to_string(static_cast<long long>(number_));
        } else {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.9g", number_);
            out += buf;
        }
        break;
    }
    case Type::String: out += jsonQuote(string_); break;
    case Type::Array:
        out += '[';
        for (size_t k = 0; k < array_.size(); ++k) {
            if (k) out += ',';
            newline(out, pretty, indent + 1);
            array_[k].dumpTo(out, pretty, indent + 1);
        }
        if (!array_.empty()) newline(out, pretty, indent);
        out += ']';
        break;
    case Type::Object:
        out += '{';
        for (size_t k = 0; k < object_.size(); ++k) {
            if (k) out += ',';
            newline(out, pretty, indent + 1);
            out += jsonQuote(object_[k].first);
            out += pretty ? ": " : ":";
            object_[k].second.dumpTo(out, pretty, indent + 1);
        }
        if (!object_.empty()) newline(out, pretty, indent);
        out += '}';
        break;
    }
}

std::string Json::dump(bool pretty) const {
    std::string out;
    dumpTo(out, pretty, 0);
    return out;
}

bool Json::parse(const std::string& text, Json& out, std::string& error) { return parseImpl(text, out, error, false); }
bool Json::parseStrict(const std::string& text, Json& out, std::string& error) { return parseImpl(text, out, error, true); }

bool Json::parseImpl(const std::string& text, Json& out, std::string& error, bool strict) {
    Parser p(text);
    p.strict = strict;
    Json v;
    if (!p.value(v)) {
        error = p.error;
        return false;
    }
    p.ws();
    if (p.i != text.size()) {
        error = "Trailing characters at offset " + std::to_string(p.i);
        return false;
    }
    out = std::move(v);
    return true;
}

} // namespace sf
