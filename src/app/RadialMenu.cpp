// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Radial (pie) menus (#45): the radial.menuN actions, opening a pie at the cursor from its key or a mouse / pen
// button, flick-and-release or tap-and-click, drawing it, and Preferences > Input > Radial Menus (menus, slots,
// labels, icons, import / export of one menu as a JSON file the user picks). A slot only names an action from the
// registry (the list the command palette searches); a menu file holds nothing that runs. The data, the slot
// geometry and the strict JSON reading are in core/RadialMenu.h.
#include "app/App.h"
#include "app/CommandPalette.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace sf {

namespace {

constexpr double kRadialTapSeconds = 0.25; // let go sooner, in the middle: a tap, the pie stays open
constexpr float kRadialDeadZone = 22.0f;   // pixels (times the UI scale) around the centre that pick nothing
constexpr float kRadialRadius = 104.0f;    // the slots' centres from the pie's centre

// The icons a slot may show instead of its action's own (ui.json stores the name).
struct RadialIcon {
    const char* name;
    const char* glyph;
};
constexpr RadialIcon kRadialIcons[] = {
    {"brush", ICON_LC_BRUSH},         {"paintbrush", ICON_LC_PAINTBRUSH}, {"pen", ICON_LC_PEN_TOOL},
    {"feather", ICON_LC_FEATHER},     {"eraser", ICON_LC_ERASER},         {"square", ICON_LC_SQUARE},
    {"square_dashed", ICON_LC_SQUARE_DASHED}, {"lasso", ICON_LC_LASSO},   {"circle", ICON_LC_CIRCLE},
    {"circle_dot", ICON_LC_CIRCLE_DOT}, {"star", ICON_LC_STAR},           {"heart", ICON_LC_HEART},
    {"eye", ICON_LC_EYE},             {"eye_off", ICON_LC_EYE_OFF},       {"layers", ICON_LC_LAYERS},
    {"grid", ICON_LC_GRID_2X2},       {"shapes", ICON_LC_SHAPES},         {"move", ICON_LC_MOVE},
    {"rotate", ICON_LC_ROTATE_CW},    {"scale", ICON_LC_SCALE_3D},        {"scissors", ICON_LC_SCISSORS},
    {"wand", ICON_LC_WAND_SPARKLES},  {"flip", ICON_LC_FLIP_HORIZONTAL_2}, {"hand", ICON_LC_HAND},
    {"droplet", ICON_LC_DROPLET},     {"palette", ICON_LC_PALETTE},       {"box", ICON_LC_BOX},
    {"sparkles", ICON_LC_SPARKLES},   {"zap", ICON_LC_ZAP},               {"sun", ICON_LC_SUN},
    {"moon", ICON_LC_MOON},           {"undo", ICON_LC_UNDO_2},           {"redo", ICON_LC_REDO_2},
    {"crosshair", ICON_LC_CROSSHAIR}, {"camera", ICON_LC_CAMERA},         {"save", ICON_LC_SAVE},
    {"mountain", ICON_LC_MOUNTAIN},   {"waves", ICON_LC_WAVES},           {"spline", ICON_LC_SPLINE},
    {"target", ICON_LC_TARGET},       {"magnet", ICON_LC_MAGNET},
};

const char* radialIconGlyph(std::string_view name) {
    for (const RadialIcon& i : kRadialIcons)
        if (name == i.name) return i.glyph;
    return nullptr;
}

std::string radialActionId(int menu) { return "radial.menu" + std::to_string(menu + 1); }

ImVec4 col(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

const char* const kDefaultKeys[] = {"T", "Y", "J"}; // free in every input style (tests/ui/radial_menu.sfui checks)

} // namespace

std::vector<RadialMenu> App::defaultRadialMenus() {
    RadialMenu brushes;
    brushes.name = "Brushes";
    for (const char* id : {"brush.standard", "brush.claybuildup", "brush.move", "brush.damstandard", "brush.inflate",
                           "brush.pinch", "brush.flatten", "brush.snakehook"})
        brushes.slots.push_back({id, "", ""});
    RadialMenu masking;
    masking.name = "Masking";
    for (const char* id : {"mask.invert", "mask.lasso", "mask.clear", "mask.rect", "mask.all", "mask.pen",
                           "view.mask", "polygroup.group_masked"})
        masking.slots.push_back({id, std::string_view(id) == "view.mask" ? "View Mask" : "", ""}); // (its own: "Mask")
    // The stroke types on one ring (U13, as 3D-Coat's stroke pop-up; single ring, as every radial menu here).
    RadialMenu strokes;
    strokes.name = "Strokes";
    for (const char* id : {"stroke.type.dots", "stroke.type.drag_stamp", "stroke.type.slide_stamp", "stroke.type.spray",
                           "stroke.type.lazy"})
        strokes.slots.push_back({id, "", ""});
    return {brushes, masking, strokes};
}

bool App::radialSlotAction(std::string_view id) const {
    if (id.rfind("radial.", 0) == 0) return false; // no pie inside a pie
    const Action* a = actions_.find(id);
    if (!a || !a->run || a->label.empty()) return false;
    if (!a->command.empty()) // a command that needs parameters would only open the console
        if (const Command* c = commands_.find(a->command))
            for (const ParamSpec& p : c->params)
                if (p.required) return false;
    return true;
}

RadialVocabulary App::radialVocabulary() const {
    RadialVocabulary v;
    v.action = [this](std::string_view id) { return radialSlotAction(id); };
    v.icon = [](std::string_view name) { return radialIconGlyph(name) != nullptr; };
    return v;
}

void App::registerRadialActions() {
    for (int i = 0; i < kRadialMenus; ++i) {
        Action a;
        a.id = radialActionId(i);
        a.icon = ICON_LC_CHART_PIE;
        a.palette = "Preferences > Input > Radial Menus";
        a.shortcut = i < int(std::size(kDefaultKeys)) ? kDefaultKeys[i] : "";
        a.run = [this, i] {
            openRadialMenu(i, keyDown_ != SDLK_UNKNOWN ? RadialHold::Key : RadialHold::None, int(keyDown_));
        };
        a.checked = [this, i] { return radial_.menu == i; };
        actions_.add(std::move(a));
    }
    // The menus ui.json brought (read before the actions existed), checked against the registry now; the defaults
    // are checked too (an action may be missing from this build).
    int dropped = 0;
    radialMenus_ = radialMenusFromJson(radialPending_ ? *radialPending_ : toJson(radialMenus_), radialVocabulary(), &dropped);
    radialPending_.reset();
    if (dropped) SDL_Log("Radial menus: %d unknown or extra entries dropped", dropped);
    refreshRadialActions();
}

void App::refreshRadialActions() {
    for (int i = 0; i < kRadialMenus; ++i) {
        const Action* old = actions_.find(radialActionId(i));
        if (!old) continue;
        Action a = *old;
        const RadialMenu* m = i < int(radialMenus_.size()) ? &radialMenus_[size_t(i)] : nullptr;
        a.label = "Radial menu " + std::to_string(i + 1) + (m && !m->name.empty() ? ": " + m->name : std::string());
        a.description = m && !m->slots.empty()
                            ? "Hold the key, flick toward an item and let go to run it; or tap the key and click one. "
                              "Edit it in Preferences > Input > Radial Menus."
                            : "Empty: give it items in Preferences > Input > Radial Menus.";
        a.enabled = [this, i] { return i < int(radialMenus_.size()) && !radialMenus_[size_t(i)].slots.empty(); };
        actions_.add(std::move(a));
    }
}

void App::applyRadialMenusJson(const Json& j) {
    if (actions_.find(radialActionId(0)) == nullptr) { // ui.json is read before the actions exist
        radialPending_ = j;
        return;
    }
    int dropped = 0;
    radialMenus_ = radialMenusFromJson(j, radialVocabulary(), &dropped);
    if (dropped) SDL_Log("Radial menus: %d unknown or extra entries dropped", dropped);
    closeRadialMenu();
    refreshRadialActions();
}

void App::resetRadialMenus() {
    applyRadialMenusJson(toJson(defaultRadialMenus()));
}

int App::radialMenuForButton(RadialButton b) const {
    if (b == RadialButton::None) return -1;
    for (size_t i = 0; i < radialMenus_.size(); ++i)
        if (radialMenus_[i].button == b && !radialMenus_[i].slots.empty()) return int(i);
    return -1;
}

PenAction App::penButtonAction(int button) const {
    if (button < 1 || button > kPenButtons) return PenAction::None;
    if (radialMenuForButton(RadialButton(int(RadialButton::PenLower) + button - 1)) >= 0) return PenAction::None;
    return uiPrefs_.tablet.buttons[size_t(button - 1)];
}

// ---------------------------------------------------------------------------
// Opening, flicking, clicking
// ---------------------------------------------------------------------------

void App::openRadialMenu(int menu, RadialHold hold, int code) {
    if (menu < 0 || menu >= int(radialMenus_.size()) || radialMenus_[size_t(menu)].slots.empty()) {
        status_ = "Radial menu " + std::to_string(menu + 1) + " is empty: Preferences > Input > Radial Menus";
        return;
    }
    if (radial_.menu == menu && radial_.hold == RadialHold::None) { // tapped open, its key again: close
        closeRadialMenu();
        return;
    }
    if (drag_ != Drag::None) return; // not in the middle of a stroke or a view drag
    quickMenu_.open = false;
    closeStrokePopup();
    const float s = uiScale_;
    // Keep the whole pie on the canvas; the flick is measured from wherever its centre ends up.
    const CanvasRect c = canvasRect();
    const float mx = (kRadialRadius + 70.0f) * s, my = (kRadialRadius + 24.0f) * s;
    ImVec2 at(mouseX_, mouseY_);
    if (c.w > 2.0f * mx) at.x = std::clamp(at.x, c.x + mx, c.x + c.w - mx);
    if (c.h > 2.0f * my) at.y = std::clamp(at.y, c.y + my, c.y + c.h - my);
    radial_.menu = menu;
    radial_.at = at;
    radial_.openedAt = ImGui::GetTime();
    radial_.hold = hold;
    radial_.code = code;
    radial_.hover = -1;
    radial_.openedFrame = ImGui::GetFrameCount();
}

void App::radialRelease() {
    if (radial_.menu < 0) return;
    const int count = int(radialMenus_[size_t(radial_.menu)].slots.size());
    const int slot = radialSlotAt(mouseX_ - radial_.at.x, mouseY_ - radial_.at.y, count, kRadialDeadZone * uiScale_);
    if (slot >= 0) { // flicked toward a slot
        runRadialSlot(slot);
        return;
    }
    if (ImGui::GetTime() - radial_.openedAt < kRadialTapSeconds) { // a tap: stays open for a click
        radial_.hold = RadialHold::None;
        return;
    }
    closeRadialMenu(); // held and let go in the middle: nothing
}

void App::runRadialSlot(int slot) {
    if (radial_.menu < 0) return;
    const RadialMenu& m = radialMenus_[size_t(radial_.menu)];
    const std::string id = slot >= 0 && slot < int(m.slots.size()) ? m.slots[size_t(slot)].action : std::string();
    closeRadialMenu();
    const Action* a = actions_.find(id);
    if (!a || !a->run) return;
    if (a->enabled && !a->enabled()) {
        status_ = a->label + " isn't available right now";
        return;
    }
    radial_.ran = slot;
    a->run();
}

void App::radialKeyUp(SDL_Keycode key) {
    if (radial_.menu >= 0 && radial_.hold == RadialHold::Key && SDL_Keycode(radial_.code) == key) radialRelease();
}

bool App::radialMouseEvent(const SDL_Event& e) {
    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN && e.type != SDL_EVENT_MOUSE_BUTTON_UP) return false;
    const int button = e.button.button;
    const uint32_t bit = button >= 0 && button < 32 ? 1u << button : 0u;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        if (radial_.menu >= 0 && radial_.hold == RadialHold::Mouse && radial_.code == button) {
            mouseX_ = e.button.x;
            mouseY_ = e.button.y;
            radialSwallowUp_ &= ~bit;
            radialRelease();
            return true;
        }
        if (radialSwallowUp_ & bit) { // the press went to the pie: so does the release
            radialSwallowUp_ &= ~bit;
            return true;
        }
        return false;
    }
    if (radial_.menu >= 0) {
        // Open: a left click (mouse or pen tip) on a slot runs it; any other click, or one in the middle or beside
        // the pie, closes it. The click never reaches the canvas or the panels under the pie.
        radialSwallowUp_ |= bit;
        if (radial_.hold == RadialHold::Mouse && radial_.code == button) return true;
        if (button == SDL_BUTTON_LEFT) {
            const int count = int(radialMenus_[size_t(radial_.menu)].slots.size());
            const int slot = radialSlotAt(e.button.x - radial_.at.x, e.button.y - radial_.at.y, count,
                                          kRadialDeadZone * uiScale_);
            const float r = (kRadialRadius + 70.0f) * uiScale_;
            const float dx = e.button.x - radial_.at.x, dy = e.button.y - radial_.at.y;
            if (slot >= 0 && dx * dx + dy * dy <= r * r) {
                runRadialSlot(slot);
                return true;
            }
        }
        closeRadialMenu();
        return true;
    }
    if (e.button.which == SDL_PEN_MOUSEID) return false; // pen buttons come through radialPenButton
    // Only a plain press opens a menu (#74): with Alt, Ctrl, Shift or the system key held the button keeps its
    // navigation or tool binding (Alt+middle pans, Alt+right zooms in the Maya style, Shift+right turns the view).
    if (SDL_GetModState() & (SDL_KMOD_SHIFT | SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) return false;
    const RadialButton b = button == SDL_BUTTON_MIDDLE ? RadialButton::MouseMiddle
                           : button == SDL_BUTTON_RIGHT ? RadialButton::MouseRight
                           : button == SDL_BUTTON_X1   ? RadialButton::MouseBack
                           : button == SDL_BUTTON_X2   ? RadialButton::MouseForward
                                                       : RadialButton::None;
    const int menu = radialMenuForButton(b);
    if (menu < 0 || ImGui::GetIO().WantCaptureMouse || !canvasRect().contains(e.button.x, e.button.y)) return false;
    mouseX_ = e.button.x;
    mouseY_ = e.button.y;
    radialSwallowUp_ |= bit;
    openRadialMenu(menu, RadialHold::Mouse, button);
    return true;
}

bool App::radialPenButton(int button, bool down, float x, float y) {
    if (button < 1 || button > kPenButtons) return false;
    const int menu = radialMenuForButton(RadialButton(int(RadialButton::PenLower) + button - 1));
    if (down) {
        if (menu < 0) return false;
        if (radial_.menu < 0 && !ImGui::GetIO().WantCaptureMouse && canvasRect().contains(x, y)) {
            mouseX_ = x;
            mouseY_ = y;
            openRadialMenu(menu, RadialHold::Pen, button);
        }
        return true; // the button belongs to the menu, not to its Preferences > Tablet mapping
    }
    if (radial_.menu >= 0 && radial_.hold == RadialHold::Pen && radial_.code == button) {
        mouseX_ = x;
        mouseY_ = y;
        radialRelease();
        return true;
    }
    return menu >= 0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void App::drawRadialMenu() {
    if (radial_.menu < 0) return;
    if (radial_.menu >= int(radialMenus_.size()) || radialMenus_[size_t(radial_.menu)].slots.empty()) {
        closeRadialMenu();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        closeRadialMenu();
        return;
    }
    const RadialMenu& m = radialMenus_[size_t(radial_.menu)];
    const float s = uiScale_;
    const int count = int(m.slots.size());
    const ImVec2 c = radial_.at;
    radial_.hover = radialSlotAt(mouseX_ - c.x, mouseY_ - c.y, count, kRadialDeadZone * s);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float dead = kRadialDeadZone * s, radius = kRadialRadius * s;
    // The centre: the dead zone, the menu's name, and a line toward the pointer once it leaves the dead zone.
    dl->AddCircleFilled(c, radius * 0.42f, IM_COL32(0x1b, 0x1b, 0x1d, 0x90), 48);
    dl->AddCircle(c, dead, IM_COL32(0x8c, 0x8c, 0x90, 0xa0), 32, 1.5f * s);
    if (radial_.hover >= 0) {
        float x = 0.0f, y = 0.0f;
        radialSlotDirection(radial_.hover, count, x, y);
        dl->AddLine(ImVec2(c.x + x * dead, c.y + y * dead), ImVec2(c.x + x * radius * 0.6f, c.y + y * radius * 0.6f),
                    ui::color::accent, 2.5f * s);
    }
    if (!m.name.empty()) {
        const ImVec2 ts = ImGui::CalcTextSize(m.name.c_str());
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y + dead + 4.0f * s), ui::color::textDim, m.name.c_str());
    }
    for (int i = 0; i < count; ++i) {
        const RadialSlot& slot = m.slots[size_t(i)];
        const Action* a = actions_.find(slot.action);
        const std::string& label = !slot.label.empty() ? slot.label : a ? a->label : slot.action;
        const char* icon = !slot.icon.empty() ? radialIconGlyph(slot.icon) : a ? a->icon : nullptr;
        const bool enabled = a && (!a->enabled || a->enabled());
        const bool checked = a && a->checked && a->checked();
        float x = 0.0f, y = 0.0f;
        radialSlotDirection(i, count, x, y);
        const ImVec2 p(c.x + x * radius, c.y + y * radius);
        const float iconW = icon ? ImGui::GetFontSize() + 6.0f * s : 0.0f;
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const float w = ts.x + iconW + 20.0f * s, h = ts.y + 14.0f * s;
        // Labels lean away from the centre: the ones on the right grow rightward, on the left leftward.
        const float left = std::abs(x) < 0.2f ? p.x - w * 0.5f : x > 0.0f ? p.x - 18.0f * s : p.x - w + 18.0f * s;
        const ImVec2 a0(left, p.y - h * 0.5f), a1(left + w, p.y + h * 0.5f);
        const bool hot = i == radial_.hover;
        dl->AddRectFilled(a0, a1, hot ? IM_COL32(0x3a, 0x30, 0x28, 0xf8) : IM_COL32(0x25, 0x25, 0x28, 0xf2), h * 0.5f);
        dl->AddRect(a0, a1, hot ? ui::color::accent : checked ? IM_COL32(0xf0, 0x88, 0x2e, 0x80) : ui::color::border,
                    h * 0.5f, 0, hot ? 2.0f * s : 1.0f);
        const ImU32 fg = !enabled ? ui::color::textDim : hot ? IM_COL32(0xff, 0xff, 0xff, 0xff) : ui::color::text;
        float tx = a0.x + 10.0f * s;
        if (icon) {
            dl->AddText(ImVec2(tx, a0.y + 7.0f * s), hot || checked ? ui::color::accent : ui::color::textDim, icon);
            tx += iconW;
        }
        dl->AddText(ImVec2(tx, a0.y + 7.0f * s), fg, label.c_str());
    }
}

// ---------------------------------------------------------------------------
// Import / export
// ---------------------------------------------------------------------------

bool App::exportRadialMenu(int menu, const std::string& path) {
    if (menu < 0 || menu >= int(radialMenus_.size())) return false;
    std::ofstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary | std::ios::trunc);
    f << radialMenuFileText(radialMenus_[size_t(menu)]);
    if (!f) {
        status_ = "Could not write " + path;
        return false;
    }
    status_ = "Radial menu saved to " + path;
    return true;
}

bool App::importRadialMenu(const std::string& path) {
    const std::filesystem::path p(std::u8string(path.begin(), path.end()));
    std::error_code ec;
    const auto size = std::filesystem::file_size(p, ec);
    if (ec || !std::filesystem::is_regular_file(p, ec)) {
        status_ = "Could not read " + path;
        return false;
    }
    if (size > kRadialFileMax) { // refused unread
        status_ = "That file is too big for a radial menu";
        return false;
    }
    std::ifstream f(p, std::ios::binary);
    std::string text(kRadialFileMax + 1, '\0'); // never more than the cap, even if the file grew since
    f.read(text.data(), std::streamsize(text.size()));
    text.resize(size_t(std::max<std::streamsize>(f.gcount(), 0)));
    RadialMenu menu;
    std::string err;
    int dropped = 0;
    if (!f.is_open() || !parseRadialMenuFile(text, radialVocabulary(), menu, err, &dropped)) {
        status_ = err.empty() ? "Could not read " + path : err;
        return false;
    }
    // Into the menu being edited when it's empty, otherwise a new one.
    int at = radialEdit_.menu;
    if (at < 0 || at >= int(radialMenus_.size()) || !radialMenus_[size_t(at)].slots.empty()) {
        if (int(radialMenus_.size()) >= kRadialMenus) {
            status_ = "Eight radial menus already: clear one first";
            return false;
        }
        radialMenus_.emplace_back();
        at = int(radialMenus_.size()) - 1;
    }
    radialMenus_[size_t(at)] = std::move(menu);
    radialEdit_.menu = at;
    refreshRadialActions();
    status_ = "Radial menu " + std::to_string(at + 1) + " imported" +
              (dropped ? " (" + std::to_string(dropped) + " unknown items left out)" : std::string()) +
              " - Store Config keeps it";
    return true;
}

// ---------------------------------------------------------------------------
// Preferences > Input > Radial Menus
// ---------------------------------------------------------------------------

void App::drawRadialMenusSection() {
    const char* path = "Preferences > Input > Radial Menus";
    ImGui::Spacing();
    ImGui::SeparatorText("Radial Menus");
    ui::HelpText("Radial Menus", nullptr,
                 "Pie menus of your own: hold a menu's key (or its mouse or pen button), flick toward an item and let "
                 "go to run it. Tap the key instead and the pie stays open: click an item, or beside the pie to close "
                 "it. Up to 8 menus of 8 items; an item is anything the command palette (F3) finds.",
                 path);
    if (radialMenus_.empty()) radialEdit_.menu = -1;
    radialEdit_.menu = std::clamp(radialEdit_.menu, radialMenus_.empty() ? -1 : 0, int(radialMenus_.size()) - 1);
    auto menuTitle = [&](int i) {
        const RadialMenu& m = radialMenus_[size_t(i)];
        const std::string key = actions_.shortcutText(radialActionId(i));
        return std::to_string(i + 1) + "  " + (m.name.empty() ? std::string(m.slots.empty() ? "(empty)" : "Menu") : m.name) +
               (key.empty() ? std::string() : "  (" + key + ")");
    };
    const float addW = ImGui::GetFrameHeight() * 1.2f;
    ImGui::SetNextItemWidth(-(addW + ImGui::GetStyle().ItemSpacing.x));
    if (ImGui::BeginCombo("##radial.menu", radialEdit_.menu >= 0 ? menuTitle(radialEdit_.menu).c_str() : "No menus")) {
        for (int i = 0; i < int(radialMenus_.size()); ++i)
            if (ImGui::Selectable((menuTitle(i) + "###radialmenu" + std::to_string(i + 1)).c_str(), i == radialEdit_.menu))
                radialEdit_.menu = i, radialEdit_.pickSlot = -2, radialEdit_.selected = -1;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(int(radialMenus_.size()) >= kRadialMenus);
    if (ImGui::Button(ICON_LC_PLUS "###radial.add_menu", ImVec2(addW, 0))) {
        radialMenus_.emplace_back();
        radialMenus_.back().name = "Menu " + std::to_string(radialMenus_.size());
        radialEdit_.menu = int(radialMenus_.size()) - 1;
        radialEdit_.pickSlot = -1; // straight to picking its first item
        radialEdit_.selected = -1;
        radialEdit_.query[0] = 0;
        radialEdit_.focusSearch = true;
        refreshRadialActions();
    }
    ImGui::EndDisabled();
    ui::HelpText("Add menu", nullptr, "A new radial menu (up to 8). Give it a key, then items.", path);

    if (radialEdit_.menu >= 0) {
        const int mi = radialEdit_.menu;
        RadialMenu& m = radialMenus_[size_t(mi)];
        ImGui::PushID(mi);
        // Name.
        char name[kRadialNameMax + 1];
        std::snprintf(name, sizeof name, "%s", m.name.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::InputTextWithHint("##radial.name", "Name", name, sizeof name)) {
            m.name = radialCleanText(name, kRadialNameMax);
            refreshRadialActions();
        }
        // Key (the keymap's, like any action's) and button.
        ImGui::TextDisabled("Key");
        ImGui::SameLine();
        const std::string id = radialActionId(mi);
        const std::string key = actions_.shortcutText(id);
        if (ImGui::Button(((key.empty() ? std::string("Set key") : key) + "###radial.key").c_str()))
            beginShortcutCapture(id);
        ui::HelpText("Key", nullptr,
                     "Click, then press the key that opens this menu (Esc cancels, Backspace clears). A key another "
                     "action has moves here. Preferences > Hotkeys lists it too.",
                     path);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##radial.button", radialButtonLabel(m.button))) {
            for (int b = 0; b < int(RadialButton::Count); ++b)
                if (ImGui::Selectable(radialButtonLabel(RadialButton(b)), int(m.button) == b)) m.button = RadialButton(b);
            ImGui::EndCombo();
        }
        ui::HelpText("Button", nullptr,
                     "A mouse or pen button that opens this menu while held, besides the key. It then stops doing "
                     "what it did before (a pen button's Preferences > Tablet mapping, the middle button's pan, the "
                     "right button's turn). Mouse buttons open it only when no modifier key is held: Alt+middle still "
                     "pans, Alt+right still zooms (Maya style), Shift+right still turns the view.",
                     path);
        if (m.button == RadialButton::MouseRight) {
            // The view must stay reachable (#74): say how it turns now.
            const char* how = blenderStyle() ? "middle-drag (or Alt+left-drag)"
                              : mayaStyle()  ? "Alt+left-drag"
                              : uiPrefs_.airStrokes ? "Shift+right-drag (Start strokes off the model is on, so a left-drag "
                                                      "beside the model sculpts)"
                                                    : "Shift+right-drag, or a left-drag beside the model";
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Right-drag opens this menu. Turn the view with %s.", how);
            ImGui::PopTextWrapPos();
        }

        // Items: one row each (the action, up / down / remove); the selected one also shows its own label and icon,
        // and Change action.
        const float rowH = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        for (int i = 0; i < int(m.slots.size()); ++i) {
            RadialSlot& slot = m.slots[size_t(i)];
            const Action* a = actions_.find(slot.action);
            const bool selected = radialEdit_.selected == i;
            ImGui::PushID(i);
            const char* glyph = !slot.icon.empty() ? radialIconGlyph(slot.icon) : a ? a->icon : nullptr;
            const std::string shown = !slot.label.empty() ? slot.label : a ? a->label : slot.action;
            const std::string text = std::to_string(i + 1) + "  " + (glyph ? std::string(glyph) + " " : std::string()) + shown;
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button((text + "###radial.slot").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - 3.0f * (rowH + spacing), 0)))
                radialEdit_.selected = selected ? -1 : i, radialEdit_.pickSlot = -2;
            if (selected) ImGui::PopStyleColor();
            ui::HelpText("Item", nullptr, "Click to change its label, icon or action.", path);
            ImGui::SameLine();
            int move = 0;
            ImGui::BeginDisabled(i == 0);
            if (ImGui::Button(ICON_LC_CHEVRON_UP "##radial.up", ImVec2(rowH, 0))) move = -1;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i + 1 == int(m.slots.size()));
            if (ImGui::Button(ICON_LC_CHEVRON_DOWN "##radial.down", ImVec2(rowH, 0))) move = 1;
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool remove = ui::GlyphButton("##radial.remove", ICON_LC_X, ImVec2(rowH, 0));
            if (selected) {
                ImGui::Indent(rowH * 0.5f);
                char label[kRadialLabelMax + 1];
                std::snprintf(label, sizeof label, "%s", slot.label.c_str());
                const float iconW = 2.6f * rowH;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - iconW - spacing);
                if (ImGui::InputTextWithHint("##radial.label", a ? a->label.c_str() : "Label", label, sizeof label))
                    slot.label = radialCleanText(label, kRadialLabelMax);
                ui::HelpText("Label", nullptr, "What the pie shows for it; empty: the action's own name.", path);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(iconW);
                if (ImGui::BeginCombo("##radial.icon", glyph ? glyph : "-")) {
                    if (ImGui::Selectable("Action's own###radial.icon_none", slot.icon.empty())) slot.icon.clear();
                    for (const RadialIcon& ic : kRadialIcons)
                        if (ImGui::Selectable((std::string(ic.glyph) + "  " + ic.name + "###radial.icon." + ic.name).c_str(),
                                              slot.icon == ic.name))
                            slot.icon = ic.name;
                    ImGui::EndCombo();
                }
                ui::HelpText("Icon", nullptr, "The icon the pie shows for it.", path);
                if (ImGui::Button(ICON_LC_SEARCH "  Change action###radial.change", ImVec2(-FLT_MIN, 0))) {
                    radialEdit_.pickSlot = radialEdit_.pickSlot == i ? -2 : i;
                    radialEdit_.query[0] = 0;
                    radialEdit_.focusSearch = true;
                }
                ImGui::Unindent(rowH * 0.5f);
            }
            ImGui::PopID();
            if (remove) {
                m.slots.erase(m.slots.begin() + i);
                radialEdit_.pickSlot = -2;
                radialEdit_.selected = -1;
                break;
            }
            if (move) {
                std::swap(m.slots[size_t(i)], m.slots[size_t(i + move)]);
                radialEdit_.pickSlot = -2;
                if (selected) radialEdit_.selected = i + move;
                break;
            }
        }
        if (int(m.slots.size()) < kRadialSlots && ImGui::Button(ICON_LC_PLUS "  Add item###radial.add_slot", ImVec2(-FLT_MIN, 0))) {
            radialEdit_.pickSlot = radialEdit_.pickSlot == -1 ? -2 : -1;
            radialEdit_.query[0] = 0;
            radialEdit_.focusSearch = true;
        }

        // Picking an action: the command palette's search, in place.
        if (radialEdit_.pickSlot >= -1) {
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (radialEdit_.focusSearch) ImGui::SetKeyboardFocusHere();
            radialEdit_.focusSearch = false;
            ImGui::InputTextWithHint("##radial.search", ICON_LC_SEARCH "  Search actions", radialEdit_.query,
                                     sizeof radialEdit_.query);
            if (radialEdit_.foundQuery != radialEdit_.query || radialEdit_.foundRevision != actions_.revision()) {
                radialEdit_.foundQuery = radialEdit_.query;
                radialEdit_.foundRevision = actions_.revision();
                radialEdit_.found = CommandPalette::find(*this, radialEdit_.query, 8,
                                                         [this](const Action& a) { return radialSlotAction(a.id); });
            }
            const std::vector<size_t> found = radialEdit_.found; // (picking one changes the registry's labels)
            const auto& all = actions_.all();
            for (size_t k : found) {
                const Action& a = all[k];
                const std::string row = std::string(a.icon ? a.icon : ICON_LC_CHEVRON_RIGHT) + "  " + a.label + "###radial.pick." + a.id;
                if (ImGui::Selectable(row.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups)) {
                    if (radialEdit_.pickSlot >= 0 && radialEdit_.pickSlot < int(m.slots.size()))
                        m.slots[size_t(radialEdit_.pickSlot)] = {a.id, "", ""};
                    else if (int(m.slots.size()) < kRadialSlots)
                        m.slots.push_back({a.id, "", ""});
                    radialEdit_.pickSlot = -2;
                    refreshRadialActions();
                }
                if (ImGui::IsItemHovered() && !a.palette.empty()) ImGui::SetTooltip("%s", a.palette.c_str());
            }
            if (found.empty()) ImGui::TextDisabled("No matching actions");
        }

        // Import / export / clear.
        ImGui::Spacing();
        const float third = (ImGui::GetContentRegionAvail().x - 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
        if (ImGui::Button("Export...##radial.export", ImVec2(third, 0))) exportRadialMenuDialog(mi);
        ui::HelpText("Export", nullptr, "Save this menu as a .json file to share or keep (action names only).", path);
        ImGui::SameLine();
        if (ImGui::Button("Import...##radial.import", ImVec2(third, 0))) importRadialMenuDialog();
        ui::HelpText("Import", nullptr,
                     "Load a menu someone exported. Only action names are read; ones this version doesn't have are "
                     "left out. Goes into this menu if it's empty, otherwise into a new one.",
                     path);
        ImGui::SameLine();
        if (ImGui::Button("Clear##radial.clear", ImVec2(third, 0))) {
            m = RadialMenu();
            while (!radialMenus_.empty() && radialMenus_.back().slots.empty() && radialMenus_.back().name.empty())
                radialMenus_.pop_back(); // empty menus at the end go; one in the middle keeps its place (and key)
            radialEdit_.pickSlot = -2;
            refreshRadialActions();
        }
        ui::HelpText("Clear", nullptr, "Empty this menu. Its key stays; an empty menu does nothing.", path);
        ImGui::PopID();
    } else if (ImGui::Button("Import...##radial.import", ImVec2(-FLT_MIN, 0))) {
        importRadialMenuDialog();
    }
    if (ImGui::Button("Restore default menus##radial.defaults", ImVec2(-FLT_MIN, 0))) {
        resetRadialMenus();
        radialEdit_ = RadialEdit();
        status_ = "Default radial menus restored - Store Config (Ctrl+Shift+I) keeps them";
    }
    ui::HelpText("Restore default menus", nullptr, "Back to the three built-in menus: Brushes (T), Masking (Y) and Strokes (J).", path);
    ImGui::TextColored(col(ui::color::textDim), "Store Config keeps them");
}

} // namespace sf
