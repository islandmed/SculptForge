// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Python scripting in the app (docs/guide/python.md). Off until Preferences > Scripts > Enable Python scripts is on
// (python.json in the settings folder, not ui.json: no layout or preset can turn it on), and never allowed when a
// studio policy says python.scripts "never".
//
// Python itself is in another library (python/PyHost.h, sculptforge_pyhost), loaded the first time a script runs:
// an app that never runs one never loads Python. A script runs only when you run it: a console line in Python
// mode, a file from the Scripts folder or Run script file. Nothing you open (projects, meshes, packs, layouts,
// macros) can carry or start one, and AI tools and macros have no command that runs Python.
//
// Each run is one undo step (an UndoBatchScope around it: every step it pushed, on any Part, undoes together).
#pragma once

#include "python/PyHost.h"

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace sf {

class App;
// sculptforge --python-selftest: starts the Python the app ships with and runs a few lines (exit code 0 = it works).
int pythonSelfTest();

class PythonScripting {
public:
    explicit PythonScripting(App& app);
    ~PythonScripting();

    // Preferences > Scripts. allowed() is false under a studio policy that says never (`why` says so).
    bool allowed(std::string* why = nullptr) const;
    bool enabled() const;
    void setEnabled(bool on);

    bool loaded() const { return host_ != nullptr; }
    int loads() const { return loads_; } // times the host was loaded (tests: opening files must leave it 0)
    const std::string& version() const { return version_; }

    enum class Result { Ok, Error, More, Unavailable };
    Result runLine(const std::string& line); // console (More: a block is open, send its next line)
    void cancelLine();
    Result runFile(const std::string& path); // a .py file
    std::vector<std::string> complete(const std::string& text);

    // The Scripts folder (Documents/SculptForge/Scripts) and the .py files in it, sorted (re-read at most every
    // two seconds; only while a list of them is drawn).
    std::string scriptsFolder() const;
    const std::vector<std::string>& scripts();

    // Output a script printed (any thread), shown in the console each frame.
    void drainOutput();

    // Script-made interface (sf.ui, P2): each action is the app action "script.py.<id>", each panel a section of
    // the Script palette ("script.py.<id>"), so shelves, My Panel, keys and the command palette take them as any.
    static constexpr const char* kActionPrefix = "script.py.";
    // The panels, to draw (and hold the values the controls show). Between beginDraw and endDraw a callback that
    // declares the interface again is applied at endDraw, so the list never changes under the loop.
    std::vector<python::UiPanel>& panels() { return ui_.panels; }
    void beginDraw() { drawing_ = true; }
    void endDraw() {
        drawing_ = false;
        syncUi();
    }
    Result runAction(const std::string& id);
    // A control changed (slider let go, check box clicked): the app's copy now, then the script's on_change.
    Result setValue(const std::string& panel, const std::string& control, double value);

    // Run at start-up (Preferences > Scripts): scripts the user ticked run once Python is on and the first frame
    // is up. Each is remembered with its SHA-256: a script that changed since doesn't run until it is ticked again.
    // Only scripts directly in the Scripts folder can be ticked or run. A mismatch says so in the status bar and the
    // console, and Preferences > Scripts offers Trust this version (setRunAtStartup again, with the new SHA-256).
    bool runsAtStartup(const std::string& path) const;
    bool setRunAtStartup(const std::string& path, bool on); // false: not a script in the Scripts folder
    std::vector<std::string> startupScripts() const;         // ticked ones (also those gone from the folder)
    bool startupChanged(const std::string& path) const { return changed_.count(path) != 0; } // didn't run: changed
    bool inScriptsFolder(const std::string& path) const;
    void runStartupScripts(bool again = false); // once, from the first frame (nothing to do: nothing loads); again: tests

    // Timers (sf.ui.timer, P3): the main loop wakes for the next one (seconds from now; false: none) and runs the
    // due ones each pass. Without timers, or Python, both are a pointer check.
    bool timerWake(double& inSeconds) const;
    void tickTimers();

private:
    bool load(); // false (after saying why in the console) when Python can't run
    template <class F>
    Result run(F&& body);
    void write(bool error, const std::string& text);
    void syncUi(); // the host's declared interface changed: actions and panels again
    bool place(const std::string& id, bool panel, python::Place where); // sf.ui.place
    void readPrefs() const;
    void savePrefs() const;

    App& app_;
    python::Host* host_ = nullptr; // (never unloaded: CPython can't be)
    int loads_ = 0;
    std::string version_;
    std::string loadError_; // why the host didn't load (said once per attempt)
    mutable int enabled_ = -1; // -1: python.json not read yet
    mutable std::vector<std::pair<std::string, std::string>> startup_; // Run at start-up: path, SHA-256
    bool startupRan_ = false;
    std::set<std::string> changed_; // ticked scripts that changed since: they didn't run
    python::UiState ui_;
    uint64_t uiRevision_ = 0;
    bool drawing_ = false;
    std::vector<std::string> scripts_;
    double scriptsScanned_ = -1e9;
    std::mutex outMutex_;
    std::vector<std::pair<bool, std::string>> out_;
    std::string partial_[2]; // text after the last newline, per stream
};

} // namespace sf
