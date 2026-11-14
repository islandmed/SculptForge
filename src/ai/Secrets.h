// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AI keys live in the OS keychain (core/Keychain.h), never in ai.json. One keychain account per key:
//   ai/<preset id>   a Brush Lab provider's API key (ai/Providers.h: "ai/anthropic", "ai/openrouter", ...)
//   ai/huggingface   the Hugging Face token;  ai/fal, ai/meshy, ai/hyper3d, ai/tripo   the Generate 3D services' keys
// The app reads the keychain once (preload, on a worker thread at start-up) into a process cache, so drawing a panel
// never waits on it; the CLI and Python read an account the first time a command needs it (setLazy). Keys found in
// an older ai.json move into the keychain and out of the file (migrateLegacyKeys). With no keychain (SF_KEYCHAIN=none, a Linux without the Secret Service), a key typed in lasts
// until the app quits; the environment variables (ANTHROPIC_API_KEY, ...) work everywhere.
#pragma once

#include <string>
#include <vector>

namespace sf::ai {
struct AiSettings;
}

namespace sf::ai::secrets {

constexpr const char* kHuggingFace = "ai/huggingface";
constexpr const char* kFal = "ai/fal";
constexpr const char* kMeshy = "ai/meshy";
constexpr const char* kHyper3d = "ai/hyper3d";
constexpr const char* kTripo = "ai/tripo";
// The account of a Brush Lab preset's key.
std::string presetAccount(const std::string& presetId);
// Every account SculptForge may keep AI keys under.
std::vector<std::string> allAccounts();

// Reads every account into the cache (blocking: a worker thread, or a CLI start).
void preload();
// The cached key ("" if none). Lazy (the default): an account not read yet is read from the keychain now (blocking);
// the app turns that off and preloads instead.
std::string cached(const std::string& account);
void setLazy(bool lazy);
// Stores (or, for "", erases) a key in the keychain and the cache. False: not kept in the keychain (none, or it
// refused); the cache still holds it for this session.
bool put(const std::string& account, const std::string& value);
bool available();

// The keys SETTINGS holds in memory (typed in, or read from an older ai.json), into the keychain. Blocking.
// False if any couldn't be stored (they stay in memory for this session).
bool storeKeys(const AiSettings& settings);
// Moves keys found in the ai.json at PATH into the keychain and rewrites the file without them. Blocking; does
// nothing when the file holds no key. MESSAGE says what happened ("" when nothing did).
bool migrateLegacyKeys(const std::string& path, std::string& message);

// Tests: empty the cache.
void clearCacheForTests();

} // namespace sf::ai::secrets
