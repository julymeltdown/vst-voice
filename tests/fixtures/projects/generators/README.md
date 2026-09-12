# Historical writer fixture provenance

These are newly generated test projects emitted by genuine historical SEAM codecs, not files saved in the past and not current JSON relabeled with an old schema number. The small drivers construct valid historical domain objects, call the historical writer, reload with that same codec and require exact equality before returning success.

| Fixture | Historical source commit | SHA-256 of emitted bytes |
| --- | --- | --- |
| `schema-1-historical-writer.seam` | `533eed00547ce4b11210fed6f7de89ef29887125` | `5a060eb76a32a840b59c3b60a7fad6df6c55f76e0e6354eb4ac4c9f7ebe89557` |
| `schema-4-historical-writer.seam` | `76fbc639b93f33591064cd9145c614215c3319bb` | `6fd852a76654c44c5dc9d7251e00bb159a68d39337c93bd4817c799f84a7a6c9` |
| `schema-5-historical-writer.seam` | `c6392bf6c7022c41eaf08784dbb880b4434e1110` | `277c870c34bde5602bea4f29c7cb23f126b944bba08c7372560fd109af4ee012` |
| `schema-6-historical-writer.seam` | `9973921e89d1fada01ba8bb4f8052b73ade63f66` | `ed56f3ea5895fcdc6eef963401c68f0bae57dc622e6d81e3e867d508dcea3083` |
| `schema-7-historical-writer.seam` | `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d` | `21038478e076003c29962ce54bafa463cfe8a33274c44602fb2875dcc3c4ffd7` |

Generated on 2026-09-06 with AppleClang 21, C++20, Release. Historical sources were exported into isolated temporary directories with `git archive`; no historical codec/domain source was modified. CMake configured with tests/benchmarks disabled and, for schema 4, native desktop disabled. Warnings-as-errors remained enabled.

Build the historical `seam_formats` target (which builds its domain/core dependencies). Compile the matching checked-in driver with historical core/domain/formats include directories and link, in order, `libseam_formats.a`, `libseam_domain.a`, `libseam_core.a`. Invoke the driver with the desired output filename. For example, from an isolated historical source checkout:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSEAM_BUILD_TESTS=OFF -DSEAM_BUILD_BENCHMARKS=OFF
cmake --build build --target seam_formats -j 4
c++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -I libs/seam-core/include -I libs/seam-domain/include -I libs/seam-formats/include /absolute/path/to/schema1.cpp build/libseam_formats.a build/libseam_domain.a build/libseam_core.a -o generate_fixture
./generate_fixture schema1.seam
```

Use `schema4.cpp` and add `-DSEAM_BUILD_NATIVE_DESKTOP=OFF` at configuration for schema 4. Compare output hashes above. The imported fixtures were byte-compared against generated outputs after adding them to the current checkout.

The historical schema-1 demo also built and round-tripped a 10,000-note project in the temporary directory. The schema-4 full demo target hit pre-existing implicit integer/float-conversion errors in historical WAV diagnostics under the current compiler; this did not affect the already-built unchanged codec/domain/core libraries used by the minimal driver. No warning was disabled and no WAV code was patched. This is serialization provenance, not historical release certification or audio-quality evidence.

Schemas 5 and 6 use `reemit.cpp`: their existing hand-authored `schema-5-audio-track.seam` and `schema-6-media-identity.seam` fixtures are input seeds, not asserted historical-writer outputs. The matching historical codec loads the seed, writes the new `*-historical-writer.seam` fixture with its original encoder, then loads its own output and verifies full domain equality. Invoke the driver as `./reemit /absolute/path/to/seed.seam /absolute/path/to/output.seam`. Both original codecs/domain/core libraries compiled unchanged with strict warnings. Their old CMake configurations require native desktop enabled when the default embedded editor is enabled; only `seam_formats` and its domain/core dependencies were built. An initial desktop-disabled configure rejection was resolved by enabling that existing option, not by modifying sources or gates.

Schema 7 uses the same `reemit.cpp` procedure and the original diagnostic melody corpus as its seed. The corpus itself is not rewritten. Its historical codec/domain/core libraries built unchanged at the commit above; the emitted output round-trips exactly through that codec. Existing Phase 2/3 demo outputs supply the remaining schema-2/3 migration evidence. None of these seed or emitted fixtures represents measured singing quality.
