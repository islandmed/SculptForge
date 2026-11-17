// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Python scripting in the app: the line between the app and its Python host. The host (AppHost.cpp, the
// sculptforge_pyhost library with CPython in it) is loaded only when a script first runs (app/Python.cpp), so an
// app that never runs Python never loads Python: nothing at start-up, nothing per frame.
//
// The app hands the host its open scene and how to change it; the host runs Python on the main thread, inside the
// frame, and gives back what it printed. Both halves are built from this source tree by the same compiler, so C++
// types cross the line; the ABI number guards against a host from another build.
#pragma once

#include "core/Commands.h"

#include <functional>
#include <string>
#include <vector>

namespace sf {

struct Document;
class Mesh;
struct SelectionSet;

namespace python {

constexpr int kHostAbi = 1;

// A direct change to a document's data from Python (doc.mask = ...).
enum class Edit { Mask, Colors, FaceGroups, Hidden };

struct AppSide {
    // The build it belongs to: the host refuses another's (SCULPTFORGE_VERSION and the layout of what crosses).
    std::string build;
    size_t documentSize = 0, meshSize = 0;
    Document* document = nullptr; // the open scene
    const CommandRegistry* registry = nullptr;
    // A command on the open scene, run the way the app runs one (it echoes, refreshes and lands in undo).
    std::function<CommandResult(const std::string& name, const Args& args)> run;
    std::function<void(Document&, Edit, const std::function<void()>& change)> edit; // records undo, then changes
    std::function<void(Document&, Mesh&&)> replaceMesh;                            // set_mesh, one undo step
    std::function<bool(Document&, SelectionSet&&, const std::string& label, std::string& msg)> addSelectionSet;
    std::function<void(Document&, int)> selectPart;
    std::function<void(bool error, const std::string& text)> write; // print() and tracebacks
    std::string home;               // the Python runtime shipped with the app ("" = the one the host was built with)
    std::vector<std::string> paths; // put first on sys.path: the scripts folder and its site-packages
};

class Host {
public:
    virtual ~Host() = default;
    enum class Status { Ok, Error, More }; // More: the console line opened a block; send the next line
    virtual Status runLine(const std::string& line) = 0; // one console line, as Python's own prompt takes it
    virtual void cancelLine() = 0;                       // drop a block typed so far
    virtual Status runFile(const std::string& path) = 0; // a .py file, as __main__
    virtual std::vector<std::string> complete(const std::string& text) = 0; // names that continue `text`
    virtual std::string version() const = 0;                                // "3.13.9"
};

} // namespace python
} // namespace sf

// The host's one export. Null with `error` set when Python can't start.
extern "C" {
typedef sf::python::Host* (*SfPyHostCreate)(int abi, const sf::python::AppSide* app, char* error, int errorSize);
}
