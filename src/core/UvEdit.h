// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One undoable UV edit on the active Part (the uv_* commands, core/UvCommands.cpp, and the UV Editor's drags,
// app/PanelUvEditor.cpp): construct it before changing Document::mesh's UVs or seams, commit() after. The step
// holds the old UVs and flags (UvUndo) and, with keepTexture on a painted Part, moves the texture onto the new
// layout so the model looks the same (core/TextureTransfer.h), the old texture in the same step.
#pragma once

#include "core/Document.h"

#include <string>

namespace sf {

class UvEditStep {
public:
    UvEditStep(Document& doc, std::string label, bool keepTexture);
    UvEditStep(const UvEditStep&) = delete;
    UvEditStep& operator=(const UvEditStep&) = delete;
    // The UVs as they were when the step began.
    const std::vector<glm::vec2>& before() const { return record_.uv->uvs; }
    // Pushes the step if anything changed; returns the texture note ("; paint moved to the new UVs ...").
    std::string commit();

private:
    Document& doc_;
    bool texture_;
    UndoRecord record_;
};

} // namespace sf
