# Original singer song 01

Development-only definition of the M1 lyric-song fixture. This is not a qualified
singer, a reviewed language resource, an approved recording or a release asset. No
audio, permission or quality approval is implied by its presence here.

## What this is

`recipe.json` is the canonical encoding of the original procedural singer the
M1 journey installs and sings with. It was written by the production encoder, not
by hand, so it is exactly the voice the code builds.

The song itself is defined in `tests/test_original_singer_song_journey.cpp`: a
24-note Japanese lyric phrase with consonants, a rest-bearing shape, unequal note
durations and a sustained final vowel, at a fixed tempo. The test writes it through
the application's own add-note command rather than by editing a project file.

## Reproducing it

```sh
cmake --build build/release --target seam_original_singer_song_journey_tests -j 8
ctest --test-dir build/release --output-on-failure -R '^seam_original_singer_song_journey_tests$'
```

The test installs this singer, selects it, writes and tunes the song, and requires
undo, redo, save, reopen and export to reproduce the sound. It fails if the checked-in
recipe no longer matches the recipe the code builds, so the retained definition cannot
quietly come to describe a singer nobody rendered.

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
