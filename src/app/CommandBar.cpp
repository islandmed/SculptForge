// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The command line of the F3 search: the catalog the parser matches against (core/CommandParse.*), the preview of what
// a reading will do, and the code that runs it. It only calls actions and commands that already exist, with numbers
// the parser clamped into each range; nothing here is new behaviour.
#include "app/App.h"
#include "app/I18n.h"
#include "core/Precision.h"
#include "core/Voxel.h"
#include "app/SearchAliases.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>

namespace sf {

namespace {

std::filesystem::path fsPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

// Every shipped language's word for an English label, so "平滑" or "Suavizar" finds Smooth whatever the interface language.
void addNames(cmd::Entry& e, const std::string& english) {
    auto add = [&](std::string s) {
        if (!s.empty() && std::find(e.names.begin(), e.names.end(), s) == e.names.end() && s != e.label) e.names.push_back(std::move(s));
    };
    add(english);
    for (int l = 1; l < kLanguageCount; ++l)
        if (const char* t = i18n::lookup(Language(l), english)) add(t);
}

std::string number(double v) {
    char buf[32];
    if (std::abs(v - std::round(v)) < 0.05) std::snprintf(buf, sizeof buf, "%.0f", v);
    else std::snprintf(buf, sizeof buf, "%.1f", v);
    return buf;
}

std::string fillIn(const char* pattern, const std::string& a, const std::string& b = {}) {
    std::string out;
    int used = 0;
    for (const char* p = pattern; *p; ++p) {
        if (p[0] == '%' && p[1] == 's') {
            out += used++ == 0 ? a : b;
            ++p;
        } else {
            out += *p;
        }
    }
    return out;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (const auto& n : names) out += (out.empty() ? "" : ", ") + n;
    return out;
}

std::string upper(char axis) { return std::string(1, char(std::toupper(static_cast<unsigned char>(axis)))); }

bool isSwitchAction(const std::string& id) {
    return id == "view.wireframe" || id == "view.xray" || id == "view.ortho" || id == "view.texture";
}

} // namespace

const cmd::Catalog& App::commandCatalog() {
    const std::string key = std::to_string(actions_.revision()) + "/" + std::to_string(int(language())) + "/" +
                            std::to_string(doc_.brushes.size());
    if (key != commandCatalogKey_) {
        commandCatalogKey_ = key;
        cmd::Catalog& c = commandCatalog_;
        c.entries.clear();
        std::set<std::string> brushIds, brushLabels;
        for (int i = 0; i < int(BrushType::Count); ++i) {
            const auto type = BrushType(i);
            std::string id = "brush.";
            for (const char* ch = brushName(type); *ch; ++ch)
                if (*ch != ' ') id += char(std::tolower(static_cast<unsigned char>(*ch)));
            if (!actions_.find(id)) continue;
            cmd::Entry e;
            e.id = id;
            e.label = brushLabel(type);
            e.role = cmd::Role::Brush;
            e.lo = kMinDrawSizePx, e.hi = kMaxDrawSizePx;
            e.priority = 2;
            addNames(e, brushLabel(type)); // the language tables name some brushes (a Chinese "平滑" finds Smooth)
            e.names.push_back(brushName(type));
            c.entries.push_back(std::move(e));
            brushIds.insert(id);
            brushLabels.insert(brushLabel(type));
        }
        for (const BrushRecipe& r : doc_.brushes) { // brushes from the library and packs
            if (r.name.empty() || brushLabels.count(r.name)) continue;
            cmd::Entry e;
            e.id = "@brush:" + r.name;
            e.label = r.name;
            e.role = cmd::Role::Brush;
            e.lo = kMinDrawSizePx, e.hi = kMaxDrawSizePx;
            e.priority = 1;
            c.entries.push_back(std::move(e));
        }
        for (const Action& a : actions_.all()) {
            if (!a.run || a.label.empty() || brushIds.count(a.id) || a.id.rfind("cmd.", 0) == 0) continue;
            cmd::Entry e;
            e.id = a.id;
            e.label = trAction(a.id, a.label);
            e.role = isSwitchAction(a.id) ? cmd::Role::Switch : a.id == "geometry.remesh" ? cmd::Role::Amount : cmd::Role::Plain;
            if (e.role == cmd::Role::Amount) e.lo = kVoxelResolutionMin, e.hi = kVoxelResolutionMax;
            addNames(e, a.label);
            for (const auto& o : searchAliasesIn(a.label, a.palette)) e.names.push_back(o);
            for (auto& s : cmd::synonymNames(a.id)) e.names.push_back(std::move(s));
            c.entries.push_back(std::move(e));
        }
        auto special = [&](const char* id, const char* english, cmd::Role role, double lo, double hi) {
            cmd::Entry e;
            e.id = id;
            e.label = tr(english);
            e.role = role;
            e.lo = lo, e.hi = hi;
            addNames(e, english);
            for (auto& s : cmd::synonymNames(id)) e.names.push_back(std::move(s));
            c.entries.push_back(std::move(e));
        };
        special("@size", "Size", cmd::Role::Value, kMinDrawSizePx, kMaxDrawSizePx);
        special("@intensity", "Intensity", cmd::Role::Value, 0.0, 100.0);
        special("@symmetry", "Symmetry", cmd::Role::Switch, 0.0, 0.0);
        special("@hide", "Hide", cmd::Role::Visibility, 0.0, 0.0);
        special("@show", "Show", cmd::Role::Visibility, 0.0, 0.0);
        special("@isolate", "Isolate", cmd::Role::Visibility, 0.0, 0.0);
    }
    commandCatalog_.parts.clear();
    for (const auto& p : doc_.parts) commandCatalog_.parts.push_back(p.name);
    return commandCatalog_;
}

namespace {

// The action whose key runs the same thing (its chord goes in the result line).
std::string keyActionId(const cmd::Entry& e, const cmd::Interpretation& r) {
    if (e.id == "@symmetry") return std::string("transform.symmetry_") + (r.axis ? r.axis : 'x');
    if (e.id.empty() || e.id[0] == '@') return {};
    return e.id;
}

} // namespace

std::string App::commandKeys(const cmd::Interpretation& r) {
    const cmd::Catalog& c = commandCatalog();
    if (r.entry >= c.entries.size()) return {};
    const std::string id = keyActionId(c.entries[r.entry], r);
    return id.empty() ? std::string() : actions_.shortcutText(id);
}

const char* App::commandIcon(const cmd::Interpretation& r) {
    const cmd::Catalog& c = commandCatalog();
    if (r.entry >= c.entries.size()) return nullptr;
    const std::string id = keyActionId(c.entries[r.entry], r);
    const Action* a = id.empty() ? nullptr : actions_.find(id);
    return a ? a->icon : nullptr;
}

std::string App::commandPreview(const cmd::Interpretation& r) {
    const cmd::Catalog& c = commandCatalog();
    if (r.entry >= c.entries.size()) return {};
    const cmd::Entry& e = c.entries[r.entry];
    switch (e.role) {
    case cmd::Role::Brush: {
        std::string out = e.label;
        std::string with;
        if (r.hasSize) with += std::string(tr("Size")) + " " + number(r.size);
        if (r.hasIntensity) with += (with.empty() ? "" : ", ") + std::string(tr("Intensity")) + " " + number(r.intensity) + "%";
        return with.empty() ? out : out + " - " + with;
    }
    case cmd::Role::Value: {
        if (!r.hasNumber) return fillIn(tr("%s - type a number"), e.label);
        return e.label + " " + number(r.number) + (e.id == "@intensity" ? "%" : "");
    }
    case cmd::Role::Switch: {
        bool now = false;
        std::string name = e.label;
        if (e.id == "@symmetry") {
            now = r.axis == 'y' ? symmetry_.y : r.axis == 'z' ? symmetry_.z : symmetry_.x;
            if (r.axis) name += " " + upper(r.axis);
            else if (r.state == 0) now = true; // "symmetry off" with no axis turns every axis off
        } else if (const Action* a = actions_.find(e.id); a && a->checked) {
            now = a->checked();
        }
        const bool after = r.state < 0 ? !now : r.state == 1;
        return name + " - " + tr(after ? "On" : "Off");
    }
    case cmd::Role::Amount: {
        if (!r.hasNumber) return e.label;
        if (r.unit == cmd::Unit::Triangles) {
            const double n = r.number;
            const std::string shown = n >= 1e6 ? number(n / 1e6) + "m" : n >= 1e3 ? number(n / 1e3) + "k" : number(n);
            return fillIn(tr("%s - about %s triangles"), e.label, shown);
        }
        return fillIn(tr("%s - %s voxels"), e.label, number(r.number));
    }
    case cmd::Role::Visibility: {
        std::vector<std::string> names;
        for (size_t i : r.parts)
            if (i < c.parts.size()) names.push_back(c.parts[i]);
        const std::string some = joinNames(names);
        if (r.needsTarget) return fillIn(tr("%s - name a Part, or say all"), e.label);
        if (e.id == "@isolate") return fillIn(tr("Isolate %s"), some);
        if (e.id == "@hide") {
            if (r.all && r.except) return fillIn(tr("Hide all except %s"), some);
            return r.all ? std::string(tr("Hide all")) : fillIn(tr("Hide %s"), some);
        }
        if (r.only) return fillIn(tr("Show only %s"), some);
        if (r.all && r.except) return fillIn(tr("Show all except %s"), some);
        return r.all ? std::string(tr("Show all")) : fillIn(tr("Show %s"), some);
    }
    case cmd::Role::Plain: break;
    }
    return e.label;
}

bool App::commandRunnable(const cmd::Interpretation& r) const {
    return !r.needsNumber && !r.needsTarget;
}

std::string App::runCommandLine(const cmd::Interpretation& r) {
    const cmd::Catalog& c = commandCatalog();
    if (r.entry >= c.entries.size()) return {};
    const cmd::Entry& e = c.entries[r.entry];
    const std::string line = cmd::canonicalLine(c, r);
    const std::string shown = commandPreview(r); // before the run: the state it will leave
    auto setSize = [&](double px) { radiusPx_ = clampDrawSize(float(px)); };
    auto setIntensity = [&](double percent) {
        intensityValue(brush_ == BrushType::Smooth && doc_.activeBrush < 0) = float(std::clamp(percent, 0.0, 100.0) / 100.0);
    };
    switch (e.role) {
    case cmd::Role::Brush: {
        if (e.id.rfind("@brush:", 0) == 0) runCommand("brush_use", {{"name", e.id.substr(7)}});
        else if (const Action* a = actions_.find(e.id); a && a->run) a->run();
        if (r.hasSize) setSize(r.size);
        if (r.hasIntensity) setIntensity(r.intensity);
        break;
    }
    case cmd::Role::Value:
        if (e.id == "@size") setSize(r.number);
        else setIntensity(r.number);
        break;
    case cmd::Role::Switch: {
        if (e.id == "@symmetry") {
            bool* flags[3] = {&symmetry_.x, &symmetry_.y, &symmetry_.z};
            if (r.axis) {
                bool& f = *flags[r.axis == 'x' ? 0 : r.axis == 'y' ? 1 : 2];
                f = r.state < 0 ? !f : r.state == 1;
            } else if (r.state == 0) {
                for (bool* f : flags) *f = false;
            } else {
                symmetry_.x = r.state < 0 ? !symmetry_.x : true;
            }
        } else if (const Action* a = actions_.find(e.id); a && a->run && (!a->enabled || a->enabled())) {
            const bool now = a->checked ? a->checked() : false;
            if (r.state < 0 || (r.state == 1) != now) a->run();
        }
        break;
    }
    case cmd::Role::Amount: {
        if (r.hasNumber) {
            if (r.unit == cmd::Unit::Triangles) {
                // The finest resolution that stays within the triangle count asked for (the estimate grows with it).
                const double area = surfaceArea(doc_.mesh);
                int lo = kVoxelResolutionMin, hi = kVoxelResolutionMax;
                while (lo < hi) {
                    const int mid = (lo + hi + 1) / 2;
                    if (double(estimateVoxelRemesh(doc_.mesh, mid, area).triangles) <= r.number) lo = mid;
                    else hi = mid - 1;
                }
                voxelResolution_ = lo;
            } else {
                voxelResolution_ = std::clamp(int(std::lround(r.number)), kVoxelResolutionMin, kVoxelResolutionMax);
            }
            voxelResolutionTyped_ = voxelResolution_;
        }
        if (const Action* a = actions_.find(e.id); a && a->run && (!a->enabled || a->enabled())) a->run();
        break;
    }
    case cmd::Role::Visibility: {
        std::set<size_t> named(r.parts.begin(), r.parts.end());
        for (size_t i = 0; i < doc_.parts.size(); ++i) {
            const bool listed = named.count(i) > 0;
            bool visible = doc_.parts[i].visible;
            if (e.id == "@isolate") visible = r.except ? !listed : listed;
            else if (r.only) visible = listed;
            else {
                const bool show = e.id == "@show";
                const bool affected = r.all ? (r.except ? !listed : true) : listed;
                if (affected) visible = show;
                else if (r.except && listed) visible = !show;
            }
            if (visible != doc_.parts[i].visible) runCommand("part_visibility", {{"index", double(i)}, {"visible", visible}});
        }
        break;
    }
    case cmd::Role::Plain:
        if (const Action* a = actions_.find(e.id); a && a->run) {
            if (a->enabled && !a->enabled()) {
                status_ = a->label + " isn't available right now";
                return status_;
            }
            a->run();
        }
        break;
    }
    commandHistory_.push(line);
    saveCommandHistory();
    if (const std::string id = keyActionId(e, r); !id.empty() && actions_.find(id)) actions_.touch(id); // Recently used
    const std::string keys = commandKeys(r);
    status_ = keys.empty() ? shown : shown + "  (" + keys + ")"; // the key teaches itself
    return status_;
}

void App::saveCommandHistory() {
    if (uiConfigDeterministic_ || configDir_.empty()) return;
    std::ofstream f(fsPath(configDir_ + "command_history.txt"), std::ios::binary | std::ios::trunc);
    f << commandHistory_.toText();
}

void App::loadCommandHistory() {
    if (configDir_.empty()) return;
    std::ifstream f(fsPath(configDir_ + "command_history.txt"), std::ios::binary);
    if (!f) return;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    commandHistory_.fromText(text);
}

} // namespace sf
