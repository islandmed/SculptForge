// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reference images (View > Reference images; core/Reference.h; idea by Oslen on Discord): pictures that stay in the
// view while you sculpt. The renderer draws them (Renderer::setReferences); this file is the View palette section,
// the file dialogs and drops, and Adjust: handles on the selected one (drag inside to move it, a corner to scale
// it, Shift keeps its proportions; the arrow keys move it, - and = scale it; Enter or Esc ends). Outside Adjust a
// reference takes no clicks: strokes, picks and navigation go straight through to the model.
#include "app/App.h"
#include "app/I18n.h"
#include "app/NativeDialog.h"
#include "app/Palettes.h"
#include "app/Theme.h"
#include "app/Ui.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace sf {

namespace {

constexpr float kCornerPx = 6.0f; // corner handle half-size, logical px
const SDL_DialogFileFilter kReferenceFilters[] = {{"Images", "png;jpg;jpeg"}, {"All images", "png;jpg;jpeg;bmp;tga;gif"}};
const char* kWhere = "View > Reference images";

double now() { return double(SDL_GetTicksNS()) * 1e-9; }

// "photo.jpg" from a UTF-8 path.
std::string fileName(const std::string& path) {
    const auto name = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().u8string();
    return std::string(name.begin(), name.end());
}

// The pointer in the reference's own plane: Behind, window pixels; In the scene, world units along its axes from
// `origin` (false when the ray misses the plane).
bool localPoint(const Camera& cam, const CanvasRect& c, const ReferenceImage& r, glm::vec3 origin, glm::vec2 p,
                glm::vec2& out) {
    if (r.mode == ReferenceMode::Behind) {
        out = p;
        return true;
    }
    glm::vec3 o, d, u, v, n;
    cam.screenRay(p.x - c.x, p.y - c.y, c.w, c.h, o, d);
    r.axes(u, v, n);
    const float dn = glm::dot(d, n);
    if (std::abs(dn) < 1e-6f) return false;
    const glm::vec3 hit = o + d * (glm::dot(origin - o, n) / dn);
    out = {glm::dot(hit - origin, u), glm::dot(hit - origin, v)};
    return true;
}

} // namespace

void SDLCALL App::onReferenceChosen(void* userdata, const char* const* files, int) {
    wakeMainLoop();
    auto* app = static_cast<App*>(userdata);
    std::lock_guard lock(app->alphaMutex_);
    app->refUi_.pending.clear();
    for (int i = 0; files && files[i]; ++i) app->refUi_.pending.emplace_back(files[i]);
    app->refUi_.pendingKind = app->refUi_.dialogKind;
}

void App::openReferenceDialog(int kind) {
    refUi_.dialogKind = kind;
    sf::dialogs::openFile(App::onReferenceChosen, this, window_, kReferenceFilters, 2, nullptr, kind == 0,
                          kind == 1 ? tr("Front reference image") : kind == 2 ? tr("Side reference image") : tr("Reference images"));
}

void App::pollReferenceFiles() {
    std::vector<std::string> files;
    int kind = 0;
    {
        std::lock_guard lock(alphaMutex_);
        files.swap(refUi_.pending);
        kind = refUi_.pendingKind;
    }
    if (files.empty()) return;
    if (kind == 1) { // the pair's front: now its side
        refUi_.pairFront = files.front();
        status_ = "Front + side: now pick the side image";
        openReferenceDialog(2);
        return;
    }
    if (kind == 2) {
        runCommand("reference_pair", {{"front", refUi_.pairFront}, {"side", files.front()}});
        refUi_.pairFront.clear();
    } else {
        for (const std::string& f : files) runCommand("reference_add", {{"path", f}});
    }
    refUi_.selected = int(doc_.references.size()) - 1;
}

bool App::takeReferenceDrop(const std::string& path) {
    if (refUi_.sectionShownAt < 0.0 || now() - refUi_.sectionShownAt > 1.0 || !Image::isImagePath(path)) return false;
    runCommand("reference_add", {{"path", path}});
    refUi_.selected = int(doc_.references.size()) - 1;
    return true;
}

void App::registerReferenceActions() {
    auto selected = [this]() -> ReferenceImage* {
        return refUi_.selected >= 0 && refUi_.selected < int(doc_.references.size()) ? &doc_.references[size_t(refUi_.selected)]
                                                                                     : nullptr;
    };
    Action a;
    a.id = "reference.add";
    a.label = "Add images...";
    a.icon = ICON_LC_IMAGE_PLUS;
    a.palette = kWhere;
    a.description = "Pictures that stay in the view while you sculpt, to match against: behind the model, or as a "
                    "plane at its front, side or top. They never take strokes. Dropping an image on the window while "
                    "this section shows adds it too.";
    a.command = "reference_add";
    a.run = [this] { openReferenceDialog(0); };
    actions_.add(a);

    a = {};
    a.id = "reference.pair";
    a.label = "Front + side...";
    a.icon = ICON_LC_COLUMNS_2;
    a.palette = kWhere;
    a.description = "Two images, a front and a side view: each a plane in the scene behind the model, the same "
                    "height, so the front shows in the Front view and the side in the Right view.";
    a.command = "reference_pair";
    a.run = [this] { openReferenceDialog(1); };
    actions_.add(a);

    a = {};
    a.id = "reference.show_all";
    a.label = "Show Reference Images";
    a.icon = ICON_LC_EYE;
    a.palette = kWhere;
    a.description = "Hide every reference image, or show them again.";
    a.command = "reference_set";
    a.run = [this] {
        const bool any = std::any_of(doc_.references.begin(), doc_.references.end(), [](const ReferenceImage& r) { return r.visible; });
        for (size_t i = 0; i < doc_.references.size(); ++i) runCommand("reference_set", {{"index", double(i)}, {"visible", !any}});
        if (any) refUi_.adjust = false;
    };
    a.checked = [this] {
        return std::any_of(doc_.references.begin(), doc_.references.end(), [](const ReferenceImage& r) { return r.shown(); });
    };
    a.enabled = [this] { return !doc_.references.empty(); };
    actions_.add(a);

    a = {};
    a.id = "reference.adjust";
    a.label = "Adjust Reference";
    a.icon = ICON_LC_MOVE;
    a.palette = kWhere;
    a.description = "Handles on the selected reference image: drag inside it to move it, a corner to scale it (Shift "
                    "keeps its proportions). Arrow keys move it, - and = scale it. Click another one to adjust that. "
                    "Enter or Esc goes back to sculpting.";
    a.run = [this, selected] {
        ReferenceImage* r = selected();
        refUi_.adjust = !refUi_.adjust && r && r->shown();
        status_ = refUi_.adjust ? "Adjust Reference: drag to move, a corner to scale (Shift: keep proportions; Enter: done)"
                                : "Adjust Reference off";
    };
    a.checked = [this] { return refUi_.adjust; };
    a.enabled = [selected] { return selected() && selected()->shown(); };
    actions_.add(a);

    struct Choice {
        const char *id, *label, *key, *value, *help;
    };
    static constexpr Choice kChoices[] = {
        {"reference.mode_behind", "Behind the model", "mode", "behind",
         "A backdrop fixed to the view, under the model: it stays put while the view turns."},
        {"reference.mode_scene", "In the scene", "mode", "scene",
         "A plane by the model that turns with the view (and fades out seen edge-on)."},
        {"reference.plane_front", "Front", "plane", "front", "The plane behind the model as the Front view sees it."},
        {"reference.plane_side", "Side", "plane", "side", "The plane behind the model as the Right view sees it."},
        {"reference.plane_top", "Top", "plane", "top", "The plane under the model, as the Top view sees it."},
    };
    for (const Choice& c : kChoices) {
        a = {};
        a.id = c.id;
        a.label = c.label;
        a.palette = kWhere;
        a.description = c.help;
        a.command = "reference_set";
        const std::string key = c.key, value = c.value;
        a.run = [this, key, value] {
            if (refUi_.selected >= 0 && refUi_.selected < int(doc_.references.size()))
                runCommand("reference_set", {{"index", double(refUi_.selected)}, {key, value}});
        };
        a.checked = [selected, key, value] {
            const ReferenceImage* r = selected();
            if (!r) return false;
            return key == "mode" ? (r->mode == ReferenceMode::Scene) == (value == "scene")
                                 : r->mode == ReferenceMode::Scene &&
                                       r->plane == (value == "side"  ? ReferencePlane::Side
                                                    : value == "top" ? ReferencePlane::Top
                                                                     : ReferencePlane::Front);
        };
        a.enabled = [selected] { return selected() != nullptr; };
        actions_.add(a);
    }
}

bool App::referenceCorners(const ReferenceImage& r, std::array<glm::vec2, 4>& out) const {
    const CanvasRect c = canvasRect();
    if (c.w <= 0.0f || c.h <= 0.0f || !r.shown()) return false;
    const std::array<glm::vec3, 4> k = r.corners(c.w / c.h);
    if (r.mode == ReferenceMode::Behind) {
        for (size_t i = 0; i < 4; ++i) out[i] = {c.x + k[i].x * c.w, c.y + k[i].y * c.h};
        return true;
    }
    if (r.facing(camera_.view(), camera_.ortho) < 0.05f) return false; // edge-on: hidden, so not adjustable
    const glm::mat4 vp = camera_.projection() * camera_.view();
    for (size_t i = 0; i < 4; ++i) {
        const glm::vec4 clip = vp * glm::vec4(k[i], 1.0f);
        if (clip.w <= 1e-6f) return false; // (a corner behind the eye)
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        out[i] = {c.x + (ndc.x * 0.5f + 0.5f) * c.w, c.y + (0.5f - ndc.y * 0.5f) * c.h};
    }
    return true;
}

bool App::referenceMouseDown(float x, float y, SDL_Keymod mod) {
    if (!refUi_.adjust || (mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI))) return false;
    const float grab = (kCornerPx + 6.0f) * uiScale_;
    // The selected one's handles first, then any other unlocked one under the pointer (the top one: drawn last).
    int pick = -1, handle = -1;
    std::array<glm::vec2, 4> c;
    if (refUi_.selected >= 0 && refUi_.selected < int(doc_.references.size())) {
        const ReferenceImage& r = doc_.references[size_t(refUi_.selected)];
        if (!r.locked && referenceCorners(r, c) && (handle = referenceHandleAt(c, {x, y}, grab)) >= 0) pick = refUi_.selected;
    }
    for (int i = int(doc_.references.size()) - 1; pick < 0 && i >= 0; --i) {
        const ReferenceImage& r = doc_.references[size_t(i)];
        if (!r.locked && referenceCorners(r, c) && referenceHandleAt(c, {x, y}, 0.0f) == 0) {
            pick = i;
            handle = 0;
        }
    }
    if (pick < 0) return true; // (off them: nothing, while adjusting; Enter goes back to sculpting)
    refUi_.selected = pick;
    const ReferenceImage& r = doc_.references[size_t(pick)];
    const CanvasRect cr = canvasRect();
    if (!localPoint(camera_, cr, r, r.origin, {x, y}, refUi_.startLocal)) return true;
    refUi_.handle = handle;
    refUi_.start = r;
    refUi_.startMouse = {x, y};
    drag_ = Drag::Reference;
    return true;
}

void App::referenceMouseMove(float x, float y) {
    const CanvasRect c = canvasRect();
    if (c.w <= 0.0f || c.h <= 0.0f || refUi_.handle < 0 || refUi_.selected < 0 || refUi_.selected >= int(doc_.references.size()))
        return;
    ReferenceImage& r = doc_.references[size_t(refUi_.selected)];
    const ReferenceImage& s0 = refUi_.start;
    glm::vec2 p;
    if (!localPoint(camera_, c, s0, s0.origin, {x, y}, p)) return;
    const bool behind = s0.mode == ReferenceMode::Behind;
    if (refUi_.handle == 0) {
        if (behind) {
            r.center = s0.center + (p - refUi_.startLocal) / glm::vec2(c.w, c.h);
        } else {
            glm::vec3 u, v, n;
            s0.axes(u, v, n);
            const glm::vec2 d = p - refUi_.startLocal;
            r.origin = s0.origin + u * d.x + v * d.y;
        }
    } else { // a corner: scaled about the centre, each way on its own unless Shift keeps the proportions
        const glm::vec2 centre = behind ? s0.center * glm::vec2(c.w, c.h) + glm::vec2(c.x, c.y) : glm::vec2(0.0f);
        const float h0 = behind ? s0.height * c.h : s0.sceneHeight;
        const glm::vec2 half0 = 0.5f * glm::vec2(h0 * s0.aspect() * s0.stretch, h0);
        glm::vec2 half = glm::abs(p - centre);
        if (inputMods() & SDL_KMOD_SHIFT) half = half0 * std::max(half.x / std::max(half0.x, 1e-6f), half.y / std::max(half0.y, 1e-6f));
        half = glm::max(half, half0 * 0.02f);
        const float h = 2.0f * half.y;
        r.stretch = std::clamp(half.x / std::max(half.y * s0.aspect(), 1e-6f), 0.05f, 20.0f);
        if (behind) r.height = std::clamp(h / c.h, 0.01f, 20.0f);
        else r.sceneHeight = std::clamp(h, 1e-6f, 1e6f);
    }
    doc_.modified = true;
}

void App::referenceMouseUp() { refUi_.handle = -1; }

bool App::referenceKey(SDL_Keycode key, SDL_Keymod mod) {
    if (!refUi_.adjust) return false;
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE) {
        refUi_.adjust = false;
        status_ = "Adjust Reference off";
        return true;
    }
    if ((mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) || refUi_.selected < 0 || refUi_.selected >= int(doc_.references.size()))
        return false;
    ReferenceImage& r = doc_.references[size_t(refUi_.selected)];
    if (r.locked) return false;
    const CanvasRect c = canvasRect();
    const bool fine = mod & SDL_KMOD_SHIFT;
    const float stepPx = (fine ? 1.0f : 10.0f) * uiScale_;
    glm::vec2 move(0.0f);
    float scale = 1.0f;
    switch (key) {
    case SDLK_LEFT: move.x = -stepPx; break;
    case SDLK_RIGHT: move.x = stepPx; break;
    case SDLK_UP: move.y = -stepPx; break;
    case SDLK_DOWN: move.y = stepPx; break;
    case SDLK_MINUS: case SDLK_KP_MINUS: scale = 1.0f / (fine ? 1.01f : 1.1f); break;
    case SDLK_EQUALS: case SDLK_KP_PLUS: scale = fine ? 1.01f : 1.1f; break;
    default: return false;
    }
    if (r.mode == ReferenceMode::Behind) {
        if (c.w > 0.0f && c.h > 0.0f) r.center += move / glm::vec2(c.w, c.h);
        r.height = std::clamp(r.height * scale, 0.01f, 20.0f);
    } else {
        glm::vec3 u, v, n;
        r.axes(u, v, n);
        // A pixel's worth of world at the plane, measured along the view's own axes so the arrows move it as seen.
        const float wpp = camera_.worldPerPixel(r.origin, c.h);
        const glm::mat3 toWorld = glm::transpose(glm::mat3(camera_.view()));
        glm::vec3 d = toWorld * glm::vec3(move.x, -move.y, 0.0f) * wpp;
        d -= n * glm::dot(d, n); // (kept in its plane)
        r.origin += d;
        r.sceneHeight = std::clamp(r.sceneHeight * scale, 1e-6f, 1e6f);
    }
    doc_.modified = true;
    return true;
}

void App::drawReferenceOverlay() {
    // One just added (a dialog, a drop, a script) is the one the section sets up; one removed takes its selection.
    if (doc_.references.size() > refUi_.count || refUi_.selected >= int(doc_.references.size()))
        refUi_.selected = int(doc_.references.size()) - 1;
    refUi_.count = doc_.references.size();
    if (!refUi_.adjust) return;
    const ReferenceImage* sel = refUi_.selected >= 0 ? &doc_.references[size_t(refUi_.selected)] : nullptr;
    if (!sel || !sel->shown()) {
        refUi_.adjust = false;
        return;
    }
    const CanvasRect cr = canvasRect();
    ImDrawList* dl = ImGui::GetBackgroundDrawList(); // under the panels, over the model
    dl->PushClipRect(ImVec2(cr.x, cr.y), ImVec2(cr.x + cr.w, cr.y + cr.h), true);
    const float s = uiScale_, h = kCornerPx * s;
    for (size_t i = 0; i < doc_.references.size(); ++i) {
        const ReferenceImage& r = doc_.references[i];
        std::array<glm::vec2, 4> c;
        if (!referenceCorners(r, c)) continue;
        const bool on = int(i) == refUi_.selected;
        const ImU32 edge = on && !r.locked ? theme::u32(theme::accent) : IM_COL32(255, 255, 255, r.locked ? 60 : 110);
        for (size_t k = 0; k < 4; ++k) {
            const ImVec2 a(c[k].x, c[k].y), b(c[(k + 1) % 4].x, c[(k + 1) % 4].y);
            dl->AddLine(a, b, IM_COL32(0, 0, 0, 120), 3.0f * s);
            dl->AddLine(a, b, edge, 1.25f * s);
        }
        if (!on || r.locked) continue;
        int hot = refUi_.handle;
        if (hot < 0) hot = referenceHandleAt(c, {mouseX_, mouseY_}, (kCornerPx + 6.0f) * s);
        for (size_t k = 0; k < 4; ++k) {
            const glm::vec2& p = c[k];
            dl->AddRectFilled(ImVec2(p.x - h - s, p.y - h - s), ImVec2(p.x + h + s, p.y + h + s), IM_COL32(0, 0, 0, 160));
            dl->AddRectFilled(ImVec2(p.x - h, p.y - h), ImVec2(p.x + h, p.y + h),
                              hot == int(k) + 1 ? theme::u32(theme::accent) : IM_COL32(240, 240, 240, 255));
        }
        const glm::vec2 m = 0.25f * (c[0] + c[1] + c[2] + c[3]);
        const ImU32 mid = hot == 0 ? theme::u32(theme::accent) : IM_COL32(240, 240, 240, 220);
        dl->AddLine(ImVec2(m.x - 2 * h, m.y), ImVec2(m.x + 2 * h, m.y), mid, 1.5f * s);
        dl->AddLine(ImVec2(m.x, m.y - 2 * h), ImVec2(m.x, m.y + 2 * h), mid, 1.5f * s);
    }
    dl->PopClipRect();
}

// View palette: the Reference images section.
void App::drawReferencesSection() {
    refUi_.sectionShownAt = now();
    tray::GroupLabel(*this, "Reference images");
    const float w2 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ui::ActionButton(*this, "reference.add", ImVec2(w2, 0), ui::Look::IconLabel);
    ImGui::SameLine();
    ui::ActionButton(*this, "reference.pair", ImVec2(-1, 0), ui::Look::IconLabel);
    if (doc_.references.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tr("Pictures to sculpt against: behind the model, or planes at its front, side or top. "
                                     "Drop images on the window while this shows."));
        ImGui::PopTextWrapPos();
        return;
    }
    if (refUi_.selected < 0 || refUi_.selected >= int(doc_.references.size())) refUi_.selected = int(doc_.references.size()) - 1;
    const float g = ImGui::GetFrameHeight();
    int remove = -1;
    for (size_t i = 0; i < doc_.references.size(); ++i) {
        const ReferenceImage& r = doc_.references[i];
        ImGui::PushID(int(i));
        const float nameW = ImGui::GetContentRegionAvail().x - 3.0f * (g + 2.0f);
        const std::string file = r.path.empty() ? r.name : fileName(r.path);
        const std::string label = r.invalid  ? r.name + "  " + std::string(tr("(invalid image)"))
                                  : !r.image ? r.name + "  " + std::string(tr("(missing)"))
                                  : r.embed  ? r.name
                                             : std::string(tr("linked:")) + " " + file;
        if (ImGui::Selectable(label.c_str(), int(i) == refUi_.selected, 0, ImVec2(nameW, g))) refUi_.selected = int(i);
        ui::HelpText("Reference image", nullptr, r.path.empty() ? "Select it to set it up below." : r.path.c_str(), kWhere);
        ImGui::SameLine(0.0f, 2.0f);
        if (ui::GlyphButton("##ref.visible", r.visible ? ICON_LC_EYE : ICON_LC_EYE_OFF, ImVec2(g, g)))
            runCommand("reference_set", {{"index", double(i)}, {"visible", !r.visible}});
        ui::HelpText("Show", nullptr, "Show or hide this reference image.", kWhere);
        ImGui::SameLine(0.0f, 2.0f);
        if (ui::GlyphButton("##ref.lock", r.locked ? ICON_LC_LOCK : ICON_LC_LOCK_OPEN, ImVec2(g, g)))
            runCommand("reference_set", {{"index", double(i)}, {"locked", !r.locked}});
        ui::HelpText("Lock", nullptr, "Locked, Adjust leaves it where it is.", kWhere);
        ImGui::SameLine(0.0f, 2.0f);
        if (ui::GlyphButton("##ref.remove", ICON_LC_X, ImVec2(g, g))) remove = int(i);
        ui::HelpText("Remove", nullptr, "Take this reference image off the view.", kWhere);
        ImGui::PopID();
    }
    if (remove >= 0) {
        runCommand("reference_remove", {{"index", double(remove)}});
        if (refUi_.selected >= int(doc_.references.size())) refUi_.selected = int(doc_.references.size()) - 1;
        if (doc_.references.empty()) refUi_.adjust = false;
        return;
    }
    const int i = refUi_.selected;
    ReferenceImage& r = doc_.references[size_t(i)];
    auto set = [&](const char* key, ArgValue v) { runCommand("reference_set", {{"index", double(i)}, {key, std::move(v)}}); };
    if (!r.image) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s %s", tr("Not found:"), r.path.c_str());
        ImGui::PopTextWrapPos();
    }
    ui::Segmented(*this, {"reference.mode_behind", "reference.mode_scene"});
    if (r.mode == ReferenceMode::Scene) ui::Segmented(*this, {"reference.plane_front", "reference.plane_side", "reference.plane_top"});
    ImGui::SetNextItemWidth(-1);
    float opacity = r.opacity;
    if (ui::SliderInside("##ref.opacity", &opacity, 0.0f, 1.0f, tr("Opacity %.2f"))) set("opacity", double(opacity));
    ui::HelpText("Opacity", nullptr, "How strongly the reference image shows over what is behind it.", kWhere);
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const float column2 = ImGui::GetCursorPosX() + half + ImGui::GetStyle().ItemSpacing.x;
    auto check = [&](const char* label, const char* key, bool value, const char* title, const char* help) {
        bool v = value;
        if (ui::Checkbox(tr(label), &v)) set(key, v);
        ui::HelpText(title, nullptr, help, kWhere);
    };
    check("Flip across##ref.flip_x", "flip_x", r.flipX, "Flip across", "Mirror the image left to right.");
    ImGui::SameLine(column2);
    check("Flip up/down##ref.flip_y", "flip_y", r.flipY, "Flip up/down", "Mirror the image top to bottom.");
    check("Show in renders##ref.in_renders", "in_renders", r.inRenders, "Show in renders",
          "Off, the reference shows only in this view: renders, exports, project thumbnails and the timelapse leave it out.");
    ImGui::SameLine(column2);
    check("Embed##ref.embed", "embed", r.embed, "Embed in the project",
          "Save the image inside the project, so it opens anywhere. Off, the project keeps only the file's path (a "
          "smaller project; the image must stay where it is).");
    bool adjust = refUi_.adjust;
    ImGui::BeginDisabled(!r.shown() || r.locked);
    if (ui::Checkbox(tr("Adjust##ref.adjust"), &adjust)) refUi_.adjust = adjust;
    ImGui::EndDisabled();
    ui::HelpAction(*this, "reference.adjust");
    ImGui::SameLine(column2);
    if (ImGui::Button(tr("Reset placement##ref.reset"), ImVec2(-1, 0))) set("reset", true);
    ui::HelpText("Reset placement", nullptr,
                 "Behind the model: centred, 80% of the view's height. In the scene: just behind the model, as tall as "
                 "it. Both back to the image's own proportions.",
                 kWhere);
    const double mb = double(referenceGpuBytes(doc_.references)) / double(1 << 20);
    ImGui::TextDisabled(tr("GPU memory %.1f MB"), mb);
    ui::HelpText("GPU memory", nullptr,
                 "What the reference images' textures take on the graphics card. Images over 4096 pixels are scaled "
                 "down when they load.",
                 kWhere);
}

bool App::referenceUiValue(const std::string& name, double& out) {
    const ReferenceImage* r = refUi_.selected >= 0 && refUi_.selected < int(doc_.references.size())
                                  ? &doc_.references[size_t(refUi_.selected)]
                                  : nullptr;
    auto x1000 = [](float v) { return std::round(double(v) * 1000.0); };
    if (name == "reference_count") out = double(doc_.references.size());
    else if (name == "reference_selected") out = refUi_.selected;
    else if (name == "reference_adjust") out = refUi_.adjust ? 1.0 : 0.0;
    else if (name == "reference_gpu_kb") out = std::round(double(referenceGpuBytes(doc_.references)) / 1024.0);
    else if (name == "reference_render_r") { // a 64 px render's mean red (what renders and exports show)
        std::vector<uint8_t> rgba;
        std::string err;
        renderer_.setReferences(doc_.references); // (as this frame's draw will have them)
        out = -1.0;
        if (renderer_.renderToImage(camera_, 64, 64, renderSettings_, rgba, err)) {
            double sum = 0.0;
            for (size_t i = 0; i < rgba.size(); i += 4) sum += rgba[i];
            out = std::round(sum / double(rgba.size() / 4));
        }
    }
    else if (!r) return name.rfind("reference_", 0) == 0 ? (out = -1.0, true) : false;
    else if (name == "reference_x") out = x1000(r->center.x); // x1000
    else if (name == "reference_y") out = x1000(r->center.y);
    else if (name == "reference_height") out = x1000(r->height);
    else if (name == "reference_scene_height") out = x1000(r->sceneHeight);
    else if (name == "reference_stretch") out = x1000(r->stretch);
    else if (name == "reference_origin_x") out = x1000(r->origin.x);
    else if (name == "reference_origin_y") out = x1000(r->origin.y);
    else if (name == "reference_origin_z") out = x1000(r->origin.z);
    else if (name == "reference_scene") out = r->mode == ReferenceMode::Scene ? 1.0 : 0.0;
    else if (name == "reference_has_image") out = r->image ? 1.0 : 0.0;
    else if (name == "reference_facing") out = x1000(r->facing(camera_.view(), camera_.ortho));
    else return false;
    return true;
}

} // namespace sf
