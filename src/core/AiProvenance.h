// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where an AI-made Part came from (areas/ai.md #3): the host that made it, the model, the local date, and the pass
// it is (as generated, the sculpting base rebuilt from it, or the high-fidelity reference kept beside the base). The
// tag rides in the Part, in project files (an optional per-Part key, older readers ignore it), follows copies and
// splits of the Part, shows in the Parts list tooltip and scene_overview, and goes out with exports whose format has
// a place for it (glTF node extras, USD customData, OBJ comments). Nothing in it leaves the machine on its own.
#pragma once

#include <string>

namespace sf {

class Json;

struct AiProvenance {
    std::string host;  // "tencent-hunyuan3d-2-1.hf.space", "queue.fal.run", "127.0.0.1:8188", "this computer"
    std::string model; // "Hunyuan3D 2.1 (Tencent)", "fal-ai/trellis-2", "Tripo v3.1"
    std::string date;  // local date the model was generated, YYYY-MM-DD
    std::string role;  // kGenerated, kSculptingBase or kReference
    bool empty() const { return host.empty() && model.empty() && date.empty(); }
    // "Hunyuan3D 2.1 (Tencent), tencent-hunyuan3d-2-1.hf.space, 2026-11-23 (sculpting base)"; "" when empty.
    std::string line() const;

    static constexpr const char* kGenerated = "generated";         // the model as the service sent it
    static constexpr const char* kSculptingBase = "sculpting base"; // rebuilt as an even mesh (Voxel / Quad Remesh)
    static constexpr const char* kReference = "reference";          // kept at full fidelity beside a sculpting base
};

// Today's local date as YYYY-MM-DD.
std::string aiProvenanceToday();

// Project / export JSON: {"host", "model", "date", "role"}; missing keys read as empty, unknown keys are ignored.
Json aiProvenanceToJson(const AiProvenance& p);
AiProvenance aiProvenanceFromJson(const Json& j);

} // namespace sf
