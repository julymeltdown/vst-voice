# Procedural singing you can actually listen to

Date: 2026-10-03.

Everything measured about SEAM's procedural singer over the past several units was
measured inside the test harness. Nobody had heard it. This is how to produce the
audio, using a tool that already existed in the repository and had never been run
for this purpose.

## The tool

`seam_singer_pilot` renders the procedural route through the **production export
path** (`authoring::ExportService::exportSetWithSources` with
`includeProceduralCandidates`), not through a test-only shortcut. The WAVs it
produces are the same bytes a user's export would contain, and each run also
writes the project, the recipe, and the analyzer's pitch record beside the audio.

```sh
cmake --build build/release --target seam_singer_pilot
./build/release/seam_singer_pilot /tmp/seam_audition/<name>/out <mode>
```

`<mode>` is one of the built-in ladders, or `phrase` for a custom one:

```sh
./build/release/seam_singer_pilot OUT phrase 'LYRIC:MIDI[:TICKS]' ...   # 1-64 notes
```

Each run emits three recipes: `baseline`, `higher-formants` (all formant
frequencies scaled by 1.15) and `breathier` (aspiration 0.20, spectral tilt
-15 dB/octave). Comparing those three is the quickest way to hear what a recipe
parameter does.

**Lyrics must be kana.** `phrase` takes real Japanese syllables, so `あ` works and
ASCII `a` does not: a bare `a` is not a kana spelling, resolves to `pau`, and the
pilot refuses with "Phone 'pau' has no explicit frication or released-stop
source". This cost me one confused debugging cycle and is worth stating plainly.

## The listening set

Rendered on macOS, Release build at `9b7376b1`, all carrying real signal
(no silent files among these):

| material | command | duration | peak | RMS |
| --- | --- | --- | --- | --- |
| vowel ladder | `seam_singer_pilot OUT` | 1.50 s | 0.0913 | 0.0301 |
| articulation, 14 syllables | `seam_singer_pilot OUT articulation` | 3.50 s | 0.1439 | 0.0356 |
| nasals | `seam_singer_pilot OUT nasals` | 1.50 s | 0.1571 | 0.0585 |
| stops | `seam_singer_pilot OUT stops` | 1.50 s | 0.0612 | 0.0086 |
| affricates | `seam_singer_pilot OUT affricates` | 1.00 s | 0.0985 | 0.0260 |
| glides | `seam_singer_pilot OUT glides` | 1.00 s | 0.0285 | 0.0056 |
| four-note melisma | `phrase 'あ:60:960' 'あ:64:960' 'あ:68:960' 'あ:72:960'` | 2.00 s | 0.0220 | 0.0074 |
| five-syllable rise | `phrase 'さ:60:960' 'し:62:960' 'す:64:960' 'せ:65:960' 'そ:67:960'` | 2.50 s | 0.1009 | 0.0329 |

## The exported audio confirms the harness measurements

The four-note melisma was rendered through the export path and then measured with
`seam_voicebank_cli extract-pitch`, independently of the test that motivated it:

```
note 1 midi 60 expect  261.6 Hz  median  261.6 Hz    -0.2 cents
note 2 midi 64 expect  329.6 Hz  median  329.6 Hz    -0.1 cents
note 3 midi 68 expect  415.3 Hz  median  415.3 Hz    -0.0 cents
note 4 midi 72 expect  523.3 Hz  median  523.3 Hz    +0.2 cents
```

The same conclusion the unit tests reached inside the harness holds on the bytes a
user would actually receive. That is a real strengthening of the evidence, and it
is the first time the procedural path has been checked outside its own tests.

## What listening to this can and cannot settle

**It can.** Whether the articulation is intelligible, whether the pitch contour
reads as a phrase, whether the four consonant classes sound distinct, whether the
timbre is pleasant or merely correct, and whether the three recipe variants are
audibly different. These are exactly the judgements M2.1 asks for and no
measurement here can make.

**It cannot.** Anything about release quality. The recipe behind these renders is
experimental formant and source data, not a qualified voice design; the tool's
own help calls the output unqualified. A pleasant result here is evidence about
the synthesis approach, not about a shippable singer.

**Still missing.** These renders use only the procedural route. The sample route's
intonation remains unmeasurable for the reason recorded in
`LISTENING_PACKET_ROOT_MIDI_MISMATCH_2026-10-03.md`, and U42 stays externally
blocked on a rights-cleared bank whose units are actually at their declared pitch.
