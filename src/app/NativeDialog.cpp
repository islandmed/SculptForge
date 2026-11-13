// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/NativeDialog.h"

#include "app/OpenUrl.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>

#ifdef __ANDROID__
#include <chrono>
#include <filesystem>
#include <vector>
#endif

namespace sf::dialogs {

namespace {

std::atomic<bool> g_scripted{false};
std::atomic<int> g_requested{0};
std::atomic<int> g_lastKind{int(Kind::None)};
std::mutex g_mutex;
std::string g_answer;
bool g_haveAnswer = false;

const char* kindName(Kind k) {
    switch (k) {
    case Kind::OpenFile: return "open file";
    case Kind::SaveFile: return "save file";
    case Kind::OpenFolder: return "open folder";
    case Kind::Message: return "message box";
    case Kind::Url: return "open url";
    default: return "none";
    }
}

// Logs and counts the request; true when it must not open (then CALLBACK, if any, has been answered).
bool intercept(Kind kind, SDL_DialogFileCallback callback, void* userdata) {
    if (!suppressed()) return false;
    ++g_requested;
    g_lastKind = int(kind);
    std::string answer;
    {
        std::lock_guard lock(g_mutex);
        if (g_haveAnswer) answer = std::exchange(g_answer, std::string());
        g_haveAnswer = false;
    }
    SDL_Log("dialog requested: %s (none opens here: %s)", kindName(kind),
            !callback ? "not shown" : answer.empty() ? "cancelled" : answer.c_str());
    if (callback) {
        const char* const cancelled[] = {nullptr};
        const char* const chosen[] = {answer.c_str(), nullptr};
        callback(userdata, answer.empty() ? cancelled : chosen, -1);
    }
    return true;
}

#ifdef __ANDROID__
// Android (android/README.md): the system document picker answers content:// URIs, which the app's file code can't
// open. Each picked file is copied (SDL reads content URIs) into the app's own storage first and the app gets that
// copy. Saving needs no picker: projects go to the app's Projects folder (androidProjectsDir), where Recent files and
// a USB cable find them. (M2: export to any folder through the picker.)
std::string fileNameOf(const std::string& uri) {
    std::string name = uri.substr(uri.find_last_of('/') + 1);
    std::string out; // percent-decoded ("primary%3ADownload%2Fbust.sfproj")
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '%' && i + 2 < name.size()) {
            out += char(std::strtol(name.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else out += name[i];
    }
    if (const size_t cut = out.find_last_of("/:"); cut != std::string::npos) out = out.substr(cut + 1);
    return out;
}

std::string firstExtension(const SDL_DialogFileFilter* filters, int nfilters) {
    if (nfilters < 1 || !filters || !filters[0].pattern) return {};
    std::string p = filters[0].pattern;
    p = p.substr(0, p.find(';'));
    return p == "*" ? std::string() : p;
}

bool copyIn(const char* from, const std::filesystem::path& to) {
    SDL_IOStream* in = SDL_IOFromFile(from, "rb");
    if (!in) return false;
    SDL_IOStream* out = SDL_IOFromFile(to.string().c_str(), "wb");
    bool ok = out != nullptr;
    char buf[1 << 16];
    for (size_t n; ok && (n = SDL_ReadIO(in, buf, sizeof buf)) > 0;) ok = SDL_WriteIO(out, buf, n) == n;
    SDL_CloseIO(in);
    if (out) ok = SDL_CloseIO(out) && ok;
    return ok;
}

struct PickedCopy {
    SDL_DialogFileCallback callback;
    void* userdata;
    std::string extension; // for a URI whose last part has no name ("document/12")
};

void SDLCALL copyPicked(void* userdata, const char* const* uris, int filter) {
    PickedCopy* p = static_cast<PickedCopy*>(userdata);
    std::vector<std::string> local;
    if (uris) {
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::path(SDL_GetAndroidInternalStoragePath()) / "Opened";
        std::filesystem::create_directories(dir, ec);
        for (int i = 0; uris[i]; ++i) {
            std::string name = fileNameOf(uris[i]);
            if (name.find('.') == std::string::npos && !p->extension.empty()) name += "." + p->extension;
            if (name.empty()) name = "picked";
            if (copyIn(uris[i], dir / name)) local.push_back((dir / name).string());
            else SDL_Log("Couldn't copy %s: %s", uris[i], SDL_GetError());
        }
    }
    std::vector<const char*> list;
    for (const std::string& l : local) list.push_back(l.c_str());
    list.push_back(nullptr);
    p->callback(p->userdata, uris ? list.data() : nullptr, filter);
    delete p;
}
#endif

} // namespace

#ifdef __ANDROID__
std::string androidProjectsDir() {
    const char* ext = SDL_GetAndroidExternalStoragePath(); // (app-specific: no storage permission)
    std::filesystem::path dir = std::filesystem::path(ext ? ext : SDL_GetAndroidInternalStoragePath()) / "Projects";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}
#endif

bool suppressed() {
    if (g_scripted) return true;
    if (const char* e = std::getenv("SF_NO_DIALOGS"); e && *e && std::strcmp(e, "0") != 0) return true;
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver) driver = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    if (!driver) driver = std::getenv("SDL_VIDEO_DRIVER");
    return driver && (std::strcmp(driver, "offscreen") == 0 || std::strcmp(driver, "dummy") == 0);
}

// A script starts counting from zero: the warm UI pool runs many scripts in one process (tools/test/warm_ui.py), so a
// request left over from the previous script must not show up in the next one's dialogs_requested.
void setScripted(bool on) {
    g_scripted = on;
    if (on) {
        g_requested = 0;
        g_lastKind = int(Kind::None);
        std::lock_guard<std::mutex> lock(g_mutex);
        g_answer.clear();
        g_haveAnswer = false;
    }
}

void answerNext(std::string path) {
    std::lock_guard lock(g_mutex);
    g_answer = std::move(path);
    g_haveAnswer = true;
}

int requested() { return g_requested; }
Kind lastKind() { return Kind(g_lastKind.load()); }

void openFile(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const SDL_DialogFileFilter* filters,
              int nfilters, const char* defaultLocation, bool allowMany, const char* title) {
    if (intercept(Kind::OpenFile, callback, userdata)) return;
#ifdef __ANDROID__
    auto* copy = new PickedCopy{callback, userdata, firstExtension(filters, nfilters)};
    SDL_ShowOpenFileDialog(copyPicked, copy, window, filters, nfilters, nullptr, allowMany);
    (void)title;
    return;
#else
    if (!title) {
        SDL_ShowOpenFileDialog(callback, userdata, window, filters, nfilters, defaultLocation, allowMany);
        return;
    }
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, const_cast<SDL_DialogFileFilter*>(filters));
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, nfilters);
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, defaultLocation);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, allowMany);
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFILE, callback, userdata, props);
    SDL_DestroyProperties(props);
#endif
}

void saveFile(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const SDL_DialogFileFilter* filters,
              int nfilters, const char* defaultLocation) {
    if (intercept(Kind::SaveFile, callback, userdata)) return;
#ifdef __ANDROID__
    std::string name = defaultLocation && *defaultLocation ? std::filesystem::path(defaultLocation).filename().string() : "";
    const std::string ext = firstExtension(filters, nfilters);
    if (name.empty()) name = "Untitled" + (ext.empty() ? std::string() : "." + ext);
    std::filesystem::path path = std::filesystem::path(androidProjectsDir()) / name;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) { // never over another project: "bust-1731340000.sfproj"
        const auto t = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch());
        path.replace_filename(path.stem().string() + "-" + std::to_string(t.count()) + path.extension().string());
    }
    const std::string chosen = path.string();
    const char* const list[] = {chosen.c_str(), nullptr};
    SDL_Log("Saving to %s (the app's Projects folder)", chosen.c_str());
    callback(userdata, list, 0);
    (void)window;
#else
    SDL_ShowSaveFileDialog(callback, userdata, window, filters, nfilters, defaultLocation);
#endif
}

void openFolder(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const char* defaultLocation,
                bool allowMany) {
    if (intercept(Kind::OpenFolder, callback, userdata)) return;
#ifdef __ANDROID__
    // No folder picker the app's file code can use (content:// trees): as a cancel, for now.
    SDL_Log("dialog requested: open folder (not on Android yet)");
    const char* const cancelled[] = {nullptr};
    callback(userdata, cancelled, -1);
    (void)window, (void)defaultLocation, (void)allowMany;
#else
    SDL_ShowOpenFolderDialog(callback, userdata, window, defaultLocation, allowMany);
#endif
}

bool message(SDL_MessageBoxFlags flags, const char* title, const char* text, SDL_Window* window) {
    if (intercept(Kind::Message, nullptr, nullptr)) return false;
    return SDL_ShowSimpleMessageBox(flags, title, text, window);
}

bool openUrl(const char* url) {
    if (intercept(Kind::Url, nullptr, nullptr)) {
        SDL_Log("  (url: %s)", url ? url : "");
        return false;
    }
    if (!url) return SDL_OpenURL(url);
    { // the same URL at most once per 2 s (a double click, a key repeat, two handlers for one button)
        static sf::opener::Debounce debounce;
        static std::mutex mutex;
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard lock(mutex);
        if (!debounce.allow(url, now)) {
            SDL_Log("open url: %s again within 2 s, once is enough", url);
            return true;
        }
    }
#if defined(__linux__) && !defined(__ANDROID__)
    // The browser gets the environment the app was started with (no AppImage paths, no test-run XDG folders), detached.
    if (sf::opener::launch(url)) return true;
#endif
    return SDL_OpenURL(url);
}

} // namespace sf::dialogs
