// SculptForge - open source digital sculpting
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ed25519 signature checking (RFC 8032) and the SHA-512 it is built on, for the updater: a release's SHA256SUMS.txt
// is signed by the release workflow and the app checks it with a public key compiled in (core/SelfUpdate.h). Verify
// only: the app never signs. The field and group arithmetic is TweetNaCl's (public domain, Bernstein et al.), with
// RFC 8032's extra rule that S must be below the group order; tests/test_self_update.cpp runs RFC 8032's vectors.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sf {

void sha512(const void* data, size_t size, uint8_t out[64]);

// True when `signature` (R || S, 64 bytes) signs `message` under `publicKey` (32 bytes).
bool ed25519Verify(const uint8_t signature[64], const void* message, size_t size, const uint8_t publicKey[32]);

} // namespace sf
