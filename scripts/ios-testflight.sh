#!/usr/bin/env bash
# SculptForge - open source digital sculpting
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds, signs and uploads the iPad app to TestFlight (docs/releasing.md "iPad / TestFlight"), with no third-party tools:
# xcodebuild archives with Xcode's cloud signing and exports straight to App Store Connect (destination "upload").
#
#   scripts/ios-testflight.sh --build N [--out DIR]
#
# Needs, in the environment (GitHub: the "testflight" environment's secrets, which only the owner adds):
#   ASC_KEY_ID, ASC_ISSUER_ID   the App Store Connect API key's id and its issuer id
#   ASC_KEY_P8                  the key itself (the .p8 file's text)
#   APPLE_TEAM_ID               the developer team
# None of them is ever printed: no `set -x` here, the key goes to a private temporary file removed on exit.
# Exit 3 when any is missing (the workflow's upload step is dormant until they exist).
set -euo pipefail
cd "$(dirname "$0")/.."

build= out=build-ios-testflight
while [ $# -gt 0 ]; do
  case $1 in
    --build) build=$2; shift ;;
    --out) out=$2; shift ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
[ -n "$build" ] || { echo "--build N is required (a number higher than the last upload's)" >&2; exit 2; }
for v in ASC_KEY_ID ASC_ISSUER_ID ASC_KEY_P8 APPLE_TEAM_ID; do
  [ -n "${!v:-}" ] || { echo "$v is not set: no TestFlight upload (docs/releasing.md \"iPad / TestFlight\")"; exit 3; }
done

keys=$(mktemp -d)
trap 'rm -rf "$keys"' EXIT
export SF_ASC_KEY_PATH="$keys/AuthKey_key.p8"
(umask 077 && printf '%s\n' "$ASC_KEY_P8" > "$SF_ASC_KEY_PATH")
unset ASC_KEY_P8 # (nothing below needs the text itself)

scripts/ios-build.sh device --signed --build "$build" --out "$out" >/dev/null

# The export: App Store Connect distribution, signed in the cloud, uploaded as it is written.
cat > "$keys/export.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>method</key><string>app-store-connect</string>
	<key>destination</key><string>upload</string>
	<key>signingStyle</key><string>automatic</string>
	<key>teamID</key><string>${APPLE_TEAM_ID}</string>
	<key>uploadSymbols</key><true/>
	<key>manageAppVersionAndBuildNumber</key><false/>
</dict>
</plist>
EOF
xcodebuild -exportArchive -archivePath "$out/SculptForge.xcarchive" -exportOptionsPlist "$keys/export.plist" \
  -exportPath "$out/export" -allowProvisioningUpdates -authenticationKeyPath "$SF_ASC_KEY_PATH" \
  -authenticationKeyID "$ASC_KEY_ID" -authenticationKeyIssuerID "$ASC_ISSUER_ID" -quiet
version=$(sed -nE 's/^project\(SculptForge VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
echo "Uploaded SculptForge $version ($build) to App Store Connect: TestFlight lists it once Apple has processed it."
