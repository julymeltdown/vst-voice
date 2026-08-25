# Official Voicebank acceptance gate

Release is blocked until all items are approved:

1. signed voice-provider contract and rights review;
2. recording-session logs, microphone chain and calibration;
3. complete target-language unit inventory and pitch layers;
4. retake closure and marker/pitch-mark review;
5. Raw, PSOLA, Spectral and Stretch listening QA;
6. deterministic `.seambank` build, signature and installation receipt;
7. Character 01 marketing-rights review and performer/character separation statement;
8. product owner and legal approval.

The Phase 11 public-domain fixture is technically useful but cannot satisfy these gates.


Phase 13B implements the dossier and evidence validator for this gate. The checked-in baseline intentionally remains blocked and contains no signed performer, recording or legal evidence.

The closed External Beta uses a separate non-official gate in
`BETA_VOICEBANK_ACCEPTANCE.md`. A Beta bank must not use `official.voice.01`,
`official=true`, or Character 01 evidence, and neither gate can substitute for
the other.
