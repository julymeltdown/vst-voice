#!/usr/bin/env bash
set -euo pipefail
: "${CLAP_WRAPPER_ROOT:?Path to audited free-audio/clap-wrapper checkout is required}"
: "${CLAP_SDK_ROOT:?Path to official CLAP SDK is required}"
: "${VST3_SDK_ROOT:?Path to Steinberg VST3 SDK is required}"
plugin="${1:?canonical ProjectSEAMEditor.clap is required}"
build="${2:-build/wrappers}"
plugin_dir="$(cd "$(dirname "$plugin")" && pwd)"
export CLAP_PATH="$plugin_dir"
args=(
  -S "$CLAP_WRAPPER_ROOT" -B "$build"
  -DCLAP_SDK_ROOT="$CLAP_SDK_ROOT"
  -DVST3_SDK_ROOT="$VST3_SDK_ROOT"
  -DCLAP_WRAPPER_OUTPUT_NAME=ProjectSEAMEditor
)
if [[ "$(uname -s)" == Darwin ]]; then
  : "${AUDIOUNIT_SDK_ROOT:?Path to Apple AudioUnitSDK is required on macOS}"
  args+=( -DAUDIOUNIT_SDK_ROOT="$AUDIOUNIT_SDK_ROOT" -DCLAP_WRAPPER_BUILD_AUV2=ON )
fi
cmake "${args[@]}"
cmake --build "$build" --config Release --parallel 2
