// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Tablet.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace sf {

namespace {

struct ActionName {
    PenAction action;
    const char* id;
    const char* label;
};
constexpr ActionName kActions[] = {
    {PenAction::None, "none", "Nothing"},
    {PenAction::Invert, "invert", "Invert (Alt)"},
    {PenAction::Smooth, "smooth", "Smooth (Shift)"},
    {PenAction::RotateView, "rotate", "Rotate view"},
    {PenAction::PanView, "pan", "Pan view"},
    {PenAction::RightClick, "right_click", "Right-click"},
    {PenAction::EraseMask, "erase_mask", "Erase mask"},
};

float clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; }

} // namespace

float PressureCurve::apply(float pressure) const {
    const float lo = clamp01(threshold);
    const float hi = std::max(clamp01(fullAt), lo + 1e-3f);
    const float t = std::clamp((clamp01(pressure) - lo) / (hi - lo), 0.0f, 1.0f);
    const float g = std::isfinite(gamma) ? std::clamp(gamma, 0.25f, 4.0f) : 1.0f;
    const float a = clamp01(minOut), b = clamp01(maxOut);
    return a + (b - a) * std::pow(t, g);
}

bool PressureCurve::isLinear() const {
    return std::abs(gamma - 1.0f) < 1e-3f && minOut <= 0.0f && maxOut >= 1.0f && threshold <= 0.0f && fullAt >= 1.0f;
}

const char* penActionId(PenAction a) {
    for (const auto& n : kActions)
        if (n.action == a) return n.id;
    return "none";
}

const char* penActionLabel(PenAction a) {
    for (const auto& n : kActions)
        if (n.action == a) return n.label;
    return "Nothing";
}

PenAction penActionFromId(const std::string& id, PenAction fallback) {
    for (const auto& n : kActions)
        if (id == n.id) return n.action;
    return fallback;
}

Json toJson(const TabletSettings& t) {
    Json curve = Json::object();
    curve.set("gamma", double(t.curve.gamma));
    curve.set("min", double(t.curve.minOut));
    curve.set("max", double(t.curve.maxOut));
    curve.set("threshold", double(t.curve.threshold));
    curve.set("fullAt", double(t.curve.fullAt));
    Json j = Json::object();
    j.set("pressureCurve", curve);
    j.set("eraser", std::string(penActionId(t.eraser)));
    Json buttons = Json::array();
    for (PenAction a : t.buttons) buttons.push(std::string(penActionId(a)));
    j.set("buttons", buttons);
    j.set("mouseMode", t.mouseMode);
    j.set("viewSpeed", double(t.viewSpeed));
    j.set("classicPenTiming", t.classicTiming);
    return j;
}

void fromJson(const Json& j, TabletSettings& t) {
    if (!j.isObject()) return;
    if (const Json& c = j["pressureCurve"]; c.isObject()) {
        t.curve.gamma = std::clamp(float(c["gamma"].asNumber(t.curve.gamma)), 0.25f, 4.0f);
        t.curve.minOut = clamp01(float(c["min"].asNumber(t.curve.minOut)));
        t.curve.maxOut = clamp01(float(c["max"].asNumber(t.curve.maxOut)));
        t.curve.threshold = std::clamp(float(c["threshold"].asNumber(t.curve.threshold)), 0.0f, 0.9f);
        t.curve.fullAt = std::clamp(float(c["fullAt"].asNumber(t.curve.fullAt)), t.curve.threshold + 0.05f, 1.0f);
    }
    if (j["eraser"].isString()) {
        const PenAction a = penActionFromId(j["eraser"].asString(), t.eraser);
        // The eraser can't take a button-only action (a view drag while drawing makes no sense).
        if (a == PenAction::None || a == PenAction::Invert || a == PenAction::Smooth || a == PenAction::EraseMask) t.eraser = a;
    }
    if (const Json& b = j["buttons"]; b.isArray())
        for (size_t i = 0; i < t.buttons.size() && i < b.size(); ++i)
            if (b[i].isString()) {
                const PenAction a = penActionFromId(b[i].asString(), t.buttons[i]);
                if (a != PenAction::EraseMask) t.buttons[i] = a;
            }
    t.mouseMode = j["mouseMode"].asBool(t.mouseMode);
    t.viewSpeed = std::clamp(float(j["viewSpeed"].asNumber(t.viewSpeed)), 0.1f, 2.0f);
    t.classicTiming = j["classicPenTiming"].asBool(t.classicTiming);
}

bool PenPressureTracker::sample(float pressure) {
    if (!std::isfinite(pressure) || (holdLostZero && down_ && sinceDown_ && raw_ > kContact && pressure <= 0.0f))
        return false;
    raw_ = clamp01(pressure);
    sinceDown_ = true;
    everSampled_ = true;
    if (raw_ > kContact) pressed_ = true;
    return true;
}

void PenDiagnostics::sampled(float p, bool touching) {
    lastPressure = p;
    if (!touching) return;
    strokeMin = strokeMin < 0.0f ? p : std::min(strokeMin, p);
    strokeMax = std::max(strokeMax, p);
}

std::string penReport(const std::vector<std::pair<std::string, std::string>>& facts, const PenDiagnostics& d) {
    std::string s;
    for (const auto& [name, value] : facts) s += name + ": " + value + "\n";
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "Pen events: proximity %llu, down %llu, up %llu, motion %llu, pressure %llu, other axes %llu, "
                  "buttons %llu\n"
                  "Lost pressure samples: %llu\nSystem mouse events while the pen was in range: %llu\n"
                  "Touch events from the pen: %llu\nPressure after motion: %s\n",
                  (unsigned long long)d.proximity, (unsigned long long)d.down, (unsigned long long)d.up,
                  (unsigned long long)d.motion, (unsigned long long)d.pressure, (unsigned long long)d.otherAxes,
                  (unsigned long long)d.buttons, (unsigned long long)d.lostSamples, (unsigned long long)d.strayMouse,
                  (unsigned long long)d.penTouchEvents, d.axesAfterMotion ? "yes" : "not seen");
    s += buf;
    if (d.lastPressure < 0.0f) s += "Last pressure: none yet\n";
    else {
        std::snprintf(buf, sizeof buf, "Last pressure: %.3f\n", double(d.lastPressure));
        s += buf;
    }
    if (d.strokeMax >= 0.0f) {
        std::snprintf(buf, sizeof buf, "Last stroke's pressure: %.3f to %.3f\n", double(d.strokeMin), double(d.strokeMax));
        s += buf;
    }
    return s;
}

void PenPressureTracker::motion() {
    if (down_ && !pressed_ && silentMotions_ < kSilentMotions) ++silentMotions_;
}

bool looksLikeTablet(const std::string& name) {
    std::string s = name;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    for (const char* what : {"wacom", "intuos", "cintiq", "bamboo", "huion", "kamvas", "xp-pen", "xppen", "gaomon",
                             "ugee", "veikk", "tablet", "stylus", "digitizer", " pen"})
        if (s.find(what) != std::string::npos) return true;
    return false;
}

TabletHotplug::Note TabletHotplug::mouseAdded(uint32_t id, const std::string& name) {
    if (looksLikeTablet(name)) {
        if (std::find(tabletMice_.begin(), tabletMice_.end(), id) == tabletMice_.end()) tabletMice_.push_back(id);
        pens_.clear(); // its pens are new, even with an id seen before
        candidate_ = 0;
        announced_ = true;
        return Note::Connected;
    }
    candidate_ = id;
    announced_ = false;
    return Note::None;
}

TabletHotplug::Note TabletHotplug::mouseRemoved(uint32_t id) {
    if (id == candidate_) candidate_ = 0;
    const auto it = std::find(tabletMice_.begin(), tabletMice_.end(), id);
    if (it == tabletMice_.end()) return Note::None;
    tabletMice_.erase(it);
    pens_.clear();
    announced_ = false;
    return Note::Disconnected;
}

TabletHotplug::Note TabletHotplug::penIn(uint32_t penId) {
    if (std::find(pens_.begin(), pens_.end(), penId) != pens_.end()) return Note::None;
    pens_.push_back(penId);
    if (candidate_ != 0) { // the generic device added just before was this tablet
        tabletMice_.push_back(candidate_);
        candidate_ = 0;
        if (!announced_) {
            announced_ = true;
            return Note::Connected;
        }
    }
    return Note::NewPen;
}

void PenCursor::cursorAt(float x, float y) {
    cursorX_ = x;
    cursorY_ = y;
    haveCursor_ = true;
    anchored_ = false;
}

void PenCursor::map(float& x, float& y, uint64_t nowNs) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    if (mouseMode) {
        // A pen that comes back (into range, or after a pause) moves the cursor on from where it is.
        if (!anchored_ || nowNs - lastNs_ > kLiftedNs) {
            offsetX_ = haveCursor_ ? cursorX_ - x : 0.0f;
            offsetY_ = haveCursor_ ? cursorY_ - y : 0.0f;
            anchored_ = true;
        }
        lastNs_ = nowNs;
        x += offsetX_;
        y += offsetY_;
    }
    cursorX_ = x;
    cursorY_ = y;
    haveCursor_ = true;
}

} // namespace sf
