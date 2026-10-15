# English candidate-to-installed-bank lifecycle — 2026-10-15

## Result and scope

**Engineering PASS; External Beta BLOCKED.** A new native integration regression
connects the generated English producer workspace to an editable schema-4
manifest, independently reviewed engineering candidate, typed signed package,
trusted installation, freshly reopened dictionary score and actual Final export.
It covers both declared stress policies. Production library behavior is unchanged
in this unit; only CMake and the new regression change executable code.

The source base is `ebae2f88dce0c6c4347f397e76651cc9d88e3c48`. This exercises the
[bank contract](ENGLISH_BANK_PHONE_CONTRACT_2026-10-15.md) and
[producer propagation](ENGLISH_PRODUCER_MANIFEST_2026-10-15.md) together. Exact
independent review of this regression is pending; review of an earlier commit
does not approve it.

The fixture uses generated periodic tones, explicit mock actors and mock quality
decisions. No internet recording is imported, no singer is approved, and no
listening or physical application/host acceptance is claimed. The retained
female original-source census remains 1,429: 25 VocalSet singing recordings,
272 Merry Orks growls and 1,132 SLT speech recordings. The 312 derived SLT donors
are not additional originals. This unit adds zero source recordings.

## Code and executable path

`tests/test_english_candidate_lifecycle.cpp` registers **one native case**, which
iterates folded and distinguished policies. `CMakeLists.txt` defines the separate
`seam_english_candidate_lifecycle_tests` target, depends on the real voicebank
CLI and applies a 60-second CTest timeout. It links production, authoring,
distribution, rendering, application and typed candidate packaging code.

Each policy creates a normal generated `workspace/inventory.json` and
`workspace/producer` layout. Its deliberately reduced profile has one sustain
phone (`ay` or `ay1`), neutral style, MIDI layers 69 and 70, requested range 68–72
and one alternate. Both planned assignments are collected. This is two-unit
coverage of that reduced profile, not the full English inventory or a measured
singer range. The source WAVs are mono PCM16, 16 kHz, 0.2 seconds each, with
procedural frequencies 440 Hz and approximately 466.16 Hz.

| Stage | Actual implementation and assertion |
| --- | --- |
| Producer source and quality | Repository registration/import and real `inspect-source-quality` / `record-source-quality` CLI. Licence and quality files explicitly say synthetic/mock only. Fixture permissions and PASS records exercise mechanics, not real rights or listening. |
| Editable manifest | Real `create-sample-draft`: no missing reduced-profile assignments, actual 16 kHz rate, two units, preserved explicit contract, release eligibility false. |
| Review separation | Real `prepare-sample-review`; `review-sample` by producer refuses with exit 2; a distinct registered mock reviewer accepts. Candidate availability becomes true, release eligibility stays false. |
| Publication | Real `publish-sample` binds reviewed generation/project hash. Typed descriptor schema 3 references the schema-4 bank manifest and remains engineering / NOT_QUALIFIED / release-ineligible. |
| Packaging and installation | `packageResourceCandidate`, trusted-signer `verifyResourceCandidatePackage`, then `VoicebankInstallerService`. Installation is TrustedInstalled and retains exact manifest policy, content and package identities. |
| Changed authority refusal | Packaging refuses/no output, but the retained altered candidate includes `manifest.json.bak`; file-count refusal precedes the intended manifest integrity check. Its exact diagnostic was not captured, so this test does not isolate changed-contract refusal. |
| Reopen and dictionary | A newly created `.seam` score contains English `I` followed by `-`, MIDI 69/70, 960 ticks each, no phonetic hints. It is saved and reopened through the actual codec. A fresh session searches only its installed root. Both resolved tokens are `ay1`. |
| Final rendering/export | Production renderer: no diagnostics or fallback, finite non-silent PCM. Folded and distinguished audio is exactly equal, including continuation behavior. Actual Float32 master and stem export both equal rendered PCM. |

Before reopening, the test renames its own producer workspace, candidate directory,
original WAVs, licence and quality files to unavailable paths. The installed
payload includes its own provenance snapshots. The editable draft and the renamed
files remain on disk for inspection; this proves resolution using the configured
installed root, not a filesystem sandbox or a syscall-level no-access audit.

The signing key is a newly generated ephemeral **test Ed25519 bank-package key**.
Only its public key is retained. This is neither an approved production signing
identity nor Apple application signing, Developer ID or notarization evidence.

## Execution evidence and accurate geometry

The full dependent `cmake --build build/release -j6` completed before the checks.
The final build took 3.198105 seconds. Two final CTest executions passed: retained
capture mode in 1.173565 seconds (native case 0.694869 seconds), and ordinary
temporary-output mode in 0.692137 seconds (native case 0.674734 seconds).
Each execution reports one case; two policy iterations are not two separately
registered cases. Earlier pre-metadata capture/default passes are retained as
historical runs and are not attributed to the final source bytes.

Each captured policy has seven real CLI receipts: six normal exits and the
producer self-review refusal. Thus the final retained capture contains 14 CLI
receipts. Typed packaging, verification, installation, reopening and rendering
are native calls in the case, not extra CLI invocations.

All four final WAVs are byte-identical: Float32 stereo, 48 kHz, **48,000 frames,
96,000 interleaved values, one second**, 384,044 bytes including the WAV header.
RMS is `0.12437670475899483`. SHA-256 is
`5b0e4fef0a1adc8f8d9031f931c152155fd27ce813705822fc478590c012b8d7`.
A prior progress update incorrectly called this two seconds by treating
interleaved stereo values as frames; this report and the final native assertions
correct that error. No F0, acoustic tolerance, singer quality or duration-based
Beta acceptance is inferred from equality and RMS.

| Final capture | Folded | Distinguished |
| --- | --- | --- |
| Candidate SHA-256 | `3e504d477649b4beb7767c3e043a419396edbc7c91470033d17de79e5d150840` | `fbd26977850983e59c15fcf24ccf32651bd936ddad2a0326ab78b6c918609f92` |
| Content SHA-256 | `6db4cb88937ba25ec5399cb429df429412e0d48762f5e035a44c19a3db95009e` | `e64765636ed0cd2bf8c946bd3b3c6931acf711c6b24c922a2b66622a4b15a232` |
| Package SHA-256 | `8edf53a1b182f7bb8d88e109e6413f7f41da4d61830b0c4bf22260c613882214` | `f6ad3c84287ee9b9ed919e9303202e2866a6f6969139d6270d61e126d516ae1b` |

Candidate and package bytes vary across captures because paths/history and the
ephemeral signing keys vary. Repeated export equality does not imply deterministic
packages. Production encoding binds the policy into manifest/content/snapshot identities. This two-arm run also changes phone labels and unit IDs, so it does not isolate policy as the cause of identity differences even though tone audio remains equal.

## Retention and limits

The [evidence index](ENGLISH_INSTALLED_LIFECYCLE_2026-10-15.evidence.json) binds
`out/evidence/english-installed-lifecycle-2026-10-15`: 468 retained artifacts and
seven preserved pins. Both captured fixture trees, packages, public keys, installed
banks, provenance, scores, CLI receipts, exports, build/test receipts and failed
source versions are retained. Final CMake/test source snapshots and both executing
binaries are retained. Seven selected unchanged production sources bind to the
source-base Git blobs; this is not all transitive build inputs or authenticated
build provenance. The export's embedded application build SHA is the source base,
not a claim that the later documentation commit was compiled.

Two initial builds failed on test-only C++ errors: calling `empty()` on an optional
phonetic hint and assigning a shared PCM buffer directly to a vector. Exact source
versions and failed logs are preserved. Repairs use optional absence and iterator
copying; production code was not changed. The first passing test executable was
overwritten by the final frame-metadata rebuild and is not retained. Its hash and
receipts are historical, not a replayable retained binary. Ordinary-mode temporary
fixture outputs are removed by their destructor.

The read-only verifier checks artifact/pin hashes, source Git bindings, final
stable run inputs, command exit receipts, descriptor payload/evidence identities,
installed manifest policies and receipts, unavailable original paths, score
contents and independently decoded WAV geometry/equality. It does not execute
retained binaries or captured scripts or independently revalidate the package
signature cryptographically; native trusted verification/install occurred during
the retained test run. Frozen local evidence is integrity/consistency evidence,
not remote authentication or release acceptance.

## Next development and acceptance

This closes a focused installed English dictionary/continuation regression.
Remaining resource work is substantive: use the correctly attributed source
packet; bind original licence and archive provenance at import; resolve owner
policy and real producer/reviewer identities; assess and collect missing
phonetic, pitch and style coverage; validate source quality and classical/neural
resources. The SLT `jh` versus native `j` mismatch remains unapproved, `axr`
remains missing, and stress folding does not create missing singing recordings.
Internet recordings also do not meet AE1/R3 original-singer requirements.

After addressing independent review, run the complete immutable candidate checks
and the actual standalone/DAW/platform journeys with qualified resources. Full
product multilingual/classical/neural requirements, R9, signing/installation,
Windows and human acoustic/accessibility evidence remain open. The four canonical
gate/resource files are pinned unchanged: Usable Alpha **0/20 recorded passes**,
U45 **HELD**, External Beta **BLOCKED**, release eligibility **false**.


## 2026-10-15 — Exact independent lifecycle review

Independent reviewer **APPROVE exact `a0259814d7e61a7745dd7481ed86f7c404417baa`**, engineering scope only, **two P3 findings/no required changes**. The [updated evidence index](ENGLISH_INSTALLED_LIFECYCLE_2026-10-15.evidence.json) binds the actual retained final (`review-a0259814.md`, 16,604 bytes, SHA-256 `abcc5d00fc1c907aed82373f06ea7cb074bc6b419b7f3cebf5ad8acebf49d757`) in a separate frozen review root. Both the reviewer and the lead rechecked all 468 historical artifacts/7 pins. The reviewer read Git/source/artifacts/hashes/JSON/WAVs; no build, test, CLI/native binary or retained script/verifier was executed. The original lifecycle root is unchanged.

P3 findings remain open: (1) remove the stray backup or write without it, require the manifest-specific size/digest diagnostic and add removed-contract packaging coverage; (2) validate editable English contracts against the bound producer inventory before review capture and review commit. The second gap predates this test commit: a changed/missing declaration can enter review/publication as supplied, including silently successful cases. Exact post-review/package integrity checks do not close this earlier authority boundary. This corrects the earlier refusal interpretation; the positive preservation/render results remain valid.

Later commits are not approved by this verdict. Producer/layout P3 implementation and coverage P2 closure retain their own review status. Alpha 0/20, U45 HELD, Beta BLOCKED; mock signing/actors/rights/quality and one-word/one-vowel rendering provide no human, source, platform or full-product acceptance.
