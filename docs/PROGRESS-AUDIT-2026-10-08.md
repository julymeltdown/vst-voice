# SEAM current full-scope progress audit

Audited remote source: `00eb57e5d1177524619076833060907ebd6cc96c`.

## Method and boundaries

This is a 2026-10-08 source/evidence audit, not a new full runtime qualification. Existing requirements remain in scope. Tests, source delivery, external integration and final acceptance are separate. Historical test receipts apply only to their recorded revision/environment; this audit did not rerun those suites. No GitHub workflow was edited or manually executed.

Where a weighted estimate is provided, each original plan area has equal weight: locally accepted A=1, implemented but unaccepted I=0.8, partial P=0.45, unimplemented N=0. These weights are an explicit coarse **estimate**, not measured labor, probability, test coverage or remaining time. Formula: sum(row weights)/number of original areas. A partial row still retains every missing requirement. Small repairs do not automatically promote a broad row. Decimal arithmetic makes the calculation reproducible, not the judgment precise.

## Full-scope 48-unit reconciliation

The original U1–U48 scope is retained. Reclassification requires the entire unit exit criterion, not a repair count. Source checks confirm phoneme timing/performance, production repository, voice design, native editing, SMF/USTX codecs and neural worker paths. October import/replay/soak-guard changes fix real behavior but do not supply a qualified singer, human listening, installed host matrix or release archive. The full execution ledger explicitly retains these boundaries.

The initially inherited 6A/10I/29P/3N table was not a fully renewed classification. A later source/ledger reconciliation corrects U6, U7, U18 and U30 from I to P, and U15 from P to I. Current conservative result: **6A + 7I + 32P + 3N = 48**, so **(6 + 7×0.8 + 32×0.45)/48 = 26/48 ≈ 54% estimated implementation**. The source/ledger classifications remain judgments pending complete row-by-row exit-criterion traceability, not new acceptance. **6/48 = 12.5% previously locally accepted plan units**, with U29 restricted to POSIX; this is not whole-product qualification. U42/U43/U48 describe final resource/qualification/archive deliverables not accepted; supporting tooling already exists. At partial weights 0.25–0.65 the index ranges from about 41% to 68%.

Correction anchors: `docs/implementation/U6_PERFORMANCE_COMPILER_STATUS_2026-09-06.md:35` explicitly says incomplete; U7's status file line 3 also says incomplete; `U18_RECIPE_CAPABILITY_ADMISSION_2026-09-22.md:5` says the admission repair does not complete U18; the full execution ledger at lines 557 and 569 keeps U30 PARTIAL. The ledger at line 225 and source `libs/seam-voicebank/src/acoustic_analysis.cpp` / production `operations.cpp` establish U15's versioned acoustic analysis and conditioning, while manual re-derivation and approximately ±512-sample voicing boundaries remain. Those are implementation limits, not final acoustic acceptance.

U21 resume, staleness, cancellation and budget-exhaustion behavior already exists (ledger line 223); its remaining limits include campaign-wide byte accounting, unregistered pre-registry campaigns and human/reviewer evidence. U13 already requires current inspection evidence before Accept; its separate marker-only step remains absent (ledger line 248). U16 spectral/stretch edge handling remains partial. Branch-only changes are excluded until integrated.

| Unit | Original scope | Status | Original source owner / audit anchor |
|---|---|---|---|
| U1 | Reconcile baseline | A | `CMakeLists.txt`, `libs/seam-voicebank/src/wav.cpp` |
| U2 | Full-scope authority and matrix | A | `docs/product/`, `tools/external_beta/release_gate.py` |
| U3 | Musical vocabulary and migration | A | `libs/seam-domain/`, `libs/seam-formats/` |
| U4 | Pronunciation identity and reconciliation | A | `libs/seam-phonemizer/` |
| U5 | Ordered phoneme timing | A | `libs/seam-synthesis/src/timing_solver.cpp` |
| U6 | Complete performance compiler | P | `libs/seam-synthesis/` |
| U7 | Context-complete chunks and resources | P | `libs/seam-rendering/` |
| U8 | Capabilities, Raw and cache provenance | P | `libs/seam-synthesis/`, `libs/seam-rendering/` |
| U9 | Draft production and per-source provenance | P | `libs/seam-voicebank-production/` |
| U10 | Style-aware inventory identity | I | `tools/voicebank_script_generator/` |
| U11 | Canonical generation-safe writes | I | `libs/seam-voicebank-production/src/repository.cpp` |
| U12 | Inspection and applicable QC | P | `libs/seam-voicebank/src/validator.cpp` |
| U13 | Reviews, retakes and edit ownership | P | `libs/seam-voicebank-production/src/repository_operations.cpp` |
| U14 | Actual candidate publication | P | `libs/seam-voicebank-production/src/repository_export.cpp` |
| U15 | Acoustic analysis and conditioning | I | `libs/seam-voicebank/`, production `operations.cpp` |
| U16 | Qualified classical processing | P | `libs/seam-synthesis/` |
| U17 | Contextual selection and style blend | I | `libs/seam-synthesis/src/unit_selection.cpp` |
| U18 | Voice recipe persistence | P | New `libs/seam-voice-design/` |
| U19 | Stable phonation and vocal tract | P | New `libs/seam-voice-design/` |
| U20 | Procedural articulation and baking | P | New `libs/seam-voice-design/` |
| U21 | Resumable generation and CLI | P | Producer repository and voicebank CLI |
| U22 | Native Designer and real input | P | Native Studio and recording adapters |
| U23 | Musical editing commands | I | Piano roll, commands and tempo maps |
| U24 | Lyric productivity and hints | I | Lyric commands, IME and native menus |
| U25 | Expression and singer inspector | P | Native controller/scene/semantics |
| U26 | Japanese reading and context | P | Japanese phonemizer and dictionaries |
| U27 | English pronunciation | P | New English phonemizer |
| U28 | Korean pronunciation | P | New Korean phonemizer |
| U29 | Safe interchange boundary | A | New `libs/seam-interchange/` |
| U30 | USTX codec | P | New `libs/seam-interchange/` |
| U31 | SMF codec | I | New `libs/seam-interchange/` |
| U32 | Native conversion lifecycle | P | Authoring runtime, native menus/dialogs |
| U33 | Correct live expression | P | CLAP adapter and `phase12c` engine |
| U34 | Host authority and offline preparation | P | CLAP timeline/runtime/coordinator |
| U35 | Neural dataset and training pipeline | P | New `tools/voice_model_training/` |
| U36 | Model qualification and export | P | Model-production manifests and assets |
| U37 | Bounded native neural deployment | P | New neural adapter/helper |
| U38 | Automatic performance ownership | P | Application commands and neural proposals |
| U39 | Advanced expression algorithms | P | Synthesis controls and backend adapters |
| U40 | Takes, harmonies and native workflow | P | Application commands and inspector |
| U41 | Character performance | P | Character/identity/native presentation |
| U42 | Production singer resource set | N | Content manifests and production dossiers |
| U43 | Musical and creator qualification | N | Corpus tooling and private evidence |
| U44 | Finish preserved production U60 | P | Existing dirty support/crash work |
| U45 | Typed full-product semantic audit | P | New full-product validator and schemas |
| U46 | Restored audit and promotion integrity | P | Beta/public audit and operations |
| U47 | Signed installed platform/host acceptance | P | Packaging and existing evidence scripts |
| U48 | Final candidate audit and GO | N | Existing release audit/operations |

Additional inspected executable anchors: `libs/seam-synthesis/src/timing_solver.cpp`, `libs/seam-synthesis/src/unit_selection.cpp`, `libs/seam-interchange/src/{ustx_codec,smf_codec}.cpp`, `libs/seam-voice-design/src/voice_recipe.cpp`, `apps/seam-neural-worker/main.cpp`, and `tests/test_{ustx_interchange,smf_interchange,neural_render_workflow}.cpp`. These contain execution/validation behavior, not just types. The latest existing 00eb57e5 CTest record is 218/218 outer entries with 104 internal skips; an outer PASS does not erase internal skips and is not a feature-completion numerator. No new full CTest run was performed for this documentation audit.

Usable Alpha, External Beta, human listening/VoiceOver, signing, Windows, the nine host tuples, rights-cleared complete resources and independent qualification stay unaccepted unless their own ledger says otherwise. Windows remains TODO. The earlier delegated repair is idle; a separate native progress audit remains active as of the later 2026-10-08 observation. Its ongoing runtime results must not be treated as completed by this document. The old September report's elapsed-time forecast and ~90% pipeline opinion are not renewed by this audit.

Subsequent critical review: [final progress and completeness review](reviews/FINAL_PROGRESS_AND_COMPLETENESS_REVIEW_2026-10-08.md). It records the inherited classification corrections, branch-only implementation, listening-evidence defects, and fresh source/build verification. The revised 54% index above remains a judgment-based planning estimate, not a measured current completion rate.
