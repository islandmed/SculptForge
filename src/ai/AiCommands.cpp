// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/AiCommands.h"

#include "ai/BrushGalleryClient.h"
#include "ai/ComfyUI.h"
#include "ai/ImageTo3DService.h"
#include "core/EnginePresets.h"
#include "core/Texture.h"
#include "core/Voxel.h"
#include "link/UnrealLink.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>

namespace sf::ai {

namespace {

std::optional<uint64_t> comfySeed(const Args& a) {
    if (!a.contains("seed")) return std::nullopt;
    return uint64_t(std::max(0.0, argNumber(a, "seed", 0.0)));
}

} // namespace

void registerAiCommands(CommandRegistry& registry, std::function<AiSettings()> settings) {
    using T = ParamSpec::Type;
    registerBrushGalleryCommands(registry); // brush_gallery_search, brush_gallery_install (BrushGalleryClient.cpp)
    registry.add({"brush_ai",
                  "Design a brush from a description. Free by default (a hosted open model, no key); or "
                  "provider=anthropic (ANTHROPIC_API_KEY), or provider=openai-compatible endpoint=... model=... "
                  "for OpenAI, Ollama, LM Studio etc.",
                  {{"prompt", T::String, "What the brush should do, in an artist's words.", true},
                   {"provider", T::String, "'free', 'anthropic' or 'openai-compatible' (default: your saved settings).", false},
                   {"endpoint", T::String, "Override the API endpoint URL.", false},
                   {"model", T::String, "Override the model name.", false}},
                  [settings](Document& doc, const Args& a) -> CommandResult {
                      AiSettings s = settings();
                      if (a.contains("provider"))
                          s = AiSettings::defaults(AiSettings::providerFromName(argString(a, "provider")));
                      if (a.contains("endpoint")) s.endpoint = argString(a, "endpoint");
                      if (a.contains("model")) s.model = argString(a, "model");
                      std::vector<ChatMessage> history = {composeUserMessage(argString(a, "prompt"), nullptr)};
                      BrushSuggestion r = requestBrush(s, history, nullptr);
                      if (!r.ok) return {false, r.error};
                      doc.activeBrush = doc.addBrush(r.recipe);
                      return {true, "'" + r.recipe.name + "': " + r.reply};
                  }});

    registry.add({"image_to_3d",
                  "Reconstruct a full 3D model from one picture (character, creature, prop) and make it sculptable. "
                  "Free by default via Hugging Face Spaces (Hunyuan3D, TRELLIS.2, Stable Fast 3D; HF_TOKEN raises the "
                  "daily quota), paid via fal.ai (FAL_KEY) / Meshy (MESHY_API_KEY), or any workflow on your own "
                  "ComfyUI (service=comfyui workflow=FILE). Replaces the scene. A transparent background works best.",
                  {{"path", T::String, "Image file (png, jpg...).", true},
                   {"service", T::String, "'huggingface' (free, default), 'fal', 'meshy', 'comfyui' (your ComfyUI server), 'command' (your generator) or 'inflate' (offline blockout).", false},
                   {"model", T::String, "huggingface: hunyuan3d-2.1, trellis-2, hunyuan3d-2-turbo, hunyuan3d-2, stable-fast-3d; fal: a model id such as fal-ai/trellis-2.", false},
                   {"workflow", T::String, "comfyui: API-format workflow file (default: your saved one, else the bundled one).", false},
                   {"url", T::String, "comfyui: server address. Default http://127.0.0.1:8188 or your saved one.", false},
                   {"seed", T::Number, "comfyui: seed for the workflow's sf_seed / seed inputs (default: random).", false},
                   {"remesh", T::Bool, "Voxel-remesh the result into a clean watertight sculpting surface. Default true.", false},
                   {"resolution", T::Number, "Remesh voxels across the model (64-512). Default 256.", false}},
                  [settings](Document& doc, const Args& a) -> CommandResult {
                      AiSettings s = settings();
                      if (a.contains("service")) s.generatorService = argString(a, "service");
                      if (a.contains("url")) s.comfyUrl = argString(a, "url");
                      if (a.contains("model")) {
                          if (s.generatorService == "meshy") s.meshyModel = argString(a, "model");
                          else if (s.generatorService == "fal") s.falModel = argString(a, "model");
                          else s.hfModel = argString(a, "model");
                      }
                      const auto stem = std::filesystem::temp_directory_path() /
                                        ("sculptforge_generated_" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                      const auto stemU8 = stem.u8string();
                      const std::string out(stemU8.begin(), stemU8.end());
                      GeneratorResult r = s.generatorService == "comfyui"
                                              ? comfy::run(s, argString(a, "workflow"), argString(a, "path"), {}, out, nullptr, comfySeed(a))
                                              : generate3D(s, argString(a, "path"), out);
                      if (!r.ok) return {false, r.error};
                      CommandResult opened = CommandRegistry().execute(doc, "open", {{"path", r.outputPath}});
                      std::error_code ec;
                      std::filesystem::remove(std::filesystem::path(std::u8string(r.outputPath.begin(), r.outputPath.end())), ec);
                      if (!opened.ok) return opened;
                      doc.path.clear();
                      const size_t raw = doc.mesh.triangleCount();
                      if (argBool(a, "remesh", true)) {
                          std::string err;
                          Mesh m = voxelRemesh(doc.mesh, std::clamp(int(argNumber(a, "resolution", 256)), 64, 512), &err);
                          if (m.triangleCount() == 0) return {false, "Generated, but remeshing failed: " + err};
                          doc.setMesh(std::move(m));
                      }
                      return {true, "Generated a 3D model (" + std::to_string(raw) + " triangles" +
                                        (raw != doc.mesh.triangleCount()
                                             ? ", remeshed to " + std::to_string(doc.mesh.triangleCount())
                                             : std::string()) +
                                        ") - ready to sculpt"};
                  }});

    registry.add({"text_to_3d",
                  "Create a 3D model from a description and make it sculptable. Free by default via Hugging Face "
                  "(Hunyuan3D 2 / 2 mini Turbo; HF_TOKEN raises the daily quota), or paid via fal.ai (FAL_KEY) / "
                  "Meshy (MESHY_API_KEY), or a workflow on your own ComfyUI (service=comfyui workflow=FILE). Replaces the scene.",
                  {{"prompt", T::String, "One object, e.g. 'a stylised dragon head with curled horns'.", true},
                   {"service", T::String, "'huggingface' (free, default), 'fal', 'meshy' or 'comfyui' (your ComfyUI server).", false},
                   {"model", T::String, "huggingface: hunyuan3d-2-turbo or hunyuan3d-2.", false},
                   {"workflow", T::String, "comfyui: API-format workflow file (default: your saved one, else the bundled one).", false},
                   {"url", T::String, "comfyui: server address. Default http://127.0.0.1:8188 or your saved one.", false},
                   {"seed", T::Number, "comfyui: seed for the workflow's sf_seed / seed inputs (default: random).", false},
                   {"remesh", T::Bool, "Voxel-remesh the result into a clean watertight sculpting surface. Default true.", false},
                   {"resolution", T::Number, "Remesh voxels across the model (64-512). Default 256.", false}},
                  [settings](Document& doc, const Args& a) -> CommandResult {
                      AiSettings s = settings();
                      if (a.contains("service")) s.generatorService = argString(a, "service");
                      if (a.contains("url")) s.comfyUrl = argString(a, "url");
                      if (s.generatorService == "command" || s.generatorService == "inflate") s.generatorService = "huggingface";
                      if (a.contains("model")) {
                          if (s.generatorService == "meshy") s.meshyModel = argString(a, "model");
                          else if (s.generatorService != "fal") s.hfModel = argString(a, "model");
                      }
                      const auto stem = std::filesystem::temp_directory_path() /
                                        ("sculptforge_text_" +
                                         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
                      const auto stemU8 = stem.u8string();
                      const std::string out(stemU8.begin(), stemU8.end());
                      GeneratorResult r;
                      if (s.generatorService == "comfyui") {
                          if (argString(a, "prompt").find_first_not_of(" \t\r\n") == std::string::npos) return {false, "Describe the model you want"};
                          r = comfy::run(s, argString(a, "workflow"), {}, argString(a, "prompt"), out, nullptr, comfySeed(a));
                      } else {
                          r = generate3DFromText(s, argString(a, "prompt"), out);
                      }
                      if (!r.ok) return {false, r.error};
                      CommandResult opened = CommandRegistry().execute(doc, "open", {{"path", r.outputPath}});
                      std::error_code ec;
                      std::filesystem::remove(std::filesystem::path(std::u8string(r.outputPath.begin(), r.outputPath.end())), ec);
                      if (!opened.ok) return opened;
                      doc.path.clear();
                      if (argBool(a, "remesh", true)) {
                          std::string err;
                          Mesh m = voxelRemesh(doc.mesh, std::clamp(int(argNumber(a, "resolution", 256)), 64, 512), &err);
                          if (m.triangleCount() == 0) return {false, "Generated, but remeshing failed: " + err};
                          doc.setMesh(std::move(m));
                      }
                      return {true, "Generated a 3D model from text (" + std::to_string(doc.mesh.triangleCount()) +
                                        " triangles) - ready to sculpt"};
                  }});

    registry.add({"comfyui_check",
                  "Check that a ComfyUI server answers (for service=comfyui) and show its version and GPU.",
                  {{"url", T::String, "Server address. Default http://127.0.0.1:8188 or your saved one.", false}},
                  [settings](Document&, const Args& a) -> CommandResult {
                      comfy::ServerInfo info;
                      std::string err;
                      const std::string url = comfy::normalizeUrl(argString(a, "url", settings().comfyUrl));
                      if (!comfy::checkServer(url, info, err)) return {false, err};
                      return {true, info.summary() + " at " + url};
                  }});

    registry.add({"unreal_editors", "List running Unreal Editors that SculptForge can send assets to (remote execution).",
                  {{"network", T::Bool, "Also look on other computers (Unreal needs Multicast TTL 1 and bind address 0.0.0.0).", false}},
                  [](Document&, const Args& a) -> CommandResult {
                      std::string err;
                      auto editors = link::findUnrealEditors(argBool(a, "network", false) ? link::UnrealEndpoint::network()
                                                                                         : link::UnrealEndpoint(),
                                                             1500, err);
                      if (editors.empty())
                          return {false, err.empty() ? "No Unreal Editor found. Enable the Python Editor Script Plugin and "
                                                       "Project Settings > Python > Enable Remote Execution."
                                                     : err};
                      std::string msg;
                      for (const auto& e : editors)
                          msg += (msg.empty() ? "" : "; ") + e.projectName + " (UE " + e.engineVersion + ", " + e.machine +
                                 (e.local() ? "" : " at " + e.address) + ")";
                      return {true, msg};
                  }});

    registry.add({"unreal_send",
                  "Send the sculpt to the running Unreal Editor: exports an Unreal-ready FBX and imports or updates the "
                  "asset in the project (static mesh with LODs, or skeletal mesh with morph targets).",
                  {{"destination", T::String, "Content folder in the project. Default /Game/SculptForge.", false},
                   {"name", T::String, "Asset name (default SM_<document> or SK_<document>).", false},
                   {"height_cm", T::Number, "Model height in Unreal, in cm. Default: 1 unit = 1 m.", false},
                   {"lods", T::Number, "LOD levels for static meshes (1-8). Default 4.", false},
                   {"project", T::String, "Pick the editor with this project name when several are open.", false},
                   {"network", T::Bool, "Unreal runs on another computer (Unreal needs Multicast TTL 1 and bind address 0.0.0.0).", false}},
                  [](Document& doc, const Args& a) -> CommandResult {
                      UnrealExportOptions o;
                      o.folder = (std::filesystem::temp_directory_path() / "sculptforge_unreal").string();
                      o.assetName = argString(a, "name");
                      o.heightCm = float(argNumber(a, "height_cm", 0.0));
                      o.lods = int(argNumber(a, "lods", 4));
                      UnrealExportResult r;
                      std::string err;
                      if (!exportForUnreal(doc, o, r, err)) return {false, err};
                      link::UnrealImport import;
                      import.fbxPath = r.path;
                      import.destination = argString(a, "destination", "/Game/SculptForge");
                      import.assetName = r.assetName;
                      import.skeletal = r.skeletal;
                      import.lods = r.lodCount > 1;
                      if (hasTexture(doc.mesh) && !link::addUnrealMaterialMaps(*doc.mesh.texture, o.folder, import, err))
                          return {false, err};
                      std::string message;
                      const auto endpoint = argBool(a, "network", false) ? link::UnrealEndpoint::network() : link::UnrealEndpoint();
                      const bool ok = link::sendToUnreal(endpoint, import, message, argString(a, "project"));
                      return {ok, message};
                  }});
}

} // namespace sf::ai
