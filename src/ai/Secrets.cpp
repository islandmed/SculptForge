// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/Secrets.h"

#include "ai/BrushAssistant.h"
#include "ai/Providers.h"
#include "core/Json.h"
#include "core/Keychain.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>

namespace sf::ai::secrets {

namespace {

std::mutex gMutex;
std::map<std::string, std::string>& cache() {
    static std::map<std::string, std::string> m;
    return m;
}
std::map<std::string, bool>& tried() { // accounts already read from the keychain
    static std::map<std::string, bool> m;
    return m;
}
bool gLazy = true;

} // namespace

std::string presetAccount(const std::string& presetId) { return "ai/" + presetId; }

std::vector<std::string> allAccounts() {
    std::vector<std::string> out;
    for (const ProviderPreset& p : providerPresets())
        if (p.kind != AiSettings::Provider::Free) out.push_back(presetAccount(p.id));
    out.push_back(kHuggingFace);
    out.push_back(kFal);
    out.push_back(kMeshy);
    out.push_back(kHyper3d);
    out.push_back(kTripo);
    return out;
}

void preload() {
    for (const std::string& account : allAccounts()) {
        std::string value;
        const bool found = keychain::load(account, value);
        std::lock_guard lock(gMutex);
        tried()[account] = true;
        if (found && !cache().count(account)) cache()[account] = value; // (a key typed in meanwhile wins)
    }
}

std::string cached(const std::string& account) {
    {
        std::lock_guard lock(gMutex);
        auto it = cache().find(account);
        if (it != cache().end()) return it->second;
        if (!gLazy || tried()[account]) return {};
        tried()[account] = true;
    }
    std::string value;
    if (!keychain::load(account, value)) return {};
    std::lock_guard lock(gMutex);
    cache()[account] = value;
    return value;
}

void setLazy(bool lazy) {
    std::lock_guard lock(gMutex);
    gLazy = lazy;
}

bool put(const std::string& account, const std::string& value) {
    {
        std::lock_guard lock(gMutex);
        auto it = cache().find(account);
        if (it != cache().end() && it->second == value) return true; // unchanged: no keychain call (no prompt)
        if (value.empty()) cache().erase(account);
        else cache()[account] = value;
    }
    return value.empty() ? keychain::erase(account) : keychain::store(account, value);
}

bool available() { return keychain::available(); }

bool storeKeys(const AiSettings& s) {
    bool ok = true;
    if (!s.apiKey.empty() && s.provider != AiSettings::Provider::Free) ok = put(presetAccount(providerId(s)), s.apiKey) && ok;
    if (!s.hfToken.empty()) ok = put(kHuggingFace, s.hfToken) && ok;
    if (!s.falKey.empty()) ok = put(kFal, s.falKey) && ok;
    if (!s.meshyKey.empty()) ok = put(kMeshy, s.meshyKey) && ok;
    if (!s.hyper3dKey.empty()) ok = put(kHyper3d, s.hyper3dKey) && ok;
    if (!s.tripoKey.empty()) ok = put(kTripo, s.tripoKey) && ok;
    return ok;
}

bool migrateLegacyKeys(const std::string& path, std::string& message) {
    message.clear();
    std::ifstream f(std::filesystem::path(std::u8string(path.begin(), path.end())), std::ios::binary);
    if (!f) return true;
    const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    Json j;
    std::string err;
    if (!Json::parse(text, j, err) || !j.isObject()) return true;
    bool any = false;
    for (const char* k : {"apiKey", "hfToken", "falKey", "meshyKey"}) any = any || !j[k].asString().empty();
    if (!any) return true;
    const AiSettings s = AiSettings::fromJson(j);
    if (!keychain::available() || !storeKeys(s)) {
        message = "AI keys: no keychain on this computer, so they can't be kept between sessions (the environment "
                  "variables ANTHROPIC_API_KEY, OPENAI_API_KEY, HF_TOKEN, FAL_KEY, MESHY_API_KEY work instead)";
        // They are still dropped from the file: keys never stay in plain files. This session has them.
    } else {
        message = "AI keys moved into the system keychain (they are no longer in ai.json)";
    }
    if (!s.save(path, err)) {
        message = "AI keys: " + err;
        return false;
    }
    return true;
}

void clearCacheForTests() {
    std::lock_guard lock(gMutex);
    cache().clear();
    tried().clear();
}

} // namespace sf::ai::secrets
