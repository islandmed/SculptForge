// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Python API (`import sculptforge`, Bindings.cpp), shared by the module for pipelines (module.cpp) and by Python
// scripting in the app (AppHost.cpp). What differs between the two is how a document is changed, so that goes
// through an Api: the module runs commands itself; in the app, commands and edits on the open scene go through
// the app (they echo in the console, refresh the interface and land in its undo history).
#pragma once

#include "core/Commands.h"
#include "python/PyHost.h"

#include <pybind11/pybind11.h>

#include <functional>
#include <memory>
#include <string>

namespace sf {

namespace python {

struct Api {
    std::function<CommandResult(Document&, const std::string&, const Args&)> execute;
    std::function<const CommandRegistry&()> registry; // commands(), tool_schema()
    // Runs `change` on `doc` as one edit (the app records it for undo first); null: just runs it.
    std::function<void(Document&, Edit, const std::function<void()>& change)> edit;
    // Replaces the active Part's mesh (set_mesh); null: Document::setMesh (clears undo).
    std::function<void(Document&, Mesh&&)> replaceMesh;
    // Stores a selection set (set_selection_set); false with `msg` on failure. Null: addSelectionSet.
    std::function<bool(Document&, SelectionSet&&, const std::string& label, std::string& msg)> addSelectionSet;
    // Selects a Part (active_part = ...); null: Document::selectPart.
    std::function<void(Document&, int)> selectPart;
    // The open scene in the app (sf.document(), and the default for sf.run("command")); null in the module.
    Document* appDocument = nullptr;
    bool releaseGil = true; // the app runs commands on its main thread, holding the GIL
};

// Adds the API to `m`. `description` is the module's docstring.
void bind(pybind11::module_& m, std::shared_ptr<const Api> api, const char* description);

// The module's own command registry (with the AI commands Preferences > AI allows).
const CommandRegistry& standaloneRegistry();

} // namespace python
} // namespace sf
