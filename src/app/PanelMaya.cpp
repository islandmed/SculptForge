// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File > Maya (#42): the file-exchange bridge to the SculptForge shelf script for Maya (io/MayaBridge.h,
// integrations/maya). Off until the user picks an exchange folder with the native folder dialog; SculptForge
// then reads and writes only inside that folder, only when the user sends or gets, or (Get automatically, on
// once a folder is chosen) after a modification-time check of that folder's one manifest every 250 ms, or (Live,
// opt-in) 300 ms after a stroke or edit ends, for the Parts it changed. No sockets, no ports, and nothing from
// the folder is ever run. While the bridge is on, the top bar has a Send to Maya button (Shell.cpp).
#include "app/App.h"
#include "app/Ui.h"

#include "core/Json.h"
#include "core/ManagedPolicy.h"
#include "io/BridgeCommands.h"
#include "io/MayaBridge.h"
#include "io/MayaCommands.h"
#include "app/Theme.h"

#include <SDL3/SDL_clipboard.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

std::filesystem::path fsPath(const std::string& utf8) { return std::filesystem::path(std::u8string(utf8.begin(), utf8.end())); }

bool isFolder(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_directory(fsPath(path), ec);
}

void menuAction(App& app, const char* id) {
    const Action* a = app.actions().find(id);
    if (!a) return;
    const std::string shortcut = app.actions().shortcutText(id);
    const std::string label = a->label + "##" + a->id;
    if (ImGui::MenuItem(label.c_str(), shortcut.empty() ? nullptr : shortcut.c_str(), false, !a->enabled || a->enabled()) &&
        a->run) {
        app.actions().touch(a->id);
        a->run();
    }
    ui::HelpAction(app, id);
}

// <prefs>/maya.json: {"folder": "...", "auto_get": true, "live": false}. Read strictly: a small file, a string path to an existing
// folder (else the bridge stays off). Get automatically is on unless the user turned it off: "auto_get", not the
// old "auto", which every earlier version wrote as false (its default was off).
void loadMayaPrefs(const std::string& configDir, UiPrefs& p) {
    if (configDir.empty()) return;
    std::ifstream f(fsPath(configDir + "maya.json"), std::ios::binary);
    if (!f) return;
    std::string text(std::istreambuf_iterator<char>(f), {});
    Json j;
    std::string err;
    if (text.size() > 16384 || !Json::parse(text, j, err) || !j.isObject()) return;
    if (j["folder"].isString() && j["folder"].asString().size() < 4096 && isFolder(j["folder"].asString()))
        p.mayaFolder = j["folder"].asString();
    p.mayaAutoReceive = !p.mayaFolder.empty() && j["auto_get"].asBool(true);
    p.mayaLive = !p.mayaFolder.empty() && j["live"].asBool(false);
}

void saveMayaPrefs(const std::string& configDir, const UiPrefs& p) {
    if (configDir.empty()) return;
    Json j = Json::object();
    j.set("folder", p.mayaFolder);
    j.set("auto_get", p.mayaAutoReceive);
    j.set("live", p.mayaLive);
    std::ofstream f(fsPath(configDir + "maya.json"), std::ios::binary | std::ios::trunc);
    f << j.dump(true) << "\n";
}

uint64_t partUndoRevision(const Document& doc, int i) {
    return i == doc.activePart ? doc.undo.revision() : doc.parts[size_t(i)].undo.revision();
}

// One number for every Part's id and undo revision: it moves with each stroke, undo, new or removed Part.
uint64_t partsKey(const Document& doc) {
    uint64_t h = 1469598103934665603ull ^ uint64_t(doc.partCount());
    for (int i = 0; i < doc.partCount(); ++i) {
        h = (h ^ doc.parts[size_t(i)].id) * 1099511628211ull;
        h = (h ^ partUndoRevision(doc, i)) * 1099511628211ull;
        h = (h ^ uint64_t(doc.parts[size_t(i)].visible)) * 1099511628211ull;
    }
    return h;
}

} // namespace

void App::mayaLiveMarkAll() {
    for (int i = 0; i < doc_.partCount(); ++i) {
        MayaLiveSeen& seen = mayaLiveSeen_[doc_.parts[size_t(i)].id];
        const uint64_t revision = partUndoRevision(doc_, i);
        if (seen.revision != revision) seen = {revision, 0};
    }
    mayaLiveWait_.clear();
}

void App::setMayaLive(bool on) {
    uiPrefs_.mayaLive = on && !uiPrefs_.mayaFolder.empty();
    if (uiPrefs_.mayaLive) {
        // Both ways: what Maya sends loads by itself too (from now on), and what is here now is in sync.
        if (!uiPrefs_.mayaAutoReceive) {
            uiPrefs_.mayaAutoReceive = true;
            const std::string inbox = mayaInbox(uiPrefs_.mayaFolder);
            mayaSeenRevision_ = mayaRevision(inbox);
            mayaManifestStamp_ = mayaManifestStamp(inbox);
        }
        mayaLiveMarkAll();
        status_ = "Maya Live on: the Parts you change go to Maya by themselves after each stroke";
    } else {
        mayaLiveWait_.clear();
        status_ = "Maya Live off: Send to Maya sends";
    }
    if (!uiConfigDeterministic_) saveMayaPrefs(configDir_, uiPrefs_);
}

void App::finishMayaLiveSend() {
    if (!mayaLiveJob_.valid()) return;
    MayaLiveResult r = mayaLiveJob_.get();
    if (!r.ok) {
        status_ = r.message; // the next change tries again
        return;
    }
    for (size_t k = 0; k < r.hashedIds.size(); ++k) {
        MayaLiveSeen& seen = mayaLiveSeen_[r.hashedIds[k]];
        if (seen.revision <= r.revisions[k]) seen = {r.revisions[k], r.hashes[k]}; // (unless a get came since)
    }
    for (size_t k = 0; k < r.ids.size() && k < r.names.size(); ++k)
        for (int i = 0; i < doc_.partCount(); ++i)
            if (doc_.parts[size_t(i)].id == r.ids[k]) partBridgeName(doc_, i) = r.names[k]; // Maya's next send updates it
    if (!r.ids.empty()) {
        status_ = r.message;
        ++mayaLiveSends_;
    }
}

void App::pollMayaLive(Uint64 now) {
    if (mayaLiveJob_.valid() && mayaLiveJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        finishMayaLiveSend();
    pollMayaLink(now);
    const bool link = mayaLink_ && mayaLink_->connected();
    if (!link && (!uiPrefs_.mayaLive || uiPrefs_.mayaFolder.empty())) return;
    if (const uint64_t key = partsKey(doc_); key != mayaLiveKey_) { // a stroke ended, an undo, a new Part...
        mayaLiveKey_ = key;
        mayaLiveWait_.changed(now);
    }
    const bool busy = doc_.sculptor.strokeActive() || drag_ != Drag::None || mayaLiveJob_.valid();
    // Over the link the stroke's end goes at once (its vertices are on their way already); files wait for 300 ms
    // of stillness, so a quick run of strokes is one send.
    if (link ? !mayaLiveWait_.pending() || busy : !mayaLiveWait_.due(now, busy)) {
        if (mayaLiveWait_.pending()) redrawIn(0.05); // (asleep, the loop wakes every 250 ms only)
        return;
    }
    mayaLiveWait_.clear();
    if (link) {
        for (int i = 0; i < doc_.partCount(); ++i) {
            const Part& part = doc_.parts[size_t(i)];
            const uint64_t revision = partUndoRevision(doc_, i);
            const auto seen = mayaLiveSeen_.find(part.id);
            if (seen != mayaLiveSeen_.end() && seen->second.revision == revision) continue;
            mayaLiveSeen_[part.id] = {revision, 0};
            if (part.visible && doc_.partMesh(i).triangleCount()) sendMayaLinkPart(i);
        }
        return;
    }
    // The visible Parts whose undo revision moved since they were last exchanged: a copy of what the OBJ carries
    // (a few ms for millions of triangles), hashed and written on a worker while sculpting goes on.
    struct Item {
        uint32_t id;
        uint64_t revision, lastHash;
        std::string name;
        Mesh mesh;
    };
    std::vector<Item> items;
    for (int i = 0; i < doc_.partCount() && items.size() < kMayaMaxObjects; ++i) {
        const Part& part = doc_.parts[size_t(i)];
        const uint64_t revision = partUndoRevision(doc_, i);
        const auto seen = mayaLiveSeen_.find(part.id);
        if (seen != mayaLiveSeen_.end() && seen->second.revision == revision) continue;
        if (!part.visible || doc_.partMesh(i).triangleCount() == 0) continue;
        const std::string& bridge = partBridgeName(doc_, i);
        items.push_back({part.id, revision, seen == mayaLiveSeen_.end() ? 0 : seen->second.hash,
                         bridge.empty() ? part.name : bridge, mayaSendCopy(doc_.partMesh(i))});
    }
    if (items.empty()) return;
    mayaLiveJob_ = std::async(std::launch::async, [folder = uiPrefs_.mayaFolder, writer = mayaWriter_,
                                                   items = std::move(items)]() {
        MayaLiveResult r;
        std::vector<MeshObjectRef> refs;
        for (const Item& it : items) {
            const uint64_t hash = mayaMeshHash(it.mesh);
            r.hashedIds.push_back(it.id);
            r.revisions.push_back(it.revision);
            r.hashes.push_back(hash);
            if (hash == it.lastHash) continue; // an undo back to what Maya has, a stroke that moved nothing
            refs.push_back({it.name, &it.mesh});
            r.ids.push_back(it.id);
        }
        r.ok = refs.empty() || mayaSend(refs, folder, r.message, &r.names, nullptr, writer, true);
        return r;
    });
}

// ---- The Live link ----

std::string App::mayaLinkName(int i) {
    std::string& bridge = partBridgeName(doc_, i);
    if (!bridge.empty()) return bridge;
    const std::string wanted = mayaSafeName(doc_.parts[size_t(i)].name);
    auto taken = [&](const std::string& n) {
        for (int k = 0; k < doc_.partCount(); ++k)
            if (k != i && partBridgeName(doc_, k) == n) return true;
        return false;
    };
    std::string name = wanted;
    for (int n = 1; taken(name); ++n) name = wanted + std::to_string(n);
    bridge = name; // Maya's sends of it update this Part
    return name;
}

void App::sendMayaLinkPart(int i) {
    const Mesh& mesh = doc_.partMesh(i);
    const uint32_t id = doc_.parts[size_t(i)].id;
    const std::string name = mayaLinkName(i);
    uint64_t topology = 0x5346u;
    for (const glm::uvec3& t : mesh.triangles) topology = (topology ^ (uint64_t(t.x) | uint64_t(t.y) << 21 | uint64_t(t.z) << 42)) * 0x100000001b3ull;
    MayaLinkSent& sent = mayaLinkSent_[id];
    if (sent.topology == topology && sent.positions.size() == mesh.positions.size()) {
        // The same faces: only the vertices that moved (since the last stream or send).
        link::MayaLinkPoints points = link::mayaLinkPointsBetween(name, sent.positions, mesh.positions);
        for (uint32_t v : points.indices) sent.positions[v] = mesh.positions[v];
        mayaLink_->send(std::move(points));
        return;
    }
    mayaLink_->send(link::mayaLinkMeshFrom(name, mesh));
    sent.positions = mesh.positions;
    sent.topology = topology;
}

void App::startMayaLink() {
    std::string error;
    if (!policy::mayaLinkAllowed(&error)) { // (a studio's policy: never listen)
        status_ = error;
        return;
    }
    const std::string endpoint = link::localEndpoint("maya-link", error);
    auto server = std::make_unique<link::MayaLinkServer>();
    if (endpoint.empty() || !server->start(endpoint, error)) {
        status_ = "Maya Live link: can't start: " + error;
        return;
    }
    mayaLink_ = std::move(server);
    mayaLinkSent_.clear();
    status_ = "Maya Live link on: in Maya, click Link on the SculptForge shelf and paste the pairing code "
              "(File > Maya > Copy pairing code)";
}

void App::stopMayaLink() {
    if (!mayaLink_) return;
    mayaLink_->stop();
    mayaLink_.reset();
    mayaLinkSent_.clear();
    status_ = "Maya Live link off";
}

void App::pollMayaLink(Uint64 now) {
    if (!mayaLink_) return;
    for (std::string e = mayaLink_->takeEvent(); !e.empty(); e = mayaLink_->takeEvent()) {
        if (e == "connected") {
            // Maya gets every visible Part whole, so both sides start from the same vertices.
            mayaLinkSent_.clear();
            mayaLiveMarkAll();
            for (int i = 0; i < doc_.partCount(); ++i)
                if (doc_.parts[size_t(i)].visible && doc_.partMesh(i).triangleCount()) sendMayaLinkPart(i);
            status_ = "Maya Live link: connected to " + mayaLink_->clientName();
        } else {
            status_ = "Maya Live link: " + e;
        }
    }
    if (!mayaLink_->connected()) return;
    // What Maya sent: each object updates the Part with its name (an undoable step), unless it is what we have.
    std::vector<MeshObject> objects;
    const bool canApply = !solverBusy() && !doc_.sculptor.strokeActive() && drag_ == Drag::None;
    for (link::MayaLinkMesh& m : canApply ? mayaLink_->takeReceived() : std::vector<link::MayaLinkMesh>{}) {
        MeshObject o;
        std::string error;
        o.name = m.name;
        if (link::mayaLinkMeshTo(m, o.mesh, error)) objects.push_back(std::move(o));
        else status_ = "Maya Live link: " + m.name + ": " + error;
    }
    if (!objects.empty()) {
        mayaDropUnchanged(doc_, objects);
        if (!objects.empty()) {
            std::unordered_map<uint32_t, uint64_t> before;
            for (int i = 0; i < doc_.partCount(); ++i) before[doc_.parts[size_t(i)].id] = partUndoRevision(doc_, i);
            const size_t count = objects.size();
            receiveObjects(doc_, objects, "Live link from Maya", true);
            // The echo guard: what Maya sent is what Maya has.
            for (int i = 0; i < doc_.partCount(); ++i) {
                const uint32_t id = doc_.parts[size_t(i)].id;
                const uint64_t revision = partUndoRevision(doc_, i);
                const auto was = before.find(id);
                if (was != before.end() && was->second == revision) continue;
                mayaLiveSeen_[id] = {revision, 0};
                mayaLinkSent_.erase(id);
                MayaLinkSent& sent = mayaLinkSent_[id];
                sent.positions = doc_.partMesh(i).positions;
                uint64_t topology = 0x5346u;
                for (const glm::uvec3& t : doc_.partMesh(i).triangles)
                    topology = (topology ^ (uint64_t(t.x) | uint64_t(t.y) << 21 | uint64_t(t.z) << 42)) * 0x100000001b3ull;
                sent.topology = topology;
            }
            mayaLiveKey_ = partsKey(doc_);
            status_ = "Maya Live link: got " + std::to_string(count) + (count == 1 ? " object" : " objects") + " from Maya";
        }
    }
    // Mid-stroke: the active Part's moved vertices, at most every 100 ms (between the solver's batches).
    if (doc_.sculptor.strokeActive() && now - mayaLinkStreamNs_ >= link::kMayaLinkStreamNs && doc_.activePart >= 0) {
        mayaLinkStreamNs_ = now;
        const auto sent = mayaLinkSent_.find(doc_.activePartInfo().id);
        if (sent != mayaLinkSent_.end() && sent->second.positions.size() == doc_.mesh.positions.size()) {
            link::MayaLinkPoints points =
                link::mayaLinkPointsBetween(mayaLinkName(doc_.activePart), sent->second.positions, doc_.mesh.positions);
            for (uint32_t v : points.indices) sent->second.positions[v] = doc_.mesh.positions[v];
            if (!points.indices.empty()) {
                mayaLink_->send(std::move(points));
                ++mayaLinkStreams_;
            }
        }
    }
    if (doc_.sculptor.strokeActive()) redrawIn(0.1);
}

void App::drawMayaLinkDialog() {
    if (!mayaLinkAsk_) return;
    const float sc = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    bool turnOn = false, cancel = false;
    if (ImGui::Begin(tr("Turn on the Maya Live link?###mayalinkask"), nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(420.0f * sc);
        ImGui::TextWrapped("%s", tr("The Live link lets the SculptForge shelf in Maya follow your strokes as you sculpt."));
        ImGui::Spacing();
        ImGui::TextColored(theme::danger, "%s", tr("It opens a connection on this computer."));
        ImGui::TextWrapped("%s", tr("Anything running on this computer that knows the pairing code could send meshes into "
                                    "SculptForge. It carries meshes only: never commands, files or scripts. Other "
                                    "computers and web pages can't reach it."));
        ImGui::Spacing();
        ImGui::TextWrapped("%s", tr("Turn it off when you're not using it: click \"Live link on\" in the top bar. It is "
                                    "off again every time SculptForge starts."));
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        turnOn = ImGui::Button(tr("Turn on##mayalink"), ImVec2(140.0f * sc, 0));
        ImGui::SameLine();
        cancel = ImGui::Button(tr("Cancel##mayalink"), ImVec2(140.0f * sc, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    }
    ImGui::End();
    if (turnOn) {
        mayaLinkAsk_ = false;
        startMayaLink();
    } else if (cancel) {
        mayaLinkAsk_ = false;
        status_ = "Maya Live link stays off";
    }
}

void App::setMayaFolder(const std::string& folder) {
    if (!isFolder(folder)) {
        status_ = "Not a folder: " + folder;
        return;
    }
    if (uiPrefs_.mayaFolder.empty()) uiPrefs_.mayaAutoReceive = true; // the bridge turns on with Get automatically
    mayaLiveMarkAll(); // (a new folder: Live sends what changes from now on)
    uiPrefs_.mayaFolder = folder;
    // What is already there is old news: Get from Maya loads it, Get automatically waits for the next send.
    const std::string inbox = mayaInbox(folder);
    mayaSeenRevision_ = mayaRevision(inbox);
    mayaManifestStamp_ = mayaManifestStamp(inbox);
    if (!uiConfigDeterministic_) saveMayaPrefs(configDir_, uiPrefs_);
    status_ = "Maya exchange folder: " + folder + " (choose the same one in Maya's SculptForge shelf)";
}

void App::sendToMaya() {
    if (uiPrefs_.mayaFolder.empty()) {
        status_ = "Choose the folder to exchange meshes with Maya through";
        chooseMayaFolder();
        return;
    }
    if (mayaLiveJob_.valid()) { // one writer at a time in the outbox
        mayaLiveJob_.wait();
        finishMayaLiveSend();
    }
    runCommand("maya_send", {{"folder", uiPrefs_.mayaFolder}});
    if (status_.rfind("Sent", 0) == 0) mayaLiveMarkAll(); // Maya has every visible Part as it is now
}

void App::receiveFromMaya() {
    if (uiPrefs_.mayaFolder.empty()) {
        status_ = "Choose the folder to exchange meshes with Maya through";
        chooseMayaFolder();
        return;
    }
    const std::string inbox = mayaInbox(uiPrefs_.mayaFolder);
    mayaSeenRevision_ = mayaRevision(inbox); // a manual get counts as seen, so polling doesn't load it again
    mayaManifestStamp_ = mayaManifestStamp(inbox);
    std::unordered_map<uint32_t, uint64_t> before;
    for (int i = 0; i < doc_.partCount(); ++i) before[doc_.parts[size_t(i)].id] = partUndoRevision(doc_, i);
    runCommand("maya_receive", {{"folder", uiPrefs_.mayaFolder}});
    // Echo guard: the Parts the get changed or added hold what Maya has, so Live doesn't send them back.
    for (int i = 0; i < doc_.partCount(); ++i) {
        const uint32_t id = doc_.parts[size_t(i)].id;
        const uint64_t revision = partUndoRevision(doc_, i);
        const auto was = before.find(id);
        if (was == before.end() || was->second != revision) mayaLiveSeen_[id] = {revision, 0};
    }
    // (Live keeps the view: Maya's edits arrive while you look at them.)
    if (status_.rfind("Received", 0) == 0 && !uiPrefs_.mayaLive) camera_.frame(sceneBounds());
}

void App::drawMayaMenu() {
    if (!ImGui::BeginMenu(tr("Maya"))) return;
    menuAction(*this, "file.send_maya");
    menuAction(*this, "file.receive_maya");
    const bool hasFolder = !uiPrefs_.mayaFolder.empty();
    if (ImGui::MenuItem(tr("Get automatically##maya"), nullptr, &uiPrefs_.mayaAutoReceive, hasFolder)) {
        if (uiPrefs_.mayaAutoReceive) { // from now on: what is there already waits for Get from Maya
            const std::string inbox = mayaInbox(uiPrefs_.mayaFolder);
            mayaSeenRevision_ = mayaRevision(inbox);
            mayaManifestStamp_ = mayaManifestStamp(inbox);
        }
        if (!uiConfigDeterministic_) saveMayaPrefs(configDir_, uiPrefs_);
    }
    ui::HelpText("Get automatically", nullptr,
                 "Load what Maya sends as soon as it arrives: SculptForge checks the exchange folder's manifest every "
                 "250 ms (its modification time and size only). On once you choose the folder; off: use Get from Maya.",
                 "File > Maya");
    bool live = uiPrefs_.mayaLive;
    if (ImGui::MenuItem(tr("Live##maya"), nullptr, &live, hasFolder)) setMayaLive(live);
    ui::HelpText("Live", nullptr,
                 "Keep Maya in step as you sculpt: after each stroke (once you pause for 300 ms), the Parts it changed "
                 "go to the exchange folder by themselves, and what Maya sends loads by itself. Still files only: no "
                 "network. Turn on Live on Maya's SculptForge shelf too.",
                 "File > Maya");
    ImGui::Separator();
    if (!mayaLink_) {
        if (ImGui::MenuItem(tr("Live link...##maya"), nullptr, false, policy::mayaLinkAllowed())) mayaLinkAsk_ = true;
        ui::HelpText("Live link", nullptr,
                     "Maya follows your strokes as you draw them, over a connection on this computer (the shelf's Link "
                     "button). Asks first, every time; off again at every start.",
                     "File > Maya");
    } else {
        if (mayaLink_->connected())
            ImGui::TextColored(theme::accent, tr("Live link: connected to %s"), mayaLink_->clientName().c_str());
        else
            ImGui::TextColored(theme::accent, "%s", tr("Live link on: waiting for Maya"));
        ImGui::TextDisabled(tr("Pairing code: %s"), link::mayaLinkShowCode(mayaLink_->code()).c_str());
        if (ImGui::MenuItem(tr("Copy pairing code##maya"))) {
            SDL_SetClipboardText(link::mayaLinkPairingText(mayaLink_->code(), mayaLink_->endpoint()).c_str());
            status_ = "Pairing code copied: paste it into Maya (SculptForge shelf > Link)";
        }
        if (ImGui::MenuItem(tr("Turn the Live link off##maya"))) stopMayaLink();
    }
    ImGui::Separator();
    menuAction(*this, "file.maya_folder");
    if (hasFolder) {
        ImGui::TextDisabled(tr("Folder: %s"), uiPrefs_.mayaFolder.c_str());
        if (ImGui::MenuItem(tr("Turn the Maya bridge off##maya"))) {
            uiPrefs_.mayaFolder.clear();
            uiPrefs_.mayaAutoReceive = false;
            uiPrefs_.mayaLive = false;
            if (!uiConfigDeterministic_) saveMayaPrefs(configDir_, uiPrefs_);
            status_ = "Maya bridge off: SculptForge no longer reads or writes the exchange folder";
        }
        ui::HelpText("Turn the Maya bridge off", nullptr, "Forget the exchange folder; nothing in it is deleted.",
                     "File > Maya");
    } else {
        ImGui::TextDisabled(tr("Off: choose an exchange folder first."));
    }
    ImGui::TextDisabled(tr("Needs the SculptForge shelf script in Maya\n(Help > User Guide > Working with Maya)."));
    ImGui::EndMenu();
}

void App::pollMaya() {
    if (!mayaInitialised_) {
        mayaInitialised_ = true;
        if (!uiConfigDeterministic_) loadMayaPrefs(configDir_, uiPrefs_);
        mayaWriter_ = mayaNewWriterId();
        if (!uiPrefs_.mayaFolder.empty()) { // what was sent before this start is old news
            const std::string inbox = mayaInbox(uiPrefs_.mayaFolder);
            mayaSeenRevision_ = mayaRevision(inbox);
            mayaManifestStamp_ = mayaManifestStamp(inbox);
        }
        mayaLiveMarkAll();
        mayaLiveKey_ = 0;
    }
    const Uint64 now = SDL_GetTicksNS();
    pollMayaLive(now);
    if (!uiPrefs_.mayaAutoReceive || uiPrefs_.mayaFolder.empty() || doc_.sculptor.strokeActive()) return;
    // UI tests check every frame (their frames are simulated); the app every 250 ms.
    const Uint64 interval = options_.uiTest.empty() ? kMayaPollNs : 0;
    if (mayaLastPollNs_ != 0 && now - mayaLastPollNs_ < interval) return;
    mayaLastPollNs_ = now;
    // The cheap check first: the manifest's modification time and size. Only when they changed is the (small,
    // size-capped) manifest read for its revision.
    const std::string inbox = mayaInbox(uiPrefs_.mayaFolder);
    const long long t = mayaManifestStamp(inbox);
    if (t == 0 || t == mayaManifestStamp_) return;
    mayaManifestStamp_ = t;
    MayaManifest m;
    std::string err;
    if (!readMayaManifest(mayaManifestPath(inbox), m, err)) return; // (half written: the next change is checked)
    if (m.revision == mayaSeenRevision_) return;
    if (!m.writer.empty() && m.writer == mayaWriter_) { // never load back what this session wrote
        mayaSeenRevision_ = m.revision;
        return;
    }
    receiveFromMaya();
}

} // namespace sf
