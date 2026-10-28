// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File > Capture from phone (link/PhoneLink.h, docs/phone-link.md): the pairing window with its QR code, what
// the phone is sending, View in AR on the phone, and the live face feed with Record / Stop. The phone link's
// server runs only while this window is open.
#include "app/App.h"
#include "app/Ui.h"

#include "link/PhoneRtc.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>

namespace sf {

namespace fs = std::filesystem;

namespace {

std::string folderName(const std::string& path) {
    const auto name = fs::path(std::u8string(path.begin(), path.end())).filename().u8string();
    return std::string(name.begin(), name.end());
}

std::string megabytes(double bytes) {
    char buf[32];
    if (bytes >= 1e9) std::snprintf(buf, sizeof buf, "%.2f GB", bytes / 1e9);
    else std::snprintf(buf, sizeof buf, "%.1f MB", bytes / 1e6);
    return buf;
}

// The file a splat capture trains from: its one video, else the folder of photos.
std::string splatSource(const link::PhoneEvent& e) {
    if (e.files.size() == 1) {
        std::string ext = fs::path(std::u8string(e.files[0].begin(), e.files[0].end())).extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".mp4" || ext == ".mov" || ext == ".m4v" || ext == ".webm" || ext == ".3gp") return e.files[0];
    }
    return e.folder;
}

// A splat .ply has Gaussian properties in its header; anything else is a mesh.
bool plyHasSplats(const std::string& path) {
    std::ifstream in(fs::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    std::string head(4096, '\0');
    in.read(head.data(), std::streamsize(head.size()));
    head.resize(size_t(in.gcount()));
    const size_t end = head.find("end_header");
    if (end != std::string::npos) head.resize(end);
    return head.find("f_dc_0") != std::string::npos || head.find("scale_0") != std::string::npos;
}

std::string fileUrl(const std::string& path) {
    std::string p = path;
    for (char& c : p)
        if (c == '\\') c = '/';
    std::string url = p.rfind('/', 0) == 0 ? "file://" : "file:///";
    const char* hex = "0123456789ABCDEF";
    for (unsigned char c : p) {
        if (std::isalnum(c) || std::strchr("/:-_.~", c)) url += char(c);
        else url += {'%', hex[c >> 4], hex[c & 15]};
    }
    return url;
}

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

const ImVec4 kOk(0.35f, 0.85f, 0.6f, 1.0f), kWarn(0.95f, 0.70f, 0.30f, 1.0f), kBad(1.0f, 0.45f, 0.40f, 1.0f);

} // namespace

void App::initPhoneActions() {
    actions_.add({"file.capture_phone", "Capture from phone", ICON_LC_SMARTPHONE,
                  "Use your phone as SculptForge's camera: scan the QR code to send scans, photos and splat files, see "
                  "the sculpt on your desk in AR, and drive a face with your own. No app to install; phone and computer "
                  "on the same Wi-Fi.",
                  "File", "", "", [this] { showPhone_ = !showPhone_; }, [this] { return showPhone_; }, nullptr});
    actions_.add({"phone.record_face", "Record face take", ICON_LC_CIRCLE_DOT,
                  "Record the live face feed into a take (Captures/<time>-face/take.jsonl, one frame of ARKit weights per "
                  "line) for playback and export. Again: stop.",
                  "File > Capture from phone", "", "",
                  [this] {
                      if (!phone_ || !phone_->running()) return;
                      if (phone_->status().recording) {
                          std::string summary;
                          const std::string folder = phone_->stopTake(&summary);
                          status_ = "Face take saved: Captures/" + folderName(folder) + " (" + summary + ")";
                          libraryScanner_.invalidate();
                      } else {
                          std::string folder, err;
                          status_ = phone_->startTake(folder, err) ? "Recording the face feed..." : "Can't record: " + err;
                      }
                  },
                  [this] { return phone_ && phone_->running() && phone_->status().recording; },
                  [this] { return phone_ && phone_->running(); }});
}

void App::setPhoneLinkOpen(bool open) {
    if (!open) {
        if (phone_ && phone_->running()) {
            std::string summary;
            const std::string take = phone_->stopTake(&summary);
            phone_->stop();
            status_ = take.empty() ? "Phone link closed" : "Phone link closed; face take saved (" + summary + ")";
        }
        return;
    }
    if (phone_ && phone_->running()) return;
    if (!phone_) phone_ = std::make_unique<link::PhoneLink>();
    phoneError_.clear();
    if (library_.user.empty()) {
        phoneError_ = "The phone link saves into your library's Captures folder: set a library in Preferences > LightBox.";
        return;
    }
    link::PhoneLinkOptions o;
    o.inboxRoot = library_.user + "/Captures";
    o.desktopName = link::hostName();
    o.version = SCULPTFORGE_VERSION;
    if (const char* port = std::getenv("SCULPTFORGE_LINK_PORT")) o.port = uint16_t(std::atoi(port)); // tests, firewalls
    std::string err;
    if (!phone_->start(o, err)) {
        phoneError_ = err;
        status_ = "Phone link: " + err;
        return;
    }
    const auto addresses = phone_->addresses();
    if (phoneAddress_ > 0 && phoneAddress_ < int(addresses.size())) phone_->setAddress(addresses[size_t(phoneAddress_)]);
    else phoneAddress_ = 0;
    phoneFaceSerial_ = 0;
    phoneFace_ = {};
    phoneFaceCommandMissing_ = false;
    phoneArRequested_ = false;
    status_ = "Phone link open: scan the QR code with your phone";
}

bool App::phoneBusy() const {
    return phoneArJob_.valid() && phoneArJob_.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

void App::handlePhoneCapture(const link::PhoneEvent& e) {
    phoneCaptures_.push_back(e.folder);
    libraryScanner_.invalidate();
    const std::string where = "Captures/" + folderName(e.folder);
    switch (e.kind) {
    case link::CaptureKind::Splat: {
        if (!commands_.find("splat_train")) {
            status_ = "Scan received (" + where + "). Splat training (splat_train) isn't in this build yet.";
            lastCommandOk_ = true;
            return;
        }
        runCommand("splat_train", {{"source", splatSource(e)}});
        return;
    }
    case link::CaptureKind::Photo: {
        if (e.files.empty() || !loadReferenceImage(e.files.front())) {
            status_ = "Photo received (" + where + ") but it can't be read: " + status_;
            return;
        }
        showImageTo3D_ = true;
        textMode_ = false;
        if (phoneAutoGenerate_) {
            startGenerator();
            status_ = "Photo received: generating a 3D model (Generate > Image to 3D)";
        } else {
            status_ = "Photo received (" + where + "): press Generate in Image to 3D";
        }
        return;
    }
    case link::CaptureKind::Ply: {
        if (e.files.empty()) return;
        const std::string& file = e.files.front();
        std::string ext = fs::path(std::u8string(file.begin(), file.end())).extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".spz") {
            status_ = "Splat received (" + where + "): .spz isn't supported yet; export .ply from the scanning app";
            return;
        }
        // Splats load next to the sculpt; a mesh .ply becomes a new SubTool (never replacing the work).
        if (ext == ".splat" || plyHasSplats(file)) runCommand("open", {{"path", file}});
        else runCommand("subtool_append", {{"path", file}});
        if (lastCommandOk_) camera_.frame(sceneBounds());
        return;
    }
    case link::CaptureKind::Face: return;
    }
}

void App::startPhoneArExport() {
    if (phoneBusy() || !phone_) return;
    // Snapshot on this thread (the worker must not see the document change under it).
    std::vector<Mesh> parts;
    if (phoneArScene_) {
        for (int i = 0; i < doc_.subtoolCount(); ++i)
            if (doc_.subtools[size_t(i)].visible) parts.push_back(doc_.subtoolMesh(i));
        if (parts.empty()) parts.push_back(doc_.mesh);
    } else {
        parts.push_back(doc_.mesh);
    }
    ArExportOptions options;
    options.sizeMeters = phoneArSizeCm_ / 100.0f;
    options.name = doc_.assetName().empty() ? "SculptForge" : doc_.assetName();
    phone_->setArBusy(true);
    phoneArStartedAt_ = ImGui::GetTime();
    auto promise = std::make_shared<std::promise<PhoneArJob>>();
    phoneArJob_ = promise->get_future();
    std::thread([promise, parts = std::move(parts), options] {
        PhoneArJob job;
        std::vector<const Mesh*> refs;
        for (const Mesh& m : parts) refs.push_back(&m);
        job.ok = exportArModel(refs, options, job.result, job.error);
        promise->set_value(std::move(job));
    }).detach();
}

void App::pollPhone() {
    if (showPhone_ && (!phone_ || !phone_->running()) && phoneError_.empty()) setPhoneLinkOpen(true);
    if (!showPhone_ && phone_ && phone_->running()) setPhoneLinkOpen(false);
    if (!showPhone_) phoneError_.clear(); // try again the next time the window opens
    if (!phone_ || !phone_->running()) return;

    for (const link::PhoneEvent& e : phone_->takeEvents()) {
        using T = link::PhoneEvent::Type;
        switch (e.type) {
        case T::DeviceConnected: status_ = "Phone connected: " + e.text; break;
        case T::CaptureReady: handlePhoneCapture(e); break;
        case T::ArRequested:
            phoneArScene_ = e.wholeScene;
            phoneArSizeCm_ = e.sizeCm;
            phoneArRequested_ = true;
            phoneArSignature_ = 0; // always a fresh model when asked
            if (!phoneBusy()) startPhoneArExport();
            status_ = "The phone asked to see the sculpt in AR: preparing it";
            break;
        case T::FaceConnected: status_ = "Face feed connected (" + e.text + ")"; break;
        case T::FaceDisconnected: status_ = "Face feed: " + e.text; break;
        case T::Error: status_ = "Phone link: " + e.text; break;
        }
    }

    // Face frames: the preview, and face_weights for the facial shapes (no undo step, no console echo).
    link::FaceFrame frame;
    if (phone_->latestFace(frame, phoneFaceSerial_)) {
        phoneFace_ = phoneMirror_ ? frame.mirrored() : frame;
        if (commands_.find("face_weights")) {
            if (!doc_.sculptor.strokeActive()) {
                const CommandResult r = commands_.execute(doc_, "face_weights", {{"json", phoneFace_.weightsJson()}});
                if (!r.ok && !phoneFaceCommandMissing_) status_ = "face_weights: " + r.message;
            }
        } else if (!phoneFaceCommandMissing_) {
            phoneFaceCommandMissing_ = true;
            status_ = "Face feed connected, but face_weights isn't in this build yet (Generate facial shapes comes with "
                      "the facs feature): the preview shows the expressions";
        }
    }

    // AR: publish a finished export; re-export after edits while the phone is looking (at most every 2.5 s).
    if (phoneArJob_.valid() && phoneArJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        PhoneArJob job = phoneArJob_.get();
        if (job.ok) phone_->publishArModel(std::move(job.result.glb), std::move(job.result.usdz), job.result.summary);
        else phone_->setArError(job.error);
    }
    const uint64_t signature = doc_.undo.revision() * 1000003ull + uint64_t(doc_.activeSubtool) * 7919ull +
                               uint64_t(doc_.subtoolCount()) + (doc_.modified ? 1ull << 62 : 0ull) + 1;
    const double viewed = phone_->secondsSinceArViewed();
    if (phoneArRequested_ && phoneLiveAr_ && !phoneBusy() && !doc_.sculptor.strokeActive() && viewed >= 0 &&
        viewed < 600 && ImGui::GetTime() - phoneArStartedAt_ > 2.5) {
        if (phoneArSignature_ == 0) phoneArSignature_ = signature; // the export the phone asked for
        else if (signature != phoneArSignature_) {
            phoneArSignature_ = signature;
            startPhoneArExport();
        }
    }
}

void App::drawFacePreview(ImVec2 size) {
    ImGui::InvisibleButton("##facepreview", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    dl->AddRectFilled(a, b, IM_COL32(0x16, 0x16, 0x1a, 0xff), 8.0f);
    const link::FaceFrame& f = phoneFace_;
    const bool live = phone_ && phone_->status().secondsSinceFace >= 0 && phone_->status().secondsSinceFace < 1.5;
    // Head rotation: yaw and pitch shift the features, roll turns the whole face.
    const glm::vec3 angles = f.hasHead ? glm::eulerAngles(f.head) : glm::vec3(0.0f);
    const float roll = -angles.z;
    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float r = std::min(size.x, size.y) * 0.42f;
    const glm::vec2 shift(std::clamp(-std::sin(angles.y), -0.5f, 0.5f) * r * 0.5f, std::clamp(std::sin(angles.x), -0.5f, 0.5f) * r * 0.4f);
    auto at = [&](float x, float y) { // face space (-1..1, y down) to screen
        const float cs = std::cos(roll), sn = std::sin(roll);
        const float px = x * r + shift.x, py = y * r + shift.y;
        return ImVec2(c.x + px * cs - py * sn, c.y + px * sn + py * cs);
    };
    const ImU32 skin = live ? IM_COL32(0xd9, 0xa8, 0x82, 0xff) : IM_COL32(0x70, 0x66, 0x60, 0xff);
    const ImU32 ink = IM_COL32(0x2a, 0x1c, 0x14, 0xff);
    dl->AddCircleFilled(c, r, skin, 48);
    // Eyes: blink closes them, wide opens them; brows rise and fall.
    for (int side = -1; side <= 1; side += 2) {
        const bool left = side < 0; // the viewer's left is the subject's right
        const float blink = f.weight(left ? "eyeBlinkRight" : "eyeBlinkLeft");
        const float wide = f.weight(left ? "eyeWideRight" : "eyeWideLeft");
        const float open = std::clamp(1.0f - blink + 0.5f * wide, 0.05f, 1.4f);
        const ImVec2 e = at(0.36f * float(side), -0.12f);
        const float ex = r * 0.13f, ey = r * 0.09f * open;
        dl->PathClear();
        for (int i = 0; i <= 20; ++i) {
            const float t = float(i) / 20.0f * 6.2831853f;
            dl->PathLineTo(ImVec2(e.x + std::cos(t) * ex, e.y + std::sin(t) * ey));
        }
        dl->PathFillConvex(IM_COL32(0xf4, 0xf0, 0xea, 0xff));
        if (open > 0.25f) dl->AddCircleFilled(e, std::min(ex, ey) * 0.6f, ink, 16);
        const float down = f.weight(left ? "browDownRight" : "browDownLeft");
        const float inner = -0.33f - 0.16f * f.weight("browInnerUp") + 0.08f * down;
        const float outer = -0.36f - 0.16f * f.weight(left ? "browOuterUpRight" : "browOuterUpLeft") + 0.03f * down;
        dl->AddLine(at(0.2f * float(side), inner), at(0.52f * float(side), outer), ink, r * 0.05f);
    }
    // Mouth: jaw opens it, smiles lift the corners, frowns pull them down, pucker narrows it.
    const float jaw = f.weight("jawOpen"), pucker = f.weight("mouthPucker") + f.weight("mouthFunnel") * 0.5f;
    const float smileL = f.weight("mouthSmileRight") - f.weight("mouthFrownRight"); // viewer's left
    const float smileR = f.weight("mouthSmileLeft") - f.weight("mouthFrownLeft");
    const float half = 0.32f * (1.0f - 0.55f * std::clamp(pucker, 0.0f, 1.0f)) + 0.08f * (f.weight("mouthStretchLeft") + f.weight("mouthStretchRight"));
    const float y0 = 0.42f, open = 0.04f + jaw * 0.32f;
    const ImVec2 lc = at(-half, y0 - smileL * 0.12f), rc = at(half, y0 - smileR * 0.12f);
    dl->PathClear();
    dl->PathLineTo(lc);
    dl->PathBezierCubicCurveTo(at(-half * 0.4f, y0 - 0.05f), at(half * 0.4f, y0 - 0.05f), rc, 12);
    dl->PathBezierCubicCurveTo(at(half * 0.4f, y0 + open + 0.04f), at(-half * 0.4f, y0 + open + 0.04f), lc, 12);
    dl->PathFillConcave(IM_COL32(0x6a, 0x22, 0x22, 0xff));
    if (!live) {
        const char* text = phoneFaceSerial_ ? "No face" : "No face yet";
        const ImVec2 ts = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, b.y - ts.y - 6.0f), IM_COL32(0xc8, 0xc8, 0xc8, 0xff), text);
    }
}

void App::drawPhoneWindow() {
    if (!showPhone_) return;
    const float scale = uiScale_;
    ImGui::SetNextWindowSize(ImVec2(780 * scale, 0), ImGuiCond_Always);
    {
        // At the top of the canvas, leaving most of the model in view.
        const CanvasRect c = canvasRect();
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + 12 * scale), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    }
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::Begin("Capture from phone", &showPhone_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                                           ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!phoneError_.empty() || !phone_ || !phone_->running()) {
        ImGui::TextColored(kBad, "The phone link couldn't start.");
        ImGui::TextWrapped("%s", phoneError_.empty() ? "Starting..." : phoneError_.c_str());
        if (ImGui::Button("Try again##phone")) phoneError_.clear();
        ImGui::End();
        return;
    }
    const link::PhoneLinkStatus st = phone_->status();
    if (phoneQrText_ != st.url) {
        std::string err;
        phoneQrText_ = st.url;
        if (!link::makeQrCode(st.url, phoneQr_, err)) phoneQr_ = {};
    }

    const float leftW = 250 * scale;
    if (!ImGui::BeginTable("##phonelayout", 2, ImGuiTableFlags_None)) {
        ImGui::End();
        return;
    }
    ImGui::TableSetupColumn("pair", ImGuiTableColumnFlags_WidthFixed, leftW);
    ImGui::TableSetupColumn("work", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();

    // --- Left: the QR code (with its quiet zone) and the other ways to pair ---
    ImGui::TableSetColumnIndex(0);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + leftW);
    const float qrSize = leftW - 4 * scale;
    ImGui::InvisibleButton("##phoneqr", ImVec2(qrSize, qrSize));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetItemRectMin();
        dl->AddRectFilled(p, ImVec2(p.x + qrSize, p.y + qrSize), IM_COL32_WHITE, 10.0f * scale);
        if (phoneQr_.size > 0) {
            const float cell = std::floor(qrSize / float(phoneQr_.size + 8));
            const float off = std::floor((qrSize - cell * float(phoneQr_.size)) * 0.5f);
            for (int y = 0; y < phoneQr_.size; ++y)
                for (int x = 0; x < phoneQr_.size; ++x)
                    if (phoneQr_.dark(x, y))
                        dl->AddRectFilled(ImVec2(p.x + off + float(x) * cell, p.y + off + float(y) * cell),
                                          ImVec2(p.x + off + float(x + 1) * cell, p.y + off + float(y + 1) * cell), IM_COL32_BLACK);
        }
    }
    ui::HelpText("QR code", nullptr,
                 "Point your phone's camera at it and tap the link: the capture page opens, paired with this window. "
                 "A new code is made every time the window opens.",
                 "File > Capture from phone");
    ImGui::TextUnformatted("Scan with your phone's camera");
    hint("Phone and computer on the same Wi-Fi. No app to install.");
    hint("Page won't open? Use Firefox on the phone: some Chrome versions block home-network addresses.");
    ImGui::Spacing();
    ImGui::TextDisabled("No camera? Open this in the phone's browser:");
    ImGui::Text("http://%s", st.address.c_str());
    ImGui::Text("and type the code  %.3s %.3s", st.code.c_str(), st.code.size() > 3 ? st.code.c_str() + 3 : "");
    ui::HelpText("Pairing code", nullptr,
                 "For a phone that can't scan: open the address in its browser and type these 6 digits. After 5 wrong "
                 "tries the code changes.",
                 "File > Capture from phone");
    const auto addresses = phone_->addresses();
    if (addresses.size() > 1) {
        ImGui::SetNextItemWidth(leftW - ImGui::CalcTextSize("Network").x - ImGui::GetStyle().ItemInnerSpacing.x - 4 * scale);
        const std::string current = addresses[size_t(std::clamp(phoneAddress_, 0, int(addresses.size()) - 1))];
        if (ImGui::BeginCombo("Network##phoneaddr", current.c_str())) {
            for (int i = 0; i < int(addresses.size()); ++i)
                if (ImGui::Selectable(addresses[size_t(i)].c_str(), i == phoneAddress_)) {
                    phoneAddress_ = i;
                    phone_->setAddress(addresses[size_t(i)]);
                }
            ImGui::EndCombo();
        }
        ui::HelpText("Network", nullptr,
                     "This computer has several network addresses. If the phone can't open the page, pick the one on "
                     "the Wi-Fi the phone uses (often 192.168.x.x).",
                     "File > Capture from phone");
    } else if (addresses.empty()) {
        ImGui::TextColored(kWarn, "This computer isn't on a local network.");
    }
    if (st.tokenExpiresIn > 0 && st.tokenExpiresIn < 120 && st.secondsSinceSeen < 0)
        ImGui::TextColored(kWarn, "This code expires in %.0f s", st.tokenExpiresIn);
    ImGui::PopTextWrapPos();

    // --- Right: the phone, what it sent, AR and the face ---
    ImGui::TableSetColumnIndex(1);
    ImGui::Indent(10 * scale);
    if (st.secondsSinceSeen >= 0 && st.secondsSinceSeen < 8) {
        ImGui::TextColored(kOk, ICON_LC_SMARTPHONE "  %s  connected", st.device.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", st.deviceAddress.c_str());
    } else if (st.secondsSinceSeen >= 0) {
        ImGui::TextColored(kWarn, ICON_LC_SMARTPHONE "  %s: not seen for %.0f s", st.device.c_str(), st.secondsSinceSeen);
        hint("The page may be closed or the phone asleep: open it again to carry on.");
    } else {
        ImGui::TextDisabled(ICON_LC_SMARTPHONE "  Waiting for your phone...");
        if (ImGui::TreeNode("The phone can't open the page?")) {
            ImGui::BulletText("Same Wi-Fi: not a guest network, not mobile data.");
            ImGui::BulletText("Turn off VPNs on the phone and the computer.");
            ImGui::BulletText("Windows: allow SculptForge on private networks\nwhen the firewall asks.");
            ImGui::BulletText("macOS: allow incoming connections for SculptForge\n(System Settings > Network > Firewall).");
            ImGui::BulletText("Several networks? Pick the Wi-Fi one under Network.");
            ImGui::BulletText("Android Chrome says \"This site can't be reached\"\n(ERR_TOO_MANY_RETRIES)? Recent Chrome versions can\n"
                              "block every home-network address, even your router.\nUse Firefox on the phone instead and allow it to\n"
                              "reach local devices when it asks.");
            ImGui::BulletText("Linux: allow port %d (TCP and UDP) through the\nfirewall, e.g. sudo ufw allow from\n<your network>/24 to any port %d",
                              int(phone_->port()), int(phone_->port()));
            ImGui::TreePop();
        }
    }

    // Uploads in progress and what arrived.
    for (const link::PhoneUploadInfo& u : st.uploads) {
        if (u.finished) continue;
        const float frac = u.size ? float(double(u.received) / double(u.size)) : 0.0f;
        const std::string label = megabytes(double(u.received)) + " of " + megabytes(double(u.size)) +
                                  (u.bytesPerSecond > 0 ? "  " + megabytes(u.bytesPerSecond) + "/s" : std::string());
        ImGui::Text("Receiving %s (%s)", u.name.c_str(), link::captureKindName(u.kind));
        ImGui::ProgressBar(frac, ImVec2(-1, 0), label.c_str());
    }
    if (!phoneCaptures_.empty()) {
        ImGui::SeparatorText("Received");
        const size_t first = phoneCaptures_.size() > 3 ? phoneCaptures_.size() - 3 : 0;
        for (size_t i = phoneCaptures_.size(); i-- > first;) {
            ImGui::PushID(int(i));
            if (ImGui::SmallButton(ICON_LC_FOLDER_OPEN) && interactive()) SDL_OpenURL(fileUrl(phoneCaptures_[i]).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show the folder");
            ImGui::PopID();
            ImGui::SameLine();
            ImGui::Text("Captures/%s", folderName(phoneCaptures_[i]).c_str());
        }
    }
    ImGui::Checkbox("Generate from a photo at once##phone", &phoneAutoGenerate_);
    ui::HelpText("Generate from a photo at once", nullptr,
                 "A photo sent from the phone starts Image to 3D straight away with the service chosen there "
                 "(free Hugging Face by default). Off: it opens in Image to 3D and waits for Generate.",
                 "File > Capture from phone");

    // AR on the desk.
    ImGui::SeparatorText("View in AR");
    if (st.arBusy) ImGui::TextColored(kWarn, "Preparing the model for the phone...");
    else if (!st.arError.empty()) ImGui::TextColored(kBad, "%s", st.arError.c_str());
    else if (st.arVersion > 0) ImGui::Text("On the phone: %s", st.arSummary.c_str());
    else hint("Tap View in AR on the phone: the active SubTool (or the scene) stands on your desk.");
    ImGui::Checkbox("Update after each stroke##phone", &phoneLiveAr_);
    ui::HelpText("Update after each stroke", nullptr,
                 "While the phone shows the model, sculpting re-exports it (at most every few seconds); the phone offers "
                 "the new version.",
                 "File > Capture from phone");

    // Live face.
    ImGui::SeparatorText("Face");
    drawFacePreview(ImVec2(112 * scale, 112 * scale));
    ImGui::SameLine(0, 12 * scale);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    if (st.faceConnected && st.secondsSinceFace >= 0 && st.secondsSinceFace < 2.0) {
        ImGui::TextColored(kOk, "Live over %s, %.0f fps", st.faceTransport.c_str(), st.faceFps);
    } else if (st.faceConnected) {
        ImGui::TextColored(kWarn, "Connected (%s), no face for %.0f s", st.faceTransport.c_str(), st.secondsSinceFace);
    } else if (!link::webrtcAvailable()) {
        hint("This build has no WebRTC, so the phone's face page can't connect.");
    } else {
        hint("On the phone: Face capture. The front camera tracks 52 expressions on the phone; only the numbers "
             "come here.");
    }
    if (phoneFaceCommandMissing_) ImGui::TextColored(kWarn, "face_weights isn't in this build: the model doesn't move yet.");
    ui::ActionButton(*this, "phone.record_face");
    if (st.recording) {
        ImGui::SameLine();
        ImGui::TextColored(kBad, ICON_LC_CIRCLE_DOT " %d frames, %.1f s", st.takeFrames, st.takeSeconds);
    }
    ImGui::Checkbox("Mirror left and right##phone", &phoneMirror_);
    ui::HelpText("Mirror left and right", nullptr,
                 "Swap the left and right expressions (and the head's turn), if the character winks with the wrong eye.",
                 "File > Capture from phone");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Unindent(10 * scale);
    ImGui::EndTable();

    ImGui::Separator();
    ImGui::TextDisabled("Saved in %s/Captures. Only devices on your network can connect, and only while this window is open.",
                        library_.user.c_str());
    ImGui::End();
}

} // namespace sf
