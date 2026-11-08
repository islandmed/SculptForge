// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// View > Community brushes: browse the shared brush gallery
// (github.com/islandmed/sculptforge-brushes), search it, and install brushes
// in one click. Everything that touches the network runs on worker threads;
// the index and previews are cached on disk so the window still works offline.
// Downloads are checked against the index's SHA-256 before brush_install.
#include "app/App.h"
#include "app/NativeDialog.h"

#include "ai/BrushGalleryClient.h"
#include "render/GL.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <map>

namespace sf {

struct BrushGalleryState {
    struct Preview {
        Image image;
        bool ok = false;
    };
    struct Loaded {
        ai::GalleryFetch fetch;
        GalleryIndex index;
        std::vector<std::string> warnings;
        std::string error;
    };
    struct Download {
        std::string path, error;
    };

    std::string location; // index URL / path in use
    std::future<Loaded> pendingIndex;
    GalleryIndex index;
    std::vector<std::string> tags;
    bool loaded = false, offline = false;
    std::string error, offlineReason;
    int skipped = 0; // malformed index entries

    std::map<std::string, std::future<Preview>> pendingPreviews; // by brush hash + file
    std::map<std::string, GLuint> textures;                       // by brush hash + file (0 = no preview)
    std::map<std::string, std::future<Download>> pendingInstalls; // by brush name
    std::string installError;

    char search[128] = "";
    std::string tag;
    char sourceEdit[512] = "";
    bool sourceEditInit = false;

    ~BrushGalleryState() {
        for (auto& [file, tex] : textures)
            if (tex) gl::DeleteTextures(1, &tex);
    }
};

namespace {

constexpr int kMaxPreviewJobs = 4;

bool sameName(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

template <class T> bool done(const std::future<T>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
template <class T> bool running(const std::future<T>& f) { return f.valid() && !done(f); }

GLuint uploadTexture(const Image& img) {
    GLuint tex = 0;
    gl::GenTextures(1, &tex);
    gl::BindTexture(GL_TEXTURE_2D, tex);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GLint(GL_LINEAR));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLint(GL_CLAMP_TO_EDGE));
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLint(GL_CLAMP_TO_EDGE));
    gl::BindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

void App::destroyBrushGallery(BrushGalleryState* state) { delete state; }

bool App::galleryBusy() const {
    if (!gallery_) return false;
    if (running(gallery_->pendingIndex)) return true;
    for (const auto& [k, f] : gallery_->pendingPreviews)
        if (running(f)) return true;
    for (const auto& [k, f] : gallery_->pendingInstalls)
        if (running(f)) return true;
    return false;
}

void App::refreshBrushGallery() {
    if (!gallery_) gallery_ = {new BrushGalleryState, &App::destroyBrushGallery};
    BrushGalleryState& g = *gallery_;
    if (running(g.pendingIndex)) return;
    const std::string location = ai::brushGalleryLocation();
    if (location != g.location) { // another gallery: forget what the old one showed
        for (auto& [key, tex] : g.textures)
            if (tex) gl::DeleteTextures(1, &tex);
        g.textures.clear();
        g.index = {};
        g.tags.clear();
        g.loaded = false;
    }
    g.location = location;
    g.error.clear();
    g.installError.clear();
    g.pendingIndex = std::async(std::launch::async, [location = g.location] {
        BrushGalleryState::Loaded r;
        r.fetch = ai::fetchGalleryIndex(location);
        if (!r.fetch.ok) r.error = r.fetch.error;
        else if (!parseGalleryIndex(r.fetch.data, r.index, r.error, &r.warnings)) r.fetch.ok = false;
        return r;
    });
}

void App::pollBrushGallery() {
    if (!gallery_) return;
    BrushGalleryState& g = *gallery_;
    if (done(g.pendingIndex)) {
        BrushGalleryState::Loaded r = g.pendingIndex.get();
        if (r.fetch.ok) {
            g.index = std::move(r.index);
            g.tags = galleryTags(g.index);
            g.loaded = true;
            g.offline = r.fetch.fromCache;
            g.offlineReason = r.fetch.error;
            g.skipped = int(r.warnings.size());
            for (const auto& w : r.warnings) SDL_Log("Brush gallery: %s", w.c_str());
            if (!g.tag.empty() && std::find(g.tags.begin(), g.tags.end(), g.tag) == g.tags.end()) g.tag.clear();
        } else {
            g.error = r.error.empty() ? "Unknown error" : r.error;
        }
    }
    for (auto it = g.pendingPreviews.begin(); it != g.pendingPreviews.end();) {
        if (!done(it->second)) {
            ++it;
            continue;
        }
        BrushGalleryState::Preview p = it->second.get();
        g.textures[it->first] = p.ok ? uploadTexture(p.image) : 0;
        it = g.pendingPreviews.erase(it);
    }
    for (auto it = g.pendingInstalls.begin(); it != g.pendingInstalls.end();) {
        if (!done(it->second)) {
            ++it;
            continue;
        }
        const BrushGalleryState::Download d = it->second.get();
        if (d.error.empty()) {
            // brush_install (the user's library) once it exists; brush_load keeps it for this session.
            if (commands_.find("brush_install"))
                runCommand("brush_install", {{"path", d.path}, {"source", std::string("gallery")}});
            else
                runCommand("brush_load", {{"path", d.path}});
            if (!lastCommandOk_) g.installError = status_;
        } else {
            g.installError = d.error;
            status_ = d.error;
        }
        it = g.pendingInstalls.erase(it);
    }
}

void App::drawBrushGallery() {
    pollBrushGallery();
    if (!showBrushGallery_) return;
    if (!gallery_ || (!gallery_->loaded && gallery_->error.empty() && !gallery_->pendingIndex.valid())) refreshBrushGallery();
    BrushGalleryState& g = *gallery_;

    const float scale = uiScale_;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(760 * scale, 640 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(360 * scale, 300 * scale), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(1.0f); // a browsing window: nothing behind it should show through
    if (!ImGui::Begin(tr("Community brushes"), &showBrushGallery_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    // Toolbar: search, tag filter, refresh, share.
    const float shareW = ImGui::CalcTextSize("Share yours").x + ImGui::GetStyle().FramePadding.x * 2;
    const float refreshW = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2;
    const float tagW = 150 * scale;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - tagW - refreshW - shareW - 3 * spacing));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InputTextWithHint("##gallerysearch", tr("Search brushes (scales, skin, stone...)"), g.search, sizeof g.search);
    if (g.search[0]) { // a clear button inside the field's right end
        const ImVec2 end = ImGui::GetItemRectMax(), start = ImGui::GetItemRectMin();
        const float h = end.y - start.y;
        ImGui::SetCursorScreenPos(ImVec2(end.x - h, start.y));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        if (ImGui::Button("x##clearsearch", ImVec2(h, h))) g.search[0] = '\0';
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Clear the search"));
    } // SameLine below continues after the field (the button ends where it does)
    ImGui::SameLine();
    ImGui::SetNextItemWidth(tagW);
    if (ImGui::BeginCombo("##gallerytag", tr(g.tag.empty() ? "All tags" : g.tag.c_str()))) {
        if (ImGui::Selectable(tr("All tags"), g.tag.empty())) g.tag.clear();
        for (const auto& t : g.tags)
            if (ImGui::Selectable(t.c_str(), g.tag == t)) g.tag = t;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(running(g.pendingIndex));
    if (ImGui::Button(tr("Refresh"))) refreshBrushGallery();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("Share yours"))) {
        if (interactive()) sf::dialogs::openUrl(kBrushGalleryShareUrl);
        status_ = std::string("Share a brush: ") + kBrushGalleryShareUrl;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Save your brush from Brush Lab, then open a pull request on the gallery.\nOpens %s"),
                          kBrushGalleryShareUrl);

    // State line: loading, offline, error.
    const ImVec4 warnColor(0.95f, 0.70f, 0.30f, 1.0f), errorColor(1.0f, 0.45f, 0.40f, 1.0f);
    if (running(g.pendingIndex) && !g.loaded) {
        hint(tr("Loading the brush gallery..."));
    } else if (!g.error.empty() && !g.loaded) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
        ImGui::TextWrapped(tr("Can't reach the brush gallery: %s"), g.error.c_str());
        ImGui::PopStyleColor();
        hint(tr(galleryLocalPath(g.location).empty()
                 ? "Check your internet connection and try again. Brushes you already installed keep working."
                 : "Check that the gallery folder exists and has an index.json. Brushes you already installed keep working."));
        if (ImGui::Button(tr("Try again"))) refreshBrushGallery();
    } else if (g.loaded) {
        if (g.offline) {
            ImGui::PushStyleColor(ImGuiCol_Text, warnColor);
            ImGui::TextWrapped(tr("Offline - showing the gallery from your last visit (%s)."), g.offlineReason.c_str());
            ImGui::PopStyleColor();
        } else if (!g.error.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
            ImGui::TextWrapped(tr("Refresh failed: %s"), g.error.c_str());
            ImGui::PopStyleColor();
        }
        if (!g.installError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
            ImGui::TextWrapped("%s", g.installError.c_str());
            ImGui::PopStyleColor();
        }
    }

    // The grid of brushes.
    std::vector<const GalleryBrush*> shown;
    for (const auto& b : g.index.brushes)
        if (galleryBrushMatches(b, g.search, g.tag)) shown.push_back(&b);
    if (g.loaded) {
        std::string count = std::to_string(shown.size()) + (shown.size() == 1 ? " brush" : " brushes");
        if (shown.size() != g.index.brushes.size()) count += " of " + std::to_string(g.index.brushes.size());
        if (g.skipped) count += "  (" + std::to_string(g.skipped) + " invalid entries skipped)";
        hint(count.c_str());
    }

    const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginChild("##gallerygrid", ImVec2(0, -footer), ImGuiChildFlags_None)) {
        if (g.loaded && shown.empty()) hint(tr(g.index.brushes.empty() ? "The gallery is empty." : "No brushes match your search."));
        const float avail = ImGui::GetContentRegionAvail().x;
        const float minCard = 190 * scale;
        const int columns = std::max(1, int((avail + spacing) / (minCard + spacing)));
        const float cardW = std::floor((avail - spacing * float(columns - 1)) / float(columns));
        const float imageH = std::floor((cardW - 2 * ImGui::GetStyle().WindowPadding.x) * 0.75f);
        const float cardH = imageH + ImGui::GetStyle().WindowPadding.y * 2 + ImGui::GetTextLineHeightWithSpacing() * 3 +
                            ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
        int column = 0;
        int jobs = 0;
        for (const auto& [k, f] : g.pendingPreviews) jobs += running(f) ? 1 : 0;
        for (const GalleryBrush* b : shown) {
            if (column > 0) ImGui::SameLine();
            ImGui::PushID(b->file.c_str());
            ImGui::BeginChild("##card", ImVec2(cardW, cardH), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
            const float innerW = ImGui::GetContentRegionAvail().x;

            // Preview: fetched lazily once the card is on screen, a few at a time.
            const bool cardVisible = ImGui::IsRectVisible(ImVec2(innerW, imageH));
            const std::string key = b->sha256 + b->file; // an edited brush gets its new preview
            auto tex = g.textures.find(key);
            if (tex == g.textures.end() && cardVisible && !g.pendingPreviews.count(key) && jobs < kMaxPreviewJobs) {
                ++jobs;
                g.pendingPreviews[key] = std::async(std::launch::async, [location = g.location, brush = *b] {
                    BrushGalleryState::Preview p;
                    const ai::GalleryFetch f = ai::fetchGalleryPreview(location, brush);
                    std::string err;
                    p.ok = f.ok && Image::decode(reinterpret_cast<const unsigned char*>(f.data.data()), f.data.size(),
                                                 p.image, err);
                    return p;
                });
            }
            const ImVec2 imagePos = ImGui::GetCursorScreenPos();
            if (tex != g.textures.end() && tex->second) {
                ImGui::Image(ImTextureID(uintptr_t(tex->second)), ImVec2(innerW, imageH));
            } else {
                ImGui::Dummy(ImVec2(innerW, imageH));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(imagePos, ImVec2(imagePos.x + innerW, imagePos.y + imageH), IM_COL32(28, 29, 33, 255), 4 * scale);
                const char* text = tex == g.textures.end() ? "Loading preview..." : "No preview";
                const ImVec2 ts = ImGui::CalcTextSize(text);
                dl->AddText(ImVec2(imagePos.x + (innerW - ts.x) * 0.5f, imagePos.y + (imageH - ts.y) * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), text);
            }
            if (ImGui::IsItemHovered() && !b->description.empty()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22);
                ImGui::TextUnformatted(b->description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::TextUnformatted(b->name.c_str());
            std::string byline = b->author.empty() ? std::string() : "by " + b->author;
            std::string tagLine;
            for (const auto& t : b->tags) tagLine += (tagLine.empty() ? "" : "  ") + t;
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(byline.empty() ? " " : byline.c_str());
            ImGui::TextUnformatted(tagLine.empty() ? " " : tagLine.c_str());
            ImGui::PopStyleColor();

            const bool installing = g.pendingInstalls.count(b->name) > 0;
            bool installed = false;
            for (const auto& r : doc_.brushes) installed = installed || sameName(r.name, b->name);
            if (installing) {
                ImGui::BeginDisabled();
                ImGui::Button(("Installing...##" + b->name).c_str(), ImVec2(-1, 0));
                ImGui::EndDisabled();
            } else if (installed) {
                ImGui::BeginDisabled();
                ImGui::Button(("Installed##" + b->name).c_str(), ImVec2(-1, 0));
                ImGui::EndDisabled();
            } else if (ImGui::Button(("Install##" + b->name).c_str(), ImVec2(-1, 0))) {
                g.installError.clear();
                g.pendingInstalls[b->name] = std::async(std::launch::async, [location = g.location, brush = *b] {
                    BrushGalleryState::Download d;
                    ai::downloadGalleryBrush(location, brush, d.path, d.error);
                    return d;
                });
            }
            ImGui::EndChild();
            ImGui::PopID();
            column = (column + 1) % columns;
        }
    }
    ImGui::EndChild();

    // Footer: where the brushes come from.
    if (!g.sourceEditInit) {
        std::snprintf(g.sourceEdit, sizeof g.sourceEdit, "%s", ai::brushGallerySetting().c_str());
        g.sourceEditInit = true;
    }
    hint(tr("Brushes are data only (no code), shared under CC0. Installed brushes join your library."));
    if (ai::brushGalleryFromEnvironment()) {
        hint(("Gallery: " + g.location + " (from SCULPTFORGE_BRUSH_GALLERY)").c_str());
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", tr("Gallery"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Use").x - ImGui::GetStyle().FramePadding.x * 2 - spacing);
        ImGui::InputTextWithHint("##gallerysource", tr("Community gallery (or a URL / folder for a studio mirror)"),
                                 g.sourceEdit, sizeof g.sourceEdit);
        ImGui::SameLine();
        if (ImGui::Button(tr("Use"))) {
            std::string err;
            if (ai::saveBrushGallerySetting(g.sourceEdit, err)) refreshBrushGallery();
            else status_ = err;
        }
    }
    ImGui::End();
}

} // namespace sf
