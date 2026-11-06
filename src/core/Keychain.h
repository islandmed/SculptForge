// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A secret in the operating system's keychain (the member-builds sign-in, core/MemberBuilds.h; AI provider keys,
// ai/Secrets.h; the studio sign-in, core/Oidc.h): Windows Credential Manager, the Secret Service on Linux (libsecret,
// loaded at run time: GNOME Keyring, KWallet, KeePassXC), the macOS Keychain (Security framework, a generic password
// per account under the service "SculptForge"). Where there is none, nothing is stored: a secret lasts until the app
// quits. Secrets never go to files, the preferences or the log. These calls can wait on the keychain (an unlock
// prompt): prefer worker threads. SF_KEYCHAIN=none turns the keychain off (render farms, CI).
#pragma once

#include <string>

namespace sf::keychain {

bool store(const std::string& account, const std::string& secret); // false: not stored (no keychain, or it refused)
bool load(const std::string& account, std::string& secret);         // false: none stored
bool erase(const std::string& account);                             // true when nothing is stored afterwards
// Whether this computer has a keychain SculptForge can use (Linux: libsecret is installed).
bool available();

// Tests: an in-memory keychain instead of the real one, so no test reads or writes the user's.
void useMemoryForTests(bool on);

} // namespace sf::keychain
