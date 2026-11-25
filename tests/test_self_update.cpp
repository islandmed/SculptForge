// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One-click update (src/core/SelfUpdate.cpp, src/core/Ed25519.cpp, ai::httpGetRestricted): only GitHub's release
// URLs, only a correctly signed SHA256SUMS.txt, only a package with its hash; the swap and the rollback. No network:
// the downloads are served by a stand-in for the HTTP client.
#include "TestHarness.h"

#include "ai/Http.h"
#include "core/Ed25519.h"
#include "core/Keychain.h"
#include "core/MemberBuilds.h"
#include "core/SelfUpdate.h"
#include "core/Sha256.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#if defined(__linux__)
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using namespace sf;
using namespace sf::update;
namespace fs = std::filesystem;

namespace {

std::string unhex(const std::string& h) {
    std::string out;
    for (size_t i = 0; i + 1 < h.size(); i += 2) out += char(std::stoi(h.substr(i, 2), nullptr, 16));
    return out;
}

std::string sha512Hex(const std::string& m) {
    uint8_t h[64];
    sha512(m.data(), m.size(), h);
    std::string out;
    for (uint8_t b : h) out += "0123456789abcdef"[b >> 4], out += "0123456789abcdef"[b & 15];
    return out;
}

bool verifyHex(const std::string& sig, const std::string& msg, const std::string& pub) {
    const std::string s = unhex(sig), k = unhex(pub);
    return ed25519Verify(reinterpret_cast<const uint8_t*>(s.data()), msg.data(), msg.size(),
                         reinterpret_cast<const uint8_t*>(k.data()));
}

// A throwaway key made for these tests with `openssl genpkey -algorithm ed25519` (its private half was deleted);
// the release key is a different one.
const char* kTestKey = "8fc8ddda8872fb72046720804a580631de32313bbfcd8bdeb6643b07f7f28f9b";
// `openssl pkeyutl -sign -rawin` over kSums with that key.
const char* kSumsSig = "7271a9be01ee009bf04ba28f25565ef8e8d330805b7815ffc191b562160f9c17"
                       "0262079ecbeb03c720243feb54d46cafbc2ace96af88d7dcc68afed8d9354b08";
const char* kPackageSha = "7176548d1467e12004d6e347ab751951139b1f34afd56bf9985d0edd26939ac8";
const std::string kSums = std::string(kPackageSha) + "  SculptForge-99.0.0-x86_64.AppImage\n" + kPackageSha +
                          "  SculptForge-linux-x86_64.AppImage\n" + kPackageSha + "  SculptForge-99.0.0-windows-x86_64.zip\n" +
                          std::string(64, '0') + "  SHA256SUMS-other.txt\n";

std::string package() { // what kPackageSha is the hash of (124000 bytes)
    std::string p;
    char line[64];
    for (int i = 0; i < 4000; ++i) {
        std::snprintf(line, sizeof line, "SculptForge test package %05d\n", i);
        p += line;
    }
    return p;
}

std::string readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

// Writes the whole file; a missing folder or a failed write is a failed check, never a silent no-op (11-24: a probe
// bundle's marker was never written and a tamper test passed vacuously).
void writeAll(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
    f.close();
    if (!f) std::fprintf(stderr, "writeAll: can't write %s\n", p.string().c_str());
    CHECK(f.good() && readAll(p) == s);
}

// Serves release files from a map, in 4 KiB pieces, like the network would; records every URL asked for.
struct FakeReleases {
    std::map<std::string, std::string> files; // file name -> body
    std::map<std::string, std::string> assetNames; // member asset id -> file name
    std::vector<std::string> asked;
    uint64_t claimTotal = 0; // a Content-Length that differs from the body (a cut-short download)
    Fetch fetch() {
        return [this](const std::string& url, const std::function<bool(const char*, size_t)>& onData,
                      const std::function<bool(uint64_t, uint64_t)>& onProgress) {
            asked.push_back(url);
            FetchResult r;
            if (!allowedUpdateUrl(url)) {
                r.error = "refused";
                return r;
            }
            std::string name = url.substr(url.rfind('/') + 1);
            if (member::isMemberAssetUrl(url)) name = assetNames.count(name) ? assetNames[name] : "";
            const auto it = files.find(name);
            if (it == files.end()) {
                r.status = 404;
                return r;
            }
            r.status = 200;
            const std::string& body = it->second;
            const bool pkg = it->first.find("AppImage") != std::string::npos || it->first.find(".dmg") != std::string::npos;
            const uint64_t total = claimTotal && pkg ? claimTotal : body.size();
            if (!onProgress(0, total)) return r;
            for (size_t i = 0; i < body.size(); i += 4096) {
                const size_t n = std::min<size_t>(4096, body.size() - i);
                if (!onData(body.data() + i, n) || !onProgress(i + n, total)) {
                    r.error = "stopped";
                    return r;
                }
            }
            return r;
        };
    }
};

struct Sandbox {
    fs::path dir;
    Install install;
    std::string note;
    explicit Sandbox(const char* name) {
        dir = tempPath(name);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "Apps");
        fs::create_directories(dir / "prefs");
        writeAll(dir / "Apps" / "SculptForge.AppImage", "old version");
        install.kind = InstallKind::AppImage;
        install.target = (dir / "Apps" / "SculptForge.AppImage").string();
        install.asset = "SculptForge-linux-x86_64.AppImage";
        note = (dir / "prefs" / "update_pending.json").string();
    }
    ~Sandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    size_t filesIn(const char* sub) const {
        size_t n = 0;
        for (const auto& e : fs::directory_iterator(dir / sub)) n += e.is_regular_file();
        return n;
    }
};

FakeReleases goodRelease() {
    FakeReleases f;
    f.files["SHA256SUMS.txt"] = kSums;
    f.files["SHA256SUMS.txt.sig"] = unhex(kSumsSig);
    f.files["SculptForge-99.0.0-x86_64.AppImage"] = package();
    return f;
}

void download(Sandbox& s, FakeReleases& f, Download& d, const char* key = kTestKey) {
    uint8_t k[32];
    CHECK(parseHexKey(key, k));
    downloadUpdate("99.0.0", s.install, k, f.fetch(), d);
    CHECK(d.done);
}

} // namespace

TEST(self_update_sha_vectors) {
    CHECK(sha512Hex("abc") == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                              "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    // Lengths around the block and padding edges (hashlib's digests).
    const std::map<size_t, std::string> expect = {
        {0, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
        {111, "3dfde1184fd99f233f98be4250f4edb9b535157909b668334370742204d97e047f1fd6a74bb5ba447f337286f421d9af957811f7ef62a458771457da126cb65e"},
        {112, "acc96c509e6d01787330a4c6a241e2cda9dcc2529dbe4288dbbcc3812133233c4698831127cf6ed0b333632b22715a5ce53a0a1002a684367b71c98aa6d1d900"},
        {128, "31f33a52b36dc2e70c83b604fa999a5cabf33bf70e4556fbed7bff10870c1b7b241dd3f15d1ade24599f068fc58ab51e0028b0f0c98895c23358e8dee032ce06"},
        {239, "d5e023834c68aeb6e8eef9619ed5fb6fd8b3df796b49d18dce5f6fae76f3f6cae8ddf055611c4031bf5fe6aa68708360f5de1fe69a77055eb55612c5509d5e19"},
        {1000, "76f1e766ac03deff8c614780dc1f26825ad762f92fbba09e2552cd70b2d6333816e7edec5d8820e8460601ea18d55b4fb154528db99a0bf17670f15f74c82cb7"}};
    for (const auto& [n, hex] : expect) {
        std::string m;
        for (size_t i = 0; i < n; ++i) m += char((i * 7 + 1) & 255);
        CHECK(sha512Hex(m) == hex);
        // The streaming SHA-256 agrees with the one-shot one, whatever the pieces.
        Sha256 h;
        for (size_t i = 0; i < n; i += 37) h.update(m.data() + i, std::min<size_t>(37, n - i));
        CHECK(h.hex() == sha256Hex(m));
    }
}

TEST(self_update_ed25519_rfc8032) {
    // RFC 8032 section 7.1, tests 1 to 3.
    CHECK(verifyHex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b",
                    "", "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
    CHECK(verifyHex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
                    unhex("72"), "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"));
    CHECK(verifyHex("6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a",
                    unhex("af82"), "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025"));
    // The wrong message, a changed signature, the wrong key.
    CHECK(!verifyHex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
                     unhex("73"), "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"));
    CHECK(!verifyHex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c01",
                     unhex("72"), "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"));
    CHECK(!verifyHex("92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00",
                     unhex("72"), "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
    // S at or above the group order is refused (RFC 8032 5.1.7), even when R would otherwise match: S + L.
    CHECK(!verifyHex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                     "4c8c7872aa064e049dbb3013fbf29380d25bf5f0595bbe24655141438e7a101b",
                     "", "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
    // The signed sums of the tests below.
    CHECK(verifyHex(kSumsSig, kSums, kTestKey));
}

TEST(self_update_urls) {
    CHECK(releaseFileUrl("0.12.0", "SHA256SUMS.txt") ==
          "https://github.com/islandmed/SculptForge/releases/download/v0.12.0/SHA256SUMS.txt");
    CHECK(releaseFileUrl("0.12.0-beta.1", "SculptForge-linux-x86_64.AppImage") ==
          "https://github.com/islandmed/SculptForge/releases/download/v0.12.0-beta.1/SculptForge-linux-x86_64.AppImage");
    for (const char* v : {"", "../0.12", "0.12/../../x", "0.12.0?x=1", "0.12.0#x", "v0.12", "0.12.0 ", "0.12..0", "0.12%2f"})
        CHECK(releaseFileUrl(v, "SHA256SUMS.txt").empty());
    CHECK(releaseFileUrl("0.12.0", "../SHA256SUMS.txt").empty());
    CHECK(releaseFileUrl("0.12.0", "a/b").empty());

    CHECK(allowedUpdateUrl("https://github.com/islandmed/SculptForge/releases/download/v0.12.0/SHA256SUMS.txt"));
    CHECK(allowedUpdateUrl("https://release-assets.githubusercontent.com/github-production-release-asset/1/2?sp=r&sig=x"));
    CHECK(allowedUpdateUrl("https://objects.githubusercontent.com/github-production-release-asset-2e65be/1/2?X-Amz=1"));
    for (const char* u : {
             "http://github.com/islandmed/SculptForge/releases/download/v0.12.0/SHA256SUMS.txt",      // not https
             "https://github.com.evil.example/islandmed/SculptForge/releases/download/v1/x",          // lookalike host
             "https://github.com@evil.example/islandmed/SculptForge/releases/download/v1/x",          // user info
             "https://github.com:8443/islandmed/SculptForge/releases/download/v1/x",                  // a port
             "https://github.com/islandmed/SculptForgeX/releases/download/v1/x",                      // another repo
             "https://github.com/someone/SculptForge/releases/download/v1/x",                         // a fork
             "https://github.com/islandmed/SculptForge/releases/download/v1/../../../evil/x",         // climbing out
             "https://github.com/islandmed/SculptForge/releases/download/v1/x?y",                     // a query
             "https://github.com/islandmed/SculptForge/releases/download/v1/a/b",                     // deeper
             "https://github.com/islandmed/SculptForge/releases/download/v1/%2e%2e",                  // escapes
             "https://github.com/islandmed/SculptForge/archive/main.zip",                             // not a release file
             "https://evil.githubusercontent.com/x",                                                  // another subdomain
             "https://objects.githubusercontent.com.evil.example/x",                                  //
             "https://raw.githubusercontent.com/islandmed/SculptForge/main/x",                        // anyone's files
             "https://sculptforge.app/SculptForge.AppImage",                                          //
             "file:///etc/passwd", ""})
        CHECK(!allowedUpdateUrl(u));
}

TEST(self_update_redirects_checked_every_hop) {
    const std::string start = "https://github.com/islandmed/SculptForge/releases/download/v1.0.0/SHA256SUMS.txt";
    std::vector<std::string> asked;
    auto server = [&](std::map<std::string, std::string> redirects) {
        return [&asked, redirects](const std::string& url, std::string& location) {
            asked.push_back(url);
            ai::HttpResponse r;
            const auto it = redirects.find(url);
            if (it != redirects.end()) {
                r.status = 302;
                location = it->second;
            } else {
                r.status = 200;
            }
            return r;
        };
    };
    // GitHub's own redirect is followed.
    ai::HttpResponse r = ai::followRedirects(start, allowedUpdateUrl,
                                             server({{start, "https://release-assets.githubusercontent.com/a?b=c"}}));
    CHECK(r.status == 200 && r.error.empty() && asked.size() == 2);
    // A redirect anywhere else is refused before it is requested (also plain http, and a second hop away).
    for (const char* evil : {"https://evil.example/x", "http://release-assets.githubusercontent.com/a",
                             "https://github.com/someone/SculptForge/releases/download/v1/x"}) {
        asked.clear();
        r = ai::followRedirects(start, allowedUpdateUrl, server({{start, evil}}));
        CHECK(r.error.find("refused") != std::string::npos && asked.size() == 1);
    }
    asked.clear();
    const std::string cdn = "https://release-assets.githubusercontent.com/a";
    r = ai::followRedirects(start, allowedUpdateUrl, server({{start, cdn}, {cdn, "https://evil.example/y"}}));
    CHECK(!r.error.empty() && asked.size() == 2);
    // A first URL that isn't allowed is never requested at all, and endless redirects end.
    asked.clear();
    r = ai::followRedirects("https://evil.example/SHA256SUMS.txt", allowedUpdateUrl, server({}));
    CHECK(!r.error.empty() && asked.empty());
    r = ai::followRedirects(start, allowedUpdateUrl, server({{start, start}}));
    CHECK(r.error == "too many redirects");
    // The real client refuses a disallowed URL without sending anything.
    const uint64_t before = ai::httpRequestCount();
    r = ai::httpGetRestricted("https://evil.example/x", allowedUpdateUrl, 5, [](const char*, size_t) { return true; });
    CHECK(!r.error.empty() && ai::httpRequestCount() == before);
}

TEST(self_update_sums) {
    CHECK(sumFor(kSums, "SculptForge-linux-x86_64.AppImage") == kPackageSha);
    // The versioned names are what's downloaded: an older release's signed sums can't pass for a newer one's.
    CHECK(versionedAsset("SculptForge-linux-x86_64.AppImage", "0.12.0") == "SculptForge-0.12.0-x86_64.AppImage");
    CHECK(versionedAsset("SculptForge-linux-arm64.AppImage", "0.12.0-beta.2") == "SculptForge-0.12.0-aarch64.AppImage");
    CHECK(versionedAsset("SculptForge-windows-x86_64-setup.exe", "1.0.0") == "SculptForge-1.0.0-windows-x86_64.exe");
    CHECK(versionedAsset("SculptForge-windows-x86_64-portable.zip", "1.0.0") == "SculptForge-1.0.0-windows-x86_64.zip");
    CHECK(versionedAsset("SculptForge-macos.dmg", "1.0.0") == "SculptForge-1.0.0-darwin-universal.dmg");
    CHECK(versionedAsset("SculptForge-macos.dmg", "1.0.0-beta.2") == "SculptForge-1.0.0-darwin-universal.dmg");
    CHECK(versionedAsset("SculptForge-macos.zip", "1.0.0").empty());
    CHECK(sumFor(kSums, "SculptForge-linux-arm64.AppImage").empty());
    CHECK(sumFor(kSums, "SHA256SUMS").empty()); // whole names only
    const std::string twice = kSums + std::string(64, 'a') + "  SculptForge-linux-x86_64.AppImage\n";
    CHECK(sumFor(twice, "SculptForge-linux-x86_64.AppImage").empty()); // listed twice: not trusted
    CHECK(sumFor(std::string(kPackageSha) + " *x.zip\r\n", "x.zip") == kPackageSha); // binary mode, CRLF
    CHECK(sumFor("zz" + std::string(62, '0') + "  x.zip\n", "x.zip").empty());
}

TEST(self_update_detects_installs) {
    std::vector<std::string> present;
    InstallProbe p;
    p.exists = [&](const std::string& f) { return std::find(present.begin(), present.end(), f) != present.end(); };
    p.os = "linux", p.arch = "x86_64", p.exePath = "/tmp/.mount_SculptXYZ/usr/bin/sculptforge";
    p.appImage = "/home/u/Apps/SculptForge.AppImage";
    present = {p.appImage};
    Install in = detectInstall(p);
    CHECK(in.kind == InstallKind::AppImage && in.target == p.appImage && in.asset == "SculptForge-linux-x86_64.AppImage");
    p.arch = "arm64";
    CHECK(detectInstall(p).asset == "SculptForge-linux-arm64.AppImage");
    p.flatpakId = "org.sculptforge.SculptForge";
    in = detectInstall(p);
    CHECK(in.kind == InstallKind::PackageManager && in.manager == "Flatpak" && in.command.empty()); // (a file install)
    p.exePath = "/app/extra/sculptforge/bin/sculptforge"; // FlatPark's package (the release tarball as extra data)
    in = detectInstall(p);
    CHECK(in.kind == InstallKind::PackageManager && in.manager == "FlatPark" &&
          in.command == "flatpak update org.sculptforge.SculptForge");
    p.exePath = "/app/bin/sculptforge"; // our .flatpak file
    CHECK(detectInstall(p).command.empty());
    p.flatpakId.clear(), p.appImage.clear(), p.exePath = "/usr/bin/sculptforge";
    present = {"/usr/bin/pacman"};
    in = detectInstall(p);
    CHECK(in.kind == InstallKind::PackageManager && in.manager == "pacman" && !in.command.empty());
    present = {"/usr/bin/dpkg"};
    in = detectInstall(p);
    CHECK(in.kind == InstallKind::PackageManager && in.command.empty()); // a downloaded .deb: download the next
    p.exePath = "/home/u/src/SculptForge/build/sculptforge"; // built from source
    CHECK(detectInstall(p).kind == InstallKind::None);

    p.os = "windows", p.arch = "x86_64", p.localAppData = "C:\\Users\\u\\AppData\\Local";
    p.exePath = "C:\\Program Files\\SculptForge\\bin\\sculptforge.exe";
    present = {(fs::path("C:\\Program Files\\SculptForge") / "Uninstall.exe").string()};
#if defined(_WIN32)
    in = detectInstall(p);
    CHECK(in.kind == InstallKind::WindowsInstaller && in.asset == "SculptForge-windows-x86_64-setup.exe");
    CHECK(stagingPath(in, "9.0.0").rfind("C:\\Users\\u\\AppData\\Local", 0) == 0);
    present.clear();
    in = detectInstall(p);
    CHECK(in.kind == InstallKind::WindowsPortable && in.asset == "SculptForge-windows-x86_64-portable.zip");
#endif
    p.os = "macos", p.exePath = "/Applications/SculptForge.app/Contents/MacOS/sculptforge", p.home = "/Users/u";
    present.clear();
    in = detectInstall(p);
    CHECK(in.kind == InstallKind::MacApp && in.target == "/Applications/SculptForge.app" && selfUpdates(in.kind, true));
    CHECK(in.asset == "SculptForge-macos.dmg");
#if !defined(_WIN32) // (the paths are joined with the host's separator; a Windows build never runs the Mac install)
    CHECK(in.stagingDir == "/Users/u/Library/Caches/SculptForge/updates");
    CHECK(stagingPath(in, "9.0.0") == "/Users/u/Library/Caches/SculptForge/updates/.download-9.0.0-SculptForge-macos.dmg");
    CHECK(mountPath(in, "9.0.0") == "/Users/u/Library/Caches/SculptForge/updates/mount-9.0.0");
    CHECK(appExecutable(in.target) == "/Applications/SculptForge.app/Contents/MacOS/sculptforge");
#endif
    CHECK(appExecutable("/home/u/Apps/SculptForge.AppImage") == "/home/u/Apps/SculptForge.AppImage");
    p.exePath = "/Users/u/Downloads/SculptForge.app/Contents/MacOS/sculptforge"; // anywhere the user can write
    CHECK(detectInstall(p).kind == InstallKind::MacApp && detectInstall(p).target == "/Users/u/Downloads/SculptForge.app");
    // Gatekeeper's read-only translocation copy, a build folder, no home folder: the Download link.
    p.exePath = "/private/var/folders/x/AppTranslocation/0A1B/d/SculptForge.app/Contents/MacOS/sculptforge";
    CHECK(detectInstall(p).kind == InstallKind::None);
    p.exePath = "/Users/u/src/SculptForge/build/sculptforge";
    CHECK(detectInstall(p).kind == InstallKind::None);
    p.exePath = "/Applications/SculptForge.app/Contents/MacOS/sculptforge", p.home.clear();
    CHECK(detectInstall(p).kind == InstallKind::None);
    p.home = "/Users/u";
    present = {"/opt/homebrew/Caskroom/sculptforge"};
    CHECK(detectInstall(p).command == "brew upgrade --cask sculptforge" && !selfUpdates(detectInstall(p).kind));
}

TEST(self_update_downloads_and_installs) {
    Sandbox s("sf_self_update_ok");
    FakeReleases f = goodRelease();
    Download d;
    download(s, f, d);
    CHECK(d.ok && d.error.empty() && d.sha256 == kPackageSha && d.received == package().size());
    CHECK(f.asked.size() == 3 && f.asked[2] == releaseFileUrl("99.0.0", "SculptForge-99.0.0-x86_64.AppImage"));
    CHECK(fs::path(d.staged).parent_path() == fs::path(s.install.target).parent_path()); // next to it: one rename
    CHECK(fs::path(d.staged).filename() == ".download-99.0.0-SculptForge-linux-x86_64.AppImage"); // (extension last)
    CHECK(readAll(s.install.target) == "old version"); // nothing in place yet
    std::string error;
    CHECK(installAppImage(s.install, d, "99.0.0", "1.0.0", s.note, error));
    CHECK(readAll(s.install.target) == package() && readAll(s.install.target + ".old") == "old version");
    CHECK(!fs::exists(d.staged));
#if !defined(_WIN32)
    CHECK((fs::status(s.install.target).permissions() & fs::perms::owner_exec) != fs::perms::none);
#endif
    // The new version's first start counts, and once it is up the backup goes.
    Pending p;
    CHECK(checkPendingAtStart(s.note, "99.0.0", p, error) == StartCheck::Counting && p.starts == 1 && p.from == "1.0.0");
    confirmStarted(s.note);
    CHECK(!fs::exists(s.note) && !fs::exists(s.install.target + ".old") && readAll(s.install.target) == package());
    CHECK(checkPendingAtStart(s.note, "99.0.0", p, error) == StartCheck::Nothing);
}

// The update replaces the AppImage in place: the file keeps the name it had (a copy downloaded as 0.11.1 and updated
// since stays SculptForge-0.11.1-x86_64.AppImage; Help > About shows the version), nothing else appears beside it,
// and versions are whole strings (0.11.10 is never 0.11.1). @ebarranco's 0.11.1 file ran 0.11.10.
TEST(self_update_appimage_names_and_versions) {
    const std::string x86 = "SculptForge-linux-x86_64.AppImage";
    CHECK(versionedAsset(x86, "0.11.10") == "SculptForge-0.11.10-x86_64.AppImage");
    CHECK(versionedAsset(x86, "0.11.11") == "SculptForge-0.11.11-x86_64.AppImage");
    CHECK(versionedAsset(x86, "0.10.0") == "SculptForge-0.10.0-x86_64.AppImage");
    CHECK(versionedAsset(x86, "0.11.11-beta.1") == "SculptForge-0.11.11-x86_64.AppImage");
    struct Step {
        const char *file, *from, *to;
    };
    for (const Step& st : {Step{"SculptForge-0.11.10-x86_64.AppImage", "0.11.10", "0.11.11"},
                           Step{"SculptForge-0.9.12-x86_64.AppImage", "0.9.12", "0.10.0"},
                           Step{"SculptForge-0.11.1-x86_64.AppImage", "0.11.1", "0.11.10"}}) {
        Sandbox s("sf_self_update_names");
        const fs::path target = s.dir / "Apps" / st.file;
        fs::remove(s.install.target);
        writeAll(target, std::string("old ") + st.from);
        s.install.target = target.string();
        const std::string staged = stagingPath(s.install, st.to), content = std::string("new ") + st.to;
        CHECK(fs::path(staged).filename() == std::string(".download-") + st.to + "-" + x86);
        writeAll(staged, content);
        Download d;
        d.staged = staged;
        d.sha256 = sha256Hex(content);
        std::string error;
        CHECK(installAppImage(s.install, d, st.to, st.from, s.note, error));
        CHECK(readAll(target) == content && readAll(s.install.target + ".old") == std::string("old ") + st.from);
        std::vector<std::string> names;
        for (const auto& e : fs::directory_iterator(s.dir / "Apps")) names.push_back(e.path().filename().string());
        std::sort(names.begin(), names.end());
        CHECK((names == std::vector<std::string>{st.file, std::string(st.file) + ".old"}));
        Pending p;
        CHECK(readPending(s.note, p) && p.version == st.to && p.from == st.from && p.target == s.install.target);
        CHECK(checkPendingAtStart(s.note, st.to, p, error) == StartCheck::Counting && p.starts == 1);
        confirmStarted(s.note);
        CHECK(!fs::exists(s.install.target + ".old") && readAll(target) == content);
    }
    // A note for 0.11.10 doesn't count a start of 0.11.1 (or the other way round): only the exact version.
    Sandbox s("sf_self_update_names_exact");
    CHECK(writePending(s.note, {"0.11.10", "0.11.1", s.install.target, s.install.target + ".old", 0}));
    Pending p;
    std::string error;
    CHECK(checkPendingAtStart(s.note, "0.11.1", p, error) == StartCheck::Abandoned);
}

TEST(self_update_refuses_bad_releases) {
    struct Case {
        const char* what;
        std::function<void(FakeReleases&)> spoil;
        const char* key;
    };
    const std::vector<Case> cases = {
        {"bad signature", [](FakeReleases& f) { f.files["SHA256SUMS.txt.sig"][5] ^= 1; }, kTestKey},
        {"sums changed after signing", [](FakeReleases& f) { f.files["SHA256SUMS.txt"][0] = 'f'; }, kTestKey},
        {"signed by another key", [](FakeReleases&) {}, "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"},
        {"no signature", [](FakeReleases& f) { f.files.erase("SHA256SUMS.txt.sig"); }, kTestKey},
        {"short signature", [](FakeReleases& f) { f.files["SHA256SUMS.txt.sig"].resize(63); }, kTestKey},
        {"bad hash", [](FakeReleases& f) { f.files["SculptForge-99.0.0-x86_64.AppImage"][1000] ^= 1; }, kTestKey},
        {"truncated", [](FakeReleases& f) { f.files["SculptForge-99.0.0-x86_64.AppImage"].resize(100000); }, kTestKey},
        {"cut short (Content-Length)", [](FakeReleases& f) { f.claimTotal = package().size() + 10; }, kTestKey},
        {"too large", [](FakeReleases& f) { f.claimTotal = kMaxPackageBytes + 1; }, kTestKey},
        {"huge sums", [](FakeReleases& f) { f.files["SHA256SUMS.txt"] += std::string(kMaxSumsBytes, ' '); }, kTestKey},
        {"an older release's sums", [](FakeReleases& f) { // signed, but naming 98.0.0's files
             f.files["SHA256SUMS.txt"] = std::string(kPackageSha) + "  SculptForge-98.0.0-x86_64.AppImage\n";
         }, kTestKey},
        {"package missing", [](FakeReleases& f) { f.files.erase("SculptForge-99.0.0-x86_64.AppImage"); }, kTestKey},
    };
    for (const Case& c : cases) {
        Sandbox s("sf_self_update_bad");
        FakeReleases f = goodRelease();
        c.spoil(f);
        Download d;
        download(s, f, d, c.key);
        if (d.ok) std::fprintf(stderr, "self_update_refuses_bad_releases: %s was accepted\n", c.what);
        CHECK(!d.ok && !d.error.empty() && d.staged.empty());
        // Nothing written in place, no temporary file left, nothing to install.
        CHECK(readAll(s.install.target) == "old version" && s.filesIn("Apps") == 1);
        std::string error;
        CHECK(!installAppImage(s.install, d, "99.0.0", "1.0.0", s.note, error) && !fs::exists(s.note));
        CHECK(readAll(s.install.target) == "old version");
    }
    // A bad signature stops it before the package is even requested.
    Sandbox s("sf_self_update_bad_sig");
    FakeReleases f = goodRelease();
    f.files["SHA256SUMS.txt.sig"][0] ^= 1;
    Download d;
    download(s, f, d);
    CHECK(f.asked.size() == 2);
}

// The release key compiled into the app (the maintainer's, 11-05): 64 hex characters, a point on the curve, and the verifier
// accepts what is signed under a key and only that key (the sign side with the throwaway test key: the release's
// private half never leaves the UPDATE_SIGNING_KEY secret).
TEST(self_update_release_key) {
    const std::string hex = kUpdatePublicKeyHex;
    CHECK(hex.size() == 64);
    CHECK(std::all_of(hex.begin(), hex.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }));
    uint8_t release[32], test[32];
    CHECK(parseHexKey(hex, release) && ed25519PublicKeyValid(release));
    CHECK(parseHexKey(kTestKey, test) && ed25519PublicKeyValid(test));
    const std::string sig = unhex(kSumsSig);
    CHECK(sumsSignatureValid(kSums, sig, test));     // signed with a key: accepted under it
    CHECK(!sumsSignatureValid(kSums, sig, release)); // and under no other, the release key included
    // A key that is no curve point is refused: y = 2 has no x on the curve ((y^2 - 1) / (d y^2 + 1) is no square).
    uint8_t bogus[32] = {2};
    CHECK(!ed25519PublicKeyValid(bogus) && !sumsSignatureValid(kSums, sig, bogus));
}

TEST(self_update_cancel_and_tamper) {
    {
        Sandbox s("sf_self_update_cancel");
        FakeReleases f = goodRelease();
        Download d;
        d.cancel = true;
        download(s, f, d);
        CHECK(!d.ok && d.error == "cancelled" && s.filesIn("Apps") == 1);
    }
    // The staged file is checked again when it is installed (it waited on disk until quitting).
    Sandbox s("sf_self_update_tamper");
    FakeReleases f = goodRelease();
    Download d;
    download(s, f, d);
    CHECK(d.ok);
    writeAll(d.staged, "something else");
    std::string error;
    CHECK(!installAppImage(s.install, d, "99.0.0", "1.0.0", s.note, error) && !error.empty());
    CHECK(readAll(s.install.target) == "old version" && !fs::exists(s.note) && s.filesIn("Apps") == 1);
}

TEST(self_update_rolls_back) {
    Sandbox s("sf_self_update_rollback");
    FakeReleases f = goodRelease();
    Download d;
    download(s, f, d);
    std::string error;
    CHECK(installAppImage(s.install, d, "99.0.0", "1.0.0", s.note, error));
    // The new version keeps dying before it is up: after kMaxUnconfirmedStarts tries the old one is put back.
    Pending p;
    for (int i = 1; i <= kMaxUnconfirmedStarts; ++i)
        CHECK(checkPendingAtStart(s.note, "99.0.0", p, error) == StartCheck::Counting && p.starts == i);
    CHECK(checkPendingAtStart(s.note, "99.0.0", p, error) == StartCheck::RolledBack);
    CHECK(readAll(s.install.target) == "old version" && !fs::exists(s.install.target + ".old") && !fs::exists(s.note));
    // An explicit rollback (the old app saw the new one exit at once) does the same.
    Download d2;
    download(s, f, d2);
    CHECK(installAppImage(s.install, d2, "99.0.0", "1.0.0", s.note, error));
    CHECK(readPending(s.note, p) && rollback(p, s.note, error));
    CHECK(readAll(s.install.target) == "old version" && !fs::exists(s.note));
    // The old version started instead (the user ran the .old file): the note is dropped, nothing is moved.
    Download d3;
    download(s, f, d3);
    CHECK(installAppImage(s.install, d3, "99.0.0", "1.0.0", s.note, error));
    CHECK(checkPendingAtStart(s.note, "1.0.0", p, error) == StartCheck::Abandoned && !fs::exists(s.note));
    CHECK(readAll(s.install.target) == package());
}

TEST(self_update_windows_helper) {
    Install in;
    in.kind = InstallKind::WindowsPortable;
    in.target = "C:\\Users\\o'brien\\SculptForge";
    in.asset = "SculptForge-windows-x86_64-portable.zip";
    Download d;
    d.staged = in.target + "\\.x.download-9.0.0";
    d.sha256 = kPackageSha;
    const std::string s = windowsHelperScript(in, d, 1234, true);
    CHECK(s.find("$root = 'C:\\Users\\o''brien\\SculptForge'") != std::string::npos); // quoted: no injection
    CHECK(s.find("Wait-Process -Id 1234") != std::string::npos);
    CHECK(s.find("Get-FileHash") < s.find("Expand-Archive")); // the hash is checked again before anything changes
    CHECK(s.find("Expand-Archive") < s.find("Move-Item -LiteralPath $root -Destination $old"));
    CHECK(s.find("$p.ExitCode -ne 0") != std::string::npos); // rollback when the new one fails
    // A portable copy's data/ folder comes along before the new version starts; a copy without the marker stays per-user.
    CHECK(s.find("Copy-Item -LiteralPath (Join-Path $oldBin 'data')") != std::string::npos);
    CHECK(s.find("Copy-Item -LiteralPath (Join-Path $oldBin 'data')") < s.find("Start-Process -FilePath $exe -PassThru"));
    CHECK(s.find("Remove-Item -LiteralPath (Join-Path $newBin 'portable.txt')") != std::string::npos);
    in.kind = InstallKind::WindowsInstaller;
    const std::string i = windowsHelperScript(in, d, 1234, false);
    CHECK(i.find("\"/S /D=$root\"") != std::string::npos && i.find("Get-FileHash") < i.find("Start-Process"));
    CHECK(i.find("Start-Process -FilePath $exe") == std::string::npos); // Later: no relaunch
    const std::string ir = windowsHelperScript(in, d, 1234, true);
    CHECK(ir.find("catch { $code = -1 }") < ir.find("Start-Process -FilePath $exe")); // declined prompt: old one again
}

// --- macOS (installMacApp: the system tools stood in for, the bundles are folders) ---

namespace {

// A second throwaway key (openssl genpkey -algorithm ed25519; its private half was deleted after signing the Mac
// release's sums below with pkeyutl -sign -rawin). The package bytes stand in for the disk image.
const char* kMacTestKey = "9944c473f10a5c5688509be3f74fff8d6da52ca81a92ac018129630e6cab2506";
const char* kMacSumsSig = "41ec81c4960ffe067a9f141bb39e6bca7bdce349b5a71c61a256807bdcd35720"
                          "5297c2d1764c149fc781e071b654e658f2a5d54afa204f571bc79f52c1176a07";

struct MacSandbox {
    fs::path dir;
    Install install;
    std::string note;
    explicit MacSandbox(const char* name) {
        dir = tempPath(name);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "Applications" / "SculptForge.app" / "Contents" / "MacOS");
        fs::create_directories(dir / "Caches");
        fs::create_directories(dir / "prefs");
        writeAll(dir / "Applications" / "SculptForge.app" / "Contents" / "MacOS" / "sculptforge", "old version");
        writeAll(dir / "Applications" / "SculptForge.app" / "Contents" / "Info.plist", "old plist");
        install.kind = InstallKind::MacApp;
        install.target = (dir / "Applications" / "SculptForge.app").string();
        install.asset = "SculptForge-macos.dmg";
        install.stagingDir = (dir / "Caches").string();
        note = (dir / "prefs" / "update_pending.json").string();
    }
    ~MacSandbox() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    std::string exe(const std::string& bundle = "") const {
        return readAll(fs::path(bundle.empty() ? install.target : bundle) / "Contents" / "MacOS" / "sculptforge");
    }
    std::vector<std::string> applications() const { // what Finder would show beside the app
        std::vector<std::string> names;
        for (const auto& e : fs::directory_iterator(dir / "Applications")) names.push_back(e.path().filename().string());
        std::sort(names.begin(), names.end());
        return names;
    }
    size_t cached() const {
        size_t n = 0;
        for (const auto& e : fs::directory_iterator(dir / "Caches")) (void)e, ++n;
        return n;
    }
};

// hdiutil, ditto and codesign as the install sees them: attach makes the mounted image's folder (with the bundles
// `inImage` names), ditto copies a folder, codesign answers `codesignRc`; every call is recorded.
struct FakeMacTools {
    std::vector<std::string> inImage = {"SculptForge.app"};
    int codesignRc = 0;
    bool signatureOk = true;
    std::string identifier = "app.sculptforge", runningIdentifier = "app.sculptforge";
    std::string teamId, runningTeamId;
    std::vector<std::vector<std::string>> calls;
    bool mounted = false;
    int attachRc = 0, dittoRc = 0, detachRc = 0;
    MacTools tools() {
        MacTools t;
        t.runningIdentifier = runningIdentifier;
        t.runningTeamId = runningTeamId;
        t.run = [this](const std::vector<std::string>& argv) {
            calls.push_back(argv);
            const std::string& tool = argv.at(0);
            if (tool == "/usr/bin/hdiutil" && argv.at(1) == "attach") {
                CHECK(std::find(argv.begin(), argv.end(), "-readonly") != argv.end());
                CHECK(std::find(argv.begin(), argv.end(), "-nobrowse") != argv.end());
                CHECK(std::find(argv.begin(), argv.end(), "-noverify") != argv.end()); // (the hash is the check)
                CHECK(fs::exists(argv.back())); // the image is there (hashed just before)
                if (attachRc) return attachRc;
                const fs::path mnt = argv[argv.size() - 2];
                for (const std::string& app : inImage) {
                    fs::create_directories(mnt / app / "Contents" / "MacOS");
                    writeAll(mnt / app / "Contents" / "MacOS" / "sculptforge", "new version");
                    writeAll(mnt / app / "Contents" / "Info.plist", "new plist");
                }
                mounted = true;
                return 0;
            }
            if (tool == "/usr/bin/hdiutil" && argv.at(1) == "detach") {
                CHECK(mounted);
                if (detachRc) return detachRc;
                mounted = false;
                return 0;
            }
            if (tool == "/usr/bin/ditto") {
                if (dittoRc) return dittoRc;
                std::error_code ec;
                fs::copy(argv.at(1), argv.at(2), fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                return ec ? 1 : 0;
            }
            if (tool == "/usr/bin/codesign") {
                CHECK(argv.size() == 5 && argv[1] == "--verify" && argv[2] == "--deep" && argv[3] == "--strict");
                CHECK(!mounted || detachRc); // the image is let go before the long checks (unless it refused to)
                CHECK(fs::exists(fs::path(argv[4]) / "Contents" / "MacOS" / "sculptforge"));
                return codesignRc;
            }
            CHECK(false);
            return -1;
        };
        t.signature = [this](const std::string& bundle, std::string& id, std::string& team, std::string& error) {
            CHECK(fs::exists(fs::path(bundle) / "Contents" / "Info.plist"));
            if (!signatureOk) {
                error = "the new app's code signature doesn't check out";
                return false;
            }
            id = identifier;
            team = teamId;
            return true;
        };
        return t;
    }
    std::vector<std::string> toolNames() const {
        std::vector<std::string> n;
        for (const auto& c : calls) n.push_back(c.at(0).substr(c.at(0).rfind('/') + 1) + (c.size() > 1 && c[0].find("hdiutil") != std::string::npos ? " " + c[1] : ""));
        return n;
    }
};

// The release as a Mac sees it: the sums list the versioned .dmg; signed with the test key.
FakeReleases macRelease(std::string& sumsOut) {
    FakeReleases f;
    sumsOut = std::string(kPackageSha) + "  SculptForge-99.0.0-darwin-universal.dmg\n" + kPackageSha + "  SculptForge-macos.dmg\n";
    f.files["SHA256SUMS.txt"] = sumsOut;
    f.files["SHA256SUMS.txt.sig"] = unhex(kMacSumsSig);
    f.files["SculptForge-99.0.0-darwin-universal.dmg"] = package();
    return f;
}

} // namespace

// The build switch (CMake SCULPTFORGE_MAC_SELF_UPDATE): OFF leaves a Mac with the Download link, every other install as it was.
TEST(self_update_mac_build_switch) {
    CHECK(selfUpdates(InstallKind::MacApp, true) && !selfUpdates(InstallKind::MacApp, false));
    CHECK(selfUpdates(InstallKind::AppImage, false) && selfUpdates(InstallKind::WindowsInstaller, false) &&
          selfUpdates(InstallKind::WindowsPortable, false));
    CHECK(!selfUpdates(InstallKind::PackageManager, true) && !selfUpdates(InstallKind::None, true));
    CHECK(selfUpdates(InstallKind::MacApp) == kMacSelfUpdate); // the default is what the build says
    if (kMacSelfUpdate) return;
    // Built OFF: the download step refuses a Mac bundle before asking for anything.
    MacSandbox s("sf_self_update_mac_off");
    std::string sums;
    FakeReleases f = macRelease(sums);
    uint8_t k[32];
    CHECK(parseHexKey(kMacTestKey, k));
    Download d;
    downloadUpdate("99.0.0", s.install, k, f.fetch(), d);
    CHECK(d.done && !d.ok && d.error == "this install can't update itself" && f.asked.empty() && s.cached() == 0);
}

TEST(self_update_mac_team_policy) {
    std::string why;
    CHECK(teamIdsAcceptable("ABCDE12345", "ABCDE12345", why) && why.empty());
    CHECK(teamIdsAcceptable("", "", why) && why.empty());  // ad-hoc on both sides: nothing to compare
    CHECK(teamIdsAcceptable("", "ABCDE12345", why));         // an ad-hoc app moving to a signed release
    CHECK(!teamIdsAcceptable("ABCDE12345", "ZZZZZ99999", why) && !why.empty());
    CHECK(why.find("ABCDE12345") == std::string::npos && why.find("ZZZZZ99999") == std::string::npos); // never printed
    CHECK(!teamIdsAcceptable("ABCDE12345", "", why) && !why.empty()); // a signed app never takes an unsigned one
    CHECK(why.find("ABCDE12345") == std::string::npos);
    // The signing identifier (the bundle id) must be the running app's own too.
    MacTools t;
    t.runningIdentifier = "app.sculptforge", t.runningTeamId = "ABCDE12345";
    CHECK(signerAcceptable(t, "app.sculptforge", "ABCDE12345", why));
    CHECK(!signerAcceptable(t, "app.other", "ABCDE12345", why) && why.find("app.other") == std::string::npos);
    CHECK(!signerAcceptable(t, "app.sculptforge", "ZZZZZ99999", why));
    t.runningIdentifier.clear(), t.runningTeamId.clear(); // an unsigned developer build: nothing to compare
    CHECK(signerAcceptable(t, "app.sculptforge", "", why));
}

TEST(self_update_mac_bundle_in_image_and_swap) {
    MacSandbox s("sf_self_update_mac_swap");
    const fs::path mnt = s.dir / "mnt";
    fs::create_directories(mnt / "SculptForge.app");
    fs::create_directories(mnt / ".background");
    writeAll(mnt / "Applications", "a symlink in a real image; here a file"); // (not a directory: ignored)
    CHECK(bundleInMount(mnt.string(), "SculptForge.app") == (mnt / "SculptForge.app").string());
    CHECK(bundleInMount(mnt.string(), "Other.app").empty());
    fs::create_directories(mnt / "Other.app");
    CHECK(bundleInMount(mnt.string(), "SculptForge.app").empty()); // two apps: not what we published
    CHECK(bundleInMount((s.dir / "nowhere").string(), "SculptForge.app").empty());
    // The swap: new in place, old as the backup, in whatever order the file system allows.
    const std::string fresh = s.install.target + ".new", backup = s.install.target + ".old";
    fs::create_directories(fs::path(fresh) / "Contents" / "MacOS");
    writeAll(fs::path(fresh) / "Contents" / "MacOS" / "sculptforge", "new version");
    fs::create_directories(backup); // a stale backup goes first
    std::string error;
    CHECK(swapDirectories(fresh, s.install.target, backup, error) && error.empty());
    CHECK(s.exe() == "new version" && s.exe(backup) == "old version" && !fs::exists(fresh));
    CHECK(s.applications() == std::vector<std::string>({"SculptForge.app", "SculptForge.app.old"}));
    CHECK(!swapDirectories(fresh, s.install.target, backup, error) && !error.empty()); // nothing to swap now
    CHECK(s.exe() == "new version");
}

TEST(self_update_mac_downloads_and_installs) {
    if (!kMacSelfUpdate) return; // (built out: self_update_mac_build_switch covers the refusal)
    MacSandbox s("sf_self_update_mac_ok");
    std::string sums;
    FakeReleases f = macRelease(sums);
    CHECK(verifyHex(kMacSumsSig, sums, kMacTestKey));
    uint8_t k[32];
    CHECK(parseHexKey(kMacTestKey, k));
    Download d;
    downloadUpdate("99.0.0", s.install, k, f.fetch(), d);
    CHECK(d.done && d.ok && d.error.empty() && d.sha256 == kPackageSha);
    CHECK(f.asked.size() == 3 && f.asked[2] == releaseFileUrl("99.0.0", "SculptForge-99.0.0-darwin-universal.dmg"));
    CHECK(fs::path(d.staged).parent_path() == s.dir / "Caches"); // our own cache folder, not /Applications
    CHECK(fs::path(d.staged).filename() == ".download-99.0.0-SculptForge-macos.dmg");
    CHECK(s.applications() == std::vector<std::string>({"SculptForge.app"}) && s.exe() == "old version");

    // Dry run: mounted, copied, checked, removed again; nothing replaced, no note, the download gone.
    FakeMacTools t;
    t.teamId = t.runningTeamId = "ABCDE12345";
    std::string error;
    CHECK(installMacApp(s.install, d, "99.0.0", "1.0.0", s.note, t.tools(), true, error) && error.empty());
    CHECK(t.toolNames() == std::vector<std::string>({"hdiutil attach", "ditto", "hdiutil detach", "codesign"}));
    CHECK(!t.mounted && s.exe() == "old version" && !fs::exists(s.note) && !fs::exists(d.staged) && s.cached() == 0);
    CHECK(s.applications() == std::vector<std::string>({"SculptForge.app"}));

    // The real thing.
    Download d2;
    downloadUpdate("99.0.0", s.install, k, f.fetch(), d2);
    CHECK(d2.ok);
    FakeMacTools t2;
    t2.teamId = t2.runningTeamId = "ABCDE12345";
    CHECK(installMacApp(s.install, d2, "99.0.0", "1.0.0", s.note, t2.tools(), false, error) && error.empty());
    CHECK(s.exe() == "new version" && s.exe(s.install.target + ".old") == "old version");
    CHECK(s.applications() == std::vector<std::string>({"SculptForge.app", "SculptForge.app.old"}));
    CHECK(!fs::exists(d2.staged) && s.cached() == 0 && !t2.mounted);
    Pending p;
    CHECK(readPending(s.note, p) && p.version == "99.0.0" && p.from == "1.0.0" && p.target == s.install.target);
    CHECK(checkPendingAtStart(s.note, "99.0.0", p, error) == StartCheck::Counting && p.starts == 1);
    confirmStarted(s.note);
    CHECK(!fs::exists(s.note) && s.applications() == std::vector<std::string>({"SculptForge.app"}) && s.exe() == "new version");

    // A new version that never gets going: the old bundle comes back and the failed one doesn't linger in Applications.
    Download d3;
    downloadUpdate("99.0.0", s.install, k, f.fetch(), d3);
    FakeMacTools t3;
    CHECK(installMacApp(s.install, d3, "99.0.0", "1.0.0", s.note, t3.tools(), false, error)); // (ad-hoc on both sides)
    writeAll(fs::path(s.install.target) / "Contents" / "MacOS" / "sculptforge", "99.0.0 that crashes");
    for (int i = 1; i <= kMaxUnconfirmedStarts; ++i) CHECK(checkPendingAtStart(s.note, "99.0.0", p, error) == StartCheck::Counting);
    CHECK(checkPendingAtStart(s.note, "99.0.0", p, error) == StartCheck::RolledBack);
    CHECK(s.exe() == "new version" && s.applications() == std::vector<std::string>({"SculptForge.app"}) && !fs::exists(s.note));

    // The image won't detach: the install still goes through, and its mount folder is left alone (never removed
    // through a mount), nothing else is left in the cache.
    Download d4;
    downloadUpdate("99.0.0", s.install, k, f.fetch(), d4);
    FakeMacTools t4;
    t4.detachRc = 1;
    CHECK(installMacApp(s.install, d4, "99.0.0", "1.0.0", s.note, t4.tools(), false, error) && t4.mounted);
    CHECK(fs::exists(mountPath(s.install, "99.0.0")) && s.cached() == 1 && !fs::exists(d4.staged));
    // A rollback whose target is already gone still brings the backup back.
    fs::remove_all(s.install.target);
    CHECK(readPending(s.note, p) && rollback(p, s.note, error) && s.exe() == "new version" && !fs::exists(s.note));
}

TEST(self_update_mac_refuses) {
    if (!kMacSelfUpdate) return;
    uint8_t k[32];
    CHECK(parseHexKey(kMacTestKey, k));
    // Before the install: the download's own checks, as on every platform.
    struct Case {
        const char* what;
        std::function<void(FakeReleases&, std::string&)> spoil;
    };
    const std::vector<Case> downloads = {
        {"tampered image", [](FakeReleases& f, std::string&) { f.files["SculptForge-99.0.0-darwin-universal.dmg"][2000] ^= 1; }},
        {"wrong size", [](FakeReleases& f, std::string&) { f.claimTotal = package().size() + 1; }},
        {"too large", [](FakeReleases& f, std::string&) { f.claimTotal = kMaxPackageBytes + 1; }},
        {"no checksum for the image", [](FakeReleases& f, std::string&) { f.files["SHA256SUMS.txt"] = std::string(kPackageSha) + "  SculptForge-99.0.0-x86_64.AppImage\n"; }},
        {"no checksum file", [](FakeReleases& f, std::string&) { f.files.erase("SHA256SUMS.txt"); }},
        {"sums changed", [](FakeReleases& f, std::string&) { f.files["SHA256SUMS.txt"][0] ^= 1; }},
        {"no signature", [](FakeReleases& f, std::string&) { f.files.erase("SHA256SUMS.txt.sig"); }},
    };
    for (const Case& c : downloads) {
        MacSandbox s("sf_self_update_mac_bad");
        std::string sums;
        FakeReleases f = macRelease(sums);
        c.spoil(f, sums);
        Download d;
        downloadUpdate("99.0.0", s.install, k, f.fetch(), d);
        if (d.ok) std::fprintf(stderr, "self_update_mac_refuses: %s was accepted\n", c.what);
        CHECK(!d.ok && !d.error.empty() && d.staged.empty() && s.cached() == 0);
        FakeMacTools t;
        std::string error;
        CHECK(!installMacApp(s.install, d, "99.0.0", "1.0.0", s.note, t.tools(), false, error) && t.calls.empty());
        CHECK(s.exe() == "old version" && !fs::exists(s.note));
    }
    // A redirect off GitHub's hosts is refused before it is requested (the same rule as every platform's download).
    CHECK(!allowedUpdateUrl("https://evil.example/SculptForge-99.0.0-darwin-universal.dmg"));
    CHECK(!allowedUpdateUrl("https://github.com/someone/SculptForge/releases/download/v99.0.0/SculptForge-99.0.0-darwin-universal.dmg"));
    CHECK(allowedUpdateUrl(releaseFileUrl("99.0.0", "SculptForge-99.0.0-darwin-universal.dmg")));

    // At install time: the copy out of the image must pass every check, or Applications stays as it was and the
    // download goes. The image is always detached.
    struct Install_ {
        const char* what;
        std::function<void(FakeMacTools&)> spoil;
        std::function<void(MacSandbox&, Download&)> before;
    };
    const std::vector<Install_> installs = {
        {"codesign --verify --deep --strict fails", [](FakeMacTools& t) { t.codesignRc = 1; }, {}},
        {"the Security framework refuses the signature", [](FakeMacTools& t) { t.signatureOk = false; }, {}},
        {"another developer's team", [](FakeMacTools& t) { t.runningTeamId = "ABCDE12345", t.teamId = "ZZZZZ99999"; }, {}},
        {"a signed app offered an unsigned one", [](FakeMacTools& t) { t.runningTeamId = "ABCDE12345", t.teamId = ""; }, {}},
        {"another bundle identifier", [](FakeMacTools& t) { t.identifier = "app.other"; }, {}},
        {"no SculptForge.app in the image", [](FakeMacTools& t) { t.inImage = {"Other.app"}; }, {}},
        {"two apps in the image", [](FakeMacTools& t) { t.inImage = {"SculptForge.app", "Other.app"}; }, {}},
        {"the image won't mount", [](FakeMacTools& t) { t.attachRc = 1; }, {}},
        {"the copy fails", [](FakeMacTools& t) { t.dittoRc = 1; }, {}},
        {"the download changed on disk", [](FakeMacTools&) {}, [](MacSandbox&, Download& d) { writeAll(d.staged, "something else"); }},
    };
    for (const Install_& c : installs) {
        MacSandbox s("sf_self_update_mac_bad_install");
        std::string sums;
        FakeReleases f = macRelease(sums);
        Download d;
        downloadUpdate("99.0.0", s.install, k, f.fetch(), d);
        CHECK(d.ok);
        if (c.before) c.before(s, d);
        FakeMacTools t;
        c.spoil(t);
        std::string error;
        const bool ok = installMacApp(s.install, d, "99.0.0", "1.0.0", s.note, t.tools(), false, error);
        if (ok) std::fprintf(stderr, "self_update_mac_refuses: install with %s was accepted\n", c.what);
        CHECK(!ok && !error.empty());
        CHECK(error.find("ABCDE12345") == std::string::npos && error.find("ZZZZZ99999") == std::string::npos &&
              error.find("app.other") == std::string::npos);
        CHECK(s.exe() == "old version" && s.applications() == std::vector<std::string>({"SculptForge.app"}));
        CHECK(!fs::exists(s.note) && !fs::exists(d.staged) && s.cached() == 0 && !t.mounted);
        // Nothing is swapped before codesign has passed: a swap shows as a backup, and there is none.
    }
}

#if defined(__APPLE__)
namespace {

// The real tools, as the app runs them: by absolute path, no shell, stdin closed; the exit code.
int spawnTool(const std::vector<std::string>& argv) {
    std::vector<char*> av;
    for (const std::string& a : argv) av.push_back(const_cast<char*>(a.c_str()));
    av.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, av[0], &actions, nullptr, av.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// An ad-hoc signed probe bundle (any Mach-O inside), as a Mac release's app is signed today.
fs::path makeProbeBundle(const fs::path& app, const std::string& marker) {
    std::error_code ec;
    fs::create_directories(app / "Contents" / "MacOS");
    fs::create_directories(app / "Contents" / "Resources"); // (writeAll makes no folders: 11-24 the marker was never written)
    writeAll(app / "Contents" / "Info.plist",
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\"><dict>"
             "<key>CFBundleIdentifier</key><string>app.sculptforge.probe</string>"
             "<key>CFBundleExecutable</key><string>sculptforge</string>"
             "<key>CFBundlePackageType</key><string>APPL</string></dict></plist>\n");
    fs::copy_file("/bin/ls", app / "Contents" / "MacOS" / "sculptforge", fs::copy_options::overwrite_existing, ec);
    writeAll(app / "Contents" / "Resources" / "marker.txt", marker); // (a resource: sealed by the signature too)
    CHECK(!ec);
    CHECK(readAll(app / "Contents" / "Resources" / "marker.txt") == marker);
    CHECK(spawnTool({"/usr/bin/codesign", "--force", "--sign", "-", app.string()}) == 0);
    return app;
}

} // namespace

// On a Mac the real checks run against a bundle made here: ad-hoc signed by codesign, it passes; one file changed
// afterwards, it fails. The running test binary's identity is read without error.
TEST(self_update_mac_real_codesign) {
    const fs::path dir = tempPath("sf_self_update_mac_codesign");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const fs::path app = makeProbeBundle(dir / "Probe.app", "probe");
    std::string id, team, error;
    CHECK(macBundleSignature(app.string(), id, team, error));
    CHECK(id == "app.sculptforge.probe" && team.empty() && error.empty()); // ad-hoc: the bundle id, no team identifier
    CHECK(runningCodeIdentity(id, team) && !id.empty());                   // this binary: whatever it is signed with
    // A resource changed after signing (the seal covers Resources: the Python runtime lives there in a real build).
    writeAll(app / "Contents" / "Resources" / "marker.txt", "changed after signing");
    CHECK(!macBundleSignature(app.string(), id, team, error) && !error.empty());
    if (error.empty()) std::fprintf(stderr, "self_update_mac_real_codesign: a changed resource was accepted\n");
    fs::remove_all(dir, ec);
}

// The bar (cto 11-24): a changed resource, a changed executable, a removed _CodeSignature, an added file and a changed
// Info.plist are each refused by BOTH checks the install runs, codesign --verify --deep --strict and the Security
// framework, on a fresh ditto copy of the signed bundle each time (as the install copies it); the untouched copy passes both.
TEST(self_update_mac_real_tampers) {
    const fs::path dir = tempPath("sf_self_update_mac_tampers");
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    const fs::path sealed = makeProbeBundle(dir / "Sealed.app", "probe");
    struct Tamper {
        const char* what;
        std::function<void(const fs::path& app)> apply;
        std::function<bool(const fs::path& app)> tookEffect; // asserted before the checks: a tamper that changed nothing is a red test
    };
    const auto exe = [](const fs::path& app) { return app / "Contents" / "MacOS" / "sculptforge"; };
    const auto marker = [](const fs::path& app) { return app / "Contents" / "Resources" / "marker.txt"; };
    const std::vector<Tamper> tampers = {
        {"untouched", [](const fs::path&) {}, [&](const fs::path& app) { return readAll(marker(app)) == "probe"; }},
        {"a resource changed", [&](const fs::path& app) { writeAll(marker(app), "changed"); },
         [&](const fs::path& app) { return readAll(marker(app)) == "changed" && readAll(marker(sealed)) == "probe"; }},
        {"the executable changed", [&](const fs::path& app) { std::ofstream(exe(app), std::ios::binary | std::ios::app) << "x"; },
         [&](const fs::path& app) { return readAll(exe(app)) == readAll(exe(sealed)) + "x"; }},
        {"_CodeSignature removed", [](const fs::path& app) { std::error_code e; fs::remove_all(app / "Contents" / "_CodeSignature", e); },
         [&](const fs::path& app) { return !fs::exists(app / "Contents" / "_CodeSignature") && fs::exists(sealed / "Contents" / "_CodeSignature"); }},
        {"a file added", [](const fs::path& app) { writeAll(app / "Contents" / "Resources" / "extra.txt", "added after signing"); },
         [&](const fs::path& app) { return fs::exists(app / "Contents" / "Resources" / "extra.txt") && !fs::exists(sealed / "Contents" / "Resources" / "extra.txt"); }},
        {"Info.plist changed", [](const fs::path& app) {
             std::string plist = readAll(app / "Contents" / "Info.plist");
             plist.replace(plist.find("app.sculptforge.probe"), 21, "app.sculptforge.probf");
             writeAll(app / "Contents" / "Info.plist", plist);
         }, [&](const fs::path& app) {
             const std::string plist = readAll(app / "Contents" / "Info.plist");
             return plist.find("app.sculptforge.probf") != std::string::npos && plist != readAll(sealed / "Contents" / "Info.plist");
         }},
    };
    for (const Tamper& t : tampers) {
        const fs::path copy = dir / "Copy.app";
        fs::remove_all(copy, ec);
        CHECK(spawnTool({"/usr/bin/ditto", sealed.string(), copy.string()}) == 0);
        t.apply(copy);
        if (!t.tookEffect(copy)) std::fprintf(stderr, "self_update_mac_real_tampers: %s changed nothing\n", t.what);
        CHECK(t.tookEffect(copy));
        const bool cli = spawnTool({"/usr/bin/codesign", "--verify", "--deep", "--strict", copy.string()}) == 0;
        std::string id, team, error;
        const bool api = macBundleSignature(copy.string(), id, team, error);
        const bool expect = std::string(t.what) == "untouched";
        if (cli != expect) std::fprintf(stderr, "self_update_mac_real_tampers: codesign --verify %s with %s\n", cli ? "passed" : "failed", t.what);
        if (api != expect) std::fprintf(stderr, "self_update_mac_real_tampers: macBundleSignature %s with %s\n", api ? "passed" : "failed", t.what);
        CHECK(cli == expect);
        CHECK(api == expect && (expect ? error.empty() && id == "app.sculptforge.probe" : !error.empty()));
    }
    fs::remove_all(dir, ec);
}

// End to end with the real tools (hdiutil, ditto, codesign, the Security framework, renamex_np): a disk image made
// here holding a signed probe bundle replaces an "installed" one; dry run first, then the install, confirm, rollback.
TEST(self_update_mac_real_install) {
    MacSandbox s("sf_self_update_mac_real");
    std::error_code ec;
    // The "installed" app is a signed bundle too; the image holds the new one.
    fs::remove_all(s.install.target, ec);
    makeProbeBundle(s.install.target, "old version");
    const fs::path src = s.dir / "image-src";
    makeProbeBundle(src / "SculptForge.app", "new version");
    const fs::path dmg = s.dir / "SculptForge-99.0.0-darwin-universal.dmg";
    CHECK(spawnTool({"/usr/bin/hdiutil", "create", "-quiet", "-srcfolder", src.string(), "-volname", "SculptForge", "-format",
                     "UDZO", dmg.string()}) == 0);
    // Staged as the download would be, with its hash.
    Download d;
    d.staged = stagingPath(s.install, "99.0.0");
    fs::copy_file(dmg, d.staged, ec);
    CHECK(!ec);
    d.sha256 = sha256Hex(readAll(d.staged));
    d.ok = true;
    MacTools tools;
    tools.run = spawnTool;
    tools.signature = macBundleSignature;
    tools.runningIdentifier = "app.sculptforge.probe"; // (what the installed probe is signed as; this test binary isn't)
    const auto marker = [&](const std::string& bundle) { return readAll(fs::path(bundle) / "Contents" / "Resources" / "marker.txt"); };
    std::string error;
    CHECK(installMacApp(s.install, d, "99.0.0", "1.0.0", s.note, tools, true, error) && error.empty()); // dry run
    CHECK(marker(s.install.target) == "old version" && s.applications() == std::vector<std::string>({"SculptForge.app"}));
    CHECK(!fs::exists(d.staged) && s.cached() == 0 && !fs::exists(s.note));
    fs::copy_file(dmg, d.staged, ec);
    CHECK(installMacApp(s.install, d, "99.0.0", "1.0.0", s.note, tools, false, error) && error.empty());
    CHECK(marker(s.install.target) == "new version" && marker(s.install.target + ".old") == "old version");
    CHECK(s.applications() == std::vector<std::string>({"SculptForge.app", "SculptForge.app.old"}) && s.cached() == 0);
    std::string id, team;
    CHECK(macBundleSignature(s.install.target, id, team, error) && id == "app.sculptforge.probe"); // the copy is intact
    Pending p;
    CHECK(checkPendingAtStart(s.note, "99.0.0", p, error) == StartCheck::Counting);
    CHECK(readPending(s.note, p) && rollback(p, s.note, error));
    CHECK(marker(s.install.target) == "old version" && s.applications() == std::vector<std::string>({"SculptForge.app"}));
    // A bundle changed after signing (its executable, here) is refused by the real codesign: the install leaves
    // everything as it was.
    const fs::path srcExe = src / "SculptForge.app" / "Contents" / "MacOS" / "sculptforge";
    const std::string sealedExe = readAll(srcExe);
    std::ofstream(srcExe, std::ios::binary | std::ios::app) << "tampered";
    CHECK(readAll(srcExe) == sealedExe + "tampered");
    const fs::path bad = s.dir / "bad.dmg";
    CHECK(spawnTool({"/usr/bin/hdiutil", "create", "-quiet", "-srcfolder", src.string(), "-volname", "SculptForge", "-format",
                     "UDZO", bad.string()}) == 0);
    fs::copy_file(bad, d.staged, fs::copy_options::overwrite_existing, ec);
    d.sha256 = sha256Hex(readAll(d.staged));
    CHECK(!installMacApp(s.install, d, "99.0.0", "1.0.0", s.note, tools, false, error) && !error.empty());
    CHECK(marker(s.install.target) == "old version" && s.applications() == std::vector<std::string>({"SculptForge.app"}));
    CHECK(!fs::exists(d.staged) && s.cached() == 0 && !fs::exists(s.note));
}
#endif

// --- Member builds (core/MemberBuilds.cpp) ---

TEST(member_builds_urls_and_headers) {
    using namespace sf::member;
    CHECK(isMemberApiUrl("https://api.github.com/repos/sculptforge/sculptforge-insiders/releases?per_page=20"));
    CHECK(isMemberApiUrl("https://api.github.com/user"));
    CHECK(isMemberAssetUrl("https://api.github.com/repos/sculptforge/sculptforge-insiders/releases/assets/123456"));
    for (const char* u : {"https://api.github.com/repos/sculptforge/sculptforge-insiders/releases/assets/12a",
                          "https://api.github.com/repos/sculptforge/sculptforge-insiders/releases/assets/",
                          "https://api.github.com/repos/sculptforge/sculptforge-insiders/releases/assets/1/../../x",
                          "https://api.github.com/repos/islandmed/SculptForge/releases?per_page=20",
                          "https://api.github.com/repos/sculptforge/sculptforge-insiders/contents/secrets",
                          "https://api.github.com/user/repos", "https://api.github.com.evil.example/user",
                          "http://api.github.com/user"})
        CHECK(!isMemberApiUrl(u) && !allowedUpdateUrl(u));
    // The token goes to the member API only: never to the asset host, the public release URLs or anywhere else.
    const auto h = headersFor("https://api.github.com/repos/sculptforge/sculptforge-insiders/releases/assets/5", "ghu_x");
    CHECK(h.size() == 3 && h[0] == "Authorization: Bearer ghu_x" && h[1] == "Accept: application/octet-stream");
    CHECK(headersFor(kReleasesUrl, "ghu_x")[1] == "Accept: application/vnd.github+json");
    for (const char* u : {"https://release-assets.githubusercontent.com/a?sig=1", "https://objects.githubusercontent.com/a",
                          "https://github.com/islandmed/SculptForge/releases/download/v1.0.0/SHA256SUMS.txt",
                          "https://evil.example/x"})
        CHECK(headersFor(u, "ghu_x").empty());
    // A redirect from the member API to the asset host is followed, without the token (headersFor per hop).
    std::vector<std::vector<std::string>> sent;
    const std::string asset = std::string(kAssetUrlPrefix) + "77";
    const ai::HttpResponse r = ai::followRedirects(asset, allowedUpdateUrl, [&](const std::string& url, std::string& loc) {
        sent.push_back(headersFor(url, "ghu_x"));
        ai::HttpResponse a;
        if (url == asset) a.status = 302, loc = "https://release-assets.githubusercontent.com/x?sig=1";
        else a.status = 200;
        return a;
    });
    CHECK(r.status == 200 && sent.size() == 2 && sent[0].size() == 3 && sent[1].empty());
}

TEST(member_builds_device_flow_answers) {
    using namespace sf::member;
    DeviceCode c;
    std::string err;
    CHECK(parseDeviceCode(R"({"device_code":"3584d83530557fdd1f46af8289938c8ef79f9dc5","user_code":"WDJB-MJHT",
        "verification_uri":"https://github.com/login/device","expires_in":900,"interval":5})", 1000, c, err));
    CHECK(c.userCode == "WDJB-MJHT" && c.interval == 5 && c.expiresAt == 1900);
    // Another page to open than GitHub's own: refused (the app opens kVerifyUrl only anyway).
    CHECK(!parseDeviceCode(R"({"device_code":"abc","user_code":"WDJB-MJHT","verification_uri":"https://evil.example/login/device"})",
                           0, c, err));
    CHECK(!parseDeviceCode(R"({"error":"device_flow_disabled"})", 0, c, err) && err.find("switched off") != std::string::npos);
    CHECK(pollBody("a b") == "client_id=Iv23liK7goArrLDUGKEk&device_code=a%20b&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code");
    CHECK(refreshBody("ghr_1").find("grant_type=refresh_token&refresh_token=ghr_1") != std::string::npos);
    CHECK(refreshBody("x").find("client_secret") == std::string::npos); // (a device-flow app has none)

    Tokens t;
    CHECK(parseTokenReply(R"({"error":"authorization_pending"})", 0, t, err) == TokenReply::Pending);
    CHECK(parseTokenReply(R"({"error":"slow_down","interval":10})", 0, t, err) == TokenReply::SlowDown);
    CHECK(parseTokenReply(R"({"error":"expired_token"})", 0, t, err) == TokenReply::Expired);
    CHECK(parseTokenReply(R"({"error":"bad_refresh_token"})", 0, t, err) == TokenReply::Expired);
    CHECK(parseTokenReply(R"({"error":"access_denied"})", 0, t, err) == TokenReply::Denied);
    CHECK(parseTokenReply("<html>", 0, t, err) == TokenReply::Error && err.find("ghu_") == std::string::npos);
    CHECK(parseTokenReply(R"({"access_token":"","token_type":"bearer"})", 0, t, err) == TokenReply::Error);
    CHECK(parseTokenReply(R"({"access_token":"ghu_abc","expires_in":28800,"refresh_token":"ghr_def",
        "refresh_token_expires_in":15897600,"token_type":"bearer","scope":""})", 100, t, err) == TokenReply::Ok);
    CHECK(t.access == "ghu_abc" && t.refresh == "ghr_def" && t.accessExpiresAt == 28900 && t.refreshExpiresAt == 15897700);
    CHECK(t.usable(100) && !t.usable(28890) && t.refreshable(28890) && !t.refreshable(15897700));
    Tokens back;
    CHECK(tokensFromSecret(tokensToSecret(t), back) && back.access == t.access && back.refresh == t.refresh &&
          back.accessExpiresAt == t.accessExpiresAt && back.refreshExpiresAt == t.refreshExpiresAt);
    CHECK(!tokensFromSecret("{}", back) && !tokensFromSecret("not json", back));
    CHECK(parseLogin(R"({"login":"some-member","id":1})") == "some-member");
    CHECK(parseLogin(R"({"login":"<script>"})").empty());
}

// The whole sign-in against a scripted GitHub: every outcome, and the polling loop ends in each of them.
namespace {
struct FakeGitHub {
    std::vector<std::string> tokenReplies;          // answers to the polls, in order (the last repeats)
    long releasesStatus = 200, userStatus = 200, inviteStatus = 200;
    std::string invitations = "[]";
    int64_t clock = 1000;
    std::vector<int> waits;                         // each wait (ms)
    int cancelAfterWaits = -1;
    int polls = 0, inviteCalls = 0;
    std::string seenCode;
    sf::member::SignInIo io() {
        using namespace sf::member;
        SignInIo o;
        o.post = [this](const std::string& url, const std::string&) {
            Reply r;
            r.status = 200;
            if (url == kDeviceCodeUrl) {
                r.body = R"({"device_code":"dc1","user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device","expires_in":900,"interval":5})";
            } else {
                r.body = tokenReplies[std::min<size_t>(polls, tokenReplies.size() - 1)];
                ++polls;
            }
            return r;
        };
        o.get = [this](const std::string& url, const std::string& token, size_t) {
            Reply r;
            if (token != "ghu_tok") { r.status = 401; return r; }
            if (url == kUserUrl) r.status = userStatus, r.body = R"({"login":"hameron"})";
            else if (url == kReleasesUrl) r.status = releasesStatus, r.body = "[]";
            else if (url == kInvitationsApiUrl) ++inviteCalls, r.status = inviteStatus, r.body = invitations;
            else r.status = 599;
            return r;
        };
        o.wait = [this](int ms) {
            waits.push_back(ms);
            clock += ms / 1000;
            return cancelAfterWaits < 0 || int(waits.size()) <= cancelAfterWaits;
        };
        o.now = [this] { return clock; };
        o.showCode = [this](const DeviceCode& c) { seenCode = c.userCode; };
        return o;
    }
};
const char* kTokenOk = R"({"access_token":"ghu_tok","token_type":"bearer","expires_in":28800,"refresh_token":"ghr_x"})";
const char* kPending = R"({"error":"authorization_pending"})";
} // namespace

TEST(member_sign_in_outcomes) {
    using namespace sf::member;
    { // signed in, on the member list
        FakeGitHub g;
        g.tokenReplies = {kPending, kPending, kTokenOk};
        const SignInOutcome o = runSignIn(g.io());
        CHECK(o.result == SignInResult::Member && o.login == "hameron" && o.tokens.access == "ghu_tok" && o.polls == 3);
        CHECK(g.seenCode == "WDJB-MJHT" && signInMessage(o).empty() && g.inviteCalls == 0);
    }
    { // signed in, not a collaborator, nothing pending: says so, with the login and what to do
        FakeGitHub g;
        g.tokenReplies = {kTokenOk};
        g.releasesStatus = 404;
        const SignInOutcome o = runSignIn(g.io());
        const std::string m = signInMessage(o);
        CHECK(o.result == SignInResult::NoAccess && o.login == "hameron" && o.tokens.access.empty());
        CHECK(m == "Signed in as hameron, but this account isn't on the member list yet. Check the invitation email from "
                   "GitHub, or tell us your GitHub name in #members.");
    }
    { // 403 is the same; an invitations answer that fails or lists other repositories changes nothing
        FakeGitHub g;
        g.tokenReplies = {kTokenOk};
        g.releasesStatus = 403;
        g.inviteStatus = 404;
        CHECK(runSignIn(g.io()).result == SignInResult::NoAccess);
        g.inviteStatus = 200;
        g.invitations = R"([{"repository":{"full_name":"someone/else"}}, 5, {"repository":null}])";
        CHECK(runSignIn(g.io()).result == SignInResult::NoAccess);
    }
    { // an invitation to the member repository is waiting
        FakeGitHub g;
        g.tokenReplies = {kTokenOk};
        g.releasesStatus = 404;
        g.invitations = R"([{"id":1,"repository":{"full_name":"sculptforge/sculptforge-insiders"}}])";
        const SignInOutcome o = runSignIn(g.io());
        CHECK(o.result == SignInResult::InvitePending && signInMessage(o).find("hameron") != std::string::npos &&
              signInMessage(o).find("isn't accepted yet") != std::string::npos);
        CHECK(std::string(kInvitationsPage) == "https://github.com/orgs/sculptforge/invitation");
    }
    { // the login is unknown (the user call failed): the message still reads
        FakeGitHub g;
        g.tokenReplies = {kTokenOk};
        g.releasesStatus = 404;
        g.userStatus = 500;
        const std::string m = signInMessage(runSignIn(g.io()));
        CHECK(m.rfind("Signed in, but this account isn't on the member list yet.", 0) == 0);
    }
    { // GitHub itself failing is a failure with a retry hint, not "no access"
        FakeGitHub g;
        g.tokenReplies = {kTokenOk};
        g.releasesStatus = 502;
        const SignInOutcome o = runSignIn(g.io());
        CHECK(o.result == SignInResult::Failed && o.error == "GitHub answered 502" && signInMessage(o).find("try again") != std::string::npos);
    }
    { // declined on GitHub
        FakeGitHub g;
        g.tokenReplies = {kPending, R"({"error":"access_denied"})"};
        const SignInOutcome o = runSignIn(g.io());
        CHECK(o.result == SignInResult::Declined && o.polls == 2 && signInMessage(o).find("try again") != std::string::npos);
    }
    { // GitHub says the code is expired
        FakeGitHub g;
        g.tokenReplies = {R"({"error":"expired_token"})"};
        const SignInOutcome o = runSignIn(g.io());
        CHECK(o.result == SignInResult::Expired && signInMessage(o).find("ran out") != std::string::npos);
    }
    { // nobody ever enters the code: the loop stops at the code's own expiry (900 s at 5 s = under 200 polls)
        FakeGitHub g;
        g.tokenReplies = {kPending};
        const SignInOutcome o = runSignIn(g.io());
        CHECK(o.result == SignInResult::Expired && o.polls > 100 && o.polls < 200 && g.clock >= 1900);
    }
    { // slow_down adds 5 s each time and the loop keeps the new pace
        FakeGitHub g;
        g.tokenReplies = {R"({"error":"slow_down","interval":10})", R"({"error":"slow_down","interval":15})", kPending, kTokenOk};
        const SignInOutcome o = runSignIn(g.io());
        CHECK(o.result == SignInResult::Member && g.waits == std::vector<int>({5000, 10000, 15000, 15000}));
    }
    { // cancelled while waiting: no further request
        FakeGitHub g;
        g.tokenReplies = {kPending};
        g.cancelAfterWaits = 2;
        const SignInOutcome o = runSignIn(g.io());
        CHECK(o.result == SignInResult::Cancelled && g.polls == 2 && g.waits.size() == 3 && signInMessage(o).empty());
    }
    { // a dropped connection or a 5xx between polls is retried; another error ends it
        FakeGitHub g;
        g.tokenReplies = {"", kTokenOk};
        sf::member::SignInIo io = g.io();
        auto post = io.post;
        int n = 0;
        io.post = [&](const std::string& url, const std::string& b) {
            Reply r = post(url, b);
            if (url == kTokenUrl && n++ == 0) r.status = 503, r.body = "<html>";
            return r;
        };
        CHECK(runSignIn(io).result == SignInResult::Member);
        FakeGitHub h;
        h.tokenReplies = {R"({"error":"incorrect_device_code"})"};
        const SignInOutcome o = runSignIn(h.io());
        CHECK(o.result == SignInResult::Failed && signInMessage(o).find("incorrect_device_code") != std::string::npos);
    }
    { // the device code request itself refused (device flow off): a failure that says why
        FakeGitHub g;
        sf::member::SignInIo io = g.io();
        io.post = [](const std::string&, const std::string&) { Reply r; r.status = 200; r.body = R"({"error":"device_flow_disabled"})"; return r; };
        const SignInOutcome o = runSignIn(io);
        CHECK(o.result == SignInResult::Failed && signInMessage(o).find("switched off") != std::string::npos);
        io.post = [](const std::string&, const std::string&) { Reply r; r.error = "could not resolve host"; return r; };
        CHECK(runSignIn(io).error == "could not resolve host");
    }
    // The invitations call carries the token like the other member API calls, and nothing else does.
    CHECK(isMemberApiUrl(kInvitationsApiUrl) && headersFor(kInvitationsApiUrl, "ghu_x").size() == 3);
    CHECK(!isMemberApiUrl("https://api.github.com/user/repository_invitations/5"));
}

// GitHub's newer tokens are ~520 characters and may grow: up to kMaxTokenLength parse, store (split across entries
// where one entry is too small, as on Windows), load and go into the header; longer is refused.
TEST(member_builds_long_tokens) {
    using namespace sf::member;
    const std::string a520 = "ghs_" + std::string(516, 'A'), r4000 = "ghr_" + std::string(3996, 'b');
    const std::string a4096 = "ghu_" + std::string(4092, 'c'), tooLong = "ghu_" + std::string(4093, 'd');
    std::string err;
    for (const auto& [access, refresh] : {std::pair{a520, r4000}, std::pair{r4000, a520}, std::pair{a4096, a4096}}) {
        Tokens t;
        CHECK(parseTokenReply(R"({"access_token":")" + access + R"(","refresh_token":")" + refresh +
                                  R"(","expires_in":28800,"token_type":"bearer"})",
                              0, t, err) == TokenReply::Ok);
        CHECK(t.access == access && t.refresh == refresh);
        CHECK(headersFor(kUserUrl, t.access)[0] == "Authorization: Bearer " + access);
        for (size_t cap : {size_t(0), size_t(2560), size_t(1000)}) {
            keychain::useMemoryForTests(true, cap);
            CHECK(keychain::store(kKeychainAccount, tokensToSecret(t)));
            std::string secret;
            Tokens back;
            CHECK(keychain::load(kKeychainAccount, secret) && tokensFromSecret(secret, back));
            CHECK(back.access == access && back.refresh == refresh);
        }
    }
    Tokens t;
    CHECK(parseTokenReply(R"({"access_token":")" + tooLong + R"(","token_type":"bearer"})", 0, t, err) == TokenReply::Error);
    CHECK(parseTokenReply(R"({"access_token":"ghu_x","refresh_token":")" + tooLong + R"(","token_type":"bearer"})", 0, t,
                          err) == TokenReply::Error);
    DeviceCode c;
    CHECK(parseDeviceCode(R"({"device_code":")" + a520 + R"(","user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device"})",
                          0, c, err));
    CHECK(!parseDeviceCode(R"({"device_code":")" + tooLong + R"(","user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device"})",
                           0, c, err));
    Tokens back;
    CHECK(!tokensFromSecret(R"({"access":")" + tooLong + R"("})", back));

    // Split entries: what earlier versions stored (one entry) still loads; a shorter secret replaces a split one and
    // leaves no parts behind; erase removes every part.
    keychain::useMemoryForTests(true, 2560);
    const std::string old = R"({"access":"ghu_short","refresh":"ghr_short"})";
    CHECK(keychain::store(kKeychainAccount, old));
    std::string got, part;
    CHECK(keychain::load(kKeychainAccount, got) && got == old && tokensFromSecret(got, back) && back.access == "ghu_short");
    const std::string big(9000, 'x');
    CHECK(keychain::store(kKeychainAccount, big) && keychain::load(kKeychainAccount, got) && got == big);
    CHECK(keychain::load(std::string(kKeychainAccount) + "#4", part) && !keychain::load(std::string(kKeychainAccount) + "#5", part));
    CHECK(keychain::store(kKeychainAccount, std::string(3000, 'y')) && keychain::load(kKeychainAccount, got) && got == std::string(3000, 'y'));
    CHECK(!keychain::load(std::string(kKeychainAccount) + "#3", part));
    CHECK(keychain::store(kKeychainAccount, old) && keychain::load(kKeychainAccount, got) && got == old);
    CHECK(!keychain::load(std::string(kKeychainAccount) + "#1", part));
    CHECK(keychain::store(kKeychainAccount, big) && keychain::erase(kKeychainAccount));
    CHECK(!keychain::load(kKeychainAccount, got) && !keychain::load(std::string(kKeychainAccount) + "#1", part));
    CHECK(!keychain::store(kKeychainAccount, std::string(2560 * 17, 'z'))); // (more parts than allowed: refused)
    keychain::useMemoryForTests(true);
}

TEST(member_builds_release_list) {
    using namespace sf::member;
    const char* list = R"([
      {"tag_name":"v99.0.0-preview.2","draft":true,"assets":[]},
      {"tag_name":"v99.0.0-preview.1","draft":false,"assets":[
          {"id":11,"name":"SHA256SUMS.txt"},{"id":12,"name":"SHA256SUMS.txt.sig"},
          {"id":13,"name":"SculptForge-99.0.0-x86_64.AppImage"},{"id":"14","name":"bad-id"},{"id":1.5,"name":"half"}]},
      {"tag_name":"v98.0.0-preview.9","draft":false,"assets":[]},
      {"tag_name":"v99.0.0/../x","assets":[]}, {"tag_name":"latest","assets":[]}, 7])";
    UpdateInfo u;
    std::string err;
    CHECK(parseMemberReleases(list, u, err));
    CHECK(u.version == "99.0.0-preview.1" && u.member && u.files.size() == 3); // the draft and odd entries skipped
    CHECK(u.files["SculptForge-99.0.0-x86_64.AppImage"] == std::string(kAssetUrlPrefix) + "13");
    CHECK(u.downloadUrl == "https://github.com/sculptforge/sculptforge-insiders/releases/tag/v99.0.0-preview.1");
    CHECK(!parseMemberReleases("[]", u, err) && !parseMemberReleases("{}", u, err));

    // A member build downloads and verifies exactly like a public one: same signature, same key, versioned name.
    Sandbox s("sf_member_download");
    FakeReleases f = goodRelease();
    f.assetNames = {{"11", "SHA256SUMS.txt"}, {"12", "SHA256SUMS.txt.sig"}, {"13", "SculptForge-99.0.0-x86_64.AppImage"}};
    uint8_t k[32];
    CHECK(parseHexKey(kTestKey, k));
    Download d;
    downloadUpdate("99.0.0-preview.1", s.install, k, f.fetch(), d, u.files);
    CHECK(d.ok && d.sha256 == kPackageSha);
    for (const auto& url : f.asked) CHECK(isMemberAssetUrl(url)); // nothing public, nothing else
    // Tampered member files fail the same way.
    f.files["SHA256SUMS.txt.sig"][3] ^= 1;
    Download bad;
    downloadUpdate("99.0.0-preview.1", s.install, k, f.fetch(), bad, u.files);
    CHECK(!bad.ok && readAll(s.install.target) == "old version");
    // A file list that points anywhere but the member asset URLs is not used.
    Download elsewhere;
    std::map<std::string, std::string> evil = u.files;
    evil["SHA256SUMS.txt"] = "https://evil.example/SHA256SUMS.txt";
    downloadUpdate("99.0.0-preview.1", s.install, k, f.fetch(), elsewhere, evil);
    CHECK(!elsewhere.ok && !elsewhere.error.empty());
}

// The real thing, once: a public release file through GitHub's redirect to its asset host, with the restricted
// client (every hop allowed, certificates checked). Catches GitHub moving its asset host away from the allowlist.
TEST(self_update_live_github_redirect) {
    if (sf::test::networkTestsOff()) return;
    std::string body;
    const ai::HttpResponse r = ai::httpGetRestricted(
        releaseFileUrl("0.11.0", "SHA256SUMS.txt"), allowedUpdateUrl, 30, [&](const char* d, size_t n) {
            body.append(d, n);
            return body.size() < kMaxSumsBytes;
        });
    if (r.status == 0) { // (offline machine: nothing to learn here)
        std::fprintf(stderr, "self_update_live_github_redirect: no connection (%s)\n", r.error.c_str());
        return;
    }
    CHECK(r.status == 200 && r.error.empty());
    CHECK(sumFor(body, "SculptForge-0.11.0-windows-x86_64.zip").size() == 64);
}

#if defined(_WIN32)
// Windows Credential Manager itself (the tests otherwise use the in-memory keychain): store, read back, replace, erase,
// and a secret over its 2560-byte entry limit (two long GitHub tokens), split across entries. A test entry, not the
// member sign-in's.
TEST(self_update_keychain_windows) {
    keychain::useMemoryForTests(false);
    const std::string account = "test-self-update";
    keychain::erase(account);
    std::string got;
    CHECK(!keychain::load(account, got));
    CHECK(keychain::store(account, "{\"access\":\"ghu_test\"}"));
    CHECK(keychain::load(account, got) && got == "{\"access\":\"ghu_test\"}");
    CHECK(keychain::store(account, "second"));
    CHECK(keychain::load(account, got) && got == "second");
    member::Tokens t;
    t.access = "ghu_" + std::string(4092, 'a');
    t.refresh = "ghr_" + std::string(4092, 'b');
    CHECK(keychain::store(account, member::tokensToSecret(t)));
    member::Tokens back;
    CHECK(keychain::load(account, got) && member::tokensFromSecret(got, back) && back.access == t.access && back.refresh == t.refresh);
    CHECK(keychain::store(account, "short again") && keychain::load(account, got) && got == "short again");
    CHECK(!keychain::load(account + "#1", got));
    CHECK(keychain::store(account, member::tokensToSecret(t)));
    CHECK(keychain::erase(account) && !keychain::load(account, got) && !keychain::load(account + "#1", got));
    CHECK(keychain::erase(account)); // (nothing there: still fine)
    keychain::useMemoryForTests(true);
}
#endif

#if defined(__linux__)
// After Restart to update, the new version ends the old one only when it's that old SculptForge from the same AppImage
// file (0.11.10 and earlier stayed hidden and kept running: @ebarranco). A fake /proc/<pid> entry.
TEST(self_update_old_appimage_check) {
    const fs::path dir = fs::temp_directory_path() / ("sf-oldappimage-" + std::to_string(::getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "mount" / "usr" / "bin");
    std::ofstream(dir / "mount" / "usr" / "bin" / "sculptforge") << "x";
    std::ofstream(dir / "mount" / "usr" / "bin" / "dolphin") << "x";
    const std::string appImage = "/home/someone/SculptForge/SculptForge-0.11.1-x86_64.AppImage";
    const auto proc = [&](const std::string& name, const std::string& program, const std::string& environ) {
        const fs::path p = dir / name;
        fs::create_directories(p);
        fs::create_symlink(dir / "mount" / "usr" / "bin" / program, p / "exe");
        std::ofstream(p / "environ", std::ios::binary) << environ;
        return p.string();
    };
    const std::string env = std::string("HOME=/home/someone") + '\0' + "APPIMAGE=" + appImage + '\0' + "OWD=/home/someone" + '\0';
    CHECK(isOldAppImage(proc("old", "sculptforge", env), appImage));
    CHECK(!isOldAppImage(proc("other-file", "sculptforge", env), appImage + ".old"));           // another copy
    CHECK(!isOldAppImage(proc("prefix", "sculptforge", env), "/home/someone/SculptForge/SculptForge")); // whole value only
    CHECK(!isOldAppImage(proc("filemanager", "dolphin", env), appImage));                        // the file manager
    CHECK(!isOldAppImage(proc("no-appimage", "sculptforge", std::string("HOME=/x") + '\0'), appImage));
    CHECK(!isOldAppImage(proc("old2", "sculptforge", env), ""));
    CHECK(!isOldAppImage((dir / "missing").string(), appImage));
    CHECK(!isOldAppImage("/proc/1", appImage)); // init: not ours
    CHECK(!isOldAppImage("/proc/self", appImage)); // the tests: not sculptforge
    fs::remove_all(dir);
}
#endif
