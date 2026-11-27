// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/AndroidHttp.h"

#include "core/NetworkPolicy.h"

#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

namespace sf::android {

bool isUpdateFeedUrl(const std::string& url) {
    for (const char* host : {"https://sculptforge.app/", "https://islandmed.github.io/"}) {
        const std::string prefix = host;
        if (url.rfind(prefix, 0) != 0) continue;
        const std::string rest = url.substr(prefix.size());
        return rest.find_first_of("?#@\\ ") == std::string::npos && rest.find("..") == std::string::npos;
    }
    return false;
}

ai::HttpResponse parseFeedAnswer(const std::string& answer) {
    ai::HttpResponse r;
    const size_t a = answer.find('\n');
    const size_t b = a == std::string::npos ? a : answer.find('\n', a + 1);
    if (b == std::string::npos) {
        r.error = "no answer";
        return r;
    }
    try {
        r.status = std::stol(answer.substr(0, a));
    } catch (...) {
        r.error = "no answer";
        return r;
    }
    r.error = answer.substr(a + 1, b - a - 1);
    r.body = answer.substr(b + 1);
    return r;
}

#ifdef __ANDROID__
ai::HttpResponse updateFeedGet(const std::string& url, int timeoutSeconds) {
    ai::HttpResponse r;
    if (!isUpdateFeedUrl(url)) {
        r.error = "refused: not the update feed";
        return r;
    }
    if (!net::urlAllowed(url, &r.error)) return r; // (studio mode's list, as in ai/Http.cpp)
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv()); // (attaches this worker thread)
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) {
        r.error = "no connection";
        return r;
    }
    jclass cls = env->GetObjectClass(activity);
    jmethodID get = cls ? env->GetStaticMethodID(cls, "updateFeedGet", "(Ljava/lang/String;I)Ljava/lang/String;") : nullptr;
    jstring jurl = get ? env->NewStringUTF(url.c_str()) : nullptr;
    auto answer = jurl ? static_cast<jstring>(env->CallStaticObjectMethod(cls, get, jurl, jint(timeoutSeconds * 1000))) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        answer = nullptr;
    }
    if (answer) {
        const char* chars = env->GetStringUTFChars(answer, nullptr);
        if (chars) {
            r = parseFeedAnswer(chars);
            env->ReleaseStringUTFChars(answer, chars);
        }
        env->DeleteLocalRef(answer);
    } else {
        r.error = "no connection";
    }
    if (jurl) env->DeleteLocalRef(jurl);
    if (cls) env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return r;
}
#endif

} // namespace sf::android
