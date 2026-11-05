// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A secret in the operating system's keychain (the member-builds sign-in, core/MemberBuilds.h): Windows Credential
// Manager, the Secret Service on Linux (libsecret, loaded at run time: GNOME Keyring, KWallet, KeePassXC), macOS
// Keychain later. Where there is none, nothing is stored: the sign-in lasts until the app quits. Secrets never go
// to files, the preferences or the log. These calls can wait on the keychain (an unlock prompt): worker threads only.
#pragma once

#include <string>

namespace sf::keychain {

bool store(const std::string& account, const std::string& secret); // false: not stored (no keychain, or it refused)
bool load(const std::string& account, std::string& secret);         // false: none stored
bool erase(const std::string& account);                             // true when nothing is stored afterwards

} // namespace sf::keychain
