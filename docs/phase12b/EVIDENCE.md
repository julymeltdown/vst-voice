# Phase 12B Evidence

## Runtime evidence

The deterministic Phase 12B project contains two vocal tracks, three regions,
three rendered phrases, sixteen selected units, an explicit host-start offset,
direct Phoneme/Unit/Pitch edits and a four-channel master route.

```text
Schema                     5
Tracks                     2
Regions                    3
Phrases                    3
Units                     16
Output channels            4
Frames               148,988
Host start offset        960 ticks
Sample Microscope       enabled
```

The first-party dynamic CLAP host reports: GUI visible, four-channel port
configuration selected, one audio-port rescan, one configuration rescan,
offline render mode accepted, active state load rejected, inactive load
accepted and state round trip PASS.

## Verification

```text
Warnings-as-errors Debug build     PASS
Debug full CTest                    29/29 PASS
Release Phase 12B CTest              3/3 PASS
Named tests                        128/128 PASS
ASan + UBSan focused tests          PASS
ThreadSanitizer focused test        PASS
Linux/X11 dynamic CLAP host         PASS
Master-only policy                  PASS
Dependency/license audit            PASS
Phase 8/11/12A/12B contracts        PASS
```

## Artifact hashes

```text
dce2b90f71dfdb33c9d6409d033a03ace98fba6932bb6df142b5f17df7de243b  phase12b-multichannel.wav
15dc6e5715e56e54e374e32e643e5608806808a8f8c12c85ff3244d18f619008  phase12b-clap-host-4ch.wav
e40664fe1d44a27f9ee02c6d2801cfe8b92063e5a8f59310a53fcfc8cac421ac  ProjectSEAMEditor.clap
cb8fcc935d30c341e5571cfcd56970f0a65924cfd35f48d54d7afdc475af569a  phase12b-clap-editor.png
386d309ccf4478983668129dca66834ff59ebf7a8cc801da341925b69e4271b3  phase12b-spectrogram.png
```

Official `clap-validator`, Windows/macOS runtime certification, VST3/AU,
signing/notarization and Official Voicebank 01 are explicitly not represented
as PASS.
