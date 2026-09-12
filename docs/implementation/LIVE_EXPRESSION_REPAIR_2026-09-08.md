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
