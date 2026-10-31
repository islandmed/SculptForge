// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Moving a file to the desktop's trash (recoverable), for files the artist removes from a library inside the app.
#pragma once

#include <string>

namespace sf {

// Linux and other freedesktop systems: the home trash ($XDG_DATA_HOME/Trash, the files/ + info/ layout the
// file managers restore from), for files on the same drive as it. Windows: the Recycle Bin. Elsewhere (macOS)
// it isn't supported yet. On success `where` names the trash; on failure the file is left where it was.
bool moveToTrash(const std::string& path, std::string& where, std::string& error);
// Whether moveToTrash can work on this system at all.
bool trashSupported();

} // namespace sf
