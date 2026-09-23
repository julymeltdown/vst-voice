# U34 loaded-CLAP Follow Host offset probe — 2026-09-24

## Scope

This is engineering evidence for the real, dynamically loaded CLAP module, not a
commercial-DAW or installed-product acceptance result. The test host uses only
the plug-in's public CLAP state, render, transport and process entry points;
it does not call `EditorRuntime` in place of the plug-in.

The probe in `apps/seam-clap-editor-host/main.cpp` loads a score with a
two-beat host placement. Its document tempo is 60 BPM, while the captured host
map starts at 120 BPM, changes to 60 BPM before the project placement, and to
240 BPM during the score. The host reports each beat through its transport,
then requests offline rendering after deactivation. The probe checks that the
first vocal onset lands at the host-map-derived time, not the document-map
offset, and that the earlier silence remains silent. It then changes a tempo
before project tick zero, delivers the new report on the owner thread, and
checks that the next offline callback errors with cleared output.

The same harness retains its cold complete-score, rate-change reprepare,
missing/failed Final, and beats-only rejection checks. Python assertions in
`tests/test_clap_offline_host.py` require both new Follow Host outcomes. The
summary deliberately keeps `followHostQualified=false` and
`releaseEligible=false`: an internal host is not one of the required DAWs.

## Local verification

On the current macOS checkout:

```text
cmake --build build/debug --target seam_clap_editor_host seam_clap_editor_plugin -j 6
ctest --test-dir build/debug -R '^seam_clap_offline_host_tests$' --repeat until-fail:5 --output-on-failure
  PASS, 5 consecutive runs
cmake --build build/sanitize --target seam_clap_editor_host seam_clap_editor_plugin -j 6
ctest --test-dir build/sanitize -R '^seam_clap_offline_host_tests$' --output-on-failure
  PASS, 1 sanitizer-backed run
```

One loaded-host run measured the expected first onset at 2.75 seconds,
zero energy in the preceding check window, positive energy at the onset, and
`followHostStaleRejected=true`. These are regression observations, not a
musical-quality measurement.

## Still required for U34/U47

- Observe the same preparation/abort behavior in each declared DAW/format tuple
  on exact installed binaries, including cold state reload and saved-project
  reopen. No supported DAW was available on this development machine for this
  checkpoint.
- Document or implement an explicit synchronization/freeze workflow wherever a
  host cannot provide enough transport history or declines the preparation
  request. The current conservative Follow Host path requires captured coverage
  from host beat zero through the end of the placed score.
- Keep signing, target-OS, full host matrix and release gates separate from this
  source-level engineering evidence. GitHub CI was explicitly deferred by the
  user and is not part of this checkpoint.
