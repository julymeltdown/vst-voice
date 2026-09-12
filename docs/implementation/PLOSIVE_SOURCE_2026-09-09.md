# Closure-and-burst source primitive

`PlosiveSource` adds a finite source component for U20 stop articulation. It
produces exact zero PCM during an explicitly sized closure, followed by seeded,
band-shaped noise with a fast smooth attack, exponential decay and smooth
terminal release. It is not sustained frication with a stop label attached.

Configuration contains the existing bounded frication spectrum/seed/gain plus
explicit closure and burst frame counts. Closure must be nonempty, burst at
least three frames, total at most two seconds at the supplied 8–384 kHz rate,
and absolute time within the existing 2^52-frame bound. These are initial
engineering limits, not a final phonetic qualification or scope waiver.

The source is sequential and single-owner. Closure-only blocks do not advance
the burst generator. Whole and partitioned calls produce identical PCM; copies
are independent checkpoints. Reset restores both clocks/filter state. Invalid
requests or cancellation leave the original source state unchanged. Requests
past the finite gesture fail rather than sustaining noise indefinitely.

Three new tests cover exact closure, nonzero/decaying burst, finite output,
difference from sustained frication, chunk/checkpoint/reset equivalence,
cancellation, invalid dimensions/clocks/spectra and supported-rate extremes.

## Remaining integration

This is a DSP primitive only. Recipe persistence, phoneme bindings, articulation
planning, typed closure/burst markers, native controls and candidate production
must still connect to it. Existing stop labels do not become renderable merely
because this class exists. Actual stop distinctions and natural pronunciation
require per-phone settings and independent acoustic/listener evidence. No U20
or Full-Scope Beta GO acceptance is claimed.

The full Release build and **4/4 focused suites passed (28.04 seconds)**:
voice design, performance snapshots, export and core. `git diff --check` passed.
No new full 120-entry run or release acceptance is claimed. Logs are retained under
`evidence/plosive-source-2026-09-09/`. No prior snapshot files were missing against
`session-preservation-Cf2YAV`; existing dirty work was preserved. No staging,
commit, push, source approval or musical qualification occurred.
