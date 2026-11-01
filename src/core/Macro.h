// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Macros (.sfmacro): a list of SculptForge's own commands in the command language of core/Script.h, one per line,
// '#' comments. A macro is data, never code: it can only run commands from the registry, and not every command:
// nothing that reaches the network, starts another program, installs or removes packs, or runs another macro
// (macroCommandAllowed). Values of file parameters (path=, folder=, ...) must be either
//   - a relative path, resolved inside the project folder (no absolute paths, drive letters, '~' or '..'), or
//   - "?" : ask, in a file dialog when the macro runs.
// so a macro never names an arbitrary file baked into it. Optional header lines:
//   # name: Clean Up Scan
//   # description: Remesh, smooth and export a GLB next to the project
// The app shows a macro's full command list and asks before it first runs (and again after it changed; the
// confirmation is kept by the text's SHA-256). sculptforge-cli and the Python module run what the user asked.
#pragma once

#include "core/Commands.h"
#include "core/Script.h"

#include <functional>
#include <string>
#include <vector>

namespace sf {

constexpr const char* kMacroExtension = ".sfmacro";
constexpr size_t kMaxMacroFileSize = 64 * 1024;
constexpr size_t kMaxMacroStatements = 500;
constexpr const char* kMacroAsk = "?"; // a file value that asks when the macro runs

struct Macro {
    std::string name;        // header "# name:", else the file name without its extension
    std::string description; // header "# description:"
    std::string text;        // the whole file
    std::vector<ScriptStatement> statements;
    std::string path; // the file it came from (empty: typed or recorded)
    std::string pack; // the pack it came with (its name), "" for the user's own
};

// What kind of file a parameter names: None for parameters that aren't files.
enum class MacroPathKind { None, Open, Save, Folder };
MacroPathKind macroPathKind(const std::string& command, const std::string& param);

// Whether a macro may run COMMAND (WHY says why not).
bool macroCommandAllowed(const std::string& command, std::string* why = nullptr);

// A file value as a macro may hold it: "?" or a confined relative path. False with ERROR otherwise.
bool macroPathValueOk(const std::string& value, std::string& error);
// Resolves a confined relative path inside PROJECTDIR (which must exist). Symbolic links on the way are followed
// and the result must still lie inside the project folder.
bool resolveMacroPath(const std::string& projectDir, const std::string& relative, std::string& out, std::string& error);

// Parses TEXT: size and statement caps, the command language, the header, and for every statement: a command the
// REGISTRY knows (null: any name; packs are checked without the app's registry), allowed in macros, with its
// required parameters, no unknown parameters, values of the right type, and confined file values. ERROR names the
// line. NAME is the fallback name (the file stem).
bool parseMacro(const std::string& text, const CommandRegistry* registry, Macro& out, std::string& error,
                const std::string& name = "Macro");
// Reads and parses a .sfmacro file (size-capped).
bool readMacroFile(const std::string& path, const CommandRegistry* registry, Macro& out, std::string& error);
// The macro's file text with a fresh header: "# name: ...", "# description: ...", then the statements.
std::string macroFileText(const std::string& name, const std::string& description,
                          const std::vector<std::string>& lines);
// "<name>.sfmacro" made safe on every OS.
std::string macroFileName(const std::string& name);

// --- Running ---

struct MacroRunContext {
    std::string projectDir; // folder of the saved project (relative file values resolve here); "" = none
    // Asks for a file for COMMAND's PARAM (a dialog in the app); returns "" when cancelled. Null: "?" values fail.
    std::function<std::string(const std::string& command, const std::string& param, MacroPathKind kind)> askPath;
    // Runs one command (the app's runCommand); null: registry.execute.
    std::function<CommandResult(const std::string& command, const Args& args)> execute;
};

struct MacroRunResult {
    bool ok = true;
    std::string message;
    int ran = 0;              // statements that ran
    bool oneUndoStep = false; // the macro's edits were folded into one undo step
};

// Folds the undo steps a macro made on the active Part into one, when it can: the same Part is active before and
// after, the Parts weren't added or removed, and none of the steps is a level, micro, texture or Part step.
class MacroUndoGroup {
public:
    explicit MacroUndoGroup(const Document& doc);
    bool end(Document& doc, const std::string& label); // true when the steps became one
    void disable() { enabled_ = false; }               // the macro ran undo / redo itself: leave its steps alone

private:
    uint32_t subtoolId_ = 0;
    int subtools_ = 0;
    size_t undoCount_ = 0;
    uint64_t revision_ = 0;
    bool enabled_ = true;
};

// The "?" values a macro will ask for, in the order runMacro asks (the app opens its dialogs first).
struct MacroAsk {
    int line = 0;
    std::string command, param;
    MacroPathKind kind = MacroPathKind::Open;
};
std::vector<MacroAsk> macroAsks(const Macro& macro);

// Runs every statement in order, stopping at the first failure; file values are resolved first ("?" asks).
MacroRunResult runMacro(Document& doc, const CommandRegistry& registry, const Macro& macro, const MacroRunContext& ctx);

// --- Recording ---

// Turns the commands the user runs into macro lines: commands macros may not run are left out, file values
// inside PROJECTDIR become relative paths and any other file becomes "?" (asked when the macro runs).
class MacroRecorder {
public:
    void start();
    void stop() { active_ = false; }
    bool active() const { return active_; }
    void record(const std::string& command, const Args& args, const std::string& projectDir);
    const std::vector<std::string>& lines() const { return lines_; }
    int skipped() const { return skipped_; }

private:
    bool active_ = false;
    std::vector<std::string> lines_;
    int skipped_ = 0;
};

// --- Library ---

// <app data>/macros: the user's own macros (empty when there is no app data folder).
std::string userMacroDir();
// Overrides userMacroDir() (UI tests, unit tests); "" = the default.
void setUserMacroDir(const std::string& dir);
// The user's macros, then every enabled pack's (core/Pack.h), sorted by name; files that can't be read are skipped
// with a message.
std::vector<Macro> listMacros(const CommandRegistry* registry, std::vector<std::string>* messages = nullptr);
// The macro called NAME (any case; "Name (Pack)" picks a pack's), or false.
bool findMacro(const std::string& name, const CommandRegistry* registry, Macro& out, std::string& error);

// Whether the user confirmed this exact macro text before (kept in <macros>/confirmed.txt by SHA-256).
bool macroConfirmed(const std::string& text);
void setMacroConfirmed(const std::string& text);

// macro_run, macro_list, macro_check.
void registerMacroCommands(CommandRegistry& registry);

} // namespace sf
