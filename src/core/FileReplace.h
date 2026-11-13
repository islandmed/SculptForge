// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The last step of every "write next to it, then replace": put a finished temp file over the target. On Windows the
// rename fails with "Access is denied" when the target is read-only or something else has it open without delete
// sharing (a virus scanner, a sync client, an indexer); it is gone a moment later. So: try again for about a second,
// clear a read-only flag, and as a last resort copy over it. Either the target holds the new bytes or the caller gets
// an error that names the file, to show the user.
#pragma once

#include <filesystem>
#include <string>

namespace sf {

// Replaces `target` with `temp` (which is removed on success). On failure `temp` is removed too and `error` says why.
bool replaceFile(const std::filesystem::path& temp, const std::filesystem::path& target, std::string& error);

} // namespace sf
