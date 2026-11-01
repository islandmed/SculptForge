// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/MayaCommands.h"

#include "io/GozCommands.h"
#include "io/MayaBridge.h"

namespace sf {

void registerMayaCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;

    reg.add({"maya_send",
             "Send the visible Parts to Maya through the exchange folder the SculptForge shelf script reads "
             "(OBJ with quads and UVs, plus a manifest). Objects that came from Maya replace their mesh there by "
             "name; the others become new objects.",
             {{"folder", T::String, "The exchange folder (the same one chosen in Maya's shelf script).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<int> indices;
                 const auto objects = gozObjects(doc, indices);
                 std::vector<std::string> sent;
                 std::string msg;
                 if (!mayaSend(objects, argString(a, "folder"), msg, &sent)) return {false, msg};
                 // Maya's next send of these objects updates the same SubTools.
                 for (size_t k = 0; k < sent.size() && k < indices.size(); ++k) subtoolGozName(doc, indices[k]) = sent[k];
                 return {true, msg};
             }});

    reg.add({"maya_receive",
             "Load what Maya last sent with the SculptForge shelf script. Each object replaces the Part with its "
             "name (an undoable step on that Part) or becomes a new Part; the first object replaces a fresh "
             "scene.",
             {{"folder", T::String, "The exchange folder (the same one chosen in Maya's shelf script).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<MeshObject> objects;
                 std::string msg;
                 if (!mayaReceive(argString(a, "folder"), objects, nullptr, msg)) return {false, msg};
                 receiveObjects(doc, objects, "Get from Maya", true);
                 return {true, msg};
             }});
}

} // namespace sf
