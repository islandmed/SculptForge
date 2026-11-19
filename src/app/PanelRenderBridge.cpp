// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// File > Render with...: a still of the scene from a Hydra render delegate the artist already has, through the render
// helper they chose (OpenUSD's usdrecord; ai/RenderBridge.h, docs/design/render-bridge.md). The Render window picks the
// renderer (the helper's own list), the size and the format, runs the helper as its own program (the view stays
// responsive; Stop ends it), shows the still, keeps it in the Renders folder and opens it in the image editor chosen
// in Preferences > Render bridge. Nothing runs until a helper is set up: a found usdrecord is only offered, and a
// helper whose file changed since it was confirmed asks again. A studio policy can turn it all off.
#include "app/App.h"
#include "app/NativeDialog.h"
#include "app/OpenUrl.h"
#include "app/Ui.h"

#include "ai/RenderBridge.h"
#include "core/ManagedPolicy.h"
#include "core/Parts.h"
#include "render/GL.h"

#include <IconsLucide.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <future>
#include <limits>
#include <mutex>

namespace sf {

namespace {

namespace rb = renderbridge;
namespace fs = std::filesystem;
constexpr const char* kPath = "File > Render with";

fs::path fsPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// Native dialog answers (any thread).
enum class Pick { Helper, Editor, Folder, SaveAs };
std::mutex g_pickMutex;
std::vector<std::pair<Pick, std::string>> g_picked;
void SDLCALL onPicked(void* userdata, const char* const* files, int) {
    wakeMainLoop();
    if (!files || !files[0]) return;
    std::lock_guard lock(g_pickMutex);
    g_picked.emplace_back(Pick(reinterpret_cast<intptr_t>(userdata)), files[0]);
}
void* pickTag(Pick p) { return reinterpret_cast<void*>(intptr_t(p)); }

// The environment the user started us with (an AppImage's own entries removed), for the programs we start.
std::vector<std::string> userEnvironment() {
    std::vector<std::string> env = opener::cleanEnvironment(opener::startupEnvironment());
    if (env.empty()) env = ai::currentEnvironment(); // (Windows, macOS: no AppImage, nothing captured)
    return env;
}

} // namespace

struct RenderBridgeUi {
    rb::Settings settings;
    std::string settingsPath; // "" = not saved (UI tests)
    bool open = false;        // the Render window
    // The helper's abilities, read (--help) on a worker after it was confirmed.
    rb::HelperInfo info;
    bool probed = false;
    std::future<rb::HelperInfo> probing;
    // A helper waiting for the user's yes: found on PATH, chosen, or changed since it was confirmed.
    std::string confirmPath;
    bool confirmChanged = false;
    bool renderAfterConfirm = false;
    bool renderAfterProbe = false;
    rb::Job job;
    std::string message;      // the last outcome, shown in the window
    std::string kept;         // the finished still in the Renders folder
    std::string shown;        // the image the texture shows
    unsigned texture = 0;
    int texW = 0, texH = 0;

    void save() {
        if (!settingsPath.empty()) settings.save(settingsPath);
    }
    std::string rendersDir() const { return settings.rendersDir.empty() ? rb::defaultRendersDir() : settings.rendersDir; }
};

void App::registerRenderBridgeUi() {
    renderBridge_ = std::make_shared<RenderBridgeUi>();
    RenderBridgeUi& r = *renderBridge_;
    if (!uiConfigDeterministic_) { // UI tests never read or write the artist's settings
        r.settingsPath = rb::defaultSettingsPath();
        r.settings = rb::Settings::load(r.settingsPath);
    } else {
        r.settings.rendersDir = "sf_ui_renders"; // (the working directory: never the artist's Pictures folder)
    }
    using T = ParamSpec::Type;
    commands_.add({"render_bridge",
                   "Render a still of the visible Parts from the current view with a Hydra render delegate, through "
                   "the render helper set up in Preferences > Render bridge (OpenUSD's usdrecord). Runs in the "
                   "background; the still shows in the Render window, is kept in the Renders folder and opens in the "
                   "image editor when that is on. A studio policy can turn this off.",
                   {{"renderer", T::String, "A renderer the helper lists. Default: the one chosen in the Render window.", false},
                    {"width", T::Number, "Width in pixels (16-16384).", false},
                    {"height", T::Number, "Height in pixels (16-16384).", false},
                    {"format", T::String, "png or exr.", false},
                    {"light", T::Bool, "The studio light (key, fill, rim, dome).", false}},
                   [this](Document&, const Args& a) -> CommandResult {
                       RenderBridgeUi& ui = *renderBridge_;
                       if (a.count("renderer")) ui.settings.renderer = argString(a, "renderer");
                       if (a.count("width")) ui.settings.width = std::clamp(int(argNumber(a, "width", 1920)), 16, 16384);
                       if (a.count("height")) ui.settings.height = std::clamp(int(argNumber(a, "height", 1080)), 16, 16384);
                       if (a.count("width") || a.count("height")) ui.settings.matchView = false;
                       if (a.count("format")) {
                           const std::string f = argString(a, "format");
                           if (f != "png" && f != "exr") return {false, "format must be png or exr"};
                           ui.settings.format = f;
                       }
                       if (a.count("light")) ui.settings.studioLight = argBool(a, "light", true);
                       return startRenderBridge();
                   }});
    commands_.add({"render_bridge_cancel", "Stop the render that is running (Render with...).", {},
                   [this](Document&, const Args&) -> CommandResult {
                       RenderBridgeUi& ui = *renderBridge_;
                       if (ui.job.state() != rb::State::Running) return {true, "No render is running"};
                       ui.job.cancel();
                       ui.message = "Render stopped";
                       return {true, "Render stopped"};
                   }});

    auto add = [this](const char* id, const char* label, const char* icon, std::function<void()> run,
                      std::function<bool()> enabled, const char* description, const char* command) {
        Action a;
        a.id = id;
        a.label = label;
        a.icon = icon;
        a.palette = kPath;
        a.run = std::move(run);
        a.enabled = std::move(enabled);
        a.description = description;
        a.command = command;
        actions_.add(std::move(a));
    };
    auto allowed = [] { return policy::renderBridgeAllowed(); };
    add("file.render_with", "Render with... (preview)", ICON_LC_APERTURE,
        [this] {
            if (policy::renderBridgeAllowed()) renderBridge_->open = true;
        },
        allowed,
        "Render a still from this view with a renderer you have installed (any Hydra render delegate your USD tools "
        "find), then see it here, keep it and open it in your image editor.",
        "render_bridge");
    add("render.start", "Render", ICON_LC_APERTURE, [this] { runCommand("render_bridge"); },
        [this] { return policy::renderBridgeAllowed() && renderBridge_->job.state() != rb::State::Running; },
        "Render the visible Parts from the current view with the chosen renderer.", "render_bridge");
    add("render.stop", "Stop", ICON_LC_X, [this] { runCommand("render_bridge_cancel"); },
        [this] { return renderBridge_->job.state() == rb::State::Running; }, "Stop the render.", "render_bridge_cancel");
    add("render.open_editor", "Open in Image Editor", ICON_LC_IMAGE, [this] { openRenderInEditor(); },
        [this] { return !renderBridge_->kept.empty(); },
        "Open the still in the image editor chosen in Preferences > Render bridge (else your system's default).", "");
    add("render.show_folder", "Show Folder", ICON_LC_FOLDER_OPEN,
        [this] {
            const std::string dir = renderBridge_->rendersDir();
            std::string url = dir.rfind('/', 0) == 0 ? "file://" : "file:///";
            url += dir;
            if (!dialogs::openUrl(url.c_str())) status_ = "Renders are kept in " + dir;
        },
        [this] { return !renderBridge_->kept.empty(); }, "Open the folder the stills are kept in.", "");
    add("render.save_as", "Save As...", ICON_LC_SAVE,
        [this] {
            const bool exr = fsPath(renderBridge_->kept).extension() == ".exr";
            static const SDL_DialogFileFilter png[] = {{"PNG image", "png"}}, exr_[] = {{"OpenEXR image", "exr"}};
            dialogs::saveFile(onPicked, pickTag(Pick::SaveAs), window_, exr ? exr_ : png, 1, nullptr);
        },
        [this] { return !renderBridge_->kept.empty(); }, "Save a copy of the still somewhere else.", "");
    add("render.choose_helper", "Choose Helper...", nullptr,
        [this] { dialogs::openFile(onPicked, pickTag(Pick::Helper), window_, nullptr, 0, nullptr, false); }, allowed,
        "Choose the program that renders: OpenUSD's usdrecord (from your USD install, a USD Python bundle or your "
        "studio's tools). It runs as its own program with the renderers your USD install finds.",
        "");
    add("render.use_found", "Use Found", nullptr,
        [this] {
            RenderBridgeUi& ui = *renderBridge_;
            ui.confirmPath = rb::foundHelper();
            ui.confirmChanged = false;
        },
        [] { return policy::renderBridgeAllowed() && !rb::foundHelper().empty(); },
        "Use the usdrecord found on your PATH (you confirm it before it runs).", "");
    add("render.choose_editor", "Choose Editor...", nullptr,
        [this] { dialogs::openFile(onPicked, pickTag(Pick::Editor), window_, nullptr, 0, nullptr, false); }, nullptr,
        "Choose the image editor finished stills open in: any program.", "");
    add("render.clear_editor", "System Default", nullptr,
        [this] {
            renderBridge_->settings.editor.clear();
            renderBridge_->save();
        },
        [this] { return !renderBridge_->settings.editor.empty(); },
        "Open stills with your system's default program for images.", "");
    add("render.choose_folder", "Choose Folder...", nullptr,
        [this] { dialogs::openFolder(onPicked, pickTag(Pick::Folder), window_, nullptr, false); }, nullptr,
        "Choose where finished stills are kept.", "");
}

// Starts a render with the window's settings from the current view, or asks to confirm the helper first.
CommandResult App::startRenderBridge() {
    RenderBridgeUi& ui = *renderBridge_;
    std::string why;
    if (!policy::renderBridgeAllowed(&why)) return {false, why};
    if (ui.job.state() == rb::State::Running) return {false, "A render is already running - Stop it first"};
    ui.open = true;
    if (ui.settings.helper.empty()) {
        ui.message = "Set up a render helper first (Preferences > Render bridge)";
        return {false, ui.message};
    }
    if (!ui.settings.helperTrusted()) { // changed (or never confirmed): the user says yes again before it runs
        ui.confirmPath = ui.settings.helper;
        ui.confirmChanged = true;
        ui.renderAfterConfirm = true;
        return {true, "The render helper changed since you confirmed it - confirm it to render"};
    }
    if (!ui.probed) { // (a first render: read what the helper can do on a worker, then render)
        if (!ui.probing.valid())
            ui.probing = std::async(std::launch::async, [path = ui.settings.helper, env = userEnvironment()] {
                return rb::probe(path, env);
            });
        ui.renderAfterProbe = true;
        return {true, "Reading what the render helper can do, then rendering"};
    }
    if (!ui.info.ok) {
        ui.message = ui.info.error;
        return {false, ui.message};
    }
    rb::Request req;
    req.renderer = ui.settings.renderer;
    if (!req.renderer.empty() && std::find(ui.info.renderers.begin(), ui.info.renderers.end(), req.renderer) == ui.info.renderers.end())
        req.renderer.clear(); // (the helper no longer lists it: its default)
    const CanvasRect c = canvasRect();
    req.width = ui.settings.width;
    req.height = ui.settings.matchView && c.w > 0 && c.h > 0 ? std::clamp(int(std::lround(req.width * c.h / c.w)), 16, 16384)
                                                             : ui.settings.height;
    req.format = ui.settings.format;
    req.studioLight = ui.settings.studioLight;
    req.view.cameraToWorld = glm::inverse(camera_.view());
    req.view.fovY = camera_.fovY;
    req.view.ortho = camera_.ortho;
    req.view.orthoHalfHeight = camera_.distance * std::tan(camera_.fovY * 0.5f);
    req.view.shift = camera_.shift;
    req.view.nearClip = camera_.distance * 0.01f;
    req.view.farClip = camera_.distance * 100.0f;
    std::string err;
    if (!ui.job.start(ui.settings.helper, ui.info, req, visiblePartObjects(doc_, true), doc_.assetName(), userEnvironment(), err)) {
        ui.message = err;
        return {false, err};
    }
    ui.message.clear();
    return {true, "Rendering " + std::to_string(req.width) + " x " + std::to_string(req.height) +
                      (req.renderer.empty() ? "" : " with " + req.renderer)};
}

void App::openRenderInEditor() {
    RenderBridgeUi& ui = *renderBridge_;
    if (ui.kept.empty()) return;
    if (ui.settings.editor.empty()) {
        std::string url = ui.kept.rfind('/', 0) == 0 ? "file://" : "file:///";
        url += ui.kept;
        if (!dialogs::openUrl(url.c_str())) status_ = "Saved " + ui.kept;
        return;
    }
    std::string program = ui.settings.editor;
    std::vector<std::string> args = {ui.kept};
#ifdef __APPLE__
    if (fsPath(program).extension() == ".app") { // an application bundle: Launch Services opens the file with it
        args = {"-a", program, ui.kept};
        program = "/usr/bin/open";
    }
#endif
    ai::ChildProcess editor;
    editor.setEnvironment(rb::cleanEnvironment(userEnvironment()));
    std::string err;
    if (!editor.start(program, args, "", {}, err)) {
        status_ = "Couldn't open the image editor: " + err;
        return;
    }
    editor.detach(); // (it runs on after us)
    status_ = "Opened in " + utf8(fsPath(ui.settings.editor).filename());
}

void App::pollRenderBridge() {
    if (!renderBridge_) return;
    RenderBridgeUi& ui = *renderBridge_;
    std::vector<std::pair<Pick, std::string>> picked;
    {
        std::lock_guard lock(g_pickMutex);
        picked.swap(g_picked);
    }
    for (auto& [kind, path] : picked) {
        switch (kind) {
        case Pick::Helper: { // chosen in a file dialog: the choice is the confirmation, its fingerprint is kept
            std::error_code ec;
            const std::string abs = utf8(fs::absolute(fsPath(path), ec));
            ui.settings.helper = abs;
            ui.settings.helperSha256 = rb::fileFingerprint(abs);
            ui.save();
            ui.probed = false;
            ui.message.clear(); // ("Set up a render helper first" is answered; the probe says if it can't run)
            ui.probing = std::async(std::launch::async, [abs, env = userEnvironment()] { return rb::probe(abs, env); });
            status_ = "Render helper: " + abs;
            break;
        }
        case Pick::Editor:
            ui.settings.editor = path;
            ui.save();
            status_ = "Image editor: " + path;
            break;
        case Pick::Folder:
            ui.settings.rendersDir = path;
            ui.save();
            status_ = "Renders are kept in " + path;
            break;
        case Pick::SaveAs: {
            std::error_code ec;
            fs::copy_file(fsPath(ui.kept), fsPath(path), fs::copy_options::overwrite_existing, ec);
            status_ = ec ? "Couldn't save " + path + ": " + ec.message() : "Saved " + path;
            break;
        }
        }
    }
    if (ui.probing.valid() && ui.probing.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        ui.info = ui.probing.get();
        ui.probed = true;
        if (!ui.info.ok) ui.message = ui.info.error;
        else if (!ui.settings.renderer.empty() &&
                 std::find(ui.info.renderers.begin(), ui.info.renderers.end(), ui.settings.renderer) == ui.info.renderers.end())
            ui.settings.renderer.clear();
        if (ui.renderAfterProbe) {
            ui.renderAfterProbe = false;
            if (ui.info.ok) runCommand("render_bridge");
        }
    }
    if (ui.job.state() != rb::State::Running) return;
    const rb::State st = ui.job.poll();
    if (st == rb::State::Running) return;
    if (st == rb::State::Failed) {
        ui.message = ui.job.error();
        status_ = "Render failed (see the Render window)";
        return;
    }
    if (st != rb::State::Done) return;
    Image image;
    std::string err;
    if (!rb::loadImage(ui.job.image(), image, err)) {
        ui.message = err;
        status_ = "Render failed: " + err;
        return;
    }
    ui.kept = rb::keepRender(ui.job.image(), ui.rendersDir(), doc_.assetName(), err);
    if (ui.kept.empty()) ui.kept = ui.job.image(); // (still shown and openable from the job folder)
    const Image shown = image.downscaled(2048); // (fits any GPU's texture limit; the file keeps every pixel)
    if (!ui.texture) gl::GenTextures(1, &ui.texture);
    gl::BindTexture(GL_TEXTURE_2D, ui.texture);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), shown.width, shown.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   shown.rgba.data());
    gl::BindTexture(GL_TEXTURE_2D, 0);
    ui.texW = image.width;
    ui.texH = image.height;
    ui.shown = ui.kept;
    char secs[32];
    std::snprintf(secs, sizeof secs, "%.1f", ui.job.elapsedSeconds());
    ui.message = "Saved " + ui.kept;
    status_ = "Rendered in " + std::string(secs) + " s: " + ui.kept;
    if (ui.settings.openInEditor) openRenderInEditor();
}

void App::shutdownRenderBridge() {
    if (renderBridge_ && renderBridge_->texture) gl::DeleteTextures(1, &renderBridge_->texture);
    if (renderBridge_) renderBridge_->texture = 0;
}

bool App::renderBridgeBusy() const { return renderBridge_ && renderBridge_->job.state() == rb::State::Running; }

double App::renderBridgeTestValue(const std::string& name) {
    const double none = std::numeric_limits<double>::quiet_NaN();
    if (!renderBridge_) return none;
    RenderBridgeUi& ui = *renderBridge_;
    if (name == "render_bridge_state") return double(int(ui.job.state())); // 0 idle, 1 running, 2 done, 3 failed, 4 stopped
    if (name == "render_bridge_window") return ui.open ? 1.0 : 0.0;
    if (name == "render_bridge_confirm") return ui.confirmPath.empty() ? 0.0 : 1.0;
    if (name == "render_bridge_ready") return ui.settings.helperTrusted() ? 1.0 : 0.0;
    if (name == "render_bridge_renderers") return double(ui.info.renderers.size());
    if (name == "render_bridge_shown") return ui.texture && !ui.shown.empty() ? double(ui.texW) : 0.0;
    if (name == "render_bridge_command") return commands_.find("render_bridge") ? 1.0 : 0.0;
    return none;
}

// Preferences > Render bridge: the helper, the image editor, where stills are kept.
void App::drawRenderBridgePrefs() {
    if (!renderBridge_) return;
    RenderBridgeUi& ui = *renderBridge_;
    std::string why;
    if (!policy::renderBridgeAllowed(&why)) {
        ImGui::TextDisabled("%s", tr("Turned off by your studio's policy"));
        return;
    }
    const float avail = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
    auto pathRow = [](const char* label, const std::string& path, const char* none) {
        ImGui::TextUnformatted(tr(label));
        ImGui::SameLine();
        if (path.empty()) ImGui::TextDisabled("%s", tr(none));
        else ImGui::TextDisabled("%s", utf8(fsPath(path).filename()).c_str());
        if (!path.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
    };
    pathRow("Render helper", ui.settings.helper, "not set up");
    if (!ui.settings.helper.empty() && !ui.settings.helperTrusted())
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "%s", tr("Changed since you confirmed it"));
    ui::ActionButton(*this, "render.choose_helper", ImVec2((avail - gap) * 0.5f, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "render.use_found", ImVec2(-1, 0));
    pathRow("Image editor", ui.settings.editor, "system default");
    ui::ActionButton(*this, "render.choose_editor", ImVec2((avail - gap) * 0.5f, 0));
    ImGui::SameLine();
    ui::ActionButton(*this, "render.clear_editor", ImVec2(-1, 0));
    pathRow("Renders folder", ui.rendersDir(), "");
    ui::ActionButton(*this, "render.choose_folder", ImVec2(-1, 0));
}

void App::drawRenderBridgeOverlay() {
    if (!renderBridge_) return;
    RenderBridgeUi& ui = *renderBridge_;
    const float s = uiScale_;
    const CanvasRect c = canvasRect();
    if (!policy::renderBridgeAllowed()) {
        ui.open = false;
        ui.confirmPath.clear();
    }
    // The helper prompt: nothing runs until the user says yes to this exact file.
    if (!ui.confirmPath.empty()) {
        ImGui::SetNextWindowPos(ImVec2(c.x + c.w * 0.5f, c.y + c.h * 0.4f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(460 * s, 0), ImGuiCond_Always);
        if (ImGui::Begin("##render_helper_confirm", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
            ImGui::TextUnformatted(tr(ui.confirmChanged ? "The render helper changed" : "Use this render helper?"));
            ImGui::Spacing();
            ImGui::TextWrapped("%s", ui.confirmPath.c_str());
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", tr(ui.confirmChanged
                                            ? "This file is different from the one you confirmed (an update, or something "
                                              "replaced it). SculptForge runs it only if you say so."
                                            : "SculptForge runs this program as a separate process to render, with the "
                                              "renderers your USD install finds. Only use one you trust."));
            ImGui::PopStyleColor();
            ImGui::Spacing();
            const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (ImGui::Button(tr("Use This Helper##render"), ImVec2(half, 0))) {
                const std::string path = ui.confirmPath;
                ui.settings.helper = path;
                ui.settings.helperSha256 = rb::fileFingerprint(path);
                ui.save();
                ui.confirmPath.clear();
                ui.probed = false;
                ui.message.clear();
                if (ui.renderAfterConfirm) {
                    ui.renderAfterConfirm = false;
                    runCommand("render_bridge");
                } else {
                    ui.probing = std::async(std::launch::async, [path, env = userEnvironment()] { return rb::probe(path, env); });
                }
                status_ = "Render helper: " + path;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel##render_confirm"), ImVec2(-1, 0))) {
                ui.confirmPath.clear();
                ui.renderAfterConfirm = false;
            }
        }
        ImGui::End();
    }
    if (!ui.open) return;
    ImGui::SetNextWindowSize(ImVec2(420.0f * s, 0.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(c.x + c.w - 440.0f * s, c.y + 50.0f * s), ImGuiCond_Appearing);
    if (!ImGui::Begin(tr("Render##render_bridge"), &ui.open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    const bool running = ui.job.state() == rb::State::Running;
    const float avail = ImGui::GetContentRegionAvail().x, gap = ImGui::GetStyle().ItemSpacing.x;
    if (ui.settings.helper.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail);
        ImGui::TextUnformatted(tr("Renders go through a render helper you already have: OpenUSD's usdrecord, with any "
                                  "Hydra renderer your USD install finds. Choose it once:"));
        ImGui::PopTextWrapPos();
        ui::ActionButton(*this, "render.choose_helper", ImVec2((avail - gap) * 0.5f, 0));
        ImGui::SameLine();
        ui::ActionButton(*this, "render.use_found", ImVec2(-1, 0));
    } else {
        ImGui::BeginDisabled(running);
        // Renderer: what the helper lists ("" = its default).
        if (!ui.probed && !ui.probing.valid() && ui.settings.helperTrusted())
            ui.probing = std::async(std::launch::async, [path = ui.settings.helper, env = userEnvironment()] {
                return rb::probe(path, env);
            });
        const std::string current = ui.settings.renderer.empty() ? std::string(tr("Default")) : ui.settings.renderer;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##renderer", current.c_str())) {
            if (ImGui::Selectable(tr("Default##renderer"), ui.settings.renderer.empty())) {
                ui.settings.renderer.clear();
                ui.save();
            }
            for (const std::string& n : ui.info.renderers)
                if (ImGui::Selectable(n.c_str(), n == ui.settings.renderer)) {
                    ui.settings.renderer = n;
                    ui.save();
                }
            ImGui::EndCombo();
        }
        ui::HelpText("Renderer", nullptr,
                     "The Hydra renderers your render helper finds (its own plugin paths). Default is the helper's own "
                     "(the fast preview renderer that comes with OpenUSD).",
                     kPath);
        if (ui.probing.valid()) ImGui::TextDisabled("%s", tr("Looking for renderers..."));
        // Size: width, then the height (from the view's shape, or typed).
        ImGui::SetNextItemWidth((avail - gap) * 0.5f);
        if (ImGui::InputInt("##rwidth", &ui.settings.width, 0)) ui.settings.width = std::clamp(ui.settings.width, 16, 16384);
        if (ImGui::IsItemDeactivatedAfterEdit()) ui.save();
        ui::HelpText("Width", nullptr, "Width of the still in pixels.", kPath);
        ImGui::SameLine();
        ImGui::BeginDisabled(ui.settings.matchView);
        ImGui::SetNextItemWidth(-1);
        int height = ui.settings.height;
        const CanvasRect cv = canvasRect();
        if (ui.settings.matchView && cv.w > 0) height = std::clamp(int(std::lround(ui.settings.width * cv.h / cv.w)), 16, 16384);
        if (ImGui::InputInt("##rheight", &height, 0)) ui.settings.height = std::clamp(height, 16, 16384);
        if (ImGui::IsItemDeactivatedAfterEdit()) ui.save();
        ImGui::EndDisabled();
        ui::HelpText("Height", nullptr, "Height of the still in pixels (from the view's shape while Match view is on).", kPath);
        if (ImGui::Checkbox(tr("Match view##render"), &ui.settings.matchView)) ui.save();
        ui::HelpText("Match view", nullptr, "The still has the viewport's shape; off: the width and height you type.", kPath);
        ImGui::SameLine();
        if (ImGui::Checkbox(tr("Studio light##render"), &ui.settings.studioLight)) ui.save();
        ui::HelpText("Studio light", nullptr,
                     "Key, fill and rim lights that turn with the camera, and a soft white sky. Off: the renderer's own "
                     "camera light.",
                     kPath);
        bool exr = ui.settings.format == "exr";
        if (ImGui::RadioButton(tr("PNG##render"), !exr)) ui.settings.format = "png", ui.save();
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("EXR##render"), exr)) ui.settings.format = "exr", ui.save();
        ui::HelpText("Format", nullptr, "PNG for previews; EXR (linear, high dynamic range) for compositing.", kPath);
        ImGui::SameLine();
        if (ImGui::Checkbox(tr("Open in editor when done##render"), &ui.settings.openInEditor)) ui.save();
        ui::HelpText("Open in editor when done", nullptr,
                     "Open each finished still in your image editor (Preferences > Render bridge).", kPath);
        ImGui::EndDisabled();
        if (running) {
            char line[64];
            const int t = int(ui.job.elapsedSeconds());
            std::snprintf(line, sizeof line, tr("Rendering %d:%02d"), t / 60, t % 60);
            ImGui::TextUnformatted(line);
            ImGui::SameLine(avail * 0.6f);
            ui::ActionButton(*this, "render.stop", ImVec2(-1, 0), ui::Look::IconLabel);
        } else {
            ui::ActionButton(*this, "render.start", ImVec2(-1, 0), ui::Look::IconLabel);
        }
    }
    if (!ui.message.empty()) {
        const bool good = ui.job.state() == rb::State::Done || ui.job.state() == rb::State::Cancelled;
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail);
        if (good) ImGui::TextDisabled("%s", ui.message.c_str());
        else ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "%s", ui.message.c_str());
        ImGui::PopTextWrapPos();
    }
    if (ui.texture && ui.texW > 0 && ui.texH > 0) {
        const float w = avail, h = std::min(w * float(ui.texH) / float(ui.texW), 480.0f * s);
        const float iw = h * float(ui.texW) / float(ui.texH);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - iw) * 0.5f);
        ImGui::Image(ImTextureID(uintptr_t(ui.texture)), ImVec2(iw, h));
        ui::ActionButton(*this, "render.open_editor", ImVec2(-1, 0), ui::Look::IconLabel);
        ui::ActionButton(*this, "render.show_folder", ImVec2((avail - gap) * 0.5f, 0), ui::Look::IconLabel);
        ImGui::SameLine();
        ui::ActionButton(*this, "render.save_as", ImVec2(-1, 0), ui::Look::IconLabel);
    }
    ImGui::End();
}

} // namespace sf
