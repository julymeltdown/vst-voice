# Original singer song 01

Development-only definition of the M1 lyric-song fixture. This is not a qualified
singer, a reviewed language resource, an approved recording or a release asset. No
audio, permission or quality approval is implied by its presence here.

## What this is

`recipe.json` is the canonical encoding of the original procedural singer the
M1 journey installs and sings with. It was written by the production encoder, not
by hand, so it is exactly the voice the code builds.

The song itself is defined in `tests/test_original_singer_song_journey.cpp`: a
48-note Japanese lyric phrase with consonants (including recipe-bound unvoiced /f/
in ふ and voiced /v/ in ゔ), unequal note durations, a real 0.5-second score rest,
and a sustained final vowel. At the default 120 BPM, its total authored duration is
41 seconds. The journey checks the middle 100 ms of the rest in the decoded master
and requires near-digital silence, rather than relying on a comment or placeholder
note to represent the gap. The test writes it through the application's own
add-note command rather than by editing a project file.

## Reproducing it

```sh
cmake --build build/release --target seam_original_singer_song_journey_tests -j 8
ctest --test-dir build/release --output-on-failure -R '^seam_original_singer_song_journey_tests$'
```

The test installs this singer, selects it, writes and tunes the song, and requires
undo, redo, save, reopen and export to reproduce the sound. The initial export also
requires both a non-silent stereo master and a finite, non-silent 48 kHz vocal stem
covering the same full 41-second score, plus no more than 250 ms of natural renderer
release tail. The one-vocal/no-backing fixture requires identical master and stem PCM,
and checks the durable receipt's file hash and frame/channel metadata. It fails if the
checked-in recipe no longer matches
the recipe the code builds, so the retained definition cannot quietly come to describe a
singer nobody rendered.

To regenerate the recipe after a deliberate change:

```sh
SEAM_SONG_FIXTURE_OUT=$PWD/assets/pilots/seam-song-01 \
  ./build/release/seam_original_singer_song_journey_tests
```

## What is not claimed

The recipe's resonance, frication, plosive and approximant parameters are development
screening values. They are not phonetic qualification for Japanese, and no listener has
judged this song. The absence of a listener decision is recorded, not hidden: this fixture
supports engineering evidence and workflow observation, not intelligibility, identity or
musical-quality claims.
