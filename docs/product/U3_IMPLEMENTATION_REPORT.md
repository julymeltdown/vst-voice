# U3 Voicebank Workflow Implementation Report

## Implemented

- `VoicebankBrowserModel` and rich `VoicebankCard` inventory.
- Trust ordering and explicit development-fixture policy.
- `VoicebankInstallerService` over the signed transactional installer.
- Exact track selection, exact relink, and explicit Undoable replacement.
- Expected/actual synthesis hash diagnostics.
- Voicebank inventory and region coverage analysis.
- Phrase-local render failure diagnostics with unaffected-project continuation.
- Standalone install/select/relink/replace/coverage operations.
- AppKit install and dynamic voicebank menus.
- AppKit exact voicebank relink-folder menu entry routed through the shared
  controller and exact ID/version/content-hash resolution.
- U3 contract verification and dedicated tests.

## Explicit limitations

- The native editor now paints the card-based browser and exposes exact-card
  selection through pointer and accessibility actions. AppKit menus remain a
  second entry point for the same application controller.
- Target macOS and Windows runtime execution remains mandatory before external
  Beta.
- The included public-domain production fixture is a technical bank, not a
  complete singing voicebank.
- Export, complete playback/status UI, and a rights-cleared usable demo bank are
  later Usable Alpha milestones.

## Verification evidence

```text
Warnings-as-errors Debug build           PASS
Named C++ tests                          205 / 205 PASS
Full CTest                                50 / 50 PASS
Dedicated U3 CTest                         3 / 3 PASS
ASan + UBSan U3 CTest                      3 / 3 PASS
ThreadSanitizer U3 CTest                   3 / 3 PASS
Linux/X11 standalone smoke                PASS
Master-only policy                        PASS
Dependency/license audit                  PASS
U3 source contract                        PASS
```

The Linux screenshot confirms that the production standalone still launches
with the exact development fixture when explicitly allowed. The card browser
and exact selection path are now shared by the native editor and application
controller; target runtime execution and a rights-cleared production bank
remain mandatory external gates.
