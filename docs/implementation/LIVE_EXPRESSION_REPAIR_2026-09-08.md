# U33 live-expression repair checkpoint

The Phase 12C live engine previously routed both CLAP vibrato and pan through
the timbre field and wrote one mono sum to every output channel. That made two
advertised controls functionally identical and made stereo pan impossible to
verify.

`EventType::Pan` and `EventType::Vibrato` are now distinct. CLAP note-expression
mapping sends each event to its matching field; MIDI CC10 updates channel pan,
and CC1 updates channel vibrato. Voices retain per-note pan/vibrato state, use a
bounded 5.5 Hz pitch LFO (up to 50 cents at full depth), and initialize pan from
the channel state on note-on. Stereo rendering uses equal-power left/right gains
and leaves additional channels on the bounded mono sum. Timbre adds a bounded
nonlinear harmonic term and brightness adds a bounded difference filter; pressure
remains the explicit scalar level control.

`tests/test_phase12c_clap_events.cpp` proves a hard-left event produces materially
more left than right energy, centered output remains symmetric while vibrato is
active, and the vibrato waveform differs from a no-vibrato baseline. Existing
allocation, event-overflow, resource-trust and live smoke tests continue to pass.
This is a realtime-safe source-level behavior repair; host-specific event
delivery, capability declarations, installed plugin tuples and independent
acoustic/listener qualification remain required for U33/Beta GO.

## 2026-09-24 repeated-key MIDI/CLAP addressing follow-up

MIDI 1.0 note-off was translated into a generic wildcard-ID note-off. With two
overlapping same-key MIDI notes, one release ended both; it could also release
a CLAP note on the same port/channel/key. This contradicted the per-event
release behavior a playing musician needs and crossed the MIDI/CLAP ownership
boundary. The CLAP [note-address specification](https://github.com/free-audio/clap/blob/main/include/clap/events.h)
defines wildcard tuple matching for CLAP events; MIDI 1.0 has no note ID and
requires a separate repeated-key policy in the engine.

Live voices now retain whether they originated from MIDI. A MIDI note-off,
including zero-velocity note-on, releases exactly one oldest still-held MIDI
voice for its channel/key on the supported port. It does not consume a
pedal-held voice twice and cannot release a CLAP-owned note. CLAP note
expressions, release and choke continue to use the full wildcard tuple.
The operation scans the fixed 32-voice array, with no audio-thread allocation.

A new mixed-event regression failed against the old binary at the first
post-release active-voice count (expected two, wildcard release left fewer).
After the repair, the focused CLAP/live target passed. Existing Phase 12C live
tests and the realtime allocation probe also passed (three Debug CTest targets
total). The Release CLAP plugin and these three targets built; their CTest
cases passed (3/3). The rebuilt Release aggregate passed 1,065 cases with
zero failures (one CTest target, 29.27 seconds). The focused target is a local
CMake/CTest target; no GitHub CI
configuration was changed. This is not evidence for real-host delivery or
listener quality, so U33 remains incomplete.
