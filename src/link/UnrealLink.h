// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// "Send to Unreal": pushes an exported FBX into a running Unreal Editor
// through Unreal's built-in Python remote execution (the same channel Epic's
// own tools use). No plugin to install - the project needs the Python Editor
// Script Plugin enabled and "Enable Remote Execution" ticked in
// Project Settings > Plugins > Python.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sf {
struct PaintTexture;
}

namespace sf::link {

struct UnrealEndpoint {
    std::string multicastGroup = "239.0.0.1";
    uint16_t multicastPort = 6766;
    std::string multicastBind = "127.0.0.1"; // multicast interface; must match Unreal's "Multicast Bind Address"
    int multicastTtl = 0;                   // 0 = this machine only; 1 = editors on the local network
    std::string commandAddress;             // empty = pick automatically (127.0.0.1, or our LAN address)

    // Settings for an Unreal Editor on another computer (it must use the same TTL and bind address).
    static UnrealEndpoint network() {
        UnrealEndpoint e;
        e.multicastTtl = 1;
        e.multicastBind = "0.0.0.0";
        return e;
    }
};

struct UnrealEditor {
    std::string nodeId;
    std::string projectName;
    std::string projectRoot;
    std::string engineVersion;
    std::string machine;
    std::string user;
    std::string address; // where its discovery reply came from
    bool local() const;  // running on this computer
};

// Finds running editors with remote execution enabled.
std::vector<UnrealEditor> findUnrealEditors(const UnrealEndpoint& endpoint, int timeoutMs, std::string& error);

// Runs Python in the editor. `output` collects what the script printed.
bool runInUnreal(const UnrealEndpoint& endpoint, const UnrealEditor& editor, const std::string& python,
                 std::string& output, std::string& error, int timeoutMs = 60000);

struct UnrealImport {
    std::string fbxPath;                         // file on this machine
    std::string downloadUrl;                     // if set, the editor fetches the file from here (other computer)
    std::string destination = "/Game/SculptForge"; // content folder
    std::string assetName;                       // e.g. SM_Rock
    bool skeletal = false;                       // needed for morph targets (blend shapes)
    bool lods = false;                           // file has an LOD group
    // A baked tangent-space normal map (core/Bake.h) imported beside the mesh as T_<name>_N, set up as a
    // normal map (linear, BC5). Only for an editor on this computer. `normalMapDirectX`: the map is already
    // green-down; an OpenGL map gets Unreal's Flip Green Channel.
    std::string normalMapPath;
    bool normalMapDirectX = false;
    // Material maps of a texture with material channels (export_maps' "unreal" preset): T_<name>_BC (sRGB),
    // T_<name>_ORM (linear, masks), T_<name>_E, with the normal above as T_<name>_N. With `buildMaterial` the
    // script then builds M_<name> from them (our own Python, Unreal's MaterialEditingLibrary) and assigns it to
    // the mesh. Only for an editor on this computer.
    std::string baseColorPath, ormPath, emissivePath;
    bool buildMaterial = false;
};
// Python that imports (or reimports, replacing) the asset and prints a result line.
std::string unrealImportScript(const UnrealImport& import);
// Exports the texture's maps with the "unreal" preset into `folder` and fills the map paths of `import` (and
// buildMaterial). Does nothing (true) for a texture without material channels.
bool addUnrealMaterialMaps(const PaintTexture& texture, const std::string& folder, UnrealImport& import, std::string& error);

// Finds an editor (the first one, or the one whose project name matches) and
// imports. `message` is a human-readable result either way.
bool sendToUnreal(const UnrealEndpoint& endpoint, const UnrealImport& import, std::string& message,
                  const std::string& projectName = {});

// Protocol messages (exposed for tests).
std::string unrealMessage(const std::string& type, const std::string& source, const std::string& dest,
                          const std::string& dataJson);

} // namespace sf::link
