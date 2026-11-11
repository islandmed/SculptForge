// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The welcome window's "Featured creator" card (core/Featured.h): a video by an artist who made something about
// SculptForge, with its thumbnail, the title and who made it. The feed (featured.json beside latest.json) and the
// thumbnails come with the daily update check and are kept in the preferences folder; the card shows one entry a
// start, opens its link in the browser, and goes when updates are off, the check can't reach the site or the list is
// empty. The small x hides that video for good.
#include "app/App.h"
#include "app/I18n.h"
#include "app/NativeDialog.h"
#include "app/Theme.h"
#include "ai/Http.h"
#include "core/Featured.h"
#include "core/Image.h"
#include "render/GL.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace sf {

namespace {

namespace fs = std::filesystem;
fs::path fsPath(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

constexpr const char* kFeedFile = "featured.json";
constexpr size_t kHiddenMax = 64;

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(fsPath(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

// Written beside its final name and renamed over it, so a quit mid-write never leaves half a file.
bool writeFile(const std::string& path, const std::string& bytes) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(fsPath(tmp), std::ios::binary | std::ios::trunc);
        if (!f.write(bytes.data(), std::streamsize(bytes.size()))) return false;
    }
    std::error_code ec;
    fs::rename(fsPath(tmp), fsPath(path), ec);
    return !ec;
}

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// The directory part of a local feed path, with its separator ("" for a bare name).
std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

bool isWebUrl(const std::string& s) { return s.rfind("https://", 0) == 0 || s.rfind("http://", 0) == 0; }

} // namespace

// A feed that is a URL or a local file (a studio's mirror on a share; UI tests).
bool App::readFeedText(const std::string& from, std::string& body, std::string& error) {
    error.clear();
    body.clear();
    if (isWebUrl(from)) {
        ai::HttpResponse r = ai::httpRequest("GET", from, {"Accept: application/json"}, {}, 15);
        if (r.status == 200) body = std::move(r.body);
        else error = r.status ? "the server answered " + std::to_string(r.status) : (r.error.empty() ? "no connection" : r.error);
    } else if (!readFile(from, body)) {
        error = "can't read " + from;
    }
    return error.empty();
}

// On the update check's worker thread, after latest.json: featured.json, then the thumbnails not kept yet.
void App::fetchFeatured(UpdateJob& job, const std::string& source, const std::string& fallback, const std::string& dir) {
    std::string body, error;
    const std::string from = featuredFeedSource(source), local = isWebUrl(source) ? std::string() : folderOf(source);
    if (from.empty()) return; // not a feed we know a sibling of: the card stays as it is
    job.featuredTried = true;
    std::vector<FeaturedItem> items;
    bool ok = readFeedText(from, body, error) && parseFeaturedFeed(body, items, error);
    if (!ok && !fallback.empty()) {
        const std::string mirror = featuredFeedSource(fallback);
        ok = !mirror.empty() && readFeedText(mirror, body, error) && parseFeaturedFeed(body, items, error);
    }
    if (!ok) return;
    std::error_code ec;
    fs::create_directories(fsPath(dir), ec);
    std::set<std::string> keep{kFeedFile};
    for (const FeaturedItem& it : items) {
        const std::string name = featuredThumbFile(it.thumbUrl);
        keep.insert(name);
        if (fs::is_regular_file(fsPath(dir + name), ec)) continue;
        std::string bytes;
        if (!local.empty()) { // a local feed's thumbnails are beside it: https://host/featured/x.jpg -> <folder>/featured/x.jpg
            const size_t pathAt = it.thumbUrl.find('/', 8);
            if (pathAt != std::string::npos) readFile(local + it.thumbUrl.substr(pathAt + 1), bytes);
        } else {
            const ai::HttpResponse r = ai::httpGetRestricted(
                it.thumbUrl, [](const std::string& url) { return featuredThumbAllowed(url); }, 15,
                [&](const char* data, size_t size) {
                    bytes.append(data, size);
                    return bytes.size() <= kFeaturedThumbMax; // a longer file is cut off here and refused below
                });
            if (r.status != 200) bytes.clear();
        }
        Image probe;
        std::string imgErr;
        if (bytes.empty() || bytes.size() > kFeaturedThumbMax ||
            !Image::decode(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), probe, imgErr))
            continue; // the card shows without its picture
        writeFile(dir + name, bytes);
    }
    writeFile(dir + kFeedFile, body);
    for (const auto& e : fs::directory_iterator(fsPath(dir), ec)) { // thumbnails of entries that left the feed
        const std::string n = e.path().filename().string();
        if (n.rfind("featured-", 0) == 0 && !keep.count(n)) fs::remove(e.path(), ec);
    }
    job.featured = std::move(items);
    job.featuredOk = true;
}

// A finished check's featured feed: kept and shown, or (the site couldn't be reached) gone: offline shows nothing.
void App::applyFeaturedJob(const UpdateJob& job) {
    if (!job.featuredTried) return;
    if (!job.featuredOk) {
        featured_.clear();
        featuredShown_ = -1;
        std::error_code ec;
        if (!prefDir_.empty())
            for (const auto& e : fs::directory_iterator(fsPath(prefDir_), ec)) {
                const std::string n = e.path().filename().string();
                if (n == kFeedFile || n.rfind("featured-", 0) == 0) fs::remove(e.path(), ec);
            }
        return;
    }
    const std::string shownUrl = featuredShown_ >= 0 && featuredShown_ < int(featured_.size()) ? featured_[size_t(featuredShown_)].url : "";
    featured_ = job.featured;
    featuredShown_ = -1;
    for (size_t i = 0; i < featured_.size() && !shownUrl.empty(); ++i) // the one on screen stays while it is still listed
        if (featured_[i].url == shownUrl && pickFeatured({featured_[i]}, featuredDay(nowSeconds()), featuredHidden_, 0) == 0)
            featuredShown_ = int(i);
    if (featuredShown_ < 0) pickFeaturedCard();
}

// At start: the kept feed (when updates are on), and this start's pick.
void App::loadFeaturedCache() {
    featured_.clear();
    featuredShown_ = -1;
    if (prefDir_.empty() || !checkUpdates_ || studioMode().active) return;
    std::string body, error;
    if (!readFile(prefDir_ + kFeedFile, body) || !parseFeaturedFeed(body, featured_, error)) {
        featured_.clear();
        return;
    }
    pickFeaturedCard();
}

void App::pickFeaturedCard() {
    featuredShown_ = pickFeatured(featured_, featuredDay(nowSeconds()), featuredHidden_, featuredLaunch_);
}

const FeaturedItem* App::featuredCard() const {
    if (!checkUpdates_ || studioMode().active || featuredShown_ < 0 || featuredShown_ >= int(featured_.size())) return nullptr;
    return &featured_[size_t(featuredShown_)];
}

void App::watchFeatured() {
    const FeaturedItem* it = featuredCard();
    if (!it || !featuredUrlAllowed(it->url)) return; // (checked again where it is opened)
    ++featuredWatchRequests_;
    if (!interactive() || !options_.uiTest.empty()) { // UI tests, renders and demos never start a browser
        status_ = "Watch: " + it->url;
        return;
    }
    status_ = dialogs::openUrl(it->url.c_str()) ? std::string("Opened the video in your browser")
                                                : std::string("Couldn't open ") + it->url + ": " + SDL_GetError();
}

void App::hideFeatured() {
    const FeaturedItem* it = featuredCard();
    if (!it) return;
    featuredHidden_.push_back(it->url);
    if (featuredHidden_.size() > kHiddenMax) featuredHidden_.erase(featuredHidden_.begin());
    pickFeaturedCard(); // the next one in its dates, or none
    saveProjectPrefs();
}

// The thumbnail as a texture, loaded once per entry from the kept file; 0 when there is none.
unsigned App::featuredTexture(const FeaturedItem& it) {
    if (featuredTexFor_ == it.url) return featuredTex_;
    if (featuredTex_) {
        const GLuint old = featuredTex_;
        gl::DeleteTextures(1, &old);
        featuredTex_ = 0;
    }
    featuredTexFor_ = it.url;
    std::string bytes, error;
    Image image;
    if (prefDir_.empty() || !readFile(prefDir_ + featuredThumbFile(it.thumbUrl), bytes) ||
        !Image::decode(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), image, error))
        return 0;
    image = image.downscaled(640);
    GLuint tex = 0;
    gl::GenTextures(1, &tex);
    gl::BindTexture(GL_TEXTURE_2D, tex);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl::TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl::PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(GL_TEXTURE_2D, 0, GLint(GL_RGBA8), image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    gl::BindTexture(GL_TEXTURE_2D, 0);
    featuredTex_ = tex;
    return featuredTex_;
}

// In the welcome window: picture on the left; "Featured creator", the title, who made it, Watch and the x on the right.
void App::drawFeaturedCard() {
    const FeaturedItem* it = featuredCard();
    if (!it) return;
    const float s = uiScale_;
    ImGui::PushID("featured");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    const ImVec2 size(176.0f * s, 99.0f * s); // 16:9
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##featured.thumb", size);
    const bool clicked = ImGui::IsItemClicked();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + size.x, at.y + size.y);
    if (const unsigned tex = featuredTexture(*it))
        dl->AddImageRounded(ImTextureID(uintptr_t(tex)), at, end, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, theme::rounding(4.0f));
    else
        dl->AddRectFilled(at, end, ImGui::GetColorU32(ImGuiCol_FrameBg), theme::rounding(4.0f));
    if (hovered) dl->AddRect(at, end, ImGui::GetColorU32(ImGuiCol_ButtonHovered), theme::rounding(4.0f), 0, 2.0f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", tr("Featured creator"));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 270.0f * s);
    ImGui::TextUnformatted(it->title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled("%s %s", tr("by"), it->creator.c_str());
    ImGui::Spacing();
    if (ImGui::Button(tr("Watch##featured.watch"))) watchFeatured();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Opens the video in your browser."));
    ImGui::SameLine();
    if (ImGui::SmallButton("x##featured.hide")) hideFeatured();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Hide this video."));
    ImGui::EndGroup();
    if (clicked) watchFeatured();
    ImGui::PopID();
}

} // namespace sf
