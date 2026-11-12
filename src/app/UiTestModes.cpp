// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// UI tests for the class of bug where a mode won't let go (GABO on Discord: after Tube or Append > Insert, picking
// another brush didn't leave the mode). docs/testing-flow.md, "Modes and the monkey".
//
//   expect_latched none|TEXT   what holds the viewport (App::latchedForTest) is nothing, or exactly TEXT
//                              ("mode: Move"; several joined with ", ")
//   expect_status_not TEXT     the status bar doesn't name TEXT, unless it says it was left ("Left ...", "... off")
//   expect_sculpts             a plain stroke across the model changes the active Part's mesh (same Part, same
//                              number of Parts)
//   unlatch                    clear every latch whatever the app's own exits do (between generated cases)
//   snapshot_start / undo_all / expect_restored   remember the scene, undo until nothing is left, and the scene is
//                              exactly as remembered (Parts, active Part, its vertices bit for bit, its triangles)
//   expect_mesh_valid          finite positions, triangle indices in range, watertight if it was at snapshot_start
//   mode_entry NAME | LINE | LINE ...   a way into a mode that isn't an action (Append > Insert's menu): the next
//                              mode_matrix probes it and tries the exits from it like an action
//   mode_matrix [known=FILE] [ID...]   every action in the registry (or the IDs given) is probed: run it, and if something
//                              then latches or a plain stroke no longer sculpts, it is a mode: each exit path (Esc, a
//                              brush on the shelf, Draw, Move, Scale, Rotate, a palette action, undo) is tried from it
//                              and the invariants checked. A new mode is covered without a line here; a mode no
//                              exit leaves fails with its id. Each case's script goes to --ui-out/replay-*.sfui.
//                              SF_MODE_MATRIX_SHARD=K/N probes only every Nth entry from the Kth (ui_mode_matrix_KofN).
//   monkey SEED STEPS [COUNT] [known=FILE]   COUNT (default 1) seeded random sequences of STEPS user actions (brushes, strokes,
//                              undo / redo, panels, mode toggles, view changes) from a new sphere, then Esc and a
//                              brush, and the invariants: nothing latched, a stroke sculpts, the mesh is valid, and
//                              undo back to the start restores it exactly. A failing seed's script is written to
//                              --ui-out/replay-monkey-SEED.sfui (tools/test/shrink_sfui.py shrinks it). known=FILE
//                              (the mode matrix's list) leaves out actions with filed failures. A sequence depends
//                              on the registry too: a new action changes the seeds' sequences.
// Values: latched (what holds the viewport, a count), latched_modes, input_captured, overlay_open, modes_found
// (actions the mode matrix found to be modes), sculpted (the active mesh changed since `snapshot`).
// Failures inside a generated case don't stop the script: the next case starts (every failing mode is listed).
#include "app/App.h"
#include "app/Demo.h"
#include "app/NativeDialog.h"
#include "app/Pickers.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace sf {

std::vector<std::string> splitScriptWords(const std::string& line); // (UiTest.cpp)
std::string restOfLine(const std::string& line, int skip);

// ---------------------------------------------------------------- what is latched (App)

std::vector<std::string> App::latchedForTest() const {
    std::vector<std::string> out;
    auto add = [&](bool on, const char* what) {
        if (on) out.emplace_back(what);
    };
    // Modes: a left drag in the view goes to them instead of the brush.
    add(tubeDraw_, "mode: Draw Tube");
    add(!insertType_.empty(), "mode: Insert");
    add(tubeActive(), "mode: Tube Part");
    add(forgesphereActive(), "mode: Forge Spheres");
    add(doc_.mesh.forgespherePreview, "mode: Forge Spheres preview");
    if (gizmo_.mode != GizmoMode::Off) { // (its button's label: a gizmo mode added later is named too)
        static const char* const kIds[] = {"tool.draw", "tool.move", "tool.scale", "tool.rotate", "tool.gizmo_all"};
        const size_t m = size_t(gizmo_.mode);
        const Action* a = m < std::size(kIds) ? actions_.find(kIds[m]) : nullptr;
        out.push_back("mode: " + (a ? a->label : "gizmo " + std::to_string(m)));
    }
    add(strokeLatch() == StrokeLatch::Smooth, "mode: Smooth latch");
    add(strokeLatch() == StrokeLatch::Mask, "mode: Mask latch");
    add(strokeLatch() == StrokeLatch::Select, "mode: Select latch");
    add(guidesMode_, "mode: QRemesh Guides");
    add(maskBrushActive(), "mode: Mask brush");
    add(stencil_.adjust, "mode: Stencil adjust");
    add(!cutPolyline_.empty(), "mode: Cut Polyline points");
    add(doc_.extractPreviewCurrent(), "mode: Extract preview");
    add(gradient_.armed || gradient_.placed, "mode: Line Gradient");
    add(capture_.armed, "mode: Capture from view");
    add(facePlacing_, "mode: Place Landmarks");
    // Input still captured: a drag that never ended, an ImGui item still holding the mouse.
    if (drag_ != Drag::None) out.push_back("input: drag " + std::to_string(int(drag_)));
    if (const ImGuiContext* g = ImGui::GetCurrentContext(); g && g->ActiveId != 0 && g->ActiveIdMouseButton >= 0)
        out.emplace_back("input: an item holds the mouse");
    // Overlays and modals over the canvas.
    add(strokePopup_.open, "overlay: stroke popup");
    add(radial_.menu >= 0, "overlay: radial menu");
    add(quickMenu_.open, "overlay: quick menu");
    add(palette_.isOpen(), "overlay: command palette");
    add(quickTourOpen_, "overlay: Quick tour");
    add(memoryConfirm_.open, "overlay: memory confirm");
    {
        ImVec2 knob;
        add(PickerUi::sliderKnob(knob), "overlay: size popup");
    }
    if (const ImGuiContext* g = ImGui::GetCurrentContext())
        for (const ImGuiPopupData& p : g->OpenPopupStack)
            if (p.Window && (p.Window->Flags & ImGuiWindowFlags_Modal)) out.push_back(std::string("overlay: modal ") + p.Window->Name);
    return out;
}

void App::unlatchForTest() {
    tubeDraw_ = false;
    insertType_.clear();
    gizmo_.mode = GizmoMode::Off;
    gizmo_.hover = GizmoHandle::None;
    strokeLatch_ = {};
    guidesMode_ = false;
    maskBrush_.on = false;
    stencil_.adjust = false;
    cutPolyline_.clear();
    gradient_.armed = gradient_.placed = false;
    gradient_.handle = -1;
    capture_.armed = capture_.open = false;
    facePlacing_ = false;
    packs_.newOpen = false; // (a floating window Esc leaves open, over the model: it would hide later cases' strokes)
    capture_.open = false;
    drag_ = Drag::None;
    strokePopup_.open = false;
    radial_.menu = -1;
    quickMenu_.open = false;
    if (quickTourOpen_) closeQuickTour();
    memoryConfirm_.open = false;
    if (ImGuiContext* g = ImGui::GetCurrentContext()) {
        ImGui::ClearActiveID();
        if (g->OpenPopupStack.Size > 0) ImGui::ClosePopupToLevel(0, false);
    }
    paintMode_ = false;
    doc_.activeBrush = -1;
    brush_ = BrushType::Standard;
}

// ---------------------------------------------------------------- generated cases (Demo)

namespace {

// Actions the mode matrix and the monkey never run: they leave the app (quit, files, the network, other programs),
// wait on real work, or open the system's own windows. Prefixes of the id; why is in the comment beside each.
const char* const kNeverRun[] = {
    "cmd.",        // every command (they have their own tests; some write files or start jobs)
    "file.",       // open / save / import / export: file dialogs and files
    "app.",        // quit, restart, fullscreen
    "help.",       // the browser, the guide's links, About
    "support.",    // membership: the browser
    "link.",       // phone link, AI live mode: listening sockets
    "phone.",      //
    "ai.",         // AI providers: the network
    "maya.",       // other programs over local sockets
    "blender.",    //
    "bridge.",     //
    "update.",     // the update check: the network
    "macro.",      // macros record and replay other actions
    "radial.",     // radial menus hold their key (radial_* tests)
    "render.",     // Save Render: files
    "bake.",       // bakes run on workers for seconds
    "splat.",      // splat training: other programs
    "sculptable.", // Make sculptable: a splat and its workers
    "studio.",     // studio mode: its own folder
    "discord.",    // Rich Presence: Discord's socket
    "ui.my_panel", // a floating window of its own at the cursor (ui_my_panel); one more toggle shifted every seed's picks
};

bool neverRun(const std::string& id) {
    for (const char* p : kNeverRun)
        if (id.rfind(p, 0) == 0) return true;
    return false;
}

// The exits tried from every mode: the lines a person's way out sends, then what must hold.
struct ModeExit {
    const char* name;
    std::vector<std::string> lines; // (generated after `action ID`)
};

std::string slug(std::string s) {
    for (char& c : s)
        if (!std::isalnum(static_cast<unsigned char>(c))) c = '-';
    return s;
}

// SplitMix64: the same sequence from a seed on every platform and compiler (std::uniform_int_distribution isn't).
struct Rng {
    uint64_t s;
    uint64_t next() {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int below(int n) { return n > 0 ? int(next() % uint64_t(n)) : 0; }
    float frac(float lo, float hi) { return lo + (hi - lo) * float(next() % 1000) / 999.0f; }
};

std::string fmt(float v) {
    std::ostringstream ss;
    ss.precision(2);
    ss << std::fixed << v;
    return ss.str();
}

constexpr const char* kStroke = "stroke 0.35 0.45 0.65 0.55";

} // namespace

// Each generated case: its lines parsed as script lines, between a begin step (which skips the case when `when`
// says so) and an end step; a failure inside it jumps to the end, and the next case starts.
bool Demo::addCase(int line, const std::string& name, std::function<bool()> when, const std::vector<std::string>& lines,
                   std::string& error, Step last, std::string key) {
    const size_t begin = steps_.size();
    steps_.emplace_back();
    for (const std::string& l : lines) {
        lineText_ = l;
        const auto words = splitScriptWords(l);
        if (words.empty()) continue;
        if (!parseStep(line, words, error)) {
            error = name + ": " + l + ": " + error;
            return false;
        }
    }
    if (last) steps_.push_back(std::move(last));
    const size_t end = steps_.size();
    steps_.push_back([this](float t) {
        if (t == 0.0f) return false; // a frame as the case left it (a failure's screenshot)
        context_.clear();
        softEnd_ = kNoCase;
        return true;
    });
    cases_[end] = {name, lines, key, setupLines_};
    steps_[begin] = [this, name, when, end](float) {
        context_ = name;
        softEnd_ = end;
        restoreToggles(); // what the last case changed (a panel shut, shelves hidden) doesn't leak into this one
        rememberToggles();
        if (when && !when()) jumpTo(end);
        else if (const std::string& key = cases_[end].key; !key.empty()) knownRan_.insert(key);
        return true;
    };
    return true;
}

// Every toggle's state (not brushes or the Draw / Move / Scale / Rotate tools: the reset picks those) when a case
// begins; the next case runs the ones it changed again first (not at its end: a failure's screenshot shows it).
void Demo::rememberToggles() {
    toggles_.clear();
    for (const Action& a : app_.actions_.all())
        if (a.checked && a.run && !neverRun(a.id) && a.id.rfind("brush.", 0) != 0 && a.id.rfind("tool.", 0) != 0)
            toggles_.emplace_back(a.id, a.checked());
}

void Demo::restoreToggles() {
    for (int pass = 0; pass < 3; ++pass) { // (one toggle may switch another: a radio group)
        bool changed = false;
        for (const auto& [id, was] : toggles_) {
            const Action* a = app_.actions_.find(id);
            if (a && a->checked && a->checked() != was && (!a->enabled || a->enabled())) {
                a->run();
                changed = true;
            }
        }
        if (!changed) break;
    }
    app_.unlatchForTest();
}

bool Demo::knownFailure(size_t end, const std::string& message) {
    const auto it = cases_.find(end);
    if (it == cases_.end() || !known_.count(it->second.key)) return false;
    knownHit_.insert(it->second.key);
    SDL_Log("KNOWN %s [%s] (%s)", message.c_str(), it->second.name.c_str(), known_[it->second.key].c_str());
    jumpTo(end);
    return true;
}

void Demo::jumpTo(size_t step) {
    step_ = step;
    jumped_ = true;
}

// A failure inside a generated case: its script beside the shots, so it replays on its own.
std::string Demo::writeReplay(size_t end) const {
    const auto it = cases_.find(end);
    if (it == cases_.end() || shotDir_.empty()) return "";
    const std::string path = shotDir_ + "/replay-" + slug(it->second.name) + ".sfui";
    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())));
    f << "# " << it->second.name << " (generated by " << scriptName_ << "; failed: " << failures_.back() << ")\n";
    for (const std::string& l : it->second.setup) { // (not other generators: their cases replay on their own)
        const auto words = splitScriptWords(l);
        if (words.empty() || (words[0] != "mode_matrix" && words[0] != "monkey")) f << l << "\n";
    }
    for (const std::string& l : it->second.lines) f << l << "\n";
    return path;
}

bool Demo::parseModeStep(int line, const std::vector<std::string>& w, std::string& error, bool& handled) {
    handled = true;
    const std::string& verb = w[0];
    if (verb == "expect_latched") {
        if (w.size() < 2) {
            error = "'expect_latched' needs none or what is latched";
            return false;
        }
        const std::string want = w[1] == "none" ? "" : restOfLine(lineText_, 1);
        steps_.push_back([this, line, want](float t) {
            if (t == 0.0f) return false; // a settled frame
            std::string have;
            for (const std::string& s : app_.latchedForTest()) have += (have.empty() ? "" : ", ") + s;
            if (have != want) fail(line, "latched: " + (have.empty() ? "none" : have) + "; expected " + (want.empty() ? "none" : want));
            return true;
        });
    } else if (verb == "expect_status_not") {
        if (w.size() < 2) {
            error = "'expect_status_not' needs a text";
            return false;
        }
        run([this, line, text = restOfLine(lineText_, 1)] {
            const std::string& s = app_.status_;
            if (s.find(text) != std::string::npos && s.find("Left ") == std::string::npos && s.find(" off") == std::string::npos)
                fail(line, "the status bar still names " + text + ": \"" + s + "\"");
        });
    } else if (verb == "expect_sculpts") {
        struct Before {
            int part = 0, parts = 0;
            std::vector<glm::vec3> positions;
        };
        auto before = std::make_shared<Before>();
        run([this, before] {
            *before = {app_.doc_.activePart, app_.doc_.partCount(), app_.doc_.mesh.positions};
        });
        lineText_ = kStroke;
        if (!parseStep(line, splitScriptWords(kStroke), error)) return false;
        steps_.push_back([this, line, before](float t) {
            if (t == 0.0f) return false;
            const auto& now = app_.doc_.mesh.positions;
            if (app_.doc_.activePart != before->part || app_.doc_.partCount() != before->parts)
                fail(line, "a plain stroke made or picked another Part (Parts " + std::to_string(before->parts) + " -> " +
                               std::to_string(app_.doc_.partCount()) + ", active " + std::to_string(before->part) + " -> " +
                               std::to_string(app_.doc_.activePart) + ")");
            else if (now == before->positions && ImGui::GetCurrentContext()->HoveredWindow)
                SDL_Log("expect_sculpts: the window \"%s\" is over the model: the stroke went to it (not a latch)",
                        ImGui::GetCurrentContext()->HoveredWindow->Name);
            else if (now == before->positions)
                fail(line, "a plain stroke didn't change the mesh (tool: brush " +
                               std::to_string(app_.doc_.activeBrush >= 0 ? -1 : int(app_.brush_)) + ")");
            return true;
        });
    } else if (verb == "try_action" || verb == "maybe_action") {
        // try_action ID: as action, but a missing or disabled one ends the case quietly; maybe_action ID: does nothing
        // then (a greyed-out button: the monkey's clicks)
        if (w.size() < 2) {
            error = "'" + verb + "' needs an action id";
            return false;
        }
        run([this, id = w[1], end = verb == "try_action"] {
            const Action* a = app_.actions_.find(id);
            if (!a || !a->run || (a->enabled && !a->enabled())) {
                if (end && softEnd_ != kNoCase) jumpTo(softEnd_);
                return;
            }
            app_.actions_.touch(id);
            a->run();
        });
        wait(0.1f);
    } else if (verb == "click_exit") {
        // click_exit LABEL NAME N: click LABEL, a way out of a mode. With no mode latched, only a menu or an overlay
        // open (the Color palette's drop-down, the command palette), the first click may just close it, as menus do:
        // then a second click must give NAME == N.
        if (w.size() < 4) {
            error = "'click_exit' needs LABEL NAME N";
            return false;
        }
        auto lenient = std::make_shared<bool>(false);
        run([this, lenient] {
            bool known = false;
            *lenient = queryModes("latched_modes", known) == 0.0;
        });
        testClick(line, w[1], SDL_BUTTON_LEFT, false);
        const std::string label = w[1], name = w[2];
        const double want = std::strtod(w[3].c_str(), nullptr);
        auto again = std::make_shared<bool>(false);
        run([this, lenient, again, name, want] {
            bool known = false;
            *again = *lenient && std::abs(query(name, known) - want) > 1e-6;
            if (*again) SDL_Log("click_exit: the first click only closed a menu or overlay; clicking again");
        });
        const size_t from = steps_.size();
        testClick(line, label, SDL_BUTTON_LEFT, false);
        // (the second click only when needed: its steps are skipped otherwise)
        const size_t to = steps_.size();
        steps_.insert(steps_.begin() + long(from), [this, again, to](float) {
            if (!*again) jumpTo(to + 1);
            return true;
        });
    } else if (verb == "unlatch") {
        run([this] { app_.unlatchForTest(); });
        wait(0.05f);
    } else if (verb == "snapshot_start") {
        run([this] {
            start_ = {app_.doc_.partCount(), app_.doc_.activePart, app_.doc_.mesh.positions, app_.doc_.mesh.triangles,
                      openEdges()};
        });
    } else if (verb == "undo_all") {
        auto undone = std::make_shared<int>(0);
        steps_.push_back([this, line, undone](float t) {
            if (t == 0.0f) *undone = 0;
            if (!app_.doc_.undo.canUndo()) return true;
            if (++*undone > 2000) {
                fail(line, "undo still has steps after 2000 undos");
                return true;
            }
            testKeyNow(SDLK_Z, SDL_KMOD_LCTRL); // as Ctrl+Z, one step a frame (the view catches up between)
            return false;
        });
        wait(0.1f);
    } else if (verb == "expect_restored") {
        run([this, line] {
            const Mesh& m = app_.doc_.mesh;
            std::string why;
            if (app_.doc_.partCount() != start_.parts) why = "Parts " + std::to_string(app_.doc_.partCount()) + ", were " + std::to_string(start_.parts);
            else if (app_.doc_.activePart != start_.active) why = "active Part " + std::to_string(app_.doc_.activePart) + ", was " + std::to_string(start_.active);
            else if (m.triangles != start_.triangles) why = "triangles " + std::to_string(m.triangles.size()) + ", were " + std::to_string(start_.triangles.size());
            else if (m.positions != start_.positions) {
                size_t moved = 0;
                for (size_t i = 0; i < m.positions.size(); ++i) moved += m.positions[i] != start_.positions[i];
                why = std::to_string(moved) + " vertices not where they started";
            }
            if (!why.empty()) fail(line, "undo back to the start didn't restore it: " + why);
        });
    } else if (verb == "expect_mesh_valid") {
        run([this, line] {
            const Mesh& m = app_.doc_.mesh;
            for (const glm::vec3& p : m.positions)
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return fail(line, "a vertex position is not finite");
            for (const glm::uvec3& t : m.triangles)
                if (t.x >= m.positions.size() || t.y >= m.positions.size() || t.z >= m.positions.size())
                    return fail(line, "a triangle names a vertex past the end");
            if (start_.openEdges == 0 && app_.doc_.activePart == start_.active && openEdges() != 0)
                fail(line, "the mesh was watertight and now has " + std::to_string(openEdges()) + " open edges");
        });
    } else if (verb == "mode_entry") {
        // mode_entry NAME | LINE | LINE ...: a way into a mode that isn't a registry action (Append > Insert's menu),
        // probed and tried by the next mode_matrix like an action
        const std::string rest = restOfLine(lineText_, 1);
        std::vector<std::string> parts;
        for (size_t at = 0;;) {
            const size_t bar = rest.find(" | ", at);
            parts.push_back(rest.substr(at, bar == std::string::npos ? std::string::npos : bar - at));
            if (bar == std::string::npos) break;
            at = bar + 3;
        }
        if (parts.size() < 2) {
            error = "'mode_entry' needs NAME | LINE [| LINE ...]";
            return false;
        }
        extraEntries_.emplace_back(parts[0], std::vector<std::string>(parts.begin() + 1, parts.end()));
    } else if (verb == "mode_matrix") {
        return buildModeMatrix(line, std::vector<std::string>(w.begin() + 1, w.end()), error);
    } else if (verb == "monkey") {
        if (w.size() < 3) {
            error = "'monkey' needs SEED STEPS [COUNT]";
            return false;
        }
        const uint64_t seed = std::strtoull(w[1].c_str(), nullptr, 10);
        const int steps = std::atoi(w[2].c_str()), count = w.size() > 3 && w[3].rfind("known=", 0) != 0 ? std::atoi(w[3].c_str()) : 1;
        for (const std::string& a : w)
            if (a.rfind("known=", 0) == 0 && !loadKnownFailures(a.substr(6), error)) return false;
        for (int k = 0; k < count; ++k)
            if (!buildMonkey(line, seed + uint64_t(k), steps, error)) return false;
    } else {
        handled = false;
    }
    return true;
}

double Demo::queryModes(const std::string& name, bool& known) const {
    known = true;
    if (name == "latched") return double(app_.latchedForTest().size());
    if (name == "undo_depth") return double(app_.doc_.undo.undoCount());
    auto count = [&](const char* prefix) {
        int n = 0;
        for (const std::string& s : app_.latchedForTest()) n += s.rfind(prefix, 0) == 0;
        return double(n);
    };
    if (name == "latched_modes") return count("mode: ");
    if (name == "input_captured") return count("input: ");
    if (name == "overlay_open") return count("overlay: ");
    if (name == "modes_found") {
        int n = 0;
        for (const auto& [id, mode] : isMode_) n += mode;
        return double(n);
    }
    if (name == "sculpted") return basePositions_.size() == app_.doc_.mesh.vertexCount() && basePositions_ == app_.doc_.mesh.positions ? 0.0 : 1.0;
    known = false;
    return 0.0;
}

int Demo::openEdges() const {
    bool known = false;
    return int(query("open_edges", known));
}

// The set-up every generated case starts from: a small sphere, the Standard brush, nothing latched.
// (wait_idle: a background job an earlier case started, Quad Remesh, would write its progress over the status bar)
static const std::vector<std::string> kReset = {"release", "hold none", "key escape", "unlatch", "wait_idle 120", "run new_sphere subdivisions=3",
                                                "action brush.standard", "action tool.draw", "unlatch", "wait 0.1"};

// tests/ui/mode_matrix_known.txt: "ID | EXIT | the fix task" per line. Those cases' failures are logged as KNOWN, not
// failed; one that passes now (or whose action is no longer a mode) fails, so the line goes when the fix lands.
bool Demo::loadKnownFailures(const std::string& file, std::string& error) {
    auto path = std::filesystem::path(std::u8string(file.begin(), file.end()));
    if (path.is_relative()) path = std::filesystem::path(std::u8string(scriptDir_.begin(), scriptDir_.end())) / path;
    std::ifstream f(path);
    if (!f) {
        error = "mode_matrix: cannot read " + file;
        return false;
    }
    std::string l;
    while (std::getline(f, l)) {
        if (l.empty() || l[0] == '#') continue;
        const size_t a = l.find(" | "), b = a == std::string::npos ? a : l.find(" | ", a + 3);
        if (b == std::string::npos) {
            error = file + ": \"" + l + "\" is not ID | EXIT | TASK";
            return false;
        }
        known_[l.substr(0, b)] = l.substr(b + 3);
    }
    return true;
}

bool Demo::buildModeMatrix(int line, const std::vector<std::string>& args, std::string& error) {
    std::vector<std::string> only;
    for (const std::string& a : args)
        if (a.rfind("known=", 0) == 0) {
            if (!loadKnownFailures(a.substr(6), error)) return false;
        } else {
            only.push_back(a);
        }
    if (!dialogs::suppressed()) { // (never: loadScript turns them off) a dialog action would open one on the desktop
        error = "mode_matrix: native dialogs are not suppressed";
        return false;
    }
    std::vector<const Action*> candidates;
    for (const Action& a : app_.actions_.all()) {
        if (!only.empty() ? std::find(only.begin(), only.end(), a.id) == only.end() : neverRun(a.id)) continue;
        if (!a.run || a.id == "brush.standard" || a.id == "tool.draw") continue; // (the reset's own)
        candidates.push_back(&a);
    }
    if (candidates.empty() && extraEntries_.empty()) {
        error = "mode_matrix: no actions to probe (the registry is empty, or no id matched)";
        return false;
    }
    // Exits: what a person does to get out, then what must hold. Esc, a brush and Draw land on a plain brush; Move /
    // Scale / Rotate on that tool (then Draw); a palette action and undo needn't leave the mode, but a brush after
    // them must (the mode can't be left half done).
    const std::string brushClay = "click_exit \"Clay##brush.clay\" brush_type " + std::to_string(int(BrushType::Clay));
    auto sculpts = [](const std::string& brush, const std::string& label) {
        std::vector<std::string> l = {"expect_latched none", "expect_value brush_type == " + brush};
        static const char* const kToolWords[] = {"Draw", "Move", "Scale", "Rotate", "Clay", "Standard"};
        if (std::none_of(std::begin(kToolWords), std::end(kToolWords), [&](const char* t) { return label == t; }))
            l.push_back("hover_model 0.5 0.5"), l.push_back("expect_status_not " + label); // (no shelf tile's hover text over it)
        l.push_back("expect_sculpts");
        return l;
    };
    const std::string kClay = std::to_string(int(BrushType::Clay));
    // What is probed: every candidate action, then the script's mode_entry ways in that aren't actions.
    struct Entry {
        std::string id, label;
        std::vector<std::string> enter, probe; // the lines that go in
        bool action = true;
    };
    std::vector<Entry> entries;
    for (const Action* a : candidates) entries.push_back({a->id, a->label, {"action " + a->id}, {"try_action " + a->id}, true});
    for (const auto& [name, lines] : extraEntries_) entries.push_back({name, name, lines, lines, false});
    // SF_MODE_MATRIX_SHARD=K/N: only every Nth entry from the Kth (CMake runs N shards side by side: one matrix took
    // 3-10 min on a CI runner, the longest test of every app T0).
    if (const char* sh = std::getenv("SF_MODE_MATRIX_SHARD"); sh && *sh) {
        int k = -1, n = 0;
        if (std::sscanf(sh, "%d/%d", &k, &n) != 2 || n < 1 || k < 0 || k >= n) {
            error = std::string("mode_matrix: SF_MODE_MATRIX_SHARD=") + sh + " is not K/N with 0 <= K < N";
            return false;
        }
        std::vector<Entry> mine;
        for (size_t i = 0; i < entries.size(); ++i)
            if (int(i % size_t(n)) == k) mine.push_back(std::move(entries[i]));
        SDL_Log("mode_matrix: shard %d/%d: %zu of %zu entries", k, n, mine.size(), entries.size());
        entries = std::move(mine);
    }
    for (const Entry& en : entries) {
        const std::string id = en.id, label = en.label;
        std::vector<std::string> enter = kReset;
        enter.insert(enter.end(), en.enter.begin(), en.enter.end());
        enter.push_back("remember brush_type entered"); // Esc, Draw ... keep the brush the action picked
        // Probe: does the action latch something, or stop a plain stroke from sculpting?
        std::vector<std::string> probe = kReset;
        probe.push_back("remember undo_depth before_action");
        probe.insert(probe.end(), en.probe.begin(), en.probe.end());
        probe.push_back("remember undo_depth after_action");
        probe.push_back("snapshot");
        probe.push_back(kStroke);
        probe.push_back("wait 0.1");
        // (the verdict, read right after the probe's stroke)
        auto dialogsBefore = std::make_shared<int>(0);
        auto windowsBefore = std::make_shared<std::set<std::string>>(); // (open before the action: left by a case)
        Step verdict = [this, line, id, dialogsBefore, windowsBefore](float) {
            if (dialogs::requested() != *dialogsBefore) { // it asks for a file: cancelled (NativeDialog.h), nothing to leave
                SDL_Log("mode_matrix: %s asks for a dialog: skipped", id.c_str());
                isMode_[id] = false;
                return true;
            }
            if (const ImGuiContext* g = ImGui::GetCurrentContext(); g && g->HoveredWindow && g->HoveredWindow->RootWindow &&
                                                                    windowsBefore->count(g->HoveredWindow->RootWindow->Name)) {
                // (open before this action ran: an earlier case left it, and it hides the model from every probe after)
                fail(line, std::string("the window \"") + g->HoveredWindow->RootWindow->Name +
                               "\", left open by an earlier case, covers the model: close it in App::unlatchForTest()");
                return true;
            }
            if (const ImGuiContext* g = ImGui::GetCurrentContext(); g && g->HoveredWindow) {
                // (a floating window over the stroke's path, Capture from view, New Pack: in the way, not a latch;
                // the model still sculpts beside it)
                SDL_Log("mode_matrix: %s opens a window over the model (%s): not a mode", id.c_str(), g->HoveredWindow->Name);
                isMode_[id] = false;
                return true;
            }
            bool known = false;
            const bool sculpted = queryModes("sculpted", known) > 0.5;
            const std::vector<std::string> latched = app_.latchedForTest();
            const bool brushPicked = app_.doc_.activeBrush >= 0 || app_.paintMode_ || app_.brush_ != BrushType::Standard;
            // (a brush that doesn't move vertices, Paint or Morph without a target, is a brush, not a mode)
            // (nor is an edit, one undo step that latches nothing: Mask All, then strokes move nothing masked)
            const bool edit = remembered_["after_action"] > remembered_["before_action"];
            const bool mode = !latched.empty() || (!sculpted && !brushPicked && !edit);
            isMode_[id] = mode;
            modalOnly_[id] = mode && std::all_of(latched.begin(), latched.end(), [](const std::string& l) {
                return l.rfind("overlay: modal ", 0) == 0;
            }) && !latched.empty();
            if (mode) {
                std::string what;
                for (const std::string& s : latched) what += (what.empty() ? "" : ", ") + s;
                SDL_Log("mode_matrix: %s is a mode (%s)", id.c_str(), what.empty() ? "a plain stroke doesn't sculpt" : what.c_str());
            }
            return true;
        };
        auto enabled = [this, id, dialogsBefore, windowsBefore, action = en.action] {
            const Action* x = app_.actions_.find(id);
            isMode_[id] = false;
            *dialogsBefore = dialogs::requested();
            windowsBefore->clear();
            for (const ImGuiWindow* win : ImGui::GetCurrentContext()->Windows)
                if (win->Active && !win->Hidden && !(win->Flags & ImGuiWindowFlags_ChildWindow)) windowsBefore->insert(win->Name);
            return !action || (x && (!x->enabled || x->enabled()));
        };
        if (!addCase(line, "probe " + id, enabled, probe, error, verdict)) return false;
        const std::vector<ModeExit> exits = {
            {"Esc", {"key escape"}},
            {"a brush on the shelf", {brushClay}},
            {"Draw", {"click_exit \"Draw##tool.draw\" gizmo_mode 0"}},
            {"Move", {"click_exit \"Move##tool.move\" gizmo_mode 1", "expect_value gizmo_mode == 1", "expect_latched mode: Move", "click \"Draw##tool.draw\""}},
            {"Scale", {"click_exit \"Scale##tool.scale\" gizmo_mode 2", "expect_value gizmo_mode == 2", "expect_latched mode: Scale", "click \"Draw##tool.draw\""}},
            {"Rotate", {"click_exit \"Rotate##tool.rotate\" gizmo_mode 3", "expect_value gizmo_mode == 3", "expect_latched mode: Rotate", "click \"Draw##tool.draw\""}},
            {"a palette action", {"action view.front", brushClay}},
            {"undo", {"key ctrl+z", brushClay}},
        };
        for (const ModeExit& e : exits) {
            if ((id == "tool.move" && std::string(e.name) == "Move") || (id == "tool.scale" && std::string(e.name) == "Scale") ||
                (id == "tool.rotate" && std::string(e.name) == "Rotate"))
                continue; // (the same tool again goes back to Draw: the Draw exit's case)
            std::vector<std::string> lines = enter;
            lines.insert(lines.end(), e.lines.begin(), e.lines.end());
            const bool toClay = e.lines.back() == brushClay;
            const auto check = sculpts(toClay ? kClay : "$entered", label);
            lines.insert(lines.end(), check.begin(), check.end());
            // (a modal dialog takes no clicks outside it, by design: Esc is its way out)
            const bool esc = std::string(e.name) == "Esc";
            if (!addCase(line, "mode " + id + " (" + label + "), exit " + e.name,
                         [this, id, esc] { return isMode_[id] && (esc || !modalOnly_[id]); }, lines, error,
                         {}, id + " | " + e.name))
                return false;
        }
    }
    // Known failures that didn't fail: fixed (or gone), so their lines go.
    std::set<std::string> probed;
    for (const Entry& en : entries) probed.insert(en.id);
    run([this, line, probed] {
        for (const auto& [key, task] : known_) {
            if (!probed.count(key.substr(0, key.find(" | ")))) continue;
            if (knownHit_.count(key)) continue;
            fail(line, knownRan_.count(key) ? "known failure \"" + key + "\" passes now: remove it from the known list (" + task + ")"
                                            : "known failure \"" + key + "\" didn't run (no longer a mode?): remove it (" + task + ")");
        }
    });
    return true;
}

bool Demo::buildMonkey(int line, uint64_t seed, int count, std::string& error) {
    if (!dialogs::suppressed()) {
        error = "monkey: native dialogs are not suppressed";
        return false;
    }
    Rng rng{seed * 0x2545F4914F6CDD1Dull + 1};
    std::vector<std::string> brushes, toggles;
    for (const Action& a : app_.actions_.all()) {
        if (neverRun(a.id) || !a.run || !a.checked) continue;
        // (known=FILE: actions whose modes have filed failures are left out: the monkey looks for new ones)
        if (std::any_of(known_.begin(), known_.end(), [&](const auto& k) { return k.first.rfind(a.id + " | ", 0) == 0; })) continue;
        (a.id.rfind("brush.", 0) == 0 ? brushes : toggles).push_back(a.id);
    }
    if (brushes.empty()) {
        error = "monkey: no brushes in the registry";
        return false;
    }
    std::vector<std::string> lines = {"release", "hold none", "unlatch", "run new_sphere subdivisions=3", "action brush.standard",
                                      "action tool.draw", "unlatch", "wait 0.1", "snapshot_start"};
    static const char* const kViews[] = {"key numpad1", "key numpad3", "key numpad7", "orbit 60 0", "orbit 0 40", "orbit -80 20",
                                         "camera 30 15", "key f"};
    for (int i = 0; i < count; ++i) {
        const int r = rng.below(100);
        if (r < 30) {
            lines.push_back("stroke " + fmt(rng.frac(0.2f, 0.8f)) + " " + fmt(rng.frac(0.2f, 0.8f)) + " " + fmt(rng.frac(0.2f, 0.8f)) +
                            " " + fmt(rng.frac(0.2f, 0.8f)));
        } else if (r < 45) {
            lines.push_back("maybe_action " + brushes[size_t(rng.below(int(brushes.size())))]);
        } else if (r < 62 && !toggles.empty()) {
            lines.push_back("maybe_action " + toggles[size_t(rng.below(int(toggles.size())))]);
        } else if (r < 72) {
            lines.push_back("key ctrl+z");
        } else if (r < 77) {
            lines.push_back("key ctrl+shift+z");
        } else if (r < 92) {
            lines.push_back(kViews[rng.below(int(std::size(kViews)))]);
        } else {
            lines.push_back("key escape");
        }
    }
    // The way out, then the invariants.
    for (const char* l : {"release", "hold none", "key escape", "action brush.clay", "action tool.draw", "expect_latched none",
                          "expect_mesh_valid", "expect_sculpts", "undo_all", "expect_restored", "expect_mesh_valid"})
        lines.emplace_back(l);
    return addCase(line, "monkey " + std::to_string(seed), nullptr, lines, error);
}

} // namespace sf
