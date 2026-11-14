// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Forge Deck (prototype; docs/phone-link.md): the phone as a control pad for the desktop session. It rides the phone
// link (link/PhoneLink.h: LAN only, QR code with a 128-bit token) and adds no listener. File > Forge Deck turns it
// on for this session (never saved: off at every app start) and shows its QR code; a banner with the phone's name
// and Disconnect shows while a phone is paired, and Disconnect (or closing the window) kills the link. A phone
// message is ONE action from link/DeckCommands.h's allowlist, run here on the main thread; anything else was
// refused and logged before it got this far.
#include "app/App.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace sf {

namespace {

// The tiles the phone shows: 12 of the sculpting brushes, in shelf order. The phone sends the id back.
constexpr BrushType kDeckBrushes[] = {BrushType::Clay,    BrushType::Standard,    BrushType::Smooth,  BrushType::Move,
                                      BrushType::Inflate, BrushType::Crease,      BrushType::Flatten, BrushType::Pinch,
                                      BrushType::Layer,   BrushType::SharpCrease, BrushType::Nudge,   BrushType::SnakeHook};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string actionIdFor(BrushType type) { // "brush.clay": the same spelling Config.cpp registers
    std::string id = "brush.";
    for (const char* c = brushName(type); *c; ++c)
        if (*c != ' ') id += char(std::tolower(static_cast<unsigned char>(*c)));
    return id;
}

const ImVec4 kOk(0.35f, 0.85f, 0.6f, 1.0f);

} // namespace

void App::initDeckActions() {
    actions_.add({"file.forge_deck", "Forge Deck (prototype)", ICON_LC_SMARTPHONE,
                  "Use your phone as a control pad for this session: big brush tiles, Size and Intensity strips, Undo, Redo and "
                  "Mirror. Off until you turn it on here; scan the QR code with your phone. The phone can only press those "
                  "controls.",
                  "File", "", "", [this] { if (uiPrefs_.forgeDeck) showDeck_ = !showDeck_; }, [this] { return showDeck_; },
                  [this] { return uiPrefs_.forgeDeck; }});
}

void App::drawExperimentalSection() {
    ui::Checkbox("Forge Deck (prototype)##experimental.forge_deck", &uiPrefs_.forgeDeck);
    ui::HelpText("Forge Deck (prototype)", nullptr,
                 "Adds File > Forge Deck: your phone becomes a control pad for the session (brush tiles, Size, Intensity, Undo, "
                 "Redo, Mirror) after you scan a QR code. The phone page loads over HTTPS and talks to this computer over an "
                 "encrypted link: directly on the same Wi-Fi, or through an encrypted relay when the phone can't reach it. Off by "
                 "default; even when on, the Deck is off every time the app starts.",
                 "Preferences > Experimental");
    if (!uiPrefs_.forgeDeck && showDeck_) killDeck(); // switched off while a phone was driving: it ends at once
}

std::string App::deckStateJson() const {
    Json j = Json::object();
    const bool custom = doc_.activeBrush >= 0 && doc_.activeBrush < int(doc_.brushes.size());
    j.set("tool", paintMode_ ? std::string("Paint") : custom ? doc_.brushes[size_t(doc_.activeBrush)].name : std::string(brushLabel(brush_)));
    j.set("brushId", custom || paintMode_ ? std::string() : std::string(brushName(brush_)));
    j.set("sizePx", double(radiusPx_));
    j.set("size", double(link::deckPxToSize(radiusPx_))); // the strip's position; the phone shows pixels from the range
    j.set("sizeMinPx", double(link::kDeckMinBrushPx));
    j.set("sizeMaxPx", double(link::kDeckMaxBrushPx));
    const bool smooth = brush_ == BrushType::Smooth && !paintMode_;
    j.set("intensity", double(std::clamp(smooth ? smoothStrength_ : paintMode_ ? rgbIntensity_ : strength_, 0.0f, 1.0f)));
    Json mirror = Json::object();
    mirror.set("x", symmetry_.x);
    mirror.set("y", symmetry_.y);
    mirror.set("z", symmetry_.z);
    j.set("mirror", mirror);
    Json brushes = Json::array();
    for (size_t i = 0; i < std::size(kDeckBrushes); ++i) {
        Json b = Json::object();
        b.set("id", std::string(brushName(kDeckBrushes[i])));
        b.set("label", std::string(brushLabel(kDeckBrushes[i])));
        if (std::find(deckImagesSent_.begin(), deckImagesSent_.end(), int(kDeckBrushes[i])) != deckImagesSent_.end())
            b.set("img", "b" + std::to_string(int(kDeckBrushes[i])));
        brushes.push(std::move(b));
    }
    j.set("brushes", brushes);
    return j.dump();
}

void App::runDeckAction(const link::DeckAction& a) {
    using C = link::DeckCommand;
    // A phone can't reach into a running stroke: undo/redo and brush changes wait for the next message.
    const bool stroke = doc_.sculptor.strokeActive();
    switch (a.command) {
    case C::Undo:
    case C::Redo:
        if (!stroke) runCommand(a.command == C::Undo ? "undo" : "redo");
        break;
    case C::SizeSet:
        radiusPx_ = link::deckSizeToPx(a.value);
        break;
    case C::IntensitySet:
        intensityValue(brush_ == BrushType::Smooth && !paintMode_) = std::clamp(a.value, 0.0f, 1.0f);
        break;
    case C::SymmetryToggle: {
        bool& axis = a.axis == 'y' ? symmetry_.y : a.axis == 'z' ? symmetry_.z : symmetry_.x;
        axis = !axis;
        break;
    }
    case C::BrushUse: {
        if (stroke) break;
        for (BrushType type : kDeckBrushes) { // only the tiles the Deck itself showed
            if (lower(brushName(type)) != lower(a.name)) continue;
            if (const Action* action = actions_.find(actionIdFor(type)); action && action->run) action->run();
            return;
        }
        status_ = "Forge Deck: no such brush tile: " + a.name;
        break;
    }
    }
}

void App::killDeck() {
    if (phone_ && phone_->running()) phone_->deckDisconnect();
    showDeck_ = false; // the link closes with the window (pollPhone), unless Capture from phone is open too
    deckApplied_ = false;
    status_ = "Forge Deck disconnected";
}

void App::pollDeck() {
    if (deckApplied_ != showDeck_) {
        if (showDeck_) phone_->setDeckEnabled(true);
        else phone_->deckDisconnect(); // closing the window ends the link (and changes the token)
        deckApplied_ = showDeck_;
        deckQrText_.clear();
        deckLinkMade_ = -1e9;
        deckImagesSent_.clear();
        deckLastState_.clear();
    }
    if (!showDeck_) return;

    for (const link::DeckAction& a : phone_->takeDeckActions()) runDeckAction(a);

    // Tile images: the brush shelf's own thumbnails, from the disk cache once they have been made (a few a second).
    if (deckImagesSent_.size() < std::size(kDeckBrushes) && ImGui::GetTime() - deckLastImageTry_ > 0.5) {
        deckLastImageTry_ = ImGui::GetTime();
        for (BrushType type : kDeckBrushes) {
            if (std::find(deckImagesSent_.begin(), deckImagesSent_.end(), int(type)) != deckImagesSent_.end()) continue;
            thumbs_.brush(int(type)); // asks for it (the shelf does the same)
            const std::string file = thumbs_.brushCacheFile(int(type));
            if (file.empty()) continue;
            std::ifstream in(std::filesystem::path(std::u8string(file.begin(), file.end())), std::ios::binary);
            std::string png((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (png.empty() || png.size() > (512u << 10)) continue;
            phone_->setDeckImage("b" + std::to_string(int(type)), std::move(png));
            deckImagesSent_.push_back(int(type));
        }
    }
    const std::string state = deckStateJson();
    if (state != deckLastState_) { // the phone's readout follows every change on the desktop
        deckLastState_ = state;
        Json j;
        std::string err;
        Json::parse(state, j, err);
        j.set("rev", double(++deckRev_));
        phone_->setDeckState(j.dump());
    }
    phone_->pumpDeck();
}

void App::drawDeckWindow() {
    if (!uiPrefs_.forgeDeck && showDeck_) killDeck(); // (the pref was cleared another way: settings reloaded, a script)
    if (!showDeck_) return;
    const float scale = uiScale_;
    const link::DeckStatus ds = phone_ && phone_->running() ? phone_->deckStatus() : link::DeckStatus{};

    // The banner: visible whenever a phone is paired with the Deck, over every other window.
    if (ds.connected) {
        const CanvasRect c = canvasRect();
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + 8 * scale), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.95f);
        ImGui::PushStyleColor(ImGuiCol_Border, kOk);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
        if (ImGui::Begin("##deckbanner", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
            ImGui::TextColored(kOk, ICON_LC_SMARTPHONE);
            ImGui::SameLine();
            ImGui::Text("%s %s", ds.device.empty() ? tr("A phone") : ds.device.c_str(), tr("is driving this session"));
            ImGui::SameLine();
            if (ImGui::Button(tr("Disconnect##deckbanner"))) killDeck();
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        if (!showDeck_) return;
    }

    ImGui::SetNextWindowSize(ImVec2(360 * scale, 0), ImGuiCond_Always);
    {
        const CanvasRect c = canvasRect();
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w - 12 * scale, c.y + 60 * scale), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    }
    ImGui::SetNextWindowBgAlpha(1.0f);
    if (!ImGui::Begin(tr("Forge Deck (prototype)"), &showDeck_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                                              ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    if (!phone_ || !phone_->running()) {
        ImGui::TextWrapped("%s", phoneError_.empty() ? tr("Starting...") : phoneError_.c_str());
        ImGui::End();
        return;
    }
    // The pairing link: a new offer takes a moment (it gathers this computer's addresses), so it is made off the UI thread.
    if (deckLinkJob_.valid() && deckLinkJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        const auto made = deckLinkJob_.get();
        deckLinkError_ = made.first ? std::string() : made.second;
        deckQrText_ = made.first ? made.second : std::string();
        deckQr_ = {};
        std::string err;
        if (made.first && !link::makeQrCode(deckQrText_, deckQr_, err)) deckLinkError_ = err;
        deckLinkMade_ = ImGui::GetTime();
    }
    if (ds.connected && !deckQrText_.empty()) { // the offer is used up: after this phone leaves, a new link is made
        deckQrText_.clear();
        deckQr_ = {};
    }
    const bool stale = ImGui::GetTime() - deckLinkMade_ > 540.0; // a link nobody used for 9 minutes is replaced
    if (!deckLinkJob_.valid() && !ds.connected && ((deckQrText_.empty() && deckLinkError_.empty()) || stale) && phone_->deckEnabled()) {
        link::PhoneLink* phone = phone_.get();
        deckLinkJob_ = std::async(std::launch::async, [phone] {
            std::string url, err;
            const bool ok = phone->createDeckLink(url, err);
            return std::make_pair(ok, ok ? url : err);
        });
    }
    const float qrSize = 300 * scale;
    ImGui::InvisibleButton("##deckqr", ImVec2(qrSize, qrSize));
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetItemRectMin();
        const bool showQr = deckQr_.size > 0 && !ds.connected;
        dl->AddRectFilled(p, ImVec2(p.x + qrSize, p.y + qrSize), showQr ? IM_COL32_WHITE : IM_COL32(30, 34, 36, 255), 10.0f * scale);
        if (showQr) {
            const float cell = std::floor(qrSize / float(deckQr_.size + 8));
            const float off = std::floor((qrSize - cell * float(deckQr_.size)) * 0.5f);
            for (int y = 0; y < deckQr_.size; ++y)
                for (int x = 0; x < deckQr_.size; ++x)
                    if (deckQr_.dark(x, y))
                        dl->AddRectFilled(ImVec2(p.x + off + float(x) * cell, p.y + off + float(y) * cell),
                                          ImVec2(p.x + off + float(x + 1) * cell, p.y + off + float(y + 1) * cell), IM_COL32_BLACK);
        } else {
            // Not a QR code: the phone that is connected (same size, so the window doesn't jump), or what we are waiting for.
            auto centred = [&](const std::string& text, float y, ImU32 color) {
                const ImVec2 sz = ImGui::CalcTextSize(text.c_str());
                dl->AddText(ImVec2(p.x + (qrSize - sz.x) * 0.5f, p.y + y), color, text.c_str());
            };
            const float mid = qrSize * 0.5f;
            if (ds.connected) {
                dl->AddCircleFilled(ImVec2(p.x + mid, p.y + mid - 40 * scale), 7.0f * scale, ImGui::GetColorU32(kOk));
                centred(ds.device.empty() ? std::string(tr("A phone")) : ds.device, mid - 18 * scale, ImGui::GetColorU32(ImGuiCol_Text));
                centred(ds.transport == "relay" ? tr("Connected through the internet") : tr("Connected over Wi-Fi"), mid + 6 * scale,
                        ImGui::GetColorU32(kOk));
            } else {
                const bool failed = !deckLinkError_.empty();
                const float t = float(ImGui::GetTime());
                if (!failed) // a small spinner: the link is being made
                    for (int i = 0; i < 8; ++i) {
                        const float a = float(i) * 0.7854f;
                        const float fade = std::fmod(t * 1.5f - float(i) / 8.0f + 8.0f, 1.0f);
                        dl->AddCircleFilled(ImVec2(p.x + mid + std::cos(a) * 16.0f * scale, p.y + mid - 22 * scale + std::sin(a) * 16.0f * scale),
                                            2.6f * scale, IM_COL32(200, 200, 205, int(40 + 215 * (1.0f - fade))));
                    }
                centred(failed ? tr("No link") : tr("Preparing a secure link..."), mid + 8 * scale, IM_COL32(170, 170, 175, 255));
            }
        }
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 340 * scale);
    ImGui::TextWrapped("%s", tr("Scan with your phone's camera. The Deck is off again when this window closes."));
    if (!deckLinkError_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", deckLinkError_.c_str());
        if (ImGui::Button(tr("Try again##deck")) && !deckLinkJob_.valid()) deckLinkError_.clear();
    } else if (!ds.connected) {
        ImGui::TextDisabled("%s", tr("Waiting for your phone..."));
    }
    ImGui::TextDisabled("%s", tr("The phone can only pick one of the brush tiles, set Size and Intensity, Undo, Redo and Mirror."));
    if (ds.refused > 0) ImGui::TextDisabled("%d %s", ds.refused, tr("request(s) refused and logged"));
    ImGui::PopTextWrapPos();
    if (ImGui::Button(tr("Disconnect and close##deck"), ImVec2(-1, 0))) killDeck();
    ImGui::End();
}

} // namespace sf
