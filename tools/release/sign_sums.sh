#!/usr/bin/env bash
# Signs DIST/SHA256SUMS.txt for one-click update (src/core/SelfUpdate.h): DIST/SHA256SUMS.txt.sig, a raw 64-byte
# Ed25519 signature, made with the private key in $UPDATE_SIGNING_KEY (a PEM; a secret of the environments release and
# member-release only, never the repository's). Used by release.yml at promotion, for public releases and member betas
# alike (the same key, the same check in the app); the staging builds never see it.
#   tools/release/sign_sums.sh DIST [SOURCE_DIR]    SOURCE_DIR: the checkout whose kUpdatePublicKeyHex must match
# No secret: a warning while the app has no key yet, an error once it has one.
set -euo pipefail
dist=$1
src=${2:-.}
pub=$(sed -n 's/.*kUpdatePublicKeyHex = "\([0-9a-f]*\)".*/\1/p' "$src/src/core/SelfUpdate.h")
if [ -z "${UPDATE_SIGNING_KEY:-}" ]; then
  [ -z "$pub" ] || { echo "::error::the app checks updates with key $pub but the UPDATE_SIGNING_KEY secret is not set"; exit 1; }
  echo "::warning::no UPDATE_SIGNING_KEY secret: no SHA256SUMS.txt.sig (one-click update stays off until the key exists)"
  exit 0
fi
umask 077
key=$(mktemp)
trap 'rm -f "$key" "$key.pub"' EXIT
printf '%s\n' "$UPDATE_SIGNING_KEY" > "$key"
# The secret must be the private half of the key compiled into the app: a mismatch fails here, not in the field.
have=$(openssl pkey -in "$key" -pubout -outform DER | tail -c 32 | od -An -tx1 -v | tr -d ' \n')
[ "$have" = "$pub" ] || { echo "::error::UPDATE_SIGNING_KEY's public key ($have) is not kUpdatePublicKeyHex (${pub:-empty})"; exit 1; }
openssl pkeyutl -sign -inkey "$key" -rawin -in "$dist/SHA256SUMS.txt" -out "$dist/SHA256SUMS.txt.sig"
openssl pkey -in "$key" -pubout -out "$key.pub"
openssl pkeyutl -verify -pubin -inkey "$key.pub" -rawin -in "$dist/SHA256SUMS.txt" -sigfile "$dist/SHA256SUMS.txt.sig" >/dev/null
[ "$(wc -c < "$dist/SHA256SUMS.txt.sig")" -eq 64 ]
chmod 644 "$dist/SHA256SUMS.txt.sig"
echo "signed SHA256SUMS.txt ($pub)"
