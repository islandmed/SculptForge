// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The names the Bridge exchange protocol needs byte for byte: the binary header, the shared folder layout and
// the other application's install names. They are the protocol's, not SculptForge's, and appear nowhere in the
// interface; they live only here (docs/legal/trademarks.md, tools/marks_allow.txt).
#pragma once

namespace sf::bridge {

inline constexpr char kExt[] = ".GoZ";      // what Bridge files are written as
inline constexpr char kExtLower[] = ".goz"; // matched without case
// File dialog filters (SDL: extensions without the dot); the macros go inside longer filter lists.
#define SF_BRIDGE_MESH_EXTS "GoZ;goz"
#define SF_BRIDGE_DIALOG_EXTS SF_BRIDGE_MESH_EXTS ";ztl;ZTL" // plus libraries the export plug-in converted
inline constexpr char kDialogPattern[] = SF_BRIDGE_MESH_EXTS;
inline constexpr char kHeader[] = "GoZb 1.0 ZBrush GoZ Binary";
inline constexpr char kMagic[] = "GoZb";
inline constexpr char kNamePrefix[] = "GoZMesh_";

// The shared bridge folder: %PUBLIC%\<kVendorFolder> on Windows, kMacRoot on macOS.
inline constexpr char kVendorFolder[] = "Pixologic";
inline constexpr char kMacRoot[] = "/Users/Shared/Pixologic";
inline constexpr char kInboxFolder[] = "GoZBrush";
inline constexpr char kProjectsFolder[] = "GoZProjects";
inline constexpr char kAppsFolder[] = "GoZApps";
inline constexpr char kObjectList[] = "GoZ_ObjectList.txt";
inline constexpr char kApplicationFile[] = "GoZ_Application.txt";
inline constexpr char kProjectPathFile[] = "GoZ_ProjectPath.txt";
inline constexpr char kInfoFile[] = "GoZ_Info.txt";
inline constexpr char kConfigFile[] = "GoZ_Config.txt";
inline constexpr char kInfoText[] =
    "NAME\t\t\t=\t\"SculptForge\"\nGOZ_VERSION\t\t=\t1\nEXTENSION\t\t=\t\".GoZ\"\n"
    "TAMPLATE\t\t=\t\"GoZ Complete Binary.GoZ\"\n\nEXPORT_FLIP_Y\t\t\t=\tFALSE\n"
    "EXPORT_FLIP_Z\t\t\t=\tFALSE\nIMPORT_FLIP_Y\t\t\t=\tFALSE\nIMPORT_FLIP_Z\t\t\t=\tFALSE\n";
inline constexpr char kNotifierWin[] = "GoZBrushFromApp.exe";
inline constexpr char kNotifierMac[] = "GoZBrushFromApp.app";

// Where the other application installs, to start it when its notifier is missing.
inline constexpr const char* kWinInstallDirs[] = {"C:\\Program Files", "C:\\Program Files\\Pixologic",
                                                  "C:\\Program Files\\Maxon"};
inline constexpr char kWinExe[] = "ZBrush.exe";
inline constexpr char kMacApp[] = "ZBrush.app";
inline constexpr char kAppNameLower[] = "zbrush"; // install folders and executables contain it
inline constexpr char kMacAppLower[] = "zbrush.app";

// Library files the Bridge export plug-in converts (opened by finding the converted Bridge files).
inline constexpr char kLibraryExtLower[] = ".ztl";

} // namespace sf::bridge
