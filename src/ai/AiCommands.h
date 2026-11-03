// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands that call out to AI services (brush design, image-to-3D). They
// live in the ai library so the core stays free of networking; the CLI, the
// MCP server and the app register them on top of the built-in commands.
#pragma once

#include "ai/BrushAssistant.h"
#include "core/Commands.h"

#include <functional>

namespace sf::ai {

// `settings` is called on every run so the latest saved settings are used. The AI ones (ai/AiPolicy.h
// aiCommandNames) only while aiEnabled(); the rest (community brushes, Unreal) always.
void registerAiCommands(CommandRegistry& registry, std::function<AiSettings()> settings);
// The AI switch at run time (Preferences > AI): adds or removes just the AI commands.
void addAiFeatureCommands(CommandRegistry& registry, std::function<AiSettings()> settings);
void removeAiFeatureCommands(CommandRegistry& registry);

} // namespace sf::ai
