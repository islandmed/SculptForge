// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/NativeDialog.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

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

} // namespace

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
}

void saveFile(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const SDL_DialogFileFilter* filters,
              int nfilters, const char* defaultLocation) {
    if (intercept(Kind::SaveFile, callback, userdata)) return;
    SDL_ShowSaveFileDialog(callback, userdata, window, filters, nfilters, defaultLocation);
}

void openFolder(SDL_DialogFileCallback callback, void* userdata, SDL_Window* window, const char* defaultLocation,
                bool allowMany) {
    if (intercept(Kind::OpenFolder, callback, userdata)) return;
    SDL_ShowOpenFolderDialog(callback, userdata, window, defaultLocation, allowMany);
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
    return SDL_OpenURL(url);
}

} // namespace sf::dialogs
