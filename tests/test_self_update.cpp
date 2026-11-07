// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// One-click update (src/core/SelfUpdate.cpp, src/core/Ed25519.cpp, ai::httpGetRestricted): only GitHub's release
// URLs, only a correctly signed SHA256SUMS.txt, only a package with its hash; the swap and the rollback. No network:
// the downloads are served by a stand-in for the HTTP client.
#include "TestHarness.h"

#include "ai/Http.h"
#include "core/Ed25519.h"
#include "core/MemberBuilds.h"
#include "core/SelfUpdate.h"
#include "core/Sha256.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

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

void writeAll(const fs::path& p, const std::string& s) { std::ofstream(p, std::ios::binary | std::ios::trunc) << s; }

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
            const uint64_t total = claimTotal && it->first.find("AppImage") != std::string::npos ? claimTotal : body.size();
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
    CHECK(versionedAsset("SculptForge-macos.dmg", "1.0.0").empty());
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
    p.os = "macos", p.exePath = "/Applications/SculptForge.app/Contents/MacOS/sculptforge";
    present.clear();
    in = detectInstall(p);
    CHECK(in.kind == InstallKind::MacApp && in.target == "/Applications/SculptForge.app" && !selfUpdates(in.kind));
    present = {"/opt/homebrew/Caskroom/sculptforge"};
    CHECK(detectInstall(p).command == "brew upgrade --cask sculptforge");
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
    in.kind = InstallKind::WindowsInstaller;
    const std::string i = windowsHelperScript(in, d, 1234, false);
    CHECK(i.find("\"/S /D=$root\"") != std::string::npos && i.find("Get-FileHash") < i.find("Start-Process"));
    CHECK(i.find("Start-Process -FilePath $exe") == std::string::npos); // Later: no relaunch
    const std::string ir = windowsHelperScript(in, d, 1234, true);
    CHECK(ir.find("catch { $code = -1 }") < ir.find("Start-Process -FilePath $exe")); // declined prompt: old one again
}

// --- Member builds (core/MemberBuilds.cpp) ---

TEST(member_builds_urls_and_headers) {
    using namespace sf::member;
    CHECK(isMemberApiUrl("https://api.github.com/repos/islandmed/sculptforge-insiders/releases?per_page=20"));
    CHECK(isMemberApiUrl("https://api.github.com/user"));
    CHECK(isMemberAssetUrl("https://api.github.com/repos/islandmed/sculptforge-insiders/releases/assets/123456"));
    for (const char* u : {"https://api.github.com/repos/islandmed/sculptforge-insiders/releases/assets/12a",
                          "https://api.github.com/repos/islandmed/sculptforge-insiders/releases/assets/",
                          "https://api.github.com/repos/islandmed/sculptforge-insiders/releases/assets/1/../../x",
                          "https://api.github.com/repos/islandmed/SculptForge/releases?per_page=20",
                          "https://api.github.com/repos/islandmed/sculptforge-insiders/contents/secrets",
                          "https://api.github.com/user/repos", "https://api.github.com.evil.example/user",
                          "http://api.github.com/user"})
        CHECK(!isMemberApiUrl(u) && !allowedUpdateUrl(u));
    // The token goes to the member API only: never to the asset host, the public release URLs or anywhere else.
    const auto h = headersFor("https://api.github.com/repos/islandmed/sculptforge-insiders/releases/assets/5", "ghu_x");
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
    CHECK(u.downloadUrl == "https://github.com/islandmed/sculptforge-insiders/releases/tag/v99.0.0-preview.1");
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
#include "core/Keychain.h"
// Windows Credential Manager: store, read back, replace, erase (a test entry, not the member sign-in's).
TEST(self_update_keychain_windows) {
    const std::string account = "test-self-update";
    keychain::erase(account);
    std::string got;
    CHECK(!keychain::load(account, got));
    CHECK(keychain::store(account, "{\"access\":\"ghu_test\"}"));
    CHECK(keychain::load(account, got) && got == "{\"access\":\"ghu_test\"}");
    CHECK(keychain::store(account, "second"));
    CHECK(keychain::load(account, got) && got == "second");
    CHECK(keychain::erase(account) && !keychain::load(account, got));
    CHECK(keychain::erase(account)); // (nothing there: still fine)
}
#endif
