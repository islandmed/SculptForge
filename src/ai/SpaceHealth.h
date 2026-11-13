// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the free Hugging Face Spaces behind Generate 3D are doing, so a dead one is greyed out with a reason instead of
// hanging the artist. The facts come from the update feed (latest.json's "ai3d" block, core/UpdateCheck.h), which the
// app already fetches once a day: no new network call, nothing about the user is sent. A script on our side checks
// the Spaces (scripts/ai3d-health.py) and writes the block. No block, or a Space it doesn't name: Up (the usual
// failure handling still applies at run time).
#pragma once

#include "core/UpdateCheck.h"

#include <string>
#include <string_view>
#include <vector>

namespace sf::ai::health {

enum class State { Up, Slow, Off };
struct Entry {
    State state = State::Up;
    std::string note; // why, in a sentence ("Switched off by its author"); may be empty
};

// Replaces what is known (the feed's list; an empty list = nothing known). Thread-safe.
void set(const std::vector<SpaceHealthFeed>& feed);
// The state of a free model (a Hugging Face preset id such as "trellis-2"); Up when unknown. Thread-safe.
Entry of(std::string_view presetId);
// "Switched off" / "May be slow to start" (the interface translates these); "" when Up.
const char* label(State state);
// The words next to a model that isn't Up ("switched off: <note>", "may be slow to start: <note>"); "" when Up.
std::string reason(const Entry& entry);

} // namespace sf::ai::health
