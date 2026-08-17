#!/usr/bin/env bash
set -euo pipefail
# Builds VST3 and optionally AUv2 through the official MIT clap-wrapper.
# Required environment:
#   CLAP_WRAPPER_ROOT  checkout of free-audio/clap-wrapper v0.16.0
#   CLAP_SDK_ROOT      official CLAP SDK
#   VST3_SDK_ROOT      official MIT VST3 SDK
# Optional on macOS:
#   AUDIOUNIT_SDK_ROOT directory containing Apple's Apache-2.0 AudioUnitSDK
: "${CLAP_WRAPPER_ROOT:?missing CLAP_WRAPPER_ROOT}"
: "${CLAP_SDK_ROOT:?missing CLAP_SDK_ROOT}"
: "${VST3_SDK_ROOT:?missing VST3_SDK_ROOT}"
BUILD_DIR="${1:-build/phase11-wrapper}"
ARGS=(
  -S "$CLAP_WRAPPER_ROOT" -B "$BUILD_DIR"
  -DCLAP_SDK_ROOT="$CLAP_SDK_ROOT"
  -DVST3_SDK_ROOT="$VST3_SDK_ROOT"
  -DCLAP_WRAPPER_OUTPUT_NAME="ProjectSEAMEditor"
  -DCMAKE_BUILD_TYPE=Release
)
if [[ "$(uname -s)" == "Darwin" ]]; then
  ARGS+=( -DCLAP_WRAPPER_BUILD_AUV2=ON )
fi
cmake "${ARGS[@]}"
cmake --build "$BUILD_DIR" --config Release --parallel 2
