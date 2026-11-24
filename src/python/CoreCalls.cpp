// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// CoreCalls.h over the core this binary links. Built into the app, the module for pipelines and the tests; never
// into the app's Python host.
#include "python/CoreCalls.h"

#include "ai/AiCommands.h"
#include "ai/AiPolicy.h"
#include "ai/SplatTrainer.h"
#include "core/BrushLibrary.h"
#include "core/Document.h"
#include "core/Multires.h"
#include "core/Script.h"
#include "core/SelectionSets.h"

namespace sf::python {

namespace {

// The core commands, with the AI commands Preferences > AI allows.
const CommandRegistry& standalone() {
    static CommandRegistry r = [] {
        CommandRegistry reg;
        ai::setAiEnabled(ai::aiFeaturesAllowed(ai::AiSettings::load(ai::defaultAiSettingsPath()).enabled)); // Preferences > AI, SF_AI
        ai::registerAiCommands(reg, [] { return ai::AiSettings::load(ai::defaultAiSettingsPath()); });
        ai::registerSplatTrainCommands(reg, [] { return ai::SplatTrainSettings::load(ai::defaultSplatTrainSettingsPath()); });
        return reg;
    }();
    return r;
}

template <class F>
bool noThrow(std::string& err, F&& f) {
    try {
        return f();
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

} // namespace

const CoreCalls& directCoreCalls() {
    static const CoreCalls calls{
        [](std::vector<std::string>& warnings) {
            auto doc = std::make_unique<Document>();
            warnings = loadBrushLibrary(*doc); // with the user's brush library and SCULPTFORGE_BRUSH_PATH studio brushes
            return doc;
        },
        [](Document& doc, const std::string& name, const Args& args) {
            try {
                return standalone().execute(doc, name, args);
            } catch (const std::exception& e) {
                return CommandResult{false, e.what()};
            }
        },
        []() -> const CommandRegistry& { return standalone(); },
        [](const CommandRegistry& r) { return r.toolSchemaJson(); },
        [](Document& d, Mesh&& m) { d.setMesh(std::move(m)); },
        [](Mesh& m) {
            m.rebuildTopology();
            m.recomputeAllNormals();
        },
        [](Mesh& m) { m.markAllDirty(); },
        [](const Document& d, const std::string& name) { return d.findPart(name); },
        [](Document& d, int i) { d.selectPart(i); },
        [](const std::string& text, std::vector<ScriptStatement>& out, std::string& err) {
            return noThrow(err, [&] { return parseScript(text, out, err); });
        },
        [](const std::string& text, uint32_t& bits, std::string& err) { return parseSetChannels(text, bits, err); },
        [](const SelectionSetList& l, const std::string& name) { return l.find(name); },
        [](const Mesh& m, const SelectionSet& s) { return selectionSetFits(m, s); },
        [](const Mesh& m, const SelectionSet& s, uint32_t channels, SelectionSetData& out, std::string& err) {
            return noThrow(err, [&] { return selectionSetAt(m, s, channels, out, err); });
        },
        [](const std::string& name, const SelectionSetData& data, uint32_t nv, uint32_t nt, int level, SelectionSet& out,
           std::string& msg) { return noThrow(msg, [&] { return makeSelectionSet(name, data, nv, nt, level, out, msg); }); },
        [](const Mesh& m) { return subdivCurrentLevel(m); },
        [](Document& d, SelectionSet&& s, const std::string& label, std::string& msg) {
            return noThrow(msg, [&] { return addSelectionSet(d, std::move(s), label, msg); });
        },
        [](const Mesh& m) { return subdivLevelCount(m); },
    };
    return calls;
}

} // namespace sf::python
