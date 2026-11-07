// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/MayaCommands.h"

#include "io/BridgeCommands.h"
#include "io/MayaBridge.h"

namespace sf {

size_t mayaDropUnchanged(const Document& doc, std::vector<MeshObject>& objects) {
    return std::erase_if(objects, [&](const MeshObject& o) {
        for (int i = 0; i < doc.partCount(); ++i) {
            const std::string& bridge = i == doc.activePart ? doc.bridgeName : doc.parts[size_t(i)].bridgeName;
            if (bridge == o.name) return mayaMeshesMatch(doc.partMesh(i), o.mesh);
        }
        return false;
    });
}

void registerMayaCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;

    reg.add({"maya_send",
             "Send the visible Parts to Maya through the exchange folder the SculptForge shelf script reads "
             "(OBJ with quads and UVs, plus a manifest). Objects that came from Maya replace their mesh there by "
             "name; the others become new objects.",
             {{"folder", T::String, "The exchange folder (the same one chosen in Maya's shelf script).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<int> indices;
                 const auto objects = bridgeObjects(doc, indices);
                 std::vector<std::string> sent;
                 std::string msg;
                 if (!mayaSend(objects, argString(a, "folder"), msg, &sent)) return {false, msg};
                 // Maya's next send of these objects updates the same Parts.
                 for (size_t k = 0; k < sent.size() && k < indices.size(); ++k) partBridgeName(doc, indices[k]) = sent[k];
                 return {true, msg};
             }});

    reg.add({"maya_receive",
             "Load what Maya last sent with the SculptForge shelf script. Each object replaces the Part with its "
             "name (an undoable step on that Part) or becomes a new Part; the first object replaces a fresh "
             "scene. Objects that match their Part already (sent back unchanged) are skipped.",
             {{"folder", T::String, "The exchange folder (the same one chosen in Maya's shelf script).", true}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<MeshObject> objects;
                 std::string msg;
                 if (!mayaReceive(argString(a, "folder"), objects, nullptr, msg)) return {false, msg};
                 // What comes back as it went out (Live's echo, or a send of untouched objects) is no new step.
                 const size_t received = objects.size();
                 mayaDropUnchanged(doc, objects);
                 if (objects.empty()) return {true, "Maya sent " + std::string(received == 1 ? "it" : "them") +
                                                        " back unchanged: nothing to update"};
                 receiveObjects(doc, objects, "Get from Maya", true);
                 return {true, msg};
             }});
}

} // namespace sf
