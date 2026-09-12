# Post-plosive full regression and explicit window sizing

## Full regression result

Ran all 120 configured Release CTest tests against the accumulated plosive,
Designer, app-bundle and density work: **119 passed, 1 failed**, in 136.91 seconds
with two test workers. The sole failure was `seam_tracked_source_closure`.

That gate reported **448 required inputs not indexed by Git**, including the new
Studio Info.plist and pre-existing implementation files. Its log contained no
missing-file records. This is a source-publication/reproducible-checkout failure,
not proof of deleted work and not a passing release gate. It remains unresolved;
the index was not changed and the verifier was not weakened to make it green.

The passing set includes producer import/ownership/staging/recovery, generation,
language/performance, scheduler/export, classical/neural protocol, host matrix,
allocation, and release-contract tests. These are engineering fixtures. Passing
them does not establish a qualified neural model, natural female character,
independently intelligible inventory, real host release matrix or public launch.

Original logs are retained under `evidence/post-plosive-full-2026-09-09/`.
The source commit remained `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d` plus the
preserved dirty worktree; that commit alone does not contain the tested changes.

## Follow-up repair after the full run

The live density check exposed a separate startup issue: explicit Studio
`--window-width`/`--window-height` options were overridden by saved macOS geometry
unless screenshot mode was used. Added a shared `restoreSavedFrame` policy,
defaulting true, and disabled it only when Studio receives explicit dimensions.
Ordinary interactive launches retain restoration; screenshot suppression remains
unchanged. Either width or height marks the request explicit.

After this repair the full Release build and aggregate-core suite passed
(21.68 seconds), including new option/policy assertions. The earlier full-suite
result predates this small repair; it is not presented as full verification of
the post-repair tree. A live launch without screenshot mode used explicit
1040-by-760 client dimensions rather than the previously saved compact window.
The UI tool displayed the corresponding larger footprint; its JPEG capture was
rescaled to 1009-by-768, so capture pixels were not treated as client dimensions.
The test process closed normally with physical input false and no recording.

## Next work and boundaries

Source closure requires coherent source publication before a clean checkout can
reproduce this build. Native onboarding, deeper Designer visual controls, broader
articulation and independently reviewed singer quality still remain required by
the original Full-Scope Beta GO plan. No whole-unit acceptance or product
completion percentage follows from the regression pass rate.

No staging, commit or push was performed. Recovery checkpoints preserve the local
work independently of conversation persistence.
