// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// SHA-256 (FIPS 180-4), for checking downloads such as community gallery
// brushes against the hash their index publishes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sf {

// Lowercase hex digest of `size` bytes.
std::string sha256Hex(const void* data, size_t size);

// The same digest fed in pieces (downloads hashed as they arrive).
class Sha256 {
public:
    void update(const void* data, size_t size);
    std::string hex(); // ends the hash; the object is then spent
private:
    uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block_[64] = {};
    size_t used_ = 0;
    uint64_t total_ = 0;
};
inline std::string sha256Hex(const std::string& data) { return sha256Hex(data.data(), data.size()); }

} // namespace sf
