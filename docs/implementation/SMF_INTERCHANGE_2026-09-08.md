# U31 bounded SMF interchange checkpoint

Status: codec foundation implemented and locally verified; native conversion
review, USTX and external-DAW interoperability are still open.

`libs/seam-interchange/src/smf_codec.cpp` now parses Type 0/1 PPQ Standard MIDI
Files with pre-allocation chunk checks, four-byte VLQs, running status, FIFO
overlap pairing, tempo/meter/lyric/text retention and explicit bounded loss
records for unsupported events. SMPTE timing, malformed data, hostile lengths,
zero tempo and trailing bytes fail without changing caller data. Text payloads
are charged against the raw-byte budget; invalid UTF-8 or embedded-NUL text is
loss-reported and discarded while notes remain. A single trailing NUL on a
lyric is stripped with a warning unless that leaves an empty lyric, which is
discarded as a loss; a zero-length lyric is likewise discarded. Repeated
diagnostics aggregate per source track/category with a PPQ tick range. Legacy
encodings are not guessed. Project conversion maps
vocal lyrics only from `0x05`; lyrics encoded as ordinary `0x01` annotation text
are not interpreted as Soft Karaoke lyrics.
The parser now admits both completed and currently active notes against
`maximumNotes` before adding another note-on to its per-key queues; a hostile
file cannot use the larger event ceiling to retain more active notes than the
declared note budget. A regression sets a one-note limit and proves the second
note-on is refused before the later malformed track terminator is reached.
Type-1 track structure is retained explicitly: track order/name, note and text
membership survive decode/encode, while tempo and meter remain global. Legacy
non-UTF-8 track names are discarded with a loss instead of rejecting the
musical file. Project import creates one SEAM vocal track/region for each source
track containing notes; lyrics on a note-free track pair only when exactly one
note-bearing track makes the mapping unambiguous, and same-track lyrics take
priority over that fallback. Lyric-free and percussion tracks remain imported
but carry review warnings. Invalid UTF-8/NUL text is loss-reported and dropped;
a trailing lyric NUL is stripped and warned. Project-level track names are capped at 256 UTF-8
bytes with an explicit loss. Project export omits a NUL-bearing or over-budget
track name with an explicit loss rather than rejecting its musical events, and
omits NUL-bearing lyric text while preserving its note. The encoder produces
deterministic multi-track Type-1 PPQ output, keeps same-tick text insertion order,
places track-name metadata first, and validates every score field—including the
cumulative serialized wire-event ceiling—before allocating encoded event
records. The exporter refuses project PPQ outside SMF's 1..32767 range instead
of clamping without rescaling. Repeated-pitch nested overlaps and multiple
different lyric identities at one track onset are refused when SMF cannot
round-trip them unambiguously. The default project export emits the whole score:
one named Type-1 track per vocal track, with each region shifted to its absolute
project timeline. A separate overload remains available for an explicitly
selected track/region. Notes and lyrics are indexed per region during conversion
to avoid a note-by-lyric linear lookup for every note. Audio tracks and
SEAM-specific performance controls are reported as losses; lyric or name events
that exceed configured text/event limits are omitted only with explicit loss
records.

The focused `seam_smf_interchange_tests` target covers deterministic round-trip
notes/tempo/meter/text, track names and membership across codec and project
conversion, conductor-only tracks, duplicate names, separate and ambiguous lyric
tracks, same-tick lyric order, lyric-free percussion tracks, project-name bounds,
malformed comment/lyric payloads and terminated lyrics, equal-key overlaps,
running status, dangling-note diagnostics, truncation, SMPTE
rejection, malformed VLQs, byte limits and invalid-score immutability. The
focused suite also covers pre-retention active note admission against a smaller
note budget, exact/exceeded cumulative wire-event admission, PPQ boundaries,
nested repeated-pitch and same-onset lyric ambiguity, plus project-wide lyric
and note export. This is a reusable bounded codec, not yet an accepted import/export
workflow: U29's held-file conversion boundary, U30 USTX, U32 native conversion
review and real-DAW/OpenUtau verification of the current multi-track behavior
remain required before Beta GO.

## 2026-09-24 independent OpenUtau GUI MIDI round-trip

Evidence boundary: this GUI round-trip predates the 2026-09-24 Type-1
track-mapping implementation. The CLI executable used for it was built before
that codec change, so this receipt supports only the earlier single-track
shared-field path and is not verification of multi-track preservation. It must
not be used as the U31 track-mapping acceptance result.

The Release CLI exported the checked-in `tests/singing_quality/corpus/original-melody.seam`
to a temporary Type-1 MIDI file with zero reported issues. Its SHA-256 was
`6c94cd7575d8e95ae066e4bbd682aa349dec1806a347466b5e53b6819b0927eb` (1,331
bytes). The locally built pinned OpenUtau app at source commit
`8c0dc4007e6e8c8181f3a12c10205671800eeb8b` opened that MIDI through its normal
Open Project picker; the UI displayed the imported piano-roll notes. I saved the
result from the GUI as `/tmp/seam-fl-smf-iXqNlK/openutau-imported.ustx` (32,351
bytes, SHA-256
`344a2bdbc23ac1028f8021f3920a8e284ea854f7f43aa0a2a7bf6bc6c764e120`).

SEAM then imported that GUI-saved USTX and exported MIDI. Despite eight explicit
USTX subset losses (including unsupported track metadata and pitch
approximation), the MIDI output was byte-identical to the original: the same
1,331-byte size and SHA-256. This gives an independent OpenUtau desktop
open/save→SEAM import/export round-trip for the shared MIDI score fields. It is
not a real DAW host, a human-authored project, or musical audio qualification;
the local OpenUtau build reports `v0.0.0.0`, and its temporary output is not a
redistributable official binary. U31 remains incomplete until real-DAW exchange
and the remaining U31/U32 acceptance are established.

## 2026-09-24 current FL Studio MIDI exchange smoke test

The Release `seam_voicebank_cli` was rebuilt after the Type-1 mapping and export
edge-case changes, then exported the checked-in `original-melody.seam` through
the production interchange service. The input file was 1,355 bytes, Format 1
with one track at 960 PPQ, SHA-256
`774231a09739d0f27ad39a4984a380bf0b5429ae5ac128c4259f2eb7a4afb79f`, with zero
conversion issues. FL Studio 2025.2.5 (build 5055, Apple Silicon) opened the
temporary MIDI in a newly launched second app process; the UI displayed the
imported `Channel #1` pattern with note blocks. The original already-open
`Project_1.flp` process was not selected or modified.

The exact 1,355-byte export was also read by the repository's external oracle
built against pinned OpenUtau commit `8c0dc4007e6e8c8181f3a12c10205671800eeb8b`.
DryWetMidi reported Format 1, one track, 960 ticks per quarter, 80 notes, and 80
UTF-8 lyric events; the run ended `ORACLE_MIDI_OK`. Its hash is now the pinned
SMF expectation in `tests/test_score_export_interop.py`.

FL Studio exported `fl-studio-roundtrip.mid` in that isolated session (933
bytes, Format 1, four tracks, 96 PPQ; SHA-256
`06192250c4a82157605e2e70400fe270acc02ce7595d3a8c12c50813709c4c6c`). SEAM's
rebuilt CLI imported the returned file to a scratch `.seam` project: 80 notes
were retained on one note-bearing track and 39 explicit losses reported for
unsupported channel controls/program/pitch bend; a separate note-bearing track
without matched lyrics was explicitly warned. The project was then exported
through the production conversion service back to MIDI (Format 1, one selected
track at 960 PPQ, zero export issues; SHA-256
`7cbbbc46de9dbc582f0b77a9bbc30e7c8e5fd554bfb5282cde249a17ae4e3397`). This
demonstrates a current-source SEAM→FL→SEAM→MIDI return exchange for the tested
single selected region and its notes; it does not prove preservation of FL's
controller data, full-project multi-track export, or that the final SEAM MIDI
was reopened in FL. The UI bridge's attempt to launch a separate instance of
the final return file surfaced an unrelated open picker rooted at the user's
`Project_1` folder; it was canceled without opening or modifying that project.
The service now defaults to whole-project export: each vocal track becomes one
named Type-1 MIDI track, all regions are placed at absolute project ticks, and
tempo/meter remain global. A two-track service import→export→import regression
asserts track names, pitches, lyrics, tempo and meter timing; a partial
track/region selection is rejected. The earlier FL run remains a bounded
single-region return-exchange smoke test and does not establish real-DAW
multi-track GUI acceptance. Therefore this is not complete U31 acceptance.
GitHub CI remains deferred.
