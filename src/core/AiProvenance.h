// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where an AI-made Part came from (areas/ai.md #3): the public provider and model that made it, the local date, the
// SculptForge version, and the pass it is (as generated, the sculpting base rebuilt from it, or the high-fidelity
// reference kept beside the base). The tag rides in the Part, in project files (an optional per-Part key, older readers
// ignore it), follows copies and splits of the Part, shows in the Parts list tooltip and scene_overview, and goes out
// with exports whose format has a place for it (glTF node extras, USD customData, OBJ comments). Those files get posted
// and sold, so the tag NEVER holds a host, URL, path, workflow or key name (input 11-24): a user's own server is just
// its provider's name. Nothing in it leaves the machine on its own.
#pragma once

#include <string>

namespace sf {

class Json;

struct AiProvenance {
    std::string provider; // "Hugging Face", "fal.ai", "Meshy", "Hyper3D", "Tripo", "ComfyUI", "Your generator command"
    std::string model;    // "Hunyuan3D 2.1 (Tencent)", "fal-ai/trellis-2", "Tripo v3.1"; "" when the provider doesn't say
    std::string date;     // local date the model was generated, YYYY-MM-DD
    std::string app;      // the SculptForge version that made it, "0.14.0"
    std::string role;     // kGenerated, kSculptingBase or kReference
    bool empty() const { return provider.empty() && model.empty() && date.empty(); }
    // "Hunyuan3D 2.1 (Tencent), Hugging Face, 2026-11-23, SculptForge 0.14.0 (sculpting base)"; "" when empty.
    std::string line() const;

    static constexpr const char* kGenerated = "generated";         // the model as the service sent it
    static constexpr const char* kSculptingBase = "sculpting base"; // rebuilt as an even mesh (Voxel / Quad Remesh)
    static constexpr const char* kReference = "reference";          // kept at full fidelity beside a sculpting base
};

// Today's local date as YYYY-MM-DD.
std::string aiProvenanceToday();

// Project / export JSON: {"provider", "model", "date", "app", "role"}; missing keys read as empty, unknown keys are ignored.
Json aiProvenanceToJson(const AiProvenance& p);
AiProvenance aiProvenanceFromJson(const Json& j);

} // namespace sf
