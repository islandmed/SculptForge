// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link/UnrealLink.h"

#include "core/Json.h"
#include "core/MapExport.h"
#include "core/Texture.h"
#include "link/Socket.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <thread>

namespace sf::link {

namespace {

constexpr int kProtocolVersion = 1;
constexpr const char* kProtocolMagic = "ue_py";

std::string newNodeId() {
    std::random_device rd;
    std::mt19937_64 rng((uint64_t(rd()) << 32) ^ rd() ^ uint64_t(std::chrono::steady_clock::now().time_since_epoch().count()));
    const char* hex = "0123456789abcdef";
    std::string id;
    for (int i = 0; i < 32; ++i) {
        if (i == 8 || i == 12 || i == 16 || i == 20) id += '-';
        id += hex[rng() & 15];
    }
    return id;
}

bool parseMessage(const std::string& text, Json& msg) {
    std::string err;
    return Json::parse(text, msg, err) && msg["magic"].asString() == kProtocolMagic &&
           int(msg["version"].asNumber()) == kProtocolVersion;
}

// Answers GET /<token>/... with `data` (once), until `done`.
void serveFileOnce(Socket& listener, const std::string& token, const std::string& data, const std::atomic<bool>& done) {
    while (!done) {
        Socket client;
        if (!listener.accept(client, 200)) continue;
        std::string request, chunk;
        while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192 && client.receiveSome(chunk, 4096, 5000))
            request += chunk;
        std::string err;
        if (request.rfind("GET /" + token + "/", 0) != 0) {
            client.sendAll("HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n", err);
            continue;
        }
        client.sendAll("HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
                           std::to_string(data.size()) + "\r\nConnection: close\r\n\r\n",
                       err);
        client.sendAll(data, err);
        return;
    }
}

// Python string literal (repr-safe) for embedding paths and names in the import script.
std::string pyString(const std::string& s) {
    std::string out = "'";
    for (unsigned char c : s) {
        if (c == '\\' || c == '\'') {
            out += '\\';
            out += char(c);
        } else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\x%02x", c);
            out += buf;
        } else {
            out += char(c);
        }
    }
    return out + "'";
}

} // namespace

std::string unrealMessage(const std::string& type, const std::string& source, const std::string& dest,
                          const std::string& dataJson) {
    std::string m = "{\"version\":" + std::to_string(kProtocolVersion) + ",\"magic\":\"" + kProtocolMagic +
                    "\",\"type\":" + jsonQuote(type) + ",\"source\":" + jsonQuote(source);
    if (!dest.empty()) m += ",\"dest\":" + jsonQuote(dest);
    if (!dataJson.empty()) m += ",\"data\":" + dataJson;
    return m + "}";
}

bool UnrealEditor::local() const {
    if (address.empty() || address.rfind("127.", 0) == 0) return true;
    std::string a = machine, b = hostName();
    for (auto* t : {&a, &b})
        for (char& c : *t) c = char(std::tolower(static_cast<unsigned char>(c)));
    return !a.empty() && a == b;
}

std::vector<UnrealEditor> findUnrealEditors(const UnrealEndpoint& e, int timeoutMs, std::string& error) {
    std::vector<UnrealEditor> editors;
    Socket udp;
    if (!udp.openMulticast(e.multicastGroup, e.multicastPort, e.multicastBind, e.multicastTtl, error)) return editors;
    const std::string self = newNodeId();
    if (!udp.sendTo(unrealMessage("ping", self, "", ""), e.multicastGroup, e.multicastPort, error)) return editors;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (true) {
        const int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        if (left <= 0) break;
        std::string data, from;
        if (!udp.receive(data, left, &from)) continue;
        Json msg;
        if (!parseMessage(data, msg) || msg["type"].asString() != "pong" || msg["dest"].asString() != self) continue;
        UnrealEditor ed;
        ed.nodeId = msg["source"].asString();
        const Json& d = msg["data"];
        ed.projectName = d["project_name"].asString();
        ed.projectRoot = d["project_root"].asString();
        ed.engineVersion = d["engine_version"].asString();
        ed.machine = d["machine"].asString();
        ed.user = d["user"].asString();
        ed.address = from;
        bool known = false;
        for (const auto& x : editors) known = known || x.nodeId == ed.nodeId;
        if (!known && !ed.nodeId.empty()) editors.push_back(ed);
    }
    return editors;
}

bool runInUnreal(const UnrealEndpoint& e, const UnrealEditor& editor, const std::string& python, std::string& output,
                 std::string& error, int timeoutMs) {
    Socket udp;
    if (!udp.openMulticast(e.multicastGroup, e.multicastPort, e.multicastBind, e.multicastTtl, error)) return false;
    // The editor connects back to us for the command channel.
    std::string commandIp = e.commandAddress;
    if (commandIp.empty()) commandIp = editor.local() ? "127.0.0.1" : localAddressToward(editor.address);
    if (commandIp.empty()) {
        error = "Cannot find a network route to " + editor.machine;
        return false;
    }
    Socket server;
    uint16_t port = 0;
    if (!server.listen(commandIp, 0, port, error)) return false;
    const std::string self = newNodeId();
    const std::string open = "{\"command_ip\":" + jsonQuote(commandIp) + ",\"command_port\":" + std::to_string(port) + "}";
    if (!udp.sendTo(unrealMessage("open_connection", self, editor.nodeId, open), e.multicastGroup, e.multicastPort, error))
        return false;
    Socket channel;
    bool connected = false;
    for (int attempt = 0; attempt < 3 && !connected; ++attempt) {
        if (attempt > 0)
            udp.sendTo(unrealMessage("open_connection", self, editor.nodeId, open), e.multicastGroup, e.multicastPort, error);
        connected = server.accept(channel, 4000);
    }
    if (!connected) {
        error = "Unreal didn't connect back to " + commandIp + ":" + std::to_string(port) +
                " (a firewall on this computer may be blocking it)";
        return false;
    }
    Json data = Json::object();
    data.set("command", python);
    data.set("unattended", true);
    data.set("exec_mode", "ExecuteFile");
    bool ok = channel.sendAll(unrealMessage("command", self, editor.nodeId, data.dump()), error);
    std::string reply;
    Json result;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (ok) {
        const int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        std::string chunk;
        if (left <= 0) {
            error = "Unreal didn't answer in time (the import may still be running)";
            ok = false;
            break;
        }
        if (!channel.receiveSome(chunk, 1 << 16, left)) {
            if (!channel.valid()) break;
            continue;
        }
        reply += chunk;
        // The reply is one JSON object with no framing: wait until it parses.
        if (parseMessage(reply, result) && result["type"].asString() == "command_result") break;
        if (reply.size() > (64u << 20)) {
            error = "Unreasonably large reply from Unreal";
            ok = false;
            break;
        }
    }
    udp.sendTo(unrealMessage("close_connection", self, editor.nodeId, ""), e.multicastGroup, e.multicastPort, error);
    if (!ok) return false;
    const Json& d = result["data"];
    for (const auto& line : d["output"].items()) output += line["output"].asString() + "\n";
    if (d["result"].isString() && d["result"].asString() != "None") output += d["result"].asString() + "\n";
    if (!d["success"].asBool(false)) {
        error = output.empty() ? "The Python command failed in Unreal" : output;
        return false;
    }
    return true;
}

std::string unrealImportScript(const UnrealImport& im) {
    std::string py;
    py += "import unreal\n";
    std::string fileExpr = pyString(im.fbxPath);
    if (!im.downloadUrl.empty()) {
        // The editor is on another computer: fetch the file from SculptForge into its temp folder.
        const std::string name = std::filesystem::path(std::u8string(im.fbxPath.begin(), im.fbxPath.end())).filename().string();
        py += "import os, tempfile, urllib.request\n";
        py += "folder = os.path.join(tempfile.gettempdir(), 'sculptforge')\n";
        py += "os.makedirs(folder, exist_ok=True)\n";
        py += "fbx_path = os.path.join(folder, " + pyString(name) + ")\n";
        py += "with urllib.request.urlopen(" + pyString(im.downloadUrl) + ", timeout=120) as r, open(fbx_path, 'wb') as f:\n";
        py += "    f.write(r.read())\n";
        fileExpr = "fbx_path";
    }
    // Material maps (export_maps' "unreal" preset) are imported first, then the mesh, then the material.
    const bool localFiles = im.downloadUrl.empty();
    py += "sf_textures = {}\n";
    const std::string base = im.assetName.rfind("SM_", 0) == 0 || im.assetName.rfind("SK_", 0) == 0 ? im.assetName.substr(3) : im.assetName;
    struct Map {
        const std::string* path;
        const char* key;
        const char* compression;
        bool srgb;
    };
    const Map maps[] = {{&im.baseColorPath, "BC", "TC_DEFAULT", true},
                        {&im.ormPath, "ORM", "TC_MASKS", false},
                        {&im.emissivePath, "E", "TC_DEFAULT", true}};
    for (const Map& m : maps) {
        if (m.path->empty() || !localFiles) continue;
        const std::string texName = "T_" + base + "_" + m.key;
        py += "try:\n";
        py += "    ttask = unreal.AssetImportTask()\n";
        py += "    ttask.set_editor_property('filename', " + pyString(*m.path) + ")\n";
        py += "    ttask.set_editor_property('destination_path', " + pyString(im.destination) + ")\n";
        py += "    ttask.set_editor_property('destination_name', " + pyString(texName) + ")\n";
        py += "    ttask.set_editor_property('replace_existing', True)\n";
        py += "    ttask.set_editor_property('automated', True)\n";
        py += "    ttask.set_editor_property('save', True)\n";
        py += "    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([ttask])\n";
        py += "    for p in list(ttask.get_editor_property('imported_object_paths') or []):\n";
        py += "        tex = unreal.load_asset(p)\n";
        py += "        if isinstance(tex, unreal.Texture2D):\n";
        py += std::string("            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.") + m.compression + ")\n";
        py += std::string("            tex.set_editor_property('srgb', ") + (m.srgb ? "True" : "False") + ")\n";
        py += "            unreal.EditorAssetLibrary.save_loaded_asset(tex)\n";
        py += std::string("            sf_textures['") + m.key + "'] = tex\n";
        py += "            print('SCULPTFORGE_TEXTURE ' + p)\n";
        py += "except Exception as e:\n";
        py += std::string("    unreal.log_warning('SculptForge: ") + m.key + " map import failed (' + str(e) + ')')\n";
    }
    if (!im.normalMapPath.empty() && im.downloadUrl.empty()) {
        // The baked normal map first (its result line comes before the mesh's, which the caller reads last).
        std::string texName = im.assetName.rfind("SM_", 0) == 0 || im.assetName.rfind("SK_", 0) == 0 ? im.assetName.substr(3) : im.assetName;
        texName = "T_" + texName + "_N";
        py += "try:\n";
        py += "    ttask = unreal.AssetImportTask()\n";
        py += "    ttask.set_editor_property('filename', " + pyString(im.normalMapPath) + ")\n";
        py += "    ttask.set_editor_property('destination_path', " + pyString(im.destination) + ")\n";
        py += "    ttask.set_editor_property('destination_name', " + pyString(texName) + ")\n";
        py += "    ttask.set_editor_property('replace_existing', True)\n";
        py += "    ttask.set_editor_property('automated', True)\n";
        py += "    ttask.set_editor_property('save', True)\n";
        py += "    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([ttask])\n";
        py += "    for p in list(ttask.get_editor_property('imported_object_paths') or []):\n";
        py += "        tex = unreal.load_asset(p)\n";
        py += "        if isinstance(tex, unreal.Texture2D):\n";
        py += "            tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)\n";
        py += "            tex.set_editor_property('srgb', False)\n";
        py += std::string("            tex.set_editor_property('flip_green_channel', ") + (im.normalMapDirectX ? "False" : "True") + ")\n";
        py += "            unreal.EditorAssetLibrary.save_loaded_asset(tex)\n";
        py += "            sf_textures['N'] = tex\n";
        py += "            print('SCULPTFORGE_TEXTURE ' + p)\n";
        py += "except Exception as e:\n";
        py += "    unreal.log_warning('SculptForge: normal map import failed (' + str(e) + ')')\n";
    }
    py += "task = unreal.AssetImportTask()\n";
    py += "task.set_editor_property('filename', " + fileExpr + ")\n";
    py += "task.set_editor_property('destination_path', " + pyString(im.destination) + ")\n";
    py += "task.set_editor_property('destination_name', " + pyString(im.assetName) + ")\n";
    py += "task.set_editor_property('replace_existing', True)\n";
    py += "task.set_editor_property('automated', True)\n";
    py += "task.set_editor_property('save', True)\n";
    py += "ui = unreal.FbxImportUI()\n";
    py += "ui.set_editor_property('import_mesh', True)\n";
    py += "ui.set_editor_property('import_materials', True)\n";
    py += "ui.set_editor_property('import_textures', False)\n";
    py += "ui.set_editor_property('import_animations', False)\n";
    if (im.skeletal) {
        py += "ui.set_editor_property('import_as_skeletal', True)\n";
        py += "ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_SKELETAL_MESH)\n";
        py += "ui.skeletal_mesh_import_data.set_editor_property('import_morph_targets', True)\n";
    } else {
        py += "ui.set_editor_property('import_as_skeletal', False)\n";
        py += "ui.set_editor_property('mesh_type_to_import', unreal.FBXImportType.FBXIT_STATIC_MESH)\n";
        py += "ui.static_mesh_import_data.set_editor_property('import_mesh_lo_ds', " + std::string(im.lods ? "True" : "False") + ")\n";
        py += "ui.static_mesh_import_data.set_editor_property('combine_meshes', True)\n";
    }
    py += "ui.set_editor_property('automated_import_should_detect_type', False)\n";
    py += "task.set_editor_property('options', ui)\n";
    // Use the FBX factory directly so these options apply as-is (UE 5.5+ would otherwise route
    // through Interchange, which ignores the asset name and maps only some options).
    // (The factory reads its options from task.options.)
    py += "try:\n";
    py += "    task.set_editor_property('factory', unreal.FbxFactory())\n";
    py += "except Exception as e:\n";
    py += "    unreal.log_warning('SculptForge: using the default importer (' + str(e) + ')')\n";
    py += "unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])\n";
    py += "paths = list(task.get_editor_property('imported_object_paths') or [])\n";
    py += "print('SCULPTFORGE_IMPORTED ' + (', '.join(paths) if paths else 'nothing'))\n";
    if (im.buildMaterial && localFiles) {
        // M_<name> from the imported maps: base colour, normal, and occlusion / roughness / metallic from the
        // ORM map's R / G / B, assigned to the mesh's first material slot. Built with Unreal's
        // MaterialEditingLibrary, so it is an ordinary editable material.
        const std::string matName = "M_" + base;
        py += "try:\n";
        py += "    mel = unreal.MaterialEditingLibrary\n";
        py += "    mat_path = " + pyString(im.destination + "/" + matName) + "\n";
        py += "    if unreal.EditorAssetLibrary.does_asset_exist(mat_path):\n";
        py += "        mat = unreal.load_asset(mat_path)\n";
        py += "        mel.delete_all_material_expressions(mat)\n";
        py += "    else:\n";
        py += "        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(" + pyString(matName) + ", " +
              pyString(im.destination) + ", unreal.Material, unreal.MaterialFactoryNew())\n";
        py += "    def sf_sample(key, x, y, sampler):\n";
        py += "        node = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, x, y)\n";
        py += "        node.set_editor_property('parameter_name', key)\n";
        py += "        node.set_editor_property('texture', sf_textures[key])\n";
        py += "        node.set_editor_property('sampler_type', sampler)\n";
        py += "        return node\n";
        py += "    if 'BC' in sf_textures:\n";
        py += "        mel.connect_material_property(sf_sample('BC', -600, -300, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR), 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)\n";
        py += "    if 'ORM' in sf_textures:\n";
        py += "        orm = sf_sample('ORM', -600, 0, unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)\n";
        py += "        mel.connect_material_property(orm, 'R', unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)\n";
        py += "        mel.connect_material_property(orm, 'G', unreal.MaterialProperty.MP_ROUGHNESS)\n";
        py += "        mel.connect_material_property(orm, 'B', unreal.MaterialProperty.MP_METALLIC)\n";
        py += "    if 'N' in sf_textures:\n";
        py += "        mel.connect_material_property(sf_sample('N', -600, 300, unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL), 'RGB', unreal.MaterialProperty.MP_NORMAL)\n";
        py += "    if 'E' in sf_textures:\n";
        py += "        mel.connect_material_property(sf_sample('E', -600, 600, unreal.MaterialSamplerType.SAMPLERTYPE_COLOR), 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR)\n";
        py += "    mel.recompile_material(mat)\n";
        py += "    unreal.EditorAssetLibrary.save_loaded_asset(mat)\n";
        py += "    for p in paths:\n";
        py += "        mesh = unreal.load_asset(p)\n";
        py += "        if isinstance(mesh, unreal.StaticMesh):\n";
        py += "            mesh.set_material(0, mat)\n";
        py += "            unreal.EditorAssetLibrary.save_loaded_asset(mesh)\n";
        py += "        elif isinstance(mesh, unreal.SkeletalMesh):\n";
        py += "            mats = mesh.get_editor_property('materials')\n";
        py += "            if mats:\n";
        py += "                mats[0].set_editor_property('material_interface', mat)\n";
        py += "                mesh.set_editor_property('materials', mats)\n";
        py += "                unreal.EditorAssetLibrary.save_loaded_asset(mesh)\n";
        py += "    print('SCULPTFORGE_MATERIAL ' + mat_path)\n";
        py += "except Exception as e:\n";
        py += "    unreal.log_warning('SculptForge: building the material failed (' + str(e) + ')')\n";
    }
    return py;
}

bool addUnrealMaterialMaps(const PaintTexture& tex, const std::string& folder, UnrealImport& im, std::string& error) {
    if (!hasMaterialChannels(tex)) return true;
    ExportPreset preset;
    if (!findExportPreset("unreal", preset, error)) return false;
    const std::string base = im.assetName.rfind("SM_", 0) == 0 || im.assetName.rfind("SK_", 0) == 0 ? im.assetName.substr(3) : im.assetName;
    std::vector<ExportedMap> written;
    if (!exportMaps(tex, preset, folder, base.empty() ? std::string("Sculpt") : base, written, error)) return false;
    for (const ExportedMap& m : written) {
        if (m.map == "BC") im.baseColorPath = m.path;
        else if (m.map == "N") im.normalMapPath = m.path, im.normalMapDirectX = true;
        else if (m.map == "ORM") im.ormPath = m.path;
        else if (m.map == "E") im.emissivePath = m.path;
    }
    im.buildMaterial = true;
    return true;
}

bool sendToUnreal(const UnrealEndpoint& endpoint, const UnrealImport& import, std::string& message,
                  const std::string& projectName) {
    std::string error;
    auto editors = findUnrealEditors(endpoint, 1500, error);
    if (editors.empty()) {
        message = error.empty() ? "No Unreal Editor found - enable the Python Editor Script Plugin and Project Settings > "
                                  "Plugins > Python > Enable Remote Execution, then try again"
                                : "Unreal discovery failed: " + error;
        return false;
    }
    const UnrealEditor* target = &editors[0];
    for (const auto& ed : editors)
        if (!projectName.empty() && ed.projectName == projectName) target = &ed;
    UnrealImport send = import;
    // A remote editor can't read our file path: serve the file once over HTTP while the import runs.
    Socket http;
    std::thread server;
    std::atomic<bool> done{false};
    if (!target->local()) {
        std::string data;
        {
            std::ifstream f(std::filesystem::path(std::u8string(send.fbxPath.begin(), send.fbxPath.end())), std::ios::binary);
            data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        const std::string ip = localAddressToward(target->address);
        uint16_t port = 0;
        if (data.empty() || ip.empty() || !http.listen(ip, 0, port, error)) {
            message = data.empty() ? "Cannot read " + send.fbxPath : "Cannot serve the file to " + target->machine + ": " + error;
            return false;
        }
        const std::string token = newNodeId();
        send.downloadUrl = "http://" + ip + ":" + std::to_string(port) + "/" + token + "/" +
                           std::filesystem::path(std::u8string(send.fbxPath.begin(), send.fbxPath.end())).filename().string();
        server = std::thread([&http, &done, token, data = std::move(data)] { serveFileOnce(http, token, data, done); });
    }
    std::string output;
    const bool ran = runInUnreal(endpoint, *target, unrealImportScript(send), output, error, 300000);
    done = true;
    if (server.joinable()) server.join();
    if (!ran) {
        message = "Unreal import failed: " + error;
        return false;
    }
    const size_t at = output.find("SCULPTFORGE_IMPORTED ");
    std::string imported = at == std::string::npos ? std::string() : output.substr(at + 21);
    imported = imported.substr(0, imported.find('\n')); // later lines: the material
    while (!imported.empty() && (imported.back() == '\n' || imported.back() == '\r')) imported.pop_back();
    if (imported.empty() || imported == "nothing") {
        message = "Unreal ran the import but reported no asset - check the Output Log in " + target->projectName;
        return false;
    }
    message = "Sent to Unreal (" + target->projectName + "): " + imported;
    if (output.find("SCULPTFORGE_MATERIAL ") != std::string::npos) message += " + material maps and M_ material";
    else if (output.find("SCULPTFORGE_TEXTURE ") != std::string::npos) message += " + normal map";
    return true;
}

} // namespace sf::link
