// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/SelfUpdate.h"

#include "core/Ed25519.h"
#include "core/Json.h"
#include "core/MemberBuilds.h"
#include "core/Sha256.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace sf::update {

namespace fs = std::filesystem;

namespace {

fs::path fsPath(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
std::string pathString(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

bool urlChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_' || c == '+';
}

bool safeName(const std::string& s) {
    return !s.empty() && s.size() <= 128 && s != "." && s != ".." && s.find("..") == std::string::npos &&
           std::all_of(s.begin(), s.end(), urlChar);
}

std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return f ? std::string(std::istreambuf_iterator<char>(f), {}) : std::string();
}

std::string hashFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    Sha256 h;
    char buf[1 << 16];
    while (f) {
        f.read(buf, sizeof buf);
        if (f.gcount() > 0) h.update(buf, size_t(f.gcount()));
    }
    return h.hex();
}

} // namespace

// --- Where files may come from ---

bool isReleaseDownloadUrl(const std::string& url) {
    const std::string prefix = kReleaseDownloadPrefix;
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string rest = url.substr(prefix.size());
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) return false;
    return safeName(rest.substr(0, slash)) && safeName(rest.substr(slash + 1)); // <tag>/<file>, one level only
}

bool isReleaseAssetHostUrl(const std::string& url) {
    // Where GitHub serves release files from (it has moved once: objects. until 2025, release-assets. since).
    for (const char* host : {"https://objects.githubusercontent.com/", "https://release-assets.githubusercontent.com/"}) {
        const size_t n = std::char_traits<char>::length(host);
        if (url.size() > n && url.compare(0, n, host) == 0) return url.find('\\') == std::string::npos;
    }
    return false;
}

bool allowedUpdateUrl(const std::string& url) {
    return isReleaseDownloadUrl(url) || isReleaseAssetHostUrl(url) || member::isMemberApiUrl(url);
}

bool safeVersion(const std::string& version) {
    if (version.empty() || version.size() > 40 || !std::isdigit(static_cast<unsigned char>(version[0]))) return false;
    return std::all_of(version.begin(), version.end(), [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-';
           }) && version.find("..") == std::string::npos;
}

std::string releaseFileUrl(const std::string& version, const std::string& file) {
    if (!safeVersion(version) || !safeName(file)) return {};
    return std::string(kReleaseDownloadPrefix) + "v" + version + "/" + file;
}

// --- What a release must prove ---

std::string versionedAsset(const std::string& asset, const std::string& version) {
    if (!safeVersion(version)) return {};
    const std::string v = version.substr(0, version.find('-')); // packages carry the version without its prerelease
    if (asset == "SculptForge-linux-x86_64.AppImage") return "SculptForge-" + v + "-x86_64.AppImage";
    if (asset == "SculptForge-linux-arm64.AppImage") return "SculptForge-" + v + "-aarch64.AppImage";
    if (asset == "SculptForge-windows-x86_64-setup.exe") return "SculptForge-" + v + "-windows-x86_64.exe";
    if (asset == "SculptForge-windows-x86_64-portable.zip") return "SculptForge-" + v + "-windows-x86_64.zip";
    return {};
}

bool parseHexKey(const std::string& hex, uint8_t key[32]) {
    if (hex.size() != 64) return false;
    for (size_t i = 0; i < 32; ++i) {
        int v = 0;
        for (int k = 0; k < 2; ++k) {
            const char c = char(std::tolower(static_cast<unsigned char>(hex[i * 2 + k])));
            if (c >= '0' && c <= '9') v = v * 16 + (c - '0');
            else if (c >= 'a' && c <= 'f') v = v * 16 + (c - 'a' + 10);
            else return false;
        }
        key[i] = uint8_t(v);
    }
    return true;
}

bool sumsSignatureValid(const std::string& sums, const std::string& signature, const uint8_t key[32]) {
    return signature.size() == 64 && !sums.empty() &&
           ed25519Verify(reinterpret_cast<const uint8_t*>(signature.data()), sums.data(), sums.size(), key);
}

std::string sumFor(const std::string& sums, const std::string& file) {
    std::string found;
    int count = 0;
    size_t i = 0;
    while (i < sums.size()) { // sha256sum's lines: "<64 hex>  <name>" (or " *<name>" in binary mode)
        size_t e = sums.find('\n', i);
        if (e == std::string::npos) e = sums.size();
        std::string line = sums.substr(i, e - i);
        i = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*')) continue;
        std::string hash = line.substr(0, 64);
        if (!std::all_of(hash.begin(), hash.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); }))
            continue;
        if (line.substr(66) != file) continue;
        for (auto& c : hash) c = char(std::tolower(static_cast<unsigned char>(c)));
        found = hash;
        ++count;
    }
    return count == 1 ? found : std::string();
}

// --- What kind of install this is ---

InstallProbe currentInstallProbe() {
    InstallProbe p;
#if defined(_WIN32)
    p.os = "windows";
    wchar_t buf[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    if (n > 0 && n < std::size(buf)) p.exePath = pathString(fs::path(std::wstring(buf, n)));
    if (const wchar_t* local = _wgetenv(L"LOCALAPPDATA"); local && *local) p.localAppData = pathString(fs::path(local));
#elif defined(__APPLE__)
    p.os = "macos";
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::error_code ec;
        const fs::path c = fs::weakly_canonical(buf, ec);
        p.exePath = ec ? std::string(buf) : pathString(c);
    }
#else
    p.os = "linux";
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) p.exePath = pathString(exe);
    if (const char* a = std::getenv("APPIMAGE")) p.appImage = a;
    if (const char* f = std::getenv("FLATPAK_ID")) p.flatpakId = f;
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    p.arch = "arm64";
#else
    p.arch = "x86_64";
#endif
    p.exists = [](const std::string& path) {
        std::error_code ec;
        return fs::exists(fsPath(path), ec);
    };
    return p;
}

Install detectInstall(const InstallProbe& probe) {
    Install in;
    const auto exists = [&](const std::string& path) { return probe.exists && probe.exists(path); };
    if (probe.os == "linux") {
        if (!probe.flatpakId.empty() || exists("/.flatpak-info")) {
            in.kind = InstallKind::PackageManager;
            in.manager = "Flatpak"; // installed from our .flatpak file: `flatpak update` doesn't reach it, a new file does
            return in;
        }
        if (!probe.appImage.empty() && probe.appImage[0] == '/' && exists(probe.appImage)) {
            in.kind = InstallKind::AppImage;
            in.target = probe.appImage;
            in.asset = probe.arch == "arm64" ? "SculptForge-linux-arm64.AppImage" : "SculptForge-linux-x86_64.AppImage";
            return in;
        }
        if (probe.exePath.rfind("/usr/", 0) == 0 || probe.exePath.rfind("/opt/", 0) == 0) {
            in.kind = InstallKind::PackageManager;
            if (exists("/usr/bin/pacman")) {
                in.manager = "pacman";
                in.command = "yay -Syu sculptforge"; // the AUR package (Omarchy's too)
            } else if (exists("/usr/bin/dpkg")) {
                in.manager = ".deb"; // from the download page: no repository to upgrade from
            } else if (exists("/usr/bin/rpm")) {
                in.manager = ".rpm";
            } else {
                in.manager = "your package manager";
            }
        }
        return in;
    }
    if (probe.os == "macos") {
        if (exists("/opt/homebrew/Caskroom/sculptforge") || exists("/usr/local/Caskroom/sculptforge")) {
            in.kind = InstallKind::PackageManager;
            in.manager = "Homebrew";
            in.command = "brew upgrade --cask sculptforge";
            return in;
        }
        const size_t app = probe.exePath.find(".app/Contents/MacOS/");
        if (app != std::string::npos) {
            in.kind = InstallKind::MacApp;
            in.target = probe.exePath.substr(0, app + 4);
            in.asset = "SculptForge-macos.dmg";
        }
        return in;
    }
    if (probe.os == "windows") {
        const fs::path exe = fsPath(probe.exePath);
        if (exe.empty() || exe.parent_path().filename() != "bin") return in; // a build folder, not a package
        const fs::path root = exe.parent_path().parent_path();
        in.target = pathString(root);
        if (probe.exePath.find("\\WinGet\\Packages\\") != std::string::npos) {
            in.kind = InstallKind::PackageManager;
            in.manager = "winget";
            in.command = "winget upgrade SculptForge";
        } else if (exists(pathString(root / "Uninstall.exe"))) {
            // (winget's installer-type packages land here too: running the new installer updates the same entry
            // in Installed apps that winget reads, so both stay in step.)
            in.kind = InstallKind::WindowsInstaller;
            in.asset = "SculptForge-windows-x86_64-setup.exe";
            if (!probe.localAppData.empty()) in.stagingDir = pathString(fsPath(probe.localAppData) / "SculptForge" / "updates");
            else in.kind = InstallKind::None;
        } else {
            in.kind = InstallKind::WindowsPortable;
            in.asset = "SculptForge-windows-x86_64-portable.zip";
        }
        return in;
    }
    return in;
}

// --- The download ---

std::string stagingPath(const Install& install, const std::string& version) {
    const std::string name = "." + install.asset + ".download-" + version;
    if (!install.stagingDir.empty()) return pathString(fsPath(install.stagingDir) / name);
    if (install.kind == InstallKind::WindowsPortable) return pathString(fsPath(install.target) / name);
    return pathString(fsPath(install.target).parent_path() / name); // next to the AppImage: one rename away
}

namespace {

// A small file of the release (SHA256SUMS.txt, its signature) into memory, at most `cap` bytes.
bool fetchSmall(const Fetch& fetch, const std::string& url, size_t cap, Download& d, std::string& out) {
    out.clear();
    bool tooBig = false;
    const FetchResult r = fetch(
        url,
        [&](const char* data, size_t n) {
            if (out.size() + n > cap) return !(tooBig = true);
            out.append(data, n);
            return !d.cancel.load();
        },
        [&](uint64_t, uint64_t total) { return total <= cap && !d.cancel.load(); });
    if (d.cancel) d.error = "cancelled";
    else if (tooBig || (!r.error.empty() && out.size() >= cap)) d.error = url.substr(url.rfind('/') + 1) + " is too large";
    else if (!r.error.empty()) d.error = r.error;
    else if (r.status != 200) d.error = "the server answered " + std::to_string(r.status) + " for " + url.substr(url.rfind('/') + 1);
    return d.error.empty();
}

} // namespace

void downloadUpdate(const std::string& version, const Install& install, const uint8_t key[32], const Fetch& fetch,
                    Download& d, const std::map<std::string, std::string>& memberFiles) {
    struct Done {
        Download& d;
        ~Done() { d.done = true; }
    } done{d};
    d.ok = false;
    d.error.clear();
    // A public release's files by their fixed URLs; a member build's by the asset URLs its release lists.
    const auto urlOf = [&](const std::string& name) -> std::string {
        if (memberFiles.empty()) return releaseFileUrl(version, name);
        const auto it = memberFiles.find(name);
        return it != memberFiles.end() && member::isMemberAssetUrl(it->second) && safeVersion(version) ? it->second : "";
    };
    const std::string file = versionedAsset(install.asset, version), sumsUrl = urlOf("SHA256SUMS.txt"),
                      sigUrl = urlOf("SHA256SUMS.txt.sig"), packageUrl = file.empty() ? "" : urlOf(file);
    if (!selfUpdates(install.kind)) {
        d.error = "this install can't update itself";
        return;
    }
    if (sumsUrl.empty() || sigUrl.empty() || packageUrl.empty()) {
        d.error = "the release doesn't have " + (sumsUrl.empty() ? std::string("SHA256SUMS.txt") : sigUrl.empty() ? std::string("a signature") : file);
        return;
    }
    std::string sums, sig;
    if (!fetchSmall(fetch, sumsUrl, kMaxSumsBytes, d, sums) || !fetchSmall(fetch, sigUrl, 1024, d, sig)) return;
    if (!sumsSignatureValid(sums, sig, key)) {
        d.error = "the release's signature doesn't match: nothing was installed";
        return;
    }
    d.sha256 = sumFor(sums, file);
    if (d.sha256.empty()) {
        d.error = "the release doesn't list " + file;
        return;
    }

    const fs::path staged = fsPath(stagingPath(install, version));
    std::error_code ec;
    fs::create_directories(staged.parent_path(), ec);
    fs::remove(staged, ec); // (a download that was cut short)
    std::ofstream out(staged, std::ios::binary | std::ios::trunc);
    if (!out) {
        d.error = "can't write to " + pathString(staged.parent_path());
        return;
    }
    Sha256 hash;
    uint64_t received = 0;
    bool tooBig = false, writeFailed = false;
    const FetchResult r = fetch(
        packageUrl,
        [&](const char* data, size_t n) {
            if (received + n > kMaxPackageBytes) return !(tooBig = true);
            out.write(data, std::streamsize(n));
            if (!out) return !(writeFailed = true);
            hash.update(data, n);
            received += n;
            d.received = received;
            return !d.cancel.load();
        },
        [&](uint64_t, uint64_t total) {
            if (total > kMaxPackageBytes) return !(tooBig = true);
            if (total) d.total = total;
            return !d.cancel.load();
        });
    out.close();
    const uint64_t total = d.total;
    if (d.cancel) d.error = "cancelled";
    else if (tooBig) d.error = file + " is larger than " + std::to_string(kMaxPackageBytes >> 20) + " MB";
    else if (writeFailed || !out) d.error = "couldn't write the download (is the disk full?)";
    else if (!r.error.empty()) d.error = r.error;
    else if (r.status != 200) d.error = "the server answered " + std::to_string(r.status) + " for " + file;
    else if (total && received != total) d.error = "the download was cut short";
    else if (hash.hex() != d.sha256) d.error = "the download doesn't match the release's checksum: nothing was installed";
    if (!d.error.empty()) {
        fs::remove(staged, ec);
        return;
    }
    d.staged = pathString(staged);
    d.ok = true;
}

// --- Installing, the first start, rolling back ---

bool readPending(const std::string& notePath, Pending& p) {
    Json j;
    std::string err;
    const std::string text = readFile(fsPath(notePath));
    if (text.empty() || !Json::parse(text, j, err) || !j.isObject()) return false;
    p.version = j["version"].isString() ? j["version"].asString() : "";
    p.from = j["from"].isString() ? j["from"].asString() : "";
    p.target = j["target"].isString() ? j["target"].asString() : "";
    p.backup = j["backup"].isString() ? j["backup"].asString() : "";
    p.starts = int(j["starts"].asNumber(0));
    return !p.version.empty() && !p.target.empty() && p.backup == p.target + ".old";
}

bool writePending(const std::string& notePath, const Pending& p) {
    Json j = Json::object();
    j.set("version", p.version);
    j.set("from", p.from);
    j.set("target", p.target);
    j.set("backup", p.backup);
    j.set("starts", p.starts);
    const fs::path path = fsPath(notePath), tmp = fsPath(notePath + ".tmp");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!(f << j.dump())) return false;
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return !ec;
}

bool installAppImage(const Install& install, const Download& d, const std::string& version,
                     const std::string& fromVersion, const std::string& notePath, std::string& error) {
    const fs::path staged = fsPath(d.staged), target = fsPath(install.target), backup = fsPath(install.target + ".old");
    std::error_code ec;
    if (install.kind != InstallKind::AppImage || d.staged.empty() || staged.parent_path() != target.parent_path()) {
        error = "nothing to install";
        return false;
    }
    if (hashFile(staged) != d.sha256 || d.sha256.empty()) { // (it sat on disk since the download)
        fs::remove(staged, ec);
        error = "the download changed since it was checked: nothing was installed";
        return false;
    }
    fs::permissions(staged, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                fs::perms::others_read | fs::perms::others_exec, ec);
    if (ec) {
        error = "can't make the new version executable: " + ec.message();
        return false;
    }
    // The current file stays as .old (a hard link: the same file under a second name; a copy where links don't work).
    fs::remove(backup, ec);
    fs::create_hard_link(target, backup, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(target, backup, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            error = "can't keep the current version as a backup: " + ec.message();
            return false;
        }
    }
    Pending p{version, fromVersion, install.target, install.target + ".old", 0};
    if (!writePending(notePath, p)) {
        fs::remove(backup, ec);
        error = "can't write the update note";
        return false;
    }
    fs::rename(staged, target, ec); // one step: the target is the old file or the new one, never half of either
    if (ec) {
        error = "can't replace " + install.target + ": " + ec.message();
        fs::remove(fsPath(notePath), ec);
        std::error_code ec2;
        fs::remove(backup, ec2);
        return false;
    }
    return true;
}

bool rollback(const Pending& p, const std::string& notePath, std::string& error) {
    std::error_code ec;
    const fs::path target = fsPath(p.target), backup = fsPath(p.backup);
    if (!fs::exists(backup, ec)) {
        error = "the previous version (" + p.backup + ") is gone";
        fs::remove(fsPath(notePath), ec);
        return false;
    }
    if (fs::is_directory(backup, ec)) { // a Windows portable folder: the failed one is set aside first
        const fs::path failed = fsPath(p.target + ".failed");
        fs::remove_all(failed, ec);
        fs::rename(target, failed, ec);
        if (ec) {
            error = "can't move the new version aside: " + ec.message();
            return false;
        }
    }
    fs::rename(backup, target, ec);
    if (ec) {
        error = "can't put the previous version back: " + ec.message();
        return false;
    }
    fs::remove(fsPath(notePath), ec);
    return true;
}

StartCheck checkPendingAtStart(const std::string& notePath, const std::string& runningVersion, Pending& p,
                               std::string& error) {
    if (!readPending(notePath, p)) {
        std::error_code ec;
        fs::remove(fsPath(notePath), ec); // (none, or not ours)
        return StartCheck::Nothing;
    }
    if (p.version != runningVersion) {
        std::error_code ec;
        fs::remove(fsPath(notePath), ec);
        return StartCheck::Abandoned;
    }
    ++p.starts;
    if (p.starts > kMaxUnconfirmedStarts) {
        if (rollback(p, notePath, error)) return StartCheck::RolledBack;
        std::error_code ec;
        fs::remove(fsPath(notePath), ec); // (no endless retries)
        return StartCheck::Abandoned;
    }
    writePending(notePath, p);
    return StartCheck::Counting;
}

void confirmStarted(const std::string& notePath) {
    Pending p;
    std::error_code ec;
    if (readPending(notePath, p)) fs::remove_all(fsPath(p.backup), ec);
    fs::remove(fsPath(notePath), ec);
}

// --- Windows ---

namespace {

std::string psQuote(const std::string& s) { // a PowerShell literal: '...' with ' doubled
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("''") : std::string(1, c);
    return out + "'";
}

} // namespace

std::string windowsHelperScript(const Install& install, const Download& d, unsigned long pid, bool relaunch) {
    const bool portable = install.kind == InstallKind::WindowsPortable;
    std::string s;
    s += "# SculptForge update helper (written by the app; deletes itself)\n";
    s += "$ErrorActionPreference = 'Stop'\n";
    s += "$staged = " + psQuote(d.staged) + "\n";
    s += "$sha = " + psQuote(d.sha256) + "\n";
    s += "$root = " + psQuote(install.target) + "\n";
    s += "$exe = Join-Path $root 'bin\\sculptforge.exe'\n";
    s += "try { Wait-Process -Id " + std::to_string(pid) + " -Timeout 600 } catch {}\n";
    s += "Start-Sleep -Milliseconds 500\n";
    s += "if ((Get-FileHash -Algorithm SHA256 -LiteralPath $staged).Hash.ToLower() -ne $sha) {\n"
         "  Remove-Item -LiteralPath $staged -Force; exit 2 }\n";
    if (!portable) {
        // NSIS: /S silent, /D= the folder (last, unquoted). Windows asks for permission when the folder needs it.
        s += "$p = Start-Process -FilePath $staged -ArgumentList \"/S /D=$root\" -Wait -PassThru\n";
        s += "Remove-Item -LiteralPath $staged -Force -ErrorAction SilentlyContinue\n";
        s += "if ($p.ExitCode -ne 0) { exit 3 }\n";
        if (relaunch) s += "Start-Process -FilePath $exe\n";
        s += "Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\n";
        return s;
    }
    s += "$new = \"$root.new\"; $old = \"$root.old\"\n";
    s += "Remove-Item -LiteralPath $new -Recurse -Force -ErrorAction SilentlyContinue\n";
    s += "Expand-Archive -LiteralPath $staged -DestinationPath $new -Force\n";
    s += "$top = Get-ChildItem -LiteralPath $new -Directory | Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName "
         "'bin\\sculptforge.exe') } | Select-Object -First 1\n";
    s += "$src = if ($top) { $top.FullName } elseif (Test-Path -LiteralPath (Join-Path $new 'bin\\sculptforge.exe')) { $new } "
         "else { Remove-Item -LiteralPath $new -Recurse -Force; exit 4 }\n";
    s += "Remove-Item -LiteralPath $old -Recurse -Force -ErrorAction SilentlyContinue\n";
    // Files still in use (an Explorer window, an antivirus scan) make the first rename fail: a few tries.
    s += "$moved = $false\n";
    s += "for ($i = 0; $i -lt 20 -and -not $moved; $i++) {\n"
         "  try { Move-Item -LiteralPath $root -Destination $old; $moved = $true } catch { Start-Sleep -Milliseconds 500 } }\n";
    s += "if (-not $moved) { Remove-Item -LiteralPath $new -Recurse -Force; exit 5 }\n";
    s += "Move-Item -LiteralPath $src -Destination $root\n";
    s += "if ($src -ne $new) { Remove-Item -LiteralPath $new -Recurse -Force -ErrorAction SilentlyContinue }\n";
    if (relaunch) {
        // A new version that exits with an error within 20 seconds is rolled back and the old one started again.
        s += "$p = Start-Process -FilePath $exe -PassThru\n";
        s += "Start-Sleep -Seconds 20\n";
        s += "if ($p.HasExited -and $p.ExitCode -ne 0) {\n"
             "  Remove-Item -LiteralPath \"$root.failed\" -Recurse -Force -ErrorAction SilentlyContinue\n"
             "  Move-Item -LiteralPath $root -Destination \"$root.failed\"; Move-Item -LiteralPath $old -Destination $root\n"
             "  Start-Process -FilePath $exe }\n";
    }
    s += "Remove-Item -LiteralPath $PSCommandPath -Force -ErrorAction SilentlyContinue\n";
    return s;
}

} // namespace sf::update
