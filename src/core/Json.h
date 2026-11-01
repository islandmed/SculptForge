// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal JSON value, parser and writer (brush files, AI provider requests
// and responses). Numbers are doubles; object key order is preserved.
#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sf {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(double d) : type_(Type::Number), number_(d) {}
    Json(int i) : type_(Type::Number), number_(i) {}
    Json(const char* s) : type_(Type::String), string_(s) {}
    Json(std::string s) : type_(Type::String), string_(std::move(s)) {}

    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json object() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    double asNumber(double fallback = 0.0) const { return type_ == Type::Number ? number_ : fallback; }
    const std::string& asString() const;

    // Arrays
    size_t size() const;
    const Json& operator[](size_t i) const;
    Json& push(Json v);
    const std::vector<Json>& items() const { return array_; }

    // Objects: get returns a shared null value when the key is missing.
    const Json& operator[](const std::string& key) const;
    Json& set(const std::string& key, Json v);
    bool has(const std::string& key) const;
    const std::vector<std::pair<std::string, Json>>& members() const { return object_; }

    std::string dump(bool pretty = false) const;
    static bool parse(const std::string& text, Json& out, std::string& error);
    // Like parse, but a key given twice in one object, or a number JSON doesn't allow (hex, inf, nan), is an error
    // (files from outside: packs, macros' companions, keymaps).
    static bool parseStrict(const std::string& text, Json& out, std::string& error);

private:
    void dumpTo(std::string& out, bool pretty, int indent) const;
    static bool parseImpl(const std::string& text, Json& out, std::string& error, bool strict);

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> object_;
};

std::string jsonQuote(const std::string& s);

} // namespace sf
