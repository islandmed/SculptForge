// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Keychain.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace sf::keychain {

namespace {

std::atomic<bool> gMemory{false};
std::atomic<size_t> gMemoryMaxEntry{0};
std::mutex gMemoryMutex;
std::map<std::string, std::string>& memoryStore() {
    static std::map<std::string, std::string> m;
    return m;
}

bool disabledByEnv() {
    const char* v = std::getenv("SF_KEYCHAIN");
    return v && (std::strcmp(v, "none") == 0 || std::strcmp(v, "off") == 0);
}

// What the OS keychain shows for an account.
const char* labelFor(const std::string& account) {
    if (account.rfind("member-builds", 0) == 0) return "SculptForge member builds (GitHub sign-in)";
    if (account.rfind("sso/", 0) == 0) return "SculptForge studio sign-in";
    if (account.rfind("ai/", 0) == 0) return "SculptForge AI provider key";
    return "SculptForge";
}

} // namespace

void useMemoryForTests(bool on, size_t maxEntry) {
    gMemory = on;
    gMemoryMaxEntry = maxEntry;
    std::lock_guard lock(gMemoryMutex);
    memoryStore().clear();
}

namespace os {
bool store(const std::string& account, const std::string& secret);
bool load(const std::string& account, std::string& secret);
bool erase(const std::string& account);
bool available();
size_t maxEntry(); // the most one entry holds (0: no limit)
} // namespace os

namespace {

// One entry, in memory (tests) or the OS keychain.
bool put(const std::string& account, const std::string& secret) {
    if (gMemory) {
        if (gMemoryMaxEntry && secret.size() > gMemoryMaxEntry) return false;
        std::lock_guard lock(gMemoryMutex);
        memoryStore()[account] = secret;
        return true;
    }
    return os::store(account, secret);
}

bool get(const std::string& account, std::string& secret) {
    if (gMemory) {
        std::lock_guard lock(gMemoryMutex);
        auto it = memoryStore().find(account);
        if (it == memoryStore().end()) return false;
        secret = it->second;
        return true;
    }
    return os::load(account, secret);
}

bool del(const std::string& account) {
    if (gMemory) {
        std::lock_guard lock(gMemoryMutex);
        memoryStore().erase(account);
        return true;
    }
    return os::erase(account);
}

size_t maxEntry() { return gMemory ? gMemoryMaxEntry.load() : os::maxEntry(); }

// A secret longer than one entry holds (Windows: 2560 bytes; two long GitHub tokens are more) is split: the account's
// own entry holds kSplitMark + the part count, and "<account>#1".."#N" hold the parts. A short secret stays one entry,
// as before, so what earlier versions stored still loads.
constexpr const char* kSplitMark = "\x01sf-split:";
constexpr int kMaxParts = 16;

std::string partName(const std::string& account, int i) { return account + "#" + std::to_string(i); }

// The part count the account's entry names (0: not split).
int splitParts(const std::string& head) {
    const std::string mark = kSplitMark;
    if (head.compare(0, mark.size(), mark) != 0) return 0;
    const int n = std::atoi(head.c_str() + mark.size());
    return n > 0 && n <= kMaxParts ? n : 0;
}

void wipe(std::string& s) {
    std::fill(s.begin(), s.end(), '\0');
    s.clear();
}

// Removes "<account>#from".."#to" (parts a longer secret left behind).
void dropParts(const std::string& account, int from, int to) {
    for (int i = from; i <= to; ++i) del(partName(account, i));
}

int storedParts(const std::string& account) {
    std::string head;
    const int n = get(account, head) ? splitParts(head) : 0;
    wipe(head);
    return n;
}

} // namespace

bool store(const std::string& account, const std::string& secret) {
    if (!gMemory && disabledByEnv()) return false;
    const int before = storedParts(account);
    const size_t cap = maxEntry();
    if (!cap || secret.size() <= cap) {
        if (!put(account, secret)) return false;
        dropParts(account, 1, before);
        return true;
    }
    const size_t n = (secret.size() + cap - 1) / cap;
    if (n > size_t(kMaxParts)) return false;
    for (size_t i = 0; i < n; ++i)
        if (!put(partName(account, int(i) + 1), secret.substr(i * cap, cap))) return (dropParts(account, 1, int(n)), false);
    if (!put(account, kSplitMark + std::to_string(n))) return (dropParts(account, 1, int(n)), false);
    dropParts(account, int(n) + 1, before);
    return true;
}

bool load(const std::string& account, std::string& secret) {
    if (!gMemory && disabledByEnv()) return false;
    std::string head;
    if (!get(account, head)) return false;
    const int n = splitParts(head);
    if (!n) {
        secret = std::move(head);
        return true;
    }
    std::string joined, part;
    for (int i = 1; i <= n; ++i) {
        if (!get(partName(account, i), part)) return (wipe(joined), false);
        joined += part;
        wipe(part);
    }
    secret = std::move(joined);
    return true;
}

bool erase(const std::string& account) {
    if (!gMemory && disabledByEnv()) return true;
    dropParts(account, 1, storedParts(account));
    return del(account);
}

bool available() { return gMemory || (!disabledByEnv() && os::available()); }

namespace os {

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

bool available() { return true; }
size_t maxEntry() { return CRED_MAX_CREDENTIAL_BLOB_SIZE; }

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
    const int r = s.store(schema(), nullptr, labelFor(account), secret.c_str(), nullptr, &error,
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

bool available() { return secretLib().ok; }
size_t maxEntry() { return 0; }

#elif defined(__APPLE__)

// A generic password per account: service "SculptForge", the account name as kSecAttrAccount, this user's login
// keychain, readable only by this app (the Keychain asks the user before another app may read it).
namespace {

struct Cf {
    CFTypeRef ref = nullptr;
    explicit Cf(CFTypeRef r) : ref(r) {}
    ~Cf() {
        if (ref) CFRelease(ref);
    }
    Cf(const Cf&) = delete;
    Cf& operator=(const Cf&) = delete;
};

CFStringRef cfString(const std::string& s) {
    return CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(s.data()), CFIndex(s.size()), kCFStringEncodingUTF8, false);
}

// The query that names one account's item.
CFMutableDictionaryRef query(const std::string& account) {
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    Cf service(cfString("SculptForge")), acct(cfString(account));
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, service.ref);
    CFDictionarySetValue(q, kSecAttrAccount, acct.ref);
    return q;
}

} // namespace

bool store(const std::string& account, const std::string& secret) {
    Cf q(query(account));
    Cf data(CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(secret.data()), CFIndex(secret.size())));
    CFMutableDictionaryRef update = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    Cf updateRef(update);
    CFDictionarySetValue(update, kSecValueData, data.ref);
    OSStatus st = SecItemUpdate(static_cast<CFDictionaryRef>(q.ref), update);
    if (st == errSecItemNotFound) {
        CFMutableDictionaryRef add = CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(q.ref));
        Cf addRef(add);
        Cf label(cfString(labelFor(account)));
        CFDictionarySetValue(add, kSecValueData, data.ref);
        CFDictionarySetValue(add, kSecAttrLabel, label.ref);
        CFDictionarySetValue(add, kSecAttrAccessible, kSecAttrAccessibleWhenUnlocked); // this Mac only, while unlocked
        st = SecItemAdd(add, nullptr);
    }
    return st == errSecSuccess;
}

bool load(const std::string& account, std::string& secret) {
    CFMutableDictionaryRef q = query(account);
    Cf qRef(q);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    if (SecItemCopyMatching(q, &result) != errSecSuccess || !result) return false;
    Cf resultRef(result);
    if (CFGetTypeID(result) != CFDataGetTypeID()) return false;
    const auto data = static_cast<CFDataRef>(result);
    secret.assign(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), size_t(CFDataGetLength(data)));
    return true;
}

bool erase(const std::string& account) {
    Cf q(query(account));
    const OSStatus st = SecItemDelete(static_cast<CFDictionaryRef>(q.ref));
    return st == errSecSuccess || st == errSecItemNotFound;
}

bool available() { return true; }
size_t maxEntry() { return 0; }

#else // another OS: nothing is kept between sessions.

bool store(const std::string&, const std::string&) { return false; }
bool load(const std::string&, std::string&) { return false; }
bool erase(const std::string&) { return true; }
bool available() { return false; }
size_t maxEntry() { return 0; }

#endif

} // namespace os

} // namespace sf::keychain
