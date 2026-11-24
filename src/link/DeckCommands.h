// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forge Deck (prototype, docs/phone-link.md): what a paired phone may ask the desktop to do. Every message is ONE
// command from a short allowlist; anything else is refused (and logged by the caller). The phone is an untrusted
// remote: it never names a command to run, only picks from this list, and every value is range-checked here.
//
//   {"cmd":"brush_use","name":"Clay"}        make a brush active (a name from the Deck's own tiles)
//   {"cmd":"undo"}  {"cmd":"redo"}
//   {"cmd":"size_set","value":0..1}          brush size, as a fraction of the size range
//   {"cmd":"intensity_set","value":0..1}
//   {"cmd":"symmetry_toggle","axis":"x"}     mirror across X (y and z too)
#pragma once

#include <chrono>
#include <string>

namespace sf::link {

enum class DeckCommand { BrushUse, Undo, Redo, SizeSet, IntensitySet, SymmetryToggle };

// The wire name of a command ("brush_use", ...).
const char* deckCommandName(DeckCommand command);

struct DeckAction {
    DeckCommand command = DeckCommand::Undo;
    std::string name;  // brush_use: the brush
    float value = 0.0f; // size_set, intensity_set: 0..1
    char axis = 'x';    // symmetry_toggle: 'x', 'y' or 'z'
};

// The brush size range the Size strip covers (screen pixels, log scale: the strip's 0..1 is the position on it).
// The desktop publishes the range in the Deck's state, so the phone shows the same pixels as the desktop.
inline constexpr float kDeckMinBrushPx = 2.0f, kDeckMaxBrushPx = 800.0f;
float deckSizeToPx(float position);  // 0..1 -> px
float deckPxToSize(float px);        // px -> 0..1 (clamped)

// Commands waiting for the desktop's main thread (it may be busy for a while): more are refused, not queued.
inline constexpr size_t kMaxQueuedDeckActions = 64;
inline constexpr size_t kMaxDeckMessageBytes = 512;
inline constexpr size_t kMaxDeckBrushNameBytes = 48;

// Parses one phone message. False with `why` (plain words, safe to log: the command name is cut and printable)
// when it isn't exactly one allowlisted command with in-range arguments. Unknown fields are refused too.
bool parseDeckMessage(const std::string& text, DeckAction& action, std::string& why);

// A token bucket: `perSecond` commands a second, bursts up to `burst`. Time is passed in so tests don't wait.
class DeckRateLimit {
public:
    using Clock = std::chrono::steady_clock;
    explicit DeckRateLimit(double perSecond = 20.0, double burst = 20.0) : rate_(perSecond), burst_(burst), tokens_(burst) {}
    // True when a command may run now (and uses a token).
    bool allow(Clock::time_point now = Clock::now());

private:
    double rate_, burst_, tokens_;
    Clock::time_point last_{};
    bool started_ = false;
};

} // namespace sf::link
