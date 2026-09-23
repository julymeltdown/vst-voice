# OpenUtau interop oracle

A read-only checker that answers one question: **can a tool that is not SEAM open a
score SEAM wrote?**

It reads a file with [OpenUtau](https://github.com/openutau/OpenUtau)'s own
`Ustx.Load` path (deserialization, migration, `AfterLoad`, validation) and model
types for USTX, and with DryWetMidi, the library OpenUtau uses for MIDI. Nothing
in SEAM runs inside it, so agreement is external evidence rather than a
self-check of SEAM's reader against SEAM's writer.

Pinned reference: OpenUtau commit `8c0dc40`.

## This is not part of CI

OpenUtau and the .NET SDK are not dependencies of this repository. The oracle is
optional on purpose — an oracle that silently stops running because a dependency
moved is worse than one that is honestly absent.

What CI *does* enforce is the byte-level half of the same claim:
`tests/test_score_export_interop.py` re-exports the pinned corpus project and checks
that the exact positions, durations, tones and UTF-8 lyrics the oracle confirmed are
still present, and that the same conversion losses are still reported. If SEAM's
writer changes in a way that would break a real OpenUtau import, that test fails.

Run the oracle by hand when you want to re-establish the external half of the
evidence — for example after changing any file under `libs/seam-interchange/`.

## Setup

You need a .NET SDK (10.0 was used) and an OpenUtau checkout. Neither needs to be
system-wide:

```sh
# .NET, user-local, no sudo
curl -sSL https://dot.net/v1/dotnet-install.sh -o /tmp/dotnet-install.sh
bash /tmp/dotnet-install.sh --channel 10.0 --install-dir /tmp/seam-dotnet --no-path

# OpenUtau, pinned
git clone https://github.com/openutau/OpenUtau.git /tmp/OpenUtau-oracle
git -C /tmp/OpenUtau-oracle checkout 8c0dc40
```

Note that `brew install --cask dotnet-sdk` requires sudo; the install script above
does not.

## Build

```sh
cd tools/openutau_oracle
DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 \
  /tmp/seam-dotnet/dotnet build --artifacts-path /tmp/seam-openutau-oracle-artifacts \
  --nologo -v q -p:OpenUtauRoot=/tmp/OpenUtau-oracle
```

Keep build output outside the SEAM checkout: the tracked-source-closure audit
intentionally rejects generated DLLs under `tools/`. A fresh build of the pinned
upstream may emit its own compiler warnings; inspect errors separately.

## Run

Export a score from a real project first, so the oracle reads production output and
not a fixture:

```sh
seam_voicebank_cli export-score tests/singing_quality/corpus/original-melody.seam /tmp/out.ustx
seam_voicebank_cli export-score tests/singing_quality/corpus/original-melody.seam /tmp/out.mid

DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 \
  /tmp/seam-dotnet/dotnet /tmp/seam-openutau-oracle-artifacts/bin/seam_openutau_oracle/debug/seam_openutau_oracle.dll /tmp/out.ustx
DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 \
  /tmp/seam-dotnet/dotnet /tmp/seam-openutau-oracle-artifacts/bin/seam_openutau_oracle/debug/seam_openutau_oracle.dll /tmp/out.mid --midi
```

Success ends with `ORACLE_OK` (USTX) or `ORACLE_MIDI_OK` (MIDI).

For a version-specific serializer fixture, rebuild this checker against that
version's pinned `OpenUtau.Core` checkout and run
`seam_openutau_oracle.dll --emit-fixture NEW_FILE.ustx`. Use
`--emit-curve-fixture NEW_FILE.ustx` for the additional 64-point `dyn` curve
variant, or `--emit-multiline-fixture NEW_FILE.ustx` for a serializer-authored
folded multiline comment. It uses that assembly's
`UProject`, validation and YAML serializer, then creates a new UTF-8 file; it
never overwrites a path. The six checked-in fixtures and exact provenance are
documented in `tests/fixtures/ustx/README.md`. A .NET 10 build of the old
checkouts required distinct resource names in their temporary project files;
no OpenUtau serializer or model code was edited. Old transitive dependencies
are used only for this isolated fixture generation, not bundled with SEAM.
The tool allows a missing `UNote.tuning` field only for historical USTX 0.6
and 0.7; a missing field in 0.8 or later fails rather than silently weakening
the pitch comparison.

`--emit-hint-fixture NEW_FILE.ustx` uses the same source-model serializer but
sets the first lyric to `あ[k a]`. The pinned OpenUtau build emits this as an
unquoted plain scalar. The checked-in result and exact hash are documented in
`tests/fixtures/ustx/README.md`; it is not a GUI-save receipt.

`--compare-hint SOURCE.ustx ROUNDTRIP.ustx` invokes the pinned OpenUtau
`UNote.ToPhonemizerNote` method on both loaded files and compares the visible
lyric and extracted phone hint for every note in their single voice part.
It requires at least one hinted note. `HINT_COMPARE_OK` is evidence about
OpenUtau's phonemizer input, not about identical rendered audio or singer
availability.

To compare a source and SEAM-round-tripped USTX with OpenUtau's own authored
pitch sampler, run
`seam_openutau_oracle.dll --compare-pitch SOURCE.ustx ROUNDTRIP.ustx`.
It checks note positions/durations/tones/lyrics and samples nine in-note ticks
per note, excluding vibrato modulation. `PITCH_COMPARE_OK` requires no sampled
error above 0.5 cent; it is not a waveform or all-curve guarantee.

For a curve-bearing score, run
`seam_openutau_oracle.dll --compare-dynamics SOURCE.ustx ROUNDTRIP.ustx`.
This loads both files through OpenUtau and compares `dyn` using its own
`UCurve.Sample` at every five-tick render-grid position in the single voice
part. `DYNAMICS_COMPARE_OK` requires zero error in integer 0.1 dB units. It
does not prove equal audio, custom expression descriptors, other curve types,
or identical editing points between render-grid samples.

## Reading the output

- `midiTextHex` is the authoritative lyric check. `midiText` can show `???` if a
  console refuses UTF-8 output, which says nothing about the file.
- A nonzero `loss:` count on stderr from `export-score` is expected for USTX. SEAM
  singer identity, bus routing and smoothstep pitch have no USTX 0.9 representation,
  and the conversion names each one rather than dropping it silently.
- A **zero-loss** USTX report for a project that does carry those features would mean
  the conversion stopped disclosing its losses; treat that as a defect, not an
  improvement.

## What this does not prove

Passing the oracle means the pinned OpenUtau core can *load and validate* the
file. It does not establish the full desktop GUI workflow, identical rendering,
that SEAM's singer is usable in OpenUtau, or that a DAW will produce the same
audio. Real DAW exchange (REAPER, Bitwig) and listening judgement remain
separate, unverified steps.
