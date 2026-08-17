# Phase 12A Evidence

## Verified behavior

```text
Debug warnings-as-errors build         PASS
Debug CTest                            26/26 PASS
Release Phase 11/12A targets           PASS
Release focused CTest                   6/6 PASS
Core named tests                      128/128 PASS
ASan + UBSan Phase 11 tests            PASS
ASan + UBSan Phase 12A tests           PASS
ASan + UBSan production demo           PASS
ASan + UBSan named suite             128/128 PASS
ASan + UBSan dynamic CLAP host         PASS
ThreadSanitizer Phase 11 tests          PASS
ThreadSanitizer Phase 12A tests         PASS
Phase 12A source/rights contract        PASS
Master-only branch policy               PASS
Dependency/license audit                PASS
Linux CLAP exported symbol              clap_entry only
```

## Production preview evidence

```text
Voicebank ID          demo.public-domain.human.production
Voicebank version     0.12.0
Voicebank content     db651501b8cf7499a902e75378c94ea7b66bea2186dd146d0481c3030f7ca60c
Phrase count          1
Unit count            8
Requested renderers   Raw / Classic PSOLA / SpectralClassic / Stretch
Fallback count        0
Output frames         135,584
Sample rate           48,000 Hz
Cold cache hits       0
Warm cache hits       1
```

The public-domain fixture validator reports zero errors and sixteen expected
warnings: the same short human source is deliberately reused for eight
technical Units, so its crude loop boundary and declared root pitch are not
release-quality. Those warnings remain visible rather than being suppressed.

## Correctness tests

`seam_phase12a_tests` verifies:

- development fixture discovery and synthesis content hashing;
- exact ID/version/hash resolution;
- version, missing-hash and content-mismatch failures;
- trusted signed-install receipt recognition;
- preference for trusted installed content over a duplicate development copy;
- changed WAV bytes invalidating the saved exact reference;
- Project and `SEAMED11` state preserving the exact reference;
- direct `ProductionRegionRenderer` PCM equalling CLAP preview PCM;
- phrase-cache reuse;
- missing-bank silence;
- explicit exact relink restoring audio;
- no hidden Renderer fallback.

## Dynamic CLAP host regression

The first-party Linux host still validates GUI creation, state round trip,
active-state load rejection, live note input and audio capture after the
production-preview replacement. Voicebank resolution and phrase rendering run
on the non-real-time worker; the audio callback consumes only published PCM.
