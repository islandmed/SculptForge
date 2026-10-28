// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "link/PhoneRtc.h"

#include <condition_variable>
#include <future>
#include <mutex>
#include <random>

#if SF_HAVE_WEBRTC
#include <mbedtls/gcm.h>
#include <rtc/rtc.hpp>
#endif

namespace sf::link {

// --- Encoding and randomness (always available) ---

std::string base64UrlEncode(const std::string& bytes) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t v = uint32_t(uint8_t(bytes[i])) << 16 | uint32_t(uint8_t(bytes[i + 1])) << 8 | uint8_t(bytes[i + 2]);
        out += table[v >> 18];
        out += table[(v >> 12) & 63];
        out += table[(v >> 6) & 63];
        out += table[v & 63];
    }
    if (i + 1 == bytes.size()) {
        const uint32_t v = uint32_t(uint8_t(bytes[i])) << 16;
        out += table[v >> 18];
        out += table[(v >> 12) & 63];
    } else if (i + 2 == bytes.size()) {
        const uint32_t v = uint32_t(uint8_t(bytes[i])) << 16 | uint32_t(uint8_t(bytes[i + 1])) << 8;
        out += table[v >> 18];
        out += table[(v >> 12) & 63];
        out += table[(v >> 6) & 63];
    }
    return out;
}

bool base64UrlDecode(const std::string& text, std::string& bytes) {
    bytes.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '-' || c == '+') v = 62;
        else if (c == '_' || c == '/') v = 63;
        else if (c == '=') break;
        else return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            bytes += char((acc >> bits) & 0xFF);
        }
    }
    return true;
}

std::string randomBytes(size_t n) {
    static std::mutex mutex;
    static std::random_device device;
    std::lock_guard lock(mutex);
    std::string out(n, '\0');
    for (size_t i = 0; i < n; i += 4) {
        const uint32_t v = device();
        for (size_t j = 0; j < 4 && i + j < n; ++j) out[i + j] = char((v >> (8 * j)) & 0xFF);
    }
    return out;
}

const char* faceRtcStateName(FaceRtc::State state) {
    switch (state) {
    case FaceRtc::State::Idle: return "idle";
    case FaceRtc::State::Offered: return "waiting for the phone";
    case FaceRtc::State::Connecting: return "connecting";
    case FaceRtc::State::Connected: return "connected";
    case FaceRtc::State::Closed: return "closed";
    case FaceRtc::State::Failed: return "failed";
    }
    return "?";
}

#if SF_HAVE_WEBRTC

bool webrtcAvailable() { return true; }

namespace {

bool gcm(bool encrypt, const std::string& key, const std::string& iv, const std::string& in, std::string& out,
         std::string& tag, std::string& error) {
    if (key.size() != 32) {
        error = "The key must be 32 bytes";
        return false;
    }
    mbedtls_gcm_context ctx;
    mbedtls_gcm_init(&ctx);
    int rc = mbedtls_gcm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, reinterpret_cast<const unsigned char*>(key.data()), 256);
    out.assign(in.size(), '\0');
    if (rc == 0) {
        if (encrypt) {
            tag.assign(16, '\0');
            rc = mbedtls_gcm_crypt_and_tag(&ctx, MBEDTLS_GCM_ENCRYPT, in.size(),
                                           reinterpret_cast<const unsigned char*>(iv.data()), iv.size(), nullptr, 0,
                                           reinterpret_cast<const unsigned char*>(in.data()),
                                           reinterpret_cast<unsigned char*>(out.data()), 16,
                                           reinterpret_cast<unsigned char*>(tag.data()));
        } else {
            rc = mbedtls_gcm_auth_decrypt(&ctx, in.size(), reinterpret_cast<const unsigned char*>(iv.data()), iv.size(),
                                          nullptr, 0, reinterpret_cast<const unsigned char*>(tag.data()), tag.size(),
                                          reinterpret_cast<const unsigned char*>(in.data()),
                                          reinterpret_cast<unsigned char*>(out.data()));
        }
    }
    mbedtls_gcm_free(&ctx);
    if (rc != 0) {
        error = encrypt ? "Encryption failed" : "The message doesn't decrypt with this session's key";
        out.clear();
        return false;
    }
    return true;
}

} // namespace

bool aesGcmEncrypt(const std::string& key, const std::string& plain, std::string& packed, std::string& error) {
    const std::string iv = randomBytes(12);
    std::string cipher, tag;
    if (!gcm(true, key, iv, plain, cipher, tag, error)) return false;
    packed = base64UrlEncode(iv + cipher + tag);
    return true;
}

bool aesGcmDecrypt(const std::string& key, const std::string& packed, std::string& plain, std::string& error) {
    std::string bytes;
    if (!base64UrlDecode(packed, bytes) || bytes.size() < 12 + 16) {
        error = "Not an encrypted message";
        return false;
    }
    std::string tag = bytes.substr(bytes.size() - 16);
    return gcm(false, key, bytes.substr(0, 12), bytes.substr(12, bytes.size() - 28), plain, tag, error);
}

struct FaceRtc::Impl {
    std::mutex mutex;
    std::condition_variable gathered;
    bool gatheringDone = false;
    State state = State::Idle;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::DataChannel> channel;
    // The test peer.
    std::shared_ptr<rtc::PeerConnection> testPc;
    std::shared_ptr<rtc::DataChannel> testChannel;
};

namespace {

rtc::DataChannelInit channelInit(FaceRtc::Mode mode) {
    rtc::DataChannelInit init;
    init.negotiated = true; // both sides open channel 0 themselves: no in-band announcement to lose
    init.id = 0;
    if (mode == FaceRtc::Mode::Face) {
        init.reliability.unordered = true;
        init.reliability.maxRetransmits = 0;
    }
    return init;
}

const char* channelLabel(FaceRtc::Mode mode) { return mode == FaceRtc::Mode::Ar ? "ar" : "face"; }

rtc::Configuration localConfiguration() {
    return rtc::Configuration(); // no STUN or TURN servers: the phone and this computer share a network
}

} // namespace

FaceRtc::FaceRtc(Mode mode) : impl_(std::make_unique<Impl>()), mode_(mode) {
    static std::once_flag logging;
    std::call_once(logging, [] { rtc::InitLogger(rtc::LogLevel::None); });
}

FaceRtc::~FaceRtc() { close(); }

FaceRtc::State FaceRtc::state() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->state;
}

bool FaceRtc::createOffer(std::string& sdp, std::string& error, int timeoutMs) {
    close();
    try {
        auto pc = std::make_shared<rtc::PeerConnection>(localConfiguration());
        Impl* impl = impl_.get();
        auto setState = [this, impl](State s) {
            {
                std::lock_guard lock(impl->mutex);
                if (impl->state == s) return;
                impl->state = s;
            }
            if (onState) onState(s);
        };
        pc->onGatheringStateChange([impl](rtc::PeerConnection::GatheringState s) {
            if (s != rtc::PeerConnection::GatheringState::Complete) return;
            std::lock_guard lock(impl->mutex);
            impl->gatheringDone = true;
            impl->gathered.notify_all();
        });
        pc->onStateChange([setState](rtc::PeerConnection::State s) {
            using S = rtc::PeerConnection::State;
            if (s == S::Connecting) setState(State::Connecting);
            else if (s == S::Failed) setState(State::Failed);
            else if (s == S::Disconnected || s == S::Closed) setState(State::Closed);
        });
        auto channel = pc->createDataChannel(channelLabel(mode_), channelInit(mode_));
        channel->onOpen([setState] { setState(State::Connected); });
        channel->onClosed([setState] { setState(State::Closed); });
        channel->onMessage([this](rtc::message_variant m) {
            if (const auto* text = std::get_if<std::string>(&m); text && onMessage) onMessage(*text);
            else if (const auto* binary = std::get_if<rtc::binary>(&m); binary && onMessage)
                onMessage(std::string(reinterpret_cast<const char*>(binary->data()), binary->size()));
        });
        {
            std::lock_guard lock(impl->mutex);
            impl->pc = pc;
            impl->channel = channel;
            impl->gatheringDone = false;
        }
        pc->setLocalDescription(rtc::Description::Type::Offer);
        std::unique_lock lock(impl->mutex);
        impl->gathered.wait_for(lock, std::chrono::milliseconds(timeoutMs), [impl] { return impl->gatheringDone; });
        const auto description = pc->localDescription();
        if (!description) {
            error = "WebRTC made no offer";
            return false;
        }
        sdp = std::string(*description);
        if (sdp.find("a=candidate:") == std::string::npos) {
            error = "This computer has no network address the phone could reach";
            return false;
        }
        impl->state = State::Offered;
        return true;
    } catch (const std::exception& e) {
        error = std::string("WebRTC: ") + e.what();
        return false;
    }
}

bool FaceRtc::setAnswer(const std::string& sdp, std::string& error) {
    std::shared_ptr<rtc::PeerConnection> pc;
    {
        std::lock_guard lock(impl_->mutex);
        pc = impl_->pc;
        if (!pc || impl_->state != State::Offered) {
            error = pc ? "This face link is already answered" : "No face link is waiting for an answer";
            return false;
        }
        impl_->state = State::Connecting;
    }
    try {
        pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
        return true;
    } catch (const std::exception& e) {
        std::lock_guard lock(impl_->mutex);
        impl_->state = State::Offered; // a bad answer doesn't spoil the offer
        error = std::string("The phone's answer is not valid: ") + e.what();
        return false;
    }
}

bool FaceRtc::send(const std::string& text) {
    std::shared_ptr<rtc::DataChannel> channel;
    {
        std::lock_guard lock(impl_->mutex);
        channel = impl_->channel;
    }
    try {
        return channel && channel->isOpen() && channel->send(text);
    } catch (const std::exception&) {
        return false;
    }
}

bool FaceRtc::sendBinary(const std::string& bytes) {
    std::shared_ptr<rtc::DataChannel> channel;
    {
        std::lock_guard lock(impl_->mutex);
        channel = impl_->channel;
    }
    try {
        if (!channel || !channel->isOpen()) return false;
        channel->send(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()); // false = queued, still sent
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void FaceRtc::close() {
    std::shared_ptr<rtc::PeerConnection> pc, testPc;
    std::shared_ptr<rtc::DataChannel> channel, testChannel;
    {
        std::lock_guard lock(impl_->mutex);
        pc = std::move(impl_->pc);
        channel = std::move(impl_->channel);
        testPc = std::move(impl_->testPc);
        testChannel = std::move(impl_->testChannel);
        impl_->state = State::Idle;
    }
    try {
        if (channel) {
            channel->onMessage(nullptr);
            channel->onOpen(nullptr);
            channel->onClosed(nullptr);
        }
        if (pc) {
            pc->onStateChange(nullptr);
            pc->onGatheringStateChange(nullptr);
            pc->close();
        }
        if (testPc) testPc->close();
    } catch (const std::exception&) {
    }
}

bool FaceRtc::answerForTest(const std::string& offerSdp, std::string& answerSdp, std::string& error, int timeoutMs) {
    try {
        auto pc = std::make_shared<rtc::PeerConnection>(localConfiguration());
        auto done = std::make_shared<std::promise<void>>();
        auto once = std::make_shared<std::once_flag>();
        pc->onGatheringStateChange([done, once](rtc::PeerConnection::GatheringState s) {
            if (s == rtc::PeerConnection::GatheringState::Complete) std::call_once(*once, [&] { done->set_value(); });
        });
        auto channel = pc->createDataChannel(channelLabel(mode_), channelInit(mode_));
        if (onTestMessage)
            channel->onMessage([handler = onTestMessage](rtc::message_variant m) {
                if (const auto* text = std::get_if<std::string>(&m)) handler(*text, false);
                else if (const auto* binary = std::get_if<rtc::binary>(&m))
                    handler(std::string(reinterpret_cast<const char*>(binary->data()), binary->size()), true);
            });
        pc->setRemoteDescription(rtc::Description(offerSdp, rtc::Description::Type::Offer));
        done->get_future().wait_for(std::chrono::milliseconds(timeoutMs));
        const auto description = pc->localDescription();
        if (!description) {
            error = "No answer";
            return false;
        }
        answerSdp = std::string(*description);
        std::lock_guard lock(impl_->mutex);
        impl_->testPc = pc;
        impl_->testChannel = channel;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool FaceRtc::sendFromTestPeer(const std::string& text) {
    std::shared_ptr<rtc::DataChannel> channel;
    {
        std::lock_guard lock(impl_->mutex);
        channel = impl_->testChannel;
    }
    try {
        return channel && channel->isOpen() && channel->send(text);
    } catch (const std::exception&) {
        return false;
    }
}

#else // without WebRTC

bool webrtcAvailable() { return false; }

struct FaceRtc::Impl {};

FaceRtc::FaceRtc(Mode mode) : impl_(std::make_unique<Impl>()), mode_(mode) {}
FaceRtc::~FaceRtc() = default;
FaceRtc::State FaceRtc::state() const { return State::Idle; }

bool FaceRtc::createOffer(std::string&, std::string& error, int) {
    error = "This SculptForge was built without WebRTC (SCULPTFORGE_WEBRTC=OFF), so the live face feed can't connect";
    return false;
}
bool FaceRtc::setAnswer(const std::string&, std::string& error) {
    error = "Built without WebRTC";
    return false;
}
bool FaceRtc::send(const std::string&) { return false; }
bool FaceRtc::sendBinary(const std::string&) { return false; }
void FaceRtc::close() {}
bool FaceRtc::answerForTest(const std::string&, std::string&, std::string& error, int) {
    error = "Built without WebRTC";
    return false;
}
bool FaceRtc::sendFromTestPeer(const std::string&) { return false; }
bool aesGcmEncrypt(const std::string&, const std::string&, std::string&, std::string& error) {
    error = "Built without WebRTC";
    return false;
}
bool aesGcmDecrypt(const std::string&, const std::string&, std::string&, std::string& error) {
    error = "Built without WebRTC";
    return false;
}

#endif

} // namespace sf::link
