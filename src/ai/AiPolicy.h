// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The AI switch (Preferences > AI > Enable AI features). Off: the AI commands
// (brush designer, image / text to 3D, ComfyUI) are not registered, so no menu,
// shelf, command palette, script or MCP tool reaches them, and every AI network
// entry point refuses before it sends anything. The rest (community brushes,
// update check, bridges, the MCP server itself) is not AI and stays.
// Studios: SF_AI=off in the environment (or --no-ai) wins over the saved choice.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sf::ai {

inline constexpr const char* kPrivacyLine =
    "Your art is never uploaded or used for training. AI features run only when you use them and can be turned off.";

// SF_AI: "off" / "0" / "false" / "no" = Off, "on" / "1" / "true" / "yes" = On, anything else = None.
enum class AiOverride { None, Off, On };
AiOverride aiEnvOverride();
void setAiForcedOff(bool off); // --no-ai: as SF_AI=off for this process
// True when the environment or --no-ai decides, so the saved choice can't change it.
bool aiPolicyLocked();
// The effective switch: the environment (or --no-ai) wins over the saved preference.
bool aiFeaturesAllowed(bool saved);

// The process-wide gate every AI entry point checks (default on).
void setAiEnabled(bool on);
bool aiEnabled();
// The error AI entry points return while it is off.
std::string aiOffError();

// The commands that exist only while AI is on.
const std::vector<std::string>& aiCommandNames();
bool isAiCommand(std::string_view name);

} // namespace sf::ai
