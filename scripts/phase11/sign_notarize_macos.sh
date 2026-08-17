#!/usr/bin/env bash
set -euo pipefail
BUNDLE="${1:?usage: sign_notarize_macos.sh ProjectSEAMEditor.clap}"
: "${APPLE_DEVELOPER_ID_APPLICATION:?missing APPLE_DEVELOPER_ID_APPLICATION}"
: "${APPLE_NOTARY_PROFILE:?missing APPLE_NOTARY_PROFILE}"
codesign --force --deep --options runtime --timestamp \
  --sign "$APPLE_DEVELOPER_ID_APPLICATION" "$BUNDLE"
codesign --verify --deep --strict --verbose=2 "$BUNDLE"
ARCHIVE="${BUNDLE%/}.notary.zip"
ditto -c -k --keepParent "$BUNDLE" "$ARCHIVE"
xcrun notarytool submit "$ARCHIVE" \
  --keychain-profile "$APPLE_NOTARY_PROFILE" --wait
xcrun stapler staple "$BUNDLE"
xcrun stapler validate "$BUNDLE"
spctl --assess --type execute --verbose=4 "$BUNDLE"
