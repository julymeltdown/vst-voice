#!/usr/bin/env bash
set -euo pipefail
bundle="${1:?signed .clap bundle is required}"
identity="${APPLE_DEVELOPER_ID_APPLICATION:?APPLE_DEVELOPER_ID_APPLICATION is required}"
profile="${APPLE_NOTARY_PROFILE:?APPLE_NOTARY_PROFILE is required}"
case "$(uname -s)" in Darwin) ;; *) echo 'Apple signing must run on macOS' >&2; exit 2;; esac
codesign --force --deep --options runtime --timestamp --sign "$identity" "$bundle"
codesign --verify --deep --strict --verbose=2 "$bundle"
archive="${bundle%/}.zip"
ditto -c -k --keepParent "$bundle" "$archive"
xcrun notarytool submit "$archive" --keychain-profile "$profile" --wait
xcrun stapler staple "$bundle"
xcrun stapler validate "$bundle"
spctl --assess --type execute --verbose=4 "$bundle"
