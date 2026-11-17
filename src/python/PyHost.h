// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Python scripting in the app: the line between the app and its Python host. The host (AppHost.cpp, the
// sculptforge_pyhost library with CPython in it) is loaded only when a script first runs (app/Python.cpp), so an
// app that never runs Python never loads Python: nothing at start-up, nothing per frame.
//
// The app hands the host its open scene and how to change it; the host runs Python on the main thread, inside the
// frame, and gives back what it printed. Both halves are built from this source tree by the same compiler, so C++
// types cross the line; the ABI number guards against a host from another build. The host has no core of its own:
// every core function it calls is the app's (AppSide::core, CoreCalls.h).
#pragma once

#include "core/Commands.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sf {

struct Document;
class Mesh;
struct SelectionSet;

namespace python {
struct CoreCalls;
}

namespace python {

constexpr int kHostAbi = 3;

// A direct change to a document's data from Python (doc.mask = ...).
enum class Edit { Mask, Colors, FaceGroups, Hidden };

// The interface a script declared (sf.ui, P2): actions and panels, drawn by the app with its own widgets. The app
// holds a copy and the controls' values; the host is asked again only when its revision changes.
struct UiAction {
    std::string id, label, help, icon; // id: the script's ("cleanup.run"); the app's action is "script.py.<id>"
};
struct UiControl {
    enum class Kind { Label, Separator, Button, Slider, Checkbox };
    Kind kind = Kind::Label;
    std::string id, label, help, action; // action: a Button's
    double value = 0.0, min = 0.0, max = 1.0;
    bool integer = false;
};
struct UiPanel {
    std::string id, title;
    std::vector<UiControl> controls;
};
struct UiState {
    std::vector<UiAction> actions;
    std::vector<UiPanel> panels;
};

// Where sf.ui.place puts an action or a panel.
enum class Place { TopShelf, BottomShelf, LeftShelf, RightShelf, MyPanel };

struct AppSide {
    // The build it belongs to: the host refuses another's (SCULPTFORGE_VERSION and the layout of what crosses).
    std::string build;
    size_t documentSize = 0, meshSize = 0;
    Document* document = nullptr; // the open scene
    const CommandRegistry* registry = nullptr;
    const CoreCalls* core = nullptr; // the app's core, for the Python API (and documents a script makes of its own)
    // A command on the open scene, run the way the app runs one (it echoes, refreshes and lands in undo).
    std::function<CommandResult(const std::string& name, const Args& args)> run;
    std::function<void(Document&, Edit, const std::function<void()>& change)> edit; // records undo, then changes
    std::function<void(Document&, Mesh&&)> replaceMesh;                            // set_mesh, one undo step
    std::function<bool(Document&, SelectionSet&&, const std::string& label, std::string& msg)> addSelectionSet;
    std::function<void(Document&, int)> selectPart;
    std::function<void(bool error, const std::string& text)> write; // print() and tracebacks
    // sf.ui.place: puts "script.py.<id>" (an action, or with `panel` a panel's section) there; false if it can't.
    std::function<bool(const std::string& id, bool panel, Place where)> place;
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
    // The interface scripts declared (P2). uiRevision() changes whenever it does (cheap: no Python runs).
    virtual uint64_t uiRevision() const = 0;
    virtual UiState ui() = 0;
    virtual Status runAction(const std::string& id) = 0; // a script action's callback
    // A control's new value (a slider let go, a check box clicked): stored, then its on_change runs.
    virtual Status setValue(const std::string& panel, const std::string& control, double value) = 0;
    // Timers (sf.ui.timer, P3): whether one is set, and in how many seconds the next is due (<= 0: now). Cheap.
    virtual bool timerDue(double& inSeconds) const = 0;
    virtual Status runTimers() = 0; // the callbacks that are due
};

} // namespace python
} // namespace sf

// The host's one export. Null with `error` set when Python can't start.
extern "C" {
typedef sf::python::Host* (*SfPyHostCreate)(int abi, const sf::python::AppSide* app, char* error, int errorSize);
}
