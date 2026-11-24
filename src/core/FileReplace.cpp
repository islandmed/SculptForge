// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/FileReplace.h"

#include <chrono>
#include <thread>

namespace fs = std::filesystem;

namespace sf {

bool replaceFile(const fs::path& temp, const fs::path& target, std::string& error) {
    std::error_code ec;
    fs::rename(temp, target, ec);
    // A target that something holds for a moment (scanner, sync client) or that is flagged read-only.
    for (int attempt = 0; ec && attempt < 6; ++attempt) {
        if (attempt == 2) {
            std::error_code perm;
            fs::permissions(target, fs::perms::owner_write, fs::perm_options::add, perm);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20 << attempt)); // 20 ms ... 1.3 s in all
        ec.clear();
        fs::rename(temp, target, ec);
    }
    if (!ec) return true;
    const std::string renameError = ec.message();
    // Not atomic, but it opens the target for writing (share-delete is not needed): better than losing the save.
    fs::copy_file(temp, target, fs::copy_options::overwrite_existing, ec);
    std::error_code ignored;
    fs::remove(temp, ignored);
    if (!ec) return true;
    const std::u8string name = target.u8string();
    error = "Cannot write " + std::string(name.begin(), name.end()) + ": " + renameError +
            " (another program may have it open, or it is read-only)";
    return false;
}

} // namespace sf
