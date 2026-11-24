// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/BlenderCommands.h"

#include "io/BlenderBridge.h"
#include "io/BridgeCommands.h"

namespace sf {

void registerBlenderCommands(CommandRegistry& reg) {
    using T = ParamSpec::Type;

    reg.add({"blender_send",
             "Send the visible Parts to Blender through the SculptForge add-on's exchange folder. Objects that "
             "came from Blender update in place there (keeping their transform, materials and modifiers); the "
             "others become new objects. Loaded Gaussian splats go along as a splat object.",
             {{"folder", T::String, "Exchange folder. Default: the per-user one the add-on uses.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 const std::string root = argString(a, "folder", blenderExchangeRoot());
                 std::vector<int> indices;
                 const auto objects = bridgeObjects(doc, indices);
                 std::vector<std::string> sent;
                 std::string msg;
                 if (!blenderSend(objects, root, msg, &sent, nullptr, &doc.splats)) return {false, msg};
                 // Blender's next send of these objects updates the same Parts.
                 for (size_t k = 0; k < sent.size() && k < indices.size(); ++k) partBridgeName(doc, indices[k]) = sent[k];
                 return {true, msg};
             }});

    reg.add({"blender_receive",
             "Load what Blender last sent with the SculptForge add-on. Each object updates the Part it was sent "
             "from (an undoable step on that Part) or becomes a new Part, placed as it is in Blender's scene; "
             "the first object replaces a fresh scene. A Gaussian splat object (Blender 5.3+) loads as the splat "
             "capture next to the sculpt, spherical harmonics and placement kept.",
             {{"folder", T::String, "Exchange folder. Default: the per-user one the add-on uses.", false}},
             [](Document& doc, const Args& a) -> CommandResult {
                 std::vector<MeshObject> objects;
                 std::string msg;
                 SplatCloud splats;
                 bool gotSplats = false;
                 if (!blenderReceive(argString(a, "folder", blenderExchangeRoot()), objects, nullptr, msg, &splats, &gotSplats))
                     return {false, msg};
                 if (!objects.empty()) receiveObjects(doc, objects, "Receive from Blender", true);
                 if (gotSplats) { // the capture next to the sculpt, as when a PLY of splats is opened
                     doc.splats = std::move(splats);
                     doc.splatsChanged();
                 }
                 return {true, msg};
             }});
}

} // namespace sf
