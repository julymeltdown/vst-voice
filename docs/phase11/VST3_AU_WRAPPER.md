# VST3 and Audio Unit wrapper strategy

The canonical plug-in implementation is CLAP. VST3 and AUv2 are generated through `free-audio/clap-wrapper` v0.15.1 so state, GUI, note-event and render behavior are not reimplemented in a second DSP core.

Required source inputs:

- clap-wrapper v0.15.1 / release commit `35f524b`;
- official CLAP SDK, MIT;
- Steinberg VST3 SDK, MIT;
- Apple AudioUnitSDK, Apache-2.0, for AUv2;
- macOS system frameworks for AUv3 if later enabled.

Use `scripts/build_clap_wrappers.sh` on the target platform with immutable SDK checkouts. VST3 output must pass the Steinberg validator. AU output must pass `auval -v`. Until those target binaries and validator logs exist, the status is `SOURCE_READY`, not `HOST_VERIFIED`.
