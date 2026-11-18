// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The core functions the Python API (Bindings.cpp) calls, as a table. The module for pipelines fills it from the
// core it links; in the app the app hands the host its own (PyHost.h, AppSide::core), so the host links no core of
// its own: one core per process, one set of its globals, and a small host library. The host is linked with no
// undefined symbols allowed, so a core call that bypasses this table fails the build.
//
// None of these throw: a failure comes back as false, an error message or a failed CommandResult.
#pragma once

#include "core/Commands.h"

#include <memory>
#include <string>
#include <vector>

namespace sf {

struct Document;
class Mesh;
struct ScriptStatement;
struct SelectionSet;
struct SelectionSetData;
struct SelectionSetList;

namespace python {

struct CoreCalls {
    // A new document of a script's own, with the user's brush library (its warnings in `warnings`).
    std::unique_ptr<Document> (*newDocument)(std::vector<std::string>& warnings);
    // A command on a document of a script's own (not the app's open scene): the core commands and the AI ones
    // Preferences > AI allows.
    CommandResult (*execute)(Document&, const std::string& name, const Args& args);
    const CommandRegistry& (*registry)(); // those commands
    std::string (*toolSchemaJson)(const CommandRegistry&);
    void (*setMesh)(Document&, Mesh&&);  // clears undo
    void (*finishMesh)(Mesh&);           // topology and normals for new positions and triangles
    void (*markAllDirty)(Mesh&);
    int (*findPart)(const Document&, const std::string& name);
    void (*selectPart)(Document&, int);
    bool (*parseScript)(const std::string& text, std::vector<ScriptStatement>& out, std::string& err);
    bool (*parseSetChannels)(const std::string& text, uint32_t& bits, std::string& err);
    int (*findSelectionSet)(const SelectionSetList&, const std::string& name);
    bool (*selectionSetFits)(const Mesh&, const SelectionSet&);
    bool (*selectionSetAt)(const Mesh&, const SelectionSet&, uint32_t channels, SelectionSetData& out, std::string& err);
    bool (*makeSelectionSet)(const std::string& name, const SelectionSetData&, uint32_t vertexCount, uint32_t triangleCount,
                             int level, SelectionSet& out, std::string& msg);
    int (*subdivCurrentLevel)(const Mesh&);
    bool (*addSelectionSet)(Document&, SelectionSet&&, const std::string& label, std::string& msg);
    int (*subdivLevelCount)(const Mesh&); // 0 without levels
};

// The table over the core this binary links (the app's, the module's, the tests').
const CoreCalls& directCoreCalls();

} // namespace python
} // namespace sf
