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

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sf {

class App;
namespace python {
class Host;
}

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

private:
    bool load(); // false (after saying why in the console) when Python can't run
    template <class F>
    Result run(F&& body);
    void write(bool error, const std::string& text);

    App& app_;
    python::Host* host_ = nullptr; // (never unloaded: CPython can't be)
    int loads_ = 0;
    std::string version_;
    std::string loadError_; // why the host didn't load (said once per attempt)
    mutable int enabled_ = -1; // -1: python.json not read yet
    std::vector<std::string> scripts_;
    double scriptsScanned_ = -1e9;
    std::mutex outMutex_;
    std::vector<std::pair<bool, std::string>> out_;
    std::string partial_[2]; // text after the last newline, per stream
};

} // namespace sf
