// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A UI test that starts from saved settings: `# prefs: {...}` (JSON, one line) in the script's first lines becomes
// the isolated profile's project.json before the app reads it (tests/ui/discord_options_note.sfui). Guards, since
// it writes a settings file: only into an isolated profile (the folder isolateSettings made, and userDataDir must be
// inside it), never with SF_ISOLATE=0; anything else refuses and writes nothing. A seeded profile counts as an
// install of this version with the new-look card seen (last_seen_version, new_look_card_done) unless the line says
// otherwise, so no What's New or card opens over the test. tests/test_uitest_seed.cpp.
#pragma once

#include <string>

namespace sf::uitest {

inline constexpr const char* kPrefsTag = "# prefs: ";

// "" when project.json was written; "none" when the script has no prefs line; else why it refused (nothing written).
std::string seedPrefs(const std::string& script, const std::string& isolatedDir, const std::string& userDataDir,
                      const char* isolateEnv, const char* version);

} // namespace sf::uitest
