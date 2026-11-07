// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One-click update (the update banner's Update button, app/PanelProject.cpp). What it trusts, in order:
//   1. Files come only from https://github.com/islandmed/SculptForge/releases/download/<tag>/<file>, and a redirect
//      only to GitHub's release asset hosts (ai::httpGetRestricted checks every hop; certificates are checked).
//   2. The release's SHA256SUMS.txt must carry a valid Ed25519 signature (SHA256SUMS.txt.sig, made by the release
//      workflow) under the public key compiled into the app (kUpdatePublicKeyHex below).
//   3. The package's SHA-256 must be the one SHA256SUMS.txt names; it is checked again right before installing.
// Until all three pass nothing is written in place and nothing downloaded runs. The package goes to a temporary
// file next to what it replaces (same folder, so the final rename is one atomic step); the old version is kept as
// "<name>.old" until the new one has started once, and put back when it doesn't start.
// Package-managed installs (Flatpak, pacman/AUR, .deb/.rpm, Homebrew) are never replaced: the banner names the
// command instead. Linux AppImage and Windows (installer and portable zip) update themselves; macOS opens the
// download page (see docs/design/one-click-update.md).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace sf::update {

inline constexpr const char* kReleaseDownloadPrefix = "https://github.com/islandmed/SculptForge/releases/download/";
inline constexpr uint64_t kMaxPackageBytes = uint64_t(1) << 30; // 1 GiB (an AppImage is ~150 MB)
inline constexpr size_t kMaxSumsBytes = 256 * 1024;
inline constexpr int kMaxUnconfirmedStarts = 3; // a new version that never got going this often is rolled back

// The public half of the release signing key (Ed25519, 32 bytes as hex; created 11-05). Empty would mean no key: the
// Update button off (the banner's Download link still works). The private half is the repository secret UPDATE_SIGNING_KEY,
// used only by .github/workflows/release.yml.
inline constexpr const char* kUpdatePublicKeyHex = "f6f8f8e9e02123a759a974bffaa07db8ebaa0c70ba746a0a86c644eba6a68642";

// --- Where files may come from ---

// https://github.com/islandmed/SculptForge/releases/download/<tag>/<file>: nothing else (no port, user, query,
// "..", backslash or percent-escape).
bool isReleaseDownloadUrl(const std::string& url);
// GitHub's release asset hosts, where the URL above redirects (https only, exact host names).
bool isReleaseAssetHostUrl(const std::string& url);
// (Member builds add their API URLs: member::isMemberApiUrl.)
bool allowedUpdateUrl(const std::string& url);
// A release version safe to put in a URL ("0.12.0", "0.12.0-beta.1").
bool safeVersion(const std::string& version);
// The download URL of `file` in release v<version>; "" when the version or file name isn't safe.
std::string releaseFileUrl(const std::string& version, const std::string& file);

// --- What a release must prove ---

// The release file that is downloaded for `asset` (a version-less name such as SculptForge-linux-x86_64.AppImage):
// its versioned twin, SculptForge-0.12.0-x86_64.AppImage. Its line in the signed SHA256SUMS.txt ties the package to
// that version, so an older release's signed files can't be passed off as a newer one's. "" for unknown assets.
std::string versionedAsset(const std::string& asset, const std::string& version);

bool parseHexKey(const std::string& hex, uint8_t key[32]);
// The signature (64 raw bytes) over SHA256SUMS.txt's exact bytes.
bool sumsSignatureValid(const std::string& sums, const std::string& signature, const uint8_t key[32]);
// The lowercase SHA-256 SHA256SUMS.txt lists for `file` ("" when it isn't listed exactly once).
std::string sumFor(const std::string& sums, const std::string& file);

// --- What kind of install this is ---

enum class InstallKind {
    None,             // built from source, or somewhere that can't be told apart: the Download link only
    AppImage,         // target: the .AppImage file
    WindowsInstaller, // target: the install folder (bin\sculptforge.exe, Uninstall.exe)
    WindowsPortable,  // target: the unzipped folder (bin\sculptforge.exe, no uninstaller)
    MacApp,           // target: the .app bundle (not self-updated yet)
    PackageManager,   // Flatpak, pacman/AUR, .deb/.rpm, Homebrew: `command` says what to run
};

struct Install {
    InstallKind kind = InstallKind::None;
    std::string target;  // what gets replaced
    std::string stagingDir; // where the download goes when not next to the target (Windows installer: %LOCALAPPDATA%)
    std::string asset;   // the release file that replaces it (version-less; versionedAsset names the download)
    std::string command; // PackageManager: the update command, "" when there is none (e.g. a downloaded .deb)
    std::string manager; // PackageManager: "Flatpak", "pacman", ...
};

// What detectInstall looks at, so tests can describe any machine.
struct InstallProbe {
    std::string os;      // "linux", "windows", "macos"
    std::string arch;    // "x86_64", "arm64"
    std::string exePath; // this program, absolute
    std::string appImage;  // $APPIMAGE (set by the AppImage runtime)
    std::string flatpakId; // $FLATPAK_ID
    std::string localAppData; // %LOCALAPPDATA% (Windows)
    std::function<bool(const std::string&)> exists;
};
InstallProbe currentInstallProbe(); // this process
Install detectInstall(const InstallProbe& probe);
inline bool selfUpdates(InstallKind k) {
    return k == InstallKind::AppImage || k == InstallKind::WindowsInstaller || k == InstallKind::WindowsPortable;
}

// --- The download (a worker thread) ---

struct FetchResult {
    long status = 0;
    std::string error;
};
// GET `url` (the app: ai::httpGetRestricted with allowedUpdateUrl). onData false or onProgress false stop it.
using Fetch = std::function<FetchResult(const std::string& url, const std::function<bool(const char*, size_t)>& onData,
                                        const std::function<bool(uint64_t received, uint64_t total)>& onProgress)>;

struct Download {
    std::atomic<uint64_t> received{0}, total{0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    bool ok = false;
    std::string error;
    std::string staged; // the verified package, next to the install's target
    std::string sha256; // what SHA256SUMS.txt says it is (checked again before installing)
};

// SHA256SUMS.txt, its signature, then the package into a temporary file in the target's folder, hashed as it
// arrives. Fails (removing the temporary file) on any doubt; on success `out.staged` is ready to install.
// `memberFiles` (a member build, core/MemberBuilds.h): the files by name as member API URLs; empty: the public release.
void downloadUpdate(const std::string& version, const Install& install, const uint8_t key[32], const Fetch& fetch,
                    Download& out, const std::map<std::string, std::string>& memberFiles = {});

// The temporary file a package is downloaded to: ".download-<version>-<asset>" (the asset's extension last) next to
// the target, inside the portable folder, or in the installer's staging folder.
std::string stagingPath(const Install& install, const std::string& version);

// --- Installing, the first start, rolling back ---

// The note an install leaves for the next start (<prefs>/update_pending.json).
struct Pending {
    std::string version; // the new one
    std::string from;    // the one it replaced
    std::string target;
    std::string backup;  // target + ".old"
    int starts = 0;      // starts of `version` that haven't been confirmed yet
};
bool readPending(const std::string& notePath, Pending& out);
bool writePending(const std::string& notePath, const Pending& p);

// Linux AppImage: checks the staged file's hash again, makes it executable, keeps the current file as <target>.old
// and renames the new one over the target (one atomic step; the running app keeps its open file). Writes the note.
bool installAppImage(const Install& install, const Download& d, const std::string& version,
                     const std::string& fromVersion, const std::string& notePath, std::string& error);

// Puts the backup back over the target. The note is removed.
bool rollback(const Pending& p, const std::string& notePath, std::string& error);

enum class StartCheck {
    Nothing,    // no note
    Counting,   // this is the new version's start number p.starts: confirmStarted() once it is up
    RolledBack, // it never got going (kMaxUnconfirmedStarts): the old version is back, run p.target again
    Abandoned,  // another version is running (the user started the .old one): the note is dropped
};
// At startup, before anything that could crash.
StartCheck checkPendingAtStart(const std::string& notePath, const std::string& runningVersion, Pending& p,
                               std::string& error);
// The new version is up: the backup and the note go.
void confirmStarted(const std::string& notePath);

// Linux AppImage: the process at procDir (/proc/<pid>) is an earlier SculptForge run from the same AppImage file:
// ours (same user), its program is named sculptforge, and its APPIMAGE is appImage. Anything else (the file manager,
// a shell, another user's process, another copy of SculptForge): false. Always false off Linux.
bool isOldAppImage(const std::string& procDir, const std::string& appImage);

// Windows: the PowerShell helper that waits for this process (`pid`) to end, checks the staged package's hash again,
// installs it (the installer silently, or the zip unpacked beside the folder and swapped in, the old folder kept
// as .old), starts the new version when `relaunch`, and rolls a portable folder back if the new one exits at once.
std::string windowsHelperScript(const Install& install, const Download& d, unsigned long pid, bool relaunch);

} // namespace sf::update
