// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Macro.h"

#include "core/BrushLibrary.h"
#include "core/Pack.h"
#include "core/RadialMenu.h"
#include "core/Sha256.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>

namespace sf {

namespace fs = std::filesystem;

namespace {

fs::path u8path(const std::string& p) { return fs::path(std::u8string(p.begin(), p.end())); }
std::string u8str(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
std::string lowerAscii(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

std::string g_macroDir;

// Commands a macro may never run: they reach the network, start or talk to another program, change what is
// installed, or run macros themselves.
const std::set<std::string>& deniedCommands() {
    static const std::set<std::string> kDenied = {
        "brush_ai",       "brush_gallery_install", "brush_gallery_search", "comfyui_check",  "image_to_3d",
        "text_to_3d",     "splat_train",           "splat_train_cancel",   "blender_send",   "blender_receive",
        "maya_send",      "maya_receive",          "goz_send",             "goz_receive",    "unreal_send",
        "unreal_editors", "blender_keymap_import", "macro_run",            "macro_list",     "macro_check",
        "pack_install",   "pack_remove",           "pack_enable",          "pack_disable",   "pack_list",
        "pack_create",    "calibrate"};
    return kDenied;
}
// Parameters a macro may never set (network addresses, programs to start).
bool deniedParam(const std::string& p) {
    return p == "url" || p == "endpoint" || p == "gallery" || p == "zbrush" || p == "workflow" || p == "launch" ||
           p == "provider" || p == "service";
}

} // namespace

MacroPathKind macroPathKind(const std::string& command, const std::string& param) {
    const bool folder = param == "folder" || param == "out" || param == "dir" || param == "directory" ||
                        (param.size() > 7 && param.compare(param.size() - 7, 7, "_folder") == 0);
    if (folder) return MacroPathKind::Folder;
    const bool file = param == "path" || param == "file" || param == "exr" || param == "output" ||
                      (param.size() > 5 && param.compare(param.size() - 5, 5, "_path") == 0) ||
                      (param.size() > 5 && param.compare(param.size() - 5, 5, "_file") == 0);
    if (!file) return MacroPathKind::None;
    const bool save = command.find("export") != std::string::npos || command.find("save") != std::string::npos ||
                      command.rfind("capture_", 0) == 0 || command == "vdm_capture" || param == "exr" ||
                      param == "output";
    return save ? MacroPathKind::Save : MacroPathKind::Open;
}

bool macroCommandAllowed(const std::string& command, std::string* why) {
    auto no = [&](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    if (deniedCommands().count(command)) {
        if (command.rfind("macro_", 0) == 0) return no("a macro can't run macros");
        if (command.rfind("pack_", 0) == 0) return no("a macro can't install, list or remove packs");
        return no("it reaches the network or another program, which macros never do");
    }
    static const char* kPrefixes[] = {"ai_", "phone_", "link_", "unreal_", "comfyui", "gallery_", "mcp_"};
    for (const char* p : kPrefixes)
        if (command.rfind(p, 0) == 0) return no("it reaches the network or another program, which macros never do");
    return true;
}

bool macroPathValueOk(const std::string& v, std::string& error) {
    if (v == kMacroAsk) return true;
    if (v.empty()) {
        error = "an empty file name";
        return false;
    }
    if (v.size() > 260 || radialCleanText(v, v.size() + 1) != v) {
        error = "a file name with control characters or invalid text";
        return false;
    }
    if (v[0] == '/' || v[0] == '\\' || v.find(':') != std::string::npos || v[0] == '~' || v[0] == '$' || v[0] == '%') {
        error = "\"" + v + "\" is not inside the project folder: macros only use files relative to the project or \"?\" "
                "(asks when the macro runs)";
        return false;
    }
    for (size_t start = 0; start <= v.size();) {
        size_t end = v.find_first_of("/\\", start);
        if (end == std::string::npos) end = v.size();
        if (v.compare(start, end - start, "..") == 0) {
            error = "\"" + v + "\" leaves the project folder ('..')";
            return false;
        }
        start = end + 1;
    }
    return true;
}

bool resolveMacroPath(const std::string& projectDir, const std::string& relative, std::string& out, std::string& error) {
    if (!macroPathValueOk(relative, error)) return false;
    if (relative == kMacroAsk) {
        error = "\"?\" must be answered in a file dialog";
        return false;
    }
    if (projectDir.empty()) {
        error = "\"" + relative + "\" is relative to the project folder: save the project first";
        return false;
    }
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(u8path(projectDir), ec);
    if (ec || !fs::is_directory(root, ec)) {
        error = "The project folder " + projectDir + " doesn't exist";
        return false;
    }
    const fs::path full = fs::weakly_canonical(root / u8path(relative), ec);
    if (ec) {
        error = "Cannot resolve " + relative;
        return false;
    }
    // Inside the project folder even after following symbolic links.
    const fs::path rel = full.lexically_relative(root);
    const std::string r = u8str(rel);
    if (rel.empty() || r == ".." || r.rfind("../", 0) == 0 || r.rfind("..\\", 0) == 0) {
        error = "\"" + relative + "\" leads outside the project folder";
        return false;
    }
    out = u8str(full);
    return true;
}

bool parseMacro(const std::string& text, const CommandRegistry* registry, Macro& out, std::string& error,
                const std::string& name) {
    out = {};
    out.name = name;
    out.text = text;
    if (text.size() > kMaxMacroFileSize) {
        error = "The macro is too long (at most 64 KB)";
        return false;
    }
    if (text.find('\0') != std::string::npos) {
        error = "The macro holds a NUL character: not a text file";
        return false;
    }
    // The header: the leading comment lines.
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = trim(text.substr(at, end - at));
        if (!line.empty() && line[0] != '#') break;
        auto key = [&](const char* k, std::string& to) {
            const std::string prefix = std::string("# ") + k + ":";
            if (line.compare(0, prefix.size(), prefix) == 0) to = radialCleanText(trim(line.substr(prefix.size())), 200);
        };
        key("name", out.name);
        key("description", out.description);
        at = end + 1;
    }
    if (trim(out.name).empty()) out.name = name;
    std::string err;
    if (!parseScript(text, out.statements, err)) {
        error = err;
        return false;
    }
    if (out.statements.size() > kMaxMacroStatements) {
        error = "The macro has too many commands (at most " + std::to_string(kMaxMacroStatements) + ")";
        return false;
    }
    if (out.statements.empty()) {
        error = "The macro has no commands";
        return false;
    }
    for (ScriptStatement& st : out.statements) {
        const std::string where = "Line " + std::to_string(st.line) + " (" + radialCleanText(st.command, 40) + "): ";
        std::string why;
        if (!macroCommandAllowed(st.command, &why)) {
            error = where + "not allowed in a macro: " + why;
            return false;
        }
        const Command* cmd = registry ? registry->find(st.command) : nullptr;
        if (registry && !cmd) {
            error = where + "unknown command";
            return false;
        }
        if (!registry) {
            bool idOk = !st.command.empty() && st.command.size() <= 64;
            for (unsigned char c : st.command) idOk = idOk && (std::islower(c) || std::isdigit(c) || c == '_');
            if (!idOk) {
                error = where + "not a command name";
                return false;
            }
        }
        for (auto& [key, value] : st.args) {
            if (deniedParam(key)) {
                error = where + key + "= is not allowed in a macro (network addresses and programs)";
                return false;
            }
            const ParamSpec* spec = nullptr;
            if (cmd)
                for (const ParamSpec& p : cmd->params)
                    if (p.name == key) spec = &p;
            if (cmd && !spec) {
                error = where + "unknown parameter " + key + "=";
                return false;
            }
            if (spec) {
                using T = ParamSpec::Type;
                if (spec->type == T::String && !std::holds_alternative<std::string>(value))
                    value = formatValue(value); // name=12: the text "12"
                else if (spec->type == T::Number && !std::holds_alternative<double>(value)) {
                    error = where + key + "= must be a number";
                    return false;
                } else if (spec->type == T::Bool && !std::holds_alternative<bool>(value)) {
                    error = where + key + "= must be true or false";
                    return false;
                }
            }
            const MacroPathKind kind = macroPathKind(st.command, key);
            if (kind != MacroPathKind::None) {
                if (!std::holds_alternative<std::string>(value)) value = formatValue(value);
                std::string perr;
                if (!macroPathValueOk(std::get<std::string>(value), perr)) {
                    error = where + key + "=: " + perr;
                    return false;
                }
            } else if (const auto* s = std::get_if<std::string>(&value);
                       s && (key == "preset") && (s->find('/') != std::string::npos || s->find('\\') != std::string::npos ||
                                                  lowerAscii(*s).find(".json") != std::string::npos)) {
                error = where + "preset= may only name a built-in preset in a macro";
                return false;
            }
        }
        if (cmd)
            for (const ParamSpec& p : cmd->params)
                if (p.required && !st.args.count(p.name)) {
                    error = where + "needs " + p.name + "=";
                    return false;
                }
    }
    return true;
}

bool readMacroFile(const std::string& path, const CommandRegistry* registry, Macro& out, std::string& error) {
    std::error_code ec;
    const fs::path p = u8path(path);
    if (!fs::is_regular_file(p, ec)) {
        error = "Cannot read " + path;
        return false;
    }
    if (fs::file_size(p, ec) > kMaxMacroFileSize) {
        error = path + " is too long for a macro (at most 64 KB)";
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    std::string text(kMaxMacroFileSize + 1, '\0');
    f.read(text.data(), std::streamsize(text.size()));
    text.resize(size_t(std::max<std::streamsize>(f.gcount(), 0)));
    if (!parseMacro(text, registry, out, error, u8str(p.stem()))) return false;
    out.path = path;
    return true;
}

std::string macroFileText(const std::string& name, const std::string& description, const std::vector<std::string>& lines) {
    std::string out = "# name: " + radialCleanText(name, 200) + "\n";
    if (!description.empty()) out += "# description: " + radialCleanText(description, 200) + "\n";
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

std::string macroFileName(const std::string& name) {
    std::string f = brushFileName(name); // the same rules: safe on every OS
    return f.substr(0, f.size() - std::strlen(".sfbrush")) + kMacroExtension;
}

// ---------------------------------------------------------------------------
// Undo as one step
// ---------------------------------------------------------------------------

MacroUndoGroup::MacroUndoGroup(const Document& doc)
    : subtoolId_(doc.activeSubtoolInfo().id), subtools_(doc.subtoolCount()), undoCount_(doc.undo.undoCount()),
      revision_(doc.undo.revision()) {}

bool MacroUndoGroup::end(Document& doc, const std::string& label) {
    if (!enabled_ || doc.subtoolCount() != subtools_ || doc.activeSubtoolInfo().id != subtoolId_) return false;
    const size_t now = doc.undo.undoCount();
    if (now < undoCount_ + 2) return false; // nothing to fold
    const size_t k = now - undoCount_;
    if (doc.undo.revision() - revision_ != k) return false; // steps were undone, redone or dropped meanwhile
    if (doc.mesh.multires || doc.mesh.micro) return false;
    for (size_t i = 0; i < k; ++i) {
        const UndoRecord* r = doc.undo.undoStep(i);
        if (!r || r->subtool || r->subdiv || r->micro || r->texture || r->textureTiles) return false;
    }
    Mesh after = doc.mesh;
    for (size_t i = 0; i < k; ++i)
        if (!doc.undo.undo(doc.mesh, doc.bvh)) return false;
    doc.replaceMeshUndoable(std::move(after), label);
    return true;
}

MacroRunResult runMacro(Document& doc, const CommandRegistry& registry, const Macro& macro, const MacroRunContext& ctx) {
    MacroRunResult result;
    std::string err;
    Macro checked;
    if (!parseMacro(macro.text, &registry, checked, err, macro.name)) { // (again: the registry may know more now)
        result.ok = false;
        result.message = macro.name + ": " + err;
        return result;
    }
    MacroUndoGroup group(doc);
    for (const ScriptStatement& st : checked.statements) {
        Args args = st.args;
        for (auto& [key, value] : args) {
            const MacroPathKind kind = macroPathKind(st.command, key);
            if (kind == MacroPathKind::None) continue;
            const std::string v = std::get<std::string>(value);
            std::string resolved;
            if (v == kMacroAsk) {
                resolved = ctx.askPath ? ctx.askPath(st.command, key, kind) : std::string();
                if (resolved.empty()) {
                    result.ok = false;
                    result.message = macro.name + ", line " + std::to_string(st.line) + ": " +
                                     (ctx.askPath ? "no file picked; the macro stopped"
                                                  : st.command + " asks for a file (\"?\"), which only the app can do");
                    group.end(doc, "Macro: " + macro.name);
                    return result;
                }
            } else if (!resolveMacroPath(ctx.projectDir, v, resolved, err)) {
                result.ok = false;
                result.message = macro.name + ", line " + std::to_string(st.line) + ": " + err;
                group.end(doc, "Macro: " + macro.name);
                return result;
            }
            value = resolved;
        }
        if (st.command == "undo" || st.command == "redo") group.disable();
        const CommandResult r = ctx.execute ? ctx.execute(st.command, args) : registry.execute(doc, st.command, args);
        if (!r.ok) {
            result.ok = false;
            result.message = macro.name + ", line " + std::to_string(st.line) + " (" + st.command + "): " + r.message;
            group.end(doc, "Macro: " + macro.name);
            return result;
        }
        ++result.ran;
    }
    result.oneUndoStep = group.end(doc, "Macro: " + macro.name);
    result.message = "Ran macro " + macro.name + " (" + std::to_string(result.ran) +
                     (result.ran == 1 ? " command" : " commands") + (result.oneUndoStep ? ", one undo step)" : ")");
    return result;
}

std::vector<MacroAsk> macroAsks(const Macro& macro) {
    std::vector<MacroAsk> out;
    for (const ScriptStatement& st : macro.statements)
        for (const auto& [key, value] : st.args)
            if (const auto* s = std::get_if<std::string>(&value);
                s && *s == kMacroAsk && macroPathKind(st.command, key) != MacroPathKind::None)
                out.push_back({st.line, st.command, key, macroPathKind(st.command, key)});
    return out;
}

// ---------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------

void MacroRecorder::start() {
    active_ = true;
    lines_.clear();
    skipped_ = 0;
}

void MacroRecorder::record(const std::string& command, const Args& args, const std::string& projectDir) {
    if (!active_) return;
    if (!macroCommandAllowed(command)) {
        ++skipped_;
        return;
    }
    Args a;
    for (const auto& [key, value] : args) {
        if (deniedParam(key)) continue;
        if (macroPathKind(command, key) == MacroPathKind::None || !std::holds_alternative<std::string>(value)) {
            a[key] = value;
            continue;
        }
        // A file inside the project folder stays as a relative path; any other file is asked for when it runs.
        std::string v = kMacroAsk;
        const std::string& s = std::get<std::string>(value);
        std::error_code ec;
        if (!projectDir.empty() && !s.empty()) {
            const fs::path root = fs::weakly_canonical(u8path(projectDir), ec);
            const fs::path full = fs::weakly_canonical(u8path(s), ec);
            const fs::path rel = full.lexically_relative(root);
            const std::u8string g = rel.generic_u8string();
            const std::string r(g.begin(), g.end());
            std::string perr;
            if (!ec && !r.empty() && r != "." && r.rfind("..", 0) != 0 && macroPathValueOk(r, perr)) v = r;
        }
        a[key] = v;
    }
    lines_.push_back(formatStatement(command, a));
}

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------

std::string userMacroDir() {
    if (!g_macroDir.empty()) return g_macroDir;
    const std::string data = appDataDir();
    return data.empty() ? std::string() : u8str(u8path(data) / "macros");
}
void setUserMacroDir(const std::string& dir) { g_macroDir = dir; }

std::vector<Macro> listMacros(const CommandRegistry* registry, std::vector<std::string>* messages) {
    std::vector<Macro> out;
    const std::string dir = userMacroDir();
    std::vector<std::string> files;
    std::error_code ec;
    if (!dir.empty())
        for (fs::directory_iterator it(u8path(dir), ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_regular_file(e2) && lowerAscii(u8str(it->path().extension())) == kMacroExtension)
                files.push_back(u8str(it->path()));
        }
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        Macro m;
        std::string err;
        if (readMacroFile(f, registry, m, err)) out.push_back(std::move(m));
        else if (messages) messages->push_back("Skipped macro " + f + ": " + err);
    }
    for (const PackContentItem& item : enabledPackItems(PackItemType::Macro)) {
        Macro m;
        std::string err;
        if (!readMacroFile(item.file, registry, m, err)) {
            if (messages) messages->push_back("Skipped macro " + item.file + " (pack " + item.packName + "): " + err);
            continue;
        }
        m.pack = item.packName;
        out.push_back(std::move(m));
    }
    std::stable_sort(out.begin(), out.end(), [](const Macro& a, const Macro& b) {
        return lowerAscii(a.name) < lowerAscii(b.name);
    });
    return out;
}

bool findMacro(const std::string& name, const CommandRegistry* registry, Macro& out, std::string& error) {
    std::vector<std::string> messages;
    const std::string want = lowerAscii(trim(name));
    for (Macro& m : listMacros(registry, &messages)) {
        const std::string label = lowerAscii(m.pack.empty() ? m.name : m.name + " (" + m.pack + ")");
        if (label == want || (m.pack.empty() && lowerAscii(m.name) == want)) {
            out = std::move(m);
            return true;
        }
    }
    for (Macro& m : listMacros(registry))
        if (lowerAscii(m.name) == want) {
            out = std::move(m);
            return true;
        }
    error = "No macro called \"" + radialCleanText(name, 64) + "\"";
    for (const std::string& m : messages) error += "; " + m;
    return false;
}

namespace {
fs::path confirmedPath() { return u8path(userMacroDir()) / "confirmed.txt"; }
} // namespace

bool macroConfirmed(const std::string& text) {
    if (userMacroDir().empty()) return false;
    std::ifstream f(confirmedPath(), std::ios::binary);
    const std::string want = sha256Hex(text);
    std::string line;
    int lines = 0;
    while (std::getline(f, line) && ++lines < 100000)
        if (trim(line) == want) return true;
    return false;
}

void setMacroConfirmed(const std::string& text) {
    if (userMacroDir().empty() || macroConfirmed(text)) return;
    std::error_code ec;
    fs::create_directories(u8path(userMacroDir()), ec);
    std::ofstream f(confirmedPath(), std::ios::binary | std::ios::app);
    f << sha256Hex(text) << "\n";
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

void registerMacroCommands(CommandRegistry& registry) {
    using T = ParamSpec::Type;
    const CommandRegistry* reg = &registry;
    registry.add({"macro_run",
                  "Run a macro (.sfmacro): a list of SculptForge commands. Its file values are relative to the project "
                  "folder (the folder of `project`, else of the open project) or \"?\", which only the app can answer. "
                  "Edits on one Part become one undo step. The app shows a macro's commands and asks before it first runs.",
                  {{"path", T::String, "The .sfmacro file (or give name=).", false},
                   {"name", T::String, "A macro in your macros folder or an enabled pack, by name.", false},
                   {"project", T::String, "The project folder relative file values resolve in.", false}},
                  [reg](Document& doc, const Args& a) -> CommandResult {
                      Macro m;
                      std::string err;
                      const std::string path = argString(a, "path"), name = argString(a, "name");
                      if (path.empty() && name.empty()) return {false, "Give path= (a .sfmacro file) or name="};
                      if (!path.empty() ? !readMacroFile(path, reg, m, err) : !findMacro(name, reg, m, err))
                          return {false, err};
                      MacroRunContext ctx;
                      ctx.projectDir = argString(a, "project");
                      if (ctx.projectDir.empty() && !doc.path.empty())
                          ctx.projectDir = u8str(u8path(doc.path).parent_path());
                      const MacroRunResult r = runMacro(doc, *reg, m, ctx);
                      return {r.ok, r.message};
                  }});
    registry.add({"macro_list", "List the macros: your own (in the macros folder) and those of enabled packs.", {},
                  [reg](Document&, const Args&) -> CommandResult {
                      std::vector<std::string> messages;
                      const auto macros = listMacros(reg, &messages);
                      std::string out = macros.empty() ? "No macros"
                                                       : std::to_string(macros.size()) + (macros.size() == 1 ? " macro" : " macros");
                      for (const Macro& m : macros)
                          out += "\n" + m.name + (m.pack.empty() ? "" : " (" + m.pack + ")") + ": " +
                                 std::to_string(m.statements.size()) + " commands" +
                                 (m.description.empty() ? "" : " - " + m.description);
                      for (const std::string& msg : messages) out += "\n" + msg;
                      return {true, out};
                  }});
    registry.add({"macro_check", "Check a macro without running it: every command known and allowed, file values confined.",
                  {{"path", T::String, "The .sfmacro file.", false}, {"text", T::String, "The macro's text instead.", false}},
                  [reg](Document&, const Args& a) -> CommandResult {
                      Macro m;
                      std::string err;
                      const bool ok = a.count("text") ? parseMacro(argString(a, "text"), reg, m, err)
                                                      : readMacroFile(argString(a, "path"), reg, m, err);
                      if (!ok) return {false, err};
                      return {true, m.name + ": " + std::to_string(m.statements.size()) + " commands, all allowed"};
                  }});
}

} // namespace sf
