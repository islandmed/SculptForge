// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands that call out to AI services (brush design, image-to-3D). They
// live in the ai library so the core stays free of networking; the CLI, the
// MCP server and the app register them on top of the built-in commands.
#pragma once

#include "ai/BrushAssistant.h"
#include "ai/ImageTo3DService.h"
#include "core/Commands.h"

#include <functional>

namespace sf::ai {

// `settings` is called on every run so the latest saved settings are used. The AI ones (ai/AiPolicy.h
// aiCommandNames) only while aiEnabled(); the rest (community brushes, Unreal) always.
void registerAiCommands(CommandRegistry& registry, std::function<AiSettings()> settings);
// The AI switch at run time (Preferences > AI): adds or removes just the AI commands.
void addAiFeatureCommands(CommandRegistry& registry, std::function<AiSettings()> settings);
void removeAiFeatureCommands(CommandRegistry& registry);

// image_to_3d / text_to_3d in two parts, so the app's window never waits on the network: `work` (the service call, minutes
// at worst) runs on a worker with no Document; `finish` (open, drop the backdrop, remesh) runs on the main thread after.
// Run in a row, they are the command. False with `refused` set for arguments that can't run (nothing started).
struct GenerateJob {
    std::function<GeneratorResult(GenerationProgress*)> work;
    std::function<CommandResult(Document&, GeneratorResult&)> finish;
};
bool prepareGenerateJob(const std::string& name, const Args& args, const AiSettings& settings, GenerateJob& job,
                        CommandResult& refused);
// The audit line (core/AuditLog.h) for a run the app finished itself.
void recordGenerateRun(const std::string& name, const Args& args, const CommandResult& result, int64_t ms);

} // namespace sf::ai
