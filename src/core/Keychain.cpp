// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Keychain.h"

#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincred.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <mutex>
#endif

namespace sf::keychain {

#if defined(_WIN32)

namespace {

std::wstring target(const std::string& account) {
    const std::string t = "SculptForge/" + account;
    return std::wstring(t.begin(), t.end()); // (ASCII account names only)
}

} // namespace

bool store(const std::string& account, const std::string& secret) {
    if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) return false;
    std::wstring name = target(account);
    CREDENTIALW c{};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = name.data();
    c.CredentialBlobSize = DWORD(secret.size());
    c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.data()));
    c.Persist = CRED_PERSIST_LOCAL_MACHINE; // this user on this PC (not roamed to other PCs)
    wchar_t user[] = L"SculptForge";
    c.UserName = user;
    return CredWriteW(&c, 0) != FALSE;
}

bool load(const std::string& account, std::string& secret) {
    PCREDENTIALW c = nullptr;
    if (!CredReadW(target(account).c_str(), CRED_TYPE_GENERIC, 0, &c) || !c) return false;
    secret.assign(reinterpret_cast<const char*>(c->CredentialBlob), c->CredentialBlobSize);
    SecureZeroMemory(c->CredentialBlob, c->CredentialBlobSize);
    CredFree(c);
    return true;
}

bool erase(const std::string& account) {
    return CredDeleteW(target(account).c_str(), CRED_TYPE_GENERIC, 0) != FALSE || GetLastError() == ERROR_NOT_FOUND;
}

#elif defined(__linux__)

// libsecret's C API, declared here so nothing links against it: a system without it simply has no keychain.
namespace {

struct SchemaAttribute {
    const char* name;
    int type; // SECRET_SCHEMA_ATTRIBUTE_STRING = 0
};
struct Schema {
    const char* name;
    int flags; // SECRET_SCHEMA_NONE = 0
    SchemaAttribute attributes[32];
    int reserved;
    void* reserved1[7];
};
struct GErrorC {
    unsigned domain;
    int code;
    char* message;
};

using StoreFn = int (*)(const Schema*, const char*, const char*, const char*, void*, GErrorC**, ...);
using LookupFn = char* (*)(const Schema*, void*, GErrorC**, ...);
using ClearFn = int (*)(const Schema*, void*, GErrorC**, ...);
using FreeFn = void (*)(char*);
using ErrorFreeFn = void (*)(GErrorC*);

struct Secret {
    StoreFn store = nullptr;
    LookupFn lookup = nullptr;
    ClearFn clear = nullptr;
    FreeFn free = nullptr;
    ErrorFreeFn errorFree = nullptr;
    bool ok = false;
};

const Secret& secretLib() {
    static Secret s;
    static std::once_flag once;
    std::call_once(once, [] {
        void* h = dlopen("libsecret-1.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!h) return;
        s.store = reinterpret_cast<StoreFn>(dlsym(h, "secret_password_store_sync"));
        s.lookup = reinterpret_cast<LookupFn>(dlsym(h, "secret_password_lookup_sync"));
        s.clear = reinterpret_cast<ClearFn>(dlsym(h, "secret_password_clear_sync"));
        s.free = reinterpret_cast<FreeFn>(dlsym(h, "secret_password_free"));
        s.errorFree = reinterpret_cast<ErrorFreeFn>(dlsym(h, "g_error_free")); // (GLib, loaded with it)
        s.ok = s.store && s.lookup && s.clear && s.free && s.errorFree;
    });
    return s;
}

const Schema* schema() {
    static Schema sc = [] {
        Schema x{};
        x.name = "app.sculptforge.Secret";
        x.attributes[0] = {"account", 0};
        return x;
    }();
    return &sc;
}

bool done(const Secret& s, GErrorC* error, int result) {
    if (error) s.errorFree(error); // (its message is not logged: it can name the item)
    return result && !error;
}

} // namespace

bool store(const std::string& account, const std::string& secret) {
    const Secret& s = secretLib();
    if (!s.ok) return false;
    GErrorC* error = nullptr;
    const int r = s.store(schema(), nullptr, "SculptForge member builds (GitHub sign-in)", secret.c_str(), nullptr, &error,
                          "account", account.c_str(), static_cast<char*>(nullptr));
    return done(s, error, r);
}

bool load(const std::string& account, std::string& secret) {
    const Secret& s = secretLib();
    if (!s.ok) return false;
    GErrorC* error = nullptr;
    char* p = s.lookup(schema(), nullptr, &error, "account", account.c_str(), static_cast<char*>(nullptr));
    if (error) s.errorFree(error);
    if (!p) return false;
    secret = p;
    std::memset(p, 0, std::strlen(p));
    s.free(p);
    return true;
}

bool erase(const std::string& account) {
    const Secret& s = secretLib();
    if (!s.ok) return true; // nothing could have been stored
    GErrorC* error = nullptr;
    const int r = s.clear(schema(), nullptr, &error, "account", account.c_str(), static_cast<char*>(nullptr));
    (void)r; // false also means "there was none"
    return !error ? true : (s.errorFree(error), false);
}

#else // macOS: the Keychain follows (Security framework); until then the sign-in isn't kept between sessions.

bool store(const std::string&, const std::string&) { return false; }
bool load(const std::string&, std::string&) { return false; }
bool erase(const std::string&) { return true; }

#endif

} // namespace sf::keychain
