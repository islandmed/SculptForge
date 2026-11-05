// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/Sha256.h"

#include <algorithm>
#include <cstring>

namespace sf {

namespace {

constexpr uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

constexpr uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void compress(uint32_t* h, const uint8_t* block) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = uint32_t(block[i * 4]) << 24 | uint32_t(block[i * 4 + 1]) << 16 | uint32_t(block[i * 4 + 2]) << 8 |
               uint32_t(block[i * 4 + 3]);
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kRound[i] + w[i];
        const uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

} // namespace

std::string sha256Hex(const void* data, size_t size) {
    Sha256 h;
    h.update(data, size);
    return h.hex();
}

void Sha256::update(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    total_ += size;
    while (size) {
        if (used_ == 0 && size >= 64) { // whole blocks straight from the input
            compress(h_, bytes);
            bytes += 64, size -= 64;
            continue;
        }
        const size_t n = std::min(size, 64 - used_);
        std::memcpy(block_ + used_, bytes, n);
        used_ += n, bytes += n, size -= n;
        if (used_ == 64) compress(h_, block_), used_ = 0;
    }
}

std::string Sha256::hex() {
    // Final block(s): the remaining bytes, 0x80, zeros, then the bit length (big-endian).
    uint8_t tail[128] = {};
    std::memcpy(tail, block_, used_);
    tail[used_] = 0x80;
    const size_t tailLen = used_ < 56 ? 64 : 128;
    const uint64_t bits = total_ * 8;
    for (int k = 0; k < 8; ++k) tail[tailLen - 1 - k] = uint8_t(bits >> (8 * k));
    compress(h_, tail);
    if (tailLen == 128) compress(h_, tail + 64);

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint32_t v : h_)
        for (int k = 28; k >= 0; k -= 4) out += hex[(v >> k) & 0xf];
    return out;
}

} // namespace sf
