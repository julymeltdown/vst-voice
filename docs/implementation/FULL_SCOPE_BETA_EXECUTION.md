# Full-Scope Beta GO Execution Evidence

This ledger records implementation evidence for [the approved plan](../plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md). It does not replace the plan or authorize release. The complete R1–R20/V01–V18 scope remains mandatory.

2026-10-01 — A ring that cannot say where the device is is not an empty ring (the second developer's CHANGES REQUESTED on `157dd58c`; a cross-cutting repair that completes and advances no plan unit), pushed in `7ecd7beb`; GitHub CI remains deferred and `.github` was not touched. **What was wrong.** The addendum on the previous entry has the review. `nextFramePosition()` gave the same answer, nothing, for an empty ring and for a reader that had used up its 1000 bounded attempts, and `audiblePlayhead()` took every nothing for an empty ring and went on from the feeder's playhead, which is ahead of the device by what the ring holds. On `157dd58c` the second developer's probe (a full 2048-frame ring, the device playing one frame and the feeder mixing one at every attempt) reported 2048 for a device at 1000 and a Pause then Play resumed at 2.048 s instead of 1.0 s; I reproduced it with their binary (0 passed, 2 failed). **What changed.** `SpscInterleavedAudioRingBuffer::nextFrame()` replaces `nextFramePosition()` and says one of four things from inside the snapshot query: Empty (the write count taken after the read count is not above it, so both were equal when the read count was taken), Placed (the coherent place of before), Unplaced (the head frame was written without a place) and Busy (every bounded attempt was invalidated); Unplaced and Busy have frames and no place and are not Empty, and nothing infers emptiness from the modulo indices. `audiblePlayhead()` returns an optional: Empty falls back to the feeder's playhead (or a queued command's), which is taken before the ring is asked, and Unplaced and Busy have no answer. `pause()`, `setLoop()` and `publishAudio()` return Conflict, and queue and record nothing, when they need the place and it has no answer; `reconfigure()` does the same after it has stopped the feeder's service, starts the service again and leaves the config, the queue and the carried state as they were. A refused publication is an owed one, and `AuthoringRuntime` retries what it owes on its own thread (read in the code; the new cases do not go through it). `state().audiblePlayhead` keeps the last confirmed place when there is no answer, so the playhead on screen stays where it was, and nothing that carries a position goes by it. The comment on the fallback no longer says that the playhead only moves forward: what holds, for a ring that was seen empty, is that the feeder's playhead is not later than the device in the order the audio is played, and a loop that wrapped puts it at a lower number. **Evidence.** Fail-first on `157dd58c`: the five new transport cases (the display stays at the last confirmed place; a pause, a loop change, a publication and a reconfigure are refused and change nothing, and asked again at rest each gets the exact place, PCM 1.0 for a device at 1000) all fail there; the sources were restored byte-identical by SHA-256 (`/tmp/seam-failfirst-QOAmDK/busy-failfirst-old-code.log`). The ring cases tell Empty, Unplaced and Busy apart (a head frame without a place is not an empty ring; a reader the consumer never leaves alone says Busy and not Empty). A mutation campaign detects 38 of 38: the ring R1–R10 and B1–B2 (including a reader that gave up saying empty, and a frame without a place saying empty), the feeder F1–F3, and the controller C1–C14 and B3–B11 (Unplaced and Busy answered with the feeder's playhead, each refusal ignored in `pause()`, `setLoop()`, `publishAudio()` and `reconfigure()`, the cached display value dropped or replaced by the feeder's, a refused reconfigure that does not start the service again); none failed to compile, and the sources were restored identical (`run_busy_campaign.sh`, `busy-campaign.log`). Debug transport 100, performance 56, render coordinator 36, characterization 18 and u2 97 pass; Release ctest 221 of 221. **Limits.** No device measurement was made for this change: a real consumer cannot move under 1000 consecutive attempts, so the cases are probes of the API, and the refusal is not something a person can reach with a real device. The editor's pause-and-reconfigure path stops the device before it asks the transport, so the consumer is quiescent there, and `restartAudio()` goes on ignoring the result of its maintenance pause. Two orderings still have no deterministic test and are argued in the headers only (the writer storing the write count before the index, and `audiblePlayhead()` taking the feeder's playhead before it asks the ring). No TSan run was made for this change; the second developer's partial TSan covered `157dd58c`, which this change edits in the same translation units. Nothing was heard by a person. **Open after this.** The second developer's review of `7ecd7beb`; then, in order, the `restartAudio()` stale-intent P2 with the four cases they named and the tail distinction, the CoreAudio stop-status hardening, the stale diagnostic-action P2 with their three probes, and a behaviour-level repaint regression test; then the rest of the UI-only journey on a fresh freeze (Space, Stop, ruler seek, loop, edit then play, the 30-minute session, relink on a copy of the bank). UA-001 to UA-020 stay NOT_RUN, the resource matrix stays UNRESOLVED, nothing here promotes Usable Alpha or External Beta, and Windows remains a README TODO. **Addendum (2026-10-01, after the second developer's re-review of `7ecd7beb`: APPROVED for the Busy/Unplaced-versus-Empty repair).** The P2 from `157dd58c` is closed and they found no new blocking finding in the diff; this is scoped implementation approval and not product or real-device acceptance. They rebuilt the six relevant implementation translation units and the tests from a pinned git-archive snapshot (no shared-source or build edits): 176 committed cases passed (transport 74, control-script 26, multichannel 8, playback 9, realtime 4, native-runtime 19, render-coordinator 36), and eight independent probes passed, the three earlier loop, tail and explicit-intent cases and five new ones: the exact 2048-frame, 1000-attempt schedule that failed on `157dd58c` now leaves the display at the confirmed 0 with the actual next frame at 1000, and a Pause returns Conflict and consumes no commands, after which a retry and a Play resume at PCM 1.0; with an Unplaced head, a pause, a loop change, a publication and a reconfigure are each refused without changing the recorded revision, config or intent, and succeed on retry; a queued explicit Seek to 500 bypasses the unknown old head and resumes at PCM 0.5. **Withdrawn:** the Limits paragraph above says a real consumer cannot move under 1000 consecutive attempts and that a person cannot reach the refusal with a real device. Nothing documents a bound on reader scheduling or preemption that establishes that. It is an adversarial probe whose incidence on a real device is unmeasured, and the refusal path handles it either way; the header comment that said the same is corrected in `52590a9e`. **A gap closed:** the playhead-before-ring ordering of the Empty fallback had no deterministic test. The second developer wrote one with the existing snapshot hook, without patching production (after the ring is drained, at the second snapshot point, after the counts were read, the feeder mixes 64 frames; the empty answer must be the playhead from before the query, 2048 there, although the producer is then at 2112), and showed that it fails a separately compiled mutant that reads the feeder's playhead again in the Empty case. It is committed in `52590a9e`, adapted to this suite's helpers (ring of 1024, answer 1024, producer at 1088); that mutant, as C15, is detected by this case alone, and the other ten audible-playhead cases pass with it. **Still open:** the writer storing the write count before the index has no deterministic test and is argued in the headers only; they did not invent a production hook for it. **Sanitizer evidence, as they report it:** the six implementation translation units plus the test wrappers and main were instrumented, and 140 cases plus the five probes passed without reports. Adding the render-coordinator test wrapper while its publication implementation stayed uninstrumented gave a repeatable CommandImpact/PublishedProjectAudio copy warning; with `render_coordinator.cpp` itself instrumented (including the publication's acquire, publish and refcount atomics) and no source change, all 36 coordinator cases passed and the case that had warned passed three more repeats, while the old mixed binary still warns. That is consistent with missing instrumentation of a synchronization point and is not a confirmed product race; the original mixed run must not be reported as clean, and none of this is a full-project TSan (the remaining support archives were uninstrumented, and I ran no TSan). **Not done by them:** they inspected the 38-mutant log and did not rerun it, did not run the 221-target suite, and made no device or listening measurement. Agreed order from here: the `restartAudio()` pending-intent and tail distinction, the CoreAudio stop-status hardening, then diagnostic action identity.

2026-10-01 — The ring says where in the audio its next frame is, so the creator's place is exact (the second developer's CHANGES REQUESTED on `8608c517`; a cross-cutting repair that completes and advances no plan unit), pushed in `157dd58c`; GitHub CI remains deferred and `.github` was not touched. **What was wrong.** The addendum on the previous entry has the review. The audible position of `8608c517` inverted the ring's two modulo indices and the loop. Loaded separately, the indices gave 961 for a creator at 64 when the device and the feeder moved between the loads; a loop that begins after the audio lost the unheard first pass (1536 for 0); a pause left out its Seek when the two positions it had sampled were equal while the feeder went on mixing (resume at 1.088 s for 1.024 s); and a settings change after the feeder had finished moved the consumer's position from 1000 to 3000. **What changed.** `SpscInterleavedAudioRingBuffer::writeFrames(samples, positions)` keeps the place in the audio of every frame, and the ring counts frames written and read in 64-bit counters that never wrap (`writeTotal_`, `readTotal_`). `nextFramePosition()` takes the read count, the write count, the place in the head frame's slot and the read count again, and starts again when the read count moved (at most 1000 attempts, then no answer): the consumer stores the read count before it gives the slot back through the index, and the producer stores a place before the write count, so an unchanged read count means the slot was not rewritten while the place was taken. A reset moves the read count in one step. The feeder writes `playhead + offset` for each frame it mixes, so a loop wrap, a loop that begins after the audio, a seek and the end of the audio are exact, and `rewoundPlayhead()` is gone. `audiblePlayhead()` is that place, or the feeder's (or a queued command's) playhead when the ring holds no frame or a reset waits. `pause()` and `setLoop()` put the Seek in their script whenever the feeder is playing and has audio; a feeder with no published audio sends none, because a script that grew from one command to two made the two `does not forget the play that is waiting for audio` cases overflow the 64-command queue they fill, and nothing can be mixed there. A feeder that is not playing seeks only for a loop change, so a pause leaves a finished feeder's tail to drain. `reconfigure()` carries the audible position whether or not the feeder is playing; whether playback resumes is still the feeder's playing flag. `TransportController::setStateSampleProbe` is now also called after `pause()` and `setLoop()` have chosen their position and before they send it, and `SpscInterleavedAudioRingBuffer::setSnapshotProbe` is called at three points of `nextFramePosition()`; both are test seams, empty in the product. **Evidence.** Fail-first on `8608c517` plus two temporary test-only probes (one between the index loads of `availableReadFrames()`, one at the end of `keepAudiblePosition()`; restored byte-identical by SHA-256): the ring-wrap cases (resume at 0.961 instead of 0.064), the loop that begins after the audio, the pause and the loop change with a block mixed after the sample (1.088 instead of 1.024) and the reconfigure after the tail all failed there, and pass now; the explicit-seek case passes on both and stays as a guard (`/tmp/seam-failfirst-QOAmDK/audible2-failfirst-old-code.log`). Eighteen cases were added and the two `rewound_playhead` cases removed: the ring (next place, slots coming round again, no place, a reset), a reader that the consumer and the producer move under at each of three points with 3 and with 8 frames, a reader that a reset moves under, a reader that gives up, the feeder (a loop that begins after the audio, the end of the audio, a seek), and the controller (the ring wrapping under the sample, with a pause; the loop that begins after the audio; a block mixed after the position was taken, for a pause and for a loop change; a reconfigure after the feeder finished; a loop change after the feeder finished; a loop the feeder has not applied; a seek the feeder has not applied). A mutation campaign detects 27 of 27: the ring R1–R10 (the reader without its recheck, `readFrames` not counting, a reset not moving the read count, an empty ring answering with the stale slot, a frame without a place answering with the marker, a reader that never gives up, the write count taken before the read count, the slot taken modulo the capacity, the writer's slot off by one, a block with too few places accepted), the feeder F1–F3 and the controller C1–C14; R3 and R8 first failed to compile under `-Werror` (an unused variable) and were rerun in a compiling form; the sources were restored identical (`run_audible2_campaign.sh`, `audible2-campaign.log`, `run_audible2_rerun.sh`, `audible2-campaign-rerun.log`). Debug transport 95, performance 56, render coordinator 36, characterization 18 and u2 97 pass; Release ctest 221 of 221. **On the real device** (a frozen build of tree `9d4cda7c`, whose code is `157dd58c`: binary SHA-256 `e6b1bb0d058a02e966d3ddfb1396f1be675b71933e8011556dc904ee4ed6c42a`, 13154448 bytes, arm64, ad-hoc, Info.plist `f29423e8…`, 53 files, bundle hash `6355bb48…` over `find -print0 | sort -z | shasum -a 256 | shasum -a 256`, project a copy of the pass-1 project `3398966d…`; every figure is attributed to the launched PID from the unified log). Five `--play` launches played 193024, 193536, 193024, 193536 and 193536 frames of a 192000-frame song. Through the UI, PID 56187: Play for 1.78 s, Pause, Play 1.5 s later and let it finish ran 86016 + 108032 = 194048 frames, 2048 over the song, against 193536 for `8608c517`. That is 512 frames, one device callback, more than the earlier build's figure and does not tell the two builds apart; it was not meant to, because the cases fixed here (an index wrap between two loads, a loop that begins after the audio, a block mixed between the sample and the send, a finished feeder's tail) are not what a Play-Pause-Play at 1.8 s into a song without a loop reaches. It shows that the build keeps the earlier behaviour on the device. **Limits.** Nothing was heard by a person: these are frame counts from coreaudiod, which say how long the output ran and not what it played. Two orderings have no deterministic test and are argued in the headers only: the writer storing the write count before the index, and `audiblePlayhead()` taking the feeder's playhead before it asks the ring. No TSan run was made. A change of settings after a feeder that has finished keeps the position and does not resume the tail; whether it should is the `restartAudio()` unit. One machine and one device (the built-in speakers). **Disclosure.** The frozen app (PID 56187) was quit with Cmd+Q through the UI driver; the driver then started the app again by its bundle path, without my launch arguments (PID 57552, 13:24:49), and I ended that one with SIGTERM. No file of `~/Library/Application Support`, `Preferences` or `Saved Application State` that mentions SEAM changed after 13:24. PIDs 74932 (the frozen `8608c517` app), 24286 (the pass-1 copy) and 92435 (an older release build) are still running and were not touched. **Open after this.** The second developer's review of `157dd58c`; then, in order, the `restartAudio()` stale-intent P2 with the four cases they named and the tail distinction, the CoreAudio stop-status hardening, the stale diagnostic-action P2 with their three probes, and a behaviour-level repaint regression test; then the rest of the UI-only journey on a fresh freeze (Space, Stop, ruler seek, loop, edit then play, the 30-minute session, relink on a copy of the bank). UA-001 to UA-020 stay NOT_RUN, the resource matrix stays UNRESOLVED, nothing here promotes Usable Alpha or External Beta, and Windows remains a README TODO. **Addendum (2026-10-01, after the second developer's review of `157dd58c`: CHANGES REQUESTED, one P2).** They accepted the slot-reuse ordering argument for a place that was read successfully, under the stated SPSC ownership, quiesced consumer handover, stopped-producer reset, no probe reassignment and non-overflow lifetime assumptions (a scoped argument, not a claim that every return path is linearizable), and the four earlier findings had supporting repair evidence. The P2: `audiblePlayhead()` took every `nullopt` from `nextFramePosition()` for an empty ring and went on from the feeder's playhead, but a reader that gives up after its bounded attempts has not seen an empty ring. With a 2048-frame ring, the feeder's service stopped and a probe that, at the third point of each attempt, makes the device play one frame and the feeder mix one, all 1000 attempts were invalidated while the ring stayed full: the device was at 1000, the position came out as 2048, and a Pause then Play resumed at 2.048 s instead of 1.0 s, skipping 1048 unheard frames (a valid interleaving of the API, not a measured incidence on a physical device). **The statement that the 1000-attempt fallback goes to the earlier-or-equal base position is false and is withdrawn:** it holds for a ring that was seen empty and not for one that could not be read, and the argument that the playhead only moves forward did not hold by number across a loop that wraps. Their direction: keep the bounded retry; tell Position, KnownEmpty and Unavailable (contention or missing metadata) apart inside the snapshot query; do not infer emptiness separately from the modulo indices; on Unavailable fail or defer without changing intent (or use a proven same-generation consumer position), and let the UI keep the last confirmed position. Their verification: 135 committed cases passed, and the same 135 under TSan on the six reviewed implementation translation units with the test wrappers (the frozen support archives were not instrumented, so this is a partial TSan and not a full-project certification); three loop, tail and explicit-seek probes pass; they read the 27 mutation logs and did not rerun the campaign or the 221-target suite. The fix is `7ecd7beb`.

2026-10-01 — Playback goes on from where the creator is, and not from where the feeder has mixed to (found by a real-device run of the frozen app; a cross-cutting repair that completes and advances no plan unit), pushed in `8608c517`; GitHub CI remains deferred and `.github` was not touched. **A correction first.** The pass-2 readings that the previous entry's open list promised for the frozen `d614e7f2` app (a tail cut at 3.36 s, a 53 ms replay at the end, a playhead reset to the start) are void as evidence about that build. The UI driver had been bound to the old pass-1 process (PID 24286, an older build) and the unified log attributes every audio event of that window to it; the frozen app (PID 67166) never started audio. The cause is a binding that was never checked against the process. Since then every UI-driven check ties its audio to the launched PID (`Project SEAM[pid]` `setPlayState` lines beside coreaudiod's `IO Stopped Context … after N frames`), and the driver is bound by bundle path and confirmed from the log after the first Play. The temporary tracing used while chasing this was reverted before any commit and is not in the repository; a build from the clean tree is byte-identical to the frozen binary (SHA-256 `6328a317193d28240e618494585b108dd2e6c15d80af4c5331ab73628555ac21`, 13153008 bytes, bundle hash `5e738dcb…`). **What was found.** With the PID attributed, that same frozen `d614e7f2` binary launched six times with `--play` played the 192000-frame song in full five times (193536, 195584, 194048, 193536 and 194048 frames) and once, on its cold first launch, 161792 frames: 0.63 s short. The shortfall is the feeder's ring (32768 frames in the app) less the five device blocks played before it happened. A render published just after Play replaced the timeline, and `TransportController::publishAudio()` put the playhead at `currentPoint().playhead`, which is where the feeder has mixed to, a ringful ahead of the device, and the replacement dropped the unplayed ring, so the audio between the two was never heard. The same defect made a Pause followed by a Play, a loop change and a change of audio settings skip a ringful: on the instrumented build of the previous code a pause at 1.8 s and a resume played 86528 + 76288 = 162816 frames of the 192000. **What changed.** `TransportState::audiblePlayhead` is the feeder's playhead less the audio the ring still holds, stepped back through a loop that has wrapped (new `rewoundPlayhead()`); where a queued seek (`QueuedIntent::positionIsExplicit`, carried across later commands the feeder has not applied) or a ring reset that the device has not answered (new `resetPending()`) has put the playhead, that is the answer. The editor's playhead shows it. `pause()` (when the point is playing), `setLoop()` (when playing), `publishAudio()` and `reconfigure()` put the playhead where the creator is; `pause()` adds a seek only when the audible position differs from the feeder's, so a pause that finds nothing playing leaves the tail to drain. `MultichannelPlaybackFeeder::feedOnce()` writes only the frames it mixed: it used to pad the last block with silence to a whole block, which put more in the ring than the playhead accounts for (`ControlScript::seeks()` is new, for the explicit-position record). **Evidence.** Four new cases failed before the change and pass after it, each resuming one ring ahead (frame 3136 to 3200 instead of 1000): a publication during playback, a pause then a play, a loop change during playback and a reconfigure during playback. Six more pin the audible position (what the device has played; after a pause; a seek the device has not answered; commands the feeder has not applied; a loop that has wrapped; the end of the audio with the tail still draining and a pause that must not cut it), four pin the helpers (two cases for `rewoundPlayhead()`, one each for `resetPending()` and `ControlScript::seeks()`), and one pins the feeder writing only what it mixed: 15 new cases in all, taking the transport target from 64 to 79. A mutation campaign detects 17 of 17 (the script and log are `/tmp/seam-failfirst-QOAmDK/run_audible_campaign.sh` and `audible-campaign.log`; the sources were restored identical). Two existing cases changed on purpose: `multichannel feeder stops after non-looping timeline end` pinned the padded block (32 became 20), and `what was asked is recorded afresh for the feeder that a reconfigure builds` paused without ever reading the ring, and now hears 200 frames first, because a pause puts the playhead where the audio was heard. Debug transport 79, performance 56, render coordinator 36, characterization 18 and u2 97 pass; Release ctest 221 of 221 (a first full run failed only the padded-block case above; `seam_tests` passed alone after the update and the other 220 had passed). **On the real device** (the frozen `8608c517` build: binary SHA-256 `a04cfb16197851b395ae6a6da4d5332ea0c9d77c19e15045d39bdd37c0263f4c`, 13153712 bytes, arm64, ad-hoc, Info.plist `a379d7a7…`, bundle hash `431d16ae…`, project a copy of the pass-1 project `3398966d…`; every figure is attributed to the launched PID from the unified log). Five `--play` launches played 193024, 192512, 193024, 193536 and 193024 frames (a little fewer than before, because the padded silent block is gone). Through the UI, PID 74932: the first Play ran 193024 frames; Play at the end replayed the whole song (194560 frames); and Play for 1.78 s, Pause, 1.9 s later Play again ran 85504 + 108032 = 193536 frames, so the resume replayed about 1.5k frames (32 ms) where the earlier code skipped about 29k. **Limits.** Nothing was heard by a person: these are frame counts from coreaudiod, which say how long the output ran and not what it played. The before-figure for pause and resume was taken on the instrumented build of the previous code, not on the frozen binary. The mid-playback re-render itself is shown at the transport layer, deterministically; on the device it was seen once, before the change, and not in the five launches after it, which does not show that it cannot recur. The audible estimate errs early by at most one feeder block, and a resume replays whatever the device played between the sample and its answer to the reset (about a callback or two). One machine and one device (the built-in speakers). The restartAudio stale-intent P2 and the CoreAudio stop-status hardening that the second developer opened are not in this unit. **Disclosure.** In this context the instrumented trace app (PID 90117) was ended with SIGTERM, and the frozen pass-2 launches were ended by the app's own `--auto-close-ms`; the previous context used `osascript` to read and answer system permission dialogs (Don't Allow on the stale Downloads-folder dialog of the killed PID 17983; Cancel and Escape on a Developer Tools access password dialog) and ended PIDs 17983 and 67166 with SIGTERM. The aborted `b0a268da` launch blocked on the Downloads-folder prompt because the bundle carries no `character-01` and the shell falls back to the source tree: launching with `SEAM_CHARACTER_ASSETS` avoids it (a separate finding, F-J16). **Open after this.** The second developer's review of `8608c517`; then, in order, the `restartAudio()` stale-intent P2 with the four cases they named, the CoreAudio stop-status hardening, the stale diagnostic-action P2 with their three probes, and a behaviour-level repaint regression test; then the rest of the UI-only journey on a fresh freeze (Space, Stop, ruler seek, loop, edit then play, the 30-minute session, relink on a copy of the bank). UA-001 to UA-020 stay NOT_RUN, the resource matrix stays UNRESOLVED, nothing here promotes Usable Alpha or External Beta, and Windows remains a README TODO. **Addendum (2026-10-01, after the second developer's review of `8608c517`: CHANGES REQUESTED).** They independently ran 119 committed focused cases (all passing) and probed the audible position with five failing assertions across four issues that this entry's evidence did not exercise. (1) The audible estimate was an inconsistent third-reader snapshot: `availableReadFrames()` loads the read index and then the write index, both modulo, and with a 1024-frame ring and 64-frame blocks a device that consumes 64 frames while the feeder writes 64 across the wrap between those two loads gave 63 unread for a ring that held 1024, so a creator at frame 64 was reported at 961 and Pause then Play resumed at 0.961 s instead of 0.064 s; the skew can be late by almost a ringful. **The statement in this entry that the audible estimate "errs early by at most one feeder block" is false and is withdrawn.** (2) In a loop that begins after the audio, the step-back assumed a wrap that had not happened: loop [512, 2048) with 1024 first-pass frames buffered and none played reported 1536, and Play resumed at 1.536 s, skipping the unheard intro. (3) A pause omitted its Seek when the two positions it had sampled were equal, but a playing feeder goes on mixing until it applies the script: with the ring empty and both positions at 1024, a block mixed after the sample was dropped and playback resumed at 1.088 s instead of 1.024 s. (4) A gap that also fails on `d614e7f2`: `reconfigure()` kept the consumer's position only `if (point.playing)`, so for a feeder that had finished (3000 frames, 1000 played, 2000 unread) a change of settings moved the carried position from 1000 to 3000 with playing false. They confirmed that a pending explicit Seek survives later loop and pause commands and expires on acknowledgement. Their order: fix this unit first (coherent monotonic accounting with reset validation, real per-frame history, an unconditional position for a playing point), then the `restartAudio()` stale intent including the tail distinction, the CoreAudio stop-status hardening and the diagnostic action identity, and re-measure the device on the resulting freeze with PID and window identity bound before each run. The fix is `157dd58c`.

2026-10-01 — The transport's settled snapshot is sampled in the right order, and a repaint can no longer send the transport a command (the second developer's P2 on `c2f377d0`; a cross-cutting repair that completes and advances no plan unit), pushed in `d614e7f2`; GitHub CI remains deferred and `.github` was not touched. **What was wrong.** The addendum on the previous entry has the review. `TransportController::state()` read `playing` and `playhead` and then the count of applied commands, against the order in which the feeder publishes them (state first, then the count with release), so a Pause the feeder applied between the reads came out as settled beside `playing=true`. The repaint policy then started the stopped device, and `startAudioForPlayback`, finding the transport not playing, sent it a new Play: a repaint undid the creator's Pause. **What changed.** `state()` reads the acknowledgement first, with acquire, then `playing`, then `playhead`, and `settled` uses that first read. `startAudioForPlayback` sends the transport no command: the editor's Play (through `AuthoringSession`), the audio restart (twice) and `--play` at startup each send their own Play before they call it, and the repaint calls it only for a transport that reports it is playing. `TransportController::setStateSampleProbe` is a test seam, empty in the product, called with the feeder before each of the three reads; with the feeder's service stopped (`shutdown()`), a test makes the feeder apply a command at any point of one sample. **Evidence.** One new case, `transport_controller_state_never_pairs_an_applied_pause_with_the_playing_flag_from_before_it`: play, wait until the report is settled and playing, stop the service, queue a Pause, and then have the feeder apply the Pause before each of the three reads (and once with no move during the sample), asserting that the report never says settled and playing together and that the next sample says settled and not playing. It fails on the old read order (checked fail-first) and passes now. The macOS source contract asserts that the body of `startAudioForPlayback` contains no `.play(`, `.pause(` or `.stop(`. A mutation campaign detects 4 of 4: the count read last, the count read between playing and the playhead, `settled` also trusting a late read of the count, and `settled` off by one (this one also fails the older settled case and the playback cases); the sources were restored identical, and the script and log are `/tmp/seam-failfirst-QOAmDK/run_b1b_campaign.sh` and `b1b-campaign.log`. Release ctest 221 of 221; Debug transport 64, performance 56, render coordinator 36, characterization 18 and u2 97 pass. **A measurement of the platform boundary the second developer left open.** `/tmp/seam-coreaudio-stop-probe/probe.cpp` (SHA-256 `523ec51aa7d4e1f5ee6d7b6efd7e4b5c84ed59dda794578e0b16293ce964ba0e`, not in the repository) opens the repository's real `createSystemAudioDevice()` (the CoreAudio DefaultOutput AudioUnit at 48 kHz with 256-frame blocks, device `coreaudio:93`, macOS 26.2) and runs start and stop cycles with the stop landing at random points of a callback that spends 1.5 ms inside itself. A first run of 300 cycles and a second of 1000 (2018 callbacks): 0 callbacks were found running after `stop()` returned, 0 entered afterwards, 7 stops overlapped a callback, and the average stop took 6.3 ms (worst 12.8 ms). That fits a synchronous `AudioOutputUnitStop` on this machine and this device, which the owner's `serviceResetRequest()` from the UI thread relies on. It is one machine and one device, so it supports the premise and does not prove it, and `coreaudio_audio_device.mm` still ignores the stop status. **Limits.** Deterministic evidence: nothing was heard, no DAW ran and there is no full-project TSan run. The probe seam is a test hook; the second developer was asked whether they would rather have a coherent published snapshot in the feeder. The audible-playhead skew (the feeder runs about a ring's depth ahead of what is heard), the pointer-refusal visibility and the stale diagnostic-action P2 are unchanged. **Open after this.** The second developer's review of `d614e7f2`; then the UI-only journey from playback on a fresh freeze of this build (pass 2), launched with the character package taken from `SEAM_CHARACTER_ASSETS` so that the new binary does not ask for access to the Downloads folder (the bundle carries no `character-01`, and the fallback is the source tree there); then the stale diagnostic-action P2 and whatever the journey shows first. Nothing here promotes Usable Alpha or External Beta, and Windows remains a README TODO. **Addendum (2026-10-01, after the second developer's review of `d614e7f2`).** They approved `d614e7f2` as a scoped code approval for closing the state-sampling P2, not as playback or product acceptance; their original forced interleaving now passes (acknowledgement observed, device stopped, one start). They opened three units, none of them in `d614e7f2`: (1) a pre-existing P2 in `NativeEditorApp::restartAudio()`, which captures `state().playing` without honouring queued intent, so a Pause that was accepted and not yet applied, followed by a block-size change, resumes playback (it reproduces identically on `d614e7f2` and `c2f377d0`; the fix is to capture the effective requested intent, queued commands included, before the maintenance pause and to restore it on success and on rollback, and not `settled && playing`, which would drop a pending Play; the cases are a pending Pause, a pending Play, a settled Pause and a failed-open rollback); (2) CoreAudio stop-status hardening: `IAudioDevice::stop()` is `void noexcept` and `coreaudio_audio_device.mm` ignores the status of `AudioOutputUnitStop`, so a failure should be reported as an explicit failed or unknown outcome and propagated, the device must not be declared stopped nor the ring-consumer role transferred, a successful stop boundary is required before reset servicing or resource replacement, and the tests inject the failure; (3) two minor requests: document the state-sample probe as non-throwing and non-reentrant and keep it unset in production, and add a behaviour-level repaint regression test through `NativeEditorApp` beside the source check. **Wording correction.** The probe's "7 stops overlapped a callback" is a lower bound: the count of stops that found a callback entering, not a total of overlaps.

2026-10-01 — Play now restarts after a playback ends, a song is heard to its last sample, and the audio device starts on audio that follows the creator's last command (B1 from the live journey; a cross-cutting repair that completes and advances no plan unit), pushed in `c2f377d0`; GitHub CI remains deferred and `.github` was not touched. **What was wrong.** The exploratory UI-only pass (previous entry) found that after the first playback ended, every Play press blocked the UI thread for about two seconds, started no sound and showed no message. The cause in that entry was a source-level hypothesis. It is now proved through the real `NativeEditorApp` with a test device that takes blocks from the editor's real audio processor only when the test asks (`tests/test_standalone_playback_cycle.cpp`), on the unfixed code `99439590` plus the new tests. The first playback delivered 112,128 of 144,000 frames, so 31,872 frames (0.664 s, within a few hundred frames of the ring's 32,768-frame capacity) of the end were never played, which matches the 0.65 s inferred from the CoreAudio log. Play pressed at the end replayed 768 frames of the old end instead of the song. A seek to the start and Play heard nothing (the device was stopped by the paint before the feeder had applied the Play; that is inferred from the repair, not separately instrumented). A probe that replayed the journey with the paint continuing between steps (end, Play at the end, seek to the start, Play) showed the last press taking 2003.8 ms, reporting success, never starting the device, leaving the ring holding 256 stale frames, with `resetWaits` at 6187 and the only message in `lastError_`: "Audio playback did not reach its startup buffer before timeout". **Cause.** The feeder asks the consumer to drop what the ring holds whenever it applies a seek, a play, a loop or a timeline, and writes nothing until the consumer has acknowledged. The standalone stopped the device with audio left in the ring and then waited on the UI thread, for up to two seconds, for the ring to refill before starting it: a wait that cannot end, because only the device that is stopped could answer. Around that, the paint stopped the device as soon as the feeder said it had finished, which cut off as much of the end as the ring holds, and it acted on a report that did not yet include the creator's last command (so it stopped a Play that had not been applied yet); Play at the end of the audio ended in the same instant; and `AuthoringSession::configureController` discarded the failure of the host's start of the device. **What changed.** `SpscInterleavedAudioRingBuffer::serviceResetRequest()` is the consumer's half of a reset, for the owner of a consumer that is not running (`consumeResetRequest` now uses it). `TransportController::awaitStartBuffer()` waits for the feeder to apply every command it was sent, answers the clear in the stopped device's place, and waits for the start buffer, or returns at once when the feeder is not playing and so will write nothing more; `TransportState::settled` says whether the feeder has applied everything it was sent; `play()` at the end of the audio rewinds to frame 0. The standalone starts the device through `awaitStartBuffer`, and the paint decides what to do about the device through `decideDeviceAction` (`playback_device_policy.hpp`): stop only once the ring has been played out, and neither stop nor start on a report that is not settled. `AuthoringSession` now returns the host's failure to start the device to the controller, so a key press is shown a notice and the transport is left paused. The macOS source contract asserted the removed wait loop and now asserts the call to `awaitStartBuffer`. **Decisions.** Play at the end of the song plays it again from the beginning (frame 0) and does not stay silent; a loop never stands at the end, and a play that waits for audio is left armed. The device stays up while the ring plays out, so the end of the song is heard in full, and the transport reports `playing` false for up to the ring's depth before the last sample is heard (the button already shows Play). **Evidence.** 17 new cases. In `tests/test_standalone_playback_cycle.cpp`: a song is heard to its last sample (what the device played is compared frame by frame with the published render, first callback fully served, silence shorter than a block after it); Play at the end plays it again, twice; a seek to the start and Play after the end; a pause, a seek and Play with no frame from before the seek heard; a Play the device cannot start is returned to the controller and leaves the transport paused with the audio-unavailable diagnostic; and four cases of the device policy. In `tests/test_transport_controller.cpp`: the rewind, no rewind away from the end, `settled`, the stopped consumer's clear answered (the feeder is parked until then), the wait for unapplied commands, no audio, and a feeder that is not playing. In `tests/test_standalone_realtime_contract.cpp`: the ring's stand-in answer. The four playback cases fail on the unfixed code, with the numbers above. A mutation campaign over the new code (rewind, the clear's answer, the applied-commands gate, the settled flag, the not-playing escape, the ring's three behaviours, the error propagation, three policy guards, the paint's stop and the removal of the wait) detects 14 of 14: three mutants first did not compile under `-Werror` and were redone as compilable ones, and one survived, removing the wait from `startAudioForPlayback`, which showed that no test noticed a device started on an empty ring; the first callback is now asserted to be fully served, and that mutant is detected. Release ctest was 220 of 221 in the full sweep; the one failure was the macOS source contract described above, which passes after the update. Debug: transport 63, performance 56, render coordinator 36, characterization 18 and u2 97 pass. Scripts and logs: `/tmp/seam-failfirst-QOAmDK/run_b1_campaign.sh`, `run_b1_campaign2.sh`, `b1-campaign.log`, `b1-campaign2.log`. **Limits.** This is deterministic-device evidence: the device is the test's, nothing was heard, no real CoreAudio device or DAW ran, and there is no ThreadSanitizer run; the owner answers the ring's reset from the UI thread on the premise that `AudioOutputUnitStop` has returned (synchronous by Apple's contract, not separately tested). The feeder still runs about a ring's depth (32768 frames, 0.68 s) ahead of what is heard, because the feeder service's control step is `feedOnce()`, which also writes a block every millisecond while the ring is above the watermark, so the ring stays nearly full instead of at the 8192-frame watermark. Source-derived, not yet measured as a live symptom: the playhead leads the sound by about that much, a pause then Play skips that much (a test of a failed start showed the transport left paused with the playhead not at 0, where the feeder had run to; how far was not measured), and a re-render published during playback jumps ahead by that much. Pointer presses that fail are still only recorded; key presses show a notice. **Open after this.** Re-freeze and rebuild the app from this commit and re-run the UI-only journey from playback (play, pause, stop, seek, loop, edit then play) with a window capture per step; then missing-bank detection and relink on a copy of the bank, the 30-minute session, the SCENE look and the Mix and Voice workspaces, and the creator listening checklist, kept separate from automation. The second developer's P2 (stale diagnostic action ids) is next after the journey shows whether playback has another blocker; the ring depth and playhead lead are the next candidate if it does. The second developer was asked to review `c2f377d0`. Nothing here promotes Usable Alpha or External Beta, and Windows remains a README TODO. **Addendum (2026-10-01, after the second developer's final scoped review of `c2f377d0`, the fix in `d614e7f2`, and two corrections).** Verdict: CHANGES REQUESTED, one P2, so this unit is not approved until they have reviewed the fix. **P2 — the settled flag was sampled after the fields it certifies.** `TransportController::state()` read `playing` and `playhead` and only then `acknowledgedCommands` (`transport_controller.cpp` about lines 561 to 570). The feeder publishes its state and then release-stores the count (`multichannel_playback.cpp` lines 274 to 277), so an acquire read of the count taken last cannot make the earlier reads include the command it acknowledges. Their forced interleaving: the toolbar Pause stops the pumped device; a repaint samples the old `playing=true`; the feeder applies, publishes and acknowledges the Pause; the repaint then samples `settled=true`; `decideDeviceAction` returns Start; `startAudioForPlayback` sees `playing=false` and sends a new Play. On the pinned code their probe through `NativeEditorApp` (temporary scheduling hooks only) printed ack=1, device running=1, transport playing=1, starts=2: a repaint undid an acknowledged Pause. A diagnostic counterfactual that loads `settled` before `playing` passes the identical probe (ack=1, running=0, playing=0, starts=1). They noted that start-on-repaint predates `c2f377d0`: the finding is that the new settled guard's guarantee was incomplete, not that every Pause-resurrection symptom began there. They asked for deterministic regression coverage of the acknowledgement advancing during state sampling, not only static policy inputs or eventual settlement, and named two acceptable shapes: acquire the count first, or a coherent published snapshot. **Their other conclusions.** No other confirmed in-scope defect in replay-at-end, lock ordering or the reset-epoch handoff, assuming an exclusive, quiesced consumer. The audible-playhead skew, the pointer-refusal visibility and the earlier diagnostic-identity P2 stay outside this unit. Real CoreAudio stop quiescence stays a platform-boundary caveat: `coreaudio_audio_device.mm` lines 233 to 237 ignore the status of `AudioOutputUnitStop` and then report stopped, and neither the pumped-device tests nor a partial TSan run establish that no physical callback is in flight; it is a caveat and not a reproduced failure. **Their verification.** 49 committed transport, 9 playback and policy and 4 realtime cases pass; the same 62 cases pass with six implementation translation units and the test wrappers freshly TSan-instrumented, with no reports, while the support static archives were taken from the existing Release build and were not instrumented (so this is neither a full-project TSan run nor a clean full CMake build); the changed macOS runtime-probe source-contract test passes. Probe artifacts are in `/tmp/seam-review-c2f3-hBKeNY/` (`review_state_window.cpp`, `instrumented_transport_controller.cpp`, `instrumented_multichannel_playback.cpp`, `ordered_transport_controller.cpp`, and the binaries `review_state_window` and `review_state_window_ordered`, filter `reviewC2f3:`). They changed nothing in the repository or the shared build. **Corrections to this entry.** The tail the first playback lost is 144,000 minus 112,128 = 31,872 frames against a ring capacity of 32,768, so the gap is 896 frames and not "within a few hundred"; and the comment in the test calls the render "about two seconds" where 144,000 frames at 48 kHz are 3.0 s (the comment itself is not yet corrected). **Resolution.** The P2 is fixed in `d614e7f2`: `state()` reads the acknowledgement first, `startAudioForPlayback` no longer sends the transport a command, and a deterministic case makes the feeder apply the Pause before each of the three reads; see the entry for that commit. Nothing here promotes Usable Alpha or External Beta.

2026-10-01 — Exploratory macOS UI-only singer journey, pass 1, on the frozen `99439590` build: authoring, saving, reopening, exporting and crash recovery work through the window, and Play does not restart after the first playback ends (B1); this is exploratory evidence that advances no plan unit and promotes no gate, and GitHub CI remains deferred with `.github` untouched. **What was driven.** Source `99439590e1c6ffa636b6873cc95fd26d1ecb3e65` (committed 2026-10-01 08:58:11 +0900; the Info.plist `ProjectSEAMSourceCommit` matches; version 0.13.1). Binary `build/release/Project SEAM.app/Contents/MacOS/Project SEAM`: arm64, ad-hoc linker-signed, 13,152,368 bytes, built 08:59:40, SHA-256 `7cc6cc97481a9acfcd6001871bbd6a294a95370d09a0a2cb28cfa729055dc101`; Info.plist SHA-256 `864403ed9ec2a48079adb63198ecaf81a09cc590a4ba85f92650360236671351`; bundle file-list hash `13c9eff0dce11d9d53f17b5390b027ecc4eb49bec36d7d7822e549423423685c`; the copy that was launched (`/tmp/seam-journey.vndqH6/app/`) has the same binary hash. Singer: the technical fixture bank `demo.public-domain.human.production` 0.12.0 (manifest SHA-256 `ea199788012edc818459a00504a6043829560b22abe46efd4572fe886df973a5`, `audio/human-vowel-demo.wav` SHA-256 `caf8ceb04b864c7501371a2adae46697495e617b9ade178560ba2ea1aa6b8cb9`, content hash `db651501b8cf7499a902e75378c94ea7b66bea2186dd146d0481c3030f7ca60c` as bound by the projects): eight units (k o, e, o, ts u, n a, g u, m a, d e) that all point at one 0.55 s public-domain vowel, with no standalone /a/; it is a technical fixture and not Official Voicebank 01. The character package `official.character.01` 0.4.0-dev (manifest SHA-256 `27ce82ffd17540f75721b5fc1ca945bda7bd63d9a81fa4fc2dcc557b81e4bab0`) names `official.voice.01`, so the UI correctly says that the loaded character package does not match the selected voicebank. **Method, and why this is not Usable Alpha evidence.** The app was started with `open -n` and the development flags `--development --application-support-root --voicebank-root --character-package --window-id-file`, and driven through the window by computer-use (clicks, drags, keys, menus and the macOS file panels), reading the accessibility tree only to observe state. It was not a Finder launch (UA-001 was not exercised), it used the fixture bank and not a rights-cleared production bank, and nothing was heard. Disclosures: the macOS prompt about access to files in the Downloads folder, shown for the new ad-hoc binary, was answered with `osascript` because the driver refuses the notification centre; the forced termination was `kill -9` from a shell; the lyrics were changed to syllables that the fixture bank covers. The journey files are in `/tmp/seam-journey.vndqH6` and not in the repository. **What worked through the window.** New Project with a name, 120 BPM, 4/4, 48 kHz, two channels, the fixture bank and the save panel. Eight notes by double-click, resized together by one drag, with Japanese lyrics set by inline edit and by batch lyrics (review, then Apply Distribution): こ な つ ま え お ぐ で, MIDI 67 69 67 64 67 69 72 67, 960 ticks each, 4.0 s. The Phonemes lane (14 phonemes): a boundary drag stored a `phonemeOverrides` entry (startOffsetUs 80208), a Unit-lane click plus R stored a `unitSelectionOverrides` entry (classic-psola on `demo.ja.g4.n-a.01`), and a Seam-band click created a seam override that a later Cmd+Z removed again. Tune: one pitch point added (+125 ct, bar 1 beat 4) and dragged to +50 ct at bar 2 beat 2.5. First Play: the transport ran, the output meter read −12.0 dBFS on both channels and the playhead stopped at 003:1:000; a seek by ruler click works. Save As produced `SEAM Journey Song.seam` (revision 33, SHA-256 `81a21b3742ed3fb6c2cc29e357de502f54f0e2402bc8b763d13581402805ec0b`). File > Export Audio produced `master-A.wav` and its receipt, and the Export workspace set produced `set-A/` with `master.wav`, `stems/Voice_1-00000000000003ec.wav` and `receipt.json` (COMMITTED, two files, `applicationBuildSha` `99439590e1c6ffa636b6873cc95fd26d1ecb3e65`). An external check (`afinfo` and numpy) reads PCM24, two channels, 48 kHz, 192,000 frames (4.000 s), peak 0.363, RMS 0.0804, left equal to right, audible from 0.0 s to 3.9995 s, with 250 ms RMS ranging from −17 to −50 dBFS; the master and the stem hash to the same SHA-256 `eb94f7b50ac677642790d1e08cb907b0ad14eae1548514acf0aa5623ffaec9ee` because there is one track. Cmd+Q asked to save changes and Save quit cleanly; after a relaunch File > Open Recent reopened the project (eight notes, voice Ready; final file SHA-256 `3398966dc3bead987bd09edb3490e20069f4c32b7e6b712c151d1b04286118eb`) and its export `master-B.wav` is byte-identical to `master-A.wav` (`cmp`). After one more edit (the first note up a semitone) the autosave tick wrote `revision-3-….seam.autosave` (MIDI 68 69 67 64 67 69 72 67), and after `kill -9` and a relaunch File > Recover Autosave restored the project with the edit and its edited badge. **Blocker B1 — Play does not restart after the first playback ends (reproducible).** After the first playback has ended, every Play press (button or Space, after a seek to the start, or after a Cmd+Z re-render) blocks the UI thread for about 1.7 s, gives no sound and shows no message; the button stays "Play playback" with value Stopped and the meter reads "No output device is playing". CoreAudio log: Started 09:23:19.355 and Stopped 09:23:22.702 (3.35 s for a 4.0 s song, so about 0.65 s of the tail never played, by timestamps and not by ear), then a Started and Stopped pair 44 ms apart when Play was pressed at the end of the song, then no further Start. A stack sample during a press shows the main thread in `NativeEditorApp::startAudioForPlayback` inside `std::this_thread::sleep_for`, with the feeder service thread alive and idle (`/tmp/seam-journey.vndqH6/out/sample-play-wedge.txt`). **The cause is a source-level hypothesis that no test has proven yet.** `MultichannelPlaybackFeeder::processControls` (`libs/seam-rendering/src/multichannel_playback.cpp:234`) requests a consumer reset when a Seek, Playing, Timeline or Loop command arrives while the ring holds frames, and returns false until the consumer acknowledges; the consumer is the audio callback, but `startAudioForPlayback` (`libs/seam-standalone/src/native_editor_app.cpp:887`) waits up to 2 s for the ring to refill before it starts the device, a wait that can only time out; and `AuthoringSession::configureController` (`libs/seam-standalone/src/authoring_session.cpp:155-162`) discards the failure with `static_cast<void>`. Separately, the paint loop stops the device as soon as the feeder reports playing=false and not when the ring drains, which leaves unread frames behind and cuts the tail. This overlaps the carried limit that, with a Clear owed and the service stopped, `play()` restarts the service and old audio can sound until the clear is consumed. **Other findings (F-J1 to F-J15).** F-J1: a fresh Untitled project is born dirty (the badge, and File > New Project asks to save with nothing entered). F-J2 and F-J15: Recover Autosave lists 13 entries and Open Recent lists duplicates, all with identical labels (no name, time or path), so the right autosave was chosen only by its order. F-J3: the default lyric あ cannot render on the only available bank ("No voicebank unit covers the sound "a" of the lyric あ at bar 1, beat 1"), and the status text is truncated ("FAILED • AUDIO STAL…"). F-J4: Cmd+A does nothing in the piano roll. F-J5: the distribution and cleanup reviews identify notes by raw 16-hex ids. F-J6: a stray yellow focus rectangle in the grid while a review overlay is open, and a stale tooltip after switching workspace. F-J7: the Unit and Seam bands draw only overrides, so default selected units are invisible, and a refused unit-variant cycle gives no visible feedback on the pointer path (the refusal text is inferred from source). F-J8: pitch-point delete is Shift-click only, with no keyboard or accessibility action. F-J9: Play at the end of the song is a silent no-op, with no rewind. F-J10: the render status leaks track, region and phrase ids and is truncated. F-J11: AppKit logs "Invalid view geometry: width/height is negative" about 300 times in 12 minutes. F-J12 (a hypothesis, not measured): the first note at tick 0 may lose its leading consonant. F-J13: an export dirties the project (the quit prompt appeared after Save plus exports and the file grew by 23 bytes), which matches the renderer provenance stamp (`renderedRenderAbi`, `renderedCompilerRevision`) and is probably by design. F-J14: a relaunch opens a fresh "Untitled • edited" project instead of the last document although `restoreLastDocument` is true (not investigated). Untested hypothesis: romaji "ko" reports the sound "pau". **Not done.** Loop, pause and stop; the stale-audio-after-edit check; pitch-point delete; hearing a unit change; missing-bank detection and relink (UA-016); the 30-minute session (UA-020); the SCENE look and the Mix and Voice workspaces; a Finder or release-mode launch; any listening. **Exploratory mapping to the UA rows (not claimable).** Exercised, with caveats: UA-002, 003, 004 (only 4 s), 005 (partial), 006, 008 (add and move only), 010 (non-silent, not heard), 012, 013, 014 (bit-identical, not heard), 015, 017, 018 and 019 (metadata and non-silence only). Blocked by B1: UA-011 and UA-020. Not exercised: UA-001, the hearing in UA-007, UA-008 delete, UA-009 (the seam edit was undone) and UA-016. Every row stays NOT_RUN in the acceptance contract. **Open after this.** Fix B1 at the root, fail-first at the lowest layer with a deterministic device (end the playback with unread frames in the ring, then seek and play, and expect the ring to refill and the playhead to advance within a bound), then re-freeze, rebuild and re-run the journey from playback with a window capture per step; then missing-bank detection and relink on a copy of the bank, the 30-minute session with memory sampling and underrun evidence, the SCENE look and the Mix and Voice workspaces, and a creator listening checklist for `master-A.wav` kept separate from automation. The second developer's P2 (stale diagnostic action ids, in the addendum above) follows B1, and the remaining UX findings are ranked F-J1, F-J2 and F-J15, F-J3, F-J8, F-J4, F-J5, F-J7, F-J10 and F-J11. Nothing here promotes Usable Alpha or External Beta; no listening, DAW, VoiceOver, signing or Windows evidence exists, and Windows remains a README TODO.

2026-10-01 — A refused key and a host that will not follow the editor are now told to the creator, after the second developer's review of the owed-transport work and their overall review of the project (cross-cutting repairs; they complete and advance no plan unit), pushed in `520f93f3` with a wording fix in `2f94bfbe`; GitHub CI remains deferred and `.github` was not touched. **What the review required.** Two silent failures in the editor: the plug-in dropped the result of every key press and text commit, so a refusal left the creator with a key that did nothing and no word why (the standalone kept it in `lastError_`, which reaches stderr when the process ends and never the window), and `followSelectionOnHost` discarded the host callback's result, so a host that would not follow the editor went unnoticed. Their requirements: a non-modal notice that reuses the Conflict message; de-duplicated; never dirtying the document; state-only under the runtime mutex plus a repaint request; for the selection, an observable failure and a bounded retry at the owner and event boundary, keeping the committed edit (no rollback), no healing in paint, retrying only the selection sync, and surfacing a persistent failure. **What changed in `520f93f3`.** Two registry codes, `EDIT_REFUSED` and `SELECTION_SYNC_FAILED` (Warning; Dismiss, and Retry for the selection notice), answered by the editor itself so that they work in the plug-in, which connects no diagnostic action callback, as much as in the standalone. `noteRefusal(Error)` turns a Conflict that carries a message into an `EDIT_REFUSED` notice in the refusal's own words, titled "Nothing was changed"; a notice is the editor's and not the document's, so raising one never touches the project, its revision or its dirty state; identical notices coalesce; refusals are bounded to eight (the oldest goes) and never push out the host notice; notices outlive the owner's `setDiagnostics`; the owner's diagnostics keep the order the owner gave them and the notices follow, so the toast is the owner's first entry and counts the notices among the rest. `followSelectionOnHost` keeps the committed edit; a refusal marks the move pending and raises `SELECTION_SYNC_FAILED`; only the selection is asked again, at the start of the next key press or pointer press, three tries per move, and then the notice stands with Retry, which starts a new count; nothing retries while painting; a retry that works, or a track or region the creator chose that the host accepts, takes the notice down. Plug-in (`editor_runtime_input.cpp`): both key paths and text commit tell refusals, and a commit only when a field was open. Standalone: key press and commit do the same. Dismissing an owner's diagnostic now removes it from the owner's list as well, so that rebuilding the panel for a notice cannot bring it back. `2f94bfbe` rewords the `playback.update-pending` detail so that it also covers an empty transport, which closes the copy note in the addendum to the `df770534` entry. **What I found while testing, and corrected.** In the plug-in the shell stops Command-modified N, O, S, E, Q, Z and Y when the host declares nothing, so a Command-Z scenario could not refuse; and a Delete with no note selected, which I had meant to extend in the retarget tests, deletes the region the editor now shows and is not a refusal at all. The plug-in case therefore uses Shift-L (distribute lyrics with nothing selected) and a Find field that goes stale before its commit. **Evidence.** Failing first: the API is new, so the earlier run is a compile failure, and I did not run the plug-in or standalone cases against the old source; the behaviour evidence is the mutation campaign. Release build and ctest 221/221 on the combined tree (654 s wall; a virtual machine, Docker and another project's Rust coverage run were competing for the machine). New cases: eight in `tests/test_native_ui.cpp` (five for the notices and the host, three for owner dismissal, Retry's new count and the creator's own track choice), one registry case, one plug-in case, one standalone case, one popover case that shows the editor answering its own notice without calling the host, and a new surface in the layout sweep (a long refusal and the selection notice with its two actions, at every size and scale the sweep covers, where every painted line must stay inside its clip or be elided with its full text exposed). An earlier run of the popover, layout-sweep, plug-in and adapter targets passed 15/15 before the last additions; the full suite covers them as committed. Debug focused binaries on the same tree, none of which links the controller: runtime 56, coordinator 36, transport 56, characterization 18, `seam_u2_tests` 97. Mutation campaign, 35 compiled mutants, 34 detected: retry before input (key, pointer, both), the tries limit, a followed host leaving the notice or the pending flag, a refusal not marking the move pending, coalescing, the bound and the host notice's exemption from it, notices surviving `setDiagnostics`, what `noteRefusal` accepts, the panel order, owner dismissal, Retry's new count, Dismiss, the creator-chosen region and track, the registry's severities and actions, the copy, and the plug-in's and the standalone's telling (the commit guard in both directions). One mutant first did not compile (an unused lambda parameter) and was redone in a compilable form. The survivor is the plug-in's review-open key path: the controller handles or ignores every key there, and I found no natural refusal to cover it. Two early work-in-progress stashes of this unit were dropped (`9d31270e49893382c6cf83fe03c49acea4bcfcca` and `f889dbfc52f3449441ce42ab030e99ae0484ba4a`; four-file early iterations that the commit supersedes; recoverable with `git branch <name> <sha>` until the objects are pruned); the creator's own stash was left alone. **Limits.** Fault injection through the public API; no live window, host, DAW, VoiceOver or listening evidence. Refusals of a creator-initiated `selectTrack` or `selectRegion` are returned to callers but still not shown. Refusal notices stay until dismissed, so whether that is too noisy for a benign refusal such as an undo at the start of history is a live-app judgment not yet made. The text is English only, and the second developer has not reviewed `520f93f3`. **The second developer's overall review, for the creator** (their words, condensed). Preserve the existing architecture and treat SEAM as a substantial Feature Alpha, not an accepted Usable Alpha or Beta: they rechecked the canonical records and found UA BLOCKED with all 20 rows NOT_RUN, full-product evidence NOT_RUN, the resource matrix and evaluation profile UNRESOLVED and releasedResources empty, which is an evidence and admission gap and not proof that none of the features work. `df770534` stays ACCEPT WITH NOTES. Next after this unit: freeze the exact app and singer identities; perform the macOS UI-only 30-second song, save, quit, reopen, master and stem export journey; then missing-bank recovery and the 30-minute session, with creator listening explicitly separate from automation; use the first reproducible blocker to choose the next fix, and do not start another broad test-growth cycle; the existing headless shell journey is valuable, but its one-note, non-silent fixture result is not that product proof. Musical qualification should keep the procedural, classical and neural candidates and their exact identities separate, with predeclared pitch, articulation, continuity and latency checks plus listening. Refactor the controller, shell and publication responsibilities only where the next verified user-visible fix needs it, with no large rewrite. Windows stays deferred, the full Beta scope is unchanged and `.github` is untouched. The three blockers I named to them, ranked: no recorded UI-only singer journey (UA-001 to UA-020 unchecked); the acoustic line, which the 2026-09-22 gate inventory records as blocked on research (neural singing R9 and EB-009, the unvoiced-aperiodicity defect parked with a baseline UV RMS of 0.657 against its own guardrail, and the next input a human listening packet marked NOT_REVIEWED; not re-checked since); and no rights-cleared production bank that sings out of the box (in an earlier live run the demo bank `demo.public-domain.human.production` 0.12.0 had no standalone /a/, so the default lyric あ never rendered; not re-verified in this session). **Open after this.** The frozen macOS UI-only singer journey, starting from the exact app and singer identities, as above. Uncovered guards and unexamined mutants from the two reviews: a committed test for the `df770534` F2 window (a seam completion released between the locked retirement and the cancel, which needs a test hook in `reconfigureAudio`), the F5 mutant, and the plug-in's review-open key path. Still open from the earlier entries: `invalidateCurrent()` and the request-id overflow branch change authority without settling the displayed progress, the romaji "ko" / "pau" question, "Untitled • edited" on a fresh project, the `leavePlace()` audit, the second developer's P3 in `tests/test_studio_generation_bank_journey.cpp`, and the live-app pass, which has never been done. Nothing here promotes Usable Alpha or External Beta, and Windows remains a README TODO. **Addendum (2026-10-01, after the second developer's final review of `520f93f3` and `2f94bfbe`, pinned at `99439590`).** Verdict: refusal reporting and the bounded host-follow pass focused review, and the `2f94bfbe` wording change is ACCEPTED; one P2 remains, so this is not an unconditional closure. **P2 — a stale diagnostic action retargets a different issue.** Action ids carry only the row index and the action (`shell_overlays.cpp:1243`); `SingShell::dispatchController` (about line 4053) refreshes the rows and then accepts the reused id; `shellPointerDown` (about lines 4285 and 4298) recomputes the rows from the current state although the pixels were painted from older state. Three failing probes, in `/tmp/seam-review-520f-INm0jl/review_notice_edges.cpp`: a retained row-1 DISMISS after the eighth-entry eviction, a pointer click on the last-painted row-1 button after an eviction, and a retained row-1 DISMISS after the selection warning is removed; each reports accepted=1, the intended `held-1` still present and the unrelated `held-2` removed. Their `review_owner_identity.cpp` shows the same defect with owner diagnostics and fails identically on `df770534`, so the defect predates `520f93f3`; the new eviction and host-follow-removal paths expose more affected cases, so it must not be described as newly introduced. Fix boundary they named: an issue identity or generation token carried through the display, the retained actions and the pointer projection, so that a stale action is rejected or stays bound to its original issue, covering both the semantic identity and the last-painted pointer projection. No rollback of the notice work was requested. **Other conclusions.** Reentry: no destructive path in the installed selection callbacks (bounded source and probe evidence, not ASan and not a real host). Dismiss semantics are confirmed: Dismiss hides the message, does not acknowledge synchronization and does not reset the three-attempt cap; whether repeat messages should be suppressed is a non-blocking live-UX decision. **Their verification.** 160 committed focused cases pass; seven additional independent cases give 3 pass and 4 fail, all four failures being the one defect above. Not run by them: full CTest, Debug, mutation, sanitizers, a device, the live UI, a DAW, VoiceOver and listening. **What they asked for.** Record the P2 without promoting any gate and without widening the frozen singer journey; from their overall review, UA-001 to UA-020 stay NOT_RUN and the resource matrix stays UNRESOLVED, creator listening stays separate from automation, the first reproducible blocker chooses the next fix, no broad test-growth cycle is started, and refactoring happens only where the next verified fix needs it. **Open.** The P2 fix, with their three probes as fail-first repository tests, then mutation and their review; it is scheduled after the first reproducible blocker that the live journey found (recorded in the journey-pass-1 entry).

2026-10-01 — A format change now replaces the transport and ends the transient audio in one step, after the second developer's review of the announcement repair (cross-cutting repairs; they complete and advance no plan unit), pushed in `df770534`; GitHub CI remains deferred and `.github` was not touched. **Second developer's verdicts at `06f7f0e8`** (source review plus their own probes: six implementation translation units freshly compiled from the pin against frozen Release support archives; 179 committed focused cases pass (runtime 55, coordinator 36, characterization 18, lifecycle 70); all twelve retained independent probes pass (debt 4, CLAP 4, gate 4); of two new probes one passes and one fails; not a full independent CTest, Debug, mutation campaign, sanitizer or ThreadSanitizer, live window or application, device, DAW, listening, VoiceOver, signing or Windows): `4781ca1d` REJECT as a completed format-retirement unit, with its failure-notification repair accepted and both earlier reproductions passing independently (their unchanged `review_gate_edges.cpp` passes 4/4); `06f7f0e8` ACCEPT WITH NOTES as the historical record. The probe that passes covers an invalid rate, zero channels and zero block frames: a refused reconfigure keeps the seam preview's ownership, and an invalid rate also keeps an active, ready comparison. **P2 in `4781ca1d`: a seam completion that was already publishing refilled the transport that a format change had replaced.** `reconfigureAudio` replaced the transport first and retired the transient audio afterwards, taking the audition lock only for the retirement. A completion that already held the lock had checked its request current and was about to publish, so it published old preview audio to the new transport, which accepts audio whenever only the block size changed; the retirement that followed cleared the flags and removed nothing. Their reproduction holds the completion at `duringSeamPreviewPublication`, calls `reconfigureAudio(48000, 2, oldBlock + 64)` on another thread, and releases the completion while the new canonical render is held in `beforeRender`: after the call returns the seam preview is over (active 0, ready 0), the transport is available and holds nonzero old PCM, and no warning is raised. They reproduced it on `06f7f0e8` twice and on `b76147b8`, so the ordering predates `4781ca1d`, which did not cause it and did not cover it. This is scheduling fault injection with ring-buffer evidence, not a listening observation. **What changed in `df770534`.** `reconfigureAudio` holds `performanceAuditionMutex_` around `transport_.reconfigure` and `retireTransientAudioLocked()`, which replaces `retireTransientAudio()` and clears the seam preview's flags, the comparison's flags and the mark that canonical audio waits behind a preview. A completion already inside the lock publishes first, to the transport that is about to go, and what it published goes with it; one that comes later finds that nothing is wanted, because `publishCompletedSeamPreview` now returns early when the preview is no longer active under the lock (the comparison's completion already did). A refused reconfigure returns before it retires anything, so ownership stays as it was. The renders are cancelled once the lock is released, because cancelling can deliver the completion on the calling thread and that takes the lock. The transport registers no callbacks and its lifecycle lock is taken after the audition lock on every path (as `publishAudio` already did), so I found no cycle by reading the code; that is a reading, not a ThreadSanitizer result. **Evidence.** Failing first: the new case (filter `already publishing refill`) was run against the `06f7f0e8` runtime sources, with the two files restored from that commit and the test kept, and failed because the transport's block size had already changed while the completion was still publishing (0 passed, 1 failed); it passes with the change. Release build and ctest 221/221 on the final tree (638 s wall, on a loaded machine). Debug: runtime 56 (55 before; one new case), coordinator 36, transport 56, characterization 18, `seam_u2_tests` 97; two loops of twenty-five consecutive runs of the runtime binary, the second after the tree was isolated to this unit, without a failure. Mutation campaign F, four mutants, all compiled: the format change not serialized with the audition lock (the previous order) and retirement not clearing the seam flags are detected; the early return for an unwanted preview and the format change not cancelling the seam render survive, because each window is also covered by the other defence. I compiled the second developer's `review_format_edges.cpp` against the fix (my re-run of their file, not their review): their first case passes unchanged; their second case stops at its own precondition, which asserts that the transport is replaced before the completion is released, the ordering the fix removes, and a copy with only that precondition inverted runs the rest as they wrote it and reports seam active 0, ready 0, transport available 0, old nonzero PCM 0 and no warning. **Limits.** Fault injection through the public API only; no live window, host, DAW, listening, VoiceOver, signing, Windows or external-review evidence, no ThreadSanitizer run, and the second developer has not reviewed `df770534`. A canonical completion that lands between the locked block and `requestPreview(true)` is still current for the old request and could be refused by the new-rate transport and owed until the new render settles it: that is a hypothesis from reading the code, not a reproduction, and it has been put to the second developer to break, together with the lock boundary. A clear that was owed before a format change is not settled by it; the decision that follows does that. The caller-ownership boundary is still not declared complete, and the comparison's unconditional hand-back stays as documented. The text is English only. **Open after this.** The notification and selection unit (the plug-in's silent refusals, and `followSelectionOnHost` discarding a host callback's refusal; work in progress sits in the stashes named "notices unit wip"), then the frozen macOS UI-only singer journey with a listening checklist for the creator, as the second developer recommends, and not another general test-growth cycle. Still open from the earlier entries: `invalidateCurrent()` and the request-id overflow branch change authority without settling the displayed progress, the romaji "ko" / "pau" question, "Untitled • edited" on a fresh project, the `leavePlace()` audit, the second developer's P3 in `tests/test_studio_generation_bank_journey.cpp`, and the live-app pass, which has never been done. Nothing here promotes Usable Alpha or External Beta, and Windows remains a README TODO. **Addendum (2026-10-01, after the second developer's review at `df770534`).** Verdict: `df770534` ACCEPT WITH NOTES, and the in-flight format-retirement P2 is independently CLOSED. They reviewed the code at `df770534`, not the ledger commit `0ab9614b` and not the uncommitted notification-unit files. Scope (source review plus their own probes: six implementation translation units freshly compiled from the pin against frozen Release support archives; 180 committed focused cases pass (runtime 56, coordinator 36, characterization 18, lifecycle 70); all fourteen retained independent probes pass, with one format-order precondition adapted; three additional instrumented cases pass; one representative guard-removal mutant is detected; not a full independent CTest, Debug, full mutation campaign, sanitizer or ThreadSanitizer, live app, device, DAW, listening, VoiceOver, signing or Windows run): their own adapted `review_format_edges.cpp` (only the broken-order precondition changed, to require that the block size stays old and that `reconfigureAudio` has not returned while the old publication is held) reports seam active 0, ready 0, transport available 0, old nonzero PCM 0 and no warning, and then gets the new canonical audio; the same adapted test fails the old-block-size assertion when rebuilt against `06f7f0e8`; the rejected-reconfiguration case passes unchanged. **Lock review.** Every path they inspected nests the audition lock outside the transport's lifecycle lock; `config()` drops the lifecycle lock before `reconfigureAudio` takes the audition lock; the feeder service that the transport stops and joins only pumps the feeder and checks its stop token, so nothing calls back into the runtime; the cancellation callbacks run after the locked replacement and retirement, and the completion and retry notifications stay outside the audition lock. Twenty alternating 44.1 kHz and 48 kHz reconfigures with pending publication debt, a running service and callbacks re-entering `diagnostics()` and `audiblePublication()` reproduced no deadlock and no stuck convergence. That is bounded source and test evidence, not a scheduler-fairness or worst-case latency guarantee: the non-real-time critical section includes stopping the service and allocating. **Correction to my mutation note above: the survivor F2 is a coverage gap, not an equivalent defence.** I wrote that the early return for an unwanted preview and the format change not cancelling the seam render survive "because each window is also covered by the other defence". For the early return that is wrong. With scheduling hooks of their own (in `/tmp`, not committed) placed immediately after the locked retirement, before the cancellation, and at the seam completion's exit, a seam completion that was held before it took the audition lock and is released in that gap is rejected by the fixed code (late audio 0, ready 0), and removing only the new early return makes the same case fail with late audio 1 and ready 1 before the cancel: the cancel does not cover that window. The guard stays. The campaign's F2 survivor stands as an unprotected guard until a committed test holds a completion in that gap, which needs a hook between the locked block and the cancels in `reconfigureAudio`; their instrumented cases are in `/tmp/seam-review-df77-iQuMeH/`. They did not test F5, the format change not cancelling the seam render, so my claim that the early return covers it is reasoning that nobody has run. **The hypothesis I put to them is reachable, with a benign outcome.** With hooks: an old 48 kHz canonical completion held before its publication lock, a reset to 44.1 kHz paused immediately after the locked retirement, and the completion released before `requestPreview(true)`. It is still current, is refused for the new sample rate and records `playback.update-pending` while the transport is unavailable; the warning persists while the new-rate render is held, and clears, with playback available, when that render succeeds. They reproduced no wrong-rate audio, no lost recovery and no lasting bad outcome, and ask for no correctness fix in this unit. **Copy note, non-blocking.** The `playback.update-pending` detail says that playback "still plays an earlier version", which is untrue while the replacement transport is empty; it is to be reworded so that it also covers an empty transport (the tests match the message key, not the text), and that was not yet done when this addendum was written. **Retained accepted take.** In the paths they inspected, the debt helper publishes `renderer_.acquireCurrent()` when a take is retained, not `retainedAcceptedAudition_` itself; performance completions are gated by the inactive flag after retirement; `requestPreview` clears the retained pointer; they found no direct route that republishes the retained take into the replacement transport. That is a source conclusion, not exhaustive coverage of every interleaving. They measured no natural timing or latency, and the scoped acceptance does not promote Usable Alpha or External Beta. **Open after this addendum.** A committed test for the F2 window (a seam completion released between the locked retirement and the cancel), which needs a test hook in `reconfigureAudio`; the F5 mutant, still unexamined; the `playback.update-pending` wording; then the notification and selection unit and the frozen macOS singer journey, as in the entry above.

2026-10-01 — A failed seam preview is now announced and a format change retires the transient audio, after the second developer's final review of the owed-transport repairs (cross-cutting repairs; they complete and advance no plan unit), pushed in `4781ca1d`; GitHub CI remains deferred and `.github` was not touched. **Second developer's verdicts at `b76147b8`** (source review plus their own probes: six implementation units freshly compiled from the pin against frozen Release support archives; 176 committed focused cases pass (runtime 52, coordinator 36, characterization 18, lifecycle 70); all eight retained independent probes pass; of four new probes two pass (the repaid-publication-then-refused-preview case that guards the waiting mark, and the documented comparison warning and recovery) and two fail (the notification and the reconfiguration below); not a full independent CTest, Debug, mutation campaign, sanitizer or ThreadSanitizer, live window, DAW, listening, VoiceOver, signing or Windows): `f6f51233` ACCEPT; `f810b69f` REJECT as a completed extended seam-recovery unit pending the notification P2 below, with both original P2s independently CLOSED (their unchanged `review_debt_edges.cpp` passes 4/4: the canonical render converges with the warning preserved while the transport refuses it, and the accepted take converges with the retained audition released) and no revert requested; `b76147b8` ACCEPT WITH NOTES (it separates my rerun from independent review and does not declare the whole caller-ownership boundary complete). They found no further false-negative waiting mark in the serialized hand-over paths they tested and no two-owner race through the critical sections of `repayTransportDebt` and `publishCompletedSeamPreview`; they did not race owner-thread calls to manufacture a seam and comparison overlap. They confirmed the documented limit that a refused comparison restores unconditionally, so that with a full queue it raises `playback.update-pending` while the same canonical revision is still on the transport and the warning clears when the service resumes; they measured no audible skip, so my reading of the feeder's ring reset on republication stays a source-level hypothesis. **New P2 in `f810b69f`: the failed seam preview was not announced.** `publishCompletedSeamPreview` returned on `!published` before it told the window, although the failure may now hand the canonical audio back or owe it. With `transportRetryAttempts` 0: publish seam A, fill the transport's queue, request seam B; the preview and the hand-back are refused, `playback.update-pending` appears and the preview goes inactive, and the completion callback is not called once (`review_gate_edges.cpp`, first case). The retry helper cannot announce it either while the queue stays full. The standalone's repaint bridge is `AuthoringSession::onRenderCompleted`; this is source and API evidence, not an observed repaint failure. **Pre-existing P2: a format change left a seam preview owning playback.** From a ready seam preview, `reconfigureAudio(44100, 2, 256)` dropped the transport's timeline, but the preview stayed active and ready, so `publishCompletedAudio` skipped the new canonical render for good: seam active and ready, transport unavailable, no warning. The same probe fails at `c6104490`, so `f810b69f` did not cause it. `NativeEditorApp::startAudioForPlayback` rejects unavailable audio, so it matters for playback (I did not run the application or a device). **What changed in `4781ca1d`.** The outcome of a seam preview is announced whichever way it went, after the audition lock is released like the other completions. `reconfigureAudio` now retires the transient audio before it asks for the new render (`retireTransientAudio`): the seam preview is revoked, a performance comparison that is open ends without a hand-back (the coordinator's canonical audio is for the old format, which the transport would refuse, so trying reported playback as behind for as long as the new render took), and the mark that canonical audio waits behind a preview is cleared; an owed publication stays owed until the decision that follows settles it or owes it afresh. **Evidence.** Failing first: the announcement case never saw the warning announced, the seam case still had the preview active straight after the format change, and the comparison case reported `update-pending` while the new render was held. Release build and ctest 221/221 on the final tree. Debug: runtime 55 (52 before; three new cases), coordinator 36, transport 56, characterization 18, `seam_u2_tests` 97; twenty-five consecutive runs of the runtime binary without a failure. Mutation campaign (six mutants, all compiled): the failed preview not announced, the announcement made with the audition lock held, the seam preview not revoked by a format change, the comparison not retired, and `reconfigureAudio` not retiring at all are detected; the waiting mark left set by a format change survives (a stale mark only makes a later failed preview hand the canonical audio over once more). The lock probe of the announcement case first hung under its own mutant, because a joined future waited for a thread that needed the lock the same thread held; it now probes from a detached thread with a bounded wait, so that a violation fails the case. **Limits.** Fault injection through the public API only; no live window, host, DAW, listening, VoiceOver, signing, Windows or external-review evidence, no ThreadSanitizer run, and the second developer has not reviewed `4781ca1d`. The caller-ownership boundary is still not declared complete, and the comparison's unconditional hand-back stays as documented. A clear that was owed before a format change is not settled by it; the decision that follows does that. The text is English only. **Open after this.** The notification and selection unit (the plug-in's silent refusals, and `followSelectionOnHost` discarding a host callback's refusal; work in progress sits in the stashes named "notices unit wip"), then the frozen macOS UI-only singer journey with a listening checklist for the creator, as the second developer recommends, and not another general test-growth cycle. Still open from the earlier entries: `invalidateCurrent()` and the request-id overflow branch change authority without settling the displayed progress, the romaji "ko" / "pau" question, "Untitled • edited" on a fresh project, the `leavePlace()` audit, the second developer's P3 in `tests/test_studio_generation_bank_journey.cpp`, and the live-app pass, which has never been done. Nothing here promotes Usable Alpha or External Beta, and Windows remains a README TODO. **Addendum (2026-10-01, after the second developer's review at `06f7f0e8`).** Format retirement was not complete at `4781ca1d`: a seam completion that was already publishing when the format changed could still refill the new transport with old preview audio, which the retirement then left in place. That ordering predates `4781ca1d` (it fails at `b76147b8` too) and is closed in `df770534`, recorded in the entry above this one. The second developer's verdicts: `4781ca1d` REJECT as a completed format-retirement unit, with its failure-notification repair accepted, and `06f7f0e8` ACCEPT WITH NOTES as the historical record. This entry's limits on the caller-ownership boundary stand.

2026-10-01 — The two holes in the owed-transport ownership are closed, and a seam preview that fails hands the canonical audio back only when something waits behind it (cross-cutting repair; it completes and advances no plan unit), pushed in `f6f51233` and `f810b69f`; GitHub CI remains deferred and `.github` was not touched. **Second developer's verdicts at `c6104490`** (source review plus their own probes: 168 committed focused cases pass and, of eight extra probes, six pass and two fail; not the full Release ctest, Debug, a mutation campaign, ThreadSanitizer, a live app, DAW, listening, VoiceOver, signing or Windows): `82260f04` ACCEPT WITH NOTES (the header comment said that any `publish` supersedes a waiting preview; only a `publish()` that finds a free slot does, and a refused one leaves the waiting preview alone; corrected in `f6f51233`, comment only); `b3d0394f` REJECT as a completed caller-convergence unit until two P2s are closed, with the approach sound and no revert needed; `f7ebc364` ACCEPT; `c6104490` ACCEPT WITH NOTES. Their wording notes, both applied in `f810b69f`: the standing warning still said a refusal "is being retried" after the bounded retries had ended (the detail now says it was refused, the impact line no longer promises that playback is being updated, and Retry asks again; the message keys are unchanged), and the comment in `shutdown()` did not name the boundary after which no helper starts (`debtRetryRetired_`, set under the lock that starts a helper, after the preview worker has been joined). Their answers to my two questions: the coordinator going Idle for an emptied score is acceptable as long as the availability and the pending diagnostic stay visible and ownership of the retry is not lost, and handing over `acquire()` (the latest canonical render) follows the existing latest-canonical policy. **What they reproduced (two P2s, through the public API with the feeder's control queue full; `review_debt_edges.cpp`, filter `reviewC610:`).** (1) The retry's publication branch treated `seamPreviewActive_`, a request, as a transient holding the transport and forgave the debt. A seam preview that is only asked for holds nothing; when the transport then refused it, the canonical render that was owed was never handed over and the warning went away. (2) The same branch forgave the debt while `retainedAcceptedAudition_` was set, even after the accepted take's canonical render had arrived and been refused; it was never handed over and the retained take was never released. Both were reachable with no ring comparison, only the published revision and the diagnostics, so my statement in the previous entry that the surviving mutant, the guard that keeps the retry from publishing over a transient preview or a waiting take, needed a fixture that can tell the audio apart was wrong; see the correction appended to that entry. **What changed in `f810b69f`.** `repayTransportDebt` forgives a publication only when the transient is audible (a seam preview that is ready, or a comparison that is active and ready, whose end hands the canonical audio back and owes it again if the transport refuses). Otherwise it hands the canonical audio over: the current render while an accepted take waits for it (the debt stays owed until that render has arrived, and the render's own publication settles it), the latest render otherwise, and the retained take is released once its render is on the transport. A seam preview or a comparison that the transport takes settles what was owed to it. Closing the first hole exposed a neighbour that predates `b3d0394f`: a canonical render that finishes while a seam preview is only asked for is left unpublished for it (`publishCompletedAudio` keeps the transport for the preview), and when that preview then failed nobody handed the render over. A seam preview that fails now hands the canonical audio over, or owes it, but only when something waits behind it: `canonicalBehindSeamPreview_` (guarded by `performanceAuditionMutex_`) is set when a preview's audio is published or a current render is left unpublished for one, and cleared when the canonical audio has been handed over or the transport emptied. The gate exists because a publication makes the feeder reset its ring and seek to its production point, so republishing audio the transport already holds during playback can skip the audio that was queued (read from the code, not measured), and because a refused republication of audio that is already there would be reported as playback being behind when it is not. A failed preview that put nothing on the transport and left nothing waiting changes nothing. **Evidence.** Failing first: the two reproductions were run against the code before the change and each failed at its first assertion about the owed publication; the cases written for the gate fail against the ungated restore (mutant D1, which restores on every failed preview); the other new cases were checked by mutation instead. Release build and ctest 221/221 on the final tree. Debug: runtime 52 (44 before; eight new cases), coordinator 36, transport 56, characterization 18, `seam_u2_tests` 97; twenty-five consecutive runs of the runtime binary without a failure. The second developer's own `review_debt_edges.cpp`, recompiled against the Debug libraries at `f810b69f`, passes 4/4: their two reproductions now converge (canonical 0 to 1, accepted take 1 to 2, with the warning shown while the transport is full). That is my re-run of their file, not their review of the fix. Mutation campaigns on `authoring_runtime.cpp` (campaign C, ten mutants; campaign D, nine mutants and a re-run of one; all compiled): detected are the preview that is only asked for excusing the debt (Q1), a retained take excusing it (Q2), the retained take not released after its render is handed over (Q3), a failed seam preview not restoring (Q6), a seam preview that the transport takes not settling (Q8), a comparison that the transport takes not settling (Q9, detected only after its case was added), the gate removed (D1), the mark not set when a preview's audio is published (D2), not set when a render is left unpublished for a preview (D3), and not cleared by a canonical publication (D4) or by the creator's restore (D5). Survivors: Q4 (a take that still waits for its render settles the debt instead of keeping it) and Q5 (the stale render is handed over while a take waits) need a held render together with a retained take; Q7 and Q10 (a comparison that is audible, or only asked for, when a seam preview fails) need a comparison that overlaps a seam preview, which the public API excludes except in a race (`auditionPerformance` reads `seamPreviewActive_` before it takes the lock); D6 to D9 (the mark not cleared after `restoreCanonicalAudio`, a repaid publication, a repaid clear or an emptied score) survive because a stale mark only makes a later failed preview hand the canonical audio over once more and never skips a hand-over. They are reported, not covered. **Limits.** Fault injection through the public API only; no live window, host, DAW, listening, VoiceOver, signing, Windows or external-review evidence, no ThreadSanitizer run (the retry helper, its condition variable and `shutdown()` remain the targets for one), and the second developer has not reviewed `f810b69f`. The caller-ownership boundary is not declared complete. A comparison that fails still restores the canonical audio unconditionally, as before, so a comparison refused by a full transport can report playback as behind while the transport holds the canonical audio; the gate covers seam previews only. While a clear is owed and the feeder's service is stopped, `play()` restarts the service and the old audio can sound until the clear is consumed (not measured). The helper retries any refusal for the bounded time, including one that can never succeed, and a refusal that outlives the retries keeps the warning up until the next decision or the creator's Retry. The text is English only. The path where the helper thread cannot be created is not exercised. **Open after this.** As in the previous entry, except that the two P2s and the two wording notes are closed: (1) the plug-in's silent refusal of a mixed selection and `followSelectionOnHost` discarding a host callback's refusal (work in progress sits in a stash named "notices unit wip" and does not compile yet); (2) `invalidateCurrent()` and the request-id overflow branch change authority without settling the displayed progress; (3) why the romaji lyric "ko" reports the sound "pau", why "Untitled • edited" appears on a fresh project, and the `leavePlace()` audit; (4) the second developer's P3 in `tests/test_studio_generation_bank_journey.cpp`; (5) app-level journey tests through `NativeEditorApp` and the live-app pass, which has never been done, then the frozen macOS UI-only singer journey with a listening checklist for the creator. Nothing here promotes Usable Alpha or External Beta, and Windows remains a README TODO. **Addendum (2026-10-01, after the second developer's review at `b76147b8`).** (1) Independently verified: both original P2s are closed; their own unchanged `review_debt_edges.cpp` passes 4/4 at `b76147b8`, so the rerun reported above is now matched by their run. (2) Two issues found by that review are recorded and handled in the entry above: `f810b69f` did not announce a failed seam preview (their verdict on it is REJECT as a completed extended seam-recovery unit until that was fixed), and a format change left a ready seam preview owning playback over an emptied transport, which predates `f810b69f` and fails at `c6104490`. (3) Their verdict on this entry's commit `b76147b8` is ACCEPT WITH NOTES: it keeps my rerun separate from their review and does not declare the caller-ownership boundary complete.

2026-10-01 — A change the transport refuses is now owed, reported and asked for again; the plug-in's preview no longer loses a render to readers that hold every slot or keeps a vocal that is gone; and the second developer's final verdicts on the five earlier repairs (cross-cutting repairs; they complete and advance no plan unit), pushed in `82260f04`, `b3d0394f` and `f7ebc364`; GitHub CI remains deferred and `.github` was not touched. **Second developer's final verdicts at `aac143a2`** (source review plus their own probes: 213 committed focused cases, 14 earlier independent probes, nine retained boundary probes, four new uninstrumented cases and three instrumented fault cases; not the full Release ctest, Debug, ThreadSanitizer or sanitizers, my mutation campaign, a live app, DAW, listening, VoiceOver, signing or Windows): `35b294b2` ACCEPT; `14d3704d` ACCEPT; `cf538128` ACCEPT WITH NOTES for the controller and feeder all-or-none admission and intent tracking; `5d4315b8` ACCEPT WITH NOTES (minor: `duringReset` runs inside the admission lock but before the revocation stores, which the header comment did not say; corrected in `f7ebc364`); `aac143a2` REJECT of its broad closure wording, not of the transport implementation. `82260f04` was not part of that review. **What they reproduced (P2, pre-existing).** `AuthoringRuntime::settleWithNothingToRender` discarded the result of `clearAudio()` and reset the coordinator to idle whatever it returned. Through the public API only: render and play the fixture, hold the consumer with `transport().shutdown()`, fill the control queue, remove the notes of the audible region. The clear is refused (`rejectedCommands` 4), the runtime reports idle with the transport still available, and starting the feeder produces non-zero old audio. The same sequence at `28332121` reproduces it (`rejectedCommands` 1), so it predates the five commits. It is scheduling and queue-pressure fault injection, not a live observation. The claim in the previous entry that a failed clear is never treated as settled therefore held for the controller's API only. Their other findings: a refused `play()` does not arm a later publication on the fixed code but does start the service, which then drains previously accepted controls without another explicit start (a refusal preserves command admission and intent, not every lifecycle side effect); an injected start failure and four reconfigure scenarios (allocation failure, a new start that fails while the old one restarts, both starts failing and then an explicit recovery, an invalid format) preserve accepted intent; acknowledgements cover already-published state but are not an atomic head, playing and count tuple, so later playback can be visible through them. The surviving mutant of the previous campaign (a refused `play()` arms later audio) is testable with their scheduling hook, and their mutant fails the new case (68 commands and playing against 67 and not playing); it is one independent mutant, not a rerun of my campaign. **What changed in `82260f04`** (the plug-in's preview publication under slot pressure; open item (2) of the previous entry). `RealtimePreviewPublication` gained `revoke()` and `publishWhenFree()`. `revoke()` stores "nothing" in the slot index and needs no free slot, and `acquire()` then returns a handle to an empty preview and never a null one, so a coordinator that has gone idle withdraws the vocal from the host instead of leaving the last one on offer (an empty publication would need a free slot, and readers can hold them all). `publishWhenFree()` installs at once when a slot is free and otherwise keeps the newest waiting preview and offers it from a helper thread that exists only while one waits (fifty tries 2 ms apart, then 20 ms apart), so readers that hold every slot no longer make the plug-in lose a render. `publish()` keeps its contract for the offline bounce. `publishPreviewFromAuthoring` revokes when the coordinator is idle and otherwise calls `publishWhenFree`. Release ctest 221/221; Debug characterization 18/18 (eight new cases: five for the publication, three for the runtime); twelve mutants, all detected. The fail-first evidence is a proxy, not a run against the old commit: with the old call sites put back (mutants M1 and M2) two and one of the eight new cases fail. Not exercised: the path where the helper thread cannot be created, where the preview waits for the next publication or revoke to supersede it. **What changed in `b3d0394f`.** The runtime keeps what the transport still owes the score, a clear or the canonical audio, under the lock that every decision about the transport is made under (`performanceAuditionMutex_`). It is reported as a standing `RENDER_STALE` diagnostic with the message keys `playback.clear-pending` and `playback.update-pending`, derived from the debt like a missing bank and therefore not removed by Dismiss; the refusal's own message goes into the detail. A helper thread, which exists only while something is owed, asks again for a bounded number of tries (`AuthoringRuntimeConfig::transportRetryAttempts`, default 150: fifty tries 2 ms apart, then 50 ms apart, about five seconds; zero leaves it to the next request), announces what it did through the completion callback because the window paints on demand, is stopped and joined by `shutdown()` before the transport and the coordinators go, and is never started after shutdown has begun. The newest decision replaces the debt (a clear owed and then a publication refused leaves a publication owed, so a late clear cannot take away the audio that followed it), and a clear or publication that succeeds settles it. The coordinator still goes idle for an emptied score: the status reads idle with the audible audio still available and the warning on screen until the clear lands, which is what the second developer asked for (an explicit pending state and bounded recovery off the audio thread, not a refusal to settle). Five call sites now keep their refusals: the emptied score, the finished canonical render, the canonical restore after a seam preview, the canonical restore when a performance comparison ends, and that restore again when a comparison's own publication fails (a refused pause owes a clear). `publishCompletedSeamPreview` and the comparison's own publication already used their results for their flags, and `auditionPerformance` submits renders and sends the transport nothing, so the audit of discarded transport results in `authoring_runtime.cpp` ends there. `RENDER_STALE` had no presentation copy, so the raw message key was shown as its text; it now reads "Playback is behind your edits" with a Retry action, which asks the runtime to decide again (a clear is asked for at once; a publication is rendered and handed over again). Only the standalone application uses the transport (the plug-in runs with `enableTransport = false`), so the plug-in is unaffected. **Evidence.** Failing first: four of the new runtime cases were run against the code before the change and each failed at its first assertion about the owed change (the transport stays available and nothing is reported; nothing is reported for a refused publication); the P2 sequence is the first of them. The other cases were written with or after the change and were checked by mutation instead. Release build and ctest 221/221 on the final tree; the first ctest run of the unit failed one case, `editor semantic tree exposes stable accessible controls`, which expected the raw message key `render.stale` as the node's value, an effect of the generic fallback copy; it now expects the words and finds the key in the node's description, and a presentation case for `RENDER_STALE` is new. Debug: runtime 44 (37 before), coordinator 36, transport 56, characterization 18, `seam_u2_tests` 97 (96 before: a comparison that ends while the transport is full). Twenty-five consecutive runs of the runtime binary without a failure. Mutation campaign on `authoring_runtime.cpp`: twenty compiled mutants, eighteen detected by named cases (the refusal of a clear ignored, the refusal of a publication ignored, a clear or a publication that succeeds not settling, the helper never paying, never giving up, not saying that it gave up, not started, not stopping when asked, not announcing, a report left standing or not reported, a clear that pays without clearing, a publication that pays without publishing, a newer debt not replacing the owed one, the fast gap as long as the slow one, a refused seam-preview restore not owed, a refused restore after a comparison not owed). Two survive: the guard that keeps the retry from publishing over a transient preview or an accepted take that is waiting for its render, and the owing of a refused pause. Both need a transient preview whose audio can be told apart from the canonical audio at the ring, which no fixture here does. Two further mutants did not compile under `-Werror` and were redone as compiling variants; the commit message of `b3d0394f` counts them wrongly (it says 22 compiled and 20 detected; the figures here are the right ones). **Limits.** Fault injection through the public API only; no live window, host, DAW, listening or external-review evidence, and no ThreadSanitizer run (the helper thread, the condition variable and `shutdown()` are the targets for one). While a clear is owed and the feeder's service is stopped, `play()` restarts the service and it drains its queue in order, so the old audio can sound until the helper's clear is consumed; the exposure is not measured. The helper retries any refusal for the bounded time, including one that can never succeed (a timeline that cannot be prepared), which then keeps the warning up until the next decision. A refused `play()` still starts the service (the second developer's finding, unchanged). The text is English only. The path where the helper thread cannot be created is not exercised; the debt then stays reported and the next request asks again. **Open after this.** (1) The plug-in's silent refusal of a mixed selection (`editor_runtime_input.cpp:90` discards the controller's result): the second developer wants a non-modal notification that reuses the existing Conflict message, dispatched after the runtime mutex is released, de-duplicated and without dirtying the document, within the 600-line cap that `scripts/verify_clap_authoring_adapter.py` enforces on `editor_runtime_adapter.cpp` (585 lines now); and `followSelectionOnHost` still discards a host callback's refusal (an observable failure and a bounded retry at the owner or event boundary, no rollback of the committed edit, no healing in paint). (2) `invalidateCurrent()` and the request-id overflow branch change authority without settling the displayed progress. (3) Why the romaji lyric "ko" reports the sound "pau" (a hypothesis only), why "Untitled • edited" appears on a fresh project, and the `leavePlace()` audit. (4) The second developer's P3 in `tests/test_studio_generation_bank_journey.cpp`: assert the installed WAV SHA against the retake audio, bind the exact unit to the audio path, and compare an isolated phrase with the superseded take. (5) App-level journey tests through `NativeEditorApp`, and the live-app pass, which has never been done. The second developer recommends finishing the notification and selection work and then stopping the growth of general regression tests in favour of a frozen macOS UI-only singer journey (install or create a singer, song and edits and play, save and reopen and recover, master and stem export) with listening and captured evidence; none of this promotes Usable Alpha or External Beta, and Windows remains a README TODO. The second developer has not reviewed `82260f04`, `b3d0394f` or `f7ebc364`. **Correction (2026-10-01, after the second developer's review at `c6104490`).** (1) The entry says that two mutants survive, "the guard that keeps the retry from publishing over a transient preview or an accepted take that is waiting for its render" and "the owing of a refused pause", and that both needed audio that can be told apart at the ring. The first was not a fixture limit: it was two real holes (the retry forgave a publication because a seam preview had only been asked for, and because an accepted take was retained after its render had arrived and been refused), reproduced by the second developer with the published revision and the diagnostics alone and closed in `f810b69f`. (2) The entry's statement that the audit of discarded transport results ends at the listed call sites held for discarded results, not for what a retry forgave; the caller-ownership boundary was not complete at `b3d0394f` and is not declared complete now. (3) The second developer's verdict on `b3d0394f` is REJECT as a completed caller-convergence unit until those two P2s were closed, with the approach sound and no revert needed; see the entry above.

2026-10-01 — The transport controller's control calls are all-or-nothing, and its decisions about audio to come follow what was asked until the feeder has applied it (cross-cutting repair; it closes open item (1) of the previous entry and completes and advances no plan unit), pushed in `cf538128`; GitHub CI remains deferred and `.github` was not touched. **What was reproduced.** Both by fault injection through the public API (the second developer's probes against `c73fefaa` and `2833…`; not live observations). (a) `publishAudio()`, `clearAudio()` and `stop()` sent the feeder their commands one push at a time, so with the service stopped and 63 of the 64 control slots taken a publication left the feeder holding a timeline that the controller never recorded (`available` false, `clearAudio()` a no-op, and start then play sounding), and a clear or a stop that found too little room applied its first commands and not the rest. (b) `publishAudio()` and `reconfigure()` read the feeder's published playing flag and playhead, which show a command only after the feeder has consumed it, so a Stop that the feeder had not yet applied was undone by the next publication (playhead 40000 after a Stop) or carried across a reconfigure as a play (playing, head 38127), and a Pause was undone the same way. **What changed.** `MultichannelPlaybackFeeder::ControlScript` and `apply()`: every command is checked by the same rules as the single commands, then all are queued with one store of the write index, or none (`ControlQueue::pushAll`). A script that does not fit is a Conflict and each of its commands counts in `rejectedCommands`, as a single command that finds the queue full does; a script that is invalid, or larger than the whole queue, is InvalidArgument and counts nowhere. `acknowledgedCommands()` counts the commands the feeder has consumed and is stored, with release order, after the state that includes them has been published (also while the feeder waits for a ring reset). `ControlScript::projectedFrom()` says what a script leaves the playing flag and the playhead as; the feeder tests compare it with what the real feeder does on fixed rows and on 3,000 pseudo-random scripts, so the two rule sets cannot drift apart unnoticed. `TransportController` sends each of `publishAudio`, `clearAudio`, `play`, `pause`, `stop`, `seek` and `setLoop` as one script and records what it asked (the projected playing flag and playhead, and how many commands it has sent); `publishAudio()` and `reconfigure()` go by that record until the feeder's acknowledged count reaches it, and by the feeder's own report after. A refused call queues nothing and leaves both the feeder and the record as they were, so asking again is the whole recovery and a failed clear is never treated as settled. `reconfigure()` takes its snapshot after the service has stopped, and the record starts again with the new feeder. The new feeder cases are in `tests/test_multichannel_control_script.cpp`, registered in `seam_tests` and in the fast `seam_authoring_transport_tests` binary. **Evidence.** Failing first: against `5d4315b8`, 10 of the 11 new transport cases fail (the eleventh guards the new record and passes there, because the old code reads the feeder directly); the 14 new feeder cases need the new API and cannot be built there. Full Release build and ctest 221/221; Debug focused binaries: transport 56 (31 before), runtime 37, coordinator 36, characterization 10, `seam_u2_tests` 96. Mutation campaign on the feeder and the controller: 44 compiled mutants, 42 killed. Four mutants did not compile under `-Werror` and were redone as compiling variants, and one early survivor (single commands no longer validated after the refactor) led to a parity case. The two survivors are keeping the replaced feeder's record after a reconfigure (equivalent: every reader either sends first or finds the record agreeing with what the reconfigure carried) and a refused `play()` staying armed (not testable deterministically, because `play()` starts the service before it sends, so the queue cannot be held full there; `pause()` and `stop()` have the same guard and it is tested). Two mutants were judged equivalent and not run: the empty-script short-circuit, and `>` against `>=` on the projection's timeline clamp, which assigns the same value. The race check (200,000 commands between a queueing, a feeding and a watching thread) kills the mutant that acknowledges before it publishes; it ran without ThreadSanitizer. **Limits.** The projection assumes the feeder accepts a timeline it is sent (one it cannot prepare is dropped there and counted in `mixFailures`, which no record can predict). For a script that does not set the playhead (a play, a pause, a loop that does not move it) the record keeps the playhead as it was when the script was sent, so a reconfigure before the acknowledgement carries that position: exact with the service stopped, and behind by at most the milliseconds the feeder kept playing with it running. `state().playing` and `state().playhead` still report the feeder's own, lagging values, as the header says. The new order inside `reconfigure()` cannot be told apart by a deterministic test. **Open after this.** The previous entry's items (2) CLAP publication pressure, (3) `invalidateCurrent()` and the request-id overflow settling the displayed progress, and (4) the plug-in's silent refusal, `followSelectionOnHost` and the outstanding live observations are unchanged. The second developer has not reviewed `35b294b2`, `14d3704d`, `5d4315b8` or `cf538128`. Not run: ThreadSanitizer, Debug `seam_tests`, `scripts/build_phase13a_formats.py` against the new CMake identity block, and any live window, host, DAW, VoiceOver, listening, signing, Windows or external-review evidence. Usable Alpha and External Beta are not promoted; Windows remains a README TODO. **Correction (2026-10-01).** (1) "a failed clear is never treated as settled" held for the transport controller's API only: the runtime discarded the refusal and settled the coordinator as idle (the second developer's P2 at `aac143a2`), which `b3d0394f` fixes at the five call sites that discarded or ignored a transport result. (2) "so the two rule sets cannot drift apart unnoticed" was too wide; the feeder tests guard the tested projection cases (fixed rows and 3,000 pseudo-random scripts), as the second developer asked. (3) The commit message of `cf538128` says that 8 of 9 new transport cases fail against the old code; the complete run against `5d4315b8` shows 10 of the 11 new cases failing (the eleventh guards the new record), which is the figure in this entry. (4) The second developer has since reviewed it: ACCEPT WITH NOTES for the controller and feeder all-or-none admission and intent tracking, with notes that a refused `play()` still starts the service and later drains accepted controls, and that acknowledgements are not an atomic head, playing and count tuple; see the entry above.

2026-10-01 — The cancel and reset admission step, the seam admission follow-up, and the second developer's final verdict on the seam and Stop units (cross-cutting; they complete and advance no plan unit), pushed in `35b294b2` and `14d3704d`; GitHub CI remains deferred and `.github` was not touched. **Verdicts.** The second developer's final bounded verdict at `28332121` (implementation units freshly compiled against pinned and frozen support archives, all artifacts retained under `/tmp/seam-review-2833-II2Tiz`; no independent full CTest, Debug, mutation, sanitizer, live-app, host, DAW or listening run): `6e13b63c` REJECT as a completed seam-currentness unit, for one reproduced P2, the replacement case (repair (2) below); `2c9617d4` ACCEPT WITH NOTES (both original saved-position probes now give 0, so the specific Stop P2 is closed, and a Stop keeps the loop selection while Clear Audio clears it, a distinction to keep documented and tested); `28332121` ACCEPT WITH NOTES (corrections (a) to (e) of the previous entry carry the earlier review accurately; its item (f), the broad closure wording, the wording of the mutation survivor and "microseconds" need the qualifications recorded as a correction on that entry). Their lock review found no inversion in the current call graph (source review and deterministic tests, not TSan or live proof) and asks that future admission locking keep notifications and reentrant cancellation out of the critical section; they also agree that keeping the seam flags when a completion finds no current audio and the progress is cancelled, idle, queued or rendering is correct for the current owners, since clearing them unconditionally would reintroduce the earlier bug, while noting that this does not give request-generation ownership of success and failure updates. Their counts: 185 committed cases pass (runtime 36, coordinator 34, transport 31, waveform 5, lifecycle 69, characterization 10); all 14 earlier probes pass; of five new probes two pass (single Stop, retained loop) and three fail (seam admission and two held-feeder Stop cases); of four retained `c73fefaa` edge probes three pass and one fails (the double Stop now passes; the partial-publication queue failure remains); two new held-feeder baseline comparisons both fail at `c73fefaa`. **Repairs.** (1) `35b294b2`, the cancel and reset admission step (the review's item B and the previous entry's open item (1)): `cancel()` dropped the pending request and zeroed the request id under `mutex_`, then, outside it, read the progress and wrote "cancelled" unconditionally, so a request admitted in between and reported as queued had that report overwritten by the cancellation of the request before it (their instrumented sequence: queued 801, cancelled 800, ready 801), leaving a render that was about to run on screen as cancelled; and `resetToIdle()` dropped the queue in one critical section and zeroed the request id, the revision floor and the progress in a second, so a submission admitted in between was revoked by the second half while the worker held it, rendered for nothing and was discarded as stale (queued 803, idle, completed 0, stale 1; the review characterizes this as a split reset and admission, not a natural lost update, because a concurrent reset may legally win). Both are now one critical section under `mutex_` with `progressMutex_` taken inside it in the order submission already uses; the completion notice, the cancelled count and the wake-up stay outside; `cancel()` reads its revision under the lock; `shutdown()` is unchanged, since it already gates new admissions. Two test-only barriers, `duringCancel` and `duringReset`, sit after the revocation and before the report with the lock held. Coordinator 36 in Debug (was 34); against the committed bodies plus only the two barrier calls in the old gaps both new cases fail (the cancel case at the check that request 801 is on screen as queued, the reset case at the check that 803 renders to ready); 14 effective mutants, 13 killed, one redone after a compile failure; a first pass left the revision of the cancelled report untested and an existing case now asserts it; one survivor, `wasActive` in `cancel()`, which differs only for a second cancel, or a cancel after a reset, while an abandoned render is still winding down (the cancellation is reported again), unspecified, unchanged and not covered by the tests. A split placed immediately after a barrier cannot be seen from outside. (2) `14d3704d`, the seam admission (the review's P2 on `6e13b63c`): `previewSeam(true)` wrote the flags and submitted its request without `performanceAuditionMutex_`, so a completion's currentness check, publication and flag writes were not atomic with a replacement's admission; with the older completion held inside its critical section, a newer request admitted and the older one released, the older published after the newer was asked for and set the ready flag before the newer had rendered. `previewSeam(true)` now takes the mutex for the flag writes and the submission (the coordinator's admission lock inside it, the order every path uses; the seam coordinator has no progress callback, and the comment records that one which called back into the runtime would deadlock); cancellation stays outside the lock. By the same change the residual listed for `6e13b63c`, a failed completion clearing a newer request's flags in the gap between its flag write and its admission, is closed by construction, because the failure's clear and the newer request's flag write and queued report are under one lock; the failure path has no barrier, so this is argued from the code and not separately tested. Runtime 37 in Debug (was 36); the new case fails on `6e13b63c` at the check that the request is still waiting while the older publication holds the lock; 4 effective mutants, all killed. **Verification.** For each unit a full Release build and ctest on its own tree (221/221) and the Debug focused binaries (after (1): coordinator 36/36, runtime 36/36, transport 31/31, characterization 10/10, `seam_u2_tests` 96/96; after (2): runtime 37/37, characterization 10/10, `seam_u2_tests` 96/96); each commit was pushed alone and checked against `origin/master` and `git ls-remote`. Not run: TSan, Debug `seam_tests`, and `scripts/build_phase13a_formats.py` against the new CMake identity block. **Open, not fixed here.** (1) The transport's publication is not transactional and reads stale feeder state: `publishAudio()` enqueues the timeline, the loop, the seek and the play one at a time before it commits its metadata (with the service stopped and 63 of 64 control slots filled the timeline is accepted and the loop refused, `available` stays false, `clearAudio()` returns success as a no-op, and starting and playing produces non-zero PCM: fault injection through the public API at `456399d3` and `c73fefaa`, not a live observation), and two pre-existing unacknowledged-command gaps reproduce against `c73fefaa` by holding the feeder with `shutdown()` (scheduling fault injection): after a seek the feeder acknowledged, a Stop followed by a replacement publication restarts at the old playhead because `publishAudio()` reads the feeder's still-old head when no reconfigure position is saved, and the same while playing with a reconfigure in between restarts playing at a stale head because `reconfigure()` snapshots the feeder's stale playing flag and head. The design to reach is an authoritative desired-state record and all-or-nothing command admission, with a failed clear never treated as settled. (2) CLAP: `editor_runtime_preview.cpp` ignores the result of `publish(false)`, so with read handles held across renders, an emptied project and the handles released, the preview keeps its old samples on an idle status and a further empty request does not retry; revocation must become authoritative independently of free PCM slots, then retry materialization without blocking the realtime thread (the previous entry's item (3), still open). (3) `invalidateCurrent()` and the request-id overflow branch revoke without settling the displayed progress; the overflow cannot be reached without a hook. (4) The plug-in's silent refusal and `followSelectionOnHost` notes, and the outstanding live observations (the romaji lyric "ko" reporting the sound "pau", "Untitled • edited" on a fresh project, the SCENE look, removing the last track, multi-note phrases and the VOICE, TUNE and MIX workspaces), remain open. The review's plan for what follows this bounded cluster is a frozen macOS UI-only journey (create or install a singer, song, edit and play, save, reopen and recover, master and stem export) with listening evidence, since broader test growth is not singer qualification. Limits: automated tests on one machine; no live-app pass of any of these changes; no host, DAW, VoiceOver, listening, signing, Windows or external-review evidence beyond the second developer's source reviews and probes; Windows remains a README TODO; Usable Alpha and External Beta are not promoted.

2026-10-01 — The second developer's final verdicts on the four repairs, and the two reliability units they asked for first (cross-cutting; they complete and advance no plan unit), pushed in `6e13b63c` and `2c9617d4`; GitHub CI remains deferred and `.github` was not touched. **Verdicts.** The second developer's final bounded review at `c73fefaa` (pinned source, the five changed implementation units rebuilt fresh, frozen support archives; no repository edits or builds by them): 174 committed focused cases pass (runtime 31, coordinator 34, transport 25, waveform 5, lifecycle 69, CLAP characterization 10), all 14 earlier probes pass (13 unchanged; the in-flight empty-selection probe now waits for the replacement's "ready" instead of expecting a stale counter), and new uninstrumented edge cases gave 2 passes and 3 failures; a separate instrumented coordinator copy fails one cancellation expectation and passes the reset characterization. Their counts are not to be combined into a release pass, and they did not run the full CTest, Debug, mutation or sanitizer suites. `10fa62e4` REJECT as a completed transport-intent unit, pending one P2 (a Stop leaves the saved playhead); the original clear-after-reconfigure regression itself is fixed. `0d84a43d` ACCEPT WITH NOTES for the same-request "queued" ordering. `3118fa6e` ACCEPT WITH NOTES (whole-score composition correct; keep the selected empty performance target). `fd3de1ee` ACCEPT WITH NOTES for ordinary idle clearing, while the publication-pressure contract stays incomplete. `c73fefaa` ACCEPT WITH NOTES for the evidence boundary and corrections, with the wording fixes recorded as a correction on the previous entry. No release promotion. The review's plan for what follows: finish the seam-preview unit, then the Stop unit, then one admission settlement for cancel and reset, then the transport and CLAP publication-failure semantics, each with focused baseline-red and current-green checks; and after that bounded cluster, shift effort to a frozen macOS UI-only journey (create or install a singer, song, edit and play, save, reopen and recover, master and stem export) with listening evidence, because broader test growth is not singer qualification. **Repairs.** (1) `6e13b63c`, seam preview (the previous entry's open item (2), confirmed by the review from the source): `publishCompletedSeamPreview()` published the coordinator's latest audio with `acquire()`, which does not ask whether its request is still current, and took no lock, so a seam preview whose render had finished could reach the transport after it was revoked: after the score was emptied, or after the A/B switch restored the canonical audio, in the gap between the render finishing and its completion arriving. It now takes `performanceAuditionMutex_`, asks for the audio of the newest request that has finished (`acquireCurrent()`) and publishes inside that critical section; when there is none it leaves the flags alone unless the request failed, because cancelling or replacing a request does not stop its completion from arriving and the progress it reads is then the cancellation's or the newer request's (before, such a completion cleared a newer seam request's "wanted" flag, so a second preview asked for while the first one's completion was on its way was forgotten). `revokeSeamPreview()` replaces the five places that cancelled the seam coordinator and cleared its flags (an edit, undo, redo, the A/B restore, `handleDocumentChanged`): it cancels first and clears the flags under the same mutex, so a completion is entirely before the revocation (what the revoker publishes next replaces it) or entirely after (it finds its request cancelled), and it cancels outside the lock because the coordinator delivers the completion on the cancelling thread when a render was in flight, as `stopPerformanceAudition()` already does. Two test-only barriers in `AuthoringRuntimeConfig` hold a completion outside and inside the critical section. Five new runtime cases (36 in Debug, was 31); against the previous function body plus only the barrier calls, three failed, each at the assertion that names the defect (the transport available again after the score was emptied; the older completion ending the newer request; the restore not held back by a completion that was publishing), and the other two pin behaviour the old code already had (a failed seam render stops being wanted; every edit, undo, redo and changed document ends the preview). Mutation campaign on the final shape: 17 effective mutants, 16 killed; the one survivor announces a preview that was not published, which only refreshes and is unchanged behaviour, since nothing outside the runtime reads the two flags. A first campaign on the earlier shape, which read the progress before the currentness check, left a second survivor (an absent current handle still reaching the transport) that differed only when a newer request was admitted between that snapshot and the check; the function now asks `acquireCurrent()` first, which removes that class instead of recording it. (2) `2c9617d4`, Stop (the review's P2 on `10fa62e4`): a reconfigure empties the timeline and keeps the loop, the play request and the playhead for the audio that follows, and `stop()` cleared the carried play but not the carried playhead, so after seek 40000, reconfigure to 44.1 kHz, Stop, reconfigure to 48 kHz the next publication started at 40000 (36750 after a single reconfigure; the single case already failed at `456399d3`, and `10fa62e4` made a second reconfigure carry the same stale record; these figures are the second developer's). `stop()` now sets the saved playhead to 0 while one is carried; a pause and a play leave it alone and the loop is untouched. The `clearAudio()` header and comment, which said a play "asked for before any audio ever existed" stays, were too narrow and now say that a play asked for while the transport holds no audio, and none was dropped by a reconfigure, stays armed whenever it was asked; the test of that name is renamed and a new case pins the armed play after a clear. Six new transport cases (31 in Debug, was 25): three fail on the previous tree at the playhead check, and the pause, play and armed-play cases pass before and after. Mutation: 6 effective mutants, all killed, including pause or play also rewinding the saved position. **Verification.** For each unit a full Release build and ctest on its own tree (221/221, with the next unit's tests kept out of the seam-preview tree) and Debug focused binaries: runtime 36/36, coordinator 34/34, transport 31/31, characterization 10/10, `seam_u2_tests` 96/96; each commit was pushed alone and checked against `origin/master` and `git ls-remote`. Not run: Debug `seam_tests`, and `scripts/build_phase13a_formats.py` against the new CMake identity block. **Open, not fixed here.** (1) The admission settlement for `cancel()` and `resetToIdle()`: the review's instrumented copy, which adds only barriers just after each releases `mutex_`, deterministically shows the cancel gap as queued 801, then cancelled 800, then ready 801, and the reset gap as queued 803, then idle, after which request 803 renders and is discarded as stale (completed 0, stale 1); the second is a characterization of a split reset and admission, since a concurrent reset may legally win unless a stronger contract is specified, so it is not claimed as a natural lost update. The fix to make is to consolidate queue removal, revocation, the revision floor and the terminal report under the admission lock and notify outside it; `shutdown()` already gates new admissions and is not to be equated with `cancel()`. (2) `TransportController::publishAudio()` is not transactional: it enqueues the timeline, the loop and the seek one at a time before it commits its metadata, so with the service stopped and 63 of 64 control slots filled, the timeline is accepted and the loop is refused, `available` stays false, `clearAudio()` returns success as a no-op, and starting the service and playing produces non-zero PCM (a fault injection through the public API, not a live observation; it reproduces at `456399d3`). The design to reach is transactional admission or one authoritative desired-state snapshot, and a failed clear must not be treated as settled. (3) CLAP: `editor_runtime_preview.cpp` ignores the result of `publish(false)`: with three read handles held across renders, the project emptied and the handles released, a further empty request does not retry because the reset was already idle, leaving 271168 old preview samples on an idle status. Revocation must become authoritative independently of free PCM slots, then retry materialization without blocking the realtime thread. The normal preview contributes no PCM once empty, offline export uses its own publication and refuses anything not ready, and live MIDI voice is separate, so this is not a claim that all sound is muted. (4) A failed seam render's completion can still clear a newer seam request's flags if it reads the failure in the microseconds between that request's flag write and its admission; the flags have no consumer outside the runtime beyond gating the canonical publication. (5) The previous entry's items (4) `invalidateCurrent()` and the overflow branch, (5) the plug-in's silent refusal and `followSelectionOnHost`, and (6) the outstanding live observations remain open; its item (2) is closed by this entry and its item (3) stands as argued from source. Limits: automated tests on one machine; no live-app pass of any of these changes; no host, DAW, VoiceOver, listening, signing, Windows or external-review evidence beyond the second developer's source reviews and probes; Windows remains a README TODO; Usable Alpha and External Beta are not promoted. **Correction (2026-10-01, the second developer's final verdict at `28332121`).** (a) `6e13b63c` closed the revoked and before-acquisition cases of the seam preview's currentness and not the replacement case: `previewSeam(true)` wrote the flags and submitted without `performanceAuditionMutex_`, so an older completion could publish after a newer request was made and mark it ready before it had rendered. The second developer reproduced this with the two committed barriers and rejected `6e13b63c` as a completed unit; it is closed by `14d3704d`. The currentness closure claimed above therefore holds for the tested revoked and before-acquisition cases only, and "a successful handle check" is not a durable guarantee once another request can be admitted. (b) "The one survivor announces a preview that was not published ... unchanged behaviour" is too strong: the previous and the committed code both return before notifying on a failure, whereas removing that guard invokes an observable completion callback, so it is notification-only behaviour that the tests do not cover; it is not claimed equivalent. (c) "Microseconds" described a preemptible scheduling gap, which has no bound; that residual is closed by construction in `14d3704d`, not bounded. (d) The review accepted `2c9617d4`: both original saved-position probes give 0 and a Stop keeps the loop selection while Clear Audio clears it. (e) Two pre-existing unacknowledged-command gaps in the transport (a Stop followed by a replacement publication restarting at the old playhead, and a Stop followed by a reconfigure restarting playing at a stale head) reproduce against `c73fefaa` by holding the feeder with `shutdown()` and are tracked with the desired-state and transactional admission work; they are not part of the Stop repair.

2026-10-01 — The second developer's review of `456399d3` and the four repairs it led to (cross-cutting; they complete and advance no plan unit), pushed in `10fa62e4`, `0d84a43d`, `3118fa6e` and `fd3de1ee`; GitHub CI remains deferred and `.github` was not touched. **What was reviewed.** `071c1c2d`: a project that has rendered nothing no longer captions its waveform "Rendering" (`RegionWaveformRequest` gained `render`; an idle publication reads "No render" and clears the cache, unless the render is queued or rendering). `456399d3`: a missing bank stays on record until it resolves, and an emptied score settles on idle. Diagnostics are recorded with provenance: the outcome of a render attempt is hidden while a newer attempt is queued or rendering and is cleared by a success, while "vocal tracks exist and none has a bank or recipe" is no longer a record but is derived from `bankUnavailable_`, which every composition of a render request sets, so an in-flight render never hides it, a success and a dismissal never clear it, and it goes when a request is composed with a usable bank. A score with nothing audible settles: the attempt outcomes are dropped, `TransportController::clearAudio()` (new) drops the transport's audio under `performanceAuditionMutex_`, and `AuthoringRenderCoordinator::resetToIdle()` (new) stops the active render, drops a pending request, zeroes the high-water marks and publishes idle, counting abandoned work in `stats().cancelled` but reporting no cancellation. Progress writes became request-aware (`updateProgressIfCurrent`), with two test hooks (`afterAdmission`, `afterSubmitAdmission`). The two commits added 6 coordinator, 12 runtime, 3 transport and 1 waveform cases and were mutation-tested with 36 mutants (runtime 17, of which 16 were caught and one survivor is equivalent; coordinator 11, transport 6 and waveform 2, all caught; three mutants did not compile under `-Werror` and were redone). Their commit message says the transport cases "failed first", which overstates it: they were not run against a stub, and mutant T1 fails the first of them. **The second developer's verdicts at `456399d3`** (source review plus 14 independent probes, of which 9 passed and 5 failed, and the 150 committed focused cases; not the full Release ctest, Debug, mutations, sanitizers, a live app, DAW, VoiceOver, listening, signing, Windows or an external review): `071c1c2d` ACCEPT; `456399d3` REJECT as a completed unit, for one reproduced P2 in the new reset contract, with the earlier missing-bank P2 closed; the derived bank flag ACCEPT WITH NOTES; the request-aware progress guard ACCEPT WITH NOTES, plus one newly reproduced race; the transport settle REJECT pending that P2; leaving a non-empty score alone ACCEPT WITH NOTES, with a separate composition defect; the CLAP gap confirmed; the wording ACCEPT WITH NOTES after corrections. The five failing probes were one transport reset, one same-request ordering, two empty-selection cases and one CLAP case; the last three also fail against the frozen `750a8b6e` sources, so they predate the idle work. **Repairs.** (1) `TransportController::clearAudio()` returned early whenever the timeline was empty, yet a reconfigure empties the timeline while keeping the loop, the play request and the playhead for the next audio: play, reconfigure 48 to 44.1 kHz, clear, publish resumed playing and kept the old loop, so the header's "the next publication starts silent" was false in that order (clear then reconfigure was already right). Repaired in `10fa62e4` with a new `audioDroppedByReconfigure_` state, set when a reconfigure empties a published timeline, remembered across repeated reconfigures and cleared by the clear and by the next publication; `clearAudio()` is a no-op only when the transport holds no audio and none was dropped. A first draft treated every carried play as dropped audio and the full suite caught it: `native editor audio settings resume a playing transport after restart` pins that a play asked for before any audio existed survives, and the contract now keeps it. The same bookkeeping had a neighbouring defect: a second reconfigure before the audio was rendered again took "was playing" and the playhead from the feeder the first had just replaced, and lost both; it now carries what the controller recorded while nothing is published, and trusts it only then, because the feeder stops itself at the end of the audio while the request to play is remembered. The header now states the asynchronous boundary (`state()` reports `available`, `loop`, `publishedRevision` and `timelineEnd` at once; `playing`, `playhead` and the ring follow). New focused target `seam_authoring_transport_tests`: 25 cases (was 15 in `seam_tests`, which still contains them); 19 effective mutants, 18 killed, one equivalent survivor (publishing not resetting the new flag: while audio is published the audio term decides, so it cannot be observed); two self-assignment mutants did not compile and were redone. (2) A same-request ordering race in the coordinator: with request 600 held, replacement 601 admitted and its submitter held after admission, the running worker takes 601 and finishes it before the submitter reports "queued", and the late report overwrote "ready" because the guard checks which request a write is for and not its phase. Repaired in `0d84a43d` by reporting "queued" inside the critical section that admits the request, under `mutex_`, the only place the worker can take a request from (lock order `mutex_` then `progressMutex_`; nothing takes them the other way round; no callback or hook runs under either). The comment that overstated the old ordering, the `afterSubmitAdmission` note and `resetToIdle()`'s accounting note were corrected. Coordinator 34/34 (was 32); 7 effective mutants, all killed, including one that restores the old ordering; one more did not compile and was redone. This removes the window named in the previous entry's open item (2), a cancel landing between admission and the "queued" report; the cancel's own terminal write is still outside the lock (open item 1 below). (3) Composition, pre-existing: with an empty region selected, `makePreviewRequest` composed no request, so a gain edit was never rendered and a superseded render left the status on "rendering" for good, although the renderer renders every region that has notes and uses the selected region only to choose whose performance it reports. Repaired in `3118fa6e`: no request exactly when nothing is audible anywhere in the score; the selected region stays the performance target even if empty, and then reports no cues and no unit plan. The header's claim that a selected region with no notes does not make the score empty, which the code contradicted, is now true; `diagnostics()` and `bankUnavailable_` now state the contract the review asked for (the bank assessment is made when a request was last composed, not a live look at the resources, so a bank bound directly through `VoicebankSession::bindTrack` is reported only after the runtime composes again, which `handleDocumentChanged` does). Runtime 31/31 (was 29; one test replaced by three); 6 effective mutants, all killed, one redone after a compile failure. The state is reachable in the editor because the MIX workspace selects regions; it was not observed in a live window. (4) CLAP, pre-existing: after the reset the completion callback made `publishPreviewFromAuthoring` republish the coordinator's retained history, so the plug-in went on offering a deleted vocal (289870 samples in `renderedPreview()` with the score empty and the status idle). Repaired in `fd3de1ee`: an idle coordinator empties the preview, and every other state still leaves the last audio on offer, so a failed or cancelled render keeps the previous one playable. The reset reaches the plug-in synchronously on the thread that made the change, after `previewMutex_` and `performanceAuditionMutex_` are released; the re-entry into the recursive `EditorRuntime::mutex_` on that thread is not the earlier cross-thread inversion, and no reset path runs from the preview worker. The plug-in still does not register the progress callback. Characterization 10/10 (was 9); 4 effective mutants, all killed. **Verification.** For each unit a full Release build and ctest on its own tree (221/221; 220 plus the new transport target) and the Debug focused binaries named in its commit message; each commit was pushed alone and checked against `origin/master` and `git ls-remote`. Not run: Debug `seam_tests` for these units (the Release `seam_tests` passes inside ctest), and `scripts/build_phase13a_formats.py` against the new CMake identity block. **Open, not fixed here.** (1) `cancel()` and `shutdown()` still write their terminal progress outside `mutex_`, and `resetToIdle()` revokes and publishes idle after releasing it, so a submit on another thread that lands in between can be overwritten or revoked; no test hook covers it. (2) Read from the code, not probed (the second developer's source-derived follow-up): `publishCompletedSeamPreview()` publishes `acquire()` to the transport without `performanceAuditionMutex_` or a currentness check. (3) The window between a finished render's currency check and its publication, relative to `clearAudio()`, is argued from source; the second developer agrees it holds by source and did not force it. (4) `invalidateCurrent()` and the request-id overflow branch change authority without settling the displayed progress; the overflow is untested at `uint64` exhaustion. (5) The plug-in's silent refusal and `followSelectionOnHost` notes from the previous entry remain open. (6) Observed in an earlier live pass and not investigated: the romaji lyric "ko" reporting the sound "pau" (hypothesis only: the phonemizer turns unreadable Latin text into a pause token), "Untitled • edited" on a fresh project,  Not yet looked at live on the new build: the SCENE look, removing the last track, multi-note phrases and the VOICE, TUNE and MIX workspaces. Limits: automated tests on one machine; no live-app pass of any of these changes (a live pass on an intermediate build was blocked by a pending macOS Downloads-folder permission prompt); no host, DAW, VoiceOver, listening, signing, Windows or external-review evidence; Windows remains a README TODO; Usable Alpha and External Beta are not promoted. **Correction (2026-10-01, the second developer's final review at `c73fefaa`).** (a) "no callback or hook runs under either" in repair (2) holds for the new admission block only: `beforeDebounceWait` and `afterDebounceWait` are hooks that intentionally run with `mutex_` held. (b) "`bankUnavailable_`, which every composition of a render request sets" means the composition in `requestPreviewImpl`, not every transient `makePreviewRequest`; a seam preview composes one and does not set it. (c) "Nothing audible" in repair (3) means that there is no render candidate (no backing audio and no note on a track that has something to sing it), not acoustic silence: it does not apply mute, solo, routing or phoneme coverage. (d) In repair (1) and the transport header, "a play asked for before any audio ever existed" was too narrow: a play asked for while the transport holds no audio, and none was dropped by a reconfigure, stays armed whenever it was asked (the wording was repaired in `2c9617d4`). (e) The CLAP repair removes the preview the plug-in offered; it does not mute all sound: the normal preview contributes no PCM once empty, offline export uses its own publication and refuses anything not ready, and live MIDI voice is separate. (f) Open item (2) was closed by `6e13b63c`, and the Stop position P2 that the review found in repair (1) by `2c9617d4`; the review's rejection of `10fa62e4` as a complete unit stood until that commit.

2026-10-01 — The render status no longer reports a cancellation nobody asked for or a failure a newer render has replaced (cross-cutting repair; it completes and advances no plan unit), verified and pushed in `ee7e98f6`; GitHub CI remains deferred and `.github` was not touched. **What the live exploration showed.** A fresh empty project's status bar read "CANCELLED 0%" with the message "Production render request cancelled", and after the creator fixed an uncovered lyric the old "Render did not complete" banner stayed for about 2.5 s, until the new render ended. The previous entry recorded both as not investigated. **Causes, established from source and by the new tests, not by a live measurement.** (1) `AuthoringRenderCoordinator::cancel()` published "cancelled" unconditionally, and the application's start-up refresh of its voicebank browser calls it to revoke the authority of any earlier sound; an empty project has no request to replace that state. The same publication also relabelled a render that had just finished as cancelled when a late cancel arrived. (2) The last failure stayed on record until a render succeeded, so the banner and the mascot's error state outlived the fix; and the window paints on demand (the AppKit run loop paints only after a repaint was requested), the debounced submission happens after the frame of the edit itself, and the coordinator's only notification fired when an attempt ended, so no frame was painted for the new attempt until it was over. **Repair.** `cancel()` still revokes the audio that was current, but reports a cancellation only when a request was pending, being rendered, or shown as queued or rendering; a coordinator with nothing in flight stays idle and sends no completion notification. The coordinator gained `setProgressCallback`, fired when a request is queued and again when its render starts, and the standalone session turns it into a repaint request. `AuthoringRuntime::diagnostics()` leaves out the render-outcome family (`BANK_MISSING`, `BANK_UNTRUSTED`, `RENDER_FAILED`, the same three a successful render clears) while a newer render is queued or rendering; the record is kept, so the failure returns if that attempt is cancelled, and an unrelated diagnostic is never hidden. `AuthoringRuntimeConfig` gained `renderHooks` so a test can hold a render in flight. The CLAP editor does not register the callback: its status refresh takes a mutex that the submitting thread's caller may hold in the other order. **Failing tests first.** Seven new cases failed on their own assertion before the change (three coordinator cases, the failure and the cancelled-retry runtime cases, the fresh-project idle case and the repaint-count case); the queued case was added afterwards. **Mutations.** Fifteen distinct mutations were run and thirteen were caught by named cases (never superseded, queued or rendering left out, always superseded, `RENDER_FAILED` outside the family, an unrelated diagnostic hidden too, unconditional cancel, a cancel that sees only a pending request, no notification at queued or at rendering, the progress callback wired to the completion callback, the session not registering the callback, the session asking for nothing). Two survive: dropping the worker's active flag from the in-flight test in `cancel()`, or dropping its queued and rendering state pair. Each is redundant with the other signal in every deterministic case and guards a race window (the worker between picking a request up and publishing "rendering"; the render ended but not yet published) that the coordinator has no test hook for, so they are kept as defensive terms and not claimed as tested. The first run of the campaign passed the runner a two-word filter that it rejects with a usage message, so most of it executed nothing; it was rerun with the runner's summary printed, and only the second run is counted for those mutations. **Verification.** Full Release build and ctest 220/220. Debug: render coordinator 26/26, authoring runtime 17/17, `seam_u2_tests` 96/96, `seam_tests` 1203/1203. **Second developer's final verdicts at `c3e23ff8`** (source review plus their own probes: 90 committed cases and 15 independent probes; not the full Release ctest, Debug, mutations, a live app, DAW, VoiceOver, listening, signing, Windows or an external review): `d550a688` ACCEPT WITH NOTES with the original P2 closed; `98736e2a` ACCEPT; the previous ledger entry ACCEPT WITH NOTES. Their notes, none blocking: the silent refusal of a mixed selection in the plug-in is safe but should become a non-modal notification reusing the existing Conflict message, dispatched after the runtime mutex is released, de-duplicated and without dirtying the document; for `followSelectionOnHost` they prefer an observable failure with a bounded retry at the owner or event boundary over healing in paint (paint should render state and not be the only synchronisation guarantee; rendering, export and technical commands can happen before paint; a persistent refusal could loop at frame rate), keeping the committed structural edit and retrying only the selection sync; and "No voicebank unit covers the sound" is literally "no eligible unit continues the chain under the current eligibility, forced and style constraints", not proof that the raw inventory holds no recording of that sound. **Open, not fixed here.** (1) The plug-in refusal has no report channel and `followSelectionOnHost` still discards a host callback's refusal; the second developer's design above is not implemented, and my earlier inclination to heal in paint is withdrawn. (2) Read from the code, not reproduced: `submitWithSources` publishes "queued" after releasing its lock, so a `cancel()` that lands in that window can be overwritten by the late "queued". (3) Unchanged: the status line still shows the coordinator's own English strings ("Production render request queued", "Production render in progress"), which are not localised, and the CLAP editor still refreshes its status only at completion and at its own refresh points. (4) The Delete key being a no-op in MIX was not investigated. (5) Hover and accessibility focus (`interaction_`) and the other per-place state named in the previous entry were not audited. Limits: automated tests on one machine; no live-app pass of this change (the effect of the extra repaints on a real window, and the status bar reading "IDLE" on a fresh project, were not observed); no host, DAW, VoiceOver, listening or external-review evidence; Usable Alpha and External Beta are not promoted. **Correction (2026-10-01).** "An unrelated diagnostic is never hidden" was too wide: only diagnostics outside the three-code family were guaranteed to be kept, and a standing `BANK_MISSING` inside the family was hidden too while a backing-only render was queued or rendering, and the clear-on-success that predates this change then removed it for good. The second developer reproduced it and rejected this repair as a complete fix; `456399d3` replaces the family rule by provenance (see the entry above).

2026-10-01 — Uncovered sounds named in a failed render, the plug-in's public selection methods made selection-safe, and the second developer's final verdicts on the two previous units (cross-cutting repairs; they complete and advance no plan unit), verified and pushed in `98736e2a` and `d550a688`; GitHub CI remains deferred and `.github` was not touched. **Independent verdicts.** The second developer's bounded review at `0824405e` rests on source review and their own probes, not on a full test, live-app or DAW run: `f58b33d8` ACCEPT WITH NOTES, `e7f682cb` ACCEPT WITH NOTES, and the previous ledger entry ACCEPT WITH NOTES (qualify "every move", record the public plug-in gap, keep the automated-only boundary). They ran 45 existing cases and ten new probes against pinned source and snapshotted Release libraries: eight probes passed (MIX pointer and semantic switching followed by Delete; arrangement accessibility switching and rejection of the stale note node; a plug-in forward duplicate followed by the editor's own Undo, Shift-Z and Y; same-target and rejected-target selection preservation; an identical-ID project replacement clearing the selection; harmony creation clearing the lead selection; lyric batch and off-screen note cases; a callback-refusal characterization) and two exposed the fault below. They did not run the full Release ctest, Debug, the mutation campaigns, a live app, listening, a DAW, VoiceOver, signing, Windows or an external review. **The previous entry's claim was too wide (`d550a688`).** "Every move of the editor" held for the nine paths inside `NativeEditorController` only. `EditorRuntime::selectTrack` and `selectRegion` change the plug-in's target and then rebuild the controller around it, and the note selection belongs to the `EditorSession` every controller shares, so the rebuilt controller showed the other region with the lead's note still selected, and a Delete key press through `EditorRuntime::keyDown` removed that note (the second developer reproduced it with both methods; whether a real host GUI calls those public methods was not established by anyone, so this is API-level evidence). Failing tests came first: two plug-in cases (another region, another track) failed at `selectedNoteCount == 0`, and two piano-roll refusal cases and one controller case failed with them; the guard cases (pointing the editor at what it already shows keeps the selection; a selected off-screen note of the region is deleted) passed before and after. The repair has two layers. `NativeEditorController`'s constructor keeps only the part of the shared selection that names notes of its own region (`PianoRollModel::ownedSelection`), and delete, move and resize in the piano roll act only on the selected notes of the region the roll shows: a selection that also names notes elsewhere is refused without touching the document and its stale part is dropped, so the next attempt acts on what is drawn as selected. Region membership decides and never the viewport. Quantize, slur, melisma, duplicate and lyric distribution already filtered by region and are unchanged. Twelve mutations were each caught by a named case and reverted to an identical file. **A failed render says what to change (`98736e2a`).** Live exploration had shown the banner "Render did not complete — Project has no audible rendered tracks: Voicebank cannot cover the phoneme sequence", which names no note, lyric or sound; the bundled voice has no stand-alone /a/, so the default lyric あ failed this way on every fresh project. `DeterministicUnitSelector` now words the failure around the token where the chain of units stops (the furthest boundary a placed unit ends on): `No voicebank unit covers the sound "a" of the lyric "あ" at bar 1, beat 1`, with the style added only when the voicebank has several, the lyric shortened at 24 characters, and the position taken from the project's meter map, which the snapshot factory now hands over through `UnitSelectionContext::meters`. The error code is unchanged (`NotFound`) and the message carries no context, so the project renderer's composition reads as one sentence. Seven selector cases failed first, fourteen mutations were each caught by a named case, three shell fixtures that stood for the captured failure and a script comment carry the new wording, the error-toast case for it still fits the stacked lane in both looks, both contrasts and at 1× and 2× with nothing elided, and one coordinator case runs the bundled demo bank on あ end to end. It names only the first sound the chain cannot pass, so a second gap appears after the first is resolved. **Verification.** Each unit: a full Release build with ctest 220/220. Debug for the render message: contextual selection 23/23, render coordinator 23/23, character surface 40/40 and shell input 98/98. Debug for the plug-in selection repair: `seam_tests` 1195/1195, `seam_u2_tests` 94/94, `seam_clap_design_shell_tests` 9/9 and `seam_design_shell_input_tests` 98/98 on the immediate re-run of a first run that failed one frame-time-budget case ("a long note at maximum zoom walks only its visible waveform columns") while the machine was loaded, with nothing in that path changed. **Open, not fixed here.** (1) `followSelectionOnHost` discards a host callback's refusal: an injected refusal leaves the committed structural edit in place, leaves the host on the previous region, and no later reconcile retries. The built-in `AuthoringRuntime` selection methods fail only with `NotFound` for a target the controller has just created or validated, so this is a contract question, not a proven production fault; the second developer's options (a distinct host-selection-sync diagnostic with a retry and degraded-state policy, or an enforced infallible-notification contract) are not designed or implemented. (2) Not investigated: the old failure text that stayed about 2.5 s after the lyric was fixed (the coordinator publishes "queued" within its 20 ms debounce and the app reads progress when it paints; whether a repaint is requested between queued and done was not established), the "Production render request cancelled" at 0% on a fresh empty project, and the Delete key being a no-op in MIX. (3) Hover and accessibility focus (`interaction_`) were not audited for the same leak. Limits: automated tests on one machine; no live-app pass of either change; no host, DAW, VoiceOver, listening or external-review evidence; Usable Alpha and External Beta are not promoted. **Correction (2026-10-01, second developer's final review at `c3e23ff8`).** In the list of eight passing new probes above, the phrase "lyric batch and off-screen note cases" names cases from the second developer's 45 existing regression cases, not new probes; the count of eight is theirs and stands, but the list without that phrase names seven, and the eighth was not recorded. Two of the three items in open list (2), the old failure text and the fresh project's "cancelled", were repaired in `ee7e98f6` (see the entry above); the Delete key in MIX stays open.

2026-09-30 — Note selection and the seam and Unit targets dropped on every move of the editor (cross-cutting repair; it completes and advances no plan unit), verified and pushed in `e7f682cb`; GitHub CI remains deferred and `.github` was not touched. The previous entry ended on an open item found by reading the code: the six forward moves did not clear what the editor selected in the place it left. Measuring it showed a real fault, not a cosmetic one. With a note selected in the lead region, `duplicateSelectedRegion()` moved the editor to the copy and left the note selected, and a Delete key press then reached `PianoRollModel::deleteSelection()`, which removes every selected note wherever it lives, so the lead's note was deleted while the piano roll showed the copy. Six new cases (add region, add track, split, duplicate region, duplicate track, copy to another track) each failed at `selection().empty()` before the change, and the Delete case failed on `findNote(noteId) != nullptr`. `NativeEditorController::leavePlace()` now owns the four resets (note selection, seam target, Unit target, seam A/B state), and every one of the nine paths that change the editor's track or region calls it once, after the track and region are set and before the piano roll's index is rebuilt: choose track, choose region, the reconcile after Undo, Redo or a removal, add track, add region, split, duplicate track, duplicate region and copy to track. Split drops the selection even when the selected notes moved into the right half; that is deliberately conservative, chosen instead of guessing which half the creator meant. From reading the key handler, Delete with no note selected still removes the region on screen after any of these moves, the rule that already applied after choosing a region. Two more cases (choose track, choose region) cover paths that were already correct and had no test, and two native-controller cases cover the Unit target (R after a duplicate no longer reaches the old Unit's renderer cycle) and the seam target with its A/B state. Thirteen mutations, each caught by a named case and reverted to an identical file: dropping the call at each of the nine sites and removing each of the four resets. The choose-track and choose-region mutations survived until their cases were written, which is how those two gaps were found. An earlier run of the mutation script, started in the background, was killed by the tool harness while one call was removed and left the source mutated; it was restored from a byte snapshot, checked by hash, and the campaign was re-run in the foreground with a restore trap, and no mutated file was ever staged. Verification: a full Release build with ctest 220/220, and Debug `seam_tests` 1189/1189, `seam_u2_tests` 94/94 and `seam_design_shell_input_tests` 98/98, run from the repository root. Limits: automated tests on one machine; no live-app pass of this change and no host, DAW, VoiceOver, listening or external-review evidence; the second developer has not reviewed `f58b33d8` or this unit; Usable Alpha and External Beta are not promoted. Open, not fixed here: `PianoRollModel::deleteSelection` still deletes selected notes wherever they live and would do so again if some other path leaked a selection, and hover and accessibility focus (`interaction_`) were not audited for the same leak. Correction (2026-10-01): "every move" meant the nine paths inside `NativeEditorController`. The plug-in runtime's public `selectTrack` and `selectRegion` rebuilt the controller around another place and kept the note selection; the second developer found it and it is repaired in `d550a688` (see the 2026-10-01 entry above).

2026-09-30 — Shared reftable stack watched for linked worktrees, the host made to follow every move of the editor's selection, and the second developer's review of the two follow-ups (cross-cutting repairs; they complete and advance no plan unit), verified and pushed in `4406bca8` and `f58b33d8`; GitHub CI remains deferred and `.github` was not touched. **Provenance, second round (`4406bca8`).** In a linked worktree of a reftable repository, `git rev-parse --git-path reftable/tables.list` names the worktree-private stack, which holds only that worktree's HEAD. A commit on a branch rewrites the stack every worktree shares, so with no reflog nothing CMake watched changed and the build kept the old SHA. The second developer reproduced it with both generators, and the failing case was written first. The identity block now also watches `<common directory>/reftable/tables.list`, found through `git rev-parse --git-common-dir`, beside the worktree-private list, which still catches a commit on a detached HEAD. `tests/test_cmake_source_commit.py` is 17 cases. Two mutations (no shared-stack watch, no worktree-private watch) each failed the reftable worktree case and were restored byte for byte; the module and the second developer's five independent Git-edge probes pass with the Ninja and the Makefile generators, and the release-gate, tracked-source and external-beta suites still pass. No C++ changed. **The host follows every move (`f58b33d8`).** An audit after the previous unit found that adding a track or a region, splitting a region, duplicating a region or a track, and copying a region to another track each stood the editor somewhere new without telling the host. The authoring session and the runtime kept naming the old track and region, so technical edits, the render's active region and the character performance pointed at a place the editor had left. `NativeEditorController::followSelectionOnHost()` is now the one routine that tells the host where the editor stands (the region, else the track, else `clearVocalTarget`); `reconcileWithProject` and the six operations use it, each before `markDocumentChanged`. An existing test asserted as a precondition that the runtime was not on a track the editor had just added, which pinned the defect; it now checks that the runtime followed and that selection moves both owners without dirtying the project. Three lifecycle cases through the owner-agreement oracle failed against the unchanged code first, one native-controller case pins the exact message the host receives for each operation, and eight mutations (each of the six calls, the reconcile call, the region-first ordering) were each caught by tests that cover them. Verification: the full Release ctest passed 218 of 220 before that test was corrected, and `seam_u2_tests` (85/85) and `seam_tests` (1178/1178) then passed through ctest; Debug `seam_tests` 1178/1178, `seam_u2_tests` 85/85 and `seam_design_shell_input_tests` 98/98, all run from the repository root. **Independent review.** The second developer's verdicts come from source review and their own probes, not from a full test, live-app or DAW run: `41a40998` ACCEPT for the scoped clear-target contract; `f6cbde48` ACCEPT WITH NOTES only together with `4406bca8`, which they ACCEPT; the `fe93df08` ledger entry ACCEPT WITH NOTES (append this evidence, do not broaden it). They compiled the byte-identical committed controller into a pinned probe and ran three plug-in cases through `EditorRuntime`: an audio-only fallback through Undo, Shift-Z and Y; repeated last-track removal and Undo with a stable controller; and a real development bank whose live sample buffer was non-zero, zero after the last track was deleted and non-zero again after Undo. That is sample-buffer evidence, not physical output or listening. They did not review `f58b33d8`. Limits: a typed `-DSEAM_SOURCE_COMMIT:STRING=` pin equal to the recorded default is still indistinguishable from a default; automated tests on one machine; no live-app pass of these edits, no host, DAW, VoiceOver, listening or external-review evidence; Usable Alpha and External Beta are not promoted. Open, from reading the code and not yet measured: `reconcileWithProject`, `selectTrack` and `selectRegion` clear the note selection and the seam and unit targets when the editor leaves a place, but the six forward moves do not, so a note selection may still name notes of the place the editor left.

2026-09-30 — Second-developer P2 follow-ups: provenance through packed refs, and clearing the editing target when no vocal track is left (cross-cutting repairs; they complete and advance no plan unit), verified and pushed in `f6cbde48` and `41a40998`; GitHub CI remains deferred and `.github` was not touched. The second developer's bounded verdict on `2c97e90e` and `05705f98` closed the three original structural bugs and reproduced two residual cases with independent probes; failing tests reproduced both here before any change. **Provenance (`f6cbde48`).** With reflogs off and the branch packed, no loose ref file exists when CMake configures, so the next commit creates the only file that changes and nothing re-ran configure; configuring before the first commit failed the same way. Configure now also watches the nearest existing directory above the branch ref when its loose file is missing, watches `reftable/tables.list` for reftable repositories, and builds absolute paths with `cmake_path`, which also removes a `file(REAL_PATH)` policy warning that the throw-away test project printed whenever a watched file did not exist yet. A `-D` value given without a type leaves the cache entry `UNINITIALIZED` until the `set()` types it, so an explicit pin is now recognised by that even when it equals the commit CMake had recorded as its own default. Correction to the `05705f98` entry below, which says a supplied commit is never replaced: a pin equal to the recorded default was replaced by a later commit. That is fixed for the untyped `-D` form the release script uses; only a typed `-DSEAM_SOURCE_COMMIT:STRING=` equal to the recorded value remains indistinguishable from a default, and the source comment says so. `tests/test_cmake_source_commit.py` grew from 7 to 16 cases (packed branch without a reflog, packed nested branch, linked worktree with a packed branch, detached HEAD and loose branch without a reflog, configure before the first commit, reftable, an explicit pin equal to the recorded default, and a build that follows a commit and then settles instead of reconfiguring again). Six failed against the unchanged block first; four mutations (no directory watch, no `UNINITIALIZED` recognition, no reftable entry, the record kept after a pin) each failed only their own cases and were restored byte for byte; the module passes with the Ninja and the Makefile generators, and the release-gate, tracked-source, external-beta and public-release suites still pass. In the real `build/release` a plain build after the commit restamped the generated header and the app's `Info.plist` from `f6cbde48` to `41a40998` without a manual configure. **Editing target (`41a40998`).** Removing the last vocal track cleared the editor but left the authoring session and the runtime naming the deleted track and region, because the host callback the editor used cannot express nothing. `EditorHostCallbacks::clearVocalTarget` is now called by `reconcileWithProject` when the editor moves to a place without a vocal track; `AuthoringRuntime::clearSelection()` drops the selected track and region, the technical-edit region and any performance audition; the standalone session and the plug-in editor wire it (the plug-in editor also refreshes its voicebank resolution, live resource and audio state), and replacing the score with one that has no vocal region now clears the runtime too, which used to keep naming the previous score's IDs. The editor also rests on an audio track only while the score has no vocal track, so an Undo that brings the vocal track back moves the editor onto it instead of leaving it on the audio track it fell back to; no host lets the editor choose an audio track, so this changes only what a removal leaves behind. The plug-in editor's selection routines moved into `editor_runtime_selection.cpp` because two sources sat exactly on the 600-line cap of the adapter gate. Coverage is a negative-state oracle over the editor, session, runtime, technical edits, piano roll and lane through menu Undo and Redo, the editor keys, an audio track that remains and a replaced score (four lifecycle cases, which failed first), three native-controller cases (the host is told once and not again, a resting audio track gives way, a host that refuses does not stop the editor), one plug-in editor case and one runtime case. Ten mutations each failed only the tests written for them; one escaped at first because the fixture's track had no bank bound, so its assertion was strengthened and re-run. Verification: Release build and ctest 220/220 (the tracked-source gate needs the new file staged and was re-run after `git add`); Debug `seam_tests` 1174/1174, `seam_u2_tests` 82/82, `seam_authoring_performance_tests` 14/14, `seam_design_shell_input_tests` 98/98, `seam_melisma_tests` 9/9 and `seam_performance_edit_preservation_tests` 5/5, run from the repository root (an early Debug run started inside `build/dev` failed one test that loads `assets/` relative to the working directory, and passed from the root). Open findings, not fixed here: split, duplicate-region, duplicate-track, copy-to-track, add-track and add-region still move the editor's selection without telling the host; the plug-in editor's own `authoring_->clearSelection()` call has no direct assertion. Limits: automated tests on one machine; the plug-in editor was exercised through `EditorRuntime`, not in a host; removing the last track was not exercised in the live app; no DAW, VoiceOver, listening or external review evidence; Usable Alpha and External Beta are not promoted.

2026-09-30 — Export receipts stamped with the commit that was built (cross-cutting repair found in live-window exploration and independently noticed by the second developer; it completes and advances no plan unit), verified and pushed in `05705f98`; GitHub CI remains deferred and `.github` was not touched. A by-hand journey in the rebuilt `Project SEAM.app` (EMO look, 1280×800, the eight-unit demo bank) drew a note, committed the lyric こ, rendered it, played it, saved, reopened the saved score and ran Export Set. The export committed `master.wav`, `stems/Voice_1-…wav` and `receipt.json`; re-reading those files for this entry shows a 24-bit stereo 48 kHz WAV of 60000 frames (1.25 s) with RMS 0.00691, peak 0.0848, identical channels and a first audible frame at 51635, and the master and the stem share the sha256 `959404d8…` that both receipt file entries repeat. The same receipt's `applicationBuildSha` was `741ae2f2…`, the commit the build directory was first configured at, while HEAD was `9d3d3c4b`. `CMakeLists.txt` wrote the local default, HEAD at the first configure, into the CMake cache and never looked again, and nothing re-ran configure when HEAD moved, so `build::kSourceCommit` and through it both receipt producers named source the bundle was not built from; the Debug directory carried another old commit. A `SEAM_SOURCE_COMMIT` that a caller supplies is still authoritative (corrected in `f6cbde48`: a pin equal to the commit CMake had recorded as its own default was in fact replaced by a later commit) (release automation passes it with `-D` and requires it to equal HEAD), and the literal `set(SEAM_SOURCE_COMMIT ...)` line that `tools/external_beta/release_gate.py` parses is unchanged. When nobody supplies one CMake now records the commit it chose in `SEAM_SOURCE_COMMIT_FROM_HEAD`, and a cache entry that still equals that record follows HEAD at every configure. Configure also depends on `HEAD`, `logs/HEAD`, `packed-refs` and the current branch ref, so a build after a commit, checkout or reset re-runs CMake by itself. A build directory made before this change keeps its old entry, which looks caller-supplied, until `cmake -U SEAM_SOURCE_COMMIT` is run once; that was done for `build/release` and `build/dev`. `tests/test_cmake_source_commit.py` runs the real identity block, taken from between two comment markers, in a throw-away Git project: a reconfigure, a build and a checkout of an earlier commit follow HEAD, an explicit value survives them and replaces an earlier default, with no Git history the zero base is kept, and the release gate's literal line is still present; the three follow-HEAD cases failed against the unchanged logic first. The export tests now require the receipt's `applicationBuildSha` to equal `build::kSourceCommit` for both the set receipt and the single-file receipt, where before they only required the key. Mutation checks, each reverted to an identical file: never following HEAD, dropping the configure dependency, treating an explicit value as a default and never recording the chosen commit each failed the cases written for them, and stamping either receipt with another string failed only that receipt's test. Verification was a full Release build with ctest 220/220 (one new test), Debug `seam_export_tests` 28/28, and the tracked-source-closure, external-beta and public-release Python suites. In the real `build/release` a plain incremental build after the commit re-ran CMake by itself and restamped the generated header, the app binary and its `Info.plist` with `05705f98`; the cost is that every commit now recompiles the fourteen C++ files that include the generated header and relinks their dependents, 140 build steps in about half a minute on this machine. Also seen in the same live pass, by the implementing agent and before this entry: after Save a Command-Z brought the edited marker back, Command-O with unsaved changes raised the Save, Discard and Cancel alert and Discard led to the Open panel, the reopened score showed one note, READY and its waveform, and the playhead ran to the end of the 1.25 s render. Open findings, not fixed here: a failed render says only that the voicebank cannot cover the phoneme sequence and names no note, lyric or phoneme; the old failure text was still shown about 2.5 s after the lyric was corrected and whether a fresh render ran in that time was not established; and the default lyric あ can never render with the only bundled voice, whose manifest holds eight units (こ, え, お, つ, な, ぐ, ま, で) and no stand-alone /a/. The SCENE look, multi-note phrases and the VOICE, TUNE and MIX workspaces were not exercised in that journey. Limits: one automated pass by the implementing agent on one machine; audible output was not verified, because the output meter read "Not measured" before Play and was not read again; there was no VoiceOver session, no 1× display, no real Japanese input method and no DAW; no fresh live export was made from the restamped bundle; and a release build through `scripts/build_phase13a_formats.py`, which passes its commit explicitly, was not run.

2026-09-30 — Editing target re-anchored after Undo, Redo and region removal (cross-cutting repair found by the second developer's read-only review of `9d3d3c4b`; it completes and advances no plan unit), verified and pushed in `2c97e90e`; GitHub CI remains deferred and `.github` was not touched. The second developer, another Codex thread and not an external reviewer, reproduced three failures with out-of-tree probes linked against the existing Release libraries: `deleteSelectedRegion` left the removed region's note ids selected, a menu Undo of removing the only region restored the note in the score while the piano roll showed none, and a menu Undo of an added region left the editor naming a region that no longer exists. I reproduced all three with their binary and found the same root cause behind four more: Redo had no re-anchoring at all (the menu's Undo repaired a removed vocal track and nothing else), Undo of removing the only track left the editor on no track, the arrangement lane was rebuilt only by the editor's own edits so a menu Undo or Redo of a region edit left a clip drawn that was gone or a restored one missing, and the editor's own Command-Z handler, which is the only Undo a plug-in window has, refreshed only the piano-roll index. `NativeEditorController::reconcileWithProject` is now the one routine. When the selected track or region is gone, or none is selected although the track has regions, the editor moves to the first surviving one or to none, the note selection is cleared, seam and unit targets are dropped, and the host is told through the existing `selectTrack` and `selectRegion` callbacks so the authoring session and runtime follow. It always rebuilds the lane and the piano-roll index, so a note selection the change left intact survives. Menu Undo and Redo, the editor's Command-Z, Command-Shift-Z and Command-Y, `deleteSelectedRegion` and `removeSelectedTrack` all call it, and the last two each kept a partial copy of it. New tests: six lifecycle cases that drive the menu commands and the editor's key handler and compare the editor, authoring session, runtime, piano roll and lane with the score, and four controller-level cases for the routine itself, including that an intact target and its note selection are left alone. Verification was a full Release build with ctest 219/219 (the count is unchanged because the cases live in existing binaries) and Debug `seam_tests` 1165/1165, `seam_u2_tests` 78/78, `seam_melisma_tests` 9/9, `seam_performance_edit_preservation_tests` 5/5 and `seam_design_shell_input_tests` 98/98. Mutation checks, each reverted to a file with the same sha256: dropping the region fallback failed all six lifecycle cases, dropping the selection clear failed the selection case, dropping the host notification failed the two cases where the host's own selection had moved, dropping the lane rebuild failed five, dropping the Redo call failed three, dropping the Undo call failed three, reverting the key path failed the key-path case, and dropping the track fallback failed the only-track case; five further mutations, including treating every call as a move, each failed the controller cases written for them. In the live window (refreshed Release bundle, EMO look, 1280×800, demo bank) Add Region then a real Command-Z took the MIX lane from "2 regions" to "1 region" with Region 1 selected, Command-Shift-Z restored "2 regions" with Region 1 still selected, and deleting the only region in SING (lane "0 regions") followed by Command-Z gave "1 region", Region 1 selected and the note back on the SING piano roll with its lyric; the probes had shown the opposite at `9d3d3c4b`, and I did not repeat these steps in a pre-fix window. Corrections to the entries below: the `d802f339` entry named the note cleanup review as a command that removes selected notes, but it issues resize and performance commands and removes none, so the concrete uncovered path was region removal, which this unit repairs; `RemoveNotesCommand` is issued only by the piano roll's delete, which clears the selection itself; and the same entry called the unsaved marker tests-only, but a later live pass showed it, since after Save a Command-Z brought the edited marker back. Limits: one automated pass by the implementing agent on one machine, with no VoiceOver session, no 1× display and no DAW; the Delete-key route was exercised in SING only, because the same key was a no-op in MIX; removing the only track, Redo of a removal under the editor and the editor's own key handler are verified by tests only; the runtime's preview already heals a stale region when it renders, so the runtime is moved only through the existing host callbacks; split and harmony commands were not examined for stale selection; and the second developer's verdicts on `5f4396a3`, `15a4bbc3`, `572f4f5e`, `d802f339` and `d112400d` were accept or accept with notes on a pinned commit, which is bounded local evidence and not release acceptance.

2026-09-30 — Inline lyric editor sized to what is typed (cross-cutting repair found in live-window exploration; it completes and advances no plan unit), verified and pushed in `d112400d`; GitHub CI remains deferred and `.github` was not touched. Continuing the by-hand journey in the rebuilt `Project SEAM.app`, a double-click on a note opened the lyric editor but nothing typed was visible. A note drawn with a double-click is a sixteenth, about 28 px wide, and the shell painted the editor's box and its text area at the note's own size, which left about 12 px for text: the box showed the first character and clipped the rest. On a note widened by hand the same editor was legible, which isolated the cause. `singLyricEditorBounds` in `sing_shell.cpp` now keeps the note's left edge and row, grows the box to the measured width of the composition text plus padding and room for the caret (never below 96 by 24), slides it to stay inside the grid and never lets it exceed the grid; the painter measures the text in its own style and gives it exactly the box less its padding. New tests: a pure geometry case, and a shell-level case over a sixteenth-note-wide note whose captured drawn text line must have a box at least as wide as the text, which failed against the old painter (a box of about 12 px for about 60 px of text). An earlier draft of that test asserted only that the line was not elided and stayed inside its clip, and passed against the old painter, because the shell paints through its own recording canvas; the box-width assertion is the one that discriminates. Verification was a full Release build with ctest 219/219, Debug `seam_design_shell_input_tests` 98/98, and a mutation check in which removing the right-edge clamp fails the containment assertion. In the live window a default-size note's editor now shows "あkomorebi" whole, where the build before the fix showed a note-sized pill with only the first character; Escape then cancelled the edit and left the lyric unchanged. The same pass entered a covered kana lyric (こ) and the render completed READY with the waveform drawn inside the note and Play enabled. Observed but not changed: after the romaji lyric "ko" was committed the status line still read the earlier "Voicebank cannot cover the phoneme sequence" failure without naming the note or the sound (whether a fresh render ran was not established), and the computer-use tool's `typeText` did not deliver kana to the field while ASCII arrived, so kana were entered by paste. Limits: no real Japanese IME session (marked text is sized by the same measured width but was not exercised live), no VoiceOver session, no 1× display, one machine and one automation pass.

2026-09-30 — Editor Undo and Redo kept in step with the score (cross-cutting repair found in live-window exploration; it completes and advances no plan unit), verified and pushed in `d802f339`; GitHub CI remains deferred and `.github` was not touched. Driving the rebuilt `Project SEAM.app` by hand showed that after Command-Z on a freshly drawn note the accessibility tree already reported the right note count while the piano roll still painted the removed note as a bare capsule, and that the mirror image held for Redo: a returned note was in the score but not on screen. Command-Z is the Edit menu's key equivalent, so it reaches the application as its Undo command and never passes through the editor's own key handler, which did rebuild its index. Three mirrors of the score were left stale, and each now has one owner that keeps it right. `PianoRollModel` records the session revision its spatial index was built at and rebuilds when the session has moved on, which covers undo, redo, any menu command and a replaced project; `EditorSession::undo` and `redo` reduce the selection to notes that still exist, so a selection can no longer name a note nobody can see; and `StandaloneApplicationController::onDocumentChanged`, where every application-level edit already reports, now refreshes the editor's mirror of the document's unsaved state (the title's Edited marker and the mascot's mood), which after Save then Undo used to stay clean while the document differed from the saved file. New tests: a piano-roll case (undo, redo, a command run straight on the session, a replaced project), a command-level case for the selection (undo of an add and redo of a removal), and a lifecycle case that draws a note with a real double-click, saves, and drives the same `dispatch(Undo)` and `dispatch(Redo)` the Edit menu uses while checking paint, hit testing, selected count and the unsaved marker, then adds a harmony track from a saved score. Verification was a full Release build with ctest 219/219, and Debug `seam_tests` 1155/1155, `seam_u2_tests` 72/72, `seam_melisma_tests` 9/9 and `seam_performance_edit_preservation_tests` 5/5. Mutation checks: removing the index synchronisation fails the piano-roll case and the lifecycle case at the paint assertion, removing the unsaved-marker refresh fails the lifecycle case at the marker assertion, and disabling the selection reconciliation fails the command case and the lifecycle case's selected-count assertion. In the live window (EMO look, 1280×800, the demo bank) a double-click drew a note, Command-Z removed it and restored the empty-grid hint with no ghost capsule, and Command-Shift-Z brought it back with its lyric. Limits: that live pass was one automated run by the implementing agent on one machine, with no VoiceOver session, no 1× display and no DAW; the unsaved marker and the selection reconciliation are verified by tests only, because Save opens a system panel the automation cannot drive and the production shell's accessibility tree exposes no selected-note count (the commit message's mention of assistive technology refers to the legacy editor's Distribute-lyrics node, which the shell does not present); the reconciliation is deliberately not applied to `execute`, so commands that remove selected notes, such as the note cleanup review, were not examined; and the ghost's absence was observed for the add-then-undo case only.

2026-09-30 — U22 generation panel wording chosen against the width the canvas really draws, and the first live-window evidence for it, verified and pushed in `572f4f5e`; GitHub CI remains deferred and `.github` was not touched. The previous entry (`15a4bbc3`) chose between full and compact wording by the bitmap face's column width, which is safe with or without a text engine but was only ever exercised in the harness. Opening the rebuilt `SEAM Voicebank Studio.app` on a workspace made by `new-producer-workspace` showed the cost: the AppKit window's system text engine draws a much narrower face, so at 720×520 the panel painted "Plan" and "Open" although "Plan campaign" and "Open / resume" fit, and wrapped a shortcut hint that fits on one line. `RasterCanvas::measureText` now returns the width a text needs on one line, from the installed engine's own measurement when there is one and from the bitmap face's columns otherwise; `studioFitText`, `studioWrapWords` and `studioControlPaintLabel` take the canvas and measure through it. A canvas with no engine behaves exactly as before, and the real window keeps the full wording wherever it truly fits. Wrapping measures each whole candidate line, so the spaces between words count as drawn. `seam_studio_campaign_tests` gained a case that uses the system engine at 1× and 2× across every window width from 720 to 1800 and checks the premise the bitmap choice rested on (the system face is narrower), that every painted label measures inside its button, that no label is shortened when it fits as drawn, and that the engine draws the painted label in the physical box `drawText` hands it without an ellipsis; halving the engine's reported width fails it, naming "Resume campaign" at width 720. Verification was the full Release build and full CTest run, 219 of 219, plus Debug `seam_studio_campaign_tests` 10 of 10, `seam_voicebank_studio_app_tests` 4 of 4 and `seam_export_tests` 28 of 28. Live evidence: the AppKit window was opened with `--production-project`, `--window-width 720 --window-height 520` and `--screenshot` (a 1440×1040 capture at 2×) and at the default 1440×900 (2880×1800), and both were read by eye: full wording on every button, a one-line hint, no truncated label except the deliberately ellipsized 64-character prompt identifier. That corrects the previous entry's limit: the Studio header's shortcut hint and its status line fit in the real window at the minimum size and were cut only in the bitmap-face harness, so they are not a defect there. What the live captures do show is legibility rather than fit: the hint, status, unit-state and queue lines are drawn at 6 to 7 points and are hard to read at 2×, and at 720×520 the intake column is only 194 px wide because the unit rail and inspector are fixed-width; both belong to the layout redesign. Limits: one retina display and one workspace state (an untouched draft with no takes), so the populated panel, VoiceOver and a 1× display were not exercised live, and the captures are read by one person, not an independent reviewer.

2026-09-30 — U22 generation panel and intake hint kept whole at the minimum window, verified and pushed in `15a4bbc3`; GitHub CI remains deferred and `.github` was not touched. At 720×520 each of the generation panel's eight buttons had about 71 pixels, so "Plan campaign" and "Resume campaign" were cut to "PLAN CAMPA" and "RESUME CAM" in the harness's bitmap face, and the intake shortcut hint ran off its column as "R REC / CMD/CTRL-I IMPOR"; DESIGN.md requires text to stay inside its rectangle at that size. `StudioSampleReviewControl` now carries a `compactLabel` beside its `label`, which remains the accessible name so the semantic tree keeps the full action wording at every width. `studioControlPaintLabel` paints the full label when it fits and the compact one when it does not: "Plan campaign" paints as "Plan", "Resume campaign" as "Resume", "Open / resume" as "Open" and "Cancel work" as "Cancel" only where the button is too narrow, and a wider window paints every label whole. The decision uses `RasterCanvas::fallbackTextAdvance`, the column width of the bitmap face the canvas draws when no text engine is installed and the same arithmetic its own clipping uses; that face is the widest the canvas can draw at these sizes, so a label that fits it is not truncated by the system text engine the AppKit window installs either. The intake hint wraps at spaces through the new `studioWrapWords` in up to two lines above the panel. A first attempt used the library's column wrapper, which split inside words ("IMPOR" / "T"); the ink-count test accepted it because no pixels were lost, so a separate case now asserts that wrapping never adds, drops or splits a word. `seam_studio_campaign_tests` gained three cases: a sweep of every window width from 720 to 1800 in an unplanned, a planning and a planned campaign state requiring each painted label to survive the canvas's own truncation, the wrap properties, and a render of the Studio painter at 720×520 and 1100×720 requiring the same hint ink on two lines and on one. Dropping the compact label for "Plan campaign" fails the sweep at width 720 naming the control, and allowing only one wrapped line fails the ink comparison. Verification was the full Release build and the full CTest run, 219 of 219, plus Debug `seam_studio_campaign_tests` 9 of 9, `seam_voicebank_studio_app_tests` 4 of 4 and `seam_export_tests` 28 of 28; the 720×520 and 1100×720 harness captures were read by eye. Limits: the captures use the bitmap face, so no AppKit window, system font, 2× display or VoiceOver session was exercised. The Studio header's shortcut hint ("CMD/CTRL-D DESIGNER  CMD/CTRL-R IMPORT WAV" in a 192 px box) and its top-right status line (a 340 px box that holds 56 columns of messages up to about 70) still exceed their boxes at the minimum window, and the intake column itself is only 194 px wide there because the unit rail and inspector are fixed-width; those belong to the layout redesign and are not closed by this entry.

2026-09-30 — U22 regenerated unit carried through publication, installation and a song, verified and pushed in `5f4396a3`; GitHub CI remains deferred and `.github` was not touched. The regeneration case ended at one reviewer decision on the retake, so nothing showed that a bank holding regenerated material could be published, installed or sung. It now advances to the next unit, accepts every remaining unit through Studio's own capture, reviewer and confirmation dialogs, then publishes, signs and installs the bank and opens a song bound to the installed content hash in a fresh editor session. The installed bank contains the regenerated audio and not the audio the retake replaced, and the song exports non-silent audio for phrases nobody recorded or generated. The publish, sign, install and sing steps the first journey case already performed are now shared helpers (`publishSignInstallAndSing`, `acceptRemainingUnits`) used by both cases, and a unit now counts as reviewed when the producer's review count grows rather than when a status line an earlier unit left behind says so; the first case also checks that every unit's active take audio is present in the draft under its content hash. The harness can write what it renders at 720×520 and 1100×720 when `SEAM_STUDIO_APP_SNAPSHOT_DIR` names a directory, and no check depends on it. Verification was `seam_studio_generation_bank_tests` in Release and in Debug, both cases passing in each; the change is test-only. Limits: the audio, source decisions and reviewer are synthetic or scripted, so no human listening, DAW or VoiceOver evidence exists and this is not singer qualification. The captured 720×520 panel shows generation control labels hard-clipped by the fallback bitmap font (for example "Plan campaign" and "Resume campaign" lose their last characters), which is an open defect that this entry does not close. Campaigns still freeze planned take IDs and the request registry refuses an occupied row, so regeneration remains a per-unit Studio action, and the CLI has no retake preparation.

2026-09-30 — U22 Studio regeneration of a generated unit, verified and pushed in `628a254c`; GitHub CI remains deferred and `.github` was not touched. Scenario 1 asks a creator to generate, edit, regenerate and review through the UI, and only the first of those had a route for generated material. Job preparation named the unit's planned take ID, which the producer refuses once the unit holds a take, so a producer who had collected a generated take could not regenerate it, while recorded material already reached a second take through Studio's retake import. `nextProceduralTakeIdentity` (`libs/seam-voicebank-production/src/repository_import.cpp`) is now the single rule for the identity a new procedural candidate carries: the planned ID for an empty unit, or a derived `-retake-N` identity that supersedes the current take for an occupied one. The interactive raw and candidate imports use it too, so a prepared job and a live import for one row cannot settle on different names. `prepareGenerationJobFromScore` takes an explicit `GenerationTakePolicy`; the default still refuses an occupied unit, so the CLI's `prepare-generation` (whose existing test still asserts that refusal) and the inventory and preflight preparation are unchanged, and only Studio opts in. Its dialogs name the action a retake when the selected row holds a take, and running a job re-derives the identity from the row's current state, so a job whose row has since moved on is refused as stale. Driving the regeneration through Studio exposed a second defect: the single Run job import never set the take's style, and a style-owned workspace, which every new workspace is, refuses a take that carries none, so a job prepared there could not be collected at all. The CLI and batch paths already carried the style. The new `seam_studio_generation_bank_tests` case runs the route through Studio's own controls: it collects a campaign, records a source-quality assessment, edits the voice, prepares and runs a job for an occupied unit, and checks that the superseded take stays in the repository marked as replaced while the new take is unapproved marker-review material under a distinct audio digest. Re-running the job is recognised as already collected, and the earlier assessment stops qualifying the new take until a fresh one is recorded, because the assessment is bound to the exact set of active takes. The draft then holds the regenerated audio and none of the audio it replaced, and one reviewer decision lands on the retake and on nothing it replaced. The case also checks that a plain preparation still refuses the occupied unit and that the dialogs name the retake. Release: the full build and the full CTest run pass (219 of 219); Debug: the new case and the Studio app, campaign, voice generation workflow, voicebank production and export suites pass. A temporary mutation that restored the old identity rule failed the case at job preparation, and before the style fix the case failed at collection with "Generated candidate request is stale or belongs to another target". Limits: campaign definitions still freeze planned take IDs and the request registry still refuses an occupied row, so regeneration is a per-unit Studio action and not a campaign operation; the CLI has no retake preparation; the audio is synthetic and the source decisions and reviewer are scripted; this case stops at the reviewer decision, so carrying a regenerated unit through publication, installation and a song, and the generation panel's narrow-window pass, are not yet shown; U22, the external-evidence gates and Beta GO remain open.

2026-09-30 — U22 Studio campaign preflight, verified and pushed in `a9631f88`; GitHub CI remains deferred and `.github` was not touched. The computer-generation route could not be completed from Voicebank Studio: `generation_campaign_advance.cpp` refuses to advance any campaign whose `preflight/report.json` is not admitted, and nothing in Studio ran a preflight, so the report had to be produced by calling the library directly. `VoicebankStudioController::beginGenerationCampaignPreflight` now renders the campaign's held-out phrases into a new `preflight` directory beside the immutable definition and reports the outcome — passed, or refused with the count of refused phrases — while generating nothing into the producer and approving no unit. An interrupted or failed attempt leaves a directory a campaign can never advance on, so it is preserved beside the definition under a free `preflight-attempt-N` name rather than deleted, and the retry then proceeds; an already admitted report is verified and reused instead of re-rendered. Studio exposes the step as a `Preflight` control on the generation panel, enabled only once a campaign identity is recorded, and the activity publishes the submitted report so a caller can read what was rendered. The new `seam_studio_generation_bank_tests` drives the whole generation route through Studio's window: a vowel-starter workspace, a procedural source and a registered reviewer, a planned campaign, an advance refused for having no preflight with the producer untouched, the preflight itself, the collected generated takes all in marker review with no reviews, a source-quality decision that still approves no unit, a draft carrying the campaign's own generated audio, per-unit capture and acceptance, publication, signing, installation, and a song the editor opens with no `BANK_MISSING`, bound to the installed content hash and exporting non-silent audio. A temporary mutation that removed the preflight gate from the advance path made the case fail on exactly the refusal assertion, and restoring it made it pass again. Release: the new suite, the Studio app, campaign, bank hand-off, sample review and manifest draft suites, the original-singer campaign workflow, inventory preflight and voice generation workflow suites all pass. Limits: the producer, recipe and audio are synthetic fixtures, the microphone and dialogs are scripted, and no physical device, AppKit panel or VoiceOver session was exercised; this shows the generation route reaches a singing song through the product, not that the generated singer is useful or qualified, and U22, the external-evidence gates and Beta GO remain open.

2026-09-29 — U22 Studio cancelled-job and recovery coverage, verified and pushed in `626f413c`; GitHub CI remains deferred and `.github` was not touched. U22 scenario 3 asks that keyboard and accessibility operation, a narrow window, long names, cancelled jobs and recovery preserve focus and data; the Studio app harness had no case for any of it, so a change that dropped the focused control on resize or wrote a take from an interrupted capture would have passed every existing check. The new `seam_voicebank_studio_app_tests` case drives Studio's own surface: it creates a workspace whose project ID is a deliberately long name, registers a licence, a source declaration and a reviewer, focuses the import control, shrinks to the 720×520 minimum and grows back — at the minimum every enabled button stays inside the window and Record stays enabled with the microphone reported idle, and after resizing the same control still holds focus with the selected row unchanged. It then interrupts a capture with Escape and asserts no take, no queue change and a usable Record, then makes publication fail by editing the licence, asserts the recorded WAV is retained with no take and no queue change, restores the licence, retries with R, and confirms the same file is published once and not re-sung. A fresh Studio reopens that folder at the minimum window with the take, the reviewer, the selected source and the durable generation intact. A temporary mutation that cleared the generation view's semantic focus on resize made the case fail on exactly the focus assertion, and removing it made the case pass again. Limits: the microphone and dialogs are scripted, so no physical device, AppKit panel or VoiceOver session was exercised; Scenario 1's generate/regenerate depth and the external-evidence gates remain open.

2026-09-29 — U22 signed sample-bank packaging, installation and song hand-off from Studio, verified and pushed in `1ceee040`; GitHub CI remains deferred and `.github` was not touched. Publishing a reviewed sample candidate produced an engineering directory that Studio could not package, install or open, so a producer who recorded or imported their own material reached a dead end at publication while only the Designer's procedural singer could reach a singing song. Three explicit steps close that route, each refusing unless its predecessor actually happened. `B SIGN BANK…` decodes the published candidate's manifest, re-encodes it, and compares both the manifest digest recorded at publication and a freshly computed content hash, so material that changed on disk is refused instead of signed under the reviewed identity; the private key is read for the call and zeroed, and only the public key is retained (`libs/seam-native-ui/src/sample_bank_package.cpp`). `P INSTALL BANK…` installs the signed package into the installed root of the bank folders the song editor catalogs through the new `StudioPlatform::voicebankRoots` hook, trusting exactly the key that signed it in this session, then re-scans that root with `VoicebankCatalog` and requires a trusted installation whose content hash is the published one. `Y NEW SONG…` writes a song bound to the exact installed bank and hands it to the editor through the new `createInstalledBankSongProject`, only after the bank re-resolves as trusted with the same content; an existing project is never replaced and no song is written inside an installation folder. Signing proves publisher authenticity only, and none of the three steps is implied by publication. `seam_studio_bank_handoff_tests` (new, 4 cases) covers packaging, installation, a tampered manifest, an untrusted key, a wrong expected content hash, the song binding, an occupied destination, a path inside the installation and a vanished installation. `seam_voicebank_studio_app_tests` drives the whole route through Studio's own keys and accessibility actions — a vowel starter workspace, ten recorded rows, source licence and declaration, reviewer registration, source-quality evidence and decision, draft creation, per-unit capture/choose-reviewer/accept, publication, signing, installation and the song hand-off — then opens that song in a fresh standalone session whose only bank root is Studio's, asserts no `BANK_MISSING`, checks the track's content hash, sings three notes and exports non-silent audio. The sample-review and manifest-draft suites check 22 bounded, non-overlapping controls at 720, 960 and 1440 px. Debug and Release: the four Studio suites, `seam_original_singer_workflow_tests`, `seam_production_draft_tests`, `seam_manifest_draft_tests`, `seam_studio_producer_workspace_tests`, `seam_studio_recording_import_parity_tests` and the macOS and Windows source contracts pass. Limits: dialogs are scripted and the microphone is scripted, so no AppKit panel, physical microphone, VoiceOver session or human listening was exercised; the bank is installed into a test folder rather than the real user library; and this is one recorded route, not U22 Scenario 3 (cancelled jobs and recovery preserving focus and data) or the generate/regenerate depth of Scenario 1.

2026-09-29 — U22 vowel starter inventory for new producer workspaces, verified and pushed in `2b537264`; GitHub CI remains deferred and `.github` was not touched. Studio-created workspaces always used the full Japanese draft (1026 rows at three pitch layers), and candidate publication must cover every assignment, so recorded material could not reach a singing test bank without 1026 approved takes. `draftInventoryPresetProfile` names the two inventories Studio offers. `JapaneseFull` is the default profile, unchanged. `JapaneseVowelStarter` records sustained a/i/u/e/o at MIDI 60 and 66: 10 rows with two alternate prompts each. It is an ordinary draft profile that the Python generator, the inventory loader and the external workspace validator admit unchanged, and it can only sing vowel lyrics. The macOS create-workspace alert gains an inventory menu that travels as `NewProducerWorkspaceInput::inventory` to `beginCreateProductionProject`, and the creation status reports the row count. `seam_production_draft_tests` (20/20) checks that the full preset reproduces the default golden digest and the starter reproduces the inventory and script SHA-256 printed by the Python generator, including coverage, prompts, assignments and reload. `seam_studio_producer_workspace_tests` creates a starter with 10 rows and reopens it from its folder. `tests/production/test_draft_producer_workspace.py` (4/4) checks that a CLI-created starter matches `generate_draft_inventory`, the operator script and `prepare_production_draft_definition`, and passes the external validator. The Studio app and macOS/Windows source-contract suites pass in Debug and Release, and the Release full build and `seam_tests` pass. Limits: the AppKit alert was compiled, not opened by hand; Win32 has no create-workspace dialog (Windows TODO). Studio still cannot package, install or hand off a published sample bank, so the recorded route does not yet reach singing through the UI.

2026-09-29 — U22 Studio reviewer registration, verified and pushed in `487cef9e`; GitHub CI remains deferred and `.github` was not touched. A Studio-created producer workspace had only its PRODUCER, and nothing could add a REVIEWER afterwards, even though unit review, source-quality decisions and candidate publication all require an independent registered reviewer. As a result, recorded or imported material in such a workspace could never be approved without editing project JSON, which fails the U22 check that both source routes work without manual JSON edits. `ProductionProjectRepository::registerReviewer` now appends one REVIEWER that the registered producer declares against the exact current snapshot, under a new `reviewer-register` journal action admitted by the C++ journal check and the Python draft validator. It refuses non-producers, IDs already registered in any role (so a producer ID cannot become a reviewer ID), malformed or space-padded IDs, non-UTC times, stale snapshots and cancellation, all without mutation, and it uses registerSource's recovery path. Nothing else changes: takes, sources, reviews and queues stay exactly as they were. Studio's sample review gains V REGISTER REVIEWER… on the source row, now split into thirds, plus a detail line listing the registered reviewers. The macOS dialog shows the producer, the project digest and the existing reviewers before asking for the new ID; Win32 keeps the default Unsupported entry while Windows stays TODO. Accessible activations now clear the previous error line, as key presses already did. `seam_production_draft_tests` covers success (the new reviewer then records a source-quality decision) and every refusal. The UI-driven Studio journey refuses the producer's own ID, registers `listener` and finds it after reopening. The sample-review and manifest-draft suites check 19 bounded, non-overlapping controls at 720, 960 and 1440 px. In Debug, the production review, ownership, staging, import-outcome, style-migration, voicebank-production and original-singer suites and the U3, standalone-production and macOS/Windows contracts pass. The Python production (118) and external-beta (190) suites pass with `/usr/local/bin/python3`; the ctest Python suites fail under Homebrew Python 3.14 in full-product report modules with or without this change. In Release, the full build, `seam_tests` and those focused suites pass. Harness snapshots at 720×520 show the new row and the retry/discard pair without overlap. Limits: the AppKit dialog was compiled, not opened by hand. A Studio-created workspace still has the fixed 1026-row inventory, and candidate publication must cover every assignment. Studio cannot yet package, install or hand off a published sample bank, so the recorded route does not yet reach singing through the UI.

2026-09-29 — U22 Studio recording controls and refused/lost-input recovery, verified and pushed in `5d3a9ad2`; GitHub CI remains deferred and `.github` was not touched. The producer view now exposes Record beside the unit rail as a pointer and accessibility action, not only as the R key. While a take is capturing or waiting to publish, the button reads R STOP + PUBLISH, PUBLISHING or R RETRY, and X DISCARD appears once publication has failed. The capture keeps every other accessibility target locked; only stop, retry and discard stay reachable. Record refuses with InvalidState before opening the microphone when no producer row or bank unit is selected. A refused microphone, a device lost mid-take and a failed stop now leave the same `NO TAKE STARTED / …` or `NO TAKE RECORDED / …` message in the error line and in the persistent recording status, so the reason survives the next key press and Record stays enabled; a microphone status node reports whether input is being captured. The app harness gained a scripted microphone plus scripted workspace-creation, source-registration and workspace-opening dialogs. A new `seam_voicebank_studio_app_tests` case, driven only through accessibility actions, keys and one pointer click, does the following. It creates a producer folder from the Designer home and registers a rights-pass human source in sample review. It is refused by the microphone (with the CoreAudio denial text) and recovers. It loses the device mid-take with nothing published. Its first publication is refused because the license evidence changed, and R then retries without writing a second WAV. It discards a second refused capture with the saved WAV kept. It imports an external 24-bit WAV on the next row into the same marker-review queue. Finally it reopens the folder in a fresh Studio with both takes durable and the microphone untouched. Temporary mutations each failed the case: restoring the refuse-everything capture gate, dropping the status prefix, and removing the no-target guard. The Studio app, recording/import parity, producer workspace, campaign and Voice Designer suites and the macOS and Windows source contracts pass in Debug. In Release, the full build, those suites and `seam_tests` (72 s) pass. Release screenshots of the producer view at 1100×720, 800×600 and 720×520 show the button between the UNITS label and the first rail row without overlap. Limits: the microphone is scripted, and no physical microphone, AppKit panel, VoiceOver session or human listening was exercised. The recorded take has not yet been carried through marker/pitch review, approval, bank publication and singing inside Studio.

2026-09-29 — U22 Studio application harness and UI-driven Designer-to-song journey, verified and pushed in `2308b82f`; GitHub CI remains deferred and `.github` was not touched. The Studio application object moved from an anonymous namespace in `main.cpp` into `studio_app.cpp` (library `seam_voicebank_studio_app`, interface `IVoicebankStudioApp`), and every operating-system effect goes through an injectable `StudioPlatform` whose defaults are the native implementations: dialogs, audition output, microphone input factories, installed-singer roots and the song-editor hand-off. Studio now also protects its installed-singer roots from Designer saves. `seam_voicebank_studio_app_tests` drives the real app through painted frames, accessibility actions, field values and key events: it creates a draft from the Designer home, sets a control value, is refused a Save As inside the singer folder and then saves, cancels and then completes Publish (Cmd-Opt-P) and Install (Cmd-Opt-I), checks that every enabled button stays on screen at the 720×520 minimum window with the long status complete, cancels the song location, hands off through the accessible action with focus preserved and through Cmd-Opt-O, and is refused an existing song file. A fresh standalone editor then opens the handed-off song with only the installed singer available, sings three notes and exports a non-silent master; a temporary mutation that skipped the launch inside Studio made the test fail. The Studio suites and the macOS (21) and Windows (9) source contracts pass in Debug; the full Release build, Release `seam_tests`, the song-journey suite and the Studio suites pass; the built app still launches and paints. This is UI-code evidence with scripted dialogs; real AppKit panels, VoiceOver, a physical microphone and the producer-workspace import route through the harness remain open, and U22 and Beta GO remain open.

2026-09-29 — U22 Studio return-to-song hand-off after install, verified and pushed in `76c41cd6`; GitHub CI remains deferred and `.github` was not touched. After Voice Designer installs a published singer, the Install slot becomes Song editor (Cmd/Ctrl-Option-O; Install gains Cmd/Ctrl-Option-I). Studio asks where to save a new song project, and `createInstalledSingerSongProject` re-scans the installed-singer roots, requires the singer this session installed to still match by render identity, folder and content hash and to resolve as trusted and renderable for the editor's engine, then publishes a new 120 BPM 48 kHz stereo project carrying exactly the reference the editor's New Project screen records. `openDocumentWithApplication` then opens it in the Project SEAM app found beside Studio, in an Applications folder or through LaunchServices, accepting only bundle id `com.project-seam.standalone`. Relative, hidden, non-`.seam`, missing-folder and installed-folder destinations are refused, an existing file is never replaced, and a written file that does not decode back bound to the singer is removed. The new journey case in `seam_original_singer_song_journey_tests` installs through the Studio session path, checks that twelve refusals leave no file, deletes the draft, package and staging copies, opens the handed-off project in a fresh editor with no BANK_MISSING, confirms the reference equals the New Project choice, and renders and exports a song. The suite passes in Debug (103 s) and Release (19 s), as do `seam_voice_designer_tests`, `seam_studio_producer_workspace_tests`, the macOS and Windows source contracts and the full Release build. A throwaway probe outside the repository located the built editor, rejected TextEdit by bundle id, opened a scratch file in TextEdit in 0.14 s and failed cleanly for a missing app. The Studio save panel and a real Studio-to-editor launch were not exercised by hand; Windows remains TODO. Genuine microphone/import parity, keyboard/accessibility journey evidence and the complete UI-only create-to-sing journey remain open; U22 and Beta GO remain open.

2026-09-29 — U22 Studio create/reopen of producer folders, verified and pushed in `7e48b794`; GitHub CI remains deferred and `.github` was not touched. The Designer home screen (Studio's launch screen) gains a fourth entry, New producer workspace (Cmd/Ctrl-Shift-D): an AppKit save panel names a new folder, an alert takes the voice project ID and producer ID, and `VoicebankStudioController::beginCreateProductionProject` runs `createDraftProducerWorkspace` (from `8e5ef6ef`) on a worker thread and adopts the new workspace through the existing open path. Status reports the missing units and that the requested range is NOT_ASSESSED; a folder published before a late cancellation is still opened, and cancellation before publication leaves nothing. Open producer workspace now recognizes a producer folder, verifies its `inventory.json` by regeneration (`beginOpenProducerFolder`) and asks only for the producer ID, so no one types a 64-character digest. The new `seam_studio_producer_workspace_tests` (create, adopt, reopen by folder, unregistered operator, edited inventory, occupied destination, relative path, cancellation invariant), `seam_studio_campaign_tests`, `seam_studio_manifest_draft_tests` and `seam_macos_source_contract` pass in Debug and Release; `seam_u3_voicebank_workflow_contract` and `seam_windows_source_contract` pass in Debug. The native Studio home screen was captured at 1100×720 and 800×600 with the four entries separate. The AppKit save panel and ID alert were not exercised by hand. A strictly UI-only journey still lacks a post-install handoff to the song editor and a harness that drives Studio's real actions; U22 and Beta GO remain open.

2026-09-29 — U22 producer workspace creation without hand-written JSON, verified and pushed in `8e5ef6ef`; GitHub CI remains deferred and `.github` was not touched. Creating a producer workspace previously required the Python generator plus a captured definition for `init-production`, so a UI-only journey could not start. `draft_inventory.hpp/.cpp` now generates the style-owned (schema 2) Japanese draft inventory exactly as `tools/voicebank_script_generator/draft_inventory.py` does, admits a loaded inventory only when its own profile regenerates it byte for byte, and builds the matching empty schema-4 Draft producer (every assignment Missing, one PRODUCER, no source, take or review). `createDraftProducerWorkspace()` publishes `inventory.json`, `recording-script.csv` and an initialized `producer/` workspace through one exclusive no-overwrite directory publication; refusals and cancellation leave nothing behind. `seam_voicebank_cli new-producer-workspace` exposes it, optionally from a captured profile and its SHA-256. `seam_production_draft_tests` passes 17/17 in Debug and Release, including golden inventory/script digests printed by the Python generator and strict-loading refusals (moved paths, rehashed edits, `60.0` versus `60`, unknown or missing fields). The new `tests/production/test_draft_producer_workspace.py` (3 cases) compares CLI-created workspaces with `generate_draft_inventory`, `render_draft_operator_csv` and `prepare_production_draft_definition` and runs the external workspace validator; `seam_public_release_python_tests` passes in Release. Studio does not call this yet, only Japanese draft profiles exist (matching the Python generator), and the requested range stays NOT_ASSESSED. U22 and Beta GO remain open.

2026-09-29 — U22 Studio request-detail output evidence wiring, verified and pushed in `a0b8d928`; GitHub CI remains deferred and `.github` was not touched. The native request-detail panel now exposes an `Inspect outputs` action that runs the read-only controller inspection from `b65673a8` for the visible job page, plus `Cancel inspection` and Escape to stop it. Each job card gains a separate output-evidence line (not prepared, incomplete, prepared without output, recovery required, output verified) so verified output is never shown as collected or reviewed; accessibility nodes carry the same state and inspector diagnostics. The request metadata block now starts below both action rows, and visible-row math was recomputed for the taller cards. Debug and Release `seam_studio_campaign_tests` pass 1/1 each and the native Studio app builds in both configurations; `git diff --check` passes. The native dialog was not exercised manually. Genuine microphone/import parity and the full UI create-to-install-to-sing journey remain open; U22 and Beta GO remain open.

2026-09-29 — U22 Studio controller read-only generation-output evidence, verified and pushed in `b65673a8`; GitHub CI remains deferred and `.github` was not touched. The controller can asynchronously inspect a bounded page of jobs from an immutable queued campaign, bind each job manifest/expectation to its admitted campaign row, and classify output via the existing strictly read-only inspector. For later batches it verifies preceding collection receipts against historical producer generations and journal events before reconstructing the expected job; it does not trust `batch.json` paths, write/recover files, mutate the producer, or equate verified output with collection/review. Regressions cover a one-job `NotPrepared` page with no batch directory or producer-generation change, invalid/oversized page bounds, and `OutputVerified` for completed jobs in a two-batch request while takes remain `MarkerReview`. Debug and Release `seam_studio_campaign_tests` pass 1/1 each; `git diff --check` passes. The native request-detail panel, controls and accessibility nodes are not yet wired to this API, and there is no native-dialog/manual test. Genuine microphone/import parity and the full create-to-install-to-sing journey remain open; U22 and Beta GO remain open.

2026-09-29 — U22 live per-job generation status in Studio, verified and pushed in `51fc3d11`; GitHub CI remains deferred and `.github` was not touched. Campaign rendering now reports an initial zero-of-total snapshot and each verified job output; advancement also reports the actual batch index so resuming past an earlier receipt cannot label the wrong job as active. Studio's coherent transient progress snapshot drives request-detail rows and accessibility nodes for waiting, preparing, processing, output ready, collected/review separate, interrupted/retry, and not collected. Live state is shown only when the active campaign hash matches the immutable request ID; terminal collection is derived from verified completed-batch evidence. No request record was made mutable and this does not persist per-job progress or prove review. Full Release build and CTest pass (214/214); after adding the final completion-state regression, the focused Studio, generation-workflow and native suites pass (3/3); `git diff --check` passes. The native dialog was not manually exercised. Durable per-job output inspection after restart, genuine microphone/import parity, and the full UI create-to-install-to-sing journey remain open; U22 and Beta GO remain open.

2026-09-29 — U22 moved generation-definition recovery, verified and pushed in `8a1be6b6`; GitHub CI remains deferred and `.github` was not touched. A pending request's detail view now offers “Locate definition…” when its stored campaign path is unavailable. The selected path remains untrusted: the existing async campaign admission verifies its bytes against the durable request ID before any producer mutation. Regressions prove tampered bytes and a missing stored locator leave producer generation/takes unchanged, while relocating the whole campaign directory (including sibling preflight artifacts) resumes the same request without duplicate takes; terminal requests expose neither resume action. The focused Release campaign test passes, the full Release build succeeds, and full CTest passes 214/214. The native dialog was not manually exercised, and this does not add per-job live progress/outcomes, genuine-recording parity, or the full UI create-to-install-to-sing journey. It closes locator recovery only; U22, R3–R5, R11, R15, R20, V16, human review and Beta GO remain open.

2026-09-29 — U22 request job/coverage detail view, verified and pushed in `d1904281`; GitHub CI remains deferred and `.github` was not touched. Voicebank Studio's verified request queue now drills into a full-width, paged detail surface exposing the immutable request identity, expected producer generation/hash, recipe identity, submitter/time, worker-definition locator, admitted budgets, terminal outcome, and each planned job's ID, take, coverage key, style, pitch layer, frame count, and batch. The same details are exposed as status nodes for accessibility; pending resume remains a separate action behind the existing request-ID/definition-byte check, while terminal requests remain non-resumable. At 720×480, 1040×720 and 1600×900, controls stay inside their layout bounds; the raster test confirms a job row is painted. Full Release build succeeds and CTest passes 214/214. These are immutable planned-request fields, not per-job progress, completion, pronunciation, review or singer-quality evidence. This closes the missing job-detail surface only; U22, R3–R5, R11, R15, R20, V16, human review and Beta GO remain open.

2026-09-29 — U22 durable generation-request queue increment, verified and pushed in `822df099`; GitHub CI remains deferred and `.github` was not touched. Voicebank Studio now refreshes the producer's validated generation-request registry asynchronously and displays a paged request-level queue with submitted job/coverage counts and terminal outcomes. A pending row invokes the existing campaign admission/advance path using the request ID as the expected campaign-definition SHA-256; missing or changed definition bytes fail before producer mutation. Moved-definition recovery was added in the later `8a1be6b6` increment above. Release `seam_studio_campaign_tests`, `seam_voice_generation_workflow_tests` and `seam_tests` pass; `seam_studio_campaign_tests` also verifies pending/terminal states, producer-byte preservation on hash mismatch, resume without duplicate takes or approval, and row bounds at 720×480, 1040×720 and 1600×900. This closes the missing request-list surface only: per-job live progress/outcome, genuine-recording parity, and the UI-only create-to-install-to-sing journey remain open. U22, R3–R5, V16, human review and Beta GO are not accepted by this increment.

2026-09-29 — U20 Japanese starter inventory render coverage, verified and pushed in `ee61b248`; GitHub CI remains deferred and `.github` was not touched. The full built-in Japanese starter inventory now reaches actual `ArticulatedStream::renderOwned` PCM rendering. The deterministic regression checks bounded exact extent, finite/clamped non-silent samples, every planned gesture span inside the rendered context, and byte-exact equality between whole-phrase output and two consecutive chunk renders. Release `seam_voice_design_tests` passes 1/1. This proves the synthetic renderer path only; it does not establish Japanese intelligibility, pronunciation quality or listener acceptance, and does not close U20 or Beta GO.

2026-09-28 — U17 producer-interpretable selection-score rationale, verified and pushed in `6d3fb89d`; GitHub CI remains deferred and `.github` was not touched. Selected-unit rationale now carries local pitch penalty, multi-phone bonus, priority bonus, and take penalty separately, verifies their sum against the local candidate score, and explains that lower heuristic score points are preferred. The shared producer detail explains each local formula, the level/four-lag correlation/eight-band spectral-envelope join terms, and route-score accumulation; the rationale remains labeled as a source-boundary proxy rather than perceptual evidence. Full Release build succeeds; focused contextual-selection, style-blending, sample-microscope, and CLAP-microscope CTest targets pass 4/4; `git diff --check` passes. This makes the algorithm inspectable, not quality-calibrated: producer review, fixed-corpus results, listener acceptance, U17 acceptance, and Beta GO remain open.

2026-09-28 — U17 producer-facing join-cost rationale breakdown, verified and pushed in `e746572a`; GitHub CI remains deferred and `.github` was not touched. Contextual unit-selection rationale now retains the independently inspectable level, short-lag correlation, and spectral-envelope costs that sum to the incoming join cost. The shared selection description exposes each component, and sample-microscope detail coverage confirms those values survive into producer inspection text. Full Release build succeeds; focused contextual-selection, style-blending, sample-microscope, and CLAP-microscope CTest targets pass 4/4; `git diff --check` passes. This improves engineering explainability only; it does not establish producer review, fixed-corpus selection quality, listener acceptance, U17 acceptance, or Beta GO.

2026-09-28 — U17 source-boundary spectral-envelope scoring, engineering increment, verified and pushed in `3637c4f9`; GitHub CI remains deferred and `.github` was not touched. Contextual unit analysis now adds a Hann-windowed, gain-normalized eight-band spectrum for the first/last bounded 20 ms of each playable source crop, alongside the existing level and short-lag correlation features. The deterministic selector adds a capped mean-band mismatch cost to contiguous acoustic joins and exposes that component in the producer rationale; spectral FFT work is budgeted and cancellable. Selection and render-identity revisions advanced to invalidate decisions/PCM made by the prior algorithm. Equal-RMS synthetic 400/800 Hz harmonic candidates prove the timbrally closer predecessor is selected deterministically across inventory order; gain-invariance and spectral-work-budget refusal are covered. Release `seam_contextual_unit_selection_tests` and `seam_style_blending_tests` pass 2/2; synthesis-quality, compiler, snapshot and voicebank-production suites pass 4/4; full Release CTest passes 214/214. This remains a source-domain numerical proxy, not post-DSP seam qualification or perceptual evidence. Producer rationale review, fixed-corpus results, listener acceptance of the blend/range behavior, U17 acceptance and Beta GO remain open.

2026-09-28 — U16 retimed voiced-island phase-resolution increment, verified and pushed in `1c6d57c6`; GitHub CI remains deferred and `.github` was not touched. Spectral Classic now halves its configured analysis hop for a source map whose sample-rate-normalized source/target spans are retimed and whose measured voicing explicitly leaves and re-enters voiced audio; formant planning and reconstruction use the same hop. The regression compresses a one-second, 48 kHz voiced/noise/voiced sample to 40,000 output frames, applies a MIDI 69→76 score change at the mapped voiced re-entry, checks the compiled score pitch, pitch-tracker results, a direct spectral peak within 35 cents, and >0.99 source correlation through the mapped fricative. All three renderer paths pass. Release and Debug `seam_synthesis_quality_tests` pass 1/1 each; Release `seam_performance_compiler_tests`, `seam_performance_snapshot_tests`, and `seam_formant_expression_tests` pass 3/3; `git diff --check` passes. This is synthetic renderer-path evidence only, not fixed-corpus qualification, listening/intelligibility acceptance, or U16/Beta GO closure.

2026-09-28 — U16 compressed-and-expanded timing regression, verified and pushed in `30f24951`; GitHub CI remains deferred and `.github` was not touched. The measured voiced/noise/voiced source now exercises the compiled score and all three classical renderer paths at both 40,000 output frames (compressed) and 60,000 (expanded). Each case asserts exact output extent, compiled MIDI 69→76 boundary alignment, both voiced-island pitch targets within 35 cents (including a direct spectral peak check for Spectral Classic), and >0.99 source correlation through the mapped unvoiced interval. Release and Debug `seam_synthesis_quality_tests` pass 1/1 each; Release compiler, snapshot and formant suites pass 3/3; `git diff --check` passes. This adds synthetic numerical duration coverage only; the fixed multilingual corpus, listening/intelligibility acceptance, exposed-mode qualification, and U16/Beta GO remain open.

2026-09-28 — Spectral renderer cache-identity correction, verified and pushed in `06f63066`; GitHub CI remains deferred and `.github` was not touched. The retimed voiced-island phase-refinement change from `1c6d57c6` alters Spectral Classic output, but the generated renderer revision still read 8. Revision 9 is now included in canonical snapshot identity, invalidating prior cached PCM without changing the cache format; a stabilization regression pins the generated revision. Release quality, snapshot and full native suites pass 3/3; Debug full native `seam_tests` passes 1/1 (210.99 seconds); `git diff --check` passes. No quality gate changed state.

2026-09-28 — U16 measured voiced-island/source-map regression, locally verified and pushed; GitHub CI remains deferred and `.github` was not touched. A deterministic one-second voiced/noise/voiced take exercises the normal compiled-performance and measured-source-map path in Classic PSOLA, Spectral Classic, and Stretch. The compiled score jumps from MIDI 69 to MIDI 76 at the unvoiced-to-voiced boundary; each renderer must keep both voiced islands within 35 cents of their respective score pitches while the middle unvoiced interval retains source correlation above 0.99. Release and Debug `seam_synthesis_quality_tests` pass 1/1 each; `git diff --check` passes. This is synthetic renderer-path evidence, not fixed-corpus qualification, listening/intelligibility acceptance, or U16/Beta GO closure.

2026-09-28 — U16 renderer-notice-to-editor-status integration, locally verified and pushed; GitHub CI remains deferred and `.github` was not touched. The first actionable voiced-edge pitch limitation now flows from phrase placements through the PCM cache, project-render result, coordinator, and existing editor render-status field. A Ready preview can report the limitation with track/region/phrase identity without misclassifying it as omitted content; Final completeness validation still succeeds for the warning. A coordinator regression covers cold render, a warm cache hit retaining the notice, Final validation, and Ready-status propagation. Release `seam_tests` and `seam_authoring_render_coordinator_tests` pass 2/2; `git diff --check` passes. This is engineering observability only, not proof of pitch quality, listening acceptance, or U16/Beta GO closure.

2026-09-28 — U16 source-voicing render diagnostics, locally verified and pushed; GitHub CI remains deferred and `.github` was not touched. Source-aligned classical render placements now disclose whether measured voicing was applied, the analysis sidecar was unavailable, or its coverage did not span the aligned unit. The snapshot-to-phrase regression asserts both the measured and unavailable states, alongside its existing PCM-difference check. Release build of `seam_performance_snapshot_tests` and CTest pass 1/1; `git diff --check` passes. This is data-provenance visibility, not a renderer-quality or listening result. U16 and Beta GO remain open.

2026-09-28 — U16 classical voiced-edge pitch-mark diagnostics, locally verified and pushed; GitHub CI remains deferred and `.github` was not touched. Successful Spectral Classic and Stretch dispatch now identifies usable sustain marks or names missing, invalid, or insufficient stored pitch marks; diagnostics are retained through short-transition and source-aligned phrase rendering. Regressions cover all four mark states for both renderers and assert that the source-aligned pipeline preserves the status. Release `seam_synthesis_quality_tests` and `seam_performance_snapshot_tests` pass 2/2; `git diff --check` passes. This makes a pitch limitation visible but does not repair it: without usable marks, rendering can still succeed while attack/release pitch remains unverified. U16 and Beta GO remain open.

2026-09-28 — U16 source-aligned measured-voicing propagation, local only; GitHub CI remains deferred and `.github` was not touched. The full source-alignment branch now combines authored phone landmarks with the frozen acoustic analysis for the same verified audio before dispatching PSOLA, Spectral Classic or Stretch. If the measurement does not cover the unit, voicing remains unknown rather than inferred. A snapshot-to-phrase-render regression writes one decoded synthetic sample, its generated pitch marks, a hash-bound alignment and an acoustic sidecar; it confirms the stored record contains an unvoiced noise span and that the same aligned Spectral Classic render changes when only the sidecar is removed. Release `seam_synthesis_quality_tests`, `seam_performance_compiler_tests`, `seam_performance_snapshot_tests`, and `seam_formant_expression_tests` pass 4/4; `git diff --check` passes. This demonstrates dataflow into PCM, not that the selected processing sounds intelligible or is musically preferred. Analysis sidecars remain optional, and unknown-voicing fallback behavior still needs a fail-closed/product-contract decision. U16 and Beta GO remain open.

2026-09-28 — U16 voiced-edge pitch-retargeting engineering increment, local only; GitHub CI remains deferred and `.github` was not touched. PSOLA's existing attack/release grain path now shares its stored-mark/voicing logic with Spectral Classic and Stretch. Those renderers retarget measured voiced edges to the current pitch target while retaining source samples for positions with no nearby stored mark or a source-map unvoiced classification; scratch buffers are limited to and reused across the edge regions. The deterministic generated-mark CV fixture checks all three renderers at MIDI 57/64/76, verifies attack/release pitch within 25 cents, preserves a +700-cent release step, correlates both the unvoiced onset and breath/noise release tail to source above 0.99 (the tail check starts 1,024 frames beyond the final-mark uncertainty guard), and confirms measured vowel-onset timing within one producer-analysis hop. Release `seam_synthesis_quality_tests`, `seam_performance_compiler_tests`, and `seam_formant_expression_tests` pass 3/3; `git diff --check` passes. A later dispatcher increment surfaces missing/invalid/insufficient marks, but the edges still use source-based samples where retargeting cannot be qualified, so full transposition intent is not guaranteed. The fixture is synthetic CV, not fixed-corpus singing, VC coverage, breath/noise intelligibility, short/long-vowel qualification, subjective listening, or external review. U16 and Beta GO remain open.

2026-09-28 — U27 English resource, shared-note syllable distribution and authored reading-hint increment, local only; GitHub CI remains deferred and `.github` was not touched. The pinned `seam-en-us` resource now binds its dictionary, exception table, resolver sources, frozen 93-symbol ARPAbet vocabulary and rule revisions into the pronunciation identity. Lexical syllables distribute across adjacent notes sharing a lyric; explicit per-note hints replace only that note's share, while a `LyricToken::readingHint` supplies a whole-lyric reading before dictionary/fallback paths. Invalid, non-ASCII or pause-bearing authored readings yield an explicit diagnostic and pause rather than silently falling back to the written surface. Coverage for the `sing` fixture reports only missing `/ng/`; the bank's `/N/` and `/n/` are not accepted as substitutes. The primary-checkout Release `seam_language_phonemizer_tests` passes 1/1, including English/Korean phonemizer regressions, and `git diff --check` passes. This is deterministic resolver/resource coverage, not native-speaker review, singing pronunciation qualification, complete dialect coverage or U27/Beta GO acceptance.

2026-09-28 — U19 sustained-pose audition reproducibility and click-screening increment, local only; GitHub CI remains deferred and `.github` was not touched. The native Designer and fingerprint tooling now share the same 48 kHz mono, one-second sustained-pose renderer and finalizer. A versioned engineering manifest binds recipe bytes/hash, seed, source-filter revision, audition revision, measurement toolchain and each oral vowel/syllabic-N case. Its verifier requires exact PCM SHA-256 reproduction, the same measured toolchain, and complete vowel/N coverage for its declared keys; its decoder rejects changed review/status statements and any claim that cross-platform tolerance was measured. Twelve log-spaced spectral shares, peak, RMS, F0 and periodicity remain diagnostic features with explicit engineering margins, not perceptual thresholds. The second-difference discontinuity screen detects a click in a deterministic fixture; it does not claim a perceptual click threshold. Release build and the focused `seam_audition_fingerprint_tests` plus `seam_voice_designer_tests` pass 2/2; the latter asserts exact equality between Designer PCM and the shared fingerprint renderer; `git diff --check` passes. A checked-in baseline manifest/listening WAV packet, cross-platform measurements, qualified phonation/timbre, independent listening, intended singer identity, U19 acceptance and U20 acceptance remain open. No listening result or V-gate changed state.

2026-09-25 — U22 New Project singer-catalogue visibility and diagnostics, local only; GitHub CI remains deferred and `.github` was not touched. The picker keeps trusted/renderable styles selectable and shows discovered but ineligible singers as disabled entries with their resolver reason. The catalogue now also reports invalid/unsafe roots and package-shaped directories it rejects (missing/malformed manifest, unreadable/missing recipe, invalid recipe identity, or enumeration error) instead of silently making them indistinguishable from an empty catalogue. Its candidate-only `scan()` contract remains intact; `scanDetailed()` performs one pass, caps retained issue rows at 64, and stops after 8,192 package folders with an explicit incomplete-scan marker. All diagnostic rows remain disabled, so this does not weaken trust or stale-choice revalidation. Regressions cover ineligible and malformed entries, invalid roots, preserved valid candidates, issue truncation and the scan ceiling. Debug/Release `seam_u2_tests` pass 71/71 each; the full procedural install journey target passes 17/17 each; macOS source contracts pass 20/20; `seam_editor_native` builds in both configurations; `git diff --check` passes. The earlier cold-launch “No Procedural Singer” trigger remains unproven and was not live-rechecked after this change. This repairs observability and bounded scanning, not U22 closure, singer-quality evidence, or Beta GO.

2026-09-25 — U4.2 English dictionary syllable boundaries generalized, local only; GitHub CI remains deferred by user direction. CMUdict-backed readings no longer depend on a six-word boundary table: boundaries are derived between adjacent vowel nuclei by assigning the longest suffix matching SEAM's current legal-onset inventory to the following syllable. Added `banana` and `computer` role regressions alongside the existing `beautiful`, `hello`, `music`, `project`, `singer`, and `extra` cases. English pronunciation identity advanced from resolver version 4 to 5 so edited role/timing ownership invalidates cached pronunciation state. Release and Debug `seam_language_phonemizer_tests` pass 1/1 each. This is a bounded syllable-structure improvement, not a full English syllabifier, dialect/morphology model, native-speaker review, learned singing quality result, or U4.2/Beta GO closure.

2026-09-25 — U4.2 CMUdict lookup corrected for out-of-order entries, local only; GitHub CI remains deferred by user direction. The pinned 135,166-line resource is not strictly sorted: source inspection found the `sepulveda`→`sepultura` and `stilton`→`stilted` inversions, and the previous raw-file binary search reproducibly missed `sepultura`. Lookup now partitions line views into case-insensitive sorted runs in one pass, then binary-searches each run; comparing matches by source position preserves the first-listed pronunciation for alternate entries without modifying the vendored bytes or sorting the full dictionary at startup. Exact-phone regressions cover both missed entries and the `a`/`a(2)` first-variant contract. English pronunciation identity advanced to v6 because previously missed dictionary words now resolve differently. Release and Debug `seam_language_phonemizer_tests` pass 1/1 each (0.39/0.57 seconds); `git diff --check` passes. This repairs lookup for unordered source runs, not dictionary correctness, dialect coverage, syllable qualification, native-speaker review, or U27/U4.2/Beta GO.

2026-09-25 — U27 English boundary-punctuation normalization, local only; GitHub CI remains deferred by user direction. The phonemizer now strips only surrounding ASCII whitespace, quotation/bracket characters and sentence punctuation before dictionary/fallback lookup, while preserving the original lyric surface, apostrophe-led CMUdict words, internal punctuation and explicit `-`/`~` continuation behavior. Regressions assert exact CMUdict phones for `hello,`, `"hello!"`, `(world!)`, and `'cause`, and verify resolution does not mutate the source region. English pronunciation identity advanced to v7 because attached-punctuation lyrics now resolve to lexical phones instead of fallback/pause. Focused `seam_language_phonemizer_tests` passes Release and Debug (1/1 each); the Release `seam_tests` aggregate passes (1/1, 27.77 seconds); `git diff --check` passes. No broader English accuracy or native-speaker qualification is implied.

2026-09-24 — U31/U37 independent-review hardening, local only; GitHub CI remains deferred by user direction. U31 now preflights the aggregate SMF wire-event count (notes count twice, plus metadata and end markers) before encoder event allocation, refuses project PPQ outside 1..32767 rather than silently clamping, rejects nested repeated-pitch overlaps and conflicting lyric surfaces at one exported track onset, and builds per-region lyric indexes for whole-project conversion. It retains repeated MIDI lyric events in the codec itself; import reports ambiguity rather than rejecting otherwise valid source SMF. The U37 exported-worker checker now verifies response requestId/backendId, binds the native renderer's reported worker SHA to the selected worker, and validates worker/manifest/project hashes plus inference steps in prepared-inputs.json. Added exact/exceeded event-budget, PPQ-boundary, overlap/lyric-identity, lyrics-before-track-names text-budget, selected non-first region, symmetric partial-selection and field-level round-trip regressions. Release `seam_smf_interchange_tests` and `seam_interchange_service_tests` pass 2/2; exported-worker replay unit tests pass 3/3. The full pinned DiffSinger→trained acoustic/vocoder export→two production-worker processes→native authoring render→two-WAV project export command exited 0 with identity/hash/step binding enabled; it remains synthetic and ineligible for singer qualification or release. A prior attempt had emitted passing-looking JSON but exited -6 during runtime teardown; this rerun completed successfully, so both outcomes remain visible rather than treating the earlier process abort as a pass. GitHub workflows and `.github` were not inspected or modified for execution. U31/U37/Beta GO remain open.

2026-09-24 — U31 independent boundary-review repairs, local only; GitHub CI remains deferred. The reviewer reproduced two event-budget defects. Whole-project export now reserves serialized-event slots for each admitted track-name meta event as well as lyrics; selected-region export also omits a track name with an explicit loss when its event slot is unavailable. Names therefore degrade predictably rather than creating invalid scores at event boundaries. `SmfLimits.maximumEvents` bounds source events and diagnostic amplification; the new `maximumSerializedEvents` independently bounds canonical output, including repaired note-offs for input notes missing their release. This preserves the bounded two-source-event missing-note-off repair at an input event limit of two while allowing callers to enforce a stricter serialized output cap. Added regressions for two named tracks at exact serialized limits 10/11/12, selected-region name omission, and the two-event missing-note-off source under both independent ceilings. Focused SMF and interchange-service CTests pass Release, Debug and Sanitizer (2/2 in each configuration); `git diff --check` passes. These repairs have not yet received independent reviewer re-verification. This closes neither external multi-track GUI acceptance nor U31/U32/Beta GO.

2026-09-24 — U37 exported-model authoring-render integration, local only; GitHub CI remains deferred and `.github` was not changed. The pinned DiffSinger diagnostic now optionally composes its actual trained acoustic export with the locally trained synthetic vocoder export, verifies that the saved native test project's resolved phones match the model vocabulary, and exercises both fresh production-worker requests and the normal authoring renderer/project-export path. On this Apple Silicon checkout, the complete command with `--check-onnx --native-probe build/release/seam_onnx_runtime_probe --vocoder-checkout build/neural-runtime/singing-vocoders-source --production-worker build/release/seam_neural_worker --voicebank-cli build/release/seam_voicebank_cli --production-render-binary build/release/seam_neural_production_render` exited 0. Both worker requests returned identity-bound finite non-silent audio; the normal renderer produced 96,000 interleaved samples and exported two WAVs from the saved project. Separate worker renders are not required to be byte-identical because the acoustic diffusion sampler draws random noise. The run remains wholly synthetic and reports `singerQualified=false` / `releaseEligible=false`; its one-step synthetic vocoder's pitch-following diagnostic also failed, so this is path execution evidence, not U35/U36 quality or U37/Beta GO acceptance. The voice-model Python suite passes 457 tests with one skipped; four focused native CTests (production worker, bundle preparation, production render, worker relocatability) pass. No GitHub workflow was run or modified.

2026-09-24 — U31 whole-project multi-track SMF export path, local only; GitHub CI remains deferred by user direction. The conversion API now emits one named Type-1 track per vocal track, places notes and lyrics from all regions at absolute project ticks, retains global tempo/meter, and reports audio tracks plus unsupported SEAM-only controls as losses. The interchange service now uses whole-project export when neither track nor region is specified, preserves explicit one-region export when both are specified, and rejects partial selection. Standalone and CLAP score-export commands no longer implicitly restrict MIDI export to the currently selected region. Service regressions verify two-vocal-track Type-1 import→export→import with names, distinct pitches/lyrics and non-default tempo/meter at tick 960; partial-selection rejection; and multi-region absolute offsets on two output tracks. The focused service target passes Debug, Release and sanitizer builds; the Release aggregate `seam_tests` target passes. `git diff --check` passes. This closes the project-wide multi-track conversion/service gap; it does not establish current multi-track GUI or real-DAW acceptance. U31/U32/Beta GO remain open.

2026-09-24 — U31 pinned-corpus MIDI oracle revalidation, local only; GitHub CI remains deferred. The current Release CLI export of `tests/singing_quality/corpus/original-melody.seam` produced 1,355 bytes and SHA-256 `774231a09739d0f27ad39a4984a380bf0b5429ae5ac128c4259f2eb7a4afb79f`. The external oracle built against pinned OpenUtau `8c0dc4007e6e8c8181f3a12c10205671800eeb8b` and its DryWetMidi dependency read the exact file as Type 1 / 960 PPQ with 80 notes and 80 UTF-8 lyric events, ending `ORACLE_MIDI_OK`. The checked-in export interoperability expectation now pins this externally revalidated hash. This verifies the one-track corpus only; multi-track data exchange with an external host remains open.

2026-09-24 — U32 standalone and CLAP whole-score export wiring, local only; GitHub CI remains deferred. Added host-level regressions that populate a second vocal track, invoke each normal Export Score flow with a currently selected track/region, accept the conversion review, and reimport the resulting MIDI. Both assert that the second named vocal track, lyric, pitch and nonzero timeline position survive, preventing UI wiring from silently narrowing exports back to the selection. `seam_clap_microscope_tests` and `seam_u2_tests` pass in Debug, Release and sanitizer configurations; the Release CLI interop target also passes against the current externally revalidated hash. This verifies the command/service path but not AppKit dialog usability, a real DAW's multi-track GUI behavior, or U32 installed acceptance.

## Starting state: 2026-09-05

- Branch: `codex/production-readiness-completion`.
- Source commit: `69901159a27b2935bb8e40c4c96eccde781f0b9f`.
- Origin report SHA-256: `635606cfd10be803612dfcb47cf84651796a06860ff34dc8343705eac20c9c01`.
- Pre-existing work: 40 modified tracked files, comprising 2,912 insertions and 339 deletions, plus untracked U60 crash/support headers, probe, schemas and fixtures. The source report and implementation plan were also untracked. These changes are preserved, not attributed to this implementation run.
- Existing `build/dev` uses Ninja/Debug. Toolchain: Apple clang 21.0.0 targeting arm64 macOS, CMake 4.1.1. Compiler warnings-as-errors remain enabled. Local generated build identity is development identity, not release provenance; the source commit plus working diff must accompany any evidence.

## Active implementation

2026-09-28 — U21 generation requests and terminal outcomes in the producer, local only; GitHub CI remains deferred and `.github` was not touched. The producer workspace now keeps a request registry (`generation_request.hpp`, `repository_generation.cpp`): each request is an immutable, create-new record of the exact producer generation it expects, the recipe, every job's ID, take, style, coverage, pitch and duration, and its budgets, with at most one terminal record (COMPLETED, STALE or BUDGET_EXHAUSTED). Both records are re-verified against producer history on every read, and COMPLETED must be proven by one `import-generated-batch` generation per batch that introduces exactly that batch's takes unreviewed. No producer journal action was added: the registry is its own append-only record, so a submission does not advance the producer and stale its own expectation. Campaign advancement is one typed operation shared by the CLI and Studio (`advanceGenerationRequest`): collection, completion, cancellation, staleness and an exhausted output budget are reported with the producer state they left; terminal outcomes are recorded before they are reported and returned without work on retry; and a take ID held by another result reads as staleness. New CLI commands are `submit-generation-campaign`, `list-generation-requests` and `inspect-generation-request`; `advance-generation-campaign` exits 3 for STALE, 4 for BUDGET_EXHAUSTED and 128+N on signal N. `seam_voice_generation_workflow_tests` (5 cases) covers resume without duplicate takes or approval, staleness from a changed recipe, a manual candidate, an inventory change and a bare save, cancellation and budget exhaustion leaving the producer byte-identical, registry admission, and inspection, independent review and edit of a generated take. The Python mirror (`_production_generation.py`, wired into `validate-workspace --draft` and `inspect-generation-requests`) matches the real CLI record for record and refuses eight forged records in both readers; 12 C++ and 8 Python mutations were each caught. Contract note: `GENERATION_REQUESTS_2026-09-28.md`. Branch `codex/u21-generation` (`c8f32ceb..832e946d`) was merged as `4113889a`; on the merged tree the full Release CTest passed 213/213 and the Python production (115) and external_beta (190) suites passed. U22's request queue and job-detail view now expose verified campaign definitions, but do not report live per-job progress or close the full Studio lifecycle. CLI prepare/run/import-generation verbs already exist (`prepare-generation`, `run-generation`, `prepare-generation-batch`, `run-generation-batch`, `import-generated` and `import-generated-batch`); durable registry submission/terminal tracking is currently campaign-scoped. Campaigns predating the registry stay unregistered, the budget counts the whole campaign directory, terminal records are integrity records rather than signatures, and every decision is a fixture operator's. No human listening, reviewer or V16 evidence exists.

2026-09-28 — U15 versioned acoustic analysis and conditioning, and U16 voiced PSOLA edges, local only; GitHub CI remains deferred and `.github` was not touched. The stored acoustic analysis and pitch-mark generation now share one voicing partition (`partitionPitchFrames`, nearest window centre); analysis algorithm version 2 refuses version-1 records, and marks are placed run by run inside voiced regions only. On 48 kHz engineering fixtures the voiced/noise/voiced boundaries moved from 18688/27392 to 19584/28288 (largest error 1408 → 512 samples), and generated marks inside spans the analysis calls unvoiced fell from 8 (and 7) to 0, none deeper than 512 samples into noise. `BankValidator` reports `pitch-marks-unvoiced` (an error for unlocked marks, a warning for reviewer-locked ones). New `storeBankAcousticAnalyses` writes audio-bound records, and sample candidate publication runs it before QC and the content identity. The bank content identity, the installer's package identity and the render snapshot identity now cover present analysis records and are unchanged when none exist; previously a regenerated analysis reused PCM rendered under the old voicing. `requireCurrentOperation` refuses derived audio recorded by a superseded method (`linear-v1` resampling) in the draft and candidate paths and names the remedy (re-derive from the preserved raw take). Classic PSOLA (renderer revision 12) retargets the voiced attack and release as well as the sustain: on a 440 Hz consonant-vowel fixture rendered to 220/329.63/659.26 Hz both moved from 440.00 Hz to within 0.4 cents, the noise onset keeps above 0.99 correlation with the source, a +700-cent jump at the release is followed, and the vowel boundary stays within one hop. New tests are in `test_audio_conditioning.cpp`, `test_performance_snapshot.cpp` and `test_voicebank_production_project.cpp`; nine mutation checks each fail a named test. Branch `codex/u15-acoustic` (`1b6be230..a1874235`) was merged as `60415dc4`; the merged-tree results are those recorded in the U21 entry above. Engineering fixtures only. U15 is implemented but not accepted: regenerating superseded derivative audio is a manual producer step, and voicing boundaries resolve to about ±512 samples. U16 remains partial: the spectral and stretch renderers still copy edges at source pitch, the comparison with the alternative analysis/synthesis trial and corpus-level numbers have not been run, and no listening, native-speaker or reviewer evidence exists. No V-gate changed state.

2026-09-28 — U13 review, retake and take selection bound to the material decided, local only; GitHub CI remains deferred and `.github` was not touched. Review status now comes only from an independent decision recorded together with the material it was made on. The single durable append path enforces the rules (`writeGeneration` calls `review_internal::applyReviewTransition`), so no command, Studio action or generic save can bypass them:

- History is append-only. Review decisions, review material, annotations, receipts, processing revisions, stored assets, and each take's identity, lineage and processing chain are never removed or rewritten; an edit only extends a chain.
- Marker review or approval is granted only by a `review` event. For each decided take it appends one decision and one `sample-candidate-review-v2` record bound to the take's current audio (raw asset through its processing chain) and review basis. The reviewer must be a registered REVIEWER and not the take's importer, processor, annotator or inspector. An approval also needs the take's current `take-inspection.v2` receipt for its stored bytes, the requirement U12 deferred here; a rejection needs none.
- Imports, generation, edits, retakes, selection and saves never grant review. `RawTakeInput::review` is removed: an import starts in MarkerReview, or Rejected when Studio QC fails. `recordMetadataRevision` refuses review-material kinds.
- An approval is lowered to MarkerReview when a generation's change reaches its take and its latest PASS and v2 record no longer describe the current audio and basis. A change reaches a take through its own row, its assignment, a new annotation or receipt, or a producer-wide field such as inventory, source policy or language. An approval without recorded material (legacy history) stands until its own take changes.

New `select-take` fills an assignment with a retained alternative: repository `selectTake`, CLI `select-take WORKSPACE PROJECT_SHA256 TAKE PRODUCER UTC` and a new journal action. Both takes keep their history. The selected take returns to marker review, because a decision is bound to the assignment it filled.

`seam_production_review_tests` (10 cases) covers the grant, history, independence, receipt and lowering rules, including the durable path's own backstop for every kind of per-take change and legacy grandfathering. It also covers AE1: an approved, normalized and annotated take is published and then retaken. The installed bank's file hashes and the old take's audio, chain, annotation, decisions and material are unchanged, and the retake waits for review. The stale candidate cannot be published, and the reviewed retake publishes to a new directory without overwriting the installed one. A duration-changing Trim lowers the approval: markers placed for the longer audio can no longer be put in front of a reviewer, and the new decision binds the remapped markers. Updated `seam_voicebank_production_tests`, `seam_production_ownership_tests` and `seam_export_tests` now approve only through the review API and expect a forged approval save to be refused.

The external validator gets a new `tools/external_beta/_production_review_transition.py`, which applies the same rules to every pair of generations in `validate_draft_workspace`. `review_basis` reproduces the C++ basis byte for byte, using the pretty project encoding and the same filtering. `tests/production/test_review_transition_mirror.py` (10 tests) drives the real CLI:

- Every generation file re-encodes byte for byte, and the basis `review-sample` recorded equals the mirrored basis.
- A retake followed by `select-take` validates.
- Five forged next generations are each refused for their own cause: a hand approval, a decision on another basis, the producer as reviewer, a rewritten decision, and a stale approval kept. Two controls that model the C++ output pass.

The legacy style-migration fixture now imports its takes for the inventory's prompts, carries genuine v2 receipts and records its approval through a review; the C++ `migrate-style` still applies it.

Mutation checks: 7 single-rule C++ mutations (grant refusal, stale lowering, append-only reviews, reviewer independence, inspection receipt, and affected takes forced to none or to all) and 9 Python mutations (the same rules plus the basis filter and the encoder's trailing newline) were each caught by a named test. The previously reported one-off `seam_export_tests` failure did not recur in 23 runs, 18 of them with six concurrent copies. Release ctest passed 212/212.

Limits: every decision here is a fixture operator's; no human reviewer, listening packet, rubric or singer qualification exists. Marker and acoustic review remain one Accept decision. A separate marker-only step (MarkerReview to PitchReview) is not implemented, and `ReviewRecord.result` stays PASS/REJECTED. Studio has no alternative-take picker yet; selection is repository and CLI only. The receipt an approval requires describes the raw stored bytes, not derived audio, which the reviewer judges by listening. No Beta gate changed state.


2026-09-28 — U12 take QC bound to the imported bytes under per-unit policies, local only; GitHub CI remains deferred and `.github` was not touched. `4aab93b2` replaces the voiced-only dry-take check with `seam.take-inspector` v2 and QC policy version 1, where the canonical coverage key selects the policy:

- Voiced: audible, root within ±80 cents.
- Breath: audible, voiced share of energetic windows ≤ 0.5.
- Pause or Closure: quiet, RMS ≤ 3e-3 and peak ≤ 3e-2.
- A special key that mixes these phone classes is refused rather than guessed.

Every raw, procedural and batch import:

- reinspects the source under its assignment's policy before copying;
- refuses a caller-supplied receipt that differs;
- requires the stored bytes' SHA-256 and size to equal the inspected ones;
- appends a `take-inspection.v2` receipt bound to take, prompt, coverage key and pitch layer. Style is left out because the style migration relabels takes.

The asset store now publishes only verified copies and repairs a corrupt or truncated content address. Project validation re-derives each v2 receipt's outcomes. `dry-take-inspection.v1` remains readable history but no longer admits a take. Release ctest passed 211/211, including the new `seam_take_inspection_tests`.

The external validator `tools/external_beta/_production_draft_validation.py` now mirrors the receipt exactly: the C++ reader's integer and number bounds, the policy map, outcome rules, and the canonical `%.17g` bytes. `tests/production/test_take_inspection_receipt.py` imports the committed nasal-consonant "ma" bake through the real CLI into breath, pause and closure units and two voiced units (MIDI 69 passes root pitch, MIDI 60 fails). It then requires the C++ loader and the Python validator to refuse the same 20 forged receipts and a stale digest, and to accept an unchanged control. Seven single-rule mutations of the mirror were each caught by a named forgery. The Python production, external-beta and source-closure CTest entries passed 8/8.

Limits: a receipt is automated signal evidence about exact bytes, not listening, marker or pitch review. Validation checks receipts that exist but does not yet require a current v2 receipt before Accept; that is U13 work. No reviewer, rubric, FL Studio, VoiceOver or Beta gate changed state.

2026-09-28 — Native editor redesign §14.4 full-matrix reproducibility, local only; GitHub CI remains deferred and `.github` was not touched. Two frozen-clock `--full-matrix --no-appkit` packets were captured at clean master `347bd79d`, each `captures=224 errors=0 failed_or_unreached=0`. `compare_fidelity_packets.py --require-identical --require-plan-matrix` reported 160/160 plan score cells in both packets and 224/224 identical frames by RGBA pixel hash, with the same clean source and app binary SHA-256. The tracked summary `docs/design/evidence/ui-fidelity-matrix-347bd79d.json` keeps both run IDs and every frame hash; the completion report's §14.4 row now reads met for the macOS software raster on this machine. This is not AppKit-window, Win32/X11, FL Studio, VoiceOver or rubric evidence, and it closes no Beta gate.

2026-09-28 — Native editor redesign §10 cold frame met in the recorded runs, local only; GitHub CI remains deferred and `.github` was not touched. `3fb39459` makes `CoreGraphicsCanvas` draw two-stop gradient fills of rounded rectangles and circles in software through the shared `paint::fillRoundRect*` rasterizer. This covers note capsules, knob bodies and glass panels; CoreGraphics had been building a shading for each. The canvas takes this route only under plain state and falls back to CoreGraphics for everything else. `ScopedBackendGradients` lets tests measure against CoreGraphics. New paint tests prove whole, clipped and banded drawing agree exactly, check the software ramp within 0.27 levels of the exact gradient, and bound the difference from CoreGraphics. Release ctest passed 210/210. The 40-sample full benchmark passed every case in both looks, true-cold p50/p95 of 10.89/12.47 ms EMO and 10.99/12.55 ms SCENE, and the design-shell gate reported a pass. All ten alternating A/B look-runs at load ~12 stayed under 14 ms. No unit, rubric, FL Studio, VoiceOver or release gate changed state.

2026-09-28 — Native editor redesign cold frame (design plan §10), local only; GitHub CI remains deferred and `.github` was not touched. `87d8fe02` paints the glass-panel fills in software in the wash's band pass, with an 8×8 ordered dither that matches CoreGraphics' anti-banding behaviour. It also makes the squared-radius wash table hold the ramp samples it replaces. Earlier the same day, `2e6712f4` gave every overlapping-note group a badge and made the error toast wrap and show its cause. `2116f741` sped up the wash without changing a pixel. Release ctest passed 210/210. True-cold p50 is now 12.1–13.0 ms in both looks, down from ~15.2 ms. p95 passed 14 ms in 4 of 8 look-runs at load average ~8.5, so §10 is still open. No unit, rubric, FL Studio, VoiceOver or release gate changed state. Evidence: `docs/design/evidence/BENCHMARK_2026-09-28.md` and `docs/design/COMPLETION_REPORT_2026-09-28.md`.

2026-09-25 — U22 producer-recording WAV export/hash moved off the owner thread, local only; GitHub CI remains deferred and `.github` was not touched. Stopping a production recording now retains the in-memory capture while an async worker writes the WAV and hashes the exact saved bytes, then starts the existing cancellable import with that digest as an expected identity. The import worker rejects changed bytes before repository commit; a new regression proves this mismatch leaves producer generation, takes and assets unchanged, then imports the unchanged digest-bound WAV through the normal path. Added retained progress/failure status for visual/accessibility surfaces, prevented discard during export/import, and made shutdown drain export before import. Release and Debug `seam_voicebank_studio_native` and `seam_tests` targets build; full `seam_tests` CTest passes 1/1 in Release (29.99 seconds) and Debug (195.30 seconds). The 17 macOS source-contract tests pass; `git diff --check` passes. This removes WAV write/hash work from the production stop interaction and binds import to those exact bytes, but does not establish live microphone, device-disconnect or perceptual acceptance, or close U22/Beta GO.

2026-09-25 — U4.2 creator-to-installed-singer independence regression, local only; GitHub CI remains deferred by user direction and `.github` was not inspected or modified. Strengthened the native singer-song journey to publish through the Designer session, install via the standalone application's `InstallProceduralSinger` command, then destroy the Designer and remove the producer draft, original package, and staging tree before binding/rendering. The installed singer then sings, exports, saves the project, and is reopened in a fresh editor session; installed resource identity/content hash remain stable and the reopened export master SHA-256 exactly matches the first export. Fresh Release and Debug `seam_original_singer_song_journey_tests` builds pass; CTest passes 1/1 in both (19.16 and 95.49 seconds). `git diff --check` passes. This guards against accidental dependence on producer-side files and proves deterministic save/reopen for this fixture; it is not human listening/creator acceptance, cross-machine package portability, or U4.2/Beta GO completion.

2026-09-25 — U38 neural manual-vibrato ownership regression, local only; GitHub CI remains deferred. A new compiled-performance→neural-request→DiffSinger-input test verifies accepted automatic pitch reaches F0 when manual vibrato is off, but an enabled note-level vibrato owns that note's pitch and suppresses the generated lane even before vibrato onset. In that case the neural request equals the score compiler's single score-plus-vibrato contour, and hop-quantized DiffSinger F0 equals the corresponding request samples, preventing a second oscillator/modulation. Release and Debug `seam_neural_worker_protocol_tests` pass 1/1 each. This verifies control-contour ownership using a synthetic model contract, not learned-model audio quality or U38/Beta GO acceptance; the current phrase-aware generator is still structural, not learned. `.github` was not inspected or modified.

2026-09-25 — U22 reviewer-visible dry-take criteria, local only; GitHub CI remains deferred. The human sample-review packet now expands matching, digest-bound dry-take inspection into the WAV sample rate/channel/bit-depth, expected and analyzed root MIDI, peak/RMS/DC measurements, inspector identity/version, and six individual automated outcomes (format, finite samples, clipping, silence, DC offset, root pitch). Every item remains labeled automated; an overall pass still says human review is required, and no reviewer or `ReviewRecord` is synthesized. A native review regression checks the full passing detail set and confirms no human approval. Release and Debug `seam_studio_sample_review_tests` pass 1/1 each; the Voicebank Studio app builds in both configurations; `git diff --check` passes. This improves decision transparency, not perceptual qualification, take approval, or U22/Beta GO completion. `.github` was not inspected or modified.

2026-09-25 — U39 source-filter expression now guards ordinary pitch and phrase duration, local only; GitHub CI remains deferred. Extended the real production-pipeline song regression for formant, breathiness, tension, airiness, gender and growl: each edit must alter PCM, preserve total output frame count, and keep both note fundamentals within 8 cents of its unedited baseline. The bounded autocorrelation oracle uses a sub-sample parabolic peak fit; initial integer-lag measurement falsely reported 10.6 cents on the second note, which disappeared after improving the measurement precision. Release and Debug `seam_expression_on_song_tests` pass 1/1 each. This verifies a synthetic source-filter fixture's pitch/timing invariants, not perceptual intent, all score pitches, alternate carriers, or U39/Beta GO. `.github` was not inspected or modified.

2026-09-25 — U22 human-review packet now surfaces hash-matched dry-take technical inspection, local only; GitHub CI remains deferred. For the exact captured sample candidate, Studio review details now locate `dry-take-inspection.v1` only when both the take ID and raw audio SHA match the review packet, and display the recorded signal-check status, inspector/version and peak/RMS/DC measurements. The status is explicitly labeled automated and says human review is still required; it creates no reviewer identity or approval. A native sample-review regression imports a valid digest-bound fixture and verifies the values appear while `ReviewRecord` remains empty and no reviewer is selected. Release and Debug `seam_studio_sample_review_tests` pass 1/1 each; the Voicebank Studio app builds in both configurations; `git diff --check` passes. This makes existing technical evidence visible at the human decision point; it does not establish listening quality, approve a take or close U22/Beta GO. `.github` was not inspected or modified.

2026-09-25 — U22 raw-take inspection evidence separated from human review, local only; GitHub CI remains deferred. The recording/import path previously wrote the automated dry-take threshold result into the producer's `ReviewRecord` list as `PASS` under the operator's reviewer identity. It now commits a versioned `dry-take-inspection.v1` metadata revision atomically with the content-addressed raw WAV, binding the canonical measurements/status, inspector ID/version, raw SHA-256, take ID, operator and timestamp. Project validation checks the record's exact field set, bounds, digest, raw-take association and derived status. Automated passing signal checks leave the take in MarkerReview and no longer create a human `PASS`; the Studio and `.inspection.json` now label them as signal checks and state human review is pending. A native import regression verifies the metadata binding and absence of a fabricated review, and rejects both a tampered digest and a semantically forged status with a recomputed digest. Release/Debug `seam_u2_tests` and `seam_voicebank_production_tests` pass 2/2 per configuration; the changed native journey is in `seam_tests`, which passes 1/1 in both configurations (1,111 test cases in the Debug run); the macOS Studio app builds in both configurations and `git diff --check` passes. This improves provenance semantics but does not qualify a take, replace marker/pitch/listening review, or close U22/Beta GO. `.github` was not inspected or modified.

2026-09-25 — U39 source-filter timbral-expression integration regressions, local only; GitHub CI remains deferred. The production-path song fixture now verifies all six currently supported timbral curves (formant, breathiness, tension, airiness, gender, and growl) change rendered PCM, survive canonical project-JSON encode/decode with each curve equal after reload, and produce byte-identical PCM before and after that round trip. A separate whole-song invariant proves adding explicit zero-valued automation for all six controls is byte-identical to an unedited baseline. The focused Release `seam_expression_on_song_tests` CTest passes (1/1; per-control audible-change, carrier-refusal, persistence and neutral-identity cases); its target now links the project-format codec explicitly. This closes a source-filter integration and serialization regression gap only: it does not prove intended perceptual quality, ordinary-pitch/timing independence across every route, trained neural expression, full U39, or Beta GO. `.github` was not inspected or modified.

2026-09-25 — U39/U6 procedural project PCM caching, local only; GitHub CI remains deferred. `ProductionProjectRenderer` now loads/stores procedural phrase PCM through the existing bounded `PcmCache`, keyed by the render snapshot's canonical content identity (score, pronunciation, recipe/style, expression, quality/sample rate and render ABI). Cache hits validate sample rate and reconstruct the same mono route; misses store the exact renderer output with procedural provenance. A production-project song regression proves cold render → exact in-memory hit → memory eviction → exact disk hit, then independently edits all six source-filter timbral controls and verifies each gets a distinct cache miss/render followed by an exact warm hit. Release expression, original-singer song and procedural-install journey CTests pass 3/3; `git diff --check` passes. This closes a procedural project-render cache gap and gives U39 expression invalidation evidence, not acoustic qualification, cache-soak/performance qualification, U6/U39 or Beta GO acceptance. `.github` was not inspected or modified.

2026-09-25 — U38 standalone proposal responsiveness, cancellation and stale-result safety, local only; GitHub CI remains deferred by user direction. Automatic performance generation now runs against its immutable captured score on a worker thread instead of blocking the editor command. The native editor polls completion on its owner thread, where the capture's stale-session check guards adoption; the Performance menu distinguishes a running proposal from an empty proposal list and exposes Cancel Generation even when earlier takes already exist. User cancellation requests the worker stop and discards its eventual result without changing the document; controller shutdown also cancels and joins. Regressions change the score during generation and verify Conflict/no publication, and cancel generation at the backend's 4,096-note admission boundary with no published take. Release `seam_tests` passes (1/1 CTest; 1,110 internal cases), the `Project SEAM` Release app builds, and `git diff --check` passes. This improves workflow safety only: the phrase-aware generator remains deterministic/structural rather than a trained singing model, so U38 quality acceptance and Beta GO remain open. `.github` was not inspected or modified.

2026-09-25 — U22 articulation-source listen-and-adjust regression, local only; GitHub CI remains deferred. The existing Designer phrase-preview path is now guarded by an edit/render/undo loop for one unvoiced affricate, one approximant and the breath source. Each authored source edit must change production-stream PCM; undo must restore the exact baseline PCM, and audition identity must remain bound to the selected phone. Release `seam_voice_designer_tests` passes (1/1 CTest); the native `SEAM Voicebank Studio` app builds. This establishes an engineering feedback loop for these three procedural source families, not pronunciation intelligibility, acoustic/listener qualification, full U22 or Beta GO. `.github` was not inspected or modified.

2026-09-25 — U37 neural request memory-boundary hardening, local only; GitHub CI remains deferred. `prepareNeuralScoreRequest` no longer allocates an all-zero breathiness plane when the model/score yields no nonzero breathiness. On first nonzero frame it preflights all three Float32 planes against the configured frame-byte ceiling before allocating that optional plane. Regressions prove the two-plane base fits its exact payload budget, a three-plane request fits at the exact boundary, and one byte below that boundary rejects. Release `seam_neural_worker_protocol_tests`, `seam_neural_render_tests`, `seam_neural_render_workflow_tests`, and both production-render journeys pass 5/5; the three available Debug targets pass 3/3 (production-render targets are not configured in Debug). `git diff --check` passes. This closes a request-preparation allocation gap only; it does not qualify the neural model or U37/U39/Beta GO. `.github` was not modified.

2026-09-25 — Full local Release rebuild and regression sweep, GitHub CI deferred. `cmake --build build/release -j 8` completed all 128 build steps after the Korean Unicode normalization change. `ctest --test-dir build/release --output-on-failure -j 2` ran all 185 registered tests: 184 passed, including the full `seam_tests` aggregate, language phonemizer target, original-singer song journey, singing-quality workflow and voice-model training suite (53.79 seconds); one failed, `seam_tracked_source_closure`, because 18 required source inputs are untracked in this user-owned worktree. Those files were not staged or otherwise altered to force closure green. `git diff --check` passes; no GitHub workflow ran and `.github` remains untouched. This is broad local regression evidence, not a clean source snapshot, install qualification, human listening result, or Beta GO.

2026-09-25 — U28 canonical Unicode Hangul input, local only; GitHub CI remains deferred. The Korean resolver now composes modern Unicode leading/vowel/trailing jamo algorithmically before its existing syllable/rule path. Precomposed and canonically decomposed `꽃잎` produce identical phones, pronunciation sequence identity and resource identity; a compound-vowel `왜` case also passes. Compatibility and archaic jamo are not heuristically composed and keep the existing visible diagnostic behavior. The focused Release `seam_language_phonemizer_tests` passes 1/1 (27 cases), and the aggregate Release `seam_tests` plus the focused target pass 2/2. This improves Unicode input normalization only; Korean still lacks the reviewed lexical/resource coverage and native-speaker/song qualification required by U28/R7. `.github` was not modified.

2026-09-25 — U28 Korean Rule 29 lexical nasal-insertion increment, local only; GitHub CI remains deferred. The built-in Hangul resolver now applies a source-versioned, exact-word allowlist at the compound boundary for nine documented forms (`꽃잎`, `깻잎`, `막일`, `솜이불`, `홑이불`, `한여름`, `나뭇잎`, `논일`, `앞이마`). Insertion runs before the existing coda-assimilation rules, preserving both coda nasalization and the added onset. It deliberately does not infer morpheme boundaries from spelling, and `꽃이` remains ordinary liaison as a negative control. The focused Release `seam_language_phonemizer_tests` passes 1/1 (26 cases); pronunciation resource and sequence hashes are asserted. The Korean resource digest covers the source change, so pronunciation/cache identity changes with the lexicon. Evidence is limited to these curated standard-pronunciation forms; Korean remains a bootstrap service, not a reviewed dictionary, complete G2P, singer-bank match, or Beta GO claim. `.github` was not modified.

2026-09-24 — U31 malformed-text diagnostic quality and bounded DAW return exchange, local only; GitHub CI remains deferred. The second-developer review approved the preceding MIDI intake fix and noted repeated identical warnings/losses could flood review UI. Decode now aggregates malformed comments, malformed lyrics, removed lyric terminators and lyrics made empty by a terminator per source track/category, preserving severity and first/last source ticks; empty and all-NUL lyrics are Losses rather than misleading successful matches. Diagnostic text pluralizes and includes PPQ. Regression coverage includes zero-length/NUL-only lyrics, doubled/embedded NUL, invalid UTF-8 after a terminator, repeated events on separate tracks with disjoint tick ranges, and end-to-end project import retaining notes plus no-matched-lyrics disclosure. MIDI v1 docs state that project import maps lyrics only from `0x05`; ordinary `0x01` text is not interpreted as Soft Karaoke lyrics. The rebuilt Release CLI completed a real FL Studio 2025.2.5 exchange: SEAM MIDI opened in a fresh FL process, FL exported Type-1 four-track MIDI, SEAM imported 80 notes with 39 explicit controller/program/pitch-bend losses and one no-lyrics track warning, then exported a selected-region MIDI with zero export issues. Details/hashes are in `SMF_INTERCHANGE_2026-09-08.md`. This is a bounded round-trip smoke test, not full-project multi-track export or GUI multi-track acceptance. Focused suite passes 27/27 in Release, Debug and sanitizer builds; selected Release CTest targets (`seam_smf_interchange_tests`, `seam_interchange_service_tests`, `seam_tests`) pass 3/3 in 28.30 seconds; `git diff --check` passes. U31 multi-track GUI review, U32 review and Beta GO remain open.

2026-09-24 — U31/U32 Type-1 shared-service boundary, local only; GitHub CI remains deferred. Added a service-level Type-1 import case with a named tempo/meter-only Conductor track followed by named Lead and Harmony tracks at the same onset but different lyrics/keys. Non-default tempo (100→150 BPM) and meter (3/4→5/8) changes on the conductor prove both map into the project at tick 0 and at tick 960 after source 480 PPQ scaling. It also proves the conductor does not create a singer track; each vocal track retains its name, one note/lyric, converted duration, and exact note-to-lyric token binding; the project name is retained and the source hash is unchanged. The independent reviewer approved the per-track behavior and requested these conductor/timing/binding/hash assertions; after noting that default-equal tempo/meter values did not prove import, the second tempo/meter change and scaled-tick assertions were added. `seam_interchange_service_tests` passes in Release, Debug and sanitizer builds (19/19 each); the Release aggregate `seam_tests` CTest passes. This validates the service path, not multi-track UI behavior or project-wide export. U31 GUI multi-track verification and U32 installed/embedded acceptance remain open.

2026-09-24 — U31 deterministic Type-1 track mapping, local only; GitHub CI remains deferred. `SmfScore` now retains source track names/order, and notes/text retain source membership. The encoder writes deterministic multi-track Type-1 output; project import creates separate vocal tracks/regions for note-bearing source tracks and pairs lyrics within each source track. A two-track regression covers decode/encode membership and names plus distinct imported notes/lyrics. The focused `seam_smf_interchange_tests` target passes 19/19 in Debug, Release and sanitizer builds; selected Release CTest targets (`seam_smf_interchange_tests`, `seam_interchange_service_tests`, `seam_tests`) pass 3/3; `git diff --check` passes. This closes the known flattening gap in codec/conversion behavior, but not U31: real-DAW exchange, remaining acceptance evidence and U32 review are still open.

2026-09-24 — U31 second-developer review remediation, local only; GitHub CI remains deferred. Fixed the review's legacy-name compatibility, separate/ambiguous lyric-track mapping, lyric-free/percussion disclosure, stable same-tick text ordering, track-name-first event ordering, selected-region track-name export, project-track-name bounds, and stale OpenUtau evidence wording. Import preserves notes on all note-bearing tracks but warns when a track has no matched lyrics or includes channel-10 percussion; it does not silently infer accompaniment intent or drop notes. Project conversion currently exports one selected region as one named track; project-wide multi-track export remains unimplemented and is not implied by codec support. Export omits invalid/NUL-bearing or over-budget SEAM track names and NUL-bearing lyric text with losses rather than refusing other musical events; same-track lyrics take precedence over fallback lyrics from a separate track. A follow-up review confirmed the NUL-lyric export repair and exposed a pre-existing import edge case: NUL/legacy-encoded text events rejected otherwise valid files. Import now drops invalid UTF-8 or embedded-NUL comments/lyrics with explicit Loss diagnostics, strips one trailing lyric NUL with a Warning, and retains notes; it does not guess legacy encodings. Added regressions for Shift-JIS names and comment/lyric payloads, terminated lyrics, separate lyrics, a conductor-only track with two note tracks and an ambiguous lyric, duplicate names, same-tick lyric order, percussion/no-lyric warnings, import/export name bounds, and NUL-bearing names/lyrics. Focused tests pass 27/27 in Release, Debug and sanitizer builds; selected Release CTest targets (`seam_smf_interchange_tests`, `seam_interchange_service_tests`, `seam_tests`) pass 3/3 in 27.28 seconds; `git diff --check` passes. The earlier OpenUtau GUI receipt is explicitly labeled pre-track-mapping and is not acceptance evidence for the new behavior. Real-DAW return exchange, current multi-track GUI verification, U32 review and Beta GO remain open.

2026-09-24 — U30 exact dynamics-capacity boundary, local only; GitHub CI remains deferred. The
USTX-to-SEAM converter previously reserved three possible guard anchors even when a curve over the
entire part emitted none, so an exactly representable 16,384-point dynamics curve was incorrectly
discarded. Admission now counts the actual start/guard/end anchors for that curve. A regression
proves the exact point ceiling imports and one point beyond is omitted with an explicit conversion
loss. The focused USTX interchange target passes in Debug, Release and sanitizer configurations;
`git diff --check` passes. This does not close parser-budget breadth, GUI-saved file coverage,
vibrato/audio equivalence or U30/Beta GO.

2026-09-24 — U30 first native desktop OpenUtau open/save smoke test, local only;
GitHub CI remains deferred. Built the pinned `8c0dc4007e6e8c8181f3a12c10205671800eeb8b`
OpenUtau source as a local macOS app bundle, opened a disposable copy of the 0.9
serializer-generated dynamics-curve project, and saved it from the desktop UI.
OpenUtau displayed `Project saved.` Its output preserved two notes and the 64-point
`dyn` curve while normalizing part duration from 960 to 1440 ticks. The resulting
GUI-saved fixture is hashed and covered by a native decode/import regression. This
is one constructed GUI smoke test, not an independently authored user project,
official versioned distribution, or audio-equivalence check. The focused
`seam_ustx_interchange_tests` executable passes 49/49 in Debug, Release and
sanitizer builds; `git diff --check` passes and `.github` remains unchanged. U30
remains PARTIAL.

2026-09-24 — U31 SMF active-note allocation bound, local only; GitHub CI remains
deferred. `decodeSmf` now admits the combined count of completed and active
notes against `SmfLimits.maximumNotes` before appending each note-on to its
per-key FIFO. This prevents unmatched notes from consuming the larger event
budget and being rejected only after parser allocation. A regression with a
one-note limit verifies the second active note is refused before a later
malformed track terminator is reached. The focused `seam_smf_interchange_tests`
suite passes 19/19 in Debug, Release and sanitizer builds. Release
`seam_interchange_service_tests` and aggregate `seam_tests` also pass (3/3
selected CTest targets total); `git diff --check` passes and `.github` remains
unchanged. This is a parser resource-bound repair, not real-DAW interoperability,
U31 or Beta GO acceptance.

2026-09-24 — U31 independent OpenUtau MIDI round-trip, local only; GitHub CI
remains deferred. The checked-in `original-melody.seam` exported to Type-1 MIDI
(1,331 bytes, SHA-256 `6c94cd7575d8e95ae066e4bbd682aa349dec1806a347466b5e53b6819b0927eb`)
with zero issues. The pinned OpenUtau desktop app opened it and saved it as USTX
(32,351 bytes, SHA-256
`344a2bdbc23ac1028f8021f3920a8e284ea854f7f43aa0a2a7bf6bc6c764e120`). SEAM
imported that GUI-saved project and re-exported MIDI byte-for-byte identical to
the original. This independently verifies one shared-field round-trip through
the OpenUtau desktop workflow despite eight explicitly reported USTX losses. It
is not a DAW-host run or human-authored project; U31/U32 and Beta GO remain
incomplete. Full receipt is in `SMF_INTERCHANGE_2026-09-08.md`.

2026-09-24 — U23/U24 overlap, overflow and accessibility-boundary regression increment, local only;
GitHub CI remains deferred. Added layout coverage proving lowest-band reuse at an exact
end/start boundary, and piano-roll coverage proving direct hit testing and box selection target
only painted overlap bands while the overlap cycle still exposes every member. Added a Japanese
compact-lyric assertion that visible truncation uses an ellipsis while retaining the full lyric.
The broadened native accessibility tree no longer publishes clipped-away expression rows as
zero-area focus targets; its capability summary retains each control's availability, curve-point
count and current playhead value. The full `seam_tests` binary passes 1,073/1,073 in Debug and
Release, `git diff --check` passes, and `.github` is unchanged. This does not establish visual
polish across installed surfaces, complete dense-note editing semantics, or U23/U24/Beta GO.

2026-09-24 — U20 Japanese voiced-affricate `j` inventory increment, local only; GitHub CI remains
deferred. The original procedural singer recipe now declares its own same-phone resonance, voiced
closure, release burst and voiced frication tail for `j`; the lyric journey exercises it with じ.
The checked-in recipe was regenerated from the production encoder, so its canonical bytes match the
song-package source and the package manifest declares the phone. Inventory coverage now treats a
recipe-authored `cv:j:a` as prepared while retaining an explicit refusal case for an unknown phone.
The inventory suite passes 3/3 and the installed-singer authoring/export journey passes 5/5 in Debug
and Release; `git diff --check` passes. Parameters are development screening values only: no listener
has qualified `j`, the singer identity or broader Japanese coverage, and U20/U42/U43/Beta GO remain open.

2026-09-24 — U41 face-anchored mouth overlay rendering regression, local only; GitHub CI remains
deferred. Added a raster-level test for the normalized `mouthPlacement` path: an alpha-transparent
mouth sprite leaves the portrait's face pixels intact, while its opaque authored pixel lands inside
the declared face rectangle. A package-loading regression also proves that a flat PPM corner key is
made transparent while the authored mouth pixel remains opaque. The focused character-performance
dock suite passes 7/7 and the package suite passes 9/9 in Debug and Release; `git diff --check` passes.
This proves package decoding/compositing/placement on the local raster surface, not final character
art quality, installed-platform display, or U41/Beta GO acceptance.

2026-09-24 — U41 singer-resource binding regression coverage, local only; GitHub CI remains
deferred. Added a package-level schema-3 case proving that the declared procedural singer identity
must match kind, ID, version and content digest before character performance can be followed or
published. The test also keeps schema 3 admitted as the current contract and moves the future-schema
rejection case to version 4. Focused character-package suite passes 8/8 in Debug and Release;
`git diff --check` passes. The suite covers code-level binding, not asset rights/provenance, final
character artwork, installed visual behavior or U41/Beta GO acceptance.

2026-09-24 — U23 dense-overlap layout complexity increment, local only; GitHub CI remains
deferred. Same-pitch interval groups now use min-heaps for active bands and reusable band indices
instead of scanning every allocated band for every note. The allocator still releases bands at
`end <= start` and always reuses the lowest available index, preserving the prior deterministic
paint ordering while reducing assignment from quadratic to O(n log n) in a dense connected group.
Debug and Release `seam_native_ui`, `seam_standalone_authoring`, and `seam_clap_editor` target
builds pass, and `git diff --check` passes. No tests were added or run. This improves the bounded
editor layout path but does not close all U23/U24 interaction, overflow, accessibility, or Beta GO
requirements.

2026-09-24 — U23/U24 compact lyric-label overflow increment, local only; GitHub CI remains deferred.
Compact piano-roll labels now reserve display width for an ellipsis when a lyric does not fit instead
of silently cutting the text. The complete lyric remains in `EditorLabel::fullText`, the focused-note
detail and accessibility node; only the short visual label is shortened. Debug/Release native UI,
standalone-authoring and CLAP editor targets compile, and `git diff --check` passes. No tests were added
or run. This improves overflow signaling but does not close the full lyric-editing, note-layout or
Beta GO requirements.

2026-09-24 — U23 dense-overlap note interaction increment, local only; GitHub CI remains deferred.
The piano-roll layout already paints up to three stable bands for same-pitch overlapping notes and
offers an overlap detail/cycle path for the full group. Direct hit-testing and box-selection now ignore
members omitted by that density cap, so clicking or marquee-selecting a visible note cannot target an
unpainted note that reused the same geometry. The detail/cycle path continues to expose hidden group
members. Debug/Release editor, native UI, standalone-authoring and CLAP editor targets compile;
`git diff --check` passes. No tests were added or run. This fixes interaction targeting, not every
visual overlap/long-lyric presentation issue, and does not close U23/U24/Beta GO.

2026-09-24 — U22 Voice Designer accessibility-description increment, local only; GitHub CI remains deferred.
The native Voice Designer now gives each accessible control an acoustic explanation rather than the
generic “validated draft voice parameter” text. Descriptions distinguish pulse shape, spectral tilt,
aspiration, modulation, oral formant frequency/bandwidth/gain, nasal resonance, frication and plosive
source controls, while clarifying that audition-pitch selection does not alter the authored song.
Descriptions also expose the actual accepted numeric bounds and keyboard increments, including the
strictly ordered formant constraint and the coupled frication/plosive center-to-bandwidth limit; the
values follow `VoiceRecipe::validate` and the editor's own adjustment steps. The standalone Voicebank
Studio target compiles in Debug and Release, and `git diff --check` passes. No tests were added or run.
This improves discoverability and accessible authoring but does not qualify the source-filter voice
acoustically or close U22/U39/Beta GO.

2026-09-24 — U41 mouth-to-face, active-style and score-range presentation increment, local only; GitHub CI remains deferred.
Schema-2 character packages may declare a bounded normalized `mouthPlacement`. For such
packages, the loader keys a uniform mouth-sprite corner background to transparency and
the native dock composites the current phoneme mouth shape at the declared face rectangle
instead of beside the energy bar. Raster image drawing now respects source alpha. Existing
schema-2 packages without placement retain their legacy side indicator. The published render
identity now carries the selected region's score pitch range through the transient character
performance read model; the dock identifies acoustic style and score range separately from the
character art's visual style. Those fields also travel through the controller-owned read model used
to build the accessibility tree, keeping the screen-reader value in sync on standalone and CLAP
surfaces. This describes notes in the
rendered score, not a qualified vocal range. The voice identity panel names a selected
procedural/neural resource by its actual ID/version/digest prefix and says `Selected` until that
route has a current, non-stale render at the requested revision and quality, instead of
mislabeling it as a missing sample bank or treating stale audio as confirmation. Character 01's
development manifest declares an artwork-specific placement. Debug/Release native targets compile; no
tests are added or run for this increment. This does not make
the current concept production art or close U41; package identity, authorized final assets,
installed visual/accessibility review, and singing-quality qualification remain open.

The character resource association now also supports schema 3's exact singer kind/ID/version/digest
binding. Rendered performance identity and the transient character binding carry resource kind through
standalone and CLAP, so a procedural or neural singer can animate only a character package explicitly
bound to that exact resource; schema 1/2 remain sample-bank-only. See
`docs/formats/CHARACTER_PACKAGE_V3.md`. This enables the code path but does not supply a qualified
production character or singer resource.

2026-09-24 — U39/U40 timbral-control accessibility increment, local only; GitHub CI remains
deferred. The selected-track accessibility tree now exposes a summary of all six singer-control
capabilities, even when the compact visual inspector shows only a subset; it also exposes per-row
current values/curve counts and exact renderer refusals for rows visible there. Capability decisions
come from the host's resolved singer route when available, preserving renderer-specific cases such as
sample-bank formant support instead of reporting only a carrier-wide approximation. This closes a
native screen-reader information gap without claiming unsupported controls are implemented. No tests
were added or run for this increment.

2026-09-24 — U41 development-art direction increment, local only; GitHub CI remains deferred.
Added a new Character 01 singing-state concept portrait to
`assets/character-01/production-development/`. It is explicitly not wired into the
runtime and does not replace existing mouth/portrait assets. Its intended use is to
guide a more legible performance-avatar treatment; the image digest and prompt summary
are recorded beside it. U41 remains incomplete: first-party final artwork, provenance
and artist agreements, complete state assets, installed-platform behavior, and review
are still required. No tests or builds were run for this art-only increment.

2026-09-24 — U37 neural inference-setting fidelity increment, local only.
The admitted render snapshot's `inferenceSteps` now reaches the first-party
worker's ONNX acoustic `steps` input instead of being ignored in favor of a
hard-coded 10. The changed helper CLI is protocol 3, and protocol-3 launches
fail closed on a missing or out-of-range setting; signed v2 deployments are
rejected by current selection instead of launching an incompatible worker.
The response backend identity names the executed step count. Candidate
qualification accepts an explicit 1..1000 setting, records it in the dossier,
and preserves older schema-1 captures by normalizing an absent setting to 10.
Its response-binding criterion now also checks the returned model, bundle and
executed-step identities against the captured request/setting.
Arithmetic-graph checks compare 10- and 20-step PCM, and the normal authoring
render path compares distinct admitted snapshots at those settings. Current
payload staging defaults to protocol 3, while v1/v2 manifests remain parseable
as historical contracts. This is execution fidelity over deterministic fixture
graphs, not learned-singer quality
or U37/Beta GO acceptance. GitHub CI remains deferred.
The local Release build completed, and the full 185-test Release CTest suite
passed in 441.96 seconds after protocol-3 packaging was included; four focused
Debug neural tests and the production worker/package checks also passed. These
are checkout tests, not clean installed-surface, model-quality or listener
acceptance evidence.

2026-09-24 — U35 vocoder recovery continuity increment. Schema-2 partial
checkpoints for `excitationNoiseId` now retain raw-draw and realized-noise
segment-chain digests bound to the admitted recovery plan. A resumed run checks
the noise identity, restores model/optimizer/RNG state, and continues the two
chains rather than recording only the suffix. New complete noise epochs name
the chain algorithm and use schema 2; export distinguishes them from existing
schema-1 concatenated-digest receipts and rejects a mismatched or malformed
noise identity. A real small Torch GAN fixture interrupted after two segments
resumed with identical complete epoch identity and model weights to an
uninterrupted run. Forty-three focused recovery, epoch, checkpoint, command and
export tests pass in the local DiffSinger model environment. The admission in
this regression is mocked: it does not establish source rights, an authorized
pilot corpus, a qualified singer, or U35/U36/Beta GO completion. GitHub CI was
not changed.

2026-09-24 — U30 folded-scalar interoperability increment, local only;
GitHub CI remains deferred. A sixth 0.9 file emitted by the historical
OpenUtau serializer contains a folded multiline comment with YAML
metacharacters. The native reader now accepts literal/folded block scalars
with bounded content bytes and physical lines, explicit indentation and
chomping; block content is not interpreted as YAML aliases/tags. The file
passes historical/current OpenUtau load, SEAM import/export, current
OpenUtau re-load and the 18-point pitch comparison (`0.160039` cents maximum).
The nonempty project comment is explicitly reported as a conversion loss;
codec tests also cover part-comment loss, multiline lyrics and
malformed/over-budget bodies. SEAM's own 0.9 writer now emits an empty comment
so a self round trip does not invent a loss; the changed 80-note production
export hash was rechecked with pinned OpenUtau `Ustx.Load`. U30
remains PARTIAL: real GUI open/save, diverse user-authored files, large-curve
contract and independent receipt reproduction remain. No Beta GO claim.

2026-09-24 — U30 historical-serializer interoperability increment, local only;
GitHub CI remains deferred by user request. Building the optional OpenUtau
oracle separately against 0.6, 0.7, 0.8 and 0.9 source tags produced four
byte-pinned baseline USTX fixtures and a fifth 0.9 curve-bearing fixture.
Real OpenUtau YAML uses indentless block sequences;
the native bounded reader previously rejected them as trailing content and
now accepts them through the existing depth/node/collection limits. All four
import through SEAM and re-export as 0.9. Their own historical loaders and the
pinned current loader accept the inputs; the pinned loader accepts all four
SEAM round trips. Its independent pitch sampler measured at most 0.160040
cents difference across 18 fixed in-note points per pair. Nonzero per-note
tuning remains audible through pitch automation, but its separate edit control
is flattened; that control loss is now named in the conversion report. The
curve-bearing file imports with an explicit curve loss and bounded-resource
regression; a read-only peer found no parser regression and highlighted this
previously untested breadth axis. Fractional `tuning` now rejects to match
OpenUtau's integer model. A separate historical-serializer probe produced a
folded `>-` multiline comment that SEAM still refuses; this joins large-curve
policy, actual desktop GUI and broader field corpus as U30 acceptance work.
Focused Release and Debug USTX/service tests pass. U30 stays PARTIAL; no
singer/Beta GO claim. Details and
hashes: `U30_ACCEPTANCE_AUDIT_2026-09-23.md` and
`tests/fixtures/ustx/README.md`.

2026-09-23 — U30 compatibility and independent-load audit, with GitHub CI deferred
at the user's request. Native USTX now imports the declared 0.6–0.9 musical
subset and continues to export 0.9. The historical OpenUtau 0.6 source check
corrected the compatibility regression to use its five-selector default.
Strengthened the optional external oracle from YAML-only deserialization to
OpenUtau's `Ustx.Load` migration/`AfterLoad`/validation path. A production SEAM
export with 80 notes and UTF-8 lyrics loaded successfully through that path;
the focused Release USTX test passes. U30 is **PARTIAL**, not accepted: actual
archived 0.6/0.7/0.8 files and desktop GUI workflow evidence are absent. See
`U30_ACCEPTANCE_AUDIT_2026-09-23.md` for exact provenance, receipts and gaps.

2026-09-19 — Persistent vocoder training batch against `cddf4f9f`. Added production
`train_vocoder` CLI and multi-epoch orchestration over existing admission/batch/GAN
services, with complete-state resume, per-epoch retained audio and exact cumulative
binary-checkpoint write limits. Actual pinned upstream GAN command ran two epochs
on explicitly synthetic oscillator fixture-policy data; a separate-process resume
reproduced all model/optimizer/scheduler/RNG state and held-out WAV bytes exactly.
Retained three GAN checkpoints total 1,660,394,196 bytes. Actual resumed checkpoint
exports to 168,336-byte ONNX with Torch parity maximum error 3.3527613e-8.
Fixture reconstruction still fails (spectral 3.970544, pitch 979.128c); no usable
learned singer, real-source approval, native deployment or Beta GO is claimed.

In parallel, fixed imported continuation double glide: successful composed USTX
pitch persists existing Pitch/Replace ownership; compiler revision 16 suppresses
only the duplicate automatic glide. Regression corrects 5800c to 6200c and preserves
ordinary native glide/phonetics. Save/reopen, four rates and ownership boundaries
pass; nonlinear/polyphonic/export-loss limits remain explicit. Complete Release
build and CTest 172/172 pass (104.54s); optional Python tests 128/128 pass (38.436s).
Source closure passes; parked coordinator unchanged. Report section 15 and training
README contain executable commands, exact artifact hashes and remaining work.

2026-09-19 — Follow-up against `ba6dbfa0`: two implementation agents plus integrator
repaired OpenUtau linear cross-note pitch composition/snap, a production last-frame
manual-pitch ownership bug (5800 vs 6199.270833 cents), float32 renderer-WAV intake,
captured note/phone/melisma label ownership, exact native-pitch source binding,
real Torch vocoder evaluation, and admitted held-out source selection. Compiler
revision 15 invalidates stale rendered caches. Unsupported nonlinear/polyphonic
USTX and automatic-glide interactions remain explicit limits.

Actual retained 16s SEAM render now passes acoustic-target extraction and ordinary
label-config/conditioning paths: 3000 hops, 50 phones, 30 notes, 29 syllables, 1 slur. Actual
pinned SingingVocoders forward on a PJS crop passes tensor/length checks and
correctly fails untrained reconstruction (spectral 51.173231, pitch 832.909c).
Stronger Whisper-small breaches all 3 negative controls; it is not adopted as an
approval oracle. PJS samples are retained locally for evaluation only, not training.

Complete Release build passes. Full CTest 171/172 in 85.58s; only source closure failed
because the new diagnostic was not yet staged. After staging, closure and its CTest
rerun pass (0.25s). Optional neural-environment Python discovery 115/115 passes.
No test weakening, coordinator repair, learned-singer qualification or new roadmap
unit acceptance. Second-developer review agrees with the three-lane next order:
persistent learned-candidate training/deployment, music-preserving interchange,
and calibrated diagnostics plus installed-song journeys. See report sections 13–14
for commands/artifact hashes, demonstrated limits and code-level exit criteria.

2026-09-19 — Three-agent production-path repair and real audio evaluation, integrated
against `7839c72f`. English pronunciation hints now accept the already-supported
`ao` inventory with stress variants; Japanese/English/Korean regressions use pinned
OpenUtau examples with explicit inventory mapping and MIT attribution. USTX import
and export now use cents and cycle percentages for vibrato, retain signed pitch
pickups in rests, and report unsupported nonlinear/cross-note behavior instead of
claiming fidelity. CLAP character artwork follows published rendered cues and host
transport; integration also repaired missing performance identity when the renderer
selects an implicit sole bank style. A mouth-pointer mutation fails the exact pixel
regression, and restoring the implementation passes.

The former ASR routine never read audio and hard-coded successful negative controls.
Replaced it with optional local faster-whisper inference, checked WAV/model identities,
executed signal controls, unprompted transcripts, and optional post-inference text
comparison. Actual inference on the unchanged 66-WAV retained packet produced 31
empty transcripts and zero exact normalized lyric matches for its six complete-song
outputs; all three negative controls produced no text. The same backend recognized
an independent local Japanese speech control with normalized CER 0.0. These are
diagnostics, not calibrated singing-quality or release evidence; historical schema-1
reports remain retained but invalid as recognition evidence.

Full Release build passes. First complete CTest run: 171/172 in 127.94 seconds, with
an unchanged native ONNX owned-bytes test timing out at 20.02 seconds. That test then
passed five consecutive isolated runs; a second complete run on unchanged source
passes 172/172 in 91.01 seconds. No test or timeout was weakened; the first timeout's
cause remains unestablished. Source closure and diff checks pass. The parked
coordinator race was not changed. The second-developer task reviewed the corrected
English report and work sequencing read-only; it did not independently rerun these
experiments. See `SEAM_AUTOMATED_VERIFICATION_AND_ACCELERATION_2026-09-19.md` for
artifact hashes, findings, four concrete next batches, and automation boundaries.
No roadmap percentage or Beta GO acceptance is increased by this maintenance batch.

Render-throughput root cause of the only red CI job: the Ubuntu `native-platform-matrix` job was failing `seam_original_singer_song_journey_tests` on its 180s CTest timeout. Three logs from that same job show it is throughput, not a stall: run `35375628884` timed out at 180.06s, while run `35362687374` passed in 135.31s and run `35321985263` passed in 156.83s. A `sample(1)` profile of the Debug binary attributes ~98% of worker time to `ArticulatedStream::renderOwned` -> `PhonationSource::render` -> `CompiledScorePerformance::at/evaluate` and `applyCompiledPerformanceGain`, all of which run once per output frame. Two behaviour-preserving repairs landed in `3276b7df`: `CompiledScorePerformance::hasManualPerformance_` lets `evaluate()` skip the 12 accepted-channel and 24 ownership bucket searches when the region carries no manual performance record, and `PhonationSource` substitutes the exact constant taper window `2.0` for `cos(pi * 0.0)`, which is bit-identical because `0.5 * (1.0 + cos(0.0))` is exactly `1.0` with both operands exact powers of two. `tests/test_framework.hpp` now prints and flushes a `[START]` line and an elapsed `[PASS]` line per case, so a CTest timeout names its slow case instead of leaving no evidence. Measured on Debug with all four cases passing: 94.71s -> 62.37s. The full local Release suite passes 172/172 under `-j8` in 117.70s with the song journey at 13.27s. No test was weakened or removed; `SOURCE_CLOSURE=PASS`. Independent review by the second-developer session confirmed both changes as bit-exact and safe against the only construction path.

2026-09-12 — Direct inspection of the planned DiffSinger backend identified the
sample-frame versus acoustic-hop mismatch and its reserved padding token. Added
verified request-to-acoustic-input conversion with cumulative duration rounding,
phone-owned F0 sampling, explicit padding trim dimensions, and rejection of erased
phones or token 0. Upstream interface revision/archive digest recorded as a
development reference. Release build/neural/core suites pass 2/2 (23.16 s).
Actual native inference/model production remain missing; no unit acceptance.
See `docs/formats/DIFFSINGER_ACOUSTIC_INPUTS_V1.md`.

2026-09-12 — Added `VerifiedNeuralDeployment`, binding exact descriptor bytes via
the existing Ed25519 verifier to a separately supplied release key and expected
build/platform/surface. Private verified fields feed native manifest loading.
Tests reject signer/byte/target/schema substitution; copied-module loading now
passes through signature verification with ephemeral fixture keys. Release build
and neural/packaging/core suites pass 3/3 (22.62 s); final affected suites pass
2/2 (3.00 s). Production key policy, signed materialization, surface wiring and
actual inference remain required for U37/Beta GO. See
`docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-12 — Owner-requested publication completed to origin/master at
`5e03d65e0f5e16f5f7f73eb3e31a9c5d48bac5f8`; remote hash was verified. Continued
with the recurring Japanese-reading lifecycle regression: replaced the nominal
one-second polling loop and unbounded terminal loops with a monotonic 15-second
completion deadline, accounting for the helper's 10-second wall deadline plus
resource checks. Worker failures remain failures with their diagnostic; Ready,
cancellation, stale-result rejection and absent-helper outcomes remain asserted.
Final Release rebuild and parallel Japanese-pronunciation/core suites pass 2/2
(23.82 s). This repairs verification reliability, not a new roadmap unit; the
full-scope implementation goal remains active.

2026-09-12 — Publication verification: accumulated implementation committed as
`fdd197005fad6b6b0fb33396854b4439be7ab18c`. Release build passed; the complete
121-target CTest run passed 119 targets in 143.97 s. Source closure identified ten
historical `test-details.log` evidence files excluded by the owner's global
`test-*.log` ignore rule; these exact files were explicitly staged and closure
then passed. The remaining failure was the Japanese-reading job test's short
Ready wait, which passed on an unchanged-source focused rerun. Both failed
targets passed on that rerun (1.99 s); the intermittent wait remains to repair.
License audit passed in an exact-commit temporary clone containing only master,
as required by the audit, without removing the owner's existing branches.
This is development publication evidence, not full Beta GO acceptance.

2026-09-12 — Resumed after interrupted sessions and recovered the completed native
manifest-loader verification from the CTest log. Address-derived bounded loading
now joins trusted expected metadata to package verification. A copied native
module fixture succeeds from an unrelated directory with PATH unavailable and
rejects substituted descriptor/manifest/dependency input. Neural, packaging and
core suites passed (11, 28 and 867 cases respectively). Publication requested by
the owner includes the accumulated implementation and its required new files;
full Beta GO remains unfinished. See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Release assembly/verifier now seal and recompute a per-surface neural
package inventory, explicitly distinguishing missing packages from verified file
metadata. Present manifests must match actual files, module paths and build ID;
forged status and stale helper bytes fail. All 138 phase13a tests pass (8.201 s)
with native probe enabled; Release regeneration/build and packaging CTest pass
(1.98 s). Existing development payloads remain release-ineligible. Native trust
delivery, qualified inference and installed-surface acceptance remain unfinished;
see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO stays open.

2026-09-10 — Payload-side neural manifest builder generates deterministic native-
compatible metadata from bounded real-file hashing. Six Python packaging tests
pass, including generated-byte acceptance and substituted-byte rejection through
the C++ probe. Registered CTest packaging check passes (0.09 s); neural/core
suites pass 2/2 (21.30 s). Release-sealing integration, trusted digest delivery,
signed-byte ordering and dependency closure remain open. No U37/Beta GO acceptance;
see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Address-based loaded-module discovery now feeds neural package
resolution. The real-binary fixture resolves and hashes its test module and
sibling probe, then executes the resolved helper. Release build and neural/core
suites pass 2/2 (22.90 s); all 11 neural cases also pass from `/private/tmp` with
`PATH=/nonexistent`. Windows and installed plugin surfaces remain unqualified;
trusted materialization and real inference are still required for U37/Beta GO.
See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Neural helper package manifest decoder now binds exact serialized
bytes to a supplied deployment digest and enforces strict schema/path/size limits.
The real-filesystem resolver fixture now consumes decoded metadata and covers
malformed and substituted inputs. Release build/neural-protocol/core suites pass
2/2 (22.69 s). Trusted digest delivery, loaded-module discovery, packaging and
native runtime integration remain incomplete; no U37/Beta GO completion claimed.
See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Typed neural package resolver validates a supplied SEAM module anchor,
matching build/protocol, contained helper/dependency paths and exact file digests.
Filesystem tests reject redirected, missing, duplicate, changed and incompatible
entries; Release build/neural-protocol/core suites pass 2/2 (21.98 s). Actual
module discovery, trusted manifest decoding, binary dependency closure, loader
control and installed runtime wiring remain required. This advances U37 without
claiming unit completion; see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Neural helper identity preflight now requires an expected canonical
executable digest and a positive size ceiling capped at 256 MiB; mismatches are
rejected before launch. Tests retain valid digest setup for response-binding
failures and add digest/size/cancellation rejection checks. Release build and
neural-protocol/core suites pass 2/2 (21.75 s). This advances U37 but does not
complete trusted package discovery, dependency closure, race-resistant launch,
actual inference or installed qualification. See
`docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO remains open.

2026-09-10 — Recovery-first continuation: all 2,078 paths in checkpoint
`session-preservation-dDpBP8` remain present; regular-file hashes differ only for
the pending neural request builder and its regression test. This comparison
cannot establish whether work was lost before that checkpoint. No restoration,
deletion, staging, commit or push was performed. Owning-note boundary conditioning
now preserves explicitly extended phones' pitch/dynamics with a two-second
limit. Final neural-protocol/core rerun passes 2/2 (21.28 s), following a disk-full
invalid run and an intermittent Japanese-reading Ready assertion failure.
See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Actual neural inference and
full Beta GO remain unfinished; no additional whole roadmap unit is accepted.

2026-09-10 — Neural syllable-order guard rejects onsets after and codas before
their associated nucleus even without span overlap. Malformed-anchor regressions
and a valid compiled coda pass; Release build/neural-protocol/core suites pass
(42.11 s). This is conditioning correctness, not neural inference or Beta GO
qualification. See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.

2026-09-10 — Complete neural score request builder now combines compiled timing,
verified vocabulary, phonetic voicing, F0 and dynamics/articulation gain with
bounded allocation and cancellation. Tests check silence/unvoiced/voiced features,
wire round-trip and score-derived probe subprocess exchange. Final Release build
and neural-protocol/core suites pass (22.15 s). Actual model/vocoder inference
and qualified singer delivery remain unfinished; see
`docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO stays open.

2026-09-10 — Neural score phonetic adapter maps compiled timing and phone symbols
through verified vocabulary IDs, with explicitly selected silence and strict
ownership/coverage checks. Real timing-fixture tests reject unknown/unresolved,
overlapping and clipped input. Release build/neural-protocol/core suites pass
(24.17 s). Full feature-request construction and real model inference remain
unfinished; see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Beta GO stays open.

2026-09-10 — Verified neural vocabulary: bounded immutable token lookup preserves
explicit IDs and validates exact bytes against the model hash. Conditioned helper
runs require that verified vocabulary and exact token count. Tests cover malformed
tokens, byte substitution, missing vocabulary, false size and successful bound
subprocess exchange. Release build/neural-protocol/core suites pass (22.25 s).
No model weights/inference qualification claimed; see
`docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO remains open.

2026-09-10 — Neural conditioned-response binding: response v2 carries the digest
of the complete canonical request, and the runner rejects missing/mismatched
bindings while retaining ID/model/shape checks. Real probe subprocess tests cover
correct, wrong and missing digests; codec tests cover v2 and invalid hashes.
Release build/neural-protocol/core suites pass (21.97 s). Correlation is not real
inference; score adapter and qualified model delivery remain open. See
`docs/formats/NEURAL_PHONETIC_CONDITIONING.md`. Full Beta GO remains unfinished.

2026-09-10 — Neural request metadata v2 now transports vocabulary identity and
bounded timed phoneme IDs alongside F0/dynamics, with strict field/version checks
and model vocabulary binding. Final Release build/neural-protocol/core suites
pass (24.17 s), including a real probe subprocess exchange. Probe PCM is not
neural singing. Complete conditioned-response binding, score adapter and actual
qualified inference remain open; see `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`.
Full Beta GO remains unfinished.

2026-09-10 — Full-scope reprioritization identified missing neural phonetic input:
v1 IPC has F0/dynamics but only a pronunciation hash. Added bounded explicit token
spans and model vocabulary-hash/budget validation as groundwork for a versioned
request. Release build and neural-protocol/core suites pass (22.73 s). Wire
integration, score adapter and actual qualified neural inference remain unfinished.
See `docs/formats/NEURAL_PHONETIC_CONDITIONING.md`; no U37 or Beta GO acceptance.

2026-09-10 — Voiced source removal lifecycle: required resonance-pose removal now
reports the explicit dependency without mutation. Tests verify source removal
preserves resonance poses, invalidates preview, and Undo restores identity and
rerendered PCM. Release build/Designer/core suites pass (22.87 s). Live removal
verification and full Beta GO remain open; see `docs/formats/VOICE_RECIPE_V5.md`.

2026-09-10 — Mixed voiced-source replay matrix: 48 rate/pitch/placement/gain
combinations verify exact whole/chunk/seek PCM, finite output and source-rate
rejection without silent retuning. Release build and voice-design/core suites
pass (23.60 s). This is local numerical coverage, not pitch-range or listening
qualification. See `docs/formats/VOICE_RECIPE_V5.md`; full Beta GO remains open.

2026-09-10 — Designer preview availability: voiced frication disables isolated
noise preview; CV/VC require selected vowel context, with explanatory semantic
labels. Shared availability tests cover voicing, pose/style mismatch and bounds.
Release build and Designer/core suites pass (28.36 s). Live disabled-action
verification remains open; see `docs/formats/VOICE_RECIPE_V5.md`. Full Beta GO
remains unfinished.

2026-09-10 — Full local post-voiced-frication regression: 119/120 tests pass
(133.37 s, -j 2). Only source closure fails: 523 existing unindexed required
inputs, zero missing-input records. All 2,075 checkpoint files remain present
with matching regular-file hashes. No gate bypass or Git publication performed.
See `VOICED_FRICATION_FULL_REGRESSION_2026-09-10.md`. This is engineering evidence,
not a product-completion percentage or full Beta GO qualification.

2026-09-10 — Voiced-frication coda round trip: English `aa1 z` now has end-to-end
regression coverage through normal rendering, exact WAV export/reload, typed
candidate-v5 markers, producer import/recovery and asynchronous Studio opening.
Take remains MarkerReview with no review. Release build/export/core suites pass
(24.48 s); bake/logs retained in `evidence/voiced-frication-coda-v5-2026-09-10/`.
This closes a coda verification gap, not listening or full Beta GO acceptance.

2026-09-10 — Live voiced-source authoring: rebuilt 720x520 Designer created an
explicit z resonance/source, accepted gain 0.3 and rendered CV. Gain 1.1 rejected
without mutation; zero invalidated preview; Undo restored 0.3; VC then rendered.
Numeric accessibility/creation/undo verified, not keyboard/drag/save/reopen or
listening acceptance. See `docs/formats/VOICE_RECIPE_V5.md`. No saved user data
changed. Full Beta GO remains open.

2026-09-10 — Native frication voicing gain: fourth source-control row routes numeric,
keyboard and drag edits through validated draft history; zero removes voicing.
Matching resonance remains explicit. Source/stop/nasal row offsets are updated.
Release build and Designer/core tests pass (22.80 s), including validation,
undo/redo/save/reopen and exact legacy identity restoration. Live native edit
qualification remains open; see `docs/formats/VOICE_RECIPE_V5.md`. Full Beta GO
and singer-quality acceptance remain unfinished.

2026-09-10 — Normal voiced-frication render/bake/import: recipe v5 is admitted by
default; export selects candidate v5 for voiced-frication markers. English `z aa1`
renders through the normal snapshot pipeline, reloads with exact Float32 PCM,
imports/reopens as MarkerReview with zero reviews and preserves typed lineage.
Designer CV/VC uses authored voicing; isolated noise rejects voiced bindings.
Final Release build and five suites pass (29.32 s), after correcting the expanded
fixture's final expected marker kind. WAV/metadata/test logs are retained under
`evidence/voiced-frication-v5-2026-09-10/`. See `docs/formats/PROCEDURAL_CANDIDATE_V5.md`.
Native voicing controls, listener qualification and full Beta GO remain unfinished.

2026-09-10 — Candidate-v5 loader: strict metadata parsing preserves voiced-frication
identity, requires explicit recipe voicing/resonance, rejects unvoiced relabeling
and requires appropriate mixed-renderer revisions. Vowel/nasal identity ambiguity
is rejected in recipe/planner validation too. Release build/voice-design/export/core
suites pass (31.81 s). Tests are metadata fixtures; writer, normal runtime admission
and real producer import remain unfinished. See `docs/formats/PROCEDURAL_CANDIDATE_V5.md`.
Full Beta GO remains open.

2026-09-10 — Voiced-frication marker transport: marker kinds share the planner's
type and projection preserves it directly, eliminating a new-kind-to-vowel
fallback. Scheduler completion/cache-hit tests preserve voiced-frication identity
and reject unknown kinds. Legacy candidate export explicitly refuses the new
kind pending a versioned format. Release build/snapshot/export/core suites pass
(27.40 s). See `docs/formats/VOICE_RECIPE_V5.md`; normal v5 admission and full Beta
GO remain unfinished.

2026-09-10 — Recipe-based voiced-frication development path: explicit opt-in now
propagates through recipe compilation and stream creation. Tests verify onset
equivalence, coda rendering/cropped replay, style/cancellation rejection and noise
seed isolation from preceding vowel PCM. Release build and four suites pass
(28.78 s). Default v5 admission and candidate integration remain closed/pending;
see `docs/formats/VOICE_RECIPE_V5.md`. Full Beta GO remains open.

2026-09-10 — Development mixed voiced-frication stream: ArticulatedStream revision
8 validates both frozen bindings, mixes authored voiced gain with noise, and
smooths contiguous gain transitions. Direct opt-in tests verify gain-dependent
PCM, stale-plan rejection, exact seek/checkpoint/block-size replay and cancellation.
Release build and four focused suites pass (28.95 s). Default v5 admission stays
closed pending candidate/marker and normal runtime integration; no singer-quality
acceptance is claimed. See `docs/formats/VOICE_RECIPE_V5.md`. Full Beta GO stays open.

2026-09-10 — Voiced-frication timing/noise stage: articulation revision 8 carries
explicit voiced-frication gain and rejects voicing mismatch; noise stream revision
3 includes mixed gestures instead of skipping all voiced gestures. Exact noise
PCM and seek comparisons pass. Final Release build and four focused suites pass
(29.73 s). Mixed voiced output, candidate identity and v5 runtime admission remain
unfinished. See `docs/formats/VOICE_RECIPE_V5.md`; full Beta GO remains open.

2026-09-10 — Voiced-frication recipe groundwork: schema 5 introduces explicit
voicingGain in (0,1] and requires same-phone/style resonance; unvoiced rows use
null and legacy recipes retain canonical encoding. Codec/validation and frozen
identity support are implemented; rendering-resource decode intentionally still
rejects v5 until voiced/noise DSP and lineage integration exist. Release build
and Designer/voice-design/export/core suites pass (29.34 s). See
`docs/formats/VOICE_RECIPE_V5.md`. This is contract groundwork, not audible voiced
frication, a completed U20, or Beta GO acceptance.

2026-09-10 — Articulation capability diagnostics now name the phone/note and
distinguish missing style bindings from unsupported voiced consonants. Regression
proves that binding noise as `z` cannot render a voiced `z` token. Release build
and voice-design/core suites pass (23.31 s). No new consonant synthesis or quality
acceptance is claimed; see `DESIGNER_FRICATION_CONTEXT_2026-09-09.md`. Full Beta GO
remains open, including voiced/noise articulation and reviewed singer resources.

2026-09-10 — Late inspection cancellation: a stop arriving after worker completion
but before UI adoption now discards the read-only score snapshot. Already-published
job preparation still reports success. Tests observe completed futures before
cancelling and verify both outcomes; final Release build/export/core suites pass
(26.11 s). See `DESIGNER_PREPARATION_DIALOG_RETENTION_2026-09-09.md`. Full Beta GO
and live worker-cancel verification remain open.

2026-09-10 — Visible producer cancellation: during active work the batch slot
becomes Cancel work, shared by painting, pointer and semantic controls; other
generation actions remain disabled. It delegates to the existing stop request,
not rollback or synthetic completion. Release build/export/core suites pass
(25.14 s). Live worker-cancel activation remains unqualified; see
`PRODUCER_GENERATION_BUTTONS_2026-09-10.md`. Full Beta GO remains open.

2026-09-10 — Generation accessibility: the inventory producer exposes generation
buttons/status with context-bound IDs and shared guarded mouse/AX dispatch.
Modal re-entry and unavailable actions are rejected. Release build/export/core
tests pass (24.02 s); live AX Prepare activation/cancel restores the controls.
Other producer semantic coverage and end-to-end generation remain unfinished.
See `PRODUCER_GENERATION_BUTTONS_2026-09-10.md`; full Beta GO remains open.

2026-09-10 — Live producer-button follow-up: at 720x520, all four generation
buttons opened their expected native chooser and returned on cancel. Labels fit
the intake panel without marker overlap in this fixture; no inputs were selected
and durable generation remained 2. Dedicated accessibility and end-to-end job
execution through these controls remain unqualified. Evidence boundary is recorded
in `PRODUCER_GENERATION_BUTTONS_2026-09-10.md`; full Beta GO remains open.

2026-09-10 — Producer generation buttons: inventory intake now paints Prepare,
Run job, Make batch and Run batch with shared geometry/enabled-state pointer
routing to existing guarded operations. Recording and active work disable them.
Release build and export/core suites pass (36.47 s). Live mouse/visual and dedicated
semantic accessibility remain unfinished. See `PRODUCER_GENERATION_BUTTONS_2026-09-10.md`.
This is partial U22 implementation; full Beta GO remains open.

2026-09-09 — Retained preparation context now invalidates on actual assignment
changes, including editable-row adoption; invalid/same-row selection preserves
it. A two-assignment A -> B -> A regression proves the snapshot cannot revive.
Release build and export/core/sample-review tests pass (33.25 s). Native row
interaction remains unqualified. See the assignment-switch follow-up in
`DESIGNER_PREPARATION_DIALOG_RETENTION_2026-09-09.md`; full Beta GO remains open.

2026-09-09 — Preparation dialog retention: native region/destination dialogs now
inspect rather than consume the frozen Designer score/recipe selection. Cancel
retains it; successful worker dispatch consumes it; obsolete assignment context
is rejected and cleared. Final Release build and Designer/export/core tests pass
(36.31 s). Live modal cancellation remains unqualified. See
`DESIGNER_PREPARATION_DIALOG_RETENTION_2026-09-09.md`. U21/U22 and Beta GO stay open.

2026-09-09 — Current full local regression: 119/120 Release CTest entries pass
(129.21 s, -j 2). Only tracked-source closure fails: 505 existing unindexed
required inputs, no missing-input records. No gate bypass or staging occurred.
Live rebuilt Studio confirms automatic scrolling/selection after noise and stop
creation; immediate keyboard focus remains unqualified after a tool key error.
See `DESIGNER_POST_CONTEXT_REGRESSION_2026-09-09.md`. This is engineering evidence,
not a product-completion percentage or Beta GO acceptance.

2026-09-09 — Source creation selection: successful Add noise/Add stop now selects
the new source's style-filtered parameter row; compatible selected vowels and
pitch are retained. Mixed-style index resolution and rejection behavior have
regression coverage. Live auto-scroll/focus remains unqualified. See
`DESIGNER_SOURCE_CREATION_SELECTION_2026-09-09.md`; U22 and Beta GO remain open.

2026-09-09 — Frication context previews: Designer now renders selected source/vowel
CV and VC pairs through ArticulatedStream, with mode-bound asynchronous state,
visible buttons and truthful labels. Release build and Designer/core suites pass
(22.71 s); live minimum-size VC rendering/playback activation and CV rendering
pass. Fixed 150 ms frication timing is a preview policy, not phonetic acceptance.
See `DESIGNER_FRICATION_CONTEXT_2026-09-09.md`. U20/U22 and full Beta GO remain open.

2026-09-09 — Compact Designer source workflow: source and generation-preparation
bars are visible at the supported 720x520 minimum, with six readable parameter
rows and shared paint/semantic/pointer geometry. Release build and Designer/core
suites passed (22.67 s); live mouse Add stop, navigation, VC/CV/Src render and VC
playback activation passed. Voice quality and full Beta GO remain unqualified.
See `DESIGNER_COMPACT_SOURCE_ACTIONS_2026-09-09.md`. The preceding AppKit completion
snapshot repair passed live pointer rendering and both suites (22.64 s); see
`SESSION_CONTINUITY_AND_AX_COMPLETION_2026-09-09.md`.

2026-09-09 — Accessibility dispatch safety: disabled controls/ancestors reject
non-focus actions, while explicit inspection focus remains supported. Dispatch
owns its callback ID across synchronous tree replacement. Two regressions from
an overly broad focus restriction were diagnosed against the existing semantic
and focus-ring contracts and corrected. Full Release build/Designer/core tests
pass (22.58 s). Visible-button live QA and full Beta GO remain open. See
`ACCESSIBILITY_DISPATCH_SAFETY_2026-09-09.md`. No Git publication occurred.

2026-09-09 — Visible Designer buttons: painted action bars and pointer dispatch
now use the same semantic nodes/bounds, with concise Src/CV/VC labels and disabled
styling. Full Release build/Designer/core tests pass (21.81 s). Live QA could not
create a draft through the UI tool; the still-live process sample showed normal
AppKit event waiting, not an observed renderer hang. New button interaction and
compact source controls remain unqualified. No missing checkpoint files or Git
publication. See `DESIGNER_VISIBLE_BUTTONS_2026-09-09.md`; full Beta GO stays active.

2026-09-09 — Vowel-stop Designer audition: added a typed final-stop preview mode
alongside isolated source and stop-vowel, using the selected vowel/pitch and
production articulation path. Modes retain separate readiness semantics and
explicit labels. Full Release build/four focused suites pass (25.84 s), covering
closure/burst bounds, reproducibility and mode switching. Native controls and
listener quality are not newly qualified. No missing checkpoint files or Git
publication. See `VOWEL_STOP_DESIGNER_AUDITION_2026-09-09.md`; Beta GO stays open.

2026-09-09 — Workspace-entry UI recovery: unsuccessful async opening now returns
to the retained Designer session with the error/cancellation reason, instead of
stranding an empty producer view. Live digest-mismatch rejection preserved an
unsaved 0.17 aspiration edit; immediate keyboard editing still worked. Full
Release build/three focused suites pass (22.90 s). Slow-I/O cancellation latency
and full Beta GO remain open. No missing checkpoint files or Git publication.
See `WORKSPACE_ENTRY_RECOVERY_2026-09-09.md`.

2026-09-09 — Async initial workspace recovery: the Designer handoff prepares
repository state and marker lineage on an isolated worker, then adopts it under
owner-thread epoch/empty-context checks. Busy conflicts, cancellation, retry and
shutdown preserve unadopted state. Multilingual candidate markers/generation
match after async recovery. Full Release build/four focused suites pass (24.74 s).
No fresh UI/large-workspace latency qualification, missing checkpoint files or Git
publication. See `ASYNC_WORKSPACE_RECOVERY_2026-09-09.md`; full Beta GO remains open.

2026-09-09 — Frication codas: configured unvoiced frication sources now render
after vowels with resolved, nonoverlapping coda timing and their sustained noise
envelope. Actual English aa1 s export reloads as candidate v2 with a final typed
marker and exact PCM equality. Full Release build/four focused suites pass
(30.06 s), including stop/frication envelope distinction and replay/rejection
checks. No missing checkpoint files or Git publication. See
`FRICATION_CODAS_2026-09-09.md`; voiced frication and full Beta GO remain open.

2026-09-09 — Released stop codas: explicit p/t/k bindings now admit bounded,
nonoverlapping post-nucleus closure/burst gestures. Normal simple-coda timing
feeds the renderer; an actual English aa1 k bake reloads with typed markers and
exact PCM equality. Full Release build/four focused suites pass (28.10 s), with
silence, burst, replay and rejection checks. Unreleased/language-specific stops
and listener qualification remain open. No missing checkpoint files or Git
publication. See `RELEASED_STOP_CODAS_2026-09-09.md`; full Beta GO remains active.

2026-09-09 — Windows workspace picker source implementation: added a native
folder picker with explicit context fields, cancellation/errors and scoped COM
cleanup. Shared macOS/Windows/Studio validation rejects malformed digests and
bounded-ID violations. Full macOS Release build/three focused suites pass
(23.52 s). Windows code is not compiled or runtime-qualified here; native parity
remains unproven. No missing checkpoint files or Git publication. See
`WINDOWS_WORKSPACE_PICKER_2026-09-09.md`; full Beta GO remains open.

2026-09-09 — Studio navigation focus repair: the AppKit bridge returns keyboard
focus to the canvas when a dispatched action removes a custom accessibility
surface, guarded against active text input/modals. Live accessibility and pointer
workspace handoffs now allow immediate Command-D without a canvas click; repeated
navigation also passed. Full Release build/three focused suites pass (26.11 s).
No capture, producer mutation, missing checkpoint files or Git publication.
See `STUDIO_NAVIGATION_FOCUS_2026-09-09.md`. Full Beta GO remains open.

2026-09-09 — Designer/workspace handoff: source-free Designer can open an
existing producer folder with an explicit inventory digest and registered
PRODUCER identity, preserving its unsaved voice draft. Invalid identity/digest
checks retain current state. Live opening restored the assignment and enabled
job preparation; aspiration 0.12 survived the round trip. Post-modal keyboard
focus still required a canvas click. Full Release build/four focused suites pass
(48.24 s). New-workspace creation, other platforms, async recovery and full Beta
GO remain open. See `DESIGNER_WORKSPACE_HANDOFF_2026-09-09.md`.

2026-09-09 — Live stop/vowel preview verification: native phrase render/play
actions reached the correct ready/playback labels; switching to isolated source
changed modes; a duration edit disabled playback and cleared readiness. Undo
restored saved values without reviving PCM. Normal exit reported unopened input
and no capture; saved recipe bytes were unchanged. This verifies focused native
dispatch, not perceived pronunciation or device-loopback fidelity. See the live
follow-up in `PLOSIVE_VOWEL_AUDITION_2026-09-09.md`. Full Beta GO remains open.

2026-09-09 — Stop/vowel Designer audition: added a context preview through the
production articulation renderer using the selected vowel/style/pitch, separate
from the isolated burst. Mode and selection identities protect async readiness.
Fixed the scratch score's missing lyric ownership through normal validation.
Full Release build and three focused suites pass (33.04 s). Native phrase-action
dispatch and musical qualification remain unverified. No checkpoint files were
missing or Git changes published. See `PLOSIVE_VOWEL_AUDITION_2026-09-09.md`;
full Beta GO remains active.

2026-09-09 — Source-free Designer startup: no-argument Studio launch now offers
visible New/Open controls without requiring a bank or initializing microphone
input. Explicit --designer preserves producer identity requirements and rejects
source-free timed recording. Live entry-button creation and vowel preview passed;
initial exit counters confirmed unopened input and no recording. Full Release
build/core tests pass (20.72 s). No missing checkpoint files or Git publication.
See `SOURCE_FREE_DESIGNER_STARTUP_2026-09-09.md`. Full producer onboarding,
source closure and Beta GO qualification remain open.

2026-09-09 — Post-plosive full regression: 119/120 configured Release tests pass
(136.91 s); only tracked-source closure fails, with 448 existing but unindexed
required inputs and no missing-file records. Kept that gate intact. Subsequently
fixed explicit Studio dimensions being overridden by saved window geometry;
full build/core tests pass (21.68 s), with focused live launch verification.
The full-run evidence predates that follow-up fix. See
`POST_PLOSIVE_FULL_REGRESSION_2026-09-09.md`. No Git publication, musical approval
or whole-unit acceptance occurred; full Beta GO remains open.

2026-09-09 — Designer readability/density: removed the six-row cap, enlarged
body/heading text, added a selection background and unified painted/semantic/hit
row geometry. Live macOS tall/minimum window checks showed sixteen/eight rows;
last-visible-row pointer/keyboard editing matched the intended parameter. Full
Release build and two focused suites pass (22.22 s). This is focused layout QA,
not a full redesign or cross-platform qualification. See `DESIGNER_DENSITY_2026-09-09.md`.
Full Beta GO remains open; no Git publication or whole-unit acceptance occurred.

2026-09-09 — macOS Studio bundle and live plosive QA: converted the macOS target
to its own app bundle, resolving native UI attachment. Live creation, duration
editing, source render/play dispatch, undo/redo, full-width seed dialog and save
passed; saved JSON independently matched. Closed test process exited normally
with no microphone capture. Visual review found undersized text and excessive
unused space, still requiring improvement. Full build/plist validation and two
focused suites pass (21.97 s). This is not Finder-first onboarding, distribution
signing or musical qualification. See
`STUDIO_MACOS_BUNDLE_AND_LIVE_PLOSIVE_QA_2026-09-09.md`. Full Beta GO remains open.

2026-09-09 — Dedicated plosive-source audition: selected stop controls now have
a separate finite closure/burst preview, render/play actions and source-only
status. Async results retain recipe/selection identity; edits and removals
invalidate PCM. Fixed overpainted preview/file status. Full Release build and
four focused suites pass (26.91 s), covering deterministic bounds, cancellation,
state separation and stale completion rejection. Live native interaction and
musical qualification remain open. No missing checkpoint files or Git publication.
See `PLOSIVE_DESIGNER_AUDITION_2026-09-09.md`; full Beta GO remains active.

2026-09-09 — Plosive Designer controls: added source creation/removal, full-width
seed editing and style-filtered spectrum/gain/duration rows, with native shortcut
and semantic-action wiring. Session history rejects stale/invalid edits and
preserves settings through undo/redo and save/reopen. Full Release build and
four focused suites pass (26.57 s). Native UI attachment was unavailable, so
live interaction remains unverified; dedicated stop audition also remains open.
No captured files were missing or Git changes published. Full Beta GO remains
open. See `PLOSIVE_DESIGNER_CONTROLS_2026-09-09.md`.

2026-09-09 — Initial plosive articulation integration: p/t/k recipe bindings now
drive silent closure and finite burst gestures before a voiced vowel. Typed
markers propagate through scheduler/cache and candidate-v4 bake/import/recovery.
Actual ka PCM, multi-rate source preparation, checkpoint/seek/reset/cancellation
and malformed metadata checks pass. Full Release build and four focused suites
pass (26.00 s). No checkpoint files were missing or Git changes published.
Native plosive editing, richer articulation, phonetic qualification and full
Beta GO remain open. See `PLOSIVE_ARTICULATION_2026-09-09.md`.

2026-09-09 — Plosive recipe persistence: schema 4 preserves explicit p/t/k
spectral sources, full-width seeds and nominal burst durations. Validation
rejects ambiguous frication/plosive bindings; history and save/reload preserve
the new data while prior schemas retain their canonical bytes. Full Release
build and five focused suites pass (27.73 s), confirmed from retained test logs
after session recovery. Hash comparison found no missing checkpoint files;
earlier historical loss remains unproven. This is persistence only: articulation,
candidate metadata and native controls remain open. No roadmap unit or Beta GO
acceptance is claimed. See `PLOSIVE_RECIPE_BINDINGS_2026-09-09.md`.

2026-09-09 — Plosive source primitive: added explicit silent closure followed by
a finite seeded spectral burst with smooth attack, decay and release. Bounds,
whole/chunk equality, checkpoint/reset and cancellation rollback are tested.
Full Release build and four focused suites pass (28.04 s). This is a source
component, not yet recipe/phoneme/candidate integration or stop pronunciation
qualification. No prior captured source paths were missing or Git changes
published. U20 and full Beta GO remain open. See `PLOSIVE_SOURCE_2026-09-09.md`.

2026-09-09 — Nasal timing display repair: the phoneme lane no longer replaces
resolved procedural coda/standalone-N spans with fixed 28-pixel estimates.
Inferred and manual labels, adjoining vowel bounds and boundary hit-testing are
verified at two zoom levels; sample-dependent estimates remain intact. Full
Release build and five focused suites pass (23.24 s). No captured files were
missing and no Git publication occurred. This is geometry/hit-testing proof,
not full native UX qualification or Beta GO. See `NASAL_TIMING_DISPLAY_2026-09-09.md`.

2026-09-09 — Automatic simple coda timing: procedural timing-policy revision 3
allocates one coda tail in a wholly untimed syllable, optionally with one onset.
The vowel ends at the inferred coda start; next-syllable edits are resolved first.
Authored groups, source-dependent timing and cluster rejection remain protected.
Articulation-plan revision 4 admits resolved inferred nasal codas. Actual aN
render/bake/import/recovery and timing regressions pass. Full Release build and
five focused suites pass (28.67 s). No captured paths were missing or Git changes
published. Phonetic quality, richer articulation and full Beta GO remain open.
See `AUTOMATIC_CODA_TIMING_2026-09-09.md`.

2026-09-09 — Standalone syllabic nasal: a sole Japanese N token now uses its
own note span without an invented vowel nucleus. Voiced snapshot/stream admission
and candidate-v3 all-N validation support the case; duplicate-note syllabic
markers and ambiguous multi-N allocation reject. Actual render/bake/load/import/
recovery runs against special:N without approval. Full Release build and five
focused suites pass (28.55 s). Plan revision 3 and stream revision 4 invalidate
old render identities. No prior captured file was missing and no Git publication
occurred. Broader pronunciation, other consonants and all Beta GO requirements
remain open. See `SYLLABIC_NASAL_2026-09-09.md`.

2026-09-09 — Initial nasal consonant gestures: explicit active m/n/ng/N poses
now use voiced nasal resonance/antiresonance with the oral output closed, not
renamed vowel coloration. Inferred onsets and explicitly timed nonoverlapping
codas bind a same-note vowel nucleus. Typed Nasal markers flow through scheduler
chunks/cache and candidate v3 baking/import/recovery without approval. Tests prove
nonzero onset/coda output, oral-band independence, checkpoint/reset/cancellation
continuity, exact scheduled PCM and a real unapproved ma candidate. Six focused
suites pass (29.15 s); full Release build and full CTest 119/120 pass (258.39 s),
only source closure with 405 unindexed required inputs. No captured paths were
missing and no Git publication occurred. Phonetic quality, nucleus-free nasals,
automatic coda/cluster timing, closures/bursts and full Beta GO remain open. See
`NASAL_CONSONANT_GESTURES_2026-09-09.md` and `../formats/PROCEDURAL_CANDIDATE_V3.md`.

2026-09-09 — Voiced-onset timing prerequisite: procedural timing-policy revision
2 allocates the existing bounded default to single voiced as well as unvoiced
onsets. Voicing, nucleus identity, authored timing and source-dependent behavior
remain unchanged; codas/clusters are not guessed. Actual Japanese m/n timing,
manual/short-note preservation and unsupported-renderer guards pass. Full Release
build and five focused suites pass (31.62 s). No source snapshot files were lost
or Git changes published. This does not complete nasal consonant synthesis or
accept U20/Beta GO. See `VOICED_ONSET_TIMING_2026-09-09.md`.

2026-09-09 — Multilingual candidate routing repair: removed two Japanese-only
vowel checks from procedural dispatch and candidate loading. Shared classification
now routes English/Korean vowel nuclei to sustained rendering and candidate v1;
the loader requires the exact pose and valid actual-rate tract. Renderer revision
8 invalidates old routing caches. A reproduced failing render/bake/load case now
passes through canonical producer import for English ah1 and Korean eo/eu, without
approval. Full Release build and five focused suites pass (31.91 s), core 841/841,
export 24/24 and snapshot 43/43. No prior snapshot files are missing. This fixes an
upstream reusable-bank defect found during consonant work; full language quality,
nasal/closure/burst articulation and all Beta GO requirements remain open. See
`MULTILINGUAL_CANDIDATE_ROUTING_2026-09-09.md`.

2026-09-09 — Nasal production integration: added scheduler transition/cache
coverage and moved the comprehensive frication/candidate/export/producer fixture
to a combined nasal/frication recipe. Actual stereo Final exports prove unchanged
100 ms consonant noise and changed vowel PCM; retained oral/nasal WAVs are finite,
unclipped engineering comparisons. Three focused suites pass (24.18 s), snapshot
43/43, export 23/23 and core 840/840. This is integration proof, not another DSP
feature or musician approval. No prior snapshot file was missing, no unit was
accepted and no Git publication occurred. Actual consonant articulation and all
remaining Beta GO requirements stay open. See `NASAL_PRODUCTION_INTEGRATION_2026-09-09.md`.

2026-09-09 — Audible nasal voice coloration: schema-3 recipes now bind explicit
nasal resonance/antiresonance frequencies and bandwidths. Stable pole/zero DSP,
independent-bank transitions, checkpoint/reset/rollback and zero-coupling exact
bypass connect to sustained/articulated rendering. Five native Designer controls
enable/edit the model with existing undo and saved-resource identity protection.
Live macOS loaded an oral recipe, pinned A, edited coupling to 0.8, rendered B and
saved a separate schema-3 recipe; no recording or quality approval occurred.
Five focused suites pass (26.97 s), design 16/16, Designer 24/24 and core 840/840.
Full Release run: 119/120 PASS (250.05 s), only source closure with 378 unindexed
required inputs at execution. No prior snapshot file was missing. This advances
U19 but does not synthesize nasal consonants or qualify a singer; full Beta GO,
remaining articulation, neural/classical and release work stay open. See
`NASAL_VOICE_DESIGN_2026-09-09.md` and `../formats/VOICE_RECIPE_V3.md`.

2026-09-09 — Native source registration: Studio now captures license bytes on its
serialized worker and exposes explicit AppKit/Win32 source-declaration forms.
All six choices (kind, rights, four permissions) require deliberate selection;
existing provenance, stale/recaptured context and late commit receipts remain
protected. Actual macOS source-free planned-Draft QA recorded only an unassessed
synthetic source with all permissions false, no recording and no unit approval.
Full Release build and seven focused suites pass (24.22 s), core 839/839 and
Studio draft/source 34/34. Eighteen control bounds and readable live form checked.
No missing prior snapshot files, unit acceptance or Git publication. Full initial
setup, Windows runtime and all remaining Beta GO requirements stay open. See
`NATIVE_SOURCE_REGISTRATION_2026-09-09.md`.

2026-09-09 — Explicit source registration: a source-free Draft now registers and
selects a new human/procedural/TTS source through a typed repository operation
and actual CLI. Exact project/evidence digests, registered producer attribution,
explicit permissions, cancellation and immutable prior take provenance are
preserved. No coverage/listening PASS or unit approval is inferred. The CLI
bank-to-installed-new-score journey now uses registration instead of a prefilled
source policy. Full Release build and five final focused suites pass (23.61 s);
Python parity/admission passes 20 tests. Three new producer regressions cover
negative/preservation boundaries. Native setup, assignment migration and the
full singer/product scope remain open. No unit acceptance or Git publication;
see `SOURCE_REGISTRATION_2026-09-09.md`.

2026-09-09 — Native source-quality assessment: added background evidence capture,
explicit AppKit/Win32 decision forms and Studio controls, with no default reviewer
or PASS outcome. Full captured identity prevents stale/recaptured modal adoption;
durable receipts survive late cancellation. Six new tests cover these boundaries.
Live macOS QA recorded only a synthetic Not assessed decision, leaving zero unit
approvals and no microphone activity. QA-discovered modal focus loss was repaired
and I/Escape/I plus D/Escape/D verified; Review typography/wrapping improved.
Final full build and seven focused suites pass (23.84 s), core 832/832 and Studio
draft/source 30/30. No prior snapshot file was missing. Windows runtime, real
source/music qualification and full Beta GO remain open; no unit acceptance or
Git publication occurred. See `NATIVE_SOURCE_QUALITY_2026-09-09.md`.

2026-09-09 — Source quality assessment workflow: added actual inspect/record
CLI commands and schema-3 append-only, policy/material/evidence-bound reviewer
decisions. Old schema-1/2 bytes remain unchanged; source permissions are not
granted or rewritten. Canonical writer transitions and C++/Python parity reject
stale/changed evidence, self-review and history rewriting. Reassessment revokes
affected current unit approval without deleting history. The real CLI fixture
now progresses from unassessed source quality through recorded review, draft,
unit review, package/install and new-score Final export without original input
paths. Full build passes. A full run before final readiness/query hardening was
119/120 (321.17 s), only source closure; latest five focused suites pass (25.76 s),
core 826/826, producer 42/42, CLI 4/4 and Python parity/admission 19/19. Native
assessment controls, actual source/music qualification, privacy/disclosure and
remaining Full-Scope units are not accepted. No Git publication or Beta GO is
claimed. See `SOURCE_QUALITY_ASSESSMENT_2026-09-09.md` and the format contract.

2026-09-09 — Responsive Studio unit selection: ordinary rail/Up/Down and
Review navigation now share a serialized background read/hash/decode/analysis
path, including external manifests without a producer. Captured context,
dirty edits, cancellation, stale rejection, current-window relayout and shutdown
joining are preserved. Four new regressions pass; current core is 822/822 and
Studio draft/close/selection is 24/24. Seven rebuilt focused CTest entries pass
in 23.06 s, including actual cold CLAP bounce and Phase12B. The preceding full
119/120 run remains historical for its checkpoint. No source inputs disappeared
relative to the preceding recovery snapshot; six source/test files changed
intentionally. See `STUDIO_BACKGROUND_SELECTION_2026-09-09.md`. No new unit,
native/Windows/latency qualification, Git publication or Beta GO is claimed.

2026-09-09 — Checkpoint integration and Studio close repair: fixed the Studio
test/private-API compile error and Phase12B's stale-plan fixture sequencing.
The now-reachable Final assertions exposed and repaired copy-on-write PCM
detachment in non-stereo preview conversion and the debounce interval during
which edited Final audio remained current. Phase12B now verifies exact captured
PCM and immediate edit invalidation through 1/2/8/4 output channels. Added native
sample Save/Discard/Cancel close handling with modal-context validation,
Designer/reentrant-close protection and four controller regressions. Full
Release build passes; full serial CTest is 119/120 (255.74 s), only source
closure failing with 347 unindexed inputs at execution. Core is 818/818;
Studio draft/close is 20/20. Local recovery comparison found no missing source
files and exactly 12 intentional changed source/test files. No Git publication,
Windows/native-modal runtime qualification, new unit acceptance or Beta GO is
claimed. See `INTEGRATION_AND_STUDIO_CLOSE_REPAIR_2026-09-09.md` and its retained
execution evidence. Earlier same-date review counts remain historical.

2026-09-09 — Audit-driven implementation and actual sample publication:
replaced the linked-engine matrix with a host that loads and processes the
canonical CLAP binary. The pre-repair run failed all 336 rows on pan; the
repaired binary passes all 336 plus text-binary rejection, explicit fixture
admission, leading-silence/status, state and event-boundary checks. Fixed CLAP
ABI/mix/addressing, persistent controllers, sustain/panic and default
polyphony; legacy legato is explicitly selected only by its engine workloads.
Repaired EN/KO context admission and cross-language retention, diagnostic-only
mixed-language review, and cached fallback counts. A new review-bound sample
candidate transaction publishes real WAV/manifest/marker/provenance bytes;
its positive test reopens a saved score and renders Final audio after hiding
producer inputs, then reproduces the result from disk cache. Studio/CLI
review/publication, package/install integration and qualified resources remain
open. Full Release build passes; full CTest was 108/110 (helper test-only
startup budget and source closure). The success-path helper test now uses its
existing production request budget, with negative deadlines unchanged;
focused protocol/helper tests and a fresh 716-case core run pass. The full
suite was not re-labelled as a new run. Source closure had 278 unindexed
inputs at that snapshot and remains an integration obligation. See
`AUDIT_REPAIR_AND_SAMPLE_PUBLICATION_2026-09-09.md` for precise boundaries.
No new Full-Scope unit or Beta GO acceptance is declared.

2026-09-09 — Read-only direction re-audit correction: the earlier 107/108
CTest run is historical, not verification of the current worktree. The
canonical matrix/soak runners hash the plugin but execute a linked engine;
a plain-text negative-control plugin also produces 336 PASS cases. The
strengthened verifier now rejects these legacy summaries. English/Korean
stale-context application and Japanese-to-Korean silent rebinding were
reproduced. An unfinished CLAP ABI/test patch predating the review pause
currently prevents the strict core test build through an external-header
warning; production fixes were not performed during this read-only review.
See `DEVELOPMENT_DIRECTION_DEEP_REVIEW_2026-09-09_KO.md` at the project root
for current findings and focused results. The U35 labels on the historical
Phase 12C entries below are not Full-Scope U35 acceptance: this plan's U35
is neural dataset/training. Preserve those entries as history, not current
canonical-host or neural implementation evidence.

2026-09-09 — Direction review and canonical-slice audit: the approved
Full-Scope plan remains the correct authority. The current implementation is
architecturally on-track but not Beta GO: Release CTest is 107/108 with only
tracked-source closure failing (264 unindexed local inputs at audit time),
`seam_tests` is 695/695, the Phase 12C canonical slice is 7/7, and strict
plugin/bank-bound verification accepts the 336-case matrix plus five-second
smoke soak. The current public-domain bank is explicitly a technical fixture,
not a release singer; neural model, full qualification, target-host evidence,
and unresolved resource/empirical criteria remain blockers. Detailed Korean
review: `docs/reviews/DEVELOPMENT_DIRECTION_REVIEW_2026-09-09.md`.

2026-09-08 — U27/U28 command and transfer migration: note/lyric mutation
reconciliation, generated-phone Find, technical-edit review, performance-take
validation and copied render-edit binding now dispatch through the region
language resolver instead of assuming Japanese. Explicit hint edits validate
against the selected English/Korean/Japanese inventory; shared resolver
admission applies the existing 10,000-note, 65,536-character and 4,096-edit
bounds to every language. English and Korean hint-command/search regressions,
transfer preservation and existing Japanese suites pass. This closes the
Japanese-only application boundary but remains bootstrap vocabulary/resource
and acoustic qualification work; U27/U28/Beta GO remain open and changes are
local/uncommitted.

2026-09-08 — U45 semantic full-product report validator foundation: added a
hash-bound report reader with duplicate-key/non-finite/depth/size limits,
no-follow regular-file checks, JSON-Schema validation, candidate/contract/
profile/resource-matrix identity binding, exact R1–R20 and 83-case coverage,
case dimension and platform/host checks, review/artifact/check/operation
coverage, and empirical-cell coverage checks. The release gate now validates a
provided `fullProductReport` instead of treating its content as opaque; the
legacy “semantic validator unavailable” diagnostic remains only when the
mandatory report reference is absent. The canonical contract is still
UNRESOLVED (resource matrix and empirical criteria), and no evidence or PASS
is fabricated. Reader, symlink, digest-tamper, and CLI tests pass. This is a
U45 foundation, not U45/Beta GO acceptance; changes remain local/uncommitted.

2026-09-08 — U35 canonical Phase 12C evidence contract foundation: added a
source-bound verifier for the canonical `com.project-seam.editor` CLAP target,
production live-engine linkage, 32-voice/1,024-event/256-MiB limits, pinned
clap-validator 0.4.1, and rejection of legacy/generated fixture symbols. When
artifact inputs are supplied it additionally rehashes the exact plugin and
voicebank, requires a 336-case finite matrix with source/build and bank
identity, validates pinned validator output, and requires canonical full-soak
identity/counters. Source-only CTest is green; the later runner-promotion
entry below records the canonical matrix/smoke identity results. This is a U35
evidence boundary, not U35/Beta GO acceptance; changes remain local/uncommitted.

2026-09-09 — U35 canonical runner promotion: Phase 12C matrix and soak
runners now consume the canonical `ProjectSEAMEditor.clap` bundle and the
production-bank root under CTest, rehash both artifacts, load resources through
the production `VoicebankCatalog`/`buildTrustedResource` path, and emit bound
plugin/bank/source/build identity. The 336-case matrix and five-second smoke
soak pass the strict canonical verifier; transition fallback is counted as an
explicit valid workload when the technical bank has no exact transition unit.
The official clap-validator, full 7,200-second soak and target-platform/DAW
acceptance remain open; changes remain local/uncommitted.

2026-09-08 — U27/U28 procedural language routing: procedural snapshot
construction now uses the shared language resolver rather than hard-coding the
Japanese service. English and Korean snapshots retain their language-bound
pronunciation identities and render only when the frozen recipe explicitly
contains matching vowel/frication poses; missing recipe coverage fails closed
instead of borrowing Japanese sounds. Release performance-snapshot and core
regressions pass for English and Korean fixtures. Native-speaker resource
review, complete dictionaries, consonant coverage and Beta GO remain open;
changes remain local/uncommitted.

2026-09-08 — U27/U28 multi-voice context routing: score voice compilation
now uses the generic language resolver for per-voice pronunciation instead of
hard-coding Japanese context. Overlapping English notes retain independent
phoneme timing/voice ownership and reject stale/custom context through the
existing coverage guards. Release compiler and core regressions pass. This
does not provide reviewed multilingual singer resources or complete language
rendering qualification; U27/U28/Beta GO remain open and changes are local/
uncommitted.

2026-09-08 — U27/U28 native coverage routing: the Style/Coverage sheet and
standalone selected-region coverage command now
uses the selected bank language's generic pronunciation service. English and
Korean regions can produce structural coverage when the bank declares matching
units; a mismatched language remains an explicit diagnostic with no fallback.
Release style-coverage, standalone workflow and core regressions pass. This is inventory/phoneme
coverage evidence only, not native-speaker review, acoustic qualification or
Beta GO; changes remain local/uncommitted.

2026-09-08 — U34 offline render authority/readiness gate: added a thread-safe
Final-only `OfflineRenderSession` that binds a host bounce to the exact project
digest, timing-authority digest, sample rate and render ABI. CLAP offline-mode
selection now prepares and waits for a current Final publication before
activation; process() emits bounded silence while offline audio is pending or
failed, and score/project/host-timeline changes invalidate the gate. The
session rejects stale completions, empty PCM and malformed provenance. Release
offline-session and Phase 12B integration tests pass. This is a non-realtime
preparation/readiness boundary, not complete Follow Host tempo-map capture or
installed DAW tuple qualification; U34/Beta GO remain incomplete.

2026-09-08 — U8 renderer capability and Raw trajectory repair: added an
explicit render-control capability contract for pitch, timing, dynamics,
vibrato, attack, release and advanced controls. Required unsupported controls
now fail before backend work and Raw fallback cannot claim pitch-preserving
transients. Raw rendering now applies an explicit bounded pitch curve on its
sustain trajectory, and the curve is included in render identity with a bumped
renderer revision. Release capability, synthesis-quality, Phase 12B and core
suites pass. This remains a capability/DSP correctness boundary, not acoustic
or listener qualification of every renderer or the full U8 acceptance.

2026-09-08 — U38 automatic-performance proposal foundation: added a
side-effect-free deterministic proposal generator bound to region,
performance-revision, pronunciation, resource, generator and seed identity.
It emits bounded Pitch/Dynamics/Attack/Release lanes as an unaccepted
`PerformanceTake`, and feeds the existing stale-safe proposal/accept commands.
Stale inputs, cancellation, duplicate channels and advanced channels without a
qualified generator fail before mutation. Release U38, proposal-command,
performance-job and compiler suites pass. This is lifecycle/contract evidence,
not a trained neural singer, acoustic qualification or full U38/U40 acceptance.

2026-09-08 — U30/U32 native USTX and interchange lifecycle: replaced the study-only YAML bridge boundary with a bounded native USTX 0.9 parser/encoder and typed project conversion. The parser rejects aliases/tags, multiple documents, duplicate keys, inconsistent indentation, non-finite values and hostile collection/scalar sizes before growing typed state; tempo/meter, note timing, pitch-in-milliseconds, vibrato, track controls and explicit conversion losses are retained. Added a stateless create-new file service with source digest, unsaved-draft preparation, explicit review callback and accept/reject replacement semantics, plus native Open USTX/MIDI and Export Score menu/file-dialog commands. Release USTX, service, SMF and core suites pass; this remains subset/loss-review evidence, not full USTX interoperability, native panel visual proof, or installed host acceptance. Details: `MIDI_INTERCHANGE_V1.md` and new USTX APIs under `libs/seam-interchange/`; U30/U32/Beta GO incomplete.

2026-09-08 — U37 neural deployment contract foundation: added a versioned little-endian length-prefixed worker frame with bounded JSON metadata, Float32 payload validation, exact model/pronunciation identity, shape/rate/channel limits and stale-response rejection. Added a model contract binding model/vocabulary hashes and runtime shape, an explicit-helper runner using the existing non-shell bounded process boundary, and a real probe process that exercises request→helper→response IPC. The helper runner now opts into an explicit bounded framed stdin ceiling while retaining the historical 4 KiB dictionary default. Release protocol/helper tests pass; this is contract and crash/resource-boundary evidence, not a trained/qualified neural singer, ONNX runtime, signed payload or Beta acceptance. Details: new `libs/seam-neural-synthesis/`; U35/U36/U37/Beta GO incomplete.

2026-09-08 — U40 harmony workflow foundation: added deterministic, side-effect-free interval harmony preparation with explicit source-note selection, MIDI range checks, copied lyric/language identity, source-vibrato preservation and fresh note/token IDs. `AddHarmonyCommand` applies a prepared layer atomically, rejects stale source revisions/identity collisions, and restores exact note/lyric state through undo/redo. Release focused harmony tests pass; native harmony review/scale constraints, neural generation and independent creator qualification remain open. U40/Beta GO incomplete.

2026-09-08 — U41 character-state binding repair: native editor scene semantics now derive Character 01 runtime states from verified voice identity and render lifecycle (warning, rendering, complete/error, focused/neutral) instead of leaving artwork permanently neutral/focused. The host-owned presentation selects the corresponding bundled state asset; focused native UI regression passes. Actual production asset rights, mouth/phoneme synchronization and installed-platform qualification remain open. U41/Beta GO incomplete.

2026-09-08 — U26 native review and Apply surface: bound the owner-thread Japanese reading job to the native replacement-review panel with preparing/ready/failure states, paged contextual-token rows, wrapped read-only detail, accessibility root/actions, explicit Apply/Cancel/Retry controls and a standalone Edit-menu command. Apply is now UI-gated by the same immutable single-note/known/no-hint ownership plan used by the command, then persists per-note phone hints plus pronunciation identity through one undoable performance transaction. Host injection points were added for a verified/staged resource in standalone and CLAP runtimes; no PATH/working-directory discovery or fake production dictionary is used. A focused native test covers real staged helper execution, accessibility row/detail navigation, Apply, JSON-visible state and exact undo/redo. Release focused native/Japanese/helper and core tests pass; this remains resource-injection and fixture evidence, not a shipped reader or native-language approval. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U27/U28 language service foundation: added bounded English and Korean phonemizers with explicit phone-hint precedence, stress-bearing English vowel symbols, bootstrap lexicon diagnostics, Hangul syllable decomposition, coda liaison and continuation handling. Versioned English/Korean resolver identities bind generated tokens to region/note/lyric context addresses; generic inspection now selects the explicit language service, and sample snapshots can resolve against an English or Korean voicebank language instead of hard-failing Japanese-only. The later command/transfer migration entry above extends this routing through mutation and persistence boundaries. Five focused language cases pass Release and are included in core. This is a rule-backed bootstrap vocabulary, not native-speaker-reviewed language resources or Beta acceptance. Details: `LANGUAGE_PHONEMIZER_2026-09-08.md`. Changes local/uncommitted; U27/U28/Beta GO incomplete.

2026-09-08 — U31 bounded SMF codec foundation: added Type 0/1 PPQ Standard MIDI import/export with pre-allocation chunk/VLQ limits, running status, FIFO equal-key overlap pairing, tempo/meter/lyric/text retention, deterministic output ordering, missing-note diagnostics and explicit loss records for unsupported events. SMPTE, malformed status/data, invalid UTF-8, hostile lengths, trailing bytes and invalid score fields reject without mutating caller state. Four focused cases pass Release and the same codec test is included in core. Details: `SMF_INTERCHANGE_2026-09-08.md` and `../formats/MIDI_INTERCHANGE_V1.md`. Native conversion lifecycle, USTX, held-file boundary and external-DAW interoperability remain open; U31/Beta GO incomplete.

2026-09-08 — U33 live-expression repair: separated CLAP vibrato and pan from timbre, added per-voice equal-power stereo panning with channel-10 MIDI support, a bounded 5.5 Hz vibrato pitch LFO and realtime-safe timbre/brightness shaping. Live output no longer duplicates one mono sum into every channel; pressure remains the amplitude control. Core CLAP/live regressions prove pan energy separation, centered stereo output and nonzero vibrato delta while existing allocation/live smoke tests pass. This is algorithmic correctness evidence, not installed-host mapping or acoustic/listener qualification. Changes local/uncommitted; U33/Beta GO incomplete.

2026-09-08 — U26 owner-thread reading worker: added serialized `JapaneseReadingJob` with immutable capture, background jthread helper/decoder/binding, request IDs, cancellation retirement, terminal diagnostics and current-result guards for region/revision/full project/performance generation/resource identity. Real staged helper fixture proves cross-note 学校 ownership and explicit-hint protection; concurrent start, cancel/restart, missing staged file and stale replacement cases pass. Release/Debug focused Japanese targets pass (0.80/0.51 s); Release core target previously 650 cases (16.54 s), builds/diff checks pass. No score mutation/UI Apply; latest-request orchestration, resource limits and host qualification remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 captured contextual reading and ownership: added immutable phrase assembly with exact lyric/note spans, shared-melisma reuse, rest separators and explicit-hint flags. Typed tokens bind to all owning notes and flag lyric/hint/unowned crossings without inventing mora allocation or applying commands. Current-document/resource/generation checks reject stale adoption. Real staged-reader probe resolves 学校 across two score notes, preserves hint protection and verifies no score/history mutation in R/D. All 650 Release core cases pass (16.54 s), 14 Japanese cases pass R/D (0.51/0.55 s); builds/diff checks pass. Latest-request worker lifecycle, helper CPU/memory limits, review/Apply/persistence and host/native-language acceptance remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 private verified resource staging: added independent streamed/hash-checked copies in a private directory, read-only publication permissions, shared consumer lifetime and fixed-file descriptor-relative cleanup. In-place original truncation/replacement does not alter staged bytes; last-owner cleanup preserves originals. Actual staged helper/dictionary -> stdin -> typed-decoder probes pass R/D. All 649 Release core cases pass (16.37 s), 13 Japanese cases pass R/D (0.38/0.41 s); builds/diff checks pass. This is not kernel immutability or protection from owner/root tampering; crash pruning, disk budgets, memory/CPU limits, supervisor and source-request adoption remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 reader resource verification: added exact helper/four-file dictionary integrity checks, canonical paths, bounded streaming hashes, no-follow/nonblocking regular-file verification on POSIX, extra-configuration rejection and repeat validation. Reading identity now includes the helper binary digest plus engine/dictionary identity. Real transport probe uses verified metadata and checks resources before/after execution; R/D pass against the pinned dictionary. Tests reject tampering, extra dicrc, oversized files, symlinks and FIFO. All 648 Release core cases pass (15.68 s), 12 Japanese cases pass R/D (0.48/0.42 s); builds/diff checks pass. Point-in-time verification is not immutable/race-free execution or release trust; staging, memory limits and request adoption remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 private stdin transport: added a 4096-byte opaque stdin channel with nonblocking input/output polling, exact EOF delivery and per-socket/send SIGPIPE suppression. Process tests cover UTF-8/binary bytes, boundary/overflow, simultaneous output, empty EOF, early exit and non-consuming timeout. Updated real MeCab probe/checker to stdin; four dictionary hashes, three reading/span fixtures and three admission checks pass. New explicit transport probe passes runner -> real dictionary -> bounded decoder -> typed validation in R/D, with synthetic identity clearly not attestation. Two process cases pass R/D (1.18/1.59 s), all 647 Release core cases pass (16.11 s); builds/diff checks pass. Resource/config verification, memory limits, host supervision and request adoption remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 helper process lifecycle primitive: added explicit-path/no-shell worker execution with clean environment, stdin/descriptor isolation, separate bounded nonblocking stdout/stderr, deadline/cancellation, process-group cleanup and direct-child reaping. macOS tests cover actual overflow, failures, timeout, descendant-held pipes, cancellation and injected environment/non-CLOEXEC descriptors; focused R/D passes (1.09/1.23 s), all 647 Release core cases pass (16.07 s). Builds/diff checks pass. This is not a verified Japanese helper service or sandbox: memory/CPU limits, resource/config verification, lyric stdin transport, exclusive plug-in-host reaping ownership, Windows/Linux qualification and stale-request adoption remain open. Details: `READING_HELPER_PROCESS_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 bounded reading response decoder: added authoring-layer version-1 JSON decoding with explicit byte/depth/node/string/collection limits, exact schema/type/source checks and final typed span/identity/expansion validation. Tests cover matching wire data, 12 invalid mutations, duplicate/concatenated/deep/oversized JSON and cancellation; live probe fields match the decoder fixture. All 647 Release core cases pass (16.76 s), 11 Japanese cases pass R/D (0.52/0.55 s); native/core/focused builds and diff checks pass. No reusable production subprocess runner exists in the inspected libraries; verified helper launch, bounded I/O, process lifecycle and stale-request adoption are next, before resolver/editor integration. Caller-supplied identity is not JSON attestation. Details: `JAPANESE_READING_INTAKE_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 typed reading boundary: added resource/source-bound reading DTOs and validation for UTF-8-aligned exact spans, unknown/missing states, count/text/expansion limits and cancellation. Development probe now emits native MeCab node spans as JSON, eliminating surface/CSV ambiguity; third observed fixture covers comma/quote/ideographic whitespace and exposes isolated 歌→カ ambiguity without claiming native approval. Four dictionary hashes, three real reading/span fixtures and two admission checks pass. All 645 Release core cases pass (16.62 s), nine Japanese cases pass R/D (0.37/0.40 s); strict probe/scoped builds and diff checks pass. Production bounded decoder, isolated execution, editable readings, owner mapping and resolver/UI integration remain open. Details: `JAPANESE_READING_INTAKE_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 real reading dependency intake: cloned/pinned Open JTalk fork `462fc38e7520aa89e4d32b2611749208528c901e`, inspected MeCab/NAIST/UniDic-CSJ notices, built the front-end library and complete UTF-8 dictionary outside the repo, and added a development probe plus hash-pinned unreviewed reading fixtures/checker. Actual kanji/particle readings and unknown-reading behavior pass; four dictionary hashes and two input-admission checks pass. No acoustic model is required. Source review identified unbounded lattice allocation/no cancellation and ambiguous convenience CSV output; typed node spans and isolated bounded execution are the next integration requirements. No production resolver or shipping dependency change yet. License auditor fails only the existing master-branch policy; no bypass. Diff check passes. Details: `JAPANESE_READING_INTAKE_2026-09-08.md`. Changes local/uncommitted; U26/Beta GO incomplete.

2026-09-08 — U26 kana normalization: added targeted half-width kana/voicing-mark and decomposed dakuten/handakuten handling while preserving stored surface text and original scalar warning offsets. Explicit phone hints retain precedence; unknown readings are not guessed. Mapping facts checked against Unicode 17.0; phonemizer revision advances to 4 and existing source-digest identity changes. Two new shared-resolver cases and a focused Japanese target pass; Release core target passes (16.28 s), focused R/D (0.37/0.40 s), native/core/focused builds and diff checks pass. Dictionary intake, contextual readings, chunk/revision qualification and native-language review remain open; U25 qualification is not waived. Details: `JAPANESE_NORMALIZATION_2026-09-08.md`. Changes local/uncommitted; no U26/Beta GO acceptance.

2026-09-08 — U25 immutable bank snapshots: added owner-only immutable resolution snapshots, bounded one-entry session caching and invalidation on reference/track, catalog refresh and actual trust-policy changes. Failed refresh makes the snapshot path unresolved. Both native hosts use snapshot identity for routine sheet checks while retaining full project/context guards and full manifest/trust validation on Apply. Real signed 256-style/16,384-unit fixture covers reuse, invalidation, direct edits, refresh failure/recovery and exact Apply/undo; audio/export regression uses the new path. Release view construction is about 0.015 ms versus 6–7 ms by-value in this fixture. All 641 Release core cases pass (16.20 s); eight style cases pass R/D (2.70/22.64 s), 23 export cases pass R/D (2.09/11.53 s). Builds/diff checks pass. Initial preparation/weighted-query and live-host latency remain open. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 style inventory/choice performance: replaced per-style full unit scans with a single inventory index and cached pronunciation/work admission for the immutable draft; repeated same-style choices retain coverage while making provenance explicit. A 256-style/16,384-unit regression validates counts, choice correctness and source preservation. Focused Release prepare/choice/1000-repeat/view-average timings are 5.690/0.314/0.007/6.050 ms; Debug 49.280/4.588/0.262/47.495 ms. View copying/comparison remains a measured target; no stale/trust checks were relaxed. All 641 Release core cases pass (14.14 s), eight focused cases pass Release/Debug (0.55/1.72 s); scoped builds/diff checks pass. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 installed-bank style audio/persistence verification: added a real signed/installed two-style synthetic bank regression through native sheet selection, strict host-style resolution, Preview/Final rendering and shared cache. Draft PCM stays unchanged; Apply selects the correct different unit/audio with one exact style-only edit. Fresh catalog scan plus cold render, JSON reload, Float32 master/stem export and undo/redo reproduce expected PCM. All 640 Release core cases pass (13.98 s); 23 export cases pass Release/Debug (2.58/10.47 s); scoped builds and diff checks pass. This is synthetic sample-bank source-selection evidence, not shipping singer/listening, procedural/neural style or live host state qualification. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 coverage issue inspection: added paged issue list and complete wrapped detail view with note/phone/style/pitch/diagnostic/witness identity, unavailable-coverage explanation, original-page restoration and preserved draft choice. Browsing is read-only; stale source/action guards and explicit Apply/Cancel remain intact. Eight-issue pagination and unavailable-language regressions pass; all 639 Release core cases pass (14.10 s), seven focused cases pass Release/Debug (0.42/0.54 s). Scoped builds/diff checks pass; inspected 480×320 detail raster fits without overlap. Long style-ID inspection outside issue details, large-bank latency and acoustic/live-host qualification remain open. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 initial native Style/Coverage sheet: connected the captured model to paged native rows, explicit Apply/Cancel/Refresh, stale semantic/source guards, current standalone/embedded voicebank resolvers, standalone Edit menu and shared STYLE inspector button. Tests cover pointer entry, draft isolation, changed trust/source, exact undo/redo, page boundaries and menu dispatch. All 637 Release core cases pass (20.83 s); five focused cases pass Release/Debug (6.01/6.31 s); scoped builds and diff checks pass. Inspected 480×320 raster fits without row/button overlap. Detailed coverage-issue navigation, full visual long-text inspection, large-bank UI latency and live host/audio qualification remain open. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 Style/Coverage model: added the planned model files with captured track/region/project/bank identity, declared-order enabled/disabled inventory, explicit style choice, bounded Japanese structural coverage and one canonical style-only Apply/undo group. Missing choice/coverage is not promoted to readiness; procedural tracks reject the inactive sample-bank style route. Three cases prove exact undo/redo/no-op, stale/trust/hash/manifest rejection, cancellation and unavailable coverage/admission behavior. All 635 Release core cases pass (13.77 s); focused cases pass Release/Debug (0.50/0.42 s); native/core/focused builds and diff checks pass. This is the model, not a painted/host-wired sheet or audio/host approval. Details: `STYLE_COVERAGE_SHEET_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 Style/Coverage prerequisite repair: found and removed false complete coverage from the union of overlapping unit spans. Analyzer now computes maximum non-overlapping coverage, reports sequence conflicts, checks all notes in a candidate span and propagates disabled/style/pitch/orphan diagnostics to interior phones. Note-pitch indexing and bounded per-token witness storage avoid retaining all candidate combinations. Two new regressions and stricter existing counts pass; all 632 Release core cases pass (14.38 s), and 16 U3 cases pass Release/Debug (0.51/0.61 s). Native/core/U3 builds and diff checks pass. Structural coverage is not renderer/source/trust/acoustic approval; the Style/Coverage sheet remains unimplemented. Details: `VOICEBANK_COVERAGE_CORRECTNESS_2026-09-08.md`. Changes local/uncommitted; no additional roadmap/Beta GO acceptance.

2026-09-08 — U25 connected measured-output inspector: bound standalone/embedded controllers to current render coordinators and added channel-cycling read-only RMS dBFS mode with numeric RMS/sample-peak/full-scale summaries and accessibility content. Exact source/request/region/frame-window keys drive cancellation/retirement/replacement; held publication metadata prevents mixed-source display. The real-render native regression proves nonzero-region frame origin, tempo-aware placement, zoom clearing, channel keyboard focus, unchanged score/history, 48→96 kHz same-revision replacement and stale-document suppression. Final verification: 630 Release core cases pass (14.05 s); focused workflow passes Release/Debug (0.53/1.03 s); native/core/focused builds and diff checks pass. Inspected 480×320 view fits. An earlier elapsed-time outlier coincided with verified system sleep. This is rendered output, not live device/microphone or isolated singer/F0 measurement; live host/input/listening and remaining U25 qualification stay open. Details: `MEASURED_DYNAMICS_INSPECTOR_2026-09-08.md`. Changes local/uncommitted; no Beta GO acceptance.

2026-09-08 — U25 document-bound measured-audio worker: ready render publications retain actual immutable project input; new capture binds that snapshot, PCM/request identity and editor session generation. Added owner-thread single-worker measurement lifecycle with guarded adoption/current reads, retirement-before-restart, cancellation discard and failure cleanup. Tests distinguish identical new sessions from revision-incrementing replacement, reject direct mixed-audio input changes, and verify stale/cancelled/invalid worker results never become current. All 629 Release core cases pass (13.54 s); 19 coordinator cases pass Release/Debug (1.81/6.37 s); native/core/coordinator builds and diff checks pass. Measured trace/controller wiring, frame-origin/stream-role presentation, viewport request lifecycle and live host qualification remain open. Details: `MEASURED_AUDIO_LEVELS_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 measured-source identity: added project/request/coordinator-scoped identity to render publications, current-ready acquisition and source matching across quality/rate/channels/PCM storage. Same-revision resubmissions, cancellation and failed requests invalidate measurement-source eligibility without removing retained playback. Tests cover real renders, cross-coordinator collisions, mutated copied PCM and altered metadata. All 627 Release core cases pass (13.82 s); 17 coordinator cases pass Release/Debug (1.64/5.88 s); native/core/coordinator builds and diff checks pass. These are point-in-time in-process source guards, not document-generation binding, cryptographic provenance or an implemented measured overlay; worker/session/UI integration remains open. Details: `MEASURED_AUDIO_LEVELS_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 strict measured-audio backend: added bounded read-only interleaved PCM window analysis with absolute frame bins, per-channel RMS/sample peak/full-scale counts and explicit silence dBFS semantics. Nonfinite samples reject rather than being substituted; work/output and stop checks are bounded. Three focused cases cover anti-phase stereo, headroom, frame partition/energy, invalid input and cancellation/budget admission. Native dynamics Final-render integration proves quarter-gain RMS/peak scaling and exact measured replay after project reload. All 626 Release core cases pass (14.91 s); three measurement cases pass Release/Debug (0.42/0.45 s), and 22 export cases pass Release/Debug (3.55/10.74 s); scoped builds and diff checks pass. This is the backend, not an inspector overlay: source/provenance binding, stale-result protection, off-thread publication and live host/audio measurement remain open. Details: `MEASURED_AUDIO_LEVELS_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 horizontal dynamics point dragging: added Shift-drag time editing with gesture-start axis locking, captured viewport tick mapping, integer rounding/clamping and gain preservation. Existing collision rejection and explicit point/region commit stages remain intact. An extreme synthetic fixture exposed the distinction between standalone point validation and region-duration admission; draft/model field validation now rejects out-of-region times before staging, while a pure mapping test covers 64-bit arithmetic separately. Three regressions prove collision recovery, exact Apply/undo/redo, zoomed bounds, nonfinite rejection and early invalid-field feedback. All 623 Release core cases pass (13.81 s); fourteen focused cases pass Release/Debug (0.80/2.91 s); native/core/focused builds and diff checks pass. Live OS/host gestures, measured audio and remaining U25 scope remain open. Details: `DYNAMICS_TIME_NAVIGATION_2026-09-08.md`. Changes local/uncommitted; no Beta GO acceptance.

2026-09-08 — U25 viewport-adaptive dynamics sampling: added validated/clipped display windows and cached compiled voices, so zoom/pan/Fit increases local target/generated sample detail without recompiling unchanged score intent. Native-curve edits invalidate the cache and resample the staged viewport. Expanded tests prove the exact tick-level generated/manual transition, denser native zoom output, invalid/out-of-region window safety and recovery. Cached resampling in the 4096-note/16-voice fixture took 0.193 ms Release / 2.424 ms Debug for 368 samples. All 620 Release core cases pass (13.76 s); eleven focused cases pass Release/Debug (0.90/2.97 s); native/core/focused builds and diff checks pass. These remain score-only samples, not continuous/sub-tick or measured audio; metadata-heavy latency and live host qualification remain open. Details: `DYNAMICS_TIME_NAVIGATION_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 dynamics time navigation: added overflow-safe plot-local zoom/pan/Fit, visible and accessible navigation controls, keyboard/wheel routing, exact native boundary interpolation and viewport filtering for handles/generated/target markers. Point paging now uses two rows to fit compact navigation. Tests prove 64-bit range safety, pointer-anchored zoom, stale action/source rejection and unchanged score/history/main timeline. All 620 Release core cases pass (14.19 s); eleven focused cases pass Release/Debug (1.09/3.21 s); native/core/focused builds and diff checks pass. Inspected 480×320 render fits without overlap. Generated/target samples are not yet recomputed at zoom-dependent density; horizontal point dragging, measured audio and live host gesture qualification remain open. Details: `DYNAMICS_TIME_NAVIGATION_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 target capacity/latency audit: exposed the allocator's existing 4096-note bound, preflighted target refresh before a large project copy, and made the exact unavailable diagnostic visible. Added 4096-note/16-voice coverage regression (12432 samples covering every note) and 10000-note model/native editing proof with explicit target unavailability and exact Apply/undo. Final focused capture/compile timings: Release 0.419/8.093 ms, Debug 2.447/85.579 ms; simple fixture only, not metadata-heavy or whole-window qualification. All 618 Release core cases pass (13.81 s); nine focused cases pass Release/Debug (0.88/2.97 s); native/core/focused builds and diff checks pass. No renderer bounds were relaxed; target support above 4096 notes and worst-case latency remain open. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 selected-generated dynamics overlay: added inspection-only compiler evaluation that exposes accepted Dynamics before manual replacement, while retaining ordinary audio evaluation and the existing scope/offset/tempo/interpolation rules. Cached preview samples now draw generated orange markers separately from cyan post-ownership target samples. Generated zero remains visible; malformed null Dynamics is rejected. All 617 Release core cases pass (13.85 s); 18 separate compiler cases pass Release/Debug (0.89/4.13 s), and eight dynamics cases pass Release/Debug (0.65/0.54 s). Native/core/focused builds and diff checks pass; inspected 480×320 render shows the ownership difference without overlap. This is active-note, selected-generated sampled inspection, not all stored proposals, measured audio, continuous coverage or live host qualification. Details: `SELECTED_GENERATED_DYNAMICS_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 compiler-backed dynamics target overview: added cached per-voice sampled targets from production `compileScoreVoices` on the captured score plus staged native curve, shown as cyan dots separate from native source/draft lines and the unsaved point. Preview refresh occurs on inspector open/point staging, not paint/drag; mutations invalidate old samples, and compiler failures report target unavailability without blocking native edits. Tests verify compiler parity, generated/manual precedence, polyphonic note/voice identity and capacity-failure behavior. All 617 Release core cases pass (14.48 s); eight focused cases pass Release/Debug (0.82/0.67 s); native/core/focused builds and diff checks pass. Inspected 480×320 target render fits. This is a synchronous, bounded, 48 kHz score-only sampled overview—not continuous coverage, raw generated/measured lanes, final audible amplitude, worst-case latency or live host qualification. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; U25/Beta GO remain incomplete.

2026-09-08 — U25 generated/native dynamics influence feedback: captured accepted-Dynamics/manual-Replace scope counts, visible override warning and accessible compiler-precedence explanation without silently claiming ownership. New native editing regression uses the actual performance compiler: after native gain becomes 0.25, generated-controlled tick 1200 remains 0.8 while manually owned tick 1680 becomes 0.25; whole-project preservation and undo/redo are exact. All 616 Release core cases pass (13.95 s); seven focused dynamics cases pass Release/Debug (0.77/0.61 s). Native/core/focused builds and diff check pass; inspected 480×320 warning/count render fits without overlap. This is scope feedback and compiled-control evidence, not generated/target/measured visualization, ownership editing, live acoustic/host or Beta GO qualification. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; U25 remains incomplete.

2026-09-08 — U25 dynamics audio/persistence integration: added graphical/field native editing → explicit draft/region Apply → persisted recipe-file resolution → Final rendering → project reload → committed Float32 master/stem export regression. Draft PCM remains unchanged; gain 0.25 produces expected quarter-amplitude samples and one-sixteenth energy within explicit tolerances, while preserving vibrato/project intent. Reload/export and undo/redo reproduce exact expected PCM. Extended embedded gesture regression through the CLAP editor-state codec and a fresh runtime: draft bytes stay unchanged and applied dynamics round-trip exactly. All 615 Release core cases pass (28.03 s); 22 export cases pass Release/Debug (5.01/23.50 s); scoped builds and diff check pass. Evidence is procedural constant-curve audio and codec/runtime persistence, not live playback/listening, classical/neural parity, arbitrary multi-point acoustic qualification or real host state-stream/OS matrix approval. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 graphical dynamics editing: added distinct applied-score/staged-curve/unsaved-point plot, three-row paging, graphical gain handles with nearest-point hit testing, exact keyboard/semantic field access and explicit point/region commit stages. Dragging clamps native gain and rejects stale documents, nonfinite vertical coordinates and changed geometry; missing held-button state retires a lost drag. Fixed embedded review routing to forward move/up while preserving background technical-edit isolation; added an in-process embedded gesture/Apply regression. All 614 Release core cases pass (13.39 s); six focused dynamics workflow cases pass Release/Debug (0.40/0.50 s). Release native app/core and Release/Debug focused builds pass; diff check passes. Inspected 480×320 raster fits point rows, legend, curve and actions without overlap. Actual generated/target/measured curves, horizontal/time navigation, this workflow's audio/persistence/plugin-state and live host qualification remain open. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 connected dynamics point inspector: added compact region-curve list and separate tick/gain fields, draft add/move/delete, explicit Save to draft then Apply region curve, Back/Cancel/Refresh, stale input/action guards and one exact undo/redo. macOS Edit menu and shared track-inspector pointer/semantic entry are wired; the latter retains the existing character-Off/available-dock visibility policy. Added planned `test_dynamics_lane_workflow.cpp` with three focused native cases and standalone menu-dispatch coverage. Final Release core rerun passes all 610 cases (20.80 s); three focused cases pass Release/Debug (0.61/5.29 s). Earlier in this checkpoint 33 lifecycle cases pass Release/Debug (0.30/1.22 s). Release native app/core, Release/Debug focused builds and diff checks pass. Inspected 480×320 raster has no point-row/action overlap. This is point-form/controller evidence, not graphical handles, full curve visualization, live IME/VoiceOver, audio/persistence/plugin-state or host qualification. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 dynamics lane model: added captured region-scoped add/update/move/delete/reset drafts with atomic collision/invalid-input rejection, domain capacity/interpolation semantics, guarded explicit Apply and one exact undo/redo command. Note selection does not redefine the continuous region curve; generated takes and ownership are not implicitly replaced. Two new cases exercise draft isolation, interpolation, invalid/missing/colliding points, full-capacity moves, reset/no-op, stale generation, cancellation and whole-project preservation. All 607 Release core cases pass (25.93 s); 30 creator cases pass Release/Debug (1.24/6.84 s); builds and diff check pass. Native lane handles, semantic/input integration and this path's audio/persistence/host qualification remain open. Details: `DYNAMICS_LANE_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 vibrato audio/persistence integration: added native seven-field editing → saved recipe-file resolution → Final procedural rendering → project save/reload → committed Float32 master/stem export regression. Drafts leave PCM unchanged; Apply changes expected vibrato only and materially changes finite same-length audio. Reload/WAV export reproduce edited PCM exactly, and undo/redo reproduce original/edited projects and PCM exactly. All 605 Release core cases pass (13.50 s); 21 export cases pass Release/Debug (3.11/9.70 s), builds/diff checks pass. This is a procedural fixture and codec/export path, not physical playback, listener/singer qualification, native file dialogs or plugin/host parity. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 native vibrato form: connected the captured draft to an expanded right-anchored inspector, existing vocal-inspector entry and Edit menu command. Seven fields use two pages and the shared native field editor; input changes drafts only, with mixed placeholders, invalid-fade recovery, selected-field reset, Refresh/Cancel and one explicit Apply to Selection. Shared review geometry supports 480×320 and normal dock sizes without changing other review layouts. Added source/selection/generation and semantic-interaction checks; keyboard test uses emitted row IDs and captures an enabled old Apply target to prove stale rejection. All 604 Release core cases pass (13.63 s); focused inspector tests pass Release/Debug, strict builds/diff checks pass. Inspected 480×320 render `/tmp/seam-vibrato-ui.zEIyDk/inspector.png`; shortest round-trippable numbers avoid unnecessary float digits. Direct handles/curve layers, capability feedback, live IME/host and audio/persistence qualification remain open. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 existing inspector geometry repair: aligned painting, pointer routing and semantics through shared inspector visibility/top calculation, full dock-bottom overlay accounting and visible track/region bounds. Hidden inspector controls no longer remain actionable. Corrected top-left-versus-baseline hit-box offsets and split painted Mute/Solo labels into their actual target columns. Native tests verify short/normal heights with diagnostics, both pointer toggles with exact undo, and crowded-dock rejection of hidden targets. All 602 Release core cases pass (13.17 s); strict Release app/core and Debug native-UI builds/diff checks pass. Inspected system-font capture `/tmp/seam-inspector-layout.DhI155/inspector.png`. Existing visibility policy is retained; long-list reveal/scroll, vibrato field rendering/controller integration and full live/host qualification remain open. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 inspector draft/input state: added seven-field vibrato form state with explicit mixed placeholders, exact float text round-trips, bounded full-value parsing, retained invalid drafts and coupled-fade correction before Apply. Field reset preserves source-specific values; active region, selection, revision/generation and closed-state guards prevent stale publication. Two focused inspector cases pass Release/Debug (0.57/1.00 s); all 600 Release core cases pass (13.21 s), strict builds and diff checks pass. This is the native inspector's draft model, not painted/controller-connected controls. Geometry, semantic/IME/host/audio integration and full U25 acceptance remain open. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no Beta GO acceptance.

2026-09-08 — U25 large-selection performance editing: added exact 10000-note vibrato Apply/undo/redo regression, measured repeated note scans, then indexed requested expression targets per live/staged command state. Local Release Apply decreased 105.08→4.52 ms; no canonical validation, hint reconciliation or undo behavior was bypassed. Missing/repeated/ambiguous requested IDs reject atomically. All 598 Release core cases pass (12.97 s); 28 creator cases pass Release/Debug and 18/5 dedicated performance-command/edit-preservation cases pass. Strict builds/diff checks pass. Native inspector/handles, audible/persistence/host and worst-case latency qualification remain open. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — U25 mixed-selection vibrato model: added source-bound inspection and partial-field patches for every canonical vibrato parameter. Mixed values remain distinct; absent patch fields preserve each note's settings. Per-target domain validation rejects coupled fade conflicts atomically. Explicit Apply uses one canonical performance command and preserves hints, ownership, pitch/dynamics, unit/seam edits and unselected notes; undo/redo, no-op, selection/generation drift and cancellation are covered. Compiler regression confirms changed modulation/frequency inside the edited note, not acoustic acceptance. All 596 Release core cases pass (13.25 s); 26 focused cases pass Release/Debug (1.27/4.16 s), strict builds and diff checks pass. Native controls/handles, host/audio qualification and maximum-size Apply latency remain open; U23/U24 prerequisites are not implicitly accepted. Details: `VIBRATO_EDITING_MODEL_2026-09-08.md`. Changes local/uncommitted; no U25/Beta GO acceptance.

2026-09-08 — Broad Release integration refresh: rebuilt all default targets (86 steps) and ran all 90 CTest targets. Initial run passed 88 and exposed five performance-snapshot fixture mismatches plus source closure. The shared fixture retained an explicit `a` hint while tests changed surface lyrics; corrected CV/multi-nucleus input setup without weakening renderer guards, added positive hint-precedence checks and strengthened a masked continuation test with an isolated-phrase counterexample. All 41 snapshot cases pass Release/Debug. Final full rerun passes 89/90 targets in 54.98 s: the sole failure is source closure for 146 unindexed required inputs. All functional targets, 595 core cases, singing-quality workflow, allocation/recovery/export/migration and CLAP host smoke pass together. No staging, commit, exclusions or gate changes. Native GUI tests are disabled and no release acceptance is implied. Details: `BROAD_RELEASE_REGRESSION_2026-09-07.md`. Changes local/uncommitted; full goal remains active.

2026-09-08 — Long diagnostic failure preservation: replaced render-message concatenation into a 128-byte key with stable keys plus a 4096-byte display-safe detail field. Generic error mapping now retains detail too. UTF-8 boundaries, visible byte escapes and truncation flags preserve readable bounded output; original-input digests prevent different truncated tails from coalescing. Detail participates in source identity/search and native inspection/explicit Copy Diagnostic, but the automatic support-field allowlist is unchanged. Regression covers boundaries, malformed bytes, long failure visibility/search, distinct aggregation/staleness and native reconstruction. All 595 Release core cases pass (13.56 s); 13 focused diagnostic cases pass Release/Debug (0.39/0.99 s), strict Release app/core and Debug diagnostic/native builds and diff checks pass. Hashing remains proportional to supplied text; full producer inventory, live copy/support/IME/host qualification and release gates remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 native active-diagnostic Find: connected background diagnostic search to a dedicated Edit entry and the shared Find field cycle. Global issues work without a vocal region. Paginated full inspection exposes scope/references/severity/count/actions as read-only text, with keyboard/accessibility input, stale source/document guards, Refresh and cancellation; no note selection, recovery execution or history mutation. Tests cover global and long-CJK cases, result/detail pages, independent diagnostic drift, document replacement and no callbacks. All 593 Release core cases pass (13.74 s); strict Release app/core and Debug native-UI/platform builds and diff checks pass. Inspected system-font 480×320 capture `/tmp/seam-diagnostic-find.wdVito/detail.png`; trimmed paint-only line terminators to remove misleading ellipses while retaining full semantic text. Producer-wide diagnostic completeness/scope metadata, live menu/IME/host qualification and worst-case latency remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 diagnostic-search job/review: added bounded entry capture without panel callbacks, a single immutable-data worker, cancellation/discard, retire-before-restart admission and owner-thread publication. Reviews retain opaque document generation/revision and diagnostic content/counts; guarded lookup returns a diagnostic index only, rejecting stale/closed/missing targets without invoking recovery or changing notes. Tests cover lifecycle, independent diagnostic drift, same-content document replacement, score edits, invalid superseding input and worker UTF-8 failure. All 591 Release core cases pass (13.19 s); 11 focused diagnostic cases pass Release/Debug (0.66/1.21 s), strict builds and diff checks pass. Native Find controls/result presentation/navigation, interaction-ID guards and worst-case capture/publication/host qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 read-only active-diagnostic search model: added bounded immutable diagnostic snapshots with independent field matching, Unicode offsets and source/field provenance. Global/opaque-ID issues remain diagnostics, not guessed note targets; search never executes recovery actions. Full-content/count matching detects diagnostic changes independently of score revision. Tests cover visible text/codes/actions/CJK, source preservation, count/dismissal drift, cancellation and malformed/oversized later fields even after an early hit. All 589 Release core cases pass (13.29 s); the new 9-case focused diagnostic suite passes Release/Debug (4.95/5.46 s), strict builds and diff checks pass. Native Find integration, worker preparation, document-generation guards and scope-aware navigation remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — Active-diagnostic integrity prerequisite: source inspection found native/runtime code+message-only coalescing could lose different affected scopes, severity and recovery actions. Added shared exact-content diagnostic identity and saturating occurrence addition, used by both authoring-runtime producers and the native panel. Regression verifies separate scope/severity/action records, correct recovery target, blocked action rejection, critical visibility/dismissal and no overflow. All 587 Release core cases pass (13.09 s); strict Release app/core and Debug native-UI/runtime builds and diff checks pass. Full active-diagnostic Find remains open: opaque/unscoped IDs cannot safely be promoted to note targets, and diagnostic-state changes need separate snapshot validation. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 bounded 10000-note derived Find: expanded indexed shared resolution to 10000 notes/lyrics and 65536 output phones while retaining independent per-lyric/aggregate text and override limits. The adapter checks its supplied token budget before appending each note's output. Model regression resolves 9999 Japanese CV notes plus contextual continuation into 19999 phones, searches all 10000 notes and transfers the async result without changing song/history; 10001-note and 80000-phone expansion inputs reject. Native regression finds the uniquely hinted final note in a 10000-note region, inspects it, selects/reveals it and restores semantic focus with exact project preservation. All 585 Release core cases pass (12.64 s); 25 focused cases pass Release/Debug (1.52/4.41 s). Rebuilt compiler/reconciliation/edit-preservation suites pass 17/14/5 cases; strict builds and diff checks pass. One resolution plus two searches measured 317.70 ms Release / 2089.38 ms Debug locally, not universal latency or renderer qualification. Full diagnostic coverage, worst-case responsiveness and live language/IME/host/release gates remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 resolver cancellation/indexing prerequisite: propagated Find stop tokens through shared Japanese resolution, recursive base resolution and a cancellable kana-adapter overload, with cooperative checks during indexing, per-note phases and character generation. Indexed lyric/note lookups and per-note overrides remove repeated whole-region scans while preserving edit order and continuation behavior. Existing interface delegates with a non-cancelled token. New regression verifies output/identity parity, append/replace order feeding continuation, pre-cancellation and unchanged source. All 583 Release core cases pass (12.48 s); 24 focused cases pass Release/Debug (0.83/0.92 s). Rebuilt Release performance compiler, override reconciliation and edit-preservation suites pass all 17/14/5 cases. Strict Release app/core and Debug focused builds and diff checks pass. Existing collection/text/token limits remain unchanged: 10000-note derived capacity and worst-case cancellation latency are still open, as are live-language/host release gates. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 asynchronous native Find: added a single-worker immutable-snapshot search job with owner-thread result publication/source validation, exception reporting, nonblocking cancellation requests and retire-before-restart admission. Native preparing state enables only Close, cancelled results cannot revive the panel, and field/result controls become available only after validated publication. Both native paint paths use the shared poller; mode switches request cancellation. Model/native regressions cover 10000-note results, cancelled/stale/replaced sources, resolver failure, invalid superseding input, pending controls and Refresh recovery. All 582 Release core cases pass (12.53 s); 23 focused cases pass Release/Debug (0.87/0.95 s), strict builds and diff checks pass. Inspected 480×320 preparing render `/tmp/seam-find-async.mhFhpX/preparing.png`; corrected inherited replacement wording. Snapshot capture, publication validation and detail wrapping remain synchronous; resolver mid-call cancellation, worst-case latency and live-host/IME/derived-capacity/full-diagnostic qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 native repeat Find: retained the reviewed result cursor after explicit selection and added Find Next/Previous application commands plus macOS Command-G/Command-Shift-G menu items. Both directions wrap through captured matches and reuse selection/reveal/accessibility-focus logic without changing song/history. Composition/review/dialog/drag guards and source revision/generation validation reject unsafe repetition. Native regression verifies both wrap directions, off-screen/low-pitch reveal, focus, cancellation/resume, source replacement and zero document notifications; dispatcher checks both routes. All 580 Release core cases pass (12.61 s); strict Release app/core and Debug native-UI/platform builds and diff checks pass. Live shortcut/non-Latin layout/cross-host, async Find, full diagnostics and derived-capacity qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 native Find entry/results: added macOS Edit Find Notes/Command-F routing, captured query input without replacement, five-field cycling, paged results and complete 32-column/six-line field-text inspection. Explicit Select and Reveal uses guarded selection, pans to the note and restores note accessibility focus without modifying project/history. Escape backs out of detail without selecting. Regression covers 400-character CJK reconstruction, field selection, pagination, semantic/keyboard input, stale callbacks/revisions, replaced documents, error-versus-empty states and exact preservation. All 579 Release core cases pass (12.20 s); strict Release app/core and Debug native-UI/platform builds and diff checks pass. Inspected system-font 480×320 capture `/tmp/seam-find-native.AOusbU/detail.png`: text/actions fit and focus defaults to Back to Results. Live menu/IME/screen-reader/cross-host, async preparation, repeat-match shortcuts, full diagnostics and 10000-note derived capacity remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 captured Find navigation: added immutable source-bound results with explicit selection, forward/backward wrap, closed/cancelled/stale rejection and opaque document-generation validation. Navigation changes only selection, not musical data or undo history. Tests cover normal/empty/stale/replaced-document cases and 10000-note capture plus three selections (5.04 ms Release / 34.86 ms Debug locally, not a universal latency guarantee). All 577 Release core cases pass (11.95 s); 22 focused cases pass Release/Debug (0.96/0.94 s), strict builds and diff checks pass. Native general Find menu/input/results, focus/reveal and async preparation remain unconnected; derived-capacity/full diagnostic/multilingual requirements remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 additional search fields: added canonical note-ID, shared-resolver generated-phone and note-associated pronunciation-warning search, retaining independent lyric/hint fields and stable tick/ID ordering. Derived text has cancellation/output bounds; resolver failures propagate explicitly. Tests cover hinted phones, warning ownership, exact offsets, source preservation, cancellation and capacity errors. All 576 Release core cases pass (12.49 s); 21 focused cases pass Release/Debug (0.85/0.84 s), Release app/core and focused builds pass, and diff checks pass. The existing resolver's 4096-note cap is not raised: 10000-note derived capacity, native general Find/navigation, complete diagnostic aggregation and multilingual/live qualification remain required. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 region-native-dynamics clear: added an explicitly whole-region captured preview, native paged review and macOS Edit command. One canonical command clears only the native curve; generated Dynamics takes/selections and unrelated intent remain unchanged, with an explicit warning that generated dynamics may still apply. Model/native regressions cover exact preservation/undo, stale generation/revision, cancellation, no-op, pagination and selection-independent region scope at 480×320. Strict Release/Debug builds and 20 focused cases pass; core verification contains 575 cases. Selected-note-only reset, live/cross-host/audio and worst-case latency qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 auto-legato implementation: added selected-adjacent/same-group/one-grid-gap policy with staccato, explicit separation, unselected-neighbor and nested-overlap safeguards. Eligible geometry and both-end articulation changes share one canonical composite/undo group and dependency dry run. macOS Edit and native review expose the mode and endpoint changes. All 573 Release core cases pass (13.22 s); 19 focused cases pass Release/Debug, strict builds/diff checks pass. Inspected system-font review render; live/audio/large-selection qualification remains open, as does clear-dynamics. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — Shared review short-window repair: replaced fixed-height/off-screen placement with available-height panel sizing and adaptive row spacing while preserving action heights. All rows/actions fit without overlap at four supported sizes, including 480×320; semantic bounds and pointer Cancel are tested. Inspected a system-font minimum-size review capture. All 571 Release core cases pass (12.15 s); Release app/Debug native-UI builds and diff checks pass. Live IME resizing and full embedded/minimum-size qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24/Beta GO acceptance.

2026-09-08 — U24 native cleanup integration: added distinct macOS Remove Selected Overlaps/Close Selected Gaps commands and shared review modes with note outcomes, skipped reasons, dependency pages, captured grid, explicit Apply/Cancel/Refresh and index rebuilding. All 570 Release core cases pass (12.88 s); 17 focused cases pass Release/Debug, strict builds/diff checks pass. Inspected both 720×520 system-font renders. Live menu/unselected-neighbor/no-op rejection passed; box selection automation failed, so successful two-note live Apply remains unqualified. Owned test process exited 0/nonphysical; a later different session was left untouched. Full small-window/host/acoustic/latency acceptance and other actions remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 cleanup dependency preflight: cleanup preparation now runs canonical resizing on a private project and captures phone/unit/seam unresolved outcomes before publication. Shared bounded/cancellable per-key comparison replaces duplicated lyric-review logic and rejects duplicate source/result keys. A Japanese shared-lyric overlap regression verifies previewed continuation-related unresolved records match actual Apply and exact undo. All 569 Release core cases pass (12.68 s); 17 focused cases pass Release/Debug, strict builds/diff checks pass. Native cleanup presentation and worst-case preparation/audio/host qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 duration-cleanup previews: added Remove Overlap/Close Gap planning with per-note outcomes, adjacent-selected policy, fixed starts, downward overlap snapping and on-grid gap closure. Ambiguous starts, staccato gap extension, unselected neighbors and nonpositive durations are reported/skipped. Captured grid/selection/generation checks and canonical resize publication preserve exact undo/no-op behavior. All 567 Release core cases pass (11.87 s); 15 focused cases pass Release/Debug, strict builds/diff checks pass. Native review/menu/dependency diagnostics, auto-legato, clear-dynamics and large-selection qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 native clear-vibrato action: added macOS Edit command and shared review mode with paged targets/counts, explicit Apply/Cancel/Refresh, stale-selection guards and disabled no-op Apply. All 565 Release core cases pass (13.29 s); 13 focused cases pass Release/Debug, strict builds/diff checks pass. Live temporary-project menu→Apply→reopen-zero→undo→reopen-one flow passed; discard exited 0 with unchanged input hash and no physical audio. System-font 720×520 review inspected. Cross-host/acoustic/large-selection qualification and other creator actions remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 creator reset foundation: checked the referenced creator-plan requirements and added captured clear-vibrato preview/guarded application. It disables modulation while preserving settings and all unrelated musical data, uses the canonical performance command/receipt, and rejects stale/cancelled/reused targets. Full-project equality regression covers retained pitch/dynamics/hints/ownership/unit/seam data and unselected vibrato. All 564 Release core cases pass (14.69 s); 13 focused cases pass Release/Debug and strict builds/diff checks pass. Native clear-vibrato integration, dynamics/geometry actions and live/large-selection qualification remain open. Details: `CREATOR_BATCH_ACTIONS_2026-09-08.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 reviewed distribution: extracted a shared read-only plan, added captured-selection distribution previews, and routed native batch submission through background review instead of immediate mutation. Explicit Apply/Cancel/Refresh, dependency/detail pages and exact undo are connected; mismatches show separate counts with Apply disabled. Added a semantic status/count child after live inspection found root-only text omitted by bridges. Final Release core passes 563 cases (12.02 s); 12 focused cases pass Release/Debug, strict builds/diff checks pass. Live Shift+L mismatch/review/Apply/undo/discard sequence passed with unchanged fixture hash and exit 0. Full IME/hint search/normalization/reset/cross-host acceptance remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 distribution semantics: default language is now preserved per token; explicit language/reset remains supported. Fully selected shared lyric groups receive one syllable/edit per distinct token; partial groups reject instead of modifying unselected notes. Reports distinguish notes/targets/changes, and no-op requests add no history/notification. Added admission/Unicode bounds and indexed lookup. All 561 Release core cases pass (11.99 s); 11 focused cases pass Release/Debug, including 10000-note distribution/undo (~4.24/35.54 ms local). Native app/Debug UI builds and diff checks pass. Reviewed distribution and live language/host qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 batch target safety: replaced boolean batch-input mode with captured document generation/revision/region/selected IDs and validates them before distribution. Selection/source drift cannot retarget input; equivalent selection ordering remains valid. Added bounded all-in-region admission, indexed membership, composition-key isolation and no lyric navigation after batch Tab completion. All 558 Release core cases pass (12.62 s); Release app/Debug native-UI builds and diff checks pass. Reviewed distribution/shared-lyric/language-policy and live IME qualification remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 long-row visual review: result rows open paged complete Before/After text with context-specific actions and semantic IDs. Back/Escape cannot apply; returning to results restores explicit Apply. Added bounded cluster-aware UTF-8 byte-range wrapping with exact reconstruction/error tests. All 557 Release core cases pass (12.48 s); Release app/Debug native-UI builds and diff checks pass. Inspected system-font 720×520 Japanese detail raster; live/script conformance and worst-case detail preparation latency remain unqualified. U24/Beta GO still open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-08 — U24 native text maintenance: implemented responder-chain clipboard commands for active custom AppKit text input and changed left/right movement to composed-character boundaries. Release app/core and Debug native-UI/platform builds pass; all 555 existing core cases pass and diff checks pass. Live fixture test verifies emoji/combining-cluster navigation/deletion, cancellation, unchanged source hash and exit 0. The automation's direct non-ASCII typing still failed; paste/copy/cut execution and full IME qualification remain unverified, with no bypass introduced. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted; no U24 acceptance.

2026-09-08 — U24 native Find/Replace entry: macOS Edit command opens captured query/replacement stages, inline empty/length validation, guarded transition to background review and explicit-only application. Fixed nil accessibility values for empty editable fields in both AppKit bridges. All 555 Release core cases pass (11.57 s); strict Release app/Debug native-UI/platform/CLAP builds and diff checks pass. Live fixture workflow verified Japanese semantic query/replacement, dependency review, Apply, one-step undo and cancellation; source fixture hash unchanged and both sessions exited 0 without physical audio. Direct Japanese typing/IME, embedded/Windows live discovery, long-row expansion and remaining U24 acceptance still open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 native replacement review panel: connected background-job polling to standalone/embedded paint paths and implemented status/counts, six-row lyric/dependency pages, explicit Apply/Cancel/Refresh, isolated semantics and stale action IDs. Apply notifies once and forms one undo group; background input is guarded, including embedded technical-lane interception. Rebuilt Release core passes 554 cases (11.80 s); Release app/Debug native-UI builds and diff checks pass. Inspected 720×520 raster confirms row/button separation. Query/replacement text entry and menu opener remain unconnected; long-row expansion/live-host/multilingual qualification remains open. No U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 background review job: added immutable project/revision overloads and a single-worker preparation lifecycle using opaque editor generation context. Worker results are owner-polled and revalidated against current document/region; cancellation discards results without joining live work, and destruction joins safely. Replaced documents, live edits, duplicate apply and stale prior-query reuse reject. Eleven focused cases pass Release/Debug (0.77/1.07 s); rebuilt core passes 553 cases (12.30 s), strict builds/diff checks pass. Snapshot copy and canonical dry-run cancellation latency remain bounded only by current synchronous operations; no native worker/dialog adapter yet. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. No U24 acceptance; changes local/uncommitted.

2026-09-07 — U24 replacement-review model: added full-text six-row pagination, canonical private-copy lyric-command dry run and per-key before/after unresolved outcomes for phoneme/unit/seam records. Review application validates identity/revision/selected region/full source-region state; applied/cancelled instances cannot be reused. All 10 creator-batch cases pass Release/Debug (1.25/1.35 s); rebuilt core passes 552 cases (11.94 s), diff checks pass. 10000-note English review preparation measured 4.76/42.23 ms Release/Debug locally. Native worker/input/dialog integration and supported-language live workflows remain unfinished; no U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 macOS menu discovery: Edit now exposes Japanese pronunciation-hint entry through an explicit application command and shared selected-note opener. Callback errors propagate without document changes; menu rejection shows a native alert rather than only logging. All 550 Release core cases pass (12.44 s); Release app/Debug native-UI/platform builds and diff checks pass. Live exact-app verification observed the menu, empty-selection alert, OK dismissal and clean exit 0/revision 0 in disposable Untitled sessions. Successful live hint commit, Windows/embedded discovery and native search/replacement review remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. No U24 acceptance; changes local/uncommitted.

2026-09-07 — U24 active hint accessibility: added an isolated field/visible-Cancel semantic surface, bounded UTF-8 value submission, monotonic interaction IDs and stale/background callback rejection. Pointer/keyboard isolation prevents score shortcuts from mutating notes during hint composition. All 550 Release core cases pass (12.10 s; capture repeat 24.51 s); Release app/Debug native-UI builds and diff checks pass. Inspected 720×520 raster verifies distinct field/cancel bounds; live AppKit/VoiceOver and permanent inspector/menu opener remain unqualified/unimplemented. Native search/replacement review and U24 acceptance remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 native keyboard hint entry: Alt+Enter opens a distinct selected-note Japanese phone field; commit uses reconciled hint commands, empty clears, and Tab does not navigate lyrics. Captured project/region/revision/source-hint checks reject stale submissions; no-ops remain history/notification-neutral. Fixed the shared composition model's unconditional empty-text rejection with an explicit auxiliary-field opt-in, retaining nonempty lyrics by default. All 549 Release core cases pass (11.99 s); Release native app, strict Debug native-UI build and diff checks pass. Dedicated accessibility/inspector controls, live platform qualification and native search/replacement review remain open; no U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U24 mixed hint writer consolidation: EditPerformanceCommand now composes actual hint changes through the dedicated reconciler on private project/command state, preserving atomic coupled expressions and exact reverse-order undo. Combined ownership/pronunciation revisions are retained for redo; invalid hints cannot partially publish or capture history. Updated combined test and added mixed-ownership regression pass in Debug/Release. Native hint UI remains open; details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. No unit acceptance.

2026-09-07 — U24 dedicated hint command: added captured-value bounded hint batches with language-aware validation, stale/repeated-target rejection, phrase impact and shared staged dependency reconciliation. Hint changes now advance pronunciation state; old changed-phone edits remain unresolved rather than retargeting. Tests verify untouched displayed lyrics, resolved sh/a phones, exact undo/redo and clear/restore. Eight focused Debug/Release cases and 547 Release core cases pass (12.74 s); diff checks pass. Native hint UI and older mixed-command writer consolidation remain open. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`.

2026-09-07 — U24 Japanese hint semantics: discovered stored phonetic hints were ignored by the adapter. Added bounded explicit phone parsing from the built-in inventory, independent of displayed lyrics, with canonical validation and visible inspection errors. Hint presence/content enters resolver identity; resolver version advances to 2 and phonemizer revision to 3. Regression covers audible-input phone changes, unchanged lyric display, rejection and removal restoration. Release core/performance suites pass and Debug phonemizer builds. Undoable hint command reconciliation/native controls remain open; no U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`.

2026-09-07 — U24 10000-note replacement: added exact apply/one-undo/redo test and indexed requested lyric targets once per live/staged command state. Measured Release apply improves ~71.6→3.8 ms on the same English fixture; undo/redo ~67→3 ms. Staging/validation/reconciliation remain; ambiguous targeted IDs reject while unrelated reused IDs are preserved. Six focused cases pass in Debug/Release. Measurements are local observations, not universal latency or multilingual qualification. Native review/IME remains open; details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`.

2026-09-07 — U24 reviewed replacement API: immutable previews deduplicate shared lyric targets, retain exact before/after/language data and count affected notes/occurrences. Linear non-overlapping replacement has bounded output and rejects empty lyrics. Apply validates project/revision/original values then uses one canonical batch command; no-op previews add no history. Tests cover source/hint preservation, one undo/redo, stale/cancel rejection and expansion bounds. Native review, precise dependency diagnostics and 10000-note commit qualification remain open; no U24 acceptance. Details: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`.

2026-09-07 — U24 bounded note search: added read-only region search separating displayed lyrics and hints, exact scalar matching/offsets, linear prefix-table search, stable note ordering, cancellation checks and 10000-note/4M-text admission. Results retain source identities/text for later reviewed edits; no mutation is implemented yet. Regression covers Unicode/shared lyrics, hint isolation, 10000 notes, malformed/oversized input and source preservation. Focused Debug/Release suites pass; native preview/replacement and U24 acceptance remain open. Contract: `NOTE_SEARCH_AND_BATCH_EDITING_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — Full Release rerun after short-transition fixes: all default targets rebuilt (85 steps), then all 88 registered CTest targets ran in 62.46 s: 87 PASS, only tracked-source closure FAIL for unindexed local implementation files. Previously failing demo/schema/platform-contract and singing-quality bank/Raw workflow pass in the same run. No tests were excluded and no source-publication gate was changed. This is stronger integration evidence, not a project-completion percentage or Beta GO acceptance. Details: `BROAD_RELEASE_REGRESSION_2026-09-07.md`. Changes remain local/uncommitted.

2026-09-07 — Raw short-transition integration: mapped transient/release source positions while retaining ordinary/compiled Raw sustain pitch stepping, gain and loop print. Exact extent/vowel validation and no-sustain rejection remain; diagnostics disclose resampled transient pitch. Raw revision advances to 9. Multi-renderer and explicit root/octave phase-step/gain regressions pass; unchanged singing-quality workflow now passes both bank/Raw modes (20.74 s). Retained Raw unequal-rests output is 438000 frames/9.125 s, no clipping, actual Raw/no fallback. Acoustic release and broad-suite recheck remain open. Details: `SHORT_UNIT_TRANSITION_MAPPING_2026-09-07.md`.

2026-09-07 — Classical short-transition integration: production timing opts into tagged simple single-nucleus short placements; default/legacy paths still reject. Concatenative PSOLA/Spectral/Stretch consume bounded marker maps with compiled performance and no Raw fallback; timing revision advances to 11 for cache provenance. Tests verify finite/nonzero PCM and exact vowel alignment. Unchanged unequal-rests bank rendering now succeeds (438000 frames/9.125 s/no clipping), while Raw remains explicitly unsupported. Details: `SHORT_UNIT_TRANSITION_MAPPING_2026-09-07.md`. Full corpus/Raw/acoustic qualification and Beta GO remain open.

2026-09-07 — Short-unit mapping primitive: traced the unequal-rests failure to a 62.5 ms target versus ~70.11 ms source post-vowel transition. Added bounded marker-based compression for simple single-nucleus CV/sustain consumers, preserving vowel anchor/end while reserving transition/sustain/release frames. Multi-rate mapping/replay and invalid-source/rate/span regressions pass in Release. The solver guard and production renderer are intentionally unchanged pending integration; corpus failure remains open. Contract and next steps: `SHORT_UNIT_TRANSITION_MAPPING_2026-09-07.md`. No unit acceptance.

2026-09-07 — Broad Release regression: rebuilt all default targets (168 steps) and ran all 88 registered CTest targets: 82 passed, 6 failed (78.14 s). Repaired obsolete Phase12B schema/migration assertions, two punctuation-bound platform contracts and an unintended Phase2 demo consonant end override; focused reruns pass. Singing-quality unequal-rests rendering still rejects a short selected CV transition, reproduced with retained logs; Git source closure still reflects unindexed local work. No checks were bypassed and no all-green/Beta GO claim is made. Detailed failures and next synthesis target: `BROAD_RELEASE_REGRESSION_2026-09-07.md`.

2026-09-07 — U23 preservation-suite reconciliation: ran the previously core-excluded dedicated suite and found obsolete expectations for interleaved duplication/unresolved complete spans/reused slur IDs. Updated them to assert common-translation/new-identity behavior while retaining all expression, context and exact undo checks; added the five-case file to the core target. Debug/Release preservation suites pass; rebuilt Release core passes 538 cases (13.48 s), and the separate performance-command suite passes its partial-span/collision coverage. Details: `DESIGNER_HANDOFF_AND_PHRASE_DUPLICATION_2026-09-07.md`. Coverage progress only; no new unit acceptance.

2026-09-07 — U23 event selection retention: refresh matches the selected tick/type instead of reusing its index or jumping to row zero; deletion selects its chronological successor/end fallback and updates the page. Cross-project opening does not preserve the old selection. Regression covers index-shifting insertion, later-page retention, deletion successor and same-tick type identity. Freshness/initial-event guards remain. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. No new unit acceptance; changes local/uncommitted.

2026-09-07 — Embedded AppKit text parity: retired old native fields before runtime callbacks, guarded reentrant end-edit notifications, added explicit Enter/Tab/Backtab/Escape delegation and missing panel key mappings. Release plugin/host/core and Debug embedded builds pass; core 532 cases pass (12.01 s). Real Cocoa fixture host smoke exits 0 with visible GUI, nonzero note energy, offline-render acceptance and exact state round-trip. Interactive embedded text/IME and actual DAW qualification remain unverified; no U23 acceptance. Evidence/boundaries: `TEMPO_METER_COMMANDS_2026-09-07.md`.

2026-09-07 — Live AppKit keyboard repair: fixed absent Cmd+A select-all handling plus missing ASCII/Korean-fallback A mappings. Retired native input before commit/cancel callbacks so two-stage event insertion retains the successor field. Final live typed 123.75 initial BPM, typed tick1920→90.5 insertion and Escape-cancelled 7/8 meter stage pass; normal discard of only disposable Untitled exits 0. Release core passes 532 cases (12.11 s); Release standalone/Debug native UI builds and diff checks pass. General IME/VoiceOver/embedded/physical playback and full U23/Beta GO remain open. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`.

2026-09-07 — Fixed-input live recheck: rebuilt AppKit app visibly paints time-map input; live semantic tempo 90.5/tick1920 and meter7/8/tick1920 insertion, refresh, isolated meter removal and normal-tree restoration pass. Ordinary test-window close shows Save/Discard/Cancel; discarding the disposable Untitled document exits 0. Release core passes 532 cases (11.74 s); Release standalone/Debug native UI builds and diff checks pass. Full int64 renderer capture fits. No physical audio, keyboard/IME or VoiceOver qualification is claimed; U23/Beta GO remain incomplete. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`.

2026-09-07 — Live time-map QA found missing painted input: AppKit semantic open/Add Tempo/tick transition worked, but screenshots showed static toolbar values rather than composition text. Fixed scene generation to supply shared explicit time-map input bounds and a dedicated active flag, and paint bounded input after the panel. Added geometry/value/cancel/19-digit regression. Original disposable test app exited 0 at its deadline with nonphysical/no-frame audio. Further evidence: `TEMPO_METER_COMMANDS_2026-09-07.md`. Live recheck and U23 acceptance remain pending; changes local/uncommitted.

2026-09-07 — U23 panel accessibility: added semantic opener and dedicated event-panel tree, row/action availability, active tick/value fields, cancellation and panel-confined Tab/Enter routing. IDs bind document/snapshot/page/selection/stage plus monotonic interaction identity; cancelled and prior-stage targets cannot revive. Dispatch explicitly rejects unavailable materialized controls and background actions. Added full semantic insertion/cancellation regression. Live VoiceOver/platform qualification remains open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 panel insertion: added visible ADD TEMPO/METER and N/Shift+N actions with tick→value native composition, stage-specific prompts, cancellation, exact bounded int64 parsing and collision/stale checks before both stages. Tick entry is nonmutating; final insertion uses one canonical command and never replaces an existing same-type event. Tests cover cancellation, same-tick independent kinds, between-stage changes and invalid ticks. Panel accessibility/live qualification remain open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 visible event panel: added TIME MAP entry button, centered six-row painting, pointer/keyboard selection, paging, edit/remove/refresh/close actions and explicit stale state. Selected removal refreshes only after canonical commit; other stale lists reject. Background pointer-down/keyboard/scroll/accessibility actions are blocked while the panel is open. Added interaction regression and optional deterministic capture. Insertion controls, panel accessibility and live platform qualification remain open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 event-list model/controller: added bounded chronological tempo/meter snapshots, six-row paging, protected initial rows and stable tick/type targets. Native selected-edit/removal dispatch checks project identity/revision/both map contents before canonical commands; stale lists require recapture. Regression covers interleaved pages, invalid indices, same-tick identity, edit/remove/undo and out-of-band rejection. Visible list/refresh UX and live qualification remain open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 slur edge repair: guarded monotonic group allocation against UINT64_MAX wraparound before command execution. Regression verifies mixed disable preserves non-Legato articulation/lyrics/unselected notes, failed allocation preserves state/revision/history, undo/redo restores exact state, and selected existing groups can still be extended without allocation. Existing group-extension semantics remain unchanged and documented. Details: `DESIGNER_HANDOFF_AND_PHRASE_DUPLICATION_2026-09-07.md`. U23/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — U23 Final-render/export evidence: added integration regression driving native tempo/meter text commits through canonical commands, Final procedural rendering, project save/reopen and committed Float32 master/stem export. Tempo change inside a sustained note changes 48000→72000 frames with new phrase identity; meter preserves this fixture's PCM; undo/redo restores exact original/slowed samples. Musical geometry remains unchanged. This validates offline integration, not live GUI/background scheduling/device playback or full U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 native meter input: split the existing BPM area into nonoverlapping tempo/meter subfields and added meter painting, hit testing and editable semantics. Bounded signature parsing validates integers before narrowing; captured revision/tick/kind prevents cross-control retargeting. Commit uses the canonical meter command with shared cancellation and text-target reset. Tests cover application/undo, invalid signatures, nonzero targeting, isolation and layout geometry. Event-list/removal UI and live qualification remain open; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 exact tempo/event context: fixed six-decimal prefill quantization using shortest-round-trip formatting and shared bounded BPM parsing. Native tempo input now captures an explicit event tick plus revision; nonzero commit inserts/replaces that event, and initial-toolbar accessibility cannot retarget an open nonzero event. Regression covers precision, invalid input, captured-target preservation and undo/redo. Visible event selection/meter controls and live qualification remain open; no unit acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 visible initial-tempo input: connected BPM hit bounds and accessible text-field activation/value assignment to native composition and revision-guarded commands. Decimal input, Escape cancellation, Tab non-lyric completion, stale/invalid rejection and target reset when switching text workflows are implemented. Added native regression; arbitrary event/meter controls and live runtime qualification remain open. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. No unit acceptance or release publication; changes local/uncommitted.

2026-09-07 — U23 native tempo/meter dispatch: added revision-bound controller entrypoints using shared commands and success-only document-change/repaint notifications. Stale context, composition, drag and phoneme-review guards prevent background mutations. New native regression covers notification counts, stale/invalid requests, composition preservation and removal/undo. Existing standalone/embedded callbacks route to authoring runtime. Visible tempo/meter input remains to be connected; no U23 acceptance. Details: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 transactional tempo/meter commands: added optional-value insert/replace/remove commands using validated map copies, complete before/after history, stale-map rejection and project-wide audio impact. Tests cover sustained-note sample mappings, exact undo/redo, captured worker invalidation, meter/bar changes, invalid inputs and atomic rejection. Native tempo/meter controls and actual playback/export integration remain open; U23/Beta GO are not accepted. Contract: `TEMPO_METER_COMMANDS_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — U23 duplicate relationship preservation: copied notes now map shared source syllables to one fresh lyric token and source slur groups to fresh region-local IDs. Explicit AddNote ReuseExact mode validates the captured existing lyric and leaves ownership with its creator, preserving reverse-order composite undo/redo without weakening ordinary collision rejection. Added pronunciation/identity/undo and shared-token admission regressions. This completes the relationship repair after common phrase translation, not full U23; owned-edit edge coverage, slur edge cases and tempo/meter commands remain. Details: `DESIGNER_HANDOFF_AND_PHRASE_DUPLICATION_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — Designer handoff verification and U23 phrase translation: verified the UI-captured 0.25 recipe survives frozen preparation without changing its 0.05 source or plain score. CLI rendering/collection reached generation 3 with one unapproved MarkerReview take; retry returns AlreadyCollected. Fixed stale AppKit Designer accessibility children on mode exit (live checked) and duplicate save-panel extension (built, default not live checked). GUI-only completion remains unverified after the Mac locked. Continued U23 by replacing per-note-duration duplicate offsets with one overflow-checked phrase translation; regressions cover unequal durations/rests, selection order, undo/redo and atomic arithmetic rejection. Debug/Release focused four-case suites pass. Details and remaining scope: `DESIGNER_HANDOFF_AND_PHRASE_DUPLICATION_2026-09-07.md`. No additional unit acceptance; changes remain local/uncommitted.

2026-09-07 — Duplicate-pose modal source guard: inspection found that changing audition pose does not change recipe revision, so a naming dialog could copy a different selected pose after returning. The native flow now captures and supplies an expected source index; the session rejects mismatch before mutation. Regression verifies unchanged-revision selection changes, rejected stale source, correct copied resonances and undo. All 23 Designer tests and both Studio builds pass in Debug/Release; diff checks pass. This is cross-cutting safety maintenance, not new unit acceptance. Full U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Height-aware Designer layout: shared 1–6-row viewport and footer geometry now drives painting, pointer hit testing and accessibility; compact status/error rendering avoids overlap and secondary commands retain keyboard/semantic access. Resize cancels an active drag before changing geometry. Geometry tests cover 320–900 heights and up to 224 controls; all 22 Designer tests pass in Debug/Release (2.71/1.10 seconds), both Studio builds pass and diff checks pass. Live supported-minimum 720×520 checks show controls/help/error fitting and invalid edits preserving SAVED; close exits 0 with unchanged producer/no recording. A 720×320 launch was correctly rejected under Studio's existing minimum. Details: `VOICE_DESIGNER_COMPACT_LAYOUT_2026-09-07.md`. Broader layout/accessibility and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Dedicated Designer frication audition: added direct seeded-noise preview with shared bounded/faded output finalization, separate ready buffer and shared cancellation/epoch/revision worker guards. Cmd/Ctrl+Space and source-index semantic actions render/play selected noise; vowel/reference buffers and A/B semantics stay separate. Tests verify deterministic finite/non-silent bounded PCM, source-seed effects, invalid/stale/cancel behavior and vowel/reference preservation. All twenty-one Designer tests and both Studio builds pass in Debug/Release; diff checks pass. New UI/device interaction and acoustic qualification remain live-unverified; full U20/U22/Beta GO remains incomplete. Changes local/uncommitted.

2026-09-07 — Frication lifecycle controls: added undoable source-index removal and exact per-source seed editing via Cmd/Ctrl+Shift+E/R plus semantic actions. Source-index/context binding prevents implicit retargeting; global seed and unrelated sources are preserved. Global/source seeds share canonical unsigned parsing. Added regression verifies precision, isolation, invalid/stale/index rejection and exact undo restoration across seed/removal changes. All twenty Designer tests and both Studio builds pass in Debug/Release; diff checks pass. New source UI/dialog interaction, dedicated frication audition and full U20/U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer frication authoring: added Cmd/Ctrl+E/semantic creation with phone/style validation and one-time global-seed initialization, plus selected-style center/bandwidth/gain rows through keyboard/drag/accessibility edits. Existing canonical validation rejects duplicate/missing-style/invalid filters; edits are undoable and change frozen recipe identity. Tests verify creation, seed/version identity, parameter-dependent source PCM, invalid-state preservation and undo. Nineteen Designer tests and both Studio builds pass in Debug/Release; diff checks pass. New interaction remains live-unverified; oral audition excludes frication, and dedicated preview/removal/seed controls plus full U20/U22/Beta GO qualification remain incomplete. Changes local/uncommitted.

2026-09-07 — Exact Designer seed control: added canonical unsigned-64-bit decimal parsing, Cmd/Ctrl+R AppKit dialog and semantic text-field editing without float narrowing. Accepted edits use existing undo/preview invalidation; unchanged/invalid/stale inputs preserve valid state. The seed affects phonation noise and modulation phase, not independent frication seeds. Eighteen focused Designer tests pass in Debug/Release, including UINT64_MAX, overflow/malformed rejection, no-op preservation and PCM/undo reproducibility; both Studio builds pass and diff checks pass. New dialog/header live checks, non-AppKit dialog support and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer periodic modulation controls: exposed pitch depth, amplitude depth and rate through keyboard/drag/accessibility edits, preserving canonical validation, undo and preview invalidation. Labels reflect the existing sinusoidal DSP rather than claiming stochastic/measured jitter/shimmer; zero rate is explicitly off. Added regression verifies separate depth effects on PCM, exact undo restoration, zero-rate neutrality and invalid-rate preservation. Both Studio builds pass; focused Designer suites were rerun in Debug/Release. Live new-control/acoustic qualification and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Visible prepare-from-Designer action: Cmd/Ctrl+Shift+P and a semantic action capture current draft resource/style, validate Designer/producer modal context and start background score inspection, returning to producer view. The score selection owns the frozen recipe and can discover plain-score regions; region/destination steps pass that exact selection to preparation with explicit snapshot labeling. No saved files, producer state or old jobs are rewritten/recaptured. Added controller test verifies discovery→selection→preparation→reload with the exact draft hash and unchanged producer state. Both Studio builds pass; focused integration suites were rerun in Debug/Release. New handoff remains live-unverified and full U21/U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Explicit Designer recipe input to preparation: shared saved-score preparation and the native worker now accept an owned frozen resource/style selection. Only the job's decoded score copy changes; source score/recipe files and producer state remain untouched. Original score-digest, assignment, pitch and producer expectation checks remain; this does not refresh existing jobs. Integration tests verify exact selected recipe identity across preparation/reload/render, changed PCM, source preservation, invalid-style/digest rejection and explicit selection for a score without a saved recipe. Visible Designer-to-preparation action remains to be connected; full U21/U22/Beta GO remains incomplete. Changes local/uncommitted.

2026-09-07 — Extended Designer semantic actions: exposed audition render/play/stop/cancel, A/B pin/play/clear, Save As, pose duplication/removal and undo/redo with availability checks, explicit action routing and audition/reference status. Transport-only actions do not require an idle file session. Added independent preview cancellation so cancelling audition cannot cancel a simultaneous save; regression confirms exact recipe persistence and clean saved state after preview cancellation. Both Studio builds and sixteen focused Designer tests pass in Debug/Release; diff checks pass. New semantic actions remain live-unverified and full screen-reader/cross-platform/U22/Beta GO acceptance remains incomplete. Changes local/uncommitted.

2026-09-07 — Designer semantic accessibility: added custom non-score accessibility-tree support and Designer numeric fields, file/page actions, save/error status and context-bound IDs. Values parse fully/finite and use canonical edits; stale/hidden/busy/drag targets reject. Live AppKit AX checks verified semantic Open, numeric 0.62→0.65 edit, rejection of 2 with error status, next-page formants and undo to SAVED. Exit 0 retained producer generation 3/approved 0/no recording. Custom-tree regression and rebuilt Release core/native suite pass 506 tests (11.51 seconds); diff checks pass. Details/limits: `VOICE_DESIGNER_ACCESSIBILITY_2026-09-07.md`. Full screen-reader traversal, missing action semantics, embedded/other-platform qualification and U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer pose management: added selected-pose duplication with AppKit phone/style naming and automatic selection, plus undoable removal. Common recipe validation rejects duplicate identities, invalid values and orphaned frication styles; last-pose removal is explicitly rejected. Original resonance values are copied as a retuning starting point, not claimed as correct new phonetics. Epoch/revision guards preserve stale-dialog safety. Fifteen Designer tests pass in Debug/Release (1.94/0.88 seconds), both Studio builds pass and diff checks pass. Actual new dialog/header interaction and cross-platform naming remain unverified/unsupported; complete articulation/accessibility and U22/Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Live Designer drag/viewport/audition QA: opened the saved synthetic recipe, dragged open quotient, verified one-step undo returned SAVED, navigated to the final formant with nonoverlapping six-row viewport, rendered preview and pinned A. Current B entered playback and returned ready without error through the linked CoreAudio path; after an F3-gain edit, A identity stayed pinned and reference playback entered its active state. The timed run ended before the planned mismatch check; programmatic test closure left the saved recipe untouched and producer generation/approval unchanged. No subjective listening/acoustic qualification is claimed. Detailed observations and test-close limits: `VOICE_DESIGNER_INTERACTION_QA_2026-09-07.md`. Rebuilt current Release core/native suite passes 504 tests (11.59 seconds), and diff checks pass. Smaller-window/accessibility, remaining interaction and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer mouse gesture wiring: numeric phonation/formant rows now capture a baseline/control/epoch and update via the shared keyboard adjustment mapping, with fixed viewport during drag, fine scaling, one-undo release and Escape rollback. Session gesture APIs guard identity/file work and invalidate preview results; invalid edits preserve the last valid value. Added regression verifies grouped history, file/replacement protection, stale-epoch rejection and audition suppression after cancel. Both Studio builds pass; Release fourteen-test Designer suite passes (0.83 seconds), with clean diff checks. Actual drag interaction and continuous audible feedback remain unverified/unimplemented respectively. Full U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Designer A/B reference: added immutable ready-preview pinning with original recipe/PCM/pose/style/pitch, preserved across draft edits and cleared on voice replacement. Cmd/Ctrl+B pins A, Shift+Space plays A only with matched pose/style/pitch, Space plays B, and Cmd/Ctrl+Shift+B clears the reference without recipe history changes. Tests verify frozen reference retention, changed current PCM, selection mismatch, reset/lifetime and clear semantics. Both Studio builds pass; Release thirteen-test Designer suite passes (0.72 seconds). Actual A/B UI/hardware playback and loudness/listening qualification remain unverified; this is not source or bank approval. Full U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Designer resonance controls: added per-formant frequency/bandwidth/gain editing for the selected pose/style, fine steps, full-list keyboard navigation and a six-row selection-following viewport. Accepted changes use canonical validation/undo and invalidate preview audio; invalid frequency crossings preserve state. Added regression proves each parameter changes PCM and undo restores exact original audio, with invalid edits preserving ready preview/revision. Both Studio builds pass; Release twelve-test Designer suite passes (0.75 seconds). Updated layout/live control interaction and physical listening remain unverified; mouse sliders, pose management/accessibility and full U22/Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Designer audition pose/pitch selection: added session-owned pose/style and MIDI 36–96 controls without changing recipe revision/hash/history. Selection is epoch/revision-guarded, invalid/no-op choices preserve valid preview state, changes invalidate pending/ready audio, and completion admission checks pose/pitch alongside voice identity. Create/Open resets selection; removed pose indexes safely return to the first pose. Studio keyboard rows expose both choices. Added PCM/worker regression verifies selected output, stale selection suppression, pitch-dependent audio and pose-removal safety. All eleven Designer tests and both Studio builds pass in Debug/Release (1.00/0.53 seconds); diff checks pass. Actual new control/playback interaction, A/B/continuous audition and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Designer sustained-pose audition: added bounded one-second/48-kHz preview through existing compiled-F0 phonation and oral tract stages, finite/peak checks and edge ramps. Separate background rendering captures immutable resource plus document epoch/revision; edits/replacements invalidate old audio and stale/cancelled results are discarded. Studio Space now renders, then plays the ready first-pose/MIDI-69 preview through the existing device audition session at 0.25 gain; device startup stays outside paint. Tests prove deterministic/non-silent/bounded PCM, aspiration-dependent output, invalid/cancel rejection, stale worker suppression and immutable retained buffers. All ten Designer tests and both Studio builds pass in strict Debug/Release (0.77/0.56 seconds); diff checks pass. Actual new Space/device interaction and listening remain unverified; pose/pitch controls, A/B/continuous preview and full U22/Beta GO remain incomplete. Details: `VOICE_DESIGNER_AUDITION_2026-09-07.md`. Changes local/uncommitted.

2026-09-07 — Designer discard/replacement flow: New/Open/Close now offer explicit discard-or-cancel confirmation for dirty drafts, with Cancel default, context revalidation and nested-dialog suppression. Saved files and producer takes are not deleted. Replacement loading/validation must succeed before the old draft is removed. AppKit/Win32 adapters are source-implemented; actual new dialog interaction and Win32 runtime remain unverified. Added regression verifies authorized-but-failed replacement preserves edited content/epoch and valid replacement advances epoch/reset history. All eight Designer tests and both Studio builds pass in strict Debug/Release (0.43/0.38 seconds), with clean diff checks. Audition, full controls/accessibility and U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Initial visible Voice Designer: Studio now hosts the file session behind Cmd/Ctrl+D, with Create/Open/Save/Save As, keyboard open-quotient/tilt/aspiration adjustments and undo/redo. Hidden producer keyboard/pointer/scroll actions are isolated; recording/worker guards protect entry, and unsaved drafts prevent window close. Live AppKit checks verified create, 0.60→0.61 adjustment, undo/redo, Save As, another edit to 0.62, rejected unsaved close, same-path save and clean exit. Saved bytes corroborate 0.62; producer generation/approval remained unchanged and no physical input occurred. Both Studio builds and seven Debug/Release Designer tests pass (0.53/0.48 seconds); diff checks pass. Details: `VOICE_DESIGNER_NATIVE_UI_2026-09-07.md`. This is a single-pose starter with no audition, mouse sliders, full pose/style controls or accessibility semantics; discard-close choice and full U22/Beta GO remain unfinished. Changes local/uncommitted.

2026-09-07 — Voice Designer file session: added standalone draft ownership and asynchronous open/save, explicit dirty replacement protection, document epochs plus model revisions, exact saved-snapshot acknowledgement, new-file-only Save As and same-path external-change checks under a cooperative persistent writer lock. Failures preserve current draft/path/history; shutdown joins and collects actual completion. Tests cover save/edit/reopen, busy/stale document actions, failed opens, external changes, existing destinations, lock contention and shutdown cleanup. All seven Designer tests pass in strict Debug/Release (0.48/0.55 seconds), and diff checks pass. Details/lock limits: `VOICE_DESIGNER_SESSION_2026-09-07.md`. Studio controls/audition and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Voice Designer gesture transactions: added begin/update/commit/cancel with immutable preview resources and monotonic revisions. Continuous changes do not grow history; commit creates one undo item, cancel restores the baseline, and neutral/cancelled gestures preserve redo. Nested/conflicting edits, undo/redo and save acknowledgement reject while active; invalid previews preserve the last valid state. Tests exercise 100 updates as one undo, stale completion, invalid preview recovery, immutable resources and saved/redo restoration. All five focused Designer tests pass in strict Debug/Release (0.44/0.48 seconds), and diff checks pass. Visible controls, asynchronous audition and full U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Editable Voice Designer draft model: added validated frozen-recipe edits with expected monotonic revisions, immutable old resource snapshots, canonical dirty/save identity and bounded 128-entry undo/redo. No-op edits retain redo; invalid/stale changes preserve state; undo restores content without reviving old actions. Recipe/engine identity is fixed within a model and no song/bank/producer mutation or approval is provided. Three focused model tests cover phonation/resonance changes, stale/invalid rejection, schema transitions and history bounds; registered in the native suite. This is the model foundation for grouped controls, not finished Designer UI/audition or acoustic qualification. Details: `VOICE_DESIGNER_MODEL_2026-09-07.md`. Full U22/Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Native batch assembly: Shift+B now selects 1–64 prepared folders/references and a new batch JSON destination via AppKit panels. Modal recording/workspace checks precede the shared async worker; it verifies recovered state and requires every job's original producer-state hash to match the captured model before new-file-only manifest publication. Success is explicitly not generated and leaves model/inspection/undo unchanged. Tests cover assembly/reload, busy guards, duplicate/wrong-state rejection and external-change rejection without output. Strict Debug/Release export suites pass (7.01/2.74 seconds), Studio builds in both configurations and diff checks pass. Actual multi-selection/save interaction remains unverified; non-AppKit multi-selection is explicitly unsupported. Durable queue/registry and full U21/U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Canonical batch manifest assembly: added `saveGenerationBatch` and CLI `prepare-generation-batch OUTPUT_JSON JOB_REFERENCE...`, accepting existing retained-digest job references without hand-written JSON. Shared admission rejects invalid/duplicate/different-state/over-budget jobs; relative paths, bounded manifest size and new-file-only publication preserve loader compatibility and existing output. No rendering, assignment or expectation recapture occurs. Library/actual CLI tests verify round-trip identity, cancellation/budget/duplicate rejection, no overwrite and unchanged producer/output state. Strict Debug/Release export suites pass (7.34/3.10 seconds), and diff checks pass. Native assembly controls and broader U21/U22/Beta GO qualification remain incomplete; changes local/uncommitted.

2026-09-07 — Live native preparation/generation/retry and Korean-input repair: exercised saved-score picker, background discovery, actual region popup, new destination, package creation, native job selection, generation/MarkerReview collection and read-only retry using a synthetic fixture. Preparation preserved generation 2; collection advanced to 3 with one unreviewed take; retry stayed at 3 and approved count stayed 0. Live QA exposed Latin-only AppKit shortcut mapping under Korean 2-Set input. Added a non-ASCII physical-key fallback without changing ASCII layouts or the separate active text-input path; after rebuilding, Cmd+Shift+I and Shift+P worked under the unchanged input mode. Release core/native suite passes 490 tests (11.36 seconds); native app exits 0 with no physical input/recorded frames; durable lineage corroborates the UI. Details and temporary evidence: `NATIVE_GENERATION_LIVE_QA_2026-09-07.md`. Multi-region/batch UI, embedded-host input, complete Designer/review/install/sing and full Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Visible saved-score preparation: Shift+P selects a saved score for bounded background inspection; after ready status, Shift+P selects its procedural region via a labeled AppKit popup and a new job-folder destination. Discovery returns retained score digest and workspace epoch/generation/selection; every modal boundary revalidates context/recording. Preparation now checks the exact owned score bytes against that digest before decoding, rejecting post-selection changes without publication. No heavy score/recipe work or automatic modal dialog is added to paint. Tests verify discovery/consumption, region/context identity and changed-byte rejection alongside existing preparation/CLI lifecycle cases. Strict Debug/Release export suites pass (7.09/3.06 seconds), both Studio builds succeed and diff checks pass. Fresh synthetic 720×520 capture verifies the shortcut label fits; exit 0, generation 4, approved 0, zero input callbacks/frames. AppKit full picker/popup/destination interaction remains unverified; other-platform region selection is explicitly unsupported. Durable queue/registry, Designer controls and full U21/U22/Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Native background job preparation: added selected-planned-assignment preparation through the shared saved-score operation on Studio's existing worker. Captured/durable-state mismatch rejects before directory creation; preparation returns an unchanged producer model and explicitly reports not generated. Busy guards, cancellation checkpoints before publication and shutdown join apply; completed package writing is not hidden by a late stop. Tests prepare/reload a package without audio or producer mutation, verify busy guards, reject an externally changed workspace and reject a pre-cancelled request without output. Strict Debug/Release export suites pass (7.44/3.00 seconds), current Release Studio builds and diff checks pass. Native score/region/destination selection controls are still absent; this is controller integration, not completed in-app preparation or U21/U22/Beta GO acceptance. Changes local/uncommitted.

2026-09-07 — Shared saved-score job preparation: extracted the CLI's preparation pipeline into `prepareGenerationJobFromScore`, retaining exact selected track/region, planned assignment, saved recipe identity/style, score-relative path resolution, sample-rate validation and immutable package/expectation publication. CLI syntax remains unchanged. Direct tests cover relative loading, original producer/recipe identity, no producer mutation, no overwrite, wrong/ambiguous targets, changed recipe rejection and packaged-source independence; actual CLI lifecycle regressions also pass. Strict Debug/Release export suites pass (7.45/3.37 seconds), current Release Studio builds, and diff checks pass. This provides the common operation for native preparation; the native score/region/job destination selection workflow is not yet implemented. Full U21/U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Native batch progress: added one coherent atomic phase/count snapshot shared with the worker, sampled only by the owner-thread status update. Studio distinguishes preflight, completed output count (including reuse) and collection pending; output completion is never labeled approval or producer commit. Future collection clears progress on success/error, while shutdown retains stop/join/result handling. Tests sample count bounds, require cleanup across success/failure/retry and exercise shutdown error cleanup without model changes. Strict Debug/Release export suites pass (5.98/1.93 seconds), both Studio builds succeed and `git diff --check` passes. Native visual observation of each phase, durable per-job queue/registry, in-app preparation and full U21/U22/Beta GO remain incomplete. Changes local/uncommitted.

2026-09-07 — Visible native batch entry: Cmd/Ctrl+Shift+B opens a dedicated batch JSON picker and routes to the asynchronous batch controller; plain B and single-job import/generation remain intact. It shares recording/busy guards, audition stop and modal workspace/selection revalidation. Explicit selection captures a bounded 64-KiB manifest digest; the worker verifies those bytes and retains all original job/producer expectations. The default budget is 64 jobs/32 Mi frames, not yet UI-editable. Wrong-digest regression and all export cases pass in strict Debug/Release (6.92/2.30 seconds), both Studio builds succeed, and diff checks pass. A fresh synthetic-input 720×520 native capture shows the combined job/batch shortcut label fitting; exit 0, generation 4, approved 0 and input frames/callbacks 0. Successful batch-picker interaction remains unverified. See `GENERATION_BATCH_EXECUTION_2026-09-07.md` for trust and capture limits. Queue/progress, in-app preparation and full U21/U22/Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Native asynchronous batch generation/collection controller: added retained-digest batch loading, bounded shared input preflight, captured/durable workspace comparison and complete/partial-history recognition on the Studio worker. Fresh work reuses verified output or generates missing output, then publishes all takes through the canonical atomic batch importer. Complete-history retry preserves manual marker edits and undo; partial collection and external workspace changes reject without replacing the visible model. Tests exercise two pitch layers, one-generation MarkerReview publication, missing output generation, budget rejection, busy guards, retry preservation and partial/external-state conflicts. Strict Debug/Release export suites pass (5.98/1.82 seconds), both native Studio builds succeed, and `git diff --check` passes. Contract: `GENERATION_BATCH_EXECUTION_2026-09-07.md`. Native batch picker/queue/progress, in-app preparation and interaction/cancellation qualification remain open. No U21/U22/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Early candidate readiness admission and native dialog opening: procedural import now checks selected-strategy readiness before candidate loading/staging; final project validation already enforced this requirement. Regression verifies an unready strategy rejects even with missing input files, preserves the model and leaves the asset store empty. Rebuilt Debug/Release export and producer suites pass (7.19/3.09 seconds), the current Release Studio builds, and `git diff --check` passes. Native Cmd+Shift+I interaction opened the intended generation dialog and Cmd+Shift+G opened its path sheet. Automated clipboard entry timed out; successful file selection/generation is not claimed. The app exited 0 with synthetic input, zero recording frames, unchanged reported generation 4 and zero approved takes. Detailed limits are in `NATIVE_PREPARED_GENERATION_2026-09-07.md`. In-app preparation/registry, native batch controls and U21/U22/full Beta GO acceptance remain open; changes local/uncommitted.

2026-09-07 — Visible native prepared-job entry: new job preparation writes a bounded `job.seamjob` reference with the original manifest digest before final manifest publication. Added strict reference loading and Cmd/Ctrl+Shift+I native file-picker wiring, with retained digest use, recording guards, audition stop and modal workspace/selection revalidation before the background worker. Intake labels expose import and generation without increasing panel height. Tests cover reference identity/relative resolution, malformed fields and the controller path; strict Debug/Release export suites pass all 19 cases (18.08/15.99 seconds), and both native Studio builds succeed. A fresh configured 720×520 AppKit run was visually inspected: shortcut text fits, process exits 0, Threaded Silence Input reports zero callbacks/frames, producer generation remains 4 and approved count is 0 in the synthetic fixture. No picker/keyboard interaction or microphone/playback qualification is claimed. Reference trust/temporary capture details are in `NATIVE_PREPARED_GENERATION_2026-09-07.md`; `git diff --check` passes. In-app preparation/registry, batch UI and full U21/U22/Beta GO qualification remain open; changes local/uncommitted.

2026-09-07 — Native Studio prepared-job worker: added an async selected-assignment generation/guarded-collection operation sharing existing busy, Escape/finish/shutdown and owner-thread polling behavior. Workers verify retained job identity, selected row and captured/durable producer state; successful fresh imports adopt committed markers, while AlreadyCollected returns unchanged status without clearing manual bounds or undo/inspection caches. Tests cover wrong-row rejection before output, busy selection/save guards, real worker generation/collection, preserved manual bounds/undo on retry and external-state rejection without replacing the visible or durable model. Strict Debug/Release export suites pass all 19 cases (8.54/5.15 seconds), both native Studio builds succeed and `git diff --check` passes. Contract: `NATIVE_PREPARED_GENERATION_2026-09-07.md`. This is a controller operation, not yet visible job picker/registry UI or physical interaction/recording qualification. U21/U22 and full Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — CLI atomic batch collection and complete-history retry: added shared batch-input inspection and existing-output verification that can recover journal-owned publication but never renders missing output. `import-generated-batch WORKSPACE BATCH_JSON BATCH_SHA256 OPERATOR UTC [MAX_TOTAL_FRAMES]` verifies the original batch, rejects stale/partially collected sets, checks committed outputs and performs one canonical batch save. Fully collected retries return observed AlreadyCollected states without output folders, reassignment or model changes. Actual CLI tests publish two takes in one generation, preserve state across complete retries and absent output folders, and reject a separate partial-collection fixture unchanged. Strict Debug/Release export suites pass all 19 cases plus CLI help (6.66/1.56 seconds); `git diff --check` passes. Updated batch/CLI documentation. Native orchestration, dedicated batch-collection interruption/race/cancellation qualification and broader voice/release gates remain open; no U21/Beta GO acceptance, changes local/uncommitted.

2026-09-07 — Atomic generated batch repository collection: added bounded multi-candidate admission against one original producer hash, unique/current assignment targets and aggregate frames. Existing strict import logic stages into a private draft without per-item publication or context recapture; one writer-locked expected-generation save publishes all takes with an import-generated-batch journal event and individual expectation-bound lineage. Failed later input leaves the active model/generation unchanged; unassigned content-addressed staging may remain. Tests reject insufficient budget and a missing second WAV, verify exact model/recovery rollback, publish two unreviewed pitch-layer takes in exactly one generation and recognize both original requests. Stale repeat collection leaves state unchanged. Producer saves now reject serialized generations above the recovery reader's 64 MiB bound; deferred staging avoids redundant full rollback copies. Strict Debug export suite passes 19 cases (6.14 seconds); Release export/repository suites pass 19 + 3 cases (3.28 seconds); `git diff --check` passes. Updated `GENERATION_BATCH_EXECUTION_2026-09-07.md`. CLI/native atomic-collection orchestration and dedicated collection interruption qualification remain open; no U21/Beta GO acceptance, changes local/uncommitted.

2026-09-07 — Bounded generation batch execution: added digest-required batch-manifest loading, copied job references, full preflight of prepared inputs/shared producer context/unique IDs and assignments, and aggregate requested-frame admission. Sequential workers retain verified completed output across cancellation and reuse it on retry; the CLI exposes `run-generation-batch BATCH_JSON BATCH_SHA256 [MAX_TOTAL_FRAMES]` through the signal bridge. Tests cover two pitch-layer jobs, budget/duplicate rejection before output, cancellation after the first result, first-job reuse plus remaining rendering, relative manifest paths, retained digest checks and actual CLI budget/reuse behavior. Strict Debug/Release export suites pass all 19 cases plus CLI help (7.15/3.06 seconds); `git diff --check` passes. Contract: `GENERATION_BATCH_EXECUTION_2026-09-07.md`. Atomic batch producer collection remains explicitly unfinished: independent imports change sibling expectations and must not be bypassed by recapture. Native orchestration and wider qualification remain open; no U21/Beta GO acceptance, changes local/uncommitted.

2026-09-07 — Read-only recognition of collected generation requests: guarded imports atomically retain the canonical expectation digest as an optional seventh procedural-lineage field; legacy/manual six-field lineage remains readable but cannot prove a generated request. Added repository recognition against verified durable assets, exact request/target identity and strict candidate lineage. CLI retries return AlreadyCollected with observed state/active status instead of creating another take; fresh imports return Collected. Tests preserve producer bytes across retries and manual edits, recognize a superseded take without reactivating it, reject mismatched requests and permit recognition after disposable staging paths disappear. Strict Debug/Release export suites pass all 19 cases (5.94/2.68 seconds); Release producer repository suite passes (0.85 seconds), and `git diff --check` passes. Documentation records integrity versus authentication and older-reader limitations of the lineage extension. Native/batch orchestration and broader qualification remain incomplete; no U21/Beta GO acceptance, changes local/uncommitted.

2026-09-07 — CLI generation signal cancellation: `run-generation` now uses scoped SIGINT/SIGTERM handlers with always-lock-free signal recording and stop requests dispatched from a normal watcher thread. Prior handlers restore on exit, setup failures prevent execution, and signal cancellation reports the conventional 128+signal status while retaining any fully committed output for verified reuse. Separate-process probe tests use the same bridge with real SIGINT before work and SIGTERM during journaled publication, verifying cancellation, expected output absence/preservation, exact audio on retry and no producer assignment. Strict Debug/Release export suites pass all 19 cases (10.46/2.58 seconds); `git diff --check` passes. Updated `GENERATION_JOB_PACKAGE_2026-09-07.md` distinguishes deterministic shared-bridge evidence from unverified interactive terminal/arbitrary timing/Windows delivery. Native/batch orchestration, pre-journal/arbitrary termination recovery and producer-commit recognition remain open. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Abrupt generation publication-exit recovery: added a dedicated test-only process probe exiting without destructors at all five journaled export publication phases. Parent-side tests observe terminal exit, rerun the same job, verify OS lock release, rollback/regeneration before receipt commit, reuse after commit, exact uninterrupted WAV digest and no producer assignment. A nonterminating fault test exposed and fixed a worker success-path gap: newly exported output now requires explicit Committed state, not merely a successful Result wrapper. Strict Debug/Release export suites pass all 19 cases (12.14/2.73 seconds); `git diff --check` passes. `GENERATION_JOB_PACKAGE_2026-09-07.md` records the evidence boundary. Power loss, arbitrary mid-write/pre-journal termination, Windows runtime, CLI signal cancellation, native/batch orchestration and producer-commit recognition remain open. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — CLI planned-assignment job preparation: added `prepare-generation WORKSPACE PROJECT TRACK_ID REGION_ID TAKE_ID JOB_ID JOB_DIRECTORY`. It recovers producer state, resolves exactly one planned take, verifies the selected score region and saved recipe, checks integer sample rate and prepares the frozen Final job. Capture rejects unready/nonprocedural strategies and existing take IDs. Actual POSIX CLI tests now exercise prepare → run → guarded collection into a separate producer workspace, recovering one unapproved MarkerReview take with matching gestures; unknown assignments, existing directories and duplicate IDs reject. Strict Debug/Release export suites pass all 19 cases plus CLI help (4.59/2.16 seconds); `git diff --check` passes. Build verification corrected explicit floating-point sample-rate admission before integer conversion. Native job orchestration, process-interruption qualification, batch generation semantics and already-committed result recognition remain unfinished. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Generation worker and verified output reuse: added `runGenerationJob` and CLI `run-generation JOB_DIRECTORY MANIFEST_SHA256`. Workers reload frozen inputs, acquire a persistent OS-backed exclusive lock, use existing export recovery/publication and strictly verify candidate identity/dimensions/origin/typed markers. Published output is reused only after committed receipt/file checks, without rewriting audio. Tests cover pre-cancellation, same-process and actual separate-process lock contention, first publication, library/CLI reuse, unchanged modification time, corruption rejection without replacement and exact-byte restoration. Producer assignments remain untouched. Strict Debug/Release export suites pass all 19 cases plus CLI help (7.65/6.41 seconds); `git diff --check` passes. Updated `GENERATION_JOB_PACKAGE_2026-09-07.md`. Actual process-kill recovery, signal cancellation, native/batch job orchestration and committed producer-result recognition remain open. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Frozen generation job package: added library preparation of a new manifest-last directory containing exact frozen score/recipe bytes and the original import expectation. A retained manifest digest binds job/source identity and all input digests. Bounded reload reconstructs the Final snapshot from owned bytes and requires the original render hash and exact dimensions; nominal score pitches must match the requested layer at preparation. Tests prove snapshot/source identity and exact PCM reconstruction, unchanged expectation, no replacement, wrong manifest digest rejection, changed score detection and missing recipe rejection. Strict Debug/Release export integration suites pass all 19 cases (4.16/1.75 seconds), and `git diff --check` passes. Build verification added the required explicit authoring-to-producer library dependency. Contract: `GENERATION_JOB_PACKAGE_2026-09-07.md`. Worker output publication, restart reuse, incomplete-preparation recovery and full process-interruption qualification remain unfinished. No U21/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Guarded generated-result CLI collection: added `import-generated WORKSPACE METADATA WAV RECIPE EXPECTATION SHA256 OPERATOR UTC`. It loads the retained digest-bound expectation, derives its target fields, recovers current producer state and calls the canonical guarded importer without recapturing. Actual POSIX executable tests reject stale expectations and wrong digests, accept a current request and reject a repeated call without duplicating takes or changing committed state. Strict Debug/Release export suites pass all 19 cases plus CLI help (7.02/1.63 seconds); `git diff --check` passes. Manual import remains separate. Documentation now distinguishes guarded collection from unfinished job submission, durable envelope binding and restart/idempotency orchestration. No U21 or Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Durable generation expectation storage: added strict v1 JSON encoding, new-file-only atomic publication returning a SHA-256, and bounded digest-required reload. Capture and guarded import share field validation. Tests preserve exact expectations across reload and enforce no-overwrite, wrong-digest rejection and semantic rejection of malformed versions, overflowed rates, control characters, missing/extra fields and zero dimensions even when malformed bytes have a matching supplied digest. Reloaded expectations participate in the existing stale-state/import lifecycle tests. Strict Debug/Release export integration suites pass all 19 cases (5.91/3.17 seconds), and `git diff --check` passes. Updated `GENERATION_IMPORT_EXPECTATIONS_2026-09-07.md`. The durable owning job envelope, retained digest binding, worker checkpoints and restart/resume orchestration remain unfinished; expectation files alone do not complete U21 or Beta GO. Changes local/uncommitted.

2026-09-07 — Generated-result admission expectations: added capture of canonical producer state, intended take/assignment/recipe and expected output style/render identity/dimensions. The canonical procedural importer optionally enforces that captured request before asset import, then retains its existing writer-locked durable-generation publication check. Tests reject altered outputs/targets, unsaved assignment changes and newer saved generations; early mismatches preserve model/assets, and an explicitly recaptured request imports normally as unapproved material. Strict Debug/Release export integration suites pass all 19 cases (3.70/1.67 seconds), and `git diff --check` passes. Contract: `GENERATION_IMPORT_EXPECTATIONS_2026-09-07.md`. This is a repository component, not an automatically wired durable job system: original expectation persistence, job IDs, cancellation/resume and partial-result reuse remain unfinished. No new roadmap-unit or Beta GO acceptance; changes local/uncommitted.

2026-09-07 — CLI project-to-candidate baking: added `bake-project PROJECT OUTPUT_DIRECTORY [SAMPLE_RATE]` using the existing Final export service, saved source references and project-relative recipe loading. Requires procedural references for every nonempty vocal track; emits unapproved candidate/source packages without producer assignment, approval or overwrite. Actual POSIX CLI tests compare mixed candidate PCM/markers with library export and verify relative-path resolution, unchanged source bytes, completed-output preservation, invalid rates and missing recipe selection. Strict Debug/Release export suites pass all 19 cases plus CLI help (3.81/1.58 seconds); `git diff --check` passes. Usage/boundaries: `docs/authoring/PROCEDURAL_CLI_BAKING.md`. U21 immutable assignment jobs, cancellation/resume, partial-result reuse and expected-generation stale-result admission remain unfinished; this command is not advertised as that complete workflow. No unit/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Explicit procedural timing edit preflight: extracted common affected-note geometry validation and applied it to both inferred-boundary materialization and subsequent explicit edits. Shared timing, resolved positive spans, note/nucleus bounds and nonoverlap must pass before publication. Unchanged explicit boundaries are no-ops. Expanded tests reject subsequent onset/nucleus/neighbor/note crossings without changing project, revision or render requests; a valid subsequent edit renders through the production pipeline and undoes exactly. Strict Release core suite passes 489 cases (10.36 seconds), native editor builds, and `git diff --check` passes. This addresses authoring consistency, not source/acoustic or native-interaction qualification. Full Beta GO remains incomplete; changes local/uncommitted.

2026-09-07 — Source-aware timing lane and inferred-boundary editing: saved procedural tracks now project the same in-note timing policy into the phoneme lane and boundary hit testing. Inferred onset/nucleus geometry replaces onset placeholders, carries a distinct `^` indicator and preserves subpixel valid spans; sample-source geometry stays unchanged. Initial edits on an inferred note materialize dependent note timing in one composite undo step, reject nucleus/note crossings before publication, and preserve neighboring boundaries. Tests cover two zoom levels, hit testing, flag/geometry distinctions, source-policy restoration, conflict rollback, one render notification and exact undo/redo; the edited score also passes real procedural snapshot/rendering. Strict Release core suite passes all 489 cases (10.32 seconds), Release native editor builds, and `git diff --check` passes. See `PROCEDURAL_ONSET_POLICY_2026-09-07.md`. No physical native-input, accessibility or pronunciation/listening qualification is claimed. U20 and full Beta GO remain incomplete; changes local/uncommitted.

2026-09-07 — Versioned default procedural onset timing: the shared timing/compiler APIs now expose an opt-in ProceduralInNote policy, selected by production procedural snapshots only. Wholly untimed single-unvoiced-onset syllables receive a bounded in-note onset, up to 60 ms/one quarter syllable, with a distinct inferred start rather than a forged explicit edit. Authored groups stay untouched; automatic prior ends stop before generated onsets. Backend recipe/source and own-note checks remain required. Compiler revision 9 and procedural timing-policy revision 1 enter render identity. Tests cover baseline timing, short/multiple syllables, explicit/partial timing preservation, ordinary untimed `さ` snapshots and actual v2 baking without saved overrides. Strict Debug timing/snapshot/export suites pass 15 + 41 + 19 cases (17.76 seconds); strict Release design/timing/snapshot/export suites pass 12 + 15 + 41 + 19 (4.58 seconds), plus 17 compiler cases (0.96 seconds). `git diff --check` passes. See `PROCEDURAL_ONSET_POLICY_2026-09-07.md`. Duration defaults remain engineering choices, not phonetic qualification; source-aware editor presentation, richer articulation and reviewed pronunciation remain open. No new U20/Beta GO acceptance; changes local/uncommitted.

2026-09-07 — Mixed candidate v2 and producer lifecycle: removed the temporary mixed-bake rejection by adding strict articulated metadata with typed vowel/frication markers and plan/source/stream revisions. Vowel-only output remains v1. The loader checks recipe/style bindings, frication rate support, both gesture classes and existing bounded hash-verified Float32 audio. Typed markers survive normal bake/import/recovery and Studio-controller manual boundary edits without changing original metadata or granting review. Tests reject incorrect kinds, bindings, styles, revisions, rate and complete attempted downgrade to v1. Final strict Debug export suite passes all 19 cases (3.00 seconds); rebuilt Debug snapshot suite passed 41 cases (13.64 seconds); final strict Release design/snapshot/export suites pass 12 + 41 + 19 cases (2.90 seconds). Release native Studio builds; `git diff --check` passes. Contract: `docs/formats/PROCEDURAL_CANDIDATE_V2.md`. Synthetic source flags are test scaffolding only. No perceptual/native-interaction qualification or new roadmap-unit acceptance; full Beta GO remains incomplete and changes are local/uncommitted.

2026-09-07 — Production articulated snapshot routing: integrated recipe-bound explicit frication/vowel preparation into the real snapshot factory, pipeline, checkpoint wrapper and typed-source Final audio export. Preserved the existing vowel renderer, active sample-edit rejection and shared timing. Plan-derived markers now carry onset-to-nucleus geometry and ownership clipping through workers/cache hits; scheduler validation checks bounded marker structure instead of a duplicate vowel whitelist. Updated procedural render identity with articulation-domain and DSP/plan revisions. Mixed candidate baking rejects before publication pending its truthful versioned format/strict ingestion. New tests exercise nonzero score origin, exact direct/pipeline/chunk PCM, cancellation/checkpoints, cache markers/work accounting, recipe identity changes, malformed markers and Final WAV equality. All 12 design + 41 snapshot + 19 export cases pass after strict Debug/Release builds (31.39/5.53 seconds); Release native editor builds and `git diff --check` passes. Verification found and fixed the scheduler's remaining vowel-only guard. Default onset policy, mixed candidate production, broader articulation and perceptual qualification remain open; no new unit or Beta GO acceptance, changes local/uncommitted.

2026-09-07 — Recipe-bound articulation preparation: added `ArticulationPlan::compileRecipe` and `ArticulatedStream::createFromRecipe`, deriving only requested style-scoped frication settings from the verified frozen resource and using the compiled score's clock/context. Preparation validates vowel poses, complete score-note coverage and each gesture's own-note bounds, with cancellation checks and no guessed onset timing. Tests use the actual Japanese resolver plus saved phoneme overrides, compare prepared and explicit-plan PCM exactly, and reject missing timing/bindings/styles/poses, corrupt resource identity, incomplete note coverage and onsets extending into a score gap. Unused frication presets do not impose their rate constraints on vowel-only material. Strict Debug design tests pass 12 cases (2.56 seconds); strict Release design/snapshot/export tests pass 12 + 40 + 18 cases (3.84 seconds). `git diff --check` passes. Production snapshot/backend adoption, mixed-candidate metadata/baking and acoustic qualification remain open. U20 and full Beta GO remain incomplete; changes are local/uncommitted.

2026-09-07 — Frication recipe/resource identity: added bounded style-scoped source settings in schema/resource version 2, preserving canonical v1 bytes for vowel-only recipes. Articulated streams now reject plans whose frication settings differ from their frozen resource. Tests cover pre-change v1 hash preservation, full-width seeds, save/reload, invalid fields/styles/version, content-hash changes and plan mismatch. Strict Debug design tests pass all 11 cases (2.32 seconds); strict Release design/export tests pass 11 + 18 cases (2.12 seconds). Verification corrected the old v1-only resource guard; `git diff --check` passes. Production snapshot/mixed-marker/bake adoption and acoustic qualification remain open; changes are local/uncommitted and Beta GO is incomplete.

2026-09-07 — Explicit-plan articulated composition: added a worker stream combining compiled-F0 voiced tract output with scheduled frication, checking shared timing/rate/voicing and applying performance gain once after composition. Tests prove onset source separation, near-440-Hz vowel output, exact whole/chunk/checkpoint/reset behavior, cross-boundary ownership, cancellation, timing-conflict rejection and shared dynamics. Strict Debug/Release voice-design builds and all 10 cases pass (2.64/0.76 seconds); `git diff --check` passes. A diagnostic composed WAV is retained in the test's temporary directory. Production snapshot/recipe identity integration, truthful mixed-marker baking, broader articulation and CV/VC/listening qualification remain open. U20/full Beta GO are incomplete; changes remain local/uncommitted.

2026-09-07 — Scheduled frication lane: articulation plans now retain their clock/context, and `FricationGestureStream` renders absolute-time frication spans with bounded smoothstep edges, zero vowel/gap output and checkpointed filter continuity. Owned-window output and discarded-prefix DSP work are independently bounded; cancellation/failure leaves stream state unchanged. Tests cover exact whole/chunk/mid-ramp output, copies/reset, silence, invalid windows and excessive-prefix rejection. Strict Debug/Release voice-design builds and all 10 cases pass (1.74/0.64 seconds); `git diff --check` passes. Mixed voiced/aperiodic composition, recipe/snapshot binding and consonant/listening qualification remain open. U20/full Beta GO are not complete; changes remain local/uncommitted.

2026-09-07 — Explicit articulation plan: added immutable ordered vowel/frication gestures consuming shared phoneme timing. Frication requires explicit source bindings/start times and a consistent associated voiced nucleus; unknown classes, missing/duplicate timing, conflicting voicing and overlapping/out-of-context spans reject. The shared-timing fixture renders a source for its compiled onset extent. All 10 strict Debug/Release voice-design cases pass (1.77/0.65 seconds), and the final private-constructor adjustment rebuilds in both configurations; `git diff --check` passes. Mixed-source streaming, recipe/snapshot identity integration, default onset policy and CV/VC/listening qualification remain open. U20 and full Beta GO remain incomplete; changes are local/uncommitted.

2026-09-07 — U20 aperiodic excitation: added deterministic band-shaped `FricationSource` with bounded configuration/output, absolute-frame noise, independent copied filter state, reset and atomic cancellation/failure handling. Extracted the existing noise function without changing phonation arithmetic. All 9 strict Debug/Release voice-design cases pass (1.81/0.81 seconds); all 40 rebuilt Release snapshot cases pass (1.74 seconds), plus `git diff --check`. A diagnostic Float32 WAV is retained by the test. See `U20_ARTICULATION_STATUS_2026-09-07.md`. This component is not yet recipe/snapshot/phoneme integrated and does not qualify any consonant, CV/VC pair or singer. U20 and full Beta GO remain incomplete; changes are local/uncommitted.

2026-09-07 — Exportable pitch inspection evidence: added captured source/take/inventory/generation/operator/recipe context and new-file-only JSON export of bounded estimator settings, frame results and nullable summaries. Cmd/Ctrl+E uses a dedicated save-dialog purpose and rechecks modal context; the controller rejects stale/missing inspection and never modifies producer state or approves a take. Strict Debug/Release native Studio/integration builds and all 18 cases pass (2.67/0.95 seconds), including report identity parsing, invalid timestamps, no-overwrite preservation and invalidated-result rejection; `git diff --check` passes. Format is documented in `docs/formats/CANDIDATE_PITCH_INSPECTION_V1.md`. Reports are unsigned estimates, not authenticated QC admission or rights proof. Native panel/Windows execution, downstream admission and full Beta GO remain open; changes are local/uncommitted.

2026-09-07 — Candidate F0 contour: worker-prepared window-center cent estimates now render in a B-toggle pitch/wave view, sharing raw-frame zoom and boundary geometry. The view breaks unvoiced/missing/invalid-window gaps and marks vertically clipped outliers without changing model values. Tests verify centers, octave offsets, connectivity, invalid inputs, outlier retention and mode switching. Strict Debug/Release native Studio/integration builds and all 18 cases pass (2.66/0.96 seconds); inspected the 720×520 contour raster and `git diff --check` passes. This is estimator visualization, not acoustic alignment, durable QC admission or full Beta GO. Physical/native interaction qualification remains open; changes are local/uncommitted.

2026-09-07 — FFT candidate pitch analysis: added opt-in zero-padded FFT autocorrelation with prefix-energy normalization, cancellation and method-specific transform admission; Direct remains the default reference for existing callers. Candidate inspection uses FFT with 4,096-window/512-million-butterfly bounds. Five focused cases compare numerical output/voicing across rates and window shapes, verify exact budget checks, and complete an eight-second gesture that exceeded the prior candidate limits. Strict Debug/Release native Studio/integration builds and focused plus 17 integration cases pass (7.59/2.26 seconds); `git diff --check` passes. Numerical tolerance is not universal estimator qualification, and no physical-hardware speed benchmark is claimed. Durable QC, native/listening acceptance and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Selected candidate pitch inspection: added an N-key read-only worker bound to verified PCM digest, current gesture/boundary revision and inventory target. It uses adaptive unpadded windows, cancellation and explicit 256-window/500-million-term admission limits. Results expose estimated window voicing, median F0 and cents from target without approval or persistence; context changes invalidate them. The baked A4 fixture measures near 440 Hz and matches direct estimator output. Strict Debug/Release native Studio/integration builds and all 17 cases pass (3.50/1.61 seconds), covering scope, busy state, cancellation and short-span rejection. Inspected the 720×520 estimator panel and `git diff --check` passes. Efficient broader-range analysis, durable QC admission, contour review, physical interaction/listening and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Pitch-analysis safety prerequisite: added optional cancellation and output/correlation-work limits to the shared analyzer, finite configuration checks, bounded frame allocation and safe floating-point lag clamping before integer conversion. The estimator's normal output is unchanged; oversized work rejects before allocation instead of silently truncating. Dedicated tests verify exact budget admission, insufficient/zero budgets, silent-input charging, NaN/infinity/subnormal configuration, huge frame size, cancellation and normal 220-Hz output. Strict Debug focused build/test passes (2 cases, 0.96 seconds); strict Release rebuild plus all 486 core cases and 2 focused cases pass (10.82 seconds), with `git diff --check`. Candidate F0 presentation/job-specific budgets and acoustic qualification remain open; the default correlation limit preserves legacy callers. Changes remain local/uncommitted and full Beta GO is not complete.

2026-09-07 — Measured raw-signal inspector: background verified-audio loading now computes full-take peak/RMS/DC/near-full-scale count through the existing shared analyzer and presents rate/frame dimensions. The panel explicitly separates measured raw signal from gesture plans/manual bounds and approval. The recorded-PCM24 dry-take acceptance policy is unchanged; no QC approval or durable measurement record is fabricated. Independent PCM calculations pass across all 17 Debug/Release integration cases (2.42/0.95 seconds); both strict native Studio builds and `git diff --check` pass. Inspected the 720×520 measurement raster. Pitch/phonetic/listening qualification, durable QC admission and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Audition failure lifecycle: replaced app-local device ownership with an injectable owner-thread session that handles open/start failure, format changes, reported write failure, unexpected stop, normal completion and a three-second callback-progress watchdog. Failure clears active playback and releases the device before callback state. New injected-device tests exercise shutdown-time callback access and deterministic watchdog timing; the existing real-thread nonphysical test remains. Strict Debug/Release native Studio/integration builds and all 17 integration cases pass (2.31/0.95 seconds); rebuilt Release core passes all 486 cases (10.18 seconds), plus `git diff --check`. Actual hardware disconnect/reconnect, native interaction and audible qualification remain unverified. Earlier native captures refer to the older binary. Changes remain local/uncommitted; full Beta GO remains open.

2026-09-07 — Native runtime checkpoint: launched current Release Studio against an isolated copied synthetic workspace, both as its raw executable and as identical bytes in a temporary app bundle. Both runs completed their 600-second native event loop and exited 0, recovered generation 18 with manual bounds/MarkerReview and recorded zero input frames. Inspected the actual AppKit final capture; see `NATIVE_STUDIO_RUNTIME_CHECK_2026-09-07.md` for hashes and scope. The UI-control service failed to attach (raw executable unresolved, bundle attachment timeout), so keyboard/mouse/dialog/physical-audition qualification remains unverified. Both existing process handles were confirmed terminal; no restart was inferred from a timeout. This yields native startup/recovery/render/shutdown evidence, not interactive or Beta GO acceptance. Source changes remain uncommitted.

2026-09-07 — Candidate waveform zoom/pan: added bounded +/- zoom, Alt +/- half-window pan, selected-gesture recentering and cached exact-PCM viewport peaks. Paint and dragging share the visible source interval; clipped spans do not expose false boundary handles. View changes do not mutate producer data or audition ranges. Strict Debug/Release native Studio/integration builds and all 16 integration cases pass (2.84/1.01 seconds), covering zoom/pan limits, sample-exact peaks, zoomed drag mapping, clipped edges and busy rejection. Inspected the 720×520 zoomed raster; `git diff --check` passes. Owner-thread peak-build latency, native input/physical audio qualification, derived coordinates and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Candidate boundary dragging: added shared waveform geometry, selected start/end handles and a draft-only drag lifecycle. Movement clamps to raw/neighbor bounds without publishing; left release commits one undoable revision. Escape/key actions, resize and shutdown cancel before other work, and stale release restores original cached bounds/history. Tests verify no durable movement writes, cancel, commit/undo, end clamping, invalid coordinates, resize and concurrent-writer rejection. Strict Debug/Release native Studio/integration builds and all 16 integration cases pass (2.35/0.94 seconds); inspected the 720×520 draft raster and `git diff --check` passes. Native mouse/focus qualification, zoom, derived coordinates and full Beta GO remain open. Changes remain local/uncommitted.

2026-09-07 — Candidate boundary undo/redo: added a bounded take-session history with owner-thread Cmd/Ctrl+Z, Shift+Z and Y wiring. Undo/redo append compensating attributed revisions rather than deleting lineage or reviving approval. History updates occur only after durable success; new edits clear redo, failures/no-ops preserve it, and context refresh resets session history while persisted bounds remain recoverable. Integration tests verify cross-gesture selection, generation advancement, review invalidation, redo branching, stale-writer failure without stack loss and reopen behavior. Strict Debug/Release native Studio/integration builds and all 16 integration cases pass (2.27/0.90 seconds); `git diff --check` passes. Durable undo-stack restoration, drag/zoom, derived coordinates, physical interaction/listening and full Beta GO remain open. Changes remain local/uncommitted.

Durable candidate boundary edits: added an ordered raw-boundary revision resolver and project-level chain validation, preserving original candidate lineage/PCM. Repository edits require the active take and matching journal attribution, clear prior marker/pitch review flags, and restore the prior project on rejection. Studio supports approximately-1-ms Alt-arrow start / Alt-Shift-arrow end nudges, manual-vs-planned labels, retained waveform and effective-range audition. Tests verify no-op, overlap/negative/stale rejection, original metadata preservation, review invalidation and reopen. Strict Debug/Release producer/export suites pass (3.53/1.86 seconds); final integration checks pass (2.10/0.88 seconds), rebuilt Release core passes all 485 cases (10.27 seconds), and native Studio builds in both configurations. Inspected edited 720×520 raster; `git diff --check` passes. Undo/redo, dragging/zoom, derived coordinates, physical interaction/listening and full Beta GO remain open. Changes remain local/uncommitted.

Candidate audition realtime audit: added a separately compiled CTest probe using the existing allocation harness, including an interception self-test, fixed 24-configuration/12,474-callback coverage and sample-exact output oracle. Debug/Release both report zero intercepted allocations/deallocations, registered lock/IO/logging calls and output mismatches, with executable hashes retained in build-local JSON reports. Strict builds and both candidate/original 100,000-callback playback probe CTests pass (10.72/2.16 seconds); `git diff --check` passes. The original probe contract is unchanged. Direct malloc/unregistered OS operations, physical-device timing, error-path coverage, editable boundaries and full Beta GO remain unqualified. Changes remain local/uncommitted; this evidence does not complete another roadmap unit.

Raw candidate audition: retained verified immutable PCM from background loading and added Space whole-take / Shift+Space selected-gesture playback through the existing system output adapter. The new bounded processor uses owner-prepared state, callback-owned cursor, fixed attenuation/peak protection and short boundary fades; no silent fallback or rate substitution is used. Context-changing UI actions and shutdown stop the device before releasing processor/audio. Strict Debug/Release native Studio and integration builds pass; all 16 export integration cases pass (1.78/0.84 seconds), including actual nonphysical output-thread completion and block/range/rate tests. Rebuilt Release core/native suite passes all 485 cases (9.90 seconds); `git diff --check` passes. Verification corrected a callback member typo and declared the new public platform header dependency. Physical-device playback, realtime allocation instrumentation, device interruption handling, acoustic/listening acceptance and full Beta GO remain open. Changes remain local/uncommitted.

Candidate gesture navigation: added bounded selection/window projection, Left/Right and gesture-area wheel navigation, selected waveform-span highlighting and stable-key rows. The minimum-height layout reserves a complete selected row with waveform loaded. A 24-gesture synthetic metadata fixture verifies all entries can be brought into view, endpoint/large-delta handling, busy rejection, reset behavior and unchanged live/durable project state. Strict Debug/Release affected builds and all 15 export integration cases pass (1.62/0.64 seconds); native Studio builds in both configurations and `git diff --check` passes. Inspected the final-gesture 720×520 scene capture with warnings/key/frame range intact. This completes overview navigation, not audible audition, boundary editing, acoustic review or full Beta GO. Changes remain local/uncommitted.

Raw candidate waveform preview: added strict stored-metadata audio loading and an isolated read-only waveform worker, exposed through Studio's existing P key mapping. It returns at most 1,024 exact min/max bins, preserves the producer generation/review state, rejects corrupted raw bytes and clears on selection/import changes. The scene overlays planned raw spans and labels display-only auto-scaling. Strict affected Debug/Release builds and all 15 export integration cases pass (1.53/0.65 seconds), plus `git diff --check`; native Studio builds in both configurations. Verification corrected an unsupported W-key reference and an incorrect test helper name without weakening checks. Inspected the final 720×900 raster capture. Audition, zoom/editing, full marker navigation, measured QC and full Beta GO remain open. Changes remain local/uncommitted.

Stored candidate marker overview: split reusable bounded metadata parsing from audio loading, retaining sample-rate/frame-count dimensions and null audio for metadata-only results. Studio caches validated raw-source planned markers for the selected take across open/selection/import and renders frame ranges/normalized bars without paint-time IO. Visible/total counts and separate NOT MEASURED/NOT APPROVED labels prevent an overview from implying acoustic review. Debug/Release affected builds and all 15 export integration cases pass (1.53/0.62 seconds); both native Studio builds and `git diff --check` pass. Inspected 720×900/1440×900 scene captures; fixed observed narrow-width instruction/warning clipping. Full-list navigation, waveform audition/editing, measured alignment/QC and full Beta GO remain open. Changes remain local/uncommitted.

Background producer intake: the native import action now launches an owned asynchronous worker with a private project/repository and captured inventory assignment/operator. Live publication is polled on the owner thread; managed conflicting operations and recording are disabled until collection. Escape requests cancellation. Shutdown cancels/joins/collects before reporting final durable state, and destruction joins rather than detaching. Integration tests exercise real worker import, busy selection/save/reopen/repeat rejection, malformed-recipe failure with preserved inspection/project/status, and cancellation racing the durable boundary with recovered/live equality. Strict Debug/Release export/native Studio builds pass; Debug export CTest passes (1.60 seconds), and Release export plus rebuilt core/native CTest pass (11.15 seconds, core 10.55 seconds). `git diff --check` passes. Native modal/focus/responsiveness, bounded filesystem cancellation latency, Windows runtime and measured singer QC remain unqualified. No additional roadmap unit or Beta GO completion is claimed; changes remain local/uncommitted.

Producer cancellation continuation: threaded optional stop tokens through native controller recipe loading, strict candidate verification, raw-bound import and repository save. Cancellation checks precede mutation/asset work, follow asset import and guard the first journal write; after publication starts the durable result wins. Candidate loader cancellation checkpoints now return Conflict rather than malformed-input errors. Regressions verify pre-cancelled save/import preserve durable and in-memory state, do not create initial assets, and retain controller inspection/status. Strict Debug/Release affected builds and both export/producer CTest entries pass (2.72/1.84 seconds); Release native Studio builds. A missing codec include in the new test was corrected after the first compile failed. No warning policy was weakened. Native asynchronous execution, bounded copy/hash cancellation latency, measured QC and full Beta GO remain open; this is a prerequisite, not another accepted unit. Changes remain local/uncommitted.

Latest continuation checkpoint: U3 is already locally accepted, as are U4/U5; work is not restarting those units. Completed producer writer safety before background intake: persistent OS locking, stale project/generation rejection under the lock, and generation-counter exhaustion rejection. Added competing-writer, stale-save, lock-release, interrupted-journal and symlink-lock regressions. Strict affected Debug/Release builds pass; all eight selected migration/command/persistence/context/reconciliation/timing/export/producer CTest entries pass (4.94/2.99 seconds), and `git diff --check` passes. The initial competing-writer regression also passed ten consecutive Debug runs before the additional interrupted-journal/symlink checks were added. Windows lock implementation is source-only here; native responsiveness, worker intake, measured QC and singer qualification remain open. This prerequisite does not complete another roadmap unit. Changes remain local/uncommitted.

U1's implementation and diagnostic-runtime criteria, U2's acceptance-contract implementation, U3's canonical vocabulary/migration/persistence implementation, U4's bounded shared-pronunciation/reconciliation implementation, and U5's ordered timing implementation are verified locally. The U5 requirement audit and post-fix broad/focused verification are in `U5_ACCEPTANCE_AUDIT_2026-09-06.md`; prior audits remain available. U6 is the next active implementation unit; U6–U48 remain uncompleted. The source-index closure check passed during the earlier user-requested checkpoint publication, not for the current uncommitted continuation. This does not certify any release-quality singer, installed host matrix or Beta GO.

### U1: reproducible build and auditory baseline

**Changes made:** explicit numeric conversions in WAV statistics, batch/streaming sample-rate conversion, CLAP PCM resampling, live-note fades and diagnostic-button width arithmetic. Corrected declaration-order aggregate initialization in standalone callback binding, native startup configuration and the render-status test fixture. The fixes preserve values/behavior and do not suppress compiler diagnostics.

**Pre-change evidence:** fresh strict builds failed on the implicit conversions and C++20 designated-initializer ordering. Each affected translation unit compiled after its correction. No new behavior test was added for mechanically equivalent casts; existing DSP, resampling, live-voice, native and lifecycle regressions provide behavioral coverage.

| Check | Observed result | Scope and remaining limit |
|---|---|---|
| `cmake --preset dev` | Exit 0 | Reused the verified Ninja cache; did not clear a build tree |
| `cmake --build --preset dev -j 4 -- -k 0` after repairs | Exit 0 | Full development build. Apple linker still reports duplicate static-library inputs; compiler warnings were not disabled |
| `cmake --preset release` and `cmake --build --preset release -j 4 -- -k 0` | Exit 0 | Full optimized build with the same strict compiler checks |
| Compiler negative probe using project warning flags and an unused local variable | Exit 1 with `-Werror,-Wunused-variable` | Confirms a genuine compiler warning still fails; probe read from stdin and produced no file |
| Four focused CTest suites: render coordinator, bank production, recovery/support, phase12c live voice | 4/4 passed | Freshly rebuilt binaries, not historical counts |
| `ctest --preset dev --output-on-failure --output-log build/dev/Testing/fullscope-baseline.log` | 64/65 entries passed, exit 8 | Sole failure: `seam_tracked_source_closure`, because plan and existing U60 files are not indexed. Do not waive the check or stage unrelated work solely to turn it green |
| `seam_tests` within the CTest run | 443 passed, 0 failed | Mechanical/native/domain regression coverage, not acoustic qualification |
| Release CTest: core suite, render coordinator, bank production, recovery/support, live voice | 5/5 entries passed | Optimized-build regression checks; not the entire Release CTest matrix |
| `python3 -m unittest discover -s tests/production -v` | 65 tests, 1 failure | Existing `test_support_bundle_hash_must_match_archived_raw_evidence` expects `PR-010-support-intake`; observed blocked IDs contain only `PR-002-root-chain`. The failing assertion remains intact pending focused diagnosis |
| Fresh native binary `--help` | Exit 0 | CLI argument surface |
| Native deterministic, paused, nonphysical-audio launch with isolated support root | Exit 0 through approved unsandboxed execution; AppKit frame emitted | Sandboxed launch aborted in macOS `_RegisterApplication`; the same binary/arguments worked through the approved GUI-capable path. This is not a synthesis crash or installed-release certification |

Native smoke-test output is local at `/private/tmp/seam-fullscope-u1.o1GgE3/native.ppm` and `native.png`. The run reported `window_backend=AppKit software raster + NSTextInputClient`, `audio_physical=false`, `voicebank_resolved=false`, and `render_state=idle`. The capture shows the empty score and the two-action missing-voicebank diagnostic. Full editor/viewport/character acceptance is still required by later units.

Two independent read-only visual reviewers passed this single captured diagnostic state. They confirmed intact labels, 112-by-28 buttons, an 8-pixel gap, and no text/button clipping or overlap. This does not certify interaction, CJK text, resizing, or the complete native UI. The temporary launch-debug journal was removed after recording the result here; no debug instrumentation or system-setting changes remain.

**Auditory baseline delivered:** [the retained packet](../../out/fullscope-beta/u1-auditory-baseline-20260905/input-provenance.json) contains two saved projects rendered in bank-selected and forced-Raw modes. Each of the two melody outputs is 41 seconds / 1,968,000 frames, and each unequal-rest output is 9.125 seconds / 438,000 frames. All four outputs have nonzero finite measured RMS. The packet retains 24 output artifacts, verified against its output manifest, plus exact input bytes, source patch/untracked-source archive, build configuration, executable identities, command logs, target timing, actual placements and fallback records. No fallback was reported for this baseline; that is not proof of pronunciation or musical quality.

The first native run rejected a missing schema-7 technical-lane field in the new fixtures. The fixtures and their locked hashes were corrected; the project validator was not weakened. Failed process diagnostics now point to retained command/stderr records.

Registered `seam_synthesis_quality_tests`, `seam_singing_quality_contract_tests`, `seam_singing_quality_workflow`, and `seam_public_release_python_tests` in CMake. The Python admission/runner suite passed 13 tests; its four optional native cases are exercised separately by the mandatory workflow target. That workflow executed all four native tests successfully in Debug (73.09 seconds) and Release (20.81 seconds), including the complete real render/analyze/save/provenance chain and invalid-input rejection. The focused synthesis executable also passed in both configurations.

CMake initially selected the bundled Python 3.12 without existing `jsonschema`/PyYAML test dependencies. Explicitly configuring `-DPython3_EXECUTABLE=/usr/local/bin/python3` selects the installed Python 3.14.3 environment that passed the complete production suite. No dependency was silently skipped or installed into the bundled runtime. The production suite now also passes through CTest. Another machine must supply an interpreter with the repository's existing test dependencies; the interpreter path is machine-local, not hardcoded into project CMake.

An independent read-only review found no actionable defect in the corpus admission, frozen snapshot/hash verification, diagnostic output or support-evidence repair. Builds, native workflows, Ruff checks and `git diff --check` were run by the root executor; review alone was not treated as runtime proof. Baseline audio remains diagnostic and does not qualify an original female singer.

U1 evidence binding: corpus SHA-256 `b5a8ab6f6a75a31e6322d23da88dfd8ea93ddc04a73d5e4445cfa48bd4275e6b`; source-evidence archive SHA-256 `cceefb2bdf98ef3c1c3ba98c0a68c208d06b3666af02e8c0e4f9e4745d918a0e`; executed Debug driver SHA-256 `6291ad5e6132591f17a6b3abefa178edb5c57512a5a30cfc863d4628d23e1d99`. The source archive contains the versioned working-tree patch and untracked-source archive against the HEAD above. Later edits invalidate reuse as evidence for a different source state.

### U2: full-scope acceptance contract

Implemented the mandatory EB-009 requirement in the External Beta contract and central READY evaluator. The typed registry covers all 20 R requirements, 18 V packages and 83 child cases; the closed evidence envelope requires a hash-bound full-product report. Both READY and CLOSED reject legacy eight-row candidates and forged ninth-row PASS summaries until U45's semantic validator is genuinely implemented. The reference hashes actual full-contract bytes, and the outer acceptance/candidate-root commitment binds that content transitively. The authority amendment preserves historical creator-study results while recording the user's superseding full-scope decision.

The root reader review reproduced an indefinitely waiting FIFO and oversized/ambiguous JSON reaching definition validation. `full_product_contract.py` now checks regular-file type and a 1 MiB ceiling before opening, checks opened-file identity/type/size, and reads at most the limit plus one byte. It rejects duplicate keys, nonfinite constants and exponent overflow, and nesting beyond 64 levels. All six reader tests pass, including exact-limit acceptance into definition validation and FIFO rejection in a timeout-bounded child process. The combined reader/gate suite passed 19 tests before the later contract-definition revisions. Ruff is clean, and an independent narrow security recheck found no actionable issue. This is reader-boundary evidence, not release authorization.

The separate contract-definition review identified missing canonical nonnumeric protocols, typed empirical result dimensions, and explicit whole-phrase versus forced-chunk continuity proof. A full suite run during those edits observed 140 tests with one canonical-definition mismatch; this intermediate state is retained as a failed run, not counted as a completed integration check.

Those definition repairs are now verified on the stable handoff. Versioned canonical protocol/check definitions prevent prose replacement from waiving counterbalancing, independent review, complete-song production or current-audio bounce requirements. Eleven empirical criteria require 175 typed cells with exact dimensions, units, comparators and environment/resource/provider/precision bindings. All checked-in qualification values remain unresolved. Eight existing cases explicitly require whole-versus-forced-chunk phoneme/timing/F0/phase evidence and neighbor-edit invalidation; no R/V outcome or existing case was removed.

The root executor ran all 147 External Beta tests and all 68 production Python tests successfully after handoff. Both registered suites also passed through Debug CTest (28.74 seconds total). Ruff passed on the changed gate/definition/reader modules and focused tests. The independent adversarial recheck reported no remaining finding in the three repaired areas. Root verified full-contract SHA-256 `d97e07403fbdc11a866eeb4b79ce5e1b5725cdd7a6c418f342a7c580b79d633f` (246,977 bytes); reader SHA-256 is `cff0ab840db37adb665f4df3d94cf52aa19c163a1ef2ff4b6e6775238966b3f0`.

Manual CLI checks: `--help` exited 0; the existing blocked candidate exited 3 for READY and CLOSED; a missing input exited 2 with structured diagnostics. The legacy eight-row test candidate sent through stdin also exited 3, with only EB-009 among blocked requirement IDs. It additionally reported the required unverified-archive diagnostic; no archive verification was fabricated. Rejection outputs are retained at `out/fullscope-beta/u2-ready-rejection.json`, `u2-closed-rejection.json` and `u2-legacy-ready-rejection.json`. These are rejection/engineering receipts, never accepted product evidence. U45/U46 remain responsible for actual raw-evidence semantics and all promotion-path closure.

### Support evidence repair discovered during baseline verification

The production-suite failure above identified a missing comparison, not a stale expectation. The support evidence record's `supportBundleSha256` was never compared with the intake's `bundleSha256`. A mismatched record triggered the generic root-chain check, while direct support-evidence validation returned no finding.

Added three focused tests in `tests/production/test_public_support_evidence.py`. Before implementation, missing and mismatched hashes both failed their rejection assertions; the matching-hash characterization passed. Added the missing semantic comparison in `tools/public_release/evidence_validation.py`, and included the matching hash in the test fixture's archived record before computing its roots. No assertion or signature/root check was removed.

The focused evidence, existing gate and restored-archive suites then passed 16 tests, including the original failing test. The complete production Python suite subsequently passed all 68 tests. This closes that observed binding defect only; U44's full support/crash/privacy and installed-platform acceptance remains incomplete.

### U3: bounded musical value types, partial

Added `NoteVibrato` and typed `DynamicsAutomation` as isolated domain values. Vibrato validates finite fractions, combined fades, depth 0–200 cents, period 5–500 milliseconds and phase in [0,1), even while disabled. Dynamics validates nonnegative ticks and linear gain from silence through +12 dB, holds endpoints, interpolates linearly and defaults to unity. Curve replacement rejects duplicate/unsorted points before mutation; edits retain ordered unique ticks. The current per-region bound is 16,384 dynamics points; replacing an existing point remains allowed at that limit.

The new `seam_performance_contract_tests` target was compiled first against declarations only and failed at linking the unimplemented methods. The initial six real-domain tests passed in Debug and Release. A seventh test loads the actual historical schema-2 and schema-3 vocal files, checks their score data and host-offset migration, and verifies complete canonical equality after encode/decode. It also passed in both configurations.

Added `performance_intent.hpp/.cpp`: typed parameter channels, manual replacement or explicit pitch-only offset mode, note-ID or half-open time-range ownership, and captured musical/pronunciation/ownership revisions. Four tests first linked against declarations and failed for missing implementations, then passed through the actual domain library. The revision helper reports Conflict when any captured dimension differs. It is not a take-acceptance transaction, and these ownership values are not yet persisted or consumed by rendering. Independent scoped review found no actionable value-semantics issue.

Three additional regressions exposed genuine migration-boundary defects. Notes and regions previously accepted overflowing end ticks; their validators now reject overflow before any end-tick addition, preserving the exact `INT64_MAX` boundary. The project decoder previously narrowed MIDI key 256 to 0 before validation; it now validates 0–127 before conversion. All three failures were observed before their fixes. After the final decoder repair, both the 14-test focused executable and the core suite passed through CTest in Debug (86.18 seconds total) and Release (10.18 seconds total). The focused library driver also passed by direct invocation in each configuration. Independent review of these small changes found no actionable regression.

Executed focused-binary SHA-256 identities: Debug `ec20c61c7b6cf4370f8105d56f09736279467a66a82541a663653f1acc0b4892`; Release `08efb0545819abf6ef44262b706b2c77b453bcc1767cf4256a3949fecff70464`. These bind the verified value-type/decoder slice, not an installed release or an audible expression workflow.

At this earlier isolated-value checkpoint the writer remained schema 7, with no new saved-performance field or renderer behavior claimed. The schema-8 integration below supersedes that persistence status; pronunciation identity, proposed/accepted takes and revisioned ownership remain required before U3 completes.

After the requested push, continued U3 with typed `VoiceStyleSelection` provenance and a shared sample-bank style resolver. It requires exact ID/version/SHA-256 and trusted-installed status even when the general catalog permits a development fixture. Legacy unresolved intent remains unchanged for missing, mismatched, untrusted or opaque-hash resources. An exact trusted legacy bank preserves its declared first style; a new multi-style bank requires a deliberate choice, and an absent selected style remains missing without substitution. Invalid provenance, unbounded/malformed UTF-8 style IDs and malformed manifests are rejected.

Five new scenarios first compiled against declarations and failed with missing implementations. After implementation the actual catalog/resolver driver passed all 19 performance tests in Debug and Release; Debug CTest also passed. A scoped independent reviewer found no actionable defect. At that checkpoint these were library-level intent/resolution contracts, not completed project persistence or UI migration. The continuation below wires the post-load/relink paths.

### U3 continuation: schema-8 expression/style integration, still partial

This uncommitted continuation is based on `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d`. The [schema-8 implementation contract](../formats/PROJECT_JSON_V8.md) explicitly remains an in-development contract, not a frozen public format. The approved implementation plan and fixed auditory corpus were not modified.

**Implemented:** note vibrato and nullable phonetic hints, region dynamics, track style provenance, and five persisted technical-lane presentations. The codec writes schema 8, preserves version-specific legacy defaults and rejects omitted/malformed new fields, invalid numeric bounds, malformed hints/styles and future/fractional versions. Schema 7 retains its exact four-lane shape. Stored float round trips and raw decimal bounds are tested separately; tiny out-of-range values cannot become valid through narrowing. Historical schemas 2/3/5/6/7 are loaded from fixed files; schema 1 is an explicit characterization fixture and schema 4 is derived from a fixed older fixture, not mislabeled historical evidence.

The shared legacy resolver is wired into open, autosave recovery, initial runtime setup, plugin replacement and later relinking. Integration tests create, sign, pack, install and verify a real synthetic test bank. They do not fabricate trusted receipts or qualify its sound. Opening/recovering preserves input bytes and the durable base hash; safe save materializes schema 8. Missing/untrusted/mismatched exact banks stay unresolved. Plugin replacement refreshes the catalog and migrates its local replacement before publication, preserving the current document on failure.

`EditPerformanceCommand` applies bounded note/hint/dynamics/style batches as one revision with precise audio impact and field-scoped undo/redo. Bank and style changes are also coupled: unrelated singers cannot inherit legacy provenance; known style choices survive same-singer updates without substitution; a changed unresolved legacy reference resets to new intent. Verified single-style assignments select their sole style, while multi-style assignments require choice. New-project creation uses the same trusted resolution before the first save. Later legacy relinking is undoable and repeat resolution does not add a redundant style revision.

Source review found and the continuation repaired three integration defects: note duplication dropped vibrato/hints; region splitting retained unshifted full dynamics in both halves; bank replacement could misattribute a different bank's first style as legacy. Split dynamics now preserve interpolated boundary gain, right-side coordinates, empty curves, endpoint hold and full 16,384-point one-sided curves. Snapshot extraction preserves only effective dynamics plus required interpolation anchors, propagates validation errors, freezes all new note/style fields, and invalidates identity for relevant changes without including faraway dynamics edits. No new audible DSP or persisted-style rendering behavior is claimed here.

**Observed verification:**

| Executable / check | Debug | Release |
|---|---:|---:|
| Full strict build | Exit 0 | Exit 0 |
| `seam_performance_contract_tests` | 23/23 | 23/23 |
| `seam_schema8_performance_tests` | 13/13 | 13/13 |
| `seam_style_migration_tests` | 9/9 | 9/9 |
| `seam_plugin_style_migration_tests` | 3/3 | 3/3 |
| `seam_performance_command_tests` | 7/7 | 7/7 |
| `seam_performance_snapshot_tests` | 7/7 | 7/7 |
| `seam_performance_edit_preservation_tests` | 5/5 | 5/5 |
| Core `seam_tests` | 445/445 | 445/445 |
| Existing project lifecycle, voicebank and standalone suites | 32/32, 13/13, 1/1 | 32/32, 13/13, 1/1 |
| Lifecycle and voicebank source-contract checks | Both passed | Both passed |

The first broad run found two old literal schema-7 expectations in current-writer routing/export tests. They now explicitly expect schema 8; historical inputs were not blindly relabeled. The later broad run passed all production/core paths but exposed an ordering error in the new duplication test: selection IDs are unordered. The test now uses the duplicate API's returned identity, confirms pitch/source correspondence, and retains all expression/undo assertions. Only test code changed after the successful core runs; all seven focused suites were rebuilt/re-run and passed (67 cases each configuration). No failing product assertion was removed or weakened.

Retained local logs: `build/dev/Testing/u3-schema8-integration-detailed.log` and the corresponding Release path preserve the broad run, including that test defect. `u3-schema8-focused-green.log` in both Testing directories records the final seven-suite pass. Core executable SHA-256: Debug `b1d672ea8d7011549adbb0781857cb33c013ba22070831ff74b85566d9d31a78`; Release `350e81c0c8e3f3ee7432d515e4e76ee169008ddaaac5615c488e2f3da2b2aaa4`. Schema test executable SHA-256: Debug `0e923587925c8577efe0f1770a01e835e301fe9d95e1ab370b985701e0629a7b`; Release `71f69262060b7e5ae38000d522988b760f2e78c00504a055aef8f9813cad129d`. These bind development binaries, not installed-release provenance.

Independent read-only reviews found no remaining actionable issue in the atomic batch command, repaired bank/style migration and numeric-codec paths. The root executor reviewed snapshot and split/duplicate changes and ran the actual native library/integration drivers. No new visual-layout or installed-host certification is claimed. The Dynamics presentation is persisted, but the visible native technical editor still renders four lanes; the planned Dynamics editor remains outstanding.

At that checkpoint, pronunciation/ownership/take persistence remained required. The next continuation below supplies its bounded state and topology preservation. U4/U6/U8 and later units still own pronunciation compilation, audible performance, backend capabilities and the complete effective render-input projection. The study-only Python USTX bridge still rejects schema 8 and requires deliberate interchange implementation, not a blind version bump. Neither checkpoint completes U3 or increases the completed roadmap-unit count.

### U3 continuation: region-owned performance data and topology preservation

Added `RegionPerformanceState` to the canonical vocal region. It stores three durable revision values, optional pronunciation identity, manual note/range ownership, immutable proposed/rejected take payloads and separate accepted channel/scope selections. Takes bind generator/version/seed, source region, captured revisions, resource kind/identity and pronunciation resource/input/sequence digests. Whole-take acceptance is deliberately not represented by a boolean: selected channels/ranges reference payloads independently. Old proposals remain saveable without becoming accepted or current.

The new domain validation enforces typed channel units, finite values, explicit unvoiced pitch, unique ordered points, bounded current references, unambiguous ownership/selections and checked signed source-time mapping. Limits are 4,096 ownership records, 16 takes, 4,096 selections, 16,384 points per lane and 65,536 points across one region's take payloads. Distinct overlapping notes can own independent pitch; duplicate same-note or ambiguous note/range and range/range bindings cannot depend on vector order. The manual eligibility helper keeps pitch offsets separate from replacement and excludes generated pitch when manual vibrato owns the note. The runtime/compiler still needs to call this policy as part of the actual acceptance/render workflow.

The schema-8 region object now requires `performance`. Counters, source region IDs and seeds use canonical unsigned hexadecimal strings, preserving all 64 bits without JSON-number precision loss. The closed nested structures reject unknown fields/discriminants, wrong units, invalid null values, malformed counters and oversized collections. The decoder counts the aggregate point budget before constructing each domain point vector. No PCM or tensors are embedded. Saved identity fields are structural data, not proof that a resource exists, is trusted or produced the claimed output.

`transformRegionPerformance` provides shared topology handling. Delete-note commands capture one before/after aggregate per affected region, remove live note-owned bindings, preserve explicit time ranges and historical takes, and restore exact state on undo. Region/track duplication remaps current note IDs while retaining original take provenance. Splitting intersects range scopes, translates the right side and adjusts the accepted source offset without rewriting take payloads. Changed contexts clear the current pronunciation identity. The helper validates complete source notes and mappings before arithmetic or mutation.

Render snapshot extraction now preserves relevant ownership and accepted selections, omits unselected takes and clears current revision bookkeeping. Adding an unused proposal does not invalidate the snapshot; adding an accepted selection does. The same canonical data survives plugin encode/decode/replacement and a real autosave write/discover/recover path, with recovery still dirty and no extra selection silently accepted.

**Test-first evidence:** five domain cases initially linked against declarations and failed for absent implementation; they then passed. Canonical round-trip and project-reference validation subsequently failed their assertions before codec/validator wiring. Delete, split and duplicate commands each failed with nonempty live state before their shared transform was integrated. The snapshot test reproduced accepted state being dropped before the projection repair. Independent review found an overlap-validation defect that conflated different note identities; its regression failed before the identity-aware repair. A signed-offset regression also failed before replacing the unnecessary nonnegative restriction with checked signed arithmetic. Additional malformed-codec, collection-budget and real lifecycle checks exercise the implemented boundary without fabricated resources or generator output.

**Final observed verification for this continuation:** full strict Debug and Release builds both exited 0. All 14 selected CTest entries passed in Debug (91.30 seconds) and Release (19.45 seconds). The eight focused U3 executables contain 83 passing cases in each configuration: 23 existing value contracts, 15 region-state/domain/codec/topology/plugin/recovery cases, 13 schema cases, 9 style migrations, 3 plugin-style cases, 7 command cases, 8 snapshot cases and 5 edit-preservation cases. The core suite passed 445 cases in both configurations; existing lifecycle, bank and standalone suites passed 32/13/1 cases, and both source-contract checks passed. `git diff --check` passed.

Detailed retained logs are `build/dev/Testing/u3-region-performance-detailed.log` and the corresponding Release path; the CTest summaries are `u3-region-performance.log`. Region-state executable SHA-256: Debug `b854164a20ae7c9c3f0a78444a4b11617bea2d31646f4df90d093e9bae41a12d`; Release `a04b8b739f9a6dc9233ad93173fae6f1601571101b36d0d0714d746563cec406`. The source remains an uncommitted continuation of `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d`; prior binary hashes are not reused for this source state.

Independent read-only recheck found no remaining actionable defect in the overlap rule, signed offsets, transform/callers or snapshot projection. That review is not runtime proof; the root ran the actual native drivers and integration tests above. No new model, production voice, listening judgment, visible ownership editor or audible advanced-expression backend was delivered by this state-persistence work.

**Remaining U3 integration:** allocate fresh durable stamps after relevant transactions without reviving stale jobs after undo/reopen; bind runtime work to document-instance/request and resource identity; persist stable phoneme-edit correspondence and explicit unresolved legacy bindings; couple manual ownership/hint/expression changes atomically; complete selected-note duplication and move/resize behavior for live ownership/selected take timing. These are not deferred from Beta GO. U38's acceptance transaction and U6/U8's audible/effective-input consumers remain mandatory downstream work. U3 is still in progress.

### U3 continuation: live-result freshness, 2026-09-06

Added an opaque `PerformanceJobContext` with an immutable source-project snapshot, a session/generation identity and a single-use completion receipt. `EditorSession` rotates generation identity after successful musical changes, including undo/redo, based on actual input comparison rather than trusting impact labels. Replacement always creates a new generation. An old context cannot become valid in another or reopened session, even with identical project bytes; retaining its identity object prevents allocator address reuse from reviving it. View-only and cosmetic/mix changes do not invalidate it, and unused proposals are excluded from musical input comparison. Ordinary transactions skip the extra comparison when no current-generation context is outstanding.

The guarded execution API validates the context before applying a command and consumes that job's receipt only after success. `ProjectDocument` forwards it with dirty-state synchronization and preserves the durable base hash. `AuthoringRuntime` uses a shared post-command path so successful results schedule the usual preview, while stale results do not mutate the score or submit another render. Validation/publication remains serialized on the editor owner thread; workers only read their captured immutable source.

Test-first checkpoints reproduced missing context, document-publication and runtime-publication implementations before their respective integration. The resulting 13-case native driver tests successful publication, duplicate completion, edit→undo/redo, view-only edits, failed commands, replacement, different/destroyed sessions, visible mutable-accessor changes, misleading impact labels, resource/tempo/style/ownership changes, selected versus unused take payloads and durable document identity. Its runtime case creates a synthetic Raw test bank, performs the real initial preview, publishes a valid result and waits for the new revision's real preview, then verifies stale completion leaves both score and render-submission count unchanged. This is workflow proof using diagnostic audio, not pitch-quality or singer qualification.

Both full strict builds exited 0. All 15 selected CTest entries passed in Debug (98.80 seconds) and Release (21.11 seconds), including 96 focused U3 cases and 445 core cases in each configuration, plus the existing 32/13/1 lifecycle/bank/standalone cases and two source-contract checks. `git diff --check` passed. Logs are `build/dev/Testing/u3-job-context-20260906.log` and the corresponding Release path. Job-context executable SHA-256: Debug `39735577af92fbcef3ca3bde405b96ec82afc5deb7e1589ec2abbebef8ef9778`; Release `e9c7ff72228652d63a17da9559b1c9eed9cdab554dfd9296f2dd85a8164a905c`.

The source remains local and uncommitted. This guard does not serialize authorization, verify resource bytes, generate a voice, or implement the U38 take-admission policy. Durable musical/pronunciation/ownership revision/hash reconciliation, stable edit bindings, coupled ownership commands and remaining topology consumers are still required. All musical mutations must use managed transactions; the legacy mutable project accessor can be checked for currently visible changes but cannot expose a transient external mutation already restored before validation. The completed roadmap-unit count is unchanged and the full Beta GO objective remains active.

### U3 continuation: coupled ownership and selected-note duplication, 2026-09-06

Implemented explicit `RegionOwnershipEdit` in the existing atomic performance command. Ownership can now change with hint/vibrato/dynamics/style in one transaction, with prepared-state conflict checks, bounded validation before mutation, overflow-safe ownership revision advancement and exact undo/redo. Ownership intent is not inferred from note values: hint-only edits do not silently claim pitch, and disabled manual vibrato may retain explicit pitch ownership. This is not yet global musical/pronunciation revision reconciliation.

Implemented `CopyNotePerformanceCommand` and wired it into the actual piano-roll duplicate composite after note creation. It copies note-scoped ownership and accepted selections, translates selected source offsets with checked arithmetic, retains immutable takes and fixed region-time scopes, and clears current pronunciation identity. Undo restores the prior aggregate before removing duplicated notes. Invalid/repeated/cyclic mappings, ownership conflicts, collection limits and offset overflow reject the operation. Duplicate start arithmetic is now checked before constructing notes. The existing UI integration test now verifies copied ownership, accepted source mapping, unchanged takes, fixed ranges and exact history, in addition to basic note expressions.

Verification: affected targets rebuilt with strict compiler settings in Debug and Release. All four selected CTest entries passed in both configurations: 12 performance-command, 5 edit-preservation, 13 runtime-context and 445 core cases (475 cases per configuration). Debug elapsed 85.03 seconds; Release 12.23 seconds. `git diff --check` passed. These were targeted builds/tests, not a new full release certification; existing duplicate-library linker warnings remain.

U3 remains open: global durable musical/pronunciation revision reconciliation, stable phoneme-edit bindings and move/resize treatment of selected take timing still require implementation. U4 has not been declared started or completed; the requested U3-to-U4 handoff is not yet fulfilled. Changes remain local and uncommitted, and the completed roadmap-unit count is unchanged. No new voice or acoustic qualification is claimed.

### U3 continuation: move/resize accepted-source mapping, 2026-09-06

Replaced the move/resize mutation loops with shared staging of affected regions. Moving a note subtracts its translation from note-scoped accepted source offsets, so the same captured phrase follows the note. Edge resizing retains the source offset, trimming or extending in that source timeline instead of slipping the source onset. Immutable take payloads, note ownership records and explicit region-time scopes remain unchanged. Complete region validation happens before any affected region is published, including for direct command calls; conflicting ownership, missing captured-source coverage, duplicate targets, invalid note geometry and checked-offset overflow reject without partially applying a multi-note edit. Undo/redo preserve exact project equality in the new regression tests.

Three new region-performance cases cover translation with fixed range selections, edge trimming, source coverage rejection, and ownership-collision rejection after another note has been staged. Affected targets compiled under strict Debug/Release settings. Both configurations passed 18 region-performance cases, 13 runtime-context cases, 5 edit-preservation cases and all 445 core cases (481 cases per configuration). Debug region/core CTest elapsed 84.47 seconds; Release's combined four entries elapsed 10.65 seconds. The Debug runtime-context/edit-preservation entries also passed separately. Explicit standard-library includes were compiled after the semantic regression run. No full-release or acoustic certification is inferred from these tests.

U3 remains open for durable musical/pronunciation revision reconciliation and stable phoneme-edit binding integration. The remaining work is implementation, not an external blocker. U4 has not yet been started. All changes remain local/uncommitted and the full-scope Beta GO goal remains active.

### U3/U4 shared prerequisite: bounded phoneme-edit correspondence, 2026-09-06

Inspection confirmed that the current Japanese adapter applies overrides by ordinal and can therefore attach an old timing lock to a newly inserted sound. Added `override_reconciliation.hpp/.cpp` in the planned phonemizer module as the matching foundation for stable saved bindings. It operates on one note's verified-compatible, unedited base sequences and retains every original edit. A rebound copy is proposed only when that exact old-to-new token pair occurs in every maximum-length ordered alignment. Symbol, role and voicing must match; timing and custom symbol payload are preserved rather than used as base identity. Repeated/removed/reordered ambiguities and missing original tokens remain explicit unresolved outcomes.

The algorithm validates note identity and contiguous ordinals, rejects edited base tokens and duplicate edit identities, and bounds inputs to 256 tokens/edits per side and 1,024 bytes per symbol before allocating the quadratic alignment tables. It does not use a greedy first-match or unique-symbol heuristic. The output is a correspondence proposal, not authorization to apply it: resolver/resource compatibility and durable source identity must be established by the integrating resolver.

Verification: the new strict Debug and Release target builds succeeded and all five focused tests passed in both configurations. Tests include the actual Japanese adapter's vowel-to-onset/vowel conversion, ambiguous repeated/deleted/reordered sounds, role/voicing changes, malformed and limit inputs, and an independent exhaustive enumeration of every ordered alignment for all 961 pairs of binary-symbol sequences of length zero through four. The oracle agrees with every proposed match. `git diff --check` passed. No full core regression rerun is claimed for this additive, not-yet-called module.

This does not yet repair the live ordinal consumer: schema bindings, pronunciation/resource revision integration and common editor/render consumption remain open. U3 is not complete and U4's dependent end-to-end behavior is not complete. The helper prepares that shared boundary without declaring a milestone handoff. Changes remain local/uncommitted; the full goal stays active.

### U3/U4 integration: live lyric reconciliation and unresolved persistence, 2026-09-06

Connected the correspondence algorithm to `BatchSetLyricsCommand` and routed `SetLyricCommand` through that same transaction. Changed regions are phonemized without overrides on both sides; compatible Japanese/unspecified-language contexts propose rebinding from base tokens, while unsupported/warned/ambiguous cases retain original edits unresolved. Already unresolved edits stay unresolved. Until stable edit IDs replace ordinal storage, key collisions preserve all originals unresolved rather than dropping an edit. Batch duplicate targets are rejected, dependencies are staged/validated before publication, and undo/redo restore lyric and override fields together.

Added persisted boolean `PhonemeOverride::unresolved`. The in-development schema-8 writer always emits it and its reader requires a boolean; versions 1–7 retain their previous false default, which is explicitly not a verified historical binding. The Japanese adapter skips unresolved edits with an explicit warning instead of applying their symbols/timing/locks. Tests exercise the real adapter after a vowel-to-onset/vowel lyric edit, unresolved repeated vowels, save/load and malformed resolution-state rejection, no automatic reactivation, batch collisions, and exact undo/redo.

The first broad regression exposed a native-editor pointer-lifetime defect: publishing the staged whole project invalidated retained region references. Repaired publication to swap only validated lyric/override fields, retaining object addresses; the same native batch-lyrics case and complete core suite then passed. A test helper typo was also corrected to the existing `stringifyJson` API. No failed check was suppressed.

Final verification: strict affected Debug/Release builds succeeded. Four selected CTest entries passed in each configuration: 21 region-performance, 13 schema-8, 5 correspondence and 445 core cases (484 per configuration). Debug elapsed 85.41 seconds; Release 13.27 seconds. `git diff --check` passed. Existing duplicate-library linker warnings remain. These tests prove the implemented lyric path, not a complete pronunciation architecture or release qualification.

U3/U4 remain incomplete: stable token/edit identities and verified legacy bindings, durable musical/pronunciation/resource revision handling, hint/articulation/resource-change reconciliation, unit/seam correspondence and a common revisioned editor/render sequence still require implementation. The model's existing pronunciation metadata is not automatically refreshed by this slice. Source remains local/uncommitted and the full Beta GO objective remains active.

### U3/U4 prerequisite: unit-span and seam-neighborhood correspondence, 2026-09-06

Inspection confirmed the next dependency gap: unit selection verifies spelling/count against the chosen bank unit but still looks up overrides by start ordinal, while phrase rendering looks up seam overrides by incoming ordinal. A matched incoming phoneme alone does not prove that the old join remains valid when its predecessor changes.

Added `reconcilePhonemeSpan` alongside the existing correspondence algorithm. A proposed start key is returned only when every token of the original span is an unambiguous ordered match and all mapped tokens remain contiguous. Insertions inside a span, removed/repeated/reordered ambiguity and missing spans return explicit unresolved `nullopt`; malformed identities, zero/excess counts and invalid base inputs return errors. Bounds are checked before arithmetic/allocation. A two-token span can verify a within-note seam neighborhood rather than matching its incoming sound alone. Cross-note seams still require the upcoming region-level identity integration.

Strict Debug/Release target builds succeeded. All eight focused correspondence cases passed in both configurations, retaining the exhaustive 961-pair oracle and adding unit movement/noncontiguity, seam predecessor changes and span-boundary tests. `git diff --check` passed. This additive API is not yet called by unit/seam consumers; their persisted resolution state, whole-span rebinding and undo integration remain required. No full-core rerun or live unit/seam repair is claimed. U3 remains open and the full goal remains active; changes are local/uncommitted.

### U3/U4 integration: unit/seam resolution state and lyric transactions, 2026-09-06

Extended unit and seam overrides with persisted `unresolved` state and included all three override collections in the lyric command's staged/captured dependencies. Complete within-note unit spans rebind only when all mapped tokens remain contiguous; within-note seams require an unchanged matched two-sound neighborhood. Ambiguity, unsupported contexts, missing spans and key collisions retain original payloads unresolved. Already unresolved entries do not silently reactivate. Undo/redo restores all dependent collections together without replacing region objects.

The unit selector ignores unresolved selections, and the concatenative renderer ignores unresolved seam parameters and unresolved unit loop/pitch-residual settings. The latter renderer path was found during consumer inspection and guarded as well. Existing synthesis tests now prove fallback selection with a retained missing unit ID, default versus active seam settings through the real concatenative renderer, and exact PCM equality when unresolved unit parameters are retained but inactive. These are diagnostic synthesis tests, not singer qualification.

Two new region-state cases cover complete unit/seam movement, changed sounds retained unresolved, codec round-trip, no revival and exact undo/redo. Strict affected builds passed. Debug and Release each passed 23 region-state cases and all 445 core cases (468 cases per configuration). The final Debug core rerun, including the additional renderer assertions, passed in 83.29 seconds; Release's combined entries passed in 11.19 seconds. `git diff --check` passed. No full-release suite is claimed.

Both new schema-8 resolution fields are required booleans; older schemas retain false defaults without claiming verified historical binding. Within-note matching is implemented, but cross-note spans/joins still need the region-level resolver: changed cross-note joins remain unresolved rather than guessing. Stable resource/token identities, other pronunciation-changing commands and a dedicated unresolved-edit UI remain open. U3 and the full Beta GO goal remain active; changes are local/uncommitted.

### U3/U4 integration: cross-note correspondence, 2026-09-06

Replaced the blanket changed-cross-note fallback in lyric transactions with `RegionPhonemeCorrespondence`. It validates region streams (maximum 4,096 tokens, unique contiguous note groups, maximum 256 tokens per note), aligns each original note against the same note ID once, and stores optional target positions for reuse. Unit spans must map fully and contiguously across the new region stream. A seam must retain both original neighboring tokens at adjacent positions; the initial boundary must remain initial. Identical sounds in another note cannot inherit an edit. Every note participating in the applied unit/join must have supported language and warning-free base resolution on both sides.

New tests cover cross-note span movement, insertion breaking adjacency, changed predecessor sounds, note-identity isolation and region/group limits. An actual lyric-command test preserves a cross-note vowel unit/join after adding sounds earlier in the first note, then makes both unresolved when the joining vowel changes, with exact undo/redo. Strict affected Debug/Release builds passed, followed by all 24 region-state, 10 correspondence and 445 core cases in each configuration (479 cases per configuration). Debug CTest elapsed 84.94 seconds; Release 11.97 seconds. `git diff --check` passed. No full-release qualification is claimed.

This supersedes the prior cross-note fallback boundary, but not the remaining durable identity or shared pronunciation compilation work. Changes remain local/uncommitted and U3/the full Beta GO goal remain active.

### U3/U4 integration: source-bound pronunciation resolver and snapshot context, 2026-09-06

Implemented `pronunciation_resolver.hpp/.cpp`. The shared Japanese entry point produces a valid `PronunciationIdentity` with bounded, length-prefixed SHA-256 input and output-sequence identities. CMake hashes the explicit bundled adapter/shared-rule/resolver/phoneme source list and tracks it as configure dependencies. Both configurations embed source-resource digest `d9524f38bfd4aaea73d117598220e3310b65208b52c021ff77d34f32900698d2`. This is bundled-source identity, not a production voice, external dictionary or compiled-binary attestation.

Bounds cover note/lyric/override counts, per-lyric and aggregate Unicode text, repeated lyric references, override symbols and resulting token count. Invalid Unicode/duplicate identities fail explicitly; empty lyric resolution retains pause/warning behavior. Input identity distinguishes unresolved retained work from the resulting active sequence and excludes non-consumed cosmetic/pitch fields.

Technical inspection now uses the shared resolver. Render snapshots resolve the full source region before projecting segment tokens, fixing lost continuation-vowel context when the preceding vowel is outside the rendered segment. They freeze the produced identity and include the resource hash plus projected token sequence in content identity. Tests prove deterministic/source/input-bound identity, unresolved versus active output distinction, expansion/Unicode bounds and a real split-segment continuation snapshot matching full-region resolution. A strict-build shadowing error was repaired by giving the pronunciation result a distinct name; no diagnostic was suppressed.

Final verification: strict affected Debug/Release builds passed. Both configurations passed all 12 correspondence/resolver cases, 9 performance-snapshot cases and 445 core cases (466 cases per configuration). Debug CTest elapsed 85.29 seconds; Release 11.31 seconds. The embedded resource hashes were independently inspected in both generated build descriptions and match. `git diff --check` passed. No full release or acoustic qualification is claimed.

This supplies runtime identity and two real consumers, not durable model reconciliation. Saving current identity after every relevant edit, stable historical edit bindings, remaining consumers/commands, hint interpretation and take-admission authorization remain open. U3/the full goal remain active and changes remain local/uncommitted.

### U3/U4 integration: transactional saved pronunciation identity, 2026-09-06

Lyric transactions now capture and publish current pronunciation identity and revision alongside phoneme/unit/seam dependencies. Each changed region advances its pronunciation revision exactly once and stores the actual bounded resolver result after reconciliation. Unsupported language or resolution failure clears prior identity rather than retaining a misleading value; valid user text remains saveable. Revision overflow rejects before publication. Undo/redo restores the exact before/after metadata and dependent edits, without replacing region objects or overwriting musical/ownership revision components.

Reconciliation now also calls the bounded shared resolver for its unedited source sequences. On failure all retained dependencies become unresolved rather than undergoing an unbounded raw-adapter pass. Tests verify saved identity equals fresh resolution, project round-trip, current identity through real plugin encode/decode/replacement and autosave recovery, exhaustion rejection, unsupported/over-limit resolution, and exact history. The old live job is explicitly verified to remain invalid after undo even though the saved identity/revision is restored: durable fields alone do not authorize publication or acceptance.

Final verification: affected strict Debug/Release builds passed. Both configurations passed 25 region-state cases and all 445 core cases (470 per configuration). Final Debug core rerun elapsed 82.72 seconds and the focused suite 0.69 seconds; Release's combined entries elapsed 10.16 seconds. `git diff --check` passed. Existing duplicate-library linker warnings remain; no full-release certification is claimed.

Other pronunciation/musical/resource edits still require consistent identity/revision maintenance, and stable historical token/edit bindings plus generated-take admission remain open. This closes the lyric persistence path, not U3 or Beta GO. Changes remain local/uncommitted and the full goal stays active.

### U3/U4 integration: phoneme-command identity maintenance, 2026-09-06

Extracted shared pronunciation refresh logic and applied it to direct phoneme upsert/reset as well as lyric edits. Changed phoneme overrides stage and validate the resulting region, advance the pronunciation revision, and save the freshly resolved identity (or clear an unavailable identity) before publishing only the relevant fields. Unchanged upserts consume no pronunciation revision. Overflow/missing/invalid operations do not partially mutate project state.

The two phoneme commands now retain exact before/after override vectors and pronunciation metadata. Undo/redo preserves original vector order and identity/revision rather than reinserting and sorting a single record, while leaving musical and ownership revision components intact. New tests cover sequential upsert/reset, current identity comparison with fresh resolution, project serialization, exact multi-step history, exhaustion rejection and unchanged edits at the counter limit.

Final verification: strict affected builds passed in Debug and Release. Each configuration passed 27 region-state cases and all 445 core cases (472 per configuration); Debug CTest elapsed 82.83 seconds and Release 9.99 seconds. `git diff --check` passed. No full release or acoustic qualification is inferred.

Direct phoneme-symbol changes still need unit/seam correspondence against their effective changed sounds; other musical/resource mutations and stable historical edit identities also remain open. This is identity maintenance for two real command paths, not U3 or Beta GO completion. Changes remain local/uncommitted and the full goal stays active.

### U3/U4 integration: direct phoneme dependency reconciliation, 2026-09-06

Direct phoneme upsert/reset now reconcile unit/seam dependencies against effective resolved before/after sounds. Temporary matching copies remove timing/lock attributes while retaining symbols, roles and voicing; this preserves valid unit/join correspondence for timing-only edits while rejecting changed sounds. The explicitly edited phoneme is not rebound or silently marked unresolved. Bounded resolution failure preserves its requested payload and marks dependent units/seams unresolved. Command history now includes all three override vectors and pronunciation metadata in the same staged publication/undo transaction.

New cases exercise symbol upsert and reset of an active symbol override with cross-note units/joins, retained payloads and fresh identity, as well as timing-only upsert/reset preserving dependencies. Both verify exact multi-step undo/redo. Strict affected builds passed in Debug/Release, and each configuration passed all 29 region-state cases plus 445 core cases (474 per configuration). Debug CTest elapsed 83.06 seconds; Release 10.20 seconds. `git diff --check` passed. No new singer or acoustic qualification is claimed; source remains local/uncommitted.

Milestone boundary recheck: the authoritative plan's U3 is canonical vocabulary, migration and coupled edit persistence, while resolver/correspondence behavior belongs to U4. These recent shared integrations are not additional completed units. The next U3 completion decision must audit its explicit migration, combined project/plugin edit round-trip and rejection/source-preservation scenarios against current code and evidence; later U4 completion work must not silently become an ever-expanding U3 prerequisite. Neither unit nor the full Beta GO goal is declared complete by this entry.

### U3 acceptance audit: historical writers and combined persistence, 2026-09-06

Added genuine historical-writer outputs for schemas 1, 4, 5, 6 and 7, with exact commits, reproducible drivers, seed/output distinctions and byte hashes under `tests/fixtures/projects/generators/README.md`. Each original codec emitted and reloaded its fixture with exact domain equality; the imported bytes were compared against generated files. Existing Phase 2/3 outputs complete the schema range. No current JSON was merely relabeled to create this evidence, and original corpus/seeds remain unchanged. Historical codec/domain/core sources were built unmodified; a schema-4 full-demo WAV warning was avoided by building/linking only the relevant original libraries, not by suppressing diagnostics.

Added one acceptance case for a coupled ownership/hint/style/dynamics/vibrato edit through actual project save/load, plugin encode/decode/runtime replacement, undo and redo; another drives invalid/future project opens and checks unchanged source bytes and live document state. Extended historical migration assertions for original note/lyric/timing/routing values and neutral new fields. Full current Debug/Release builds passed. All 15 selected U3 acceptance entries passed in both configurations (Debug 89.49 seconds, Release 16.88 seconds), with final expanded historical fixture coverage rechecked through the rebuilt schema-8 target in both. `git diff --check` passed.

The explicit audit is `U3_COMPLETION_AUDIT_2026-09-06.md`. Persistence scenarios are verified, but U3 is not declared complete: inspection found default render style selection still falls back to the manifest's first style without consulting saved track intent. Correcting that concrete consumer behavior is next. U4's remaining pronunciation work remains U4 work; the full Beta GO scope is unchanged. Source remains local/uncommitted.

### U3 complete locally; U4 continuation, 2026-09-06

Default snapshot rendering now uses saved style intent, rejects missing selected styles, rejects unresolved legacy defaults until migration and requires choice for unselected multi-style banks. Whole-project rendering no longer injects the first manifest style. Explicit API style arguments remain temporary deliberate overrides; default single-style behavior remains supported without mutating saved provenance. Tests verify the actual selected unit and cache identity, whole-project selected units, and missing-style failure without substitution. The shared fixture supplies both actual declared styles/units.

Full strict Debug/Release builds succeeded. All 15 selected U3 acceptance entries passed after this fix in both configurations (87.78/15.12 seconds). The initial project-render test expected a partial result when every track failed; inspection confirmed the existing API returns an error with style diagnostics in its context, and the test now asserts that contract. No production failure was suppressed. `git diff --check` passed.

The U3 acceptance audit now passes against the authoritative milestone scope. U4 remains active with the already implemented resolver and reconciliation work; its remaining stable historical identities, shared consumers and other edit paths are not silently counted complete. All downstream Beta GO units remain mandatory. Source remains local/uncommitted and publication/release qualification is not claimed.

### U4 continuation: shared native/embedded inspection, 2026-09-06

Removed direct Japanese adapter calls from native scene population, native phoneme/unit hit-testing and embedded-editor inspection. These paths and the technical editor now use `inspectJapanesePronunciation`, a shared UI adapter over the bounded resolver. It preserves ordinary warnings and emits explicit `ResolutionFailure` on resolver failure instead of falling back to raw phonemization. Source inspection confirms no direct adapter calls remain in the inspected native/embedded/technical paths.

Added a real native-controller scene test comparing resolved tokens/warnings and verifying over-limit text yields the shared bounded failure, plus a resolver-inspection test distinguishing ordinary empty-lyric warnings from resolution failure. The source-bound resource hash automatically changed as expected; both generated configurations embed `00251ec8895ec6d39e75cdd6a7fef6259e4346801252922c44da9190e6f2bab9`. This is current source identity, not a release attestation.

Final verification: affected strict Debug/Release builds passed, followed by 13 resolver/correspondence cases and all 446 core cases in each configuration (459 per configuration). Debug CTest elapsed 84.06 seconds; Release 9.89 seconds. `git diff --check` passed. No full-release suite or visual/acoustic qualification is inferred.

U3 remains accepted at its implementation boundary. U4 still requires stable historical token/edit/resource bindings and remaining pronunciation-changing paths; this does not claim all U4 scenarios complete. Changes remain local/uncommitted and Beta GO remains unfinished.

### U4 continuation: resolved token context addresses, 2026-09-06

Added runtime `PhonemeToken::contextId` and `lyricOwner`, populated by the shared resolver and propagated through existing editor inspection and frozen snapshot tokens. Addresses bind exact source resources, region/note/lyric ownership, the note's effective sound sequence and ordinal. Timing/lock-only edits and unrelated-note sound changes retain the address; changed note sequences invalidate it, including ambiguous repeated-sound insertion. Bound tokens require a lowercase SHA-256 address and valid lyric owner; raw adapter output remains explicitly unbound.

Sequence hash format v2 includes context addresses/owners, and input hashing includes region identity. Source configuration regenerated the same resource hash in Debug and Release: `4edb5a1fb259b6ded9967422be6001e70c943fad9649bcf1ca6c4537ca48b2be`. Tests cover address stability/invalidation, distinct duplicate sounds, owner validation and existing editor/snapshot token equality. These are context-scoped content addresses, not a claim of persistent edit lineage or authorization to apply arbitrary saved work.

Final verification: strict affected Debug/Release builds passed; each configuration passed 14 resolver/correspondence cases, 11 snapshot cases and all 446 core cases (471 per configuration). Debug CTest elapsed 84.57 seconds; Release 11.09 seconds. `git diff --check` passed. No full release or acoustic qualification is inferred.

Stable persisted historical bindings and their validation/reconciliation remain required for U4. No unit completion or Beta GO is claimed; changes remain local/uncommitted and the full goal stays active.

### U4 continuation: persisted phoneme source-context bindings, 2026-09-06

Added nullable `PhonemeOverride::sourceContextId`, bounded/validated as lowercase SHA-256 and required as a nullable field by the in-development schema-8 codec. Explicit new/changed phoneme edits obtain a binding from unedited base resolution; deliberately appended slots have a separate base-relative address. Supplied stale bindings reject changed commands before mutation. The shared resolver verifies bound records against current base contexts and suppresses mismatches in its effective copy while retaining saved user data. This bounded extra resolution has no override recursion beyond the cleared base.

Lyric reconciliation now verifies a bound original context before moving its edit, then records the matched new context. Failed verification remains unresolved rather than rebinding by ordinal. Binding data participates in input hashing and command history. Null legacy bindings retain the existing compatibility behavior and are explicitly not verified historical work; their migration and copy/split/resource-change rebinding still require implementation.

Tests cover actual command creation, project save/load equality, suppression after an out-of-band lyric change, stale-command conflict without mutation, verified lyric rebinding/undo, appended-slot versus new-base-token confusion, and malformed binding rejection. Strict affected builds passed in Debug/Release. Each configuration passed 33 region-state cases and all 446 core cases (479 per configuration). Debug core elapsed 84.07 seconds; final focused Debug/Release runs elapsed 0.93/0.48 seconds; Release core passed in 9.95 seconds. `git diff --check` passed. No full-release qualification is claimed.

U4 and Beta GO remain unfinished; U3's prior implementation acceptance is not a claim of historical-binding completion. Changes remain local/uncommitted and the full goal stays active.

### U4 continuation: region copy/split binding transfer, 2026-09-06

Added bounded `rebindTransferredPhonemeContexts` and connected it to region/track cloning and both sides of region splitting. The explicit note map is validated before mutation. Active bound records transfer only when the original payload and source binding verify, source/destination base sounds match in a supported unchanged language, and resolution has no note warning. Destination addresses reflect new region/note/lyric IDs. Stale or already unresolved bindings are never promoted; a split that removes continuation context preserves the old edit unresolved. Unbound legacy records are not invented into verified bindings.

Tests execute actual duplication/split commands and verify active copied locks, fresh addresses, unchanged original records, stale-binding retention, continuation-context loss, preserved timing payloads and exact undo/redo. Strict affected Debug/Release builds passed; each configuration passed 35 region-state cases and all 446 core cases (481 per configuration). Debug CTest elapsed 86.09 seconds; Release 11.57 seconds. `git diff --check` passed. No full-release certification is claimed.

Immutable take provenance and existing performance-state transformations remain unchanged. Selected-note duplication and historical/resource-change binding migration remain open; U4 and Beta GO are not complete. Source remains local/uncommitted.

### U4 continuation: selected-note phoneme edit copying, 2026-09-06

Extended the actual piano-roll duplication composite's `CopyNotePerformanceCommand` to copy note-scoped phoneme overrides and rebind verified contexts to copied IDs, alongside existing ownership/accepted-performance preservation. Candidate regions are validated before publication; copied key collisions and collection limits reject without partial changes. Before/after phoneme vectors are stored with performance state so undo restores dependencies before the composite removes duplicated notes. Binding work is skipped when there are no active bound records.

The existing real piano-roll duplication test now starts with a command-created timing lock and verifies its copied source-context address, preserved offset and active resolved lock, plus exact undo/redo. A new direct command regression rejects a phoneme-key collision with unchanged project state. Strict affected Debug/Release builds passed; each configuration passed 13 command cases, 5 edit-preservation cases and all 446 core cases (464 per configuration). Debug CTest elapsed 90.24 seconds; Release 11.94 seconds. `git diff --check` passed. No full-release qualification is inferred.

Selected-note cross-note unit/seam span copying and historical unbound-record migration remain open; no U4 or Beta GO completion is claimed. Changes remain local/uncommitted.

### U4 continuation: explicit pre-binding schema migration, 2026-09-06

Schemas 1–7 now migrate existing phoneme/unit/seam overrides as unresolved, preserving original keys, symbols, timing, locks, unit IDs and seam settings without inventing verified context bindings. Newer resolution/binding fields injected into an old schema do not bypass this policy. This intentionally supersedes the earlier false-default compatibility behavior: legacy manual edits remain saved but require deliberate resolution before affecting audio again. Canonical state/cache identity changes accordingly; no original project file is rewritten by decoding.

Historical-writer migration tests now check unresolved/manual-binding defaults across the actual fixture set. An explicitly labeled characterization seeded by the historical schema-4 fixture adds unit/seam controls and false binding claims, verifies payload preservation and inactive resolved timing, then saves/reopens schema 8 with exact equality. A test initializer brace error was corrected before successful builds; no production validation was weakened.

Final verification: strict affected Debug/Release builds passed; each configuration passed all 14 schema-8/migration cases and 446 core cases (460 per configuration). Debug CTest elapsed 84.84 seconds; Release 11.45 seconds. `git diff --check` passed. No full-release or acoustic qualification is claimed.

Review/rebinding UI and schema-8 unbound-record handling remain required, along with remaining U4 edit paths. This is pre-binding schema migration, not U4/Beta GO completion. Source remains local/uncommitted and the full goal stays active.

### U4 continuation: deliberate phoneme review/rebind transaction API, 2026-09-06

Added read-only `reviewPhonemeBindings` to the technical editor, returning retained unresolved/unbound/stale edit payloads, current base target tokens/context IDs and resolver warnings. Added explicit `rebindPhonemeOverride`: it verifies the reviewed source still matches, verifies the chosen target context, rejects occupied destination keys and publishes retained payload/key/context changes through the normal command/dirty/notification path. Moving to another key composes remove/upsert into one editor undo step; no automatic review acceptance is performed.

New controller tests verify read-only review, retained timing applied to the deliberately chosen current vowel, a bound resolved result, exactly one editor revision/edit notification, exact undo/redo, and unchanged project/revision/notification count for stale source, stale context or occupied-target rejection. Strict affected Debug/Release builds passed, followed by all 448 core cases in each configuration (83.38/9.83 seconds). `git diff --check` passed. No full-release qualification is inferred.

This is the backend transaction API, not a completed visible review panel. Unit/seam review and remaining U4 binding/consumer work remain open; source is local/uncommitted and Beta GO remains unfinished.

### U4 continuation: review target eligibility, 2026-09-06

Inspection found that the new review API exposed fallback tokens from unsupported/missing lyrics as if they were valid rebinding targets. It now excludes warning-affected notes and unsupported declared languages while retaining their original edits and warnings. Unaffected notes remain available. The existing commit-time review refresh enforces this eligibility as well as context identity, including language changes that leave kana/token context bytes otherwise identical.

A new regression covers unsupported declared language, empty lyric and unknown-character fallback, preserving retained payloads and unrelated targets, and rejecting the stale target without project/revision/edit-notification mutation. Strict affected Debug/Release builds passed and all 449 core cases passed in each configuration (82.45/9.46 seconds). `git diff --check` passed. No full-release qualification is claimed.

This prepares the review API for UI exposure; it does not implement the visible panel or complete U4. Changes remain local/uncommitted and Beta GO remains unfinished.

### U4 continuation: visible native/embedded phoneme review, 2026-09-06

Added a compact Review phoneme edits overlay and lower-right entry control, wired to the checked technical-editor API in standalone and embedded hosts. It displays retained key/symbol/timing/lock data, allows source and current-sound navigation, requires explicit target selection before Apply, reports stale-review failures without mutation, refreshes after success and closes with Close/Escape. Embedded callback publication also updates its dirty state. A user guide is `docs/authoring/PHONEME_EDIT_REVIEW.md`.

Mouse, Tab/Shift-Tab, Enter and accessibility actions share the same controls/geometry. Opening cancels pending drag mode, modal input blocks background edits/scroll/accessibility actions, and disabled buttons do not execute. Visual inspection exposed disabled-control focus and background-note virtualization; these were repaired and asserted in tests. A drawText overload ambiguity was fixed with explicit point types, without suppressing compiler diagnostics.

Tests drive the actual native controller/backend through open, explicit target selection, keyboard Apply, exact undo, stale-error presentation and no mutation on failure; an embedded-runtime test verifies the real callback and dirty state. Strict affected Debug/Release builds passed and all 451 core cases passed in each configuration (81.65/9.17 seconds). `git diff --check` passed. A 480x320 software-rendered capture was inspected before and after the focus repair; panel text and controls fit without overlap, and initial Tab focus lands on enabled Close. The final preview is `out/fullscope-beta/u4-review-20260906/phoneme-review-480x320.png`. This is software-frame QA, not installed-host or complete visual/acoustic qualification.

Unit/seam review and remaining U4 identity/edit-path work are still outstanding. U4 and Beta GO remain unfinished; source remains local/uncommitted and the full goal stays active.

### U4 continuation: review modal draft preservation, 2026-09-06

Opening phoneme review previously cancelled active text composition silently. The controller now rejects opening until the user finishes or explicitly cancels the text edit, preserving the pending lyric draft and avoiding the review callback entirely. Pointer move/up events are also ignored while the overlay is open, preventing hover or gesture updates in the score underneath.

A native controller regression verifies preserved composition text, unchanged project/revision, no review callback on rejected opening, successful opening after explicit cancellation, and no background pointer mutation. All 452 core cases passed in both Debug and Release (82.70/9.33 seconds). These results qualify this local regression repair, not installed-host or acoustic readiness.

Unit/seam review and remaining U4 identity/edit-path work remain open. U3 implementation acceptance remains complete locally; U4 and Beta GO remain unfinished. Changes remain local/uncommitted.

### U4 continuation: retained render-edit review and explicit seam rebinding, 2026-09-06

Added a read-only retained unit/seam inventory with the effective pronunciation identity, complete token sequence and warnings. Keeping the full sequence avoids inventing adjacency by filtering unavailable notes. The new seam-rebinding transaction checks the reviewed source payload, current pronunciation and target sequence, target occupancy, and warnings affecting either the incoming token or its predecessor. It preserves seam settings and publishes an explicit target change as one undoable editor action. Unsupported language boundaries cannot be accepted through fallback tokens.

Inspection also exposed seam command undo sorting original payload vectors. Seam upsert/remove commands now capture and restore the original vector, preserving exact saved ordering during rebinding undo. Regression coverage includes unsorted retained data, exact undo/redo, a single revision/notification, stale pronunciation, stale source payload, occupied destinations and a warning on the preceding note. An invalid empty-seam test fixture was corrected to contain an actual seam setting; production validation was not relaxed.

Final verification: affected strict Debug/Release builds passed, followed by all 454 core cases in each configuration (82.48/9.16 seconds). `git diff --check` passed. This is a backend seam transaction and retained-unit inventory, not the combined review UI or unit-rebinding implementation. U4 and Beta GO remain unfinished; changes are local/uncommitted.

### U4 continuation: explicit retained-unit span rebinding, 2026-09-06

Added `rebindUnitOverride` to the technical editor. The transaction requires the reviewed source payload and current complete pronunciation sequence to match, preserves the saved token count and unit/renderer/tuning/lock settings, and checks the whole destination span. Incomplete spans, warning-affected notes and overlaps with other active or retained unit edits reject without publication. Same-key confirmation and cross-note spans are supported. The target change publishes as one editor action; unit upsert/remove now restore original vector ordering on undo, matching the seam repair.

Regression tests cover same-key confirmation, moved binding, cross-note spans, exact project undo/redo with unsorted saved vectors, one revision/notification, stale pronunciation, changed source payload, incomplete spans, unsupported-language coverage and overlaps with both active and unresolved records. Rebinding verifies the pronunciation address, not installed unit availability or acoustic suitability; resource validation remains a separate obligation. Combined review UI and remaining U4 identity/edit paths are still open. Changes remain local/uncommitted and Beta GO remains unfinished.

Verification for retained-unit rebinding: affected strict Debug/Release builds passed; all 456 core cases passed in each configuration (82.20/9.46 seconds). `git diff --check` passed. These are local implementation checks, not installed-host or singer qualification.

### U4 continuation: combined retained-edit review UI, 2026-09-06

Extended the existing native review overlay to navigate retained phoneme, unit and seam records in one list. Unit summaries show saved ID, span, renderer and lock state; seam summaries show key, amount and overlap. Target summaries identify the end of a unit span or the preceding seam sound/initial boundary. Changing records clears target selection; Apply uses the checked backend transaction for that record type and refreshes the list. Standalone and embedded hosts wire both new callbacks, including embedded dirty-state publication. Existing modal keyboard/accessibility behavior remains shared; internal accessibility IDs stay compatible while the visible entry/title now say Review retained edits.

New tests drive the mixed native panel through navigation, explicit selection, unit/seam application, exact undo and stale-review failure without mutation. Embedded-runtime tests exercise both real host callbacks and dirty state. A 480x320 software frame was inspected with a unit record displayed: title, source, status and six controls fit without overlap. This is software-rendered UI evidence, not installed-host qualification. The authoring guide now documents the combined workflow and pronunciation-versus-resource limits.

Verification: affected strict Debug/Release builds passed and all 458 core cases passed in each configuration (81.87/9.35 seconds). `git diff --check` passed. Remaining U4 identity/edit-path and resource obligations are still open. This UI does not certify installed unit availability or singer quality. Changes remain local/uncommitted; Beta GO remains unfinished.

### U4 continuation: note-add pronunciation metadata, 2026-09-06

`AddNoteCommand` previously changed the token-producing note/lyric inputs without updating saved pronunciation metadata. It now stages and validates the region before publishing note/lyric vectors, refreshes the identity through the shared resolver for supported referenced lyrics, and increments the pronunciation revision with overflow rejection. Unsupported languages retain an absent identity rather than claiming Japanese resolution. Undo restores the exact prior identity/revision; redo restores the captured result. Other performance channels are not replaced.

A regression compares the saved identity against actual shared resolution, verifies it differs from the pre-add sequence, checks exact project undo/redo, unsupported-language invalidation and atomic revision-exhaustion rejection. Remaining note geometry/topology and dependent-edit correspondence paths still require U4 work; this change is not a complete topology reconciliation claim. Source remains local/uncommitted and Beta GO remains unfinished.

Verification for note-add metadata: affected strict Debug/Release builds passed, followed by 459 core cases and 5 performance-edit-preservation cases in each configuration. Debug CTest elapsed 83.09 seconds; Release 9.79 seconds. `git diff --check` passed. No release or installed-host qualification is inferred.

### U4 continuation: move/resize pronunciation metadata, 2026-09-06

The staged note-geometry path now captures before/after pronunciation metadata for regions whose note start times change. Moves and left-edge resizing refresh the identity through the shared resolver and advance its bounded revision; undo/redo restore captured metadata alongside the existing source-offset transformations. Pitch-only moves and duration-only resizing leave pronunciation revision unchanged because neither is an input to the current pronunciation resolver. Overflow rejects before any staged region is published.

A four-scenario regression compares persisted identity with actual resolver output, checks revision behavior, exact project undo/redo and overflow behavior (including allowing edits that do not consume a new pronunciation revision). This addresses metadata maintenance, not all dependent unit/seam correspondence after note reordering; that remains explicit U4 work. Changes remain local/uncommitted and full Beta GO remains unfinished.

Move/resize verification: affected strict Debug/Release builds passed; 460 core cases and 5 performance-edit-preservation cases passed in each configuration (Debug CTest 82.62 seconds, Release 9.40 seconds). `git diff --check` passed. These results do not establish U4 or release completion.

### U4 continuation: geometry-dependent edit correspondence, 2026-09-06

Note start-time geometry edits now reconcile dependent records before capturing the refreshed pronunciation identity. The application reuses existing bounded correspondence: phoneme bindings follow verified base-sound matches, then unit spans and seam joins follow effective sounds. The second pass starts from original unit/seam records, avoiding mapping an already remapped key twice. Noncontiguous spans, changed predecessors and other unverified matches remain saved unresolved. Captured before/after override vectors restore exact payloads and ordering on undo/redo together with pronunciation metadata.

A command regression contrasts an ordinary time shift with moving the middle note after its neighbor. The ordinary shift preserves bindings; reordering retains the broken cross-note unit and seam unresolved, while preserving a separate unit, the unchanged initial boundary and a bound timing edit. It also checks current saved pronunciation identity and exact project undo/redo. Add/delete/articulation topology paths still need separate examination; this is not U4 completion. Source remains local/uncommitted and Beta GO remains unfinished.

Geometry correspondence verification: affected strict Debug/Release builds passed, followed by 461 core cases and 5 performance-edit-preservation cases in each configuration (Debug CTest 83.01 seconds; Release 9.40 seconds). `git diff --check` passed. No installed-host or acoustic qualification is claimed.

### U4 continuation: insertion-dependent edit correspondence, 2026-09-06

`AddNoteCommand` now reconciles retained override records before computing the new pronunciation identity, using the same base-then-effective correspondence as geometry changes. Before/after phoneme, unit and seam vectors are captured with metadata; publication remains staged and undo/redo preserve original payloads and order. The shared helper is named `reconcileRetainedNoteOverrides` to describe its requirement that all original notes/records survive; it is not yet a deletion adapter.

A three-scenario command regression covers appending an unrelated note, inserting inside an existing cross-note unit/seam, and inserting a different vowel before a continuation. Unchanged bindings remain active; interrupted spans/joins and the changed continuation timing lock remain retained unresolved. Actual resolver identity and exact project undo/redo are checked. Delete/articulation paths remain separate U4 work; source is local/uncommitted and Beta GO is not complete.

Insertion correspondence verification: affected strict Debug/Release builds passed, then 462 core cases and 5 performance-edit-preservation cases passed in each configuration (Debug CTest 84.15 seconds; Release 10.16 seconds). `git diff --check` passed. No release qualification is inferred.

### U4 continuation: deletion-dependent edit correspondence, 2026-09-06

Deletion capture now stages surviving notes with the original override vectors, reconciles base/effective correspondence, then prunes records belonging to deleted notes and unused lyrics. This ordering preserves enough original context to identify broken cross-note spans and changed seam predecessors. The candidate is validated and its pronunciation identity/revision refreshed before normal command publication. Before/after override vectors are captured with the existing performance-state transformation, preserving exact surviving payloads and undo/redo ordering. Revision exhaustion rejects before deletion.

A regression deletes the middle of three notes, covering a cross-note unit, changed seam predecessor, unaffected initial boundary and both ordinary and continuation timing locks. Broken surviving relationships remain unresolved; deleted-note data follows the existing undoable removal policy. Saved identity is compared with actual shared resolution, and exact undo/redo plus overflow rejection are checked. This supersedes the previous helper restriction against deletion: original override vectors must remain intact until reconciliation, but original notes may be absent in the staged target. Articulation/lyric-reference paths still require examination. U4 and Beta GO remain incomplete; source is local/uncommitted.

Deletion correspondence verification: affected strict Debug/Release builds passed; 463 core cases and 5 performance-edit-preservation cases passed in each configuration (Debug CTest 83.16 seconds; Release 9.94 seconds). `git diff --check` passed. No release qualification is inferred.

### U4 continuation: lyric-reference and articulation transaction path, 2026-09-06

`SetNotePerformanceCommand` now builds staged note replacements instead of directly mutating note fields. Changing a lyric reference triggers shared pronunciation refresh and dependent-edit reconciliation; slur-only changes preserve pronunciation metadata because slur is not consumed by the current resolver. The correspondence helper now looks up the target note's actual lyric ID rather than reusing the old note's lyric ID, so unsupported reassigned language cannot be treated as a verified old-language match. Candidate validation and revision-exhaustion checks precede publication; captured overrides and identity support exact undo/redo.

A three-scenario regression checks slur-only preservation, reassignment to a different Japanese sound and reassignment to an unsupported language. It checks unresolved manual/unit edits, actual resolver identity or explicitly absent unsupported identity, exact project undo/redo, and bounded revision behavior. The remaining U4 acceptance needs a fresh source-level review across all consumers and topology paths rather than treating this command repair as milestone completion. Source remains local/uncommitted; Beta GO remains unfinished.

Lyric-reference transaction verification: affected strict Debug/Release builds passed, followed by 464 core cases and 5 performance-edit-preservation cases in each configuration (Debug CTest 84.44 seconds; Release 10.00 seconds). `git diff --check` passed. No U4 or release completion is inferred.

### U4 source-level acceptance review, 2026-09-06

`U4_ACCEPTANCE_REVIEW_2026-09-06.md` records a fresh requirement-by-requirement review of the current worktree. U4 remains incomplete for specific reasons: split unit/seam transfer lacks relationship verification, selected-note duplication omits those records, and phrase projection needs a span/join regression beyond the existing continuation test. The first two are source-confirmed omissions; the third is explicitly an unverified boundary risk. Next implementation order is split transfer, selected-note transfer, then phrase-boundary verification. This review inspected the existing passing logs (464 core plus 5 preservation cases per configuration); it did not rerun tests or claim broader completion. `git diff --check` passed.

### U4 continuation: mapped split/clone render-edit validation, 2026-09-06

Added bounded `validateTransferredRenderEdits` after phoneme-binding transfer in region/track cloning and both split destinations. It verifies original payload equality, supported unchanged language, warning-free effective sounds and explicit note-ID mapping. Units require every constituent token to match contiguously; seams require both predecessor and incoming sounds, or preservation of the original initial boundary. Missing/changed relationships retain payloads unresolved and never promote already unresolved records.

A split regression checks a left-side cross-boundary unit, lost right-side seam predecessor, unchanged initial and internal boundaries, changed continuation context and exact undo/redo. Broader region-state testing exposed two outdated expectations from earlier metadata changes: left-edge resize now refreshes identity/revision, and deletion now produces a current resolved identity. Assertions now compare fresh resolver output while retaining complete unchanged-performance checks where applicable. No production validation was weakened. Selected-note unit/seam copying and phrase-boundary verification remain open; U4/Beta GO remain unfinished and source remains local/uncommitted.

Split-transfer verification: affected strict Debug/Release builds passed. The unchanged 464-case core suite passed in both configurations (83.14/9.68 seconds). After correcting the two stale test expectations, all 36 region-state cases passed in each configuration (0.85/0.67 seconds). The initial combined runs failed on those two assertions; they are not represented as all-green runs. `git diff --check` passed. No installed-host or acoustic qualification is inferred.

### U4 continuation: selected-note unit/seam copying, 2026-09-06

`CopyNotePerformanceCommand` now copies unit and seam payloads whose start/incoming notes are selected. Bounds and occupied destination keys reject before publication. Only copied records are passed to mapped effective-token validation, with only the selected-note map: an uncopied neighbor cannot complete a partial copied span/join. Original records remain unchanged. Before/after vectors are captured alongside existing phoneme/performance state for exact undo/redo; fixed-region ownership semantics are unchanged.

Direct tests cover complete and partial cross-note units, internal and cross-note seams, renderer/loop-print payload preservation, unit/seam target collisions and exact undo/redo. The actual piano-roll duplication regression checks copied retained unit/seam records as well as existing phoneme and performance preservation. Its interleaved placement breaks cross-note adjacency, so those copies must be unresolved; an initial assertion incorrectly expected them active and was corrected. The contiguous direct-copy scenario independently requires active relationships. Phrase-projection relationship verification remains open; U4 and Beta GO are not complete. Source remains local/uncommitted.

Selected-note copy verification: strict affected Debug/Release builds passed. All 464 core cases passed in both configurations (82.55/9.79 seconds). After correcting the interleaved-placement expectation, 15 performance-command and 5 preservation cases passed in each configuration (focused CTest 0.65/0.77 seconds). Initial combined runs failed on that expectation and are not reported as all-green. `git diff --check` passed. No installed-host or acoustic qualification is inferred.

### U4 continuation: explicit phrase-projection relationship checks, 2026-09-06

Render snapshot construction now checks active projected unit spans and seam predecessors against the full resolved region before filtering tokens. Incomplete relationships reject with an explicit phrase-boundary conflict; the original project is not mutated and edits are not silently disabled. Already unresolved records retain their existing inactive behavior. The selector already rejected incompatible unit phone sequences, but this adds a relationship-specific boundary check and covers seams as well.

A manifest/WAV-backed snapshot regression covers units and seams split across rest-separated phrases, verifies the boundary error and unchanged source, then includes the complete relationship in one phrase and requires successful snapshot construction. It also checks rendering with explicitly unresolved retained records. This is a correctness guard, not the completed product solution: dependency-aware segmentation must still group active relationships while respecting bounded maximum duration. U4 and full Beta GO remain unfinished; changes are local/uncommitted.

Phrase-projection verification: affected strict Debug/Release builds passed, followed by 464 core cases and 12 performance-snapshot cases in each configuration (Debug CTest 83.33 seconds; Release 10.24 seconds). `git diff --check` passed. These checks do not establish dependency-aware segmentation or release qualification.

### U4 continuation: dependency-aware phrase segmentation, 2026-09-06

Phrase segmentation now resolves active unit/seam dependencies into forbidden note-boundary cuts. Overlapping relationships form atomic groups; their full duration is considered before choosing a duration split, allowing a cut before the group instead of through it. Required multi-note groups exceeding the configured maximum duration reject explicitly, as do invalid active addresses/spans and excessive dependency counts. Already unresolved edits do not force grouping. Source data is unchanged.

The manifest/audio-backed projection regression now requires default segmentation to group complete unit/seam relationships and construct successful snapshots. Deliberately partial segments still exercise the defensive snapshot rejection. A separate regression tests overlapping span/seam dependencies, duration lookahead, stable grouping, over-limit rejection and inactive-record behavior. All three findings from the U4 source review now have implementation follow-ups, but final matrix-wide verification remains required before marking U4 complete. Source is local/uncommitted; full Beta GO remains incomplete.

Dependency segmentation verification: affected strict Debug/Release builds passed, then 465 core cases and 12 performance-snapshot cases passed in each configuration (Debug CTest 83.10 seconds; Release 9.94 seconds). `git diff --check` passed. This is focused implementation evidence, not final U4 or release sign-off.

### U4 implementation acceptance and transition to U5, 2026-09-06

The final consumer sweep found and repaired the raw-phonemizer bypass in standalone coverage analysis. Coverage now uses shared bounded resolution; its existing workflow test also verifies rejection of an oversized lyric. The approved U4 criteria were reviewed against command, topology, persistence, native/embedded and render code, including the three findings and their follow-ups in `U4_ACCEPTANCE_REVIEW_2026-09-06.md`.

After rebuilding all eight relevant targets, both Debug and Release passed 574 cases across 8/8 CTest entries (85.95/12.74 seconds). `git diff --check` passed. U4 implementation acceptance is PASS locally. The next active implementation unit is U5: ordered phoneme timing, including sequential nuclei in multi-syllable notes, deterministic offset-to-frame behavior and actionable short-note conflicts. This milestone does not complete the full goal, qualify a singer or publish the uncommitted source.

### U5 start: sequential nucleus timing allocation, 2026-09-06

Added `phoneme_timing_plan.hpp/.cpp` and wired the compiler into `TimingSolver`. The bounded compiler groups tokens by note, assigns successive nucleus anchors in equal elapsed-time portions between tempo-resolved note endpoints, and retains explicit offsets relative to note start. Source sample markers remain separate. Unit placement now uses the first covered nucleus and the last covered token's end rather than giving every unit the whole note. Invalid unit spans, noncontiguous token groups, invalid offsets, insufficient frame space and absolute-tick overflow reject explicitly.

A solver regression uses actual Japanese phonemization and unit selection for `かき` within one note, requires nucleus anchors 12,000 frames apart at 48 kHz/120 BPM, and verifies a 30 ms edit shifts the second nucleus by exactly 1,440 frames. It also rejects zero-length unit coverage and an offset beyond its allocated end. A test-only `Cv` enum spelling error was corrected before successful compilation.

This is the first U5 timing layer, not full ordered-phoneme/audio qualification. Onset/coda-specific placement, source-transition feasibility, coarticulation/clipping, cross-token offset ordering, editor timing presentation and real rendered-audio/undo evidence remain to be completed. U5 and full Beta GO remain open; changes are local/uncommitted.

Initial U5 timing verification: affected strict Debug/Release builds passed; all 466 core cases passed in each configuration (81.57/9.40 seconds). `git diff --check` passed. No full U5, listening or release qualification is inferred.

### U5 continuation: ordered nucleus edits and dependent automatic ends, 2026-09-06

The timing compiler now requires strictly increasing nucleus anchors within each note. Automatic ends for the preceding syllable follow an edited next nucleus; explicit ends crossing that nucleus reject with a keyed conflict instead of silently clamping user data. A final positive-span check rejects edits leaving no space before the next nucleus. This resolves inter-nucleus ordering for the current default allocation, not full onset/coda/coarticulation constraints.

The existing actual phonemizer/selector/solver regression now moves the second nucleus earlier, verifies the preceding automatic end follows it, rejects an explicit crossing end, and rejects a reversed nucleus with a specific diagnostic. Source markers remain untouched. Short source-transition feasibility and real audio/undo verification still remain for U5. Source is local/uncommitted and full Beta GO remains open.

Ordered-edit verification: affected strict Debug/Release builds passed and all 466 core cases passed in each configuration (81.92/9.87 seconds). `git diff --check` passed. Full U5 and release qualification are not claimed.

### U5 continuation: short-transition feasibility, 2026-09-06

`TimingSolver` no longer silently extends a target end to satisfy the selected source unit's transition length. It preserves the timing-plan end and returns an actionable keyed conflict if the transition plus positive destination space cannot fit. Source sample-rate bounds and unit validation precede marker-to-frame conversion. Existing negative-preutterance reporting remains; full clipping/coarticulation policy is still outstanding.

A regression selects a real CV unit for a short note, requires a too-short conflict with unchanged project state, lengthens the note and verifies the exact tempo-resolved target end, then checks rejection of a zero source sample rate. Onset-specific edits, rendered-audio/undo verification and the remaining U5 constraints still need implementation. Source remains local/uncommitted; Beta GO is not complete.

Short-transition verification: affected strict Debug/Release builds passed and all 467 core cases passed in each configuration (81.80/9.26 seconds). `git diff --check` passed. This does not complete U5 or qualify rendered singing quality.

### U5 continuation: rendered nucleus edit and exact audio undo evidence, 2026-09-06

Tracing both phrase renderers showed that requested starts are realigned to the rendered vowel offset. Therefore changing a solver start alone cannot implement independent consonant/onset timing; that requires an explicit audio-retiming contract and remains open. No onset support is claimed from metadata changes.

Extended the existing WAV-backed raw phrase regression through an actual `EditorSession` timing command, shared pronunciation resolution, deterministic unit selection, timing compilation and raw rendering. A committed +30 ms vowel edit must move the rendered vowel, aligned unit start and composed audio start by exactly 1,440 frames at 48 kHz. Undo must restore the exact audio start and sample vector; redo must reproduce the edited output exactly. This tests the nucleus edit already implemented, not independent onset timing or singer quality. Source remains local/uncommitted; U5 and full Beta GO remain open.

Rendered-edit verification: strict Debug/Release test builds passed; all 467 core cases passed in each configuration with the extended raw-audio/undo assertions (81.63/9.91 seconds). `git diff --check` passed. This is deterministic diagnostic-audio evidence, not perceptual or production-singer qualification.

### U5 continuation: independent onset-start waveform retiming, 2026-09-06

Explicit starts on tokens preceding a unit's nucleus now set the requested placement start and must precede that nucleus. A placement flag instructs both raw and dispatched phrase renderers to retime their rendered waveform around the vowel landmark before alignment. The bounded two-segment linear resampler preserves sample count and endpoints while moving the waveform's vowel landmark to the required offset; resulting alignment honors the explicit onset start without moving the desired vowel anchor. Invalid/non-interior landmark pairs reject without changing the supplied waveform.

The WAV-backed command/audio regression now performs onset edits through both renderer paths, checks fixed vowel anchors, exact requested/aligned starts, changed waveform output and exact audio restoration on undo. A focused resampling test checks endpoint preservation, marker relocation and unchanged output on invalid input. This deterministic resampling baseline can alter timbre/pitch within warped material; it is not a claim of perceptually qualified onset processing. Coarticulation/clipping, additional phoneme-boundary semantics and remaining U5 acceptance work remain open. Source is local/uncommitted; Beta GO remains incomplete.

Onset-retiming verification: strict affected Debug/Release builds passed, followed by all 468 core cases in each configuration (81.73/11.99 seconds). `git diff --check` passed. These tests establish deterministic placement/undo behavior, not perceptual or release qualification.

### U5 continuation: timing/onset cache revision boundary, 2026-09-06

Advanced `SEAM_TIMING_SOLVER_REVISION` from 1 to 2 for sequential nuclei, explicit timing constraints and onset-aware placement, and `SEAM_RAW_RENDERER_REVISION` from 2 to 3 for vowel-landmark onset waveform retiming. `addAlgorithmRevisions` already includes both values in every snapshot identity, including dispatched rendering, so the revised audio rules cannot share the old algorithm identity. The PCM storage format itself is unchanged; no cache files were deleted.

Both regenerated build headers were inspected and contain timing revision 2 / raw revision 3. This is a manual algorithm-version boundary, not an automatic source-attestation system or a claim that future changes need no revision bump. Remaining U5 clipping/coarticulation and timing-boundary work stays open; source is local/uncommitted and full Beta GO is incomplete.

Cache-revision verification: affected strict Debug/Release builds passed; 468 core cases (including PCM cache behavior) and 12 snapshot cases passed in each configuration (Debug CTest 84.04 seconds; Release 11.68 seconds). `git diff --check` passed. No U5 or release completion is inferred.

### U5 continuation: negative-preutterance arithmetic safety, 2026-09-06

Source inspection confirms the existing timeline policy: phrase composition retains negative preutterance; region mixing clips samples before project frame zero rather than shifting surviving audio. Two extreme-position overflow hazards were repaired: composed span length is now computed with unsigned subtraction before its allocation bound, and negative region starts are converted to clipped magnitude without signed negation of the minimum frame value. Ordinary output arithmetic/policy is unchanged, so no audio-algorithm revision bump was made for this invalid/extreme-input repair.

A composer regression verifies normal negative preutterance bounds and rejection of a minimum-frame-to-positive-frame extent without allocating the impossible span. The region clipping conversion was checked by source inspection; this regression alone is not a dedicated end-to-end extreme cached-PCM test. Full project-zero acoustic alignment, coarticulation and remaining timing-boundary work remain U5 obligations. Source is local/uncommitted; Beta GO remains incomplete.

Negative-preutterance verification: strict affected Debug/Release builds passed; all 469 core cases passed in each configuration (182.46/20.12 seconds). Debug took longer than recent runs; the original session completed successfully without restarting. `git diff --check` passed. This does not complete U5 or certify acoustic clipping behavior.

### U5 continuation: end-to-end frame-zero clipping evidence, 2026-09-06

Added a WAV-backed regression through actual snapshot construction, `PhraseRenderPipeline`, `ProductionRegionRenderer` and disk PCM cache. At both 44.1 and 48 kHz, with default and explicit onset timing, the phrase must retain negative preutterance and place its vowel at frame zero. Region output must equal every surviving phrase sample after the exact negative prefix is removed (subject only to the existing mix clamp), with no time shift. Clearing memory and replaying the disk cache must produce identical output; the source project must remain unchanged.

Both strict test-target builds passed and all 13 snapshot cases passed in Debug and Release (0.85/0.39 seconds). No production code changed in this turn, so the unrelated core suite was not rerun or represented as freshly verified. `git diff --check` passed. This closes the specific frame-zero clipping evidence gap for diagnostic audio; broader U5 timing-boundary/coarticulation acceptance and perceptual singer qualification remain separate. Source is local/uncommitted and Beta GO remains incomplete.

### U5 remaining-contract source review, 2026-09-06

`U5_TIMING_ACCEPTANCE_REVIEW_2026-09-06.md` records concrete remaining gaps: intermediate token edits inside a selected unit can be ignored by placement; the editor lane uses independent role-weight geometry and shifts a default end with start-only edits; release/coarticulation policy needs a complete boundary contract; and the planned dedicated timing-test artifact is not yet present. Next work is compatible unit selection/alignment for intermediate edits, then shared display geometry and boundary policy. This source/documentation turn did not change runtime behavior or rerun tests. `git diff --check` passed. U5/Beta GO remain incomplete and source remains local/uncommitted.

### U5 continuation: explicit-boundary-compatible unit selection, 2026-09-06

Added `supportsExplicitPhonemeTiming` to describe the current renderer landmarks: first-token start, first-nucleus start and final-token end. Candidate generation excludes units that would hide other explicit boundaries. Forced incompatible units produce a keyed conflict; `TimingSolver` repeats the capability check for externally supplied/stale plans. No source phone landmarks are invented. Selector algorithm revision advances from 1 to 2 so cache identity tracks changed selection behavior.

A regression starts with a preferred long `かき` unit, edits an interior start or end, and requires two compatible CV units instead. It verifies the corresponding frame placement, renders WAV-backed audio and checks the actual aligned start/unit end, then rejects the stale long plan and a forced incompatible unit. Remaining shared display geometry, release/coarticulation and dedicated timing-matrix work remain open. Source is local/uncommitted; U5/Beta GO are incomplete.

The first full regression run exposed an authoring-runtime test expecting an interior consonant-end edit on a forced CV unit to become Ready. That expectation relied on the formerly ignored boundary. The positive submission/dirty-impact test now edits the supported vowel end; the new forced-unit regression separately requires rejection of unsupported interior edits. Neither the boundary guard nor renderer validation was relaxed.

Interior-boundary verification: strict affected Debug/Release builds passed. After updating the unsupported positive-test expectation, all 470 core cases passed in each configuration (82.68/9.74 seconds). Initial failed runs are not claimed as passing. `git diff --check` passed. Full U5 and release qualification remain open.

### U5 continuation: start-only lane edit preserves the end boundary, 2026-09-06

The phoneme lane now computes its end coordinate independently from its start and derives width afterward. Previously, changing only the start retained the old width and inadvertently moved the displayed end. Explicit end overrides still replace the end coordinate. The existing lane regression now checks preserved default ends and matching end-boundary hit targets at two zoom levels.

This fixes one confirmed UI boundary defect, not the broader shared timing-display contract. Role-weight defaults and invalid-span presentation still need replacement with a target-timing-derived model. Source is local/uncommitted; U5 and Beta GO remain incomplete.

Lane-end verification: strict affected Debug/Release builds passed. The initial test incorrectly required a specific owner at a shared boundary; it now verifies the resolved coordinate while preserving existing tie behavior. All 470 core cases then passed in each configuration (84.35/9.40 seconds). `git diff --check` passed. No complete shared-geometry or U5 acceptance is claimed.

### U5 continuation: dedicated compiler matrix and trailing-coda repair, 2026-09-06

Added the planned `tests/test_phoneme_timing.cpp` artifact and strict `seam_phoneme_timing_tests` CTest target. Five focused cases cover equal elapsed-time nucleus allocation across a tempo change at four sample rates, note-relative offset conversion at 44.1/48 kHz, trailing-coda grouping, malformed ordinals/reversed nuclei/invalid rates, and a note with fewer available frames than nuclei.

The first focused run reproduced a trailing-coda bug: clamping a lower-bound lookup to the last nucleus and then decrementing attached the coda to the penultimate syllable. Coda lookup now directly chooses the preceding nucleus using an upper bound. Timing solver revision advances to 3 to separate the changed output. Existing end-to-end synthesis and snapshot tests remain in place. Shared timing display and release/coarticulation work are still open; U5/Beta GO remain incomplete and source remains local/uncommitted.

Dedicated-timing verification: strict affected Debug/Release builds passed; after the reproduced coda failure was repaired, 5 compiler-contract, 13 snapshot and 470 core cases passed in each configuration (Debug CTest 88.70 seconds; Release 11.28 seconds). `git diff --check` passed. This does not complete the U5 contract or release qualification.

### U5 continuation: separate explicit token starts from nucleus anchors, 2026-09-06

`PhonemeTimingAnchor` now publishes `explicitStartFrame` separately from the syllable's actual `nucleusFrame`. Previously an onset override occupied the field named nucleusFrame, making it unsafe for a shared display model. The compiler retains an absent explicit start when no such target was supplied and publishes the edited shared nucleus for every token in the group. The solver consumes the explicit onset field for onset-aware placement; nucleus-free units use an explicit start when present. Timing revision advances to 4 for the changed anchor semantics.

A dedicated regression combines a negative consonant start with a positive vowel edit and checks their distinct coordinates, shared nucleus identity and absence of an invented explicit start on an unedited onset. This establishes the API distinction needed by the UI; lane integration and release/coarticulation policy are not yet complete. Source is local/uncommitted and full Beta GO remains open.

Separated-anchor verification: strict affected Debug/Release builds passed; 6 dedicated timing and 470 core cases passed in each configuration (Debug CTest 84.51 seconds; Release 9.90 seconds). `git diff --check` passed. Shared UI geometry and full U5 acceptance remain open.

### U5 continuation: shared timing-derived phoneme lane, 2026-09-06

The phoneme lane now compiles the same target timing as synthesis for nucleus spans and explicit boundaries. Unresolved source-dependent onset/geminate/coda extents are fixed-width estimated labels in an upper band, distinguished by `~`; nucleus spans occupy the lower band. Compiler failure or reversed display bounds mark tokens with `!` while retaining editable fallback geometry. `PHONEME_TIMING_DISPLAY.md` explains these semantics and the 48 kHz display reference rate.

UI regression coverage now checks shared `かき` boundaries, updated automatic ends, estimate/conflict flags and two-band separation, alongside the existing zoom/end-hit checks. Native integration testing exposed unit-row hit testing inheriting vertical bands; it now uses horizontal coverage. Software-frame inspection exposed double-added keyboard offsets and label clipping problems; phoneme/unit/seam painting now uses absolute model coordinates and clips compact text inside each band. No renderer audio behavior changed in this UI integration. Release/coarticulation contract work remains open; U5 and Beta GO are incomplete and source is local/uncommitted.

Shared-lane verification: strict affected Debug/Release builds passed; after repairing the unit-row regression and final paint bounds, all 470 core cases passed in each configuration (82.55/9.58 seconds). A 960x720 software frame was inspected iteratively; final bars align with notes and compact labels remain inside their bands. This is software-frame evidence, not installed-host visual certification. `git diff --check` passed. Full U5/release qualification remains open.

### U5 continuation: region-owned release boundaries, 2026-09-06

The timing compiler validates owning region bounds and note containment, permits an explicit release beyond its note within that region, and rejects a release past the region with an actionable conflict. It does not clamp saved offsets. Distinct overlapping notes retain independent anchors rather than inheriting within-note ordering constraints. Timing revision advances to 5 for the new policy; the authoring guide now describes the release boundary and unchanged frame-zero clipping behavior.

New compiler tests check valid extended releases, unchanged overrun input, note containment and overlapping-note independence. A WAV-backed snapshot/pipeline regression verifies the actual audio tail ends at the requested extended release. Strict affected Debug/Release builds passed, followed by 30 synthesis, 8 timing-contract and 14 snapshot cases in each configuration (52 cases; CTest 4.66/1.99 seconds). The broad core suite was not rerun or claimed as freshly passing. `git diff --check` passed. Coarticulation interactions and final U5 acceptance remain open; source is local/uncommitted and Beta GO remains incomplete.

### U5 continuation: post-vowel transition feasibility independent of onset edits, 2026-09-06

The short-transition check now measures the required stable-region transition from the vowel anchor rather than from the requested consonant start. Previously, a very early explicit onset could make an impossible short vowel span pass, while a late/compressed onset could make an otherwise valid post-vowel interval fail. Default placement remains algebraically equivalent; explicit onset feasibility is corrected. Timing revision advances to 6.

The short-note regression now rejects an early onset that cannot rescue the short post-vowel span and accepts a shortened onset when sufficient post-vowel time remains. Strict affected Debug/Release builds passed, followed by 30 synthesis, 8 timing-contract and 14 snapshot cases per configuration (52 cases; CTest 4.24/2.03 seconds). `git diff --check` passed. The broad core suite was not rerun or claimed as fresh evidence. Final U5/coarticulation acceptance remains open; source is local/uncommitted and Beta GO is incomplete.

### U5 continuation: checked source-marker frame conversion, 2026-09-06

Source marker conversion now uses checked integer quotient/remainder rescaling with nearest-frame rounding rather than calling `llround` on a potentially out-of-range floating-point product. Preutterance subtraction and timed-unit extent are checked before downstream signed subtraction/allocation, using the existing 100-million-frame composition bound. Timing revision advances to 7 for explicit rounding/range semantics.

A regression verifies exact 48 kHz-to-44.1 kHz marker rounding and rejects structurally valid but enormous marker metadata at an upsampled rate before unsafe conversion. Strict affected Debug/Release builds passed; 31 synthesis, 8 timing-contract and 14 snapshot cases passed in each configuration (53 cases; CTest 4.53/2.16 seconds). `git diff --check` passed. The broad core suite was not rerun or claimed as freshly verified. Final U5/Beta GO acceptance remains open and source is local/uncommitted.

### U5 continuation: explicit syllable membership and boundary provenance, 2026-09-06

The compiled timing contract now includes note-local syllable indices, optional real-nucleus keys and explicit-end provenance, alongside the existing optional explicit start. Nucleus-free material keeps a fallback timing anchor without inventing a vowel identity. This fills a data-contract requirement from the virtual-singer roadmap without changing placement or audio calculations, so no algorithm revision was advanced.

A dedicated test checks membership across two syllables, start/end provenance and a breath-only group without a real nucleus. Strict Debug/Release timing/synthesis/snapshot targets and the editor timing consumer compiled successfully. All 31 synthesis, 9 timing-contract and 14 snapshot cases passed per configuration (54 cases; CTest 4.25/1.99 seconds). `git diff --check` passed. The broad core/native runtime suite was not rerun or claimed as freshly passing. Final U5 and full Beta GO acceptance remain open; source is local/uncommitted.

### U5 continuation: bounded seam-window composition evidence, 2026-09-06

Added a deterministic composer regression using constant outgoing/incoming samples. It checks exact handoff positions for 64- and 256-frame windows, immediate incoming replacement for a zero window, negative-window rejection, preserved overall start/length, and unchanged incoming placement/landmark/source samples. The guide now states that the setting bounds crossfading, not incoming-unit duration, and distinguishes phase-basis processing from target timing.

No production behavior changed. Strict Debug/Release synthesis-test builds passed and all 32 synthesis cases passed in each configuration (3.46/1.00 seconds). `git diff --check` passed. Other suites were not rerun or claimed as fresh evidence. This closes the specific overlap-window behavior check; final U5 acceptance still requires a consolidated review. Source is local/uncommitted and full Beta GO remains incomplete.

### U5 consolidated review: remaining multi-nucleus unit mapping, 2026-09-06

Rechecked the approved U5 criteria against current selection, timing placement, source markers and regression coverage. Explicit interior edits now force compatible units, but unedited multi-nucleus units can still be selected while `TimingSolver` consumes only their first nucleus anchor. The existing long-unit selection test and one-landmark placement/source contracts directly establish the gap. The separate-CV and first-nucleus audio tests do not prove later anchors within a single unit.

`U5_TIMING_ACCEPTANCE_REVIEW_2026-09-06.md` now records the remaining completion path: carry all target anchors, use verified per-phone/nucleus source landmarks, map them through rendering, and verify nonfirst-anchor audio/undo/cache behavior. Smaller-unit fallback or explicit capability rejection is acceptable for unaligned resources but cannot replace general multi-nucleus support in the full product. This was a source/documentation review, not a new runtime implementation or test run. `git diff --check` passed. U5/Beta GO remain incomplete; source is local/uncommitted.

### U5 continuation: retain all phoneme targets in unit placement, 2026-09-06

`TimedUnitPlacement` now carries the complete ordered compiler records for its covered tokens, preserving later nucleus positions, syllable membership and explicit-boundary provenance instead of discarding them at placement. The solver requires ordered, exactly-once token coverage and bounds entry count, preventing malformed duplicate plans from amplifying target storage. Existing valid audio behavior is unchanged; renderers have not yet consumed the additional landmarks.

The long-unit regression checks all four `かき` target keys, both distinct nucleus identities/positions and the shared boundary, and rejects duplicated coverage. Strict Debug/Release targeted builds passed; 32 synthesis, 9 timing-contract and 14 snapshot cases passed per configuration (55 cases; CTest 4.54/2.00 seconds). `git diff --check` passed. The broad core suite was not rerun. Verified source-landmark support and waveform mapping are the next required work; U5/Beta GO remain incomplete and source is local/uncommitted.

### U5 continuation: audio-bound source phoneme alignment contract, 2026-09-06

Added `SourcePhonemeAlignment` and `SourcePhoneLandmark` as a bounded authored-source contract. Validation requires a matching unit ID, lowercase SHA-256 equal to the caller's verified encoded-audio hash, complete phone-string coverage (maximum 256 landmarks), strict frame ordering and containment within the unit and decoded audio. Empty/partial mappings, mismatched audio/phones, duplicate/reversed frames and out-of-bounds positions reject. No uniform source-position inference or claim of acoustic/phonetic review is made.

Strict Debug/Release timing-target builds passed and all 10 timing-contract cases passed per configuration (0.41/0.47 seconds), including valid four-phone alignment and seven malformed variants plus audio/hash bounds. `git diff --check` passed. This is a new validation component; source-alignment persistence and waveform consumption are not yet implemented. Other suites were not rerun. Source remains local/uncommitted; U5/Beta GO are incomplete.

### U5 continuation: bounded source-alignment JSON codec, 2026-09-06

Added version-1 source-alignment encode/decode functions with exact root/landmark shapes, integer-only frame positions, bounded parsing and domain revalidation against the caller's actual unit, verified audio hash and decoded frame count. Encoding validates first and checks final size. The separate format is documented in `docs/formats/SOURCE_PHONEME_ALIGNMENT_V1.md`; existing manifest schema and installed banks are unchanged. A compile-time initializer brace error was corrected without relaxing checks.

Strict Debug/Release timing-target builds passed; all 11 contract cases passed per configuration (0.45/0.35 seconds), including round-trip equality, stale audio, future schema, fractional frame, unknown field, empty coverage and oversized input rejection. `git diff --check` passed. This delivers serialization, not file/package registration or renderer integration; those remain required. Other suites were not rerun. Source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: multi-anchor source-to-target map construction, 2026-09-06

Added `SourceTargetMap` construction from validated audio-bound source alignment and complete unit target records. It includes unit endpoints, real nucleus anchors, explicit starts and explicit ends; an interior end maps to the next authored phone boundary, while the final end maps to the exclusive source audio end. Identical knots coalesce; contradictory same-source targets, crossed times, repeated target keys, coverage mismatch and out-of-bounds target extents reject. No source landmarks are fabricated.

Strict Debug/Release timing-target builds passed and all 12 contract cases passed per configuration (0.53/0.34 seconds). The new case verifies both nuclei in a four-phone unit and rejection of crossed nuclei/conflicting adjacent explicit boundaries. `git diff --check` passed. This is map construction only: waveform application, package loading and cache identity integration remain required. Other suites were not rerun; source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: bounded multi-anchor waveform mapping operator, 2026-09-06

Added `applySourceTargetMap` for mono sample spans. It validates ordered source/target knots against source bounds, caps source and output at 32 Mi frames and maps at 770 knots, checks finite input and cancellation, and uses piecewise linear interpolation with exclusive-end handling. It returns the mapped absolute start and samples without mutating source data. This is deterministic local-rate resampling, not pitch-preserving DSP or perceptually qualified singing synthesis.

A regression places two distinct waveform landmarks at exact output frames, moves the later target independently, and rejects cancellation, crossed targets, out-of-bounds source ends, oversized output and NaN input. Strict Debug/Release timing-target builds passed; all 13 contract cases passed per configuration (0.66/0.40 seconds). `git diff --check` passed. The operator is not yet connected to source-resource discovery, render dispatch or cache identity. Other suites were not rerun; source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: callable aligned raw-unit renderer, 2026-09-06

Added `renderAlignedRawUnit` to combine source/audio-bound alignment validation, multi-anchor map construction, waveform mapping and unit gain in one cancellable call. It rejects requested root-pitch transposition rather than ignoring `targetMidi`, validates gain output for finiteness, and retains the documented limitation that local-rate mapping itself is not pitch-preserving. This is an explicit raw path, not automatic dispatcher fallback or complete F0 synthesis.

The source-map regression now calls the combined renderer with diagnostic samples at two authored nucleus landmarks, moves the second target by 1,440 frames while preserving the first, checks gain, and rejects stale audio identity and unsupported transposition. Strict Debug/Release timing builds passed and all 13 contract cases passed per configuration (0.42/0.33 seconds). `git diff --check` passed. Resource discovery, normal phrase-dispatch wiring and cache identity remain open; other suites were not rerun. Source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: alignment discovery, frozen resources and render identity, 2026-09-06

Snapshots now discover optional selected-unit sidecars at `alignments/<sha256(unit-id-bytes)>.json`, reject unsafe/non-file or invalid present resources, validate against frozen encoded-audio hashes and decoded bounds, and freeze each unique unit's alignment once. Sidecars are bounded to 512 KiB each and 4 MiB aggregate. Frozen unit audio retains both the alignment and verified audio digest. Exact alignment bytes are included in selected-unit identity, and the render identity domain advances to v4. No cache files or existing bank manifests were rewritten.

A WAV-backed snapshot regression checks legacy absence, valid freezing, sidecar-sensitive cache keys, immutability after file edits, stale-audio rejection and symlink rejection. Strict Debug/Release targeted builds passed; 32 synthesis, 13 timing-contract and 15 snapshot cases passed per configuration (60 cases; CTest 4.86/2.55 seconds). `git diff --check` passed. Normal phrase dispatch and bank-level content identity/package promotion remain open; the broad core suite was not rerun. Source is local/uncommitted and U5/Beta GO remain incomplete.

### U5 continuation: frozen aligned phrase rendering, 2026-09-06

The normal concatenative frozen phrase path now consumes authored source alignment and maps all retained nucleus targets through the bounded raw operator. It avoids applying legacy onset retiming a second time. Renderer selection shares the dispatcher's explicit-unit/global-policy/bank-hint precedence; requests for non-raw backends reject rather than silently substitute raw alignment. Root-pitch, pitch-automation, raw-override and source-buffer constraints remain explicit. Raw renderer revision advances to 4.

A WAV-backed regression verifies both nucleus samples at exact requested frames after deleting the test-created sidecar, explicit Raw precedence over a global PSOLA policy, explicit PSOLA rejection under ForceRaw, and unsupported loop-print/additional-gain rejection. Strict affected Debug/Release builds passed; 32 synthesis, 13 timing-contract and 16 snapshot cases passed per configuration (61 cases; CTest 4.13/2.14 seconds). `git diff --check` passed. Broad core/native/installed-host suites were not rerun. U3/U4 retain their recorded local acceptance; U5 remains incomplete pending alignment-aware interior-edit selection, edit/undo/cache and cross-note acceptance, and package identity integration. The raw warp is not pitch-preserving. Source is local/uncommitted; full Beta GO remains incomplete.

### U5 continuation: evidence-aware interior timing capability, 2026-09-06

Added borrowed source-alignment evidence to candidate generation, forced-unit validation and timing solving. It checks actual unit/phone coverage and validates the authored landmarks against the supplied verified WAV digest and decoded frame count, instead of trusting a unit-ID capability flag. Legacy callers retain the existing smaller-unit/conflict behavior. Frozen phrase rendering supplies its owned evidence to the solver. Selector revision is now 3; timing revision is 8.

The WAV-backed selection/pipeline regression edits the second vowel to 280 ms, retains the same long unit, checks its actual sample at a 1,440-frame displacement while preserving the first target, and rejects stale selection evidence and missing frozen alignment. Strict affected Debug/Release builds passed; all 61 focused synthesis/timing/snapshot cases passed per configuration (4.96/1.85 seconds). `git diff --check` passed. No broad core/native/host rerun is claimed. Snapshot creation still discovers sidecars after selection: moving bounded resource discovery ahead of selection is the next integration step, followed by actual command/undo/cache and cross-note coverage. U5 and full Beta GO remain incomplete; source remains local/uncommitted.

### U5 continuation: snapshot discovery before selection and edit/cache workflow, 2026-09-06

Refactored snapshot resource freezing into one reusable, bounded loader. Before selection it probes enabled, matching-style, phone-matching candidates requiring interior timing capability. Present sidecars cause audio-bound validation/freezing and evidence publication; absent sidecars do not load candidate WAVs. Final selection reuses those frozen resources and emits only selected resources into the snapshot. Candidate and final resource loads share aggregate budgets; malformed present candidate resources fail explicitly.

Expanded the WAV-backed regression through actual phoneme command apply/revert/reapply, normal snapshot creation and production region rendering. It checks the same long unit, a 30 ms second-vowel displacement, changed edited audio/cache identity, restored original identity/audio on undo, restored edited identity/audio on redo, and byte-equivalent disk-cache audio after memory eviction. Frozen snapshots also still render after the test-created sidecar is removed. Strict affected Debug/Release builds passed and all 61 focused cases passed per configuration (3.58/1.19 seconds). `git diff --check` passed. No broad core/native/host rerun is claimed. Cross-note coverage, unaligned multi-nucleus policy, package identity and consolidated U5 acceptance remain open; source is local/uncommitted and full Beta GO remains incomplete.

### U5 continuation: cross-note alignment and pitch-change integrity, 2026-09-06

Extended the same waveform/command/cache workflow over two adjacent same-pitch notes. The second phoneme uses its own note-relative 30 ms offset; exact target samples, long-unit selection, frozen-resource rendering, undo/redo and disk-cache replay are verified in both the one-note and two-note variants.

The changed-pitch regression initially failed because aligned raw rendering silently ignored the second note's pitch. The renderer now validates every covered note against the placement pitch and explicitly rejects unsupported within-unit changes. Raw renderer revision advances to 5. This prevents incorrect success; pitch-preserving multi-note melody synthesis remains required downstream and is not replaced by rejection.

After fixing a test fixture field-name typo and then the actual reproduced pitch defect, strict affected Debug/Release builds passed. All 61 focused synthesis/timing/snapshot cases passed per configuration (4.50/1.90 seconds); the expanded snapshot case exercises both variants. `git diff --check` passed. Initial failures are not claimed as passing; no broad core/native/host rerun is claimed. Unaligned multi-nucleus policy, package identity and final U5 acceptance remain open. Source stays local/uncommitted and full Beta GO remains incomplete.

### U5 continuation: bank/package alignment identity, 2026-09-06

Bank content identity now includes an optional sorted per-unit alignment digest section. Banks without matching sidecars retain their legacy hash. Files are bounded to 512 KiB each and 64 MiB per bank; symlink/non-file paths reject. This hashes exact bytes rather than claiming semantic alignment validation. Updated the installer service's separate signed-entry identity calculation to match disk/catalog identity and limits.

New regressions verify absence/empty-directory compatibility, byte-sensitive hashes, oversized and symlink rejection, valid alignment pack/install preservation, source/installed identity agreement, idempotent reinstall, and loss of receipt-backed trust after installed sidecar modification. Strict affected Debug/Release builds passed. All 76 cases across synthesis (33), timing (13), snapshots (16) and installer/U3 (14) passed per configuration (3.70/1.18 seconds). `git diff --check` passed. Broad core/native/host qualification was not rerun. Unaligned multi-nucleus behavior and consolidated U5 acceptance remain open; source remains local/uncommitted and full Beta GO is incomplete.

### U5 continuation: unaligned multi-nucleus selection policy, 2026-09-06

Normal snapshot selection now opts into complete nucleus-alignment capability checks. Matching multi-nucleus candidates participate in preselection sidecar discovery even without timing overrides. Missing landmarks exclude automatic candidates, allowing compatible smaller-unit selection; forced unaligned units report an actionable conflict. The frozen pipeline independently rejects an unaligned multi-nucleus legacy plan. Resource-free selector APIs preserve planning behavior by default, not a rendering guarantee. Selector revision advances to 4.

New regression covers successful smaller-unit rendering, forced long-unit conflict, absent alternatives and rejection of a manually supplied legacy plan. An existing complete forced-relationship fixture initially failed under the new policy; it now supplies validated source landmarks while preserving all projection/boundary assertions. Strict affected Debug/Release builds passed; 33 synthesis, 13 timing and 17 snapshot cases passed per configuration (63 total; 3.59/1.33 seconds). `git diff --check` passed. Initial failures are not claimed as passing. Consolidated U5 acceptance and broader core/native regression checks are next; source remains local/uncommitted and full Beta GO is incomplete.

### U5 acceptance audit and dependent-boundary repair, 2026-09-06

The consolidated audit reproduced a false timing conflict when both nuclei move beyond their original equal-time boundary. The compiler now resolves an automatic end from the edited next nucleus before checking the span; explicitly reversed ends and crossed nuclei still reject. Timing revision advances to 9. A dedicated failing-then-passing regression and normal snapshot/command waveform assertions cover 300/400 ms anchors and restoration.

Strict affected Debug/Release builds passed. Final four-suite runs passed in both configurations: 474 core/native cases, 33 synthesis cases, 14 dedicated timing cases and 17 snapshot cases (overlapping suites, not a unique-test total). Debug elapsed 88.96 seconds; Release 11.98 seconds. `git diff --check` passed. `U5_ACCEPTANCE_AUDIT_2026-09-06.md` maps every explicit U5 criterion to source and runtime evidence and records local implementation acceptance. No fresh installed-host/visual/listening or full release-matrix claim is made. Source remains local/uncommitted. U6 complete F0/expression compilation is next; all remaining full Beta GO units stay mandatory.

### U6 started: bounded unit-independent score evaluator, 2026-09-06

Added the planned performance compiler files and dedicated test target. The first component freezes bounded absolute note spans, tempo, manual pitch offsets and dynamics, and evaluates score pitch plus note vibrato statelessly at absolute frames. It has no bank/unit input and returns both C4/G4 score plateaus. Score frequency is explicitly not a phonetic voicing decision. Accepted-take evaluation and overlapping voice allocation currently reject pending their full implementation; articulation is retained but not yet applied as a gate.

Strict Debug/Release dedicated builds passed; all three compiler cases passed per configuration (0.84/0.46 seconds), including tempo-dependent boundaries, immutable score capture, neutral/edited dynamics, analytic vibrato/offset and reverse-block phase invariance. `git diff --check` passed. Renderer consumers/audio qualification and the rest of U6 remain open; other suites were not rerun. `U6_PERFORMANCE_COMPILER_STATUS_2026-09-06.md` records the remaining exact scope. Source is local/uncommitted and the complete Beta GO objective stays active.

### U6 continuation: accepted pitch/dynamics and manual ownership, 2026-09-06

The score evaluator now consumes validated selected pitch/dynamics lanes through note/range scopes and source offsets. Proposed-only takes remain inactive. Generated MIDI-cents pitch is the selected base; only explicit additive ownership applies a manual offset to it. Replace ownership and enabled manual vibrato use the score/manual base instead, preventing generated oscillation from being added to manual vibrato. Accepted null pitch remains absent frequency with retained note identity. Dynamics obeys channel replacement ownership. Numeric interpolation and discrete null transitions are documented in the U6 status file.

After correcting a test initializer syntax error, strict Debug/Release compiler builds passed and all four cases passed per configuration (0.78/0.41 seconds), covering note/range ownership, source offsets, null values, proposals, pitch replacement/offset, dynamics and vibrato suppression. `git diff --check` passed. Other suites were not rerun; no renderer-level acoustic result or U6 completion is claimed. Remaining channels, lookup efficiency, phonetic voicing, articulation and renderer consumption remain required. Source remains local/uncommitted and the full Beta GO goal stays active.

### U6 continuation: exact ownership/selection sample boundaries, 2026-09-06

Reproduced a real early handoff: inverse-tempo tick rounding released manual pitch ownership one output frame before the declared end. The compiler now freezes note/range ownership and accepted-selection scopes as half-open absolute-frame intervals. Sample evaluation uses those intervals for Replace/PitchOffset and accepted selection eligibility, retaining note IDs for note scopes; lane interpolation remains tick-domain.

Strict Debug/Release builds passed. All four expanded compiler cases passed per configuration (0.49/0.78 seconds), including before/at start and before/at end at 44.1/48/192 kHz with a tempo change, plus accepted-selection range boundaries. The pre-fix regression failed as expected and is not claimed as passing. `git diff --check` passed. Other suites were not rerun. This repairs the evaluator contract; U6 renderer/voicing/articulation integration and the full Beta GO goal remain incomplete. Changes remain local/uncommitted.

### U6 continuation: compiled melody drives PSOLA sustain pitch, 2026-09-06

Classic PSOLA now consumes immutable compiled performance at an explicit absolute output origin and selects sustain pulse pitch from the complete score evaluator. Duplicate pitch curves, mismatched rates and overflowing origins reject; accepted-unvoiced pitch explicitly requires a different processing path. PSOLA revision advances to 3. Snapshot creation rejects caller-injected compiled performance until snapshot-owned freezing/identity is integrated, with a cache-safety regression.

A real PCM/pitch-analysis regression verifies C4 and G4 sustained plateaus inside one rendered unit with no manual pitch curve. Its first run exposed the generic fixture's 19,200-frame raw release beginning before the second note. The fixture now declares sustained source material spanning both notes; raw attack/release retargeting remains an explicit implementation gap, not a passing claim. Strict affected Debug/Release builds passed; all 56 focused cases passed per configuration (33 synthesis, 5 compiler, 18 snapshot; 4.20/1.30 seconds). `git diff --check` passed. Normal phrase integration, remaining renderer families, phonetic voicing, articulation and full U6 acceptance remain incomplete. Source is local/uncommitted and the full Beta GO goal remains active.

### U6 continuation: snapshot-owned performance reaches normal PSOLA rendering, 2026-09-06

Effective Classic PSOLA selections now cause snapshot-owned score/performance compilation. The frozen pipeline forwards that immutable output internally; phrase dispatch sets each unit's absolute origin and avoids adding manual pitch a second time. Snapshot identity v5 includes compiler revision 1 alongside existing frozen score/options/resources. Caller-injected compiler objects remain prohibited. Compiled PSOLA errors cannot degrade to raw fallback, and explicit onset warping rejects pending pitch-preserving mapping.

The normal snapshot/phrase audio regression measures 440 Hz and a single +100-cent shift, verifies frozen behavior after live-curve removal and identity restoration, and requires runtime rejection of missing sustain-mark capability. Its initial zero-mark fixture failed manifest validation; replacing it with valid out-of-sustain marks reaches the intended backend failure without weakening validation. Strict affected Debug/Release builds passed; all 57 focused cases passed per configuration (4.27/1.42 seconds). `git diff --check` passed. Aligned multi-nucleus PSOLA, voiced attack/release processing, other render families, voicing/articulation/dynamics consumption and full U6 remain open. Source is local/uncommitted; full Beta GO remains active.

### U6 continuation: compiled dynamics affect PSOLA PCM, 2026-09-06

PSOLA now consumes compiled dynamics after DC correction/fades, avoiding a later processing stage distorting the gain envelope. Unity is transparent; gain application is cancellable and rejects non-finite/overflow output. PSOLA revision advances to 4. The evaluator's current neutral behavior outside active score notes is retained explicitly; preutterance/release expression is not claimed complete.

Extended direct audio tests verify exact unity PCM and every sample of a 0.25→0.75 ramp. Normal snapshot/phrase tests verify half-gain PCM and identity restoration after removing the curve. Strict affected Debug/Release builds passed; all 57 focused cases passed per configuration (5.19/2.48 seconds). `git diff --check` passed. Other backends, articulation/voicing and full U6 remain open; no broad core/native/host rerun is claimed. Source remains local/uncommitted and the full Beta GO goal remains active.

### U6 continuation: audible staccato gate and bounded release, 2026-09-06

Compiler revision 2 adds staccato gate/release frames using a documented baseline of half elapsed note duration and a release of up to 10 ms within the gate. PSOLA revision 5 consumes the articulation envelope along with dynamics. Stored note duration remains unchanged; a closed gate remains closed through a gap/extended source tail and reopens for the next active note. Legato/slur/reattack and phonetic voicing remain separate unfinished work.

Direct PCM verification checks every staccato sample against the normal waveform times the compiled envelope, exact release midpoint, and the shortened gate. Dedicated tests cover tempo changes, 8/44.1/48 kHz and gap/next-note behavior. Strict affected Debug/Release builds passed; all 58 focused cases passed per configuration (4.81/2.45 seconds). `git diff --check` passed. This is a deterministic articulation baseline, not listening qualification or full U6 acceptance. Source stays local/uncommitted and the full Beta GO goal remains active.

### U6 continuation: pronunciation-aware explicit vowel continuation, 2026-09-06

Compiler revision 3 consumes the supplied shared phoneme sequence to identify adjacent compatible explicit continuation notes, retaining reattack for repeated lyrics and breaking links across gaps/staccato. Linked score notes receive a bounded smoothstep pitch transition (up to 20 ms/half note duration). Normal snapshot compilation supplies its resolved tokens; the compiler does not independently phonemize.

New checks distinguish repeated Legato-labeled syllables from actual continuation, assert pitch-transition endpoints/interior and gap/staccato behavior, and compare PSOLA boundary PCM with/without the transition. Strict affected Debug/Release builds passed; all 59 focused cases passed per configuration (5.33/2.55 seconds). `git diff --check` passed. Separate-unit reattack suppression, shared-lyric/slur editor repair, full voicing and other renderers remain incomplete; no full legato or U6 acceptance is claimed. Source remains local/uncommitted and full Beta GO stays active.

### U6 continuation: editor melisma produces vowel continuation, 2026-09-06

Added the shared domain rule for adjacent Legato notes with a common lyric owner. Japanese phonemization and performance compilation now agree that this is vowel continuation, not repeated consonants. The immediate predecessor must supply the vowel; an unrelated older vowel is not borrowed for shared-lyric continuation. Slur disable clears Legato instead of setting it. Duration/articulation now participate in pronunciation input identity and command reconciliation; the new domain helper sources enter resource identity. Phonemizer/compiler revisions are 2/4.

The editor regression proves `k a / k a` becomes `k a / a`, compiles a non-reattack continuation, and preserves exact project undo/redo and slur-disable pronunciation revisions. Strict affected Debug/Release builds passed; all 67 focused cases across synthesis, melisma/editor workflow, compiler, snapshot and edit preservation passed per configuration (6.95/4.09 seconds). `git diff --check` passed. No broad core/native/installed-host rerun or complete acoustic legato claim is made. Separate-unit attack suppression, full voicing and remaining U6/Beta GO work stay open; source remains local/uncommitted.

### U6 continuation: broad regression repair and continuation context protection, 2026-09-06

Rebuilt the broad core/native target in both configurations. The initial Release run found six failures: an old expectation that duration changes cannot affect pronunciation, and five runtime fixtures forcing PSOLA on `demo.ja.g4.o.01` without pitch marks. Updated the duration assertion to match the actual resolver inputs. Success fixtures now request capable PSOLA units, while a new negative runtime test requires unsupported PSOLA to fail without successful publication. Production fallback safeguards remain intact. Test waits now report terminal diagnostics immediately; the coordinator's non-null idle result object is tested according to its actual contract.

Source review also found shared-lyric continuation could be split at maximum phrase duration. The segmenter now preserves those adjacency relationships as atomic groups or reports a bounded over-limit conflict; snapshot intake rejects manual cuts through them. Context-complete splitting remains U7 work, not waived by this protection.

Final strict affected Debug/Release builds passed. All three regression suites passed per configuration: core/native 476 cases, authoring runtime 10 cases, snapshots 20 cases (overlapping targets). Debug elapsed 85.89 seconds; Release 9.89 seconds. `git diff --check` passed. Initial failures are not passing evidence. U6 aligned PSOLA mapping, phonetic voicing, other renderer integrations and complete acceptance remain open. No installed-host/listening qualification or full Beta GO is claimed; source remains local/uncommitted.

### U6 continuation: aligned long-unit PSOLA through normal snapshots, 2026-09-06

Added validated forward/inverse source-map evaluation and wired authored maps into PSOLA. Inverse positions choose nearest source sustain marks while compiled score/performance sets output pulse spacing, separating source duration mapping from target pitch. Mapped boundaries govern stable/release placement and transient fallback sampling. The frozen phrase path now supports aligned PSOLA and binds output origin/nucleus offsets internally; caller-supplied source maps cannot bypass snapshot identity. PSOLA revision advances to 6.

A normal snapshot selects and renders one aligned two-vowel unit across C4→G4, with both sustained plateaus measured from PCM and no raw fallback/manual curve. Source-map tests verify knot/inverse behavior and invalid input rejection. Strict affected Debug/Release builds passed; all 75 focused cases passed per configuration (5.90/2.95 seconds). `git diff --check` passed. Nearest-mark mapping is not single-sample phonetic-event qualification; voiced attacks/releases, unvoiced processing, separate-unit attack suppression and remaining renderer integrations remain required. U6/full Beta GO remain incomplete; source is local/uncommitted.

### U6 continuation: spectral performance integration and off-bin pitch correction, 2026-09-06

Spectral snapshots now own compiled performance; spectral frame processing consumes full-score pitch and the shared gain stage applies dynamics/articulation. Duplicate manual pitch, external injection and fallback that would discard compiled intent are rejected. PSOLA shares the gain helper. Renderer revisions are PSOLA 7 and spectral 3.

An audio regression found the old bin-center phase advance rendered about 422 Hz for a 440 Hz request. The compiled path now tracks measured inter-frame source frequency and uses it for pitch-scaled output phase; phaseReset contributes at initialization rather than continually detuning the compiled trajectory. The original pitch tolerance was retained. After correcting a shadowed local in the shared-helper refactor, strict affected Debug/Release builds passed. All 61 focused cases passed per configuration (5.52/1.56 seconds), including spectral C4/G4 sustain plateaus and normal snapshot pitch/half-gain checks across PSOLA and spectral. `git diff --check` passed. Aligned spectral mapping, voicing/transients, remaining renderer families and full U6 acceptance remain open; source stays local/uncommitted and full Beta GO remains active.

### U6 continuation: aligned spectral rendering through normal snapshots, 2026-09-06

Spectral revision 4 now uses authored maps for source analysis centers, stable/release placement, transient fallback and uncovered samples. Frequency tracking accounts for actual mapped source advance, keeping pitch independent of nonuniform duration mapping. Unit/source/output bounds validate before map use; external source-map injection remains rejected. The frozen phrase path dispatches aligned spectral or PSOLA explicitly without raw substitution.

The aligned long-unit normal-snapshot regression now covers both classical backends, measured sustained C4/G4 pitches, a 30 ms second-vowel edit that changes target frames/PCM/identity, and identity restoration on undo. Strict affected Debug/Release builds passed; all 61 focused cases passed per configuration (6.02/1.72 seconds). `git diff --check` passed. Voicing/transients, exact phonetic-event qualification, attack suppression and remaining renderer families stay open. U6/full Beta GO remain incomplete; source stays local/uncommitted.

### U6 continuation: sample-exact accepted voicing transitions, 2026-09-06

Reproduced accepted pitch becoming null one frame early because lane segments were selected through inverse tick rounding. Compiler revision 5 now chooses segments at forward-mapped output-frame boundaries, preserving discrete voiced/null transitions and source offsets. Numeric lane interpolation remains clamped tick-domain interpolation. Validated source windows bound destination-time arithmetic.

Expanded regression checks both transition directions before/at the boundary at 44.1/48/192 kHz with a tempo change and nonzero source offsets. Strict affected Debug/Release builds passed; all 28 compiler/snapshot cases passed per configuration (4.13/1.85 seconds). `git diff --check` passed. The pre-fix failure is not passing evidence. Phonetic unvoiced rendering and the remaining U6/full Beta GO requirements stay open; other suites were not rerun and source remains local/uncommitted.

### U6 continuation: preserve authored unvoiced material in classical rendering, 2026-09-06

Timing targets now retain optional resolved phoneme voicing, and source maps derive contiguous phone spans from authored landmarks. PSOLA masks known-unvoiced source/target grain positions; spectral masks known-unvoiced analysis/output contributions. Mapped source material remains in those intervals. Accepted null pitch can use a known-unvoiced source path, while unsupported voiced-to-unvoiced conversion remains explicit. Revisions are timing 10, PSOLA 8 and spectral 5; no source JSON schema or measured acoustic labels were invented.

Tests verify onset/nucleus map labels, bounded contiguous coverage and source-relative sample preservation in a mixed diagnostic fixture through both classical backends, contrasted with periodic treatment. Strict affected Debug/Release builds passed; all 76 focused cases passed per configuration (7.45/3.03 seconds). `git diff --check` passed. Acoustic voicing analysis, natural transition quality, attack suppression and remaining renderer work stay open; U6/full Beta GO are incomplete and source remains local/uncommitted.

### U6 continuation: separate vowel units consume continuation attack intent, 2026-09-06

For single-vowel units without authored maps, PSOLA/spectral now consult compiled reattack intent at the vowel landmark. Continuation begins from sustain processing rather than replaying the recorded attack, while preserving output length and vowel placement. Repeated pronunciation retains the attack. Renderer revisions are PSOLA 9 and spectral 6; authored-map/multi-phone continuation remains open rather than being claimed covered.

A diagnostic impulse regression verifies preserved versus suppressed recorded attack in both backends and unchanged output extent/landmark. Strict affected Debug/Release builds passed; all 63 focused cases passed per configuration (7.65/3.01 seconds). `git diff --check` passed. This is not phase-continuity/listening qualification, a broad core/native rerun, or full U6 acceptance. Remaining continuation variants and renderer work stay required; source remains local/uncommitted and full Beta GO remains active.

### U6 continuation: authored-map PSOLA continuation attack suppression, 2026-09-06

Extended PSOLA continuation attack suppression to single-vowel authored maps without rewriting source landmarks or changing output extent/vowel placement. PSOLA revision is 10. The direct impulse matrix includes mapped PSOLA; a normal lyric-command→snapshot→phrase test verifies changed continuation PCM, unchanged vowel onset and restored identity on undo.

Strict affected Debug/Release builds passed; all 31 compiler/snapshot cases passed per configuration (4.66/2.18 seconds). `git diff --check` passed. Spectral mapped continuation, multi-phone transitions, perceptual continuity and full U6 remain unfinished. Other suites were not rerun; source remains local/uncommitted and full Beta GO stays active.

### U6 continuation: mapped spectral continuation with held-source pitch tracking, 2026-09-06

Spectral revision 7 now handles mapped single-vowel continuation without replaying its recorded attack. It holds the sustained source entry, estimates its local frequency with one adjacent FFT window, and retains that estimate while source motion is zero. This avoids detuning to FFT-bin centers during the hold. The authored map and target extent remain intact.

Direct tests cover attack preservation/suppression, unchanged landmarks, and held-interval pitch for mapped/unmapped classical paths. Normal lyric-command/snapshot/phrase checks now exercise PSOLA and spectral identity restoration. Strict affected Debug/Release builds passed; all 64 focused cases passed per configuration (10.05/5.05 seconds). `git diff --check` passed. General transition quality, measured voicing and remaining renderer-family/U6 requirements remain open; source is local/uncommitted and full Beta GO stays active.

### U6 continuation: compiled granular stretch performance and grain phase repair, 2026-09-06

Stretch revision 3 now consumes snapshot-owned compiled pitch and shared dynamics/staccato gain. An initial C4 audio result near 297 Hz exposed phase inconsistency between drifting grains. Compiled grain centers now align to accumulated target phase using the declared source-root period; drift still controls source-content traversal. The original audio tolerance remains unchanged. Unsupported compiled paths cannot degrade to raw fallback or accept duplicate/untracked pitch inputs.

Direct C4/G4 sustain tests and normal snapshot pitch/half-gain/identity checks pass for the granular backend alongside PSOLA/spectral. Strict affected Debug/Release builds passed; all 64 focused cases passed per configuration (9.06/3.70 seconds). `git diff --check` passed. Authored stretch maps, unvoiced/continuation processing, measured source-pitch/transient qualification and full rendering-family integration remain required. This is not full U6/Beta GO acceptance; source remains local/uncommitted.

### U6 continuation: authored granular timing and unvoiced preservation, 2026-09-06

Stretch revision 4 consumes authored source maps for grain content, stable/release placement and fallback positions while compiled pitch sets phase/ratio. Known-unvoiced source/target spans bypass pitched grains and retain mapped source material. Unit bounds and map identity remain enforced; nondefault source-drift overrides conflict with authored timing rather than being silently ignored. Normal aligned rendering now supports all three classical backends.

The normal C4/G4 aligned-unit and second-vowel edit/undo-identity regression includes granular stretch; mixed-source tests verify its unvoiced relative samples. Strict affected Debug/Release builds completed and final post-build runs passed all 64 focused cases per configuration (9.26/3.41 seconds). `git diff --check` passed. Delayed live operations were waited on, not restarted. Granular continuation, transient/voicing qualification and full U6/Beta GO remain unfinished; source stays local/uncommitted.

### U6 continuation: granular single-vowel continuation consumes attack intent, 2026-09-06

Stretch revision 5 suppresses the recorded attack for compatible single-vowel continuation, including authored maps that hold the sustained source entry. Output length/vowel placement remain fixed and phase follows compiled pitch. Tests now compare mapped/unmapped repeated versus continued attacks in all three classical backends, verify held-interval pitch, and exercise normal lyric-command/snapshot/phrase undo identity with granular rendering.

Strict affected Debug/Release builds passed; all 64 focused cases passed per configuration (9.42/3.21 seconds). `git diff --check` passed. Raw-only performance consumption, overlapping voice allocation, remaining channels/families and general transition quality remain explicit U6 gaps; no broad core/native rerun or full U6/Beta GO acceptance is claimed. Source remains local/uncommitted.

### U6 continuation: raw backend consumes compiled pitch and expression, 2026-09-06

Raw revision 6 adds per-frame compiled playback-rate integration and shared dynamics/articulation gain. Raw cancellation now propagates through dispatcher/render/gain stages. This remains raw resampling with its existing unit-section boundaries, not pitch-preserving time warping. Snapshot intake rejects externally injected raw compiler objects; normal raw-only snapshot integration remains pending rather than bypassing ownership/cache identity.

Direct audio regressions verify C4/G4 pitch plateaus, sample-exact gain multiplication and cancellation. Strict affected Debug/Release builds passed; all 64 focused cases passed per configuration (9.07/2.67 seconds). `git diff --check` passed. Raw snapshot ownership, aligned raw pitch, overlapping voices and remaining U6/full Beta GO requirements remain open; source stays local/uncommitted.

### U6 continuation: raw-only snapshots consume owned performance, 2026-09-06

Normal raw snapshots now compile/freeze performance and forward it internally. Raw revision 7 derives expression origin from the requested vowel landmark and actual pitch-dependent rendered offset, preventing transposition from shifting the gain timeline. Mapped raw output applies compiled gain but explicitly rejects unsupported vibrato/manual/accepted pitch demands rather than ignoring them. Raw mapping remains non-pitch-preserving.

The normal pitch/half-gain/identity matrix includes Raw, and a transposed direct regression verifies every sample's gain at the derived origin. Strict affected Debug/Release builds passed; all 65 focused cases passed per configuration (10.44/3.66 seconds). `git diff --check` passed. Overlap allocation and unsupported-channel boundaries now also apply to raw-only snapshots; broader core/native compatibility, authored raw pitch and remaining U6/full Beta GO work remain open. Source remains local/uncommitted.

### U6 continuation: raw vowel continuation consumes reattack intent, 2026-09-06

Raw revision 8 now enters looped sustain for compiled single-vowel continuation while preserving its pitch-dependent rendered vowel offset. Repeated pronunciation retains the recorded attack. The transposed direct attack test and normal lyric-command/snapshot/undo-identity matrix include unmapped Raw alongside the existing mapped classical cases.

Strict affected Debug/Release builds passed; all 65 focused cases passed per configuration (10.71/3.66 seconds). `git diff --check` passed. Authored raw-map continuation/pitch, overlapping voices and remaining family/channel/U6 requirements stay open; no broad core/native or listening qualification is claimed. Source remains local/uncommitted and full Beta GO remains active.

### U6 continuation: bounded deterministic score voice allocation, 2026-09-06

Added a score-overlap allocator that preserves note IDs/timing, sorts by start/ID, reuses free voices deterministically, and prefers a unique compatible continuation predecessor. Ambiguous shared/literal vowel predecessors conflict instead of attaching silently to another voice. Allocation is bounded to 4,096 notes and 1–64 voices (default 16), distinct from permitted source preutterance overlap.

Strict Debug/Release compiler builds passed; all 12 compiler cases passed per configuration (3.14/1.62 seconds), including overlap, order invariance, limits, immutable input and continuation/ambiguity behavior. `git diff --check` passed. Per-voice projection, snapshot/unit-selection integration and audio summing remain unfinished; the current monophonic compiler still rejects overlaps. No broader tests or U6/Beta GO completion are claimed; source remains local/uncommitted.

### U6 continuation: independent per-voice performance projection, 2026-09-06

Added bounded per-voice compilation using the deterministic allocation plan. Projection retains note-owned edits/accepted selections only on their voice, preserves range ownership, omits unaccepted proposals from compiled copies and leaves the source project untouched. Supplied phonemes validate with full-score coverage before filtering; no independent phonemization or source time shift is introduced. Aggregate retained points are bounded across voices.

Strict Debug/Release compiler builds passed; all 12 expanded cases passed per configuration (3.12/1.60 seconds), covering simultaneous pitches, accepted/manual ownership isolation, immutable source state and incomplete phoneme rejection. `git diff --check` passed. Snapshot selection, rendering and mixing of independently allocated voices remain unfinished; other suites were not rerun. Source remains local/uncommitted and full U6/Beta GO remains active.

## Release status

### Requested checkpoint publication: 2026-09-05

The user explicitly requested committing and pushing the current project work, including the preserved crash/support changes, then continuing implementation. U2 was committed as `79d4faef`. Staging the approved source/test/plan files made `verify_tracked_source_closure.py` pass; that prior integration obligation was satisfied without weakening its check. Build directories, local audio evidence, private temporary files and unrelated branches/worktrees are not included in the commits.

Pre-publication review found a crash-handler lifetime race: native teardown drained writers before disabling handle acquisition. A shared lock-free `CrashWriterSlot` now invalidates acquisition before draining, with sequentially consistent ordering; both native destructors and macOS installation rollback close only afterward. A deterministic driver using the original order returned the old handle to a late writer for both descriptor/handle types (exit 1); the repaired order returned the invalid sentinel (exit 0). The root ran nine recovery/support tests successfully, including real macOS crash subprocesses. Independent recheck confirmed the ordering repair; Windows runtime integration remains unverified.

The main checkout retains its existing development branches and another developer's worktree. The repository's master-only publishing audit runs in an isolated checkout of the exact committed source, which is fast-forwarded and re-audited before pushing to `origin/master`. No branch deletion, force push or history rewrite is authorized or used.

Checkpoint commits `79d4faef` and `2f88761c8b43a893f1b4fcb1210d2b7aac036f7b` were pushed by fast-forward. `git ls-remote` independently confirmed the latter as `origin/master`. The isolated exact-commit checkout passed license/branch-policy and source-closure audits. The full Debug build, nine recovery/support tests in both configurations, and the core Debug suite passed after the race repair; the final three-entry core/recovery/closure CTest run took 97.34 seconds. This establishes source-checkpoint publication, not release qualification.

The subsequent isolated U3 style-resolver checkpoint `741ae2f244b9d3ff8eb6f31dc1f73cae54ea974d` was also fast-forwarded and independently verified as `origin/master`. The schema-8 continuation described above remains local and uncommitted; this ledger does not represent it as pushed source. Existing branches/worktrees remain intact.

No model was trained, no new production singer or commercially qualified voicebank was delivered, no independent listening/creator study ran, and no signed installed platform/host matrix was completed in this baseline work. Beta GO is not achieved. Continue independent implementation while keeping these acceptance obligations explicit.

U6 continuation: multi-voice performance compilation now rejects active forced-unit and seam dependencies that would cross voice boundaries or lose their original phoneme context during projection. Valid same-voice edits remain accepted; unresolved edits remain inactive. Strict Debug/Release compiler builds and all 13 compiler cases passed (2.65/0.79 seconds), plus `git diff --check`. U3–U5 remain locally accepted; U6 remains open pending renderer integration and its other acceptance obligations. These changes remain local/uncommitted.

U6 region-render continuation: validated voice projection now feeds independent snapshot/selection/render paths, with per-voice pronunciation and sequential audio summation. Tests prove exact three-voice sum/cache behavior and correct held-vowel context under overlap. Strict affected Debug/Release builds and 37 focused compiler/snapshot cases pass (8.51/1.91 seconds); `git diff --check` passes. Live editor scheduling, other U6 obligations and full Beta GO qualification remain open. Changes remain local/uncommitted.

U6 authoring continuation: the existing asynchronous coordinator already uses the voice-aware project/region path; corrected its monophonic phrase-progress estimate. A new runtime regression verifies overlapping-voice publication, pitch edit, exact undo PCM/cache restoration, progress counts and finite non-silent transport-buffer output. Strict Debug/Release authoring builds and all 11 focused cases pass (1.91/0.80 seconds), plus `git diff --check`. Installed UI/host/hardware and acoustic qualification are not claimed; U6 and Beta GO remain incomplete. Changes remain local/uncommitted.

U6 accepted-attack continuation: compiler revision 6 consumes accepted Attack milliseconds as an absolute-time amplitude ramp, respecting manual replacement, continuation and staccato gates without accelerating long attacks. Sample-by-sample regressions cover all four existing sample renderers; all 38 focused compiler/snapshot cases pass after strict Debug/Release builds (8.22/2.12 seconds), plus `git diff --check`. Accepted Release/Timing/timbral controls, procedural/neural-family integration and remaining U6/full Beta GO requirements stay open. This does not qualify recorded transients or listening quality. Changes remain local/uncommitted.

U6 attack normal-path verification: four-backend coverage now exercises manual ownership through EditorSession, snapshot/PCM changes, exact undo/redo restoration, frozen snapshot immutability and a real region-cache hit after undo. All 25 snapshot cases pass after strict Debug/Release builds (6.04/1.21 seconds), plus `git diff --check`. This uses pre-existing accepted fixture data; it does not implement the still-required proposal/take-acceptance workflow. No additional production change or broad-suite qualification is claimed. U6 remains open; changes remain local/uncommitted.

Take-selection continuation: implemented an undoable SetAcceptedPerformanceCommand for existing proposals, with expected-state, captured-revision, pronunciation and selection-validity checks. Manual ownership and proposal data remain intact. The normal attack/render/cache regression now uses the actual acceptance command. All 41 focused command/snapshot cases pass after strict Debug/Release builds (6.60/1.58 seconds), plus `git diff --check`. Proposal generation/review UI/async delivery and other-language integration remain unfinished; no full U40/U6/Beta GO acceptance is claimed. Changes remain local/uncommitted.

Proposal-delivery continuation: added AddPerformanceProposalCommand for inert, undoable proposal storage with bounded source/identity/revision validation. Tests deliver it through a one-time live job context, verify stale/duplicate rejection and unchanged snapshot identity, then explicitly accept through the tested render path. Strict Debug/Release affected builds and all 55 focused job-context/command/snapshot cases pass (7.06/2.41 seconds), plus `git diff --check`. No proposal generator or review UI was implemented; U6/full Beta GO remain incomplete. Changes remain local/uncommitted.

Live proposal verification: AuthoringRuntime now has a regression proving inert delivery causes no render submission, explicit acceptance publishes changed PCM with one submission, and undo restores original cached audio. Removing the inert proposal also schedules no render. A redundant test-owned preview was removed after the initial failure; all 12 authoring cases pass after strict Debug/Release builds (2.11/1.42 seconds), plus `git diff --check`. Existing production dispatch needed no change. Generator/review UI and full U6/Beta GO remain open; changes remain local/uncommitted.

U6 evaluator indexing: replaced per-sample linear ownership/selection scans with compiled per-channel/mode time indexes and prebound take/lane indexes. A 2,048-interval regression checks exact boundaries, tempo/rate changes, unordered input and copy/move stability. Strict Debug/Release affected builds and all 40 compiler/snapshot cases pass (12.21/2.07 seconds), plus `git diff --check`. No quantified whole-engine speedup or production-load qualification is claimed. U6 remains incomplete; changes remain local/uncommitted.

U6 voice-context consistency: multi-voice compilation now resolves verified Japanese input independently per projected voice, matching region rendering, instead of inheriting the wrong globally preceding vowel. Explicit overrides remain authoritative; custom/non-Japanese multi-voice context remains an explicit unsupported integration requirement. Strict Debug/Release affected builds and all 41 compiler/snapshot cases pass (12.37/2.15 seconds), plus `git diff --check`. U6/full Beta GO remain open; changes remain local/uncommitted.

U6 independent-context API: callers can now supply validated complete per-voice phoneme sequences without Japanese re-resolution. Coverage, assignment, ordering and aggregate-size checks prevent ambiguous projection. Expanded tests preserve custom continuation/reattack and reject malformed input; all 41 compiler/snapshot cases pass after strict Debug/Release builds (12.15/2.17 seconds), plus `git diff --check`. New-language resolver/render adapters and remaining U6/full Beta GO work remain open. Changes remain local/uncommitted.

Broad integration checkpoint: strict Debug/Release `seam_tests` builds succeeded and all 478 core/native cases passed (84.60/9.51 seconds), plus `git diff --check`. No repair was needed. Inspection established the next U7 migration boundary: sample-specific snapshot fields, factory hashing/freezing and pipeline timing/render consumption must migrate together to typed resources. No U7 implementation, installed-app qualification or U6/full Beta GO completion is claimed. Source remains local/uncommitted.

U7 typed-resource migration started: moved manifest/unit-plan/frozen-audio/selected-identity/bank-path ownership into SampleSingerResource within a variant; migrated factory, pipeline, region renderer, quality-tool consumers and tests. Sample dispatch rejects Procedural/Neural descriptors rather than falling back. Identity v6 includes the sample-resource contract tag. Strict Debug/Release core/snapshot/quality-tool builds passed; all 478 core/native plus 26 snapshot cases passed (combined 91.78/10.95 seconds), plus `git diff --check`. See `U7_TYPED_RESOURCE_STATUS_2026-09-06.md`. Non-sample payloads/backends and context-complete chunks remain unfinished; U6/U7/Beta GO are not accepted. Changes remain local/uncommitted.

U7 sample-option separation: moved PhraseRenderOptions into SampleSingerResource and updated factory/pipeline consumers. A normal-snapshot regression proves frozen option copying, identity restoration and -6 dB PCM scaling. Strict Debug/Release affected builds and all 27 snapshot cases pass (8.18/1.40 seconds), plus `git diff --check`. No fresh broad-suite/quality-tool run is claimed. Shared backend contracts, non-sample payloads/adapters and U6/U7/Beta GO acceptance remain open. Changes remain local/uncommitted.

U7 non-sample byte freezing: added private immutable payload storage and typed factories with kind/digest/bounds checks, deep-copy ownership and cancellation-aware hashing. Tests cover source mutation, mismatches, missing/invalid input and unsupported dispatch; all 28 snapshot cases pass after strict Debug/Release builds (8.15/1.28 seconds), plus `git diff --check`. Payloads are opaque bytes, not validated patches/models or working singer backends. U6/U7/Beta GO remain incomplete; changes remain local/uncommitted.

U7 output ownership: introduced common backend audio/frame-range finalization with exact context coverage, finite-PCM/bounds/cancellation checks and owned-only slicing. Sample rendering uses its full legacy extent, preserving output; independently declared snapshot chunk windows and identity binding remain unfinished. Strict Debug/Release affected builds and all 62 synthesis/snapshot cases pass (12.73/2.97 seconds), plus `git diff --check`. Context-complete splitting and non-sample backends are not claimed complete. U6/U7/Beta GO remain open; changes remain local/uncommitted.

U7 snapshot-owned windows: factory-owned absolute output ranges now enter identity v7 and control publication while retaining full phrase musical/source context. Four-renderer linked-note tests reconstruct exact whole-phrase PCM from two chunks and verify identity/invalid-window behavior. All 30 snapshot cases pass after strict Debug/Release affected builds (10.05/1.56 seconds), plus `git diff --check`. Full-context re-rendering per slice is not efficient context trimming or automatic scheduling; these and non-sample backends remain open. U6/U7/Beta GO are incomplete; changes remain local/uncommitted.

U7 snapshot scheduling: added immutable snapshot submission with window-specific job IDs and pre-cache family validation. Cache/worker PCM must match declared owned endpoints before publication. Two-worker tests verify sibling coexistence, exact reconstruction, cache reuse and wrong-extent rejection; all 31 snapshot cases pass after strict Debug/Release affected builds (9.93/1.43 seconds), plus `git diff --check`. Automatic planning/group invalidation, context efficiency and native runtime adoption remain unfinished. U6/U7/Beta GO stay open; changes remain local/uncommitted.

U7 revision groups: snapshot jobs now use project-scoped keys and stable region revision groups. New revisions cancel obsolete group work, late unseen old windows are stale, and queued old successful/cache-hit PCM is removed at delivery. All 32 snapshot cases pass in strict Debug/Release (10.29/1.54 seconds); strict Release core/native build and 478 cases also pass (9.67 seconds), plus `git diff --check`. Automatic planning, lifecycle cleanup/reset, no-replacement invalidation and native adoption remain open. U6/U7/Beta GO are incomplete; changes remain local/uncommitted.

U7 no-replacement invalidation: added explicit bounded, monotonic group revision floors for deletion/mute-style transitions without dummy jobs, plus a shared snapshot group-key helper. Tests verify old queued/late jobs cannot publish, other groups remain valid and cached content remains reusable. All 33 snapshot cases pass after strict Debug/Release affected builds (9.89/1.41 seconds), plus `git diff --check`. Lifecycle event wiring and reset/reclamation remain open; U6/U7/Beta GO are incomplete. Changes remain local/uncommitted.

U7 chunk planning: added deterministic bounded output-window planning and frozen-source snapshot subdivision with shared resource objects, identity parity and aggregate metadata limits. Tests reconstruct exact four-backend PCM after source WAV removal from its expected path and reject invalid/expanding plans. All 34 snapshot cases pass after strict Debug/Release affected builds (11.61/1.95 seconds), plus `git diff --check`. Project-level extent derivation/submission and efficient context rendering remain open. U6/U7/Beta GO are incomplete; changes remain local/uncommitted.

U7 scheduler reset: added epoch-isolated reset for queued/active work, completion delivery and revision-group tracking, preserving reusable PCM. A held-worker regression proves old work cannot enter a new revision-zero session and discarded queued work never executes. All 35 snapshot cases pass after strict Debug/Release affected builds (16.04/1.61 seconds), plus `git diff --check`. Lifecycle event wiring and automatic chunk orchestration remain unfinished; U6/U7/Beta GO stay open. Changes remain local/uncommitted.

U7 owned-output assembly: added bounded exact-coverage assembly from unordered chunk PCM views, rejecting incomplete/overlapping/non-finite output and supporting cancellation without partial publication. Real scheduler completions reconstruct full PCM through the helper. All 36 snapshot cases pass after strict Debug/Release affected builds (10.65/1.41 seconds), plus `git diff --check`. Completion-manifest provenance checks and native project orchestration remain unfinished; U6/U7/Beta GO stay open. Changes remain local/uncommitted.

U7 completion-manifest gate: assembly now requires exact expected job/hash/revision/rate/window matches and complete successful PCM delivery before coverage validation. Real worker/cache-hit regressions and altered-metadata rejection pass across all 36 snapshot cases after strict Debug/Release affected builds (10.97/1.68 seconds), plus `git diff --check`. Caller manifest freshness and final native revision-gated publication still require orchestration integration. U6/U7/Beta GO remain incomplete; changes remain local/uncommitted.

Procedural recipe foundation: added seam_voice_design with an initial versioned VoiceRecipe and bounded, strict JSON codec. Roundtrip tests preserve controls, style/phone poses and a full uint64 seed; malformed ranges, duplicate poses, unsupported engine/schema, invalid UTF-8, unknown fields and malformed seeds reject. Strict Debug/Release dedicated builds and the regression case pass (0.47/0.39 seconds), plus `git diff --check`. See `docs/formats/VOICE_RECIPE_V1.md`. This starts a U18 prerequisite, not DSP, a qualified voice or U18/U6/U7/Beta GO acceptance. Changes remain local/uncommitted.

Procedural recipe/resource bridge: strict recipes now freeze into identity-bound immutable procedural payloads and decode through format/version/ID validation. Tests distinguish semantic recipes from arbitrary hash-valid bytes and preserve prior data across draft edits. Both dedicated voice-design cases pass after strict Debug/Release builds (0.47/0.42 seconds), plus `git diff --check`. Actual synthesis/dispatch, UI, provenance and acoustic qualification remain unfinished; Beta GO stays open. Changes remain local/uncommitted.

U19 oral-resonance foundation: implemented a stateful linear parallel resonance bank with recipe selection, Nyquist/stability checks, relative band weights, bounded normalized input and atomic failed/cancelled-block state handling. Diagnostic tests verify spectral reweighting, tone-frequency preservation, block invariance and parameter extremes. All three voice-design cases pass in strict Debug/Release (0.56/0.35 seconds), plus `git diff --check`. See `U19_VOCAL_DSP_STATUS_2026-09-06.md` for the primary formula source and limits. Phonation, nasal modeling, interpolation, articulation, renderer integration and listening qualification remain unfinished; no complete voice or Beta GO is claimed. Changes remain local/uncommitted.

U19 compiled-F0 excitation: added sequential bounded harmonic excitation, deterministic filtered aspiration, recipe modulation and transactional state advancement from immutable compiled performance. Actual PCM tests measure C4→G4, block/reset reproducibility, seed variation, silence, failure/cancellation and one steady-state folded-harmonic diagnostic. All four voice-design cases pass after strict Debug/Release builds (1.01/0.48 seconds), plus `git diff --check`. This is not a qualified alias-free glottal source or complete singer; tract/phrase integration and acoustic acceptance remain open. Changes remain local/uncommitted.

Sustained-pose audition integration: connected frozen recipe decoding, compiled-F0 excitation, oral resonance, post-filter dynamics/articulation and owned-window finalization with resource/algorithm metadata. Tests verify block equality, exact slices, half gain and closed staccato gates; all five voice-design cases pass after strict Debug/Release builds (1.42/0.55 seconds), plus `git diff --check`. This is not consonant/phrase synthesis or procedural snapshot dispatch; native integration and acoustic/intelligibility qualification remain open. U19/U20/Beta GO are incomplete; changes remain local/uncommitted.

Source-project identity repair: the internal canonical render ProjectId was incorrectly being used for scheduler isolation. Added real sourceProjectId metadata, used it for job/group/manifest identity and preserved canonical content hashes for audio reuse. A two-project regression verifies independent revision behavior despite equal acoustic hashes; all 37 snapshot cases pass in strict Debug/Release (10.71/1.42 seconds), plus `git diff --check`. This corrects an earlier isolation claim. Procedural snapshot factory/dispatch and native orchestration remain unfinished; Beta GO stays open. Changes remain local/uncommitted.

Procedural snapshot/dispatch: implemented whole-region frozen recipe/pronunciation/performance capture and explicit sustained-vowel pipeline dispatch with independent resource/cache identity and no fake sample units. Guards reject unsupported consonants, vowel transitions, multi-nucleus notes, retiming and sample-specific edits. Exact DSP parity, immutable state, owned slices and sample-file independence are covered; all 43 focused snapshot/voice-design cases pass in strict Debug/Release (11.98/2.37 seconds), plus `git diff --check`. Scheduling/native selection, fuller articulation and acoustic acceptance remain open; Beta GO is not achieved. Changes remain local/uncommitted.

Procedural scheduling: supported procedural snapshots now enter workers and cache lookup only after shared semantic preflight; full/owned frame extents and matched completion assembly remain enforced. Worker/direct PCM parity, cache reuse, invalid cached-request rejection and exact two-chunk assembly pass across all 43 focused cases in strict Debug/Release (12.01/1.70 seconds), plus `git diff --check`. Native resource selection, automatic procedural subdivision and full articulation/quality work remain open. Beta GO is incomplete; changes remain local/uncommitted.

Procedural subdivision: automatic owned-window derivation now shares the procedural identity builder and immutable source objects, with output/context and metadata budgets enforced. Derived chunks schedule and reconstruct exact PCM; all 43 focused cases pass in strict Debug/Release (11.74/1.72 seconds), plus `git diff --check`. Full-context DSP repetition, native integration and articulation/quality qualification remain unfinished. Beta GO stays open; changes remain local/uncommitted.

Procedural causal-window optimization: stops DSP at owned end and discards pre-roll PCM while retaining exact source/filter state. The checked 4,000–9,000 slice processes 9,000 frames rather than 24,000 and retains only its 5,000 samples. All 43 focused tests pass in strict Debug/Release (13.10/2.99 seconds), plus `git diff --check`. No whole-app speedup or noncausal-lookahead shortcut is claimed; repeated pre-roll/native/quality work remain open. Beta GO is incomplete; changes remain local/uncommitted.

Procedural streaming/checkpoints: added sequential sustained-pose rendering with independent DSP checkpoint copies, reset, forward-gap processing and whole-window rollback. Two adjacent 12,000-frame windows advance 24,000 rather than 36,000 DSP frames and match full PCM exactly. Gap/tail, checkpoint independence, invalid-window and zero/nonzero-position failure recovery regressions pass. Strict affected Debug/Release builds and all 43 focused cases pass (11.94/1.76 seconds), plus `git diff --check`. Independent snapshot jobs still use one-shot rendering; checkpoint identity binding and scheduler/native adoption remain open. Clarified the historical U3 handoff: U3–U5 are locally accepted, U6 is the next unaccepted unit. No new unit or Beta GO acceptance is claimed; source remains local/uncommitted.

Procedural snapshot checkpoint binding: added a single-owner stream that requires exact immutable context objects and matching resource/lifecycle/render metadata before reuse, and refuses output ownership expansion. Tests prove rejected/cancelled requests preserve state and valid sibling chunks reconstruct full PCM with no repeated DSP frames. Strict Debug/Release affected builds and all 38 snapshot cases pass (11.02/1.48 seconds), plus `git diff --check`. Scheduler worker sharing, out-of-order checkpoint selection and native lifecycle adoption remain unfinished; U6/U7/Beta GO stay open. Changes remain local/uncommitted.

Procedural scheduler checkpoint reuse: workers now copy the nearest compatible completed checkpoint, render outside the scheduler lock and retain at most 16 states. Reverse-order/unavailable checkpoints fall back to context-origin rendering; reset and revision invalidation discard obsolete state, and epoch/cancellation gates prevent stale retention. Worker teardown now joins before member destruction. Sequential, reverse-order and reset tests verify exact PCM and actual DSP-frame counts. Strict affected Debug/Release builds and all 38 snapshot cases pass (11.16/1.49 seconds), plus `git diff --check`. Concurrent starts may still repeat pre-roll; native integration, full expressions/articulation and acoustic acceptance remain unfinished. U6/U7/Beta GO remain open; source is local/uncommitted.

Typed project-rendering integration: introduced sample/procedural track-source variants and shared project rendering, preserving the old sample entrypoint. Procedural regions now enter real project routing at their absolute frame origin without fake sample metadata. New tests verify direct PCM equality, leading silence, pan, gain and invalid source/capability rejection. All 39 snapshot cases pass in strict Debug/Release (11.92/1.57 seconds), and the rebuilt Release core/native suite passes (9.47 seconds); `git diff --check` passes. Native coordinator selection/preflight, persisted recipes, procedural project caching/chunk scheduling and overlap allocation remain unfinished. U6/U7/Beta GO remain open; changes are local/uncommitted.

Typed preview coordinator: added explicit resolved-source submission, procedural recipe preflight, typed project rendering and procedural phrase-progress counting while preserving sample trust checks and revision-gated publication. Async tests verify exact PCM, truthful non-bank metadata, corrupt-resource failure with last-good-audio preservation, recovery and old-revision rejection. All 28 coordinator/runtime cases pass in strict Debug/Release (7.58/2.87 seconds), plus `git diff --check`. Native UI/session selection and recipe persistence are not yet implemented; project caching/chunk scheduling, articulation and acoustic acceptance remain open. U6/U7/Beta GO stay incomplete; source is local/uncommitted.

Recipe file persistence: implemented validated durable atomic save and bounded read-only loading into canonical immutable resources, optionally requiring exact singer identity. Tests preserve original bytes on invalid/cancelled/pre-replacement-injected save failures, reject stale identity/oversized/future/symlink input and retain old frozen resources across valid draft changes. All six voice-design cases pass in strict Debug/Release (1.59/0.61 seconds), plus `git diff --check`. This supplies a durable loader for upcoming native selection; saved project references, UI, relinking and lineage remain unfinished. No new unit or Beta GO acceptance is claimed; changes remain local/uncommitted.

Saved procedural selection contract: project schema 9 adds a validated track-level recipe path, exact identity and style; schemas 1–8 migrate with no procedural selection. Strict decode rejects malformed references and version-downgraded non-null selections. Project rendering/snapshot factories prevent sample substitution and mismatched procedural identity/style. All 54 codec/snapshot cases pass in strict Debug/Release (12.22/1.62 seconds for the selected entries); the rebuilt Release core/native suite passes 479 cases (9.26 seconds) after updating two obsolete schema-8 test literals and the required include. `git diff --check` passes. See `docs/formats/PROJECT_JSON_V9.md`. Runtime path resolution, native selection/undo/relinking and procedural export integration remain open. No additional unit/Beta GO acceptance is claimed; source remains local/uncommitted.

Saved-recipe runtime resolution: authoring requests now carry unresolved typed file references into the render worker, which resolves project-relative paths, verifies exact saved identity and renders without sample fallback. No file I/O was added to UI request construction. An actual saved/reopened project previews through transport with no sample bank, follows note edits/undo, rejects changed recipe bytes while preserving last-good audio, and recovers after restoration. Unsaved relative paths reject without CWD guessing. All 29 runtime/coordinator cases pass in strict Debug/Release (8.16/3.00 seconds), plus `git diff --check`. A non-copyable test-factory construction issue was corrected without changing production ownership. Native recipe selection/relinking UI, selection commands, packaging and export remain open. U6/U7/Beta GO stay incomplete; changes remain local/uncommitted.

Undoable recipe selection: added a pure expected-value-guarded command for selecting/clearing/repointing recipe references, with track audio invalidation and exact undo/redo. Sample-bank selection now clears and restores procedural selection appropriately. Fixed live performance-job identity comparison to include recipe references; pending jobs cannot survive singer selection or be revived by undo. Command/runtime regressions prove exact state/audio restoration, stale/invalid rejection and sample switching. All 45 focused cases pass in strict Debug/Release (4.09/2.54 seconds), plus `git diff --check`. Native picker/relink UI, full asynchronous picker lifetime guards, packaging and export remain open. No new unit or Beta GO acceptance is claimed; changes remain local/uncommitted.

Native recipe picker/relink: added macOS File-menu actions and controller file-dialog handling. Selection commits an absolute external recipe reference; relinking enforces the prior identity/style. A captured live job context rejects obsolete modal-dialog results, including same-content document replacement. Injected-dialog tests cover cancellation, selection, identity mismatch, relink, undo and stale results. Strict Release core/native build and suite pass (10.26 seconds), plus `git diff --check`. Actual native-panel visual interaction was not tested. Multi-style selection still requires explicit style-choice UI, and packaging/export/cross-platform parity remain open. U6/U7/Beta GO stay incomplete; changes remain local/uncommitted.

Procedural export integration: typed sources now flow through single-file final export and transactional master/stem export sets, including both standalone actions. Set receipts separate procedural identities/styles from sample banks. A real WAV regression verifies exact Float32 single/master/stem PCM and preservation of an existing committed set after changed recipe content is rejected. All 15 export cases pass in strict Debug/Release (1.19/0.52 seconds); the rebuilt Release core/native suite passes (10.45 seconds), plus `git diff --check`. Packaging, native-panel visual verification, multi-style selection and acoustic qualification remain open. U6/U7/Beta GO are incomplete; changes remain local/uncommitted.

Multi-style native recipe selection: added a bounded, accessibility-labeled macOS style popup backed by the loaded recipe's explicit style set. Controller validation rejects unknown styles and obsolete dialog results; cancellation remains side-effect free. Injected-dialog tests cover non-default selection/undo, cancellation, invalid responses and same-content document replacement during style selection. Strict Release native/core build and all 482 cases pass (10.14 seconds), plus `git diff --check`. Actual native-popup visual interaction, other-platform parity, packaging and acoustic qualification remain open. No new unit or Beta GO acceptance is claimed; changes remain local/uncommitted.

Save As recipe-reference safety: fixed silent base-directory retargeting by verifying relative recipe identities at the destination before writing a relocated project. Missing or changed recipes preserve existing destination bytes and all live document state; an exact destination recipe permits save without clearing undo history. Strict Release core/native build and all 483 cases pass (10.21 seconds), plus `git diff --check`. No automatic copying or portable-package completion is claimed; a proper packaging transaction remains open alongside acoustic/full-scope work. U6/U7/Beta GO remain incomplete; changes remain local/uncommitted.

Transactional recipe/project snapshot packaging: added opt-in export preparation that freezes canonical recipe resources, deduplicates bounded bytes by hash, writes relative references into a project copy and publishes those files through the existing export transaction. Audio outputs use the same frozen resources. A regression removes the original recipe mid-export, then reopens the packaged project and reproduces exact PCM, including receipt/recovery coverage. All 15 export cases pass in strict Debug/Release (1.18/0.56 seconds), plus `git diff --check`. The working project is unchanged. Native option exposure and bundling of external sample/backing dependencies remain open; this is not universal portable-project completion. U6/U7/Beta GO stay incomplete; changes remain local/uncommitted.

Native packaging opt-in: macOS Export Set now offers Audio Only (default), Include Project, and Cancel for procedural projects, with explicit scope/privacy wording. Document-generation checks cover both modal steps. Controller tests verify cancelled/stale requests create no export, packaging commits asynchronously without mutating the live project, and audio-only output excludes editable project/recipe files. Strict Release core/native build and all 483 cases pass (11.10 seconds), plus `git diff --check`. Actual native-panel visual verification, cross-platform parity, broader dependency packaging and acoustic/full-scope acceptance remain open. Beta GO is incomplete; changes remain local/uncommitted.

U6 accepted amplitude release: compiler revision 7 accepts Release lanes through shared ownership/interpolation, applies a within-note end fade, preserves linked continuations and prevents released source tails from reopening. Manual Replace and zero/absent release remain neutral. Multi-rate evaluator/shared-gain tests and actual procedural PCM multiplication checks pass, along with all snapshot regressions: 62 focused cases in strict Debug/Release (18.11/3.39 seconds), plus `git diff --check`. Release-consonant modeling, remaining expression channels and Neural/acoustic qualification remain unfinished. U6/U7/Beta GO stay open; changes remain local/uncommitted.

U19 oral-pose crossfade primitive: added bounded transitions between independently stable resonance banks, with smoothstep output blending, differing formant-count support and atomic bank/progress rollback. Tests verify exact block invariance, blend oracle/target convergence, cancellation/invalid input and reset behavior. All 46 voice-design/snapshot cases pass in strict Debug/Release (14.02/2.19 seconds), plus `git diff --check`. This primitive is not yet scheduled by phonemes in the phrase adapter; nasal/retargeting/articulation and listening work remain open. U19/U20/Beta GO remain incomplete; changes are local/uncommitted.

Procedural vowel-sequence integration: revision 2 schedules validated oral-vowel poses from compiled note-bound phonemes, splits DSP at absolute events, uses bounded 20 ms/short-note crossfades and preserves transition state across chunks/reset. Required poses validate before cache admission; same-vowel notes retain their bank. A real a-to-i sequence changes only the post-boundary audio and reconstructs exactly across a mid-transition scheduler checkpoint without repeated DSP. All 47 voice-design/snapshot cases pass in strict Debug/Release (14.37/2.70 seconds), plus `git diff --check`. Consonants, multi-nucleus note timing, nasal behavior and acoustic/intelligibility qualification remain unfinished. U19/U20/Beta GO stay open; changes remain local/uncommitted.

Shared multi-nucleus vowel timing: compiler revision 8 retains immutable shared phoneme timing anchors; procedural revision 3 schedules from those anchors rather than note-only boundaries. Multiple oral vowels in one note now render with strict key/coverage checks and phoneme-span-bounded transitions. Tests verify same-note a-to-i timing/audio and a very short a-i-a sequence, while existing checkpoint/reset cases remain passing. All 64 focused cases pass in strict Debug/Release (19.06/3.35 seconds), plus `git diff --check`. Explicit retiming, consonants, nasal behavior and acoustic qualification remain open; U6/U19/U20/Beta GO are incomplete. Changes remain local/uncommitted.

Explicit within-note procedural timing: revision 4 consumes shared edited vowel spans, gates excitation/publication outside them and splits DSP at activity boundaries. Invalid/overlapping/out-of-note timing rejects through common preflight before cache admission. Tests verify delayed onset, internal silence, early end, exact owned-chunk reconstruction and early/late out-of-note rejection. All 47 voice-design/snapshot cases pass in strict Debug/Release (16.83/2.93 seconds), plus `git diff --check`. Extended phonation context, consonants, nasal behavior and acoustic/click-free qualification remain unfinished. U19/U20/Beta GO stay open; changes are local/uncommitted.

Edited vowel-boundary ramps: procedural revision 5 replaces hard authored gate edges with at-most-5-ms smoothstep ramps, shortened for brief spans. Contiguous vowels retain uninterrupted gain; silence remains exact outside activity. Boundary-zero and mid-ramp checkpoint reconstruction checks pass across all 47 voice-design/snapshot cases in strict Debug/Release (14.85/2.78 seconds), plus `git diff --check`. This is an artifact-reduction implementation, not listening/intelligibility acceptance. Extended context, consonants, nasal behavior and full Beta GO remain unfinished; changes are local/uncommitted.

Procedural output markers: revision 6 returns planned vowel gesture keys/labels and owned-window-bounded spans with explicit clipping flags through the phrase pipeline, separate from sample placements. No markers are fabricated for unscheduled auditions or gap-only windows, and no acoustic measurement/approval is implied. Full/cropped/gap marker regressions pass across all 47 voice-design/snapshot cases in strict Debug/Release (14.83/2.76 seconds), plus `git diff --check`. Durable baking, scheduler marker delivery, consonants, nasal behavior and full acoustic/Beta GO acceptance remain unfinished; changes are local/uncommitted.

Scheduler marker delivery: typed snapshot requests now carry bounded frozen-window marker projections through workers and cache hits. Stale/cancelled completion gates clear them with PCM; manifest assembly compares them against the expected snapshot and rejects tampering. All 40 snapshot cases pass in strict Debug/Release (13.21/1.78 seconds), plus `git diff --check`. PCM cache bytes are unchanged; markers derive from current frozen input metadata. Durable baking, native marker presentation, consonants/nasal behavior and full Beta GO remain unfinished; changes are local/uncommitted.

Explicit procedural candidate baking: added opt-in mono Float32 region candidates, planned-marker metadata, exact audio/resource/render lineage and unapproved status to the existing export transaction, including source project/recipe snapshots. Candidate-only requests are supported with bounded candidate/note/file counts and duplicate-source rejection. Tests prove exact Final pipeline PCM, hashes/relative marker bounds, recovery and unchanged source project. All 15 export cases pass in strict Debug/Release (1.25/0.57 seconds); rebuilt Release core/native tests pass (10.00 seconds), plus `git diff --check`. See `docs/formats/PROCEDURAL_CANDIDATE_V1.md`. Native bake actions, producer ingestion/QC, consonants/nasal behavior and acoustic/full Beta GO acceptance remain open; changes are local/uncommitted.

Native candidate-bake action: added the macOS File-menu command and dedicated save-dialog purpose, routing candidate-only/source-snapshot requests through the existing asynchronous export worker and document-generation guard. Tests verify empty input, cancellation, stale modal results, committed WAV/metadata/source files, no master audio and unchanged working project. Strict Release core/native build and all 483 cases pass (10.90 seconds), plus `git diff --check`. Win32 dialog routing is source-updated but unverified; real panel interaction, producer ingestion/QC, consonants/nasal behavior and full Beta GO remain open. Changes are local/uncommitted.

Strict candidate loader: added read-only loading against an expected recipe, bounded metadata/audio, canonical phoneme keys, ordered marker/frame validation and owned-byte digest/Float32 decoding. No approval or repository mutation is returned. Real-bake and malformed metadata/audio regressions pass across all 15 export cases in strict Debug/Release (1.25/0.63 seconds), plus `git diff --check`; a decimal-vs-hex key parser mistake was repaired against the domain formatter. Producer repository integration/lineage/QC and full consonant/acoustic/Beta GO acceptance remain open; changes are local/uncommitted.

Producer candidate import/lineage: added strict candidate loading into the existing raw-asset/take transaction with stored-digest recheck, mandatory MarkerReview/no review, procedural-strategy gating and one-generation lineage persistence. Exact candidate/recipe bytes and hashes are bound to the raw asset; retakes clear prior review flags. Tests verify import/recovery, approval rejection, lineage tamper rejection, retakes and failed-save state preservation across all 17 export/producer cases in strict Debug/Release (1.46/0.61 seconds), plus `git diff --check`. Verification caught and resolved the missing journal-action registration and JSON include. Native/CLI producer wiring, measured QC/admission, consonants/nasal behavior and full Beta GO remain open; changes are local/uncommitted.

Producer CLI import: added `import-procedural` with explicit existing workspace, candidate/recipe files, inventory assignment, MIDI layer, operator/time and optional retake predecessor. It delegates to the strict repository import and prints MarkerReview/digest/generation without approval flags or strategy creation. A real shell-free subprocess regression verifies committed retake/lineage recovery and malformed MIDI rejection. Export integration plus CLI help/validate/inspect CTest entries all pass in strict Debug/Release (2.84/1.48 seconds), plus `git diff --check`. Windows subprocess execution, native producer import UI, resumable generation/QC and full Beta GO remain open; changes are local/uncommitted.

Native producer intake: completed the interrupted Studio import integration with Cmd/Ctrl+I metadata/recipe dialogs, selected-row retake binding, strict repository import and inspection clearing. Centralized epoch/generation/row checks now run after each dialog; workspace reopening invalidates prior context. Recording must finish before import. All 15 controller/export cases pass in strict Debug/Release (1.44/0.61 seconds), the Release native Studio executable builds, and `git diff --check` passes. Non-fatal host Xcode filesystem/cache warnings appeared during successful builds; compiler policy was unchanged. Real panel interaction, asynchronous intake/responsiveness, measured QC, consonants/nasal behavior and full Beta GO remain open; changes are local/uncommitted.

2026-09-23 — U15 acoustic analysis contract against `03f82ab5`. The plan names
`acoustic_analysis.hpp` and `test_audio_conditioning.cpp`; neither existed, and the
core U15 deliverable was missing. The analyser ran, callers read its voicing
decisions, and the result was discarded, so renderers and producer QC each derived
voicing separately and nothing recorded which algorithm reached a conclusion.

Added `AcousticAnalysis`: per-unit voicing spans with measured fundamental and
confidence, the digest of the exact audio measured, and the algorithm identity.
Spans are not analysis frames. Frames overlap (a frame at origin s covers
[s, s + frameSize) while frames are one hop apart), so one span per frame would
describe the same sample several times, possibly differently. Each frame is given
the region no later frame also covers and consecutive agreeing frames are merged,
so spans partition [0, decodedFrames) exactly and a gap or overlap is rejected.
Self-contradictory records are refused: an unvoiced span reporting a fundamental,
or a voiced span reporting none.

Measured on an alternating voiced/fricative/voiced fixture: three spans of 200 Hz
voiced, unvoiced, 240 Hz voiced, with confidence 0.98 / 0.095 / 0.978, and the
fricative middle reported unvoiced. An earlier revision of this change emitted
overlapping spans and was caught by its own validation on that fixture, not by
inspection.

Algorithm identity makes "regenerate derivatives after algorithm changes"
checkable: a record from a different revision is refused rather than silently
reinterpreted. The analysis configuration is now single-sourced as
`producerPitchConfig`, `producerPitchMarkConfig`, `producerAnalysisWork` and
`producerPitchLimits`; the producer draft path, QC and the analysis all use them.
Three sites had each written their own copy of the 2048/256, 60-1200 Hz, 0.32
constants and agreed only by convention — the same shape of duplication that
produced three separately aliasing resamplers.

Bank QC now consumes the contract. A stored analysis is re-checked against the
audio present rather than trusted because it parsed: `acoustic-analysis-stale` for
a record that cannot be trusted for this audio, `acoustic-analysis-mismatch` when
it binds correctly but disagrees with a fresh analysis. Absence remains
non-error. The sidecar is located through the same containment helper as audio.

Removed a defect introduced within this work: `analyzeUnitAcoustics` accepted a
`PitchConfig` defaulting to direct correlation while the producer and QC use FFT, so
a caller omitting the argument stored a different measurement under the canonical
algorithm identity. That is the same defect as an analysis that does not say which
audio it came from, and it is what made the QC round trip disagree with itself.
The function now has no configuration parameter. Determinism was measured first —
two analyses of the same input give bit-identical f0 and confidence — so exact
equality is a fair comparison and the mismatch was a real disagreement.

Fixed alongside, found while closing U15 scenario 3: stored pitch marks are a
measurement of one specific take and nothing recorded which. Every rule applied to
a mark was take-independent, so replacing a WAV of the same name and length left
marks that satisfied all of them while describing audio that no longer existed.
Measured with the product own analyser on the shipped fixture: stored marks imply
390.3 Hz while the audio present measures 980.0 Hz, 1594 cents apart, with the
declared root agreeing with the marks rather than the audio. `BankValidator` now
raises `pitch-marks-stale` past 300 cents, and `git HEAD` validator was compiled
into the same probe as a negative control: it reports nothing.

Verified: CTest 183/183; external-beta 189 tests OK; `verify_tracked_source_closure`,
`verify_phase12b_contracts` and `verify_phase11_contracts` PASS; producer suite
51/51. Formats recorded in `docs/formats/ACOUSTIC_ANALYSIS_V1.md` and
`docs/implementation/PITCH_MARK_STALENESS_2026-09-23.md`. U15 now has all three
plan scenarios covered in code. Unit acceptance is NOT claimed: the plan requires
fixed-corpus numerical and listening results, and 0 of 46 blind-listening
scoresheet rows are filled. The shipped demo fixture is deliberately left with its
stale marks, since silently rewriting them would erase the evidence that this error
class occurs.

U15 residual, measured rather than assumed: the plan verification clause reads
"renderers and producer QC consume the same versioned analysis contract". QC now
does. The renderers do not. `libs/seam-rendering/src/render_snapshot.cpp` has zero
references to the analysis sidecar, `FrozenUnitAudio` carries only the alignment
and the audio digest, and both source-map builders leave measured voicing unused:
`compileShortUnitMarkerMap` never populates `SourceTargetMap::voicing`, and
`compileSourceTargetMap` fills it from `PhonemeTimingAnchor::voiced`, which is a
phoneme-symbol claim from the phonemizer rather than a measurement. The
`voicedAtSource` accessor the renderers consult is therefore drawn from the written
phoneme, not from the take.

So three paths can currently disagree about where one installed take is voiced: the
stored analysis, the phoneme symbols, and the `acousticVoicedAt` helper, which
nothing calls yet. That is the same class of disagreement U15 was opened to remove,
and it is not closed. No defect has been demonstrated from it in a render -- the
renderer re-checks bounds and the guards fail closed -- so it is recorded as an
open clause rather than claimed as a finding. Wiring measured voicing into source
map construction is the next U15 step, and it changes render behaviour, so it needs
its own measured before/after rather than being folded into this entry.

U15 verification clause now met. `render_snapshot.cpp` loads the stored analysis
sidecar when one exists (optional; a bank without one keeps its previous
rendering, a broken one is refused rather than silently ignored, since ignoring it
would render unvoiced material as voiced and look like success), and
`FrozenUnitAudio` carries it through to `compileShortUnitMarkerMap`, which emits no
voicing of its own.

The mechanism was measured before being called a defect: a renderer asks
`voicedAtSource(...) == false` in order to skip a sample, and that accessor answers
`nullopt` when no span covers the frame, so "no answer" read as "not unvoiced". A
marker-only map reported `voicingSpans=0` across the whole unit, so every frame of
a short CV transition -- including the unvoiced consonant -- presented as voiced to
every renderer. `applyMeasuredVoicing` now replaces that with the measured spans,
clipped to the map extent and made contiguous because the map requires complete
coverage; it returns false and changes nothing when the analysis covers no part of
the range, so a partial description cannot reintroduce the same
unknown-means-voiced default.

Measured on a real CV fixture (0.5 s of unvoiced fricative then a 220 Hz vowel):
before, every probe answered unknown; after, the fricative answers unvoiced and the
vowel answers voiced, and an empty analysis is declined rather than half-applied.
Note the boundary is deliberately not asserted at the marker: a 2048-sample window
at the consonant edge already reaches into the vowel, so the analyser correctly
calls that frame voiced. The assertion sits where the measurement is unambiguous.

Verified: CTest 183/183 including the render path; external-beta 189 tests OK;
source closure, phase12b and phase11 contracts PASS. This changes render output
for banks that store an analysis, which is why the whole suite was rerun rather
than the affected target alone.

2026-09-23 — U16 scenario 1 against `79cabbf7`. The suite transposed up only
(rootMidi 69 rendered to target 72), so a defect at exact integer down-ratios was
invisible: rendering to a target an exact integer multiple below the source left
the output at the SOURCE pitch. Measured 440.00 Hz out for a 220.00 Hz target
(ratio 2.000, +1200.0 cents) and the same for a 110.00 Hz target (ratio 4.000,
+2400.0 cents).

Established by holding the target fixed and varying only the source, so the target
could not be the explanation: every source from ratio 1.000 to 1.498 transposed
correctly to within 1 cent, and the source at exactly ratio 2.000 did not. Two
instruments agreed it was not an analyser artefact — the product analyser reported
440.00 Hz and an independent FFT peaked at 440.00 Hz with the 220 Hz partial 39 dB
down. The harness was validated first by reproducing the existing passing test
exactly.

Cause: the grain read used sourceMark.frame + relative * sourcePerOutput, where
sourcePerOutput is only the sample-rate ratio and carries no target-pitch scaling.
A grain was a 1:1 copy holding the source periodicity at its original spacing, and
the output lost it only where neighbouring grains overlapped enough to cancel — so
at integer down-ratios, where grains barely overlap, the source pitch survived. That
is why the failure tracked the ratio rather than the target.

Grains are now resampled by periodSource * sampleRateRatio / targetPeriod, and the
overlap-add window is sized from the target period to match. Sizing the window from
the source period was the wrong unit once the read step changed: at ratio 2.52 a
219-sample window read 0.6 of a source period and the truncation left a subharmonic
at 87.56 Hz. SEAM_PSOLA_RENDERER_REVISION went 10 -> 11; seam_phase12a_tests caught
the omission of that bump, because cached PCM from the old renderer would otherwise
have been reused against a changed renderer.

Verified across all 46 semitone targets from one source: the 42 below the analyser
1200 Hz ceiling land within about 3 cents including both previously failing integer
ratios, and the four above the ceiling were confirmed by FFT within 2 cents. The new
test was checked for teeth by compiling the pre-fix classic_psola.cpp from git and
linking it into the same probe; it reports +1200.0 cents, which the assertion
rejects.

An earlier window-only hypothesis was proposed and then refuted by measurement; that
is recorded rather than quietly dropped. Full account in
docs/implementation/PSOLA_OCTAVE_DOWN_TRANSPOSITION_2026-09-23.md. U16 scenario 1 is
now covered. Scenarios 2 and 3 remain: intelligibility and duration for short and
long vowels, breath/noise and pitch jumps need listening evidence, and truthful
failure with no hidden fallback path is a code question not yet examined here. Unit
acceptance is NOT claimed.

U16 scenarios 1 and 3 are now covered — scenario 1 by the transposition fix above,
scenario 3 by `1f062c92`, which pins that a required control either applies or
fails: driving formant, gender and growl through `UnitRendererDispatcher` with
`allowRawFallback` explicitly true returns "Selected renderer lacks required
controls: <control>", and vibrato is honoured by classic-psola without falling
back. Measured silent losses of a required control: 0.

U16 scenario 2 remains open and is a data problem, not a code one. "Short/long
vowels, breath/noise and pitch jumps retain intelligibility and correct duration"
is a listening claim. Duration is checkable numerically, but intelligibility is not
— and the corpus available for it is two diagnostic cases
(`tests/singing_quality/corpus/corpus.json`, id `u1-public-domain-diagnostic`)
against a plan that requires 60 phrases per language across three songs. The
blind-listening scoresheet is still empty.

U16 unit acceptance is therefore NOT claimed. The plan requires "fixed-corpus
numerical and listening results" to support the declared default and every exposed
mode.

Independent confirmation of the PSOLA fix on the down-transposition range. An 18
target sweep from cascade down to D4 (4 octaves below the source) on the fixed
renderer: **0 of 18 off by more than 50 cents**, worst case +2.6 cents. The same
sweep linked against the pre-fix `classic_psola.cpp` compiled from git: **6 of 18
off**, including 440.00 Hz measured for the 220.00 Hz target. This is a second
independent probe, not the probe the fix was developed against, and it was run
after the fact to check the fix did not merely satisfy its own harness.

Checked whether the PSOLA grain-read defect also affects `spectral_classic`, which
has the same looking shape at `sourcePosition = sourceCenter + relative *
sourcePerOutput`. A first sweep reported 15 of 18 down-transposition targets off by
more than 50 cents, which looked like a second instance.

It is not. The control settled it: the same fixture failed at **unison** (target =
source), measuring 469.05 Hz for 440.00 Hz, which no working renderer does. The
fixture was wrong, not the renderer — a 24000-frame take with `releaseStart` 19200
while the existing spectral test uses a 30000-frame take with `releaseStart` 25000
and `phaseReset = 0.0F`. Re-run against the configuration the existing passing test
uses, spectral holds pitch correctly: 440.88 Hz for a 440.00 Hz target (+3.5 cents),
392.73 for 392.00 (+3.2), 351.25 for 349.23 (+10.0), including down-transposition.

So there is no second defect here, and the first sweep is recorded as a bad
measurement rather than a finding. The lesson is the same one that caught the
analyzer octave error earlier: validate the instrument against a known-good case
before believing its failure. A unison target is that case for a pitch renderer, and
it should have been the first thing run.

2026-09-23 -- Repaired an intermittent segfault in `seam_phase11_tests` on the macOS
CI leg (`phase11-plugin-formats`, job `clap-editor (macos-latest)`) rather than
dismissing it as a flake. It surfaced on `1b58bb64`, a documentation-only commit, and
re-running workflow `35846674545` passed all five jobs -- intermittent, but not
innocent, because a docs-only commit cannot introduce a defect.

ThreadSanitizer gave the pair directly. The owner thread writes `controller_`
(`unique_ptr::reset` in `EditorRuntime::configureControllerCallbacks()`,
`editor_runtime_adapter.cpp:534`, reached from `replaceProject()` at
`editor_runtime_project.cpp:107` under `mutex_`), while the render worker reads it
(`unique_ptr::operator->` in `EditorRuntime::refreshRenderStatusView()`,
`editor_runtime_preview.cpp:48`, reached from `publishPreviewFromAuthoring()` at
`editor_runtime_adapter.cpp:274` via the completion callback registered at
`editor_runtime_adapter.cpp:173` and `notifyCompletion()`). That is a use-after-free
window, not a benign race: the worker can dereference the controller while the owner
destroys it.

The reason it survived review is a declaration comment asserting the function was
owner-thread only and "never takes this runtime's mutex". Both halves were false. The
declaration is corrected in the same commit. The fix takes `mutex_` in
`refreshRenderStatusView()`; `mutex_` is a `std::recursive_mutex`
(`editor_runtime.hpp:387`) and this is load-bearing, because `replaceProject()` holds
it across `rebuildController()` -> `configureControllerCallbacks()`, which calls
`refreshRenderStatusView()` at `editor_runtime_adapter.cpp:547`.

Verified both directions. Before: two consecutive aborts (exit 134) with the
`unique_ptr<...NativeEditorController>` race signature. After: 12/12 clean runs, then
12/12 clean again once the header was corrected, zero ThreadSanitizer reports, every
run identical at `Phase 11 tests PASS: notes=8 previewEnergy=3704 liveEnergy=14.0871`.
The exact CI configuration reproduces locally (Debug `seam_phase11_tests` 1/1 in
3.46 s); Release CTest 183/183 in 89.09 s; external-beta 189/189 in 25.66 s; source
closure, phase11 and phase12b contract gates pass.

Deadlock was checked rather than assumed. `notifyCompletion()`
(`render_coordinator.cpp:824`) invokes the callback at line 830 with no coordinator
lock held, so no lock-order inversion exists; nothing holds `EditorRuntime::mutex_`
while blocking on the worker (`prepareOfflineRender()` polls `progress()`/`latest()`
outside the lock); the two accessors taken under the lock (`progress()` at
`render_coordinator.cpp:371`, `acquireCurrent()` at 347) are `noexcept` and
non-blocking; and the only join path clears the completion callback before joining
without holding `mutex_`.

This is maintenance and advances no roadmap unit or Beta criterion. No renderer
revision is bumped because no rendered sample changes. Full account in
`docs/implementation/PREVIEW_STATUS_RACE_2026-09-23.md`. The separate one-off
`seam_phase12b_tests` teardown abort, which did not reproduce in 200 serial runs, is
unrelated to this race and remains tracked on its own.

2026-09-23 -- Repaired a verification gate that had silently stopped passing, and
closed the reason it could. Found by running every `scripts/verify_*.py` by hand and
sorting on exit status, because no part of the build or CI produces this signal.

`scripts/verify_clap_authoring_adapter.py` enforces that `EditorRuntime` owns the
shared `AuthoringRuntime` rather than private business state, and that no
`editor_runtime_*.cpp` exceeds 600 lines. It was failing on two files
(`editor_runtime_adapter.cpp` 684, `editor_runtime_project.cpp` 621). The ceiling had
been respected when the gate was written at `6e3be9ec` (395 and 193 lines) and was
lost as the adapter grew.

The real defect is that the gate was reachable only by hand: not referenced in
`CMakeLists.txt`, in any `.github/workflows/` file, or in any CTest test. Nothing ran
it, so nothing reported that it had stopped passing. Repairing the two files without
addressing that would leave the same trap for the next growth spurt.

Fixed by splitting the interchange surface into `editor_runtime_interchange.cpp`
along its own seam (`prepareInterchangeImport`, `acceptInterchangeImport`,
`exportInterchange`, the import/export/review handoff setters and the two request
entrypoints; they were already the only consumers of the interchange includes in
either file). Adapter 683 -> 592, project 620 -> 577, interchange 150. The gate is
now a CTest entry, `seam_clap_authoring_adapter_contract`.

The U32 token set in `verify_phase12b_contracts.py` is unchanged -- the same four
strings, checked at the path the code moved to -- so the constraint that the embedded
editor share the standalone interchange boundary is enforced rather than relaxed.

Verified: the size gate exits 0 where it exited 1; the new CTest entry passes 1/1;
phase11 and phase12b contracts pass; source closure passes; Release CTest 184/184
where it was 183, the increase being the gate itself; `seam_tests` 1042 passed, 0
failed; external-beta 189/189 in 24.69 s; CI Debug `seam_phase11_tests` 1/1 in
1.17 s; and 12/12 ThreadSanitizer runs clean, which confirms the preview-status race
repair survived the translation-unit move. Behaviour is unchanged: no logic, no
rendered output, no renderer revision, and no roadmap unit or Beta criterion
advances. Other un-wired gates were surveyed; only this one had stopped passing.
Full account in `docs/implementation/CLAP_ADAPTER_SIZE_GATE_2026-09-23.md`.

2026-09-23 — U30 legacy USTX import increment. The approved U30 plan requires
USTX 0.6–0.9 import, but the native codec admitted only 0.9. The pinned
OpenUtau `Ustx.Load` source (`8c0dc400`) shows that versions 0.6–0.8 retain
the musical timing/note schema used here; its post-0.6 migration changes
expression selectors, which SEAM does not map to its score. The bounded codec
now admits exactly 0.6, 0.7, 0.8 and 0.9, retains the source version in the
decoded document, and emits 0.9 on deterministic re-export. Pre-0.6 and future
versions still fail closed.

A version sweep imports the shared tempo/meter/track/note/lyric/pitch subset,
including the shorter pre-0.7 selector list, and checks 0.9 re-export and
re-import. Negative cases cover 0.5, 0.10 and 1.0. Local Release USTX and
interchange-service tests pass 2/2. This is a code coverage increment, not U30
acceptance: archival files from each older OpenUtau release and independent
application verification of the imported results remain to be collected.
`docs/formats/USTX_INTERCHANGE_V1.md` records the precise boundary.

2026-09-24 — U40 harmony workflow increment. A creator can now choose an
explicit chromatic interval or major/natural-minor scale and degree offset from
the macOS File menu, for the selected region or selected notes. Preparation
rejects out-of-scale and out-of-MIDI-range notes rather than silently
quantizing. A successful result creates a separately editable vocal track with
fresh track/region/note/lyric identities, preserved relative note timing and
shared-lyric melisma, copied singer/style routing, and no inherited absolute
performance curves. Acceptance is one stale-safe undoable command; undoing a
selected harmony re-anchors the runtime and native editor on an extant track.

Local Release and Debug tests cover cancellation-equivalent validation failures,
stale source rejection, melisma, undo/redo, independent selection, native
save/reopen, and a committed export with master plus lead and harmony stems.
The AppKit menu and controller compile in both configurations; the full local
Release build and CTest suite pass (185/185). This does **not** close U40 or Beta GO:
physical UI interaction/accessibility review, musical quality of the generated
harmony, scale/key UX beyond major and natural minor, and independent creator
qualification remain open. GitHub CI work is deferred at the user's request;
no CI configuration is changed in this increment.

2026-09-24 — U41 selected-singer character binding correction. The published
character cue/identity came from the selected vocal region, but its energy
envelope was computed from the entire master mix. An overlapping singer or
backing track could therefore move the selected singer's mouth. The render now
publishes the active region's own mono PCM by shared buffer, with its absolute
start frame, alongside the active track/region IDs. Character binding uses
that source instead of the master, refuses an audio span outside it, and drops
the previous phrase when the creator selects another track or region before a
fresh render. The native paint path now evaluates a new publication before
comparing generation counters; previously that comparison could prevent the
dock from ever binding a freshly completed render.

Focused tests verify nonzero and distinct harmony stems, nonzero-offset audio
frame indexing, stable lead mouth energy when a second singer changes the
master mix, selection-switch invalidation, and paint-triggered generation.
The full local Release build and CTest suite pass (185/185); focused Debug
character, dock and lifecycle tests pass (3/3).
This is not U41 acceptance: final authorized character artwork/provenance,
package-to-singer association and installed-app visual/accessibility review
remain open. GitHub CI work remains deferred.

2026-09-24 — U40 alternate-take comparison correction. The previous comparison
path applied an accepted-take command to the live project on Begin and on every
Swap, so merely auditioning a take changed the revision, dirty/autosave state,
undo history and potentially the saved project. Comparison now prepares the
exact merged selection with the existing stale-safe command on a private project
copy, renders that copy through a separate transient coordinator, and switches
transport audio at the same playhead. Swap, Keep Original and Cancel restore
canonical audio without a document edit. Accept Compared Take is enabled only
after the candidate audio has been published and then makes one undoable
canonical edit. Edits, selection changes and a new canonical render end a
transient audition; stale held decisions cannot be accepted. The character
performance model follows the audible audition instead of the canonical render.
AppKit comparison actions now surface errors rather than silently discarding
them; an asynchronous audition render failure enters the diagnostic registry
and leaves the canonical timeline in place. A regression checks unchanged
serialization/revision/dirty/undo state,
save-during-audition, playhead continuity, audition audio provenance, character
switching, cancellation, one-step acceptance/undo and stale edit rejection.
This is a U40 code increment, not U40 or Beta GO acceptance: installed-app
interaction/audio/accessibility review (including accept-click audio
continuity), measured musical quality and independent
creator qualification remain open. GitHub CI remains explicitly deferred and
no CI configuration is changed.

Verification for this correction: final Release and Debug macOS app/test
targets build. The final Release core test binary passes 1,071/1,071 cases.
The local Release CTest run passed 183/185 while two unrelated long-running
training/public-release suites timed out under concurrent app builds; those
two suites then passed 2/2 when rerun sequentially without build load. A
previous full Release run before the final error-display refinement passed
185/185. The Debug core suite passed before that final refinement; Debug final
builds pass, but a final Debug core rerun and physical app review are not claimed.

2026-09-24 — U40 decision feedback follow-up. The native performance menu had
discarded errors from take acceptance/rejection, per-channel acceptance,
selected-note acceptance and channel-limited generation. These actions now show
the returned reason through the existing AppKit error sheet, including stale
proposal/ownership conflicts; a blocked decision no longer looks like a silent
no-op. This improves U40 interaction feedback but does not qualify locked-channel
semantics across all supported hosts. Release and Debug macOS app targets build
with the new handlers. GitHub CI remains deferred.

2026-09-24 — U40 acceptance audio continuity. Accepting a ready comparison now
executes the canonical acceptance command while retaining the exact audition
render in transport and the character read model. The retained render is
released only when the current canonical render for the accepted project is
published; subsequent project/render changes discard that hold. Saving and
export still read the accepted canonical project. Release and Debug macOS app
targets build with this transition. No runtime audio-device journey or new test
was run for this increment; accept-click continuity still needs installed-app
and physical audio verification. GitHub CI remains deferred.

2026-09-24 — U32 native MIDI import review empty-state correction. A fresh
Release standalone build was exercised through File → “Open USTX or MIDI…”
with the repository's disposable original-melody MIDI fixture (SHA-256
`774231a09739d0f27ad39a4984a380bf0b5429ae5ac128c4259f2eb7a4afb79f`). The
pre-fix native review allocated a large empty striped issues table even for a
loss-free import. The AppKit review now omits that table when there are no
issues and displays a compact explicit empty state; issue-bearing reports keep
their selectable, scrollable table and full-detail pane. Review summaries now
use singular nouns for one track/region/note/loss/warning.

The rebuilt app's accessibility tree and visual review showed “1 track, 1
vocal region, 80 notes; 0 losses; 0 warnings,” the source hash, a clear
overwrite/undo warning, and an explicit unresolved-singer disclosure stating
that no singer is substituted automatically. Accepting the import yielded an
80-note native arrangement. This proves the macOS single-track, loss-free MIDI
review-and-accept path only: the fixture has no usable singer, so no rendering
or vocal quality is claimed. Multi-track MIDI, issue-bearing native-dialog
visual review, real DAW round-trip, saved-project reopening, and the complete
U32 acceptance gate remain open. Conversion-review model tests pass in Debug,
Release and Sanitizer (3/3); `seam_u2_tests` passes in all three configurations
(3/3). This is a U32 implementation increment, not unit or Beta GO acceptance.
GitHub CI remains deferred and no CI configuration is changed.

2026-09-24 — U32 populated conversion-report sizing. A 54-byte disposable MIDI
fixture with one valid lyric/note and one unsupported meta-event exercised the
loss-bearing path in the rebuilt Release app. The one-loss report previously
reserved its maximum 230-point table height, leaving most of the striped table
empty. The table now sizes to visible issue rows plus the header, with a 46-point
minimum and the existing 230-point cap; longer reports remain scrollable. The
native screenshot and accessibility tree show the singular “1 loss” summary,
the row's tick:480 location and full message, source path/hash, unresolved
singer warning, and the separate full-detail pane, without the unused rows.
The loss-free case was also rechecked: it has no table and retains the compact
explicit empty state. Release, Debug and Sanitizer `seam_u2_tests` pass (3/3),
and the Release conversion-review model test passes. This closes the two native
dialog sizing defects found in this pass only; multi-track native GUI import,
save/discard/cancel journey coverage beyond the existing tests, DAW GUI
multi-track acceptance and complete U32/Beta GO remain open. GitHub CI stays
deferred and `.github` remains unchanged.

2026-09-24 — U40 named harmony-scale expansion. The native Create Harmony Track
workflow now exposes major, natural minor, harmonic minor, melodic minor, and
the Dorian, Phrygian, Lydian, Mixolydian and Locrian modes, alongside chromatic
semitone harmony. The standalone controller maps each named choice to explicit
pitch-class intervals; scale-degree offsets remain diatonic, chromatic offsets
remain semitones, and source notes outside a selected diatonic scale still fail
without quantization. Release `seam_harmony_workflow_tests` and `seam_u2_tests`
pass (2/2); the build compiled both the Objective-C++ menu and controller, and
`git diff --check` passes. The algorithm remains deterministic interval
transposition: this does not add voice-leading optimization, chord progression
analysis, or independent musical/creator qualification. GitHub CI is deferred
and `.github` remains unchanged.

2026-09-24 — U32 native multi-track import smoke test. A disposable Type-1
fixture with Conductor, Lead and Harmony source tracks (one lyric/note each on
the two vocal tracks) was admitted by the Release standalone review as “2
tracks, 2 vocal regions, 2 notes,” with the expected velocity-conversion loss
and explicit unresolved-singer warning; accepting the disclosed loss completed
the normal import path. The imported `.seam` produced by the same codec/service
was independently inspected and contains named Lead and Harmony tracks, each
with one lyric/note and the expected offset. The installed editor screenshot
after acceptance displayed only one piano-roll note (“la”). The missing-singer
recovery support was active and suppressed the arrangement selector; the
accessibility tree therefore exposed the selected note but not the other
track's note or a track selector. This is not multi-track native-editor
acceptance and does not prove data loss: it confirms bounded conversion and
review/accept, while simultaneous visibility and selectable track navigation
remain unverified until the normal arrangement surface is available.

2026-09-24 — U32 track selection synchronization and support-panel navigation.
The controller's track selection updated its local arrangement/piano-roll state
but did not update `AuthoringSession`'s selected track/region or the
`AuthoringRuntime` selection used by singer resolution and rendering. The
recovery-support surface also replaces the arrangement dock, removing the
visible track rows. Track changes now notify the host; the standalone host
updates its runtime and cached selection before the controller rebuilds its
piano-roll view. The support panel now has visible PREV/NEXT controls and an
announced selected-track label; mouse, accessibility activation, and
Option+Left/Right all use the same synchronized, wrapping navigation path. The
regression tests verify Lead/Harmony navigation through mouse, accessibility,
and keyboard; host/session/runtime agreement; and that selecting does not dirty
the document. A separate Type-1 MIDI lifecycle case prepares and accepts
Conductor/Lead/Harmony source tracks, then navigates to Harmony through the
dock's accessible NEXT control and verifies its pitch-67 note is active.
The 960x600 raster fixture shows the enabled controls, selected-track label and
report cards without overlap; its screenshot was reviewed locally. Release and
Debug `seam_u2_tests` pass (64/64 each), and Release and Debug `seam_tests`
pass (1,090/1,090 each). This verifies the imported-session/controller path and
local raster, not an installed-app screenshot, actual vocal rendering, DAW
round-trip, or U32/Beta GO acceptance. GitHub CI remains deferred and `.github`
is unchanged.

2026-09-24 — U6 installed-singer melody export check. The end-to-end song journey
already required finite, non-silent PCM; it now also pitch-analyzes the final
sustained E4 vowel from the exported master WAV and checks it against the
authored MIDI-64 target (12 Hz tolerance). This crosses the installed procedural
singer, lyric/pronunciation, compiled performance, renderer and WAV-export path;
it is stronger than checking the score evaluator alone, but is not a full melody,
intonation, consonant-intelligibility or listening qualification. The focused
journey passes in Debug (83.07 s) and Release (17.03 s), and `git diff --check`
passes. U6/U19/U20 and Beta GO remain incomplete. GitHub CI remains deferred;
no CI configuration was changed.

2026-09-24 — U20 English CV–VC articulated-stream regression. A resolved
one-note English `s ae1 t` phrase now exercises the language resolver, shared
procedural timing, recipe-bound /s/ onset, voiced /ae1/ nucleus and /t/ coda,
then renders the phrase in whole and split/checkpoint windows. A vowel-only
control verifies that both consonant-owned spans add actual PCM while the
contextually articulated vowel retains the authored MIDI-60 F0 (12 Hz tolerance).
The test passes as part of `seam_voice_design_tests` in Debug (23.37 s) and
Release (10.28 s); `git diff --check` passes. This is deterministic engineering
coverage for one explicit ARPAbet fixture, not dictionary coverage, an unseen
phrase, native-speaker intelligibility, listening acceptance or U20/Beta GO.
GitHub CI remains deferred; no CI configuration was changed.

2026-09-24 — U27 English spelling-estimate increment. The bundled English
fallback now recognizes common vowel spellings (`ai/ay`, `ee/ea`, `oa/oo`,
`oi/oy`, `ow/ou`), a narrow final-e pattern, and soft `c/g` before `e/i/y`.
Regression cases cover `train`, `boat`, `make`, `city` and `gem`. These remain
explicit `EstimatedPronunciation` outputs, and the English resource fingerprint
is source-derived so the behavior change invalidates prior resource identities.
The focused `seam_language_phonemizer_tests` passes in Debug. This improves a
small set of spelling estimates; it does not provide an English dictionary,
stress/syllable qualification, dialect handling, or U27 completion. GitHub CI
remains deferred and `.github` is unchanged.

2026-09-24 — U27 pinned English dictionary integration. Added the unmodified
135,166-line CMU Pronouncing Dictionary at immutable upstream revision
`74790861f652b15e4ac49015a90074ad62a27690` (archive SHA-256
`741c592660bbf10ab93fe3d5aa709b73a3b5ba91e3291a066715440f4137a58d`, data SHA-256
`81917843c7f44ce2b094ac63873c2c7a4cf802040792c455ba3ca406891c3d22`). The
BSD-2-Clause license and attribution are recorded in the third-party manifest,
SBOM, repository notice, and distribution Notices payload. CMake embeds the
bounded 32 KiB raw-literal chunks into the phonemizer library; the dictionary
content hash participates in the English resource identity. Resolver lookup
selects the first upstream variant and preserves CMU stress; explicit note
hints remain highest priority and out-of-lexicon spelling remains marked
estimated. Debug `seam_language_phonemizer_tests`
passes (24 cases), and the local license/provenance audit passes. The resource
has not been reviewed for singing, every dialect, or proper-name accuracy, and
native-speaker song qualification/U27/Beta GO remain open. GitHub CI is deferred;
`.github` is unchanged.

2026-09-24 — U28 Korean ㄼ boundary-rule increment. The built-in Hangul path now
distinguishes 밟- before a following consonant (밟고 [밥꼬], 밟는 [밤는]) from
vowel liaison (밟아 [발바]), keeps 넓- inflections on the ㄹ reading (넓고
[널꼬], 넓다 [널따]), and covers the listed 넓적-/넓죽- compound pattern.
Regressions assert exact phone sequences and the 밟고 behavior across two note
owners; the resolver leaves the source region unchanged. The focused language
phonemizer suite passes in Debug and Release. This is a small lexical/context
rule slice, not a Korean dictionary, dialect/lexical coverage, native-speaker
review, singing qualification, or U28/Beta GO. GitHub CI remains deferred and
`.github` is unchanged.

2026-09-24 — U28 Korean ㄾ coda/tensing increment. The built-in Hangul boundary
logic now reduces ㄾ to [ㄹ] before a consonant and tensifies following ㄱ/ㄷ/ㅅ/ㅈ
for the represented stem-ending pattern. Exact regressions cover 핥고 [할꼬],
핥다 [할따], 핥소 [할쏘], 핥지 [할찌], and the vowel-boundary contrast 핥아 [할타].
The suite also verifies the 핥고 coda/onset assignment across two note owners.
This follows NIKL Standard Pronunciation Rules 10 and 25. The change remains a
surface-rule implementation, not a Korean morphology engine or native-speaker
qualification. GitHub CI remains deferred; `.github` is unchanged.

2026-09-24 — U33 MIDI channel-level volume/expression increment. MIDI 1.0 CC7
and CC11 now maintain separate normalized per-channel levels, update current
and later voices on that channel, and multiply independently from targeted CLAP
per-note volume/expression; CC121 restores channel defaults without overwriting
note-level controls. Focused regressions measure half-level output, prove
wrong-channel isolation and event-order independence, and cover active voices
and reset behavior. `seam_clap_live_events_tests` passes Debug, Release and
Sanitizer. This does not
qualify CC7 gain calibration or delivery/mapping in installed hosts; U33/Beta GO
remain open.
GitHub CI remains deferred; `.github` is unchanged.

2026-09-25 — Portable WAV receipt oracle made path-exact. The regression now
maps each of the two allowed `media/<sha256>.wav` paths to its expected content
hash and frame count, rejects every unexpected packaged media path, and requires
each expected path exactly once; substring classification no longer treats any
other media path as the second source. The Release `seam_export_tests` target
builds, focused CTest passes 1/1 (3.92 seconds), and `git diff --check` passes.
Test source SHA-256: `383e401ea4fb78ba87e01e03842192a2ccbd4dbf08786d8b75e2ce7d86a1ec12`.
This oracle tightening does not add product-scope acceptance. GitHub CI remains
deferred and `.github` is unchanged.

2026-09-25 — Relocated ProjectCopy reopen/render regression. The standalone
lifecycle test imports and relinks backing WAV, saves the project, moves the
project directory with its `.media` payload, reopens from the moved path, and
waits for a Ready render at the reopened document revision. It asserts the
saved relative path and ownership survive, the relocated bytes match the
persisted hash, and the render has no missing-media diagnostic for the track.
The Release `seam_u2_tests` target builds and passes 1/1 CTest (1.12 seconds);
`git diff --check` passes. Test source SHA-256:
`e8c4cd3f003978731caba19db5bf3f366cc47b36b6ad323235cdaa43e9b4afde`.
This is code-owned relocation evidence, not an unaided creator session, human
audio review, whole-U6 acceptance, or Beta GO. GitHub CI is deferred and
`.github` is unchanged.

2026-09-24 — U35 training-environment identity increment. Acoustic training now
captures the active Python executable/runtime identity and content hashes for
each installed distribution file into checkpoint metadata. Exact resume compares
that snapshot, refusing a changed package environment instead of silently
continuing under different dependencies. Capture is bounded by distribution,
file-count and byte limits; the actual model-training venv was inventoried as
63 distributions / 31,156 files / 863,940,664 bytes. All 455 voice-model
training tests pass (1 skipped) under that venv. The build directory had been
configured to use an unrelated Python 3.14 interpreter without SciPy; after
correcting only its local `SEAM_VOICE_TRAINING_PYTHON` cache entry, the registered
`seam_voice_model_training_tests` CTest passes (51.15 s). No repository CMake or
GitHub CI configuration was changed. This records the environment used but does
not produce a hash-locked rebuild, prove lawful corpus admission, or qualify a
trained voice. U35/U36/Beta GO remain open; GitHub CI is deferred and `.github`
is unchanged.

2026-09-24 — U35 derived-clip dataset join. Schema-2 dataset configurations can
now bind segment configurations and existing clip artifacts. At every assembly
or training refresh, the assembler freshly re-admits parent rights, regenerates
the exact crop, verifies the existing PCM and segment record, requires the
crop's parent label/score to match the current reviewed parent, and requires
the child audio/label/score to appear in the separately signed label config.
Child sources inherit singer identity and song/session/lineage and are included
in the deterministic pre-augmentation split; their segment and admission hashes
participate in dataset identity. Reassembly produces the same dataset hash, and
tampered parent supervision or segment records reject without publishing a
snapshot. `seam_voice_model_training_tests` passes (456 tests, 1 skipped; 53.47 s).
The join currently supports hop-aligned derived crops; off-grid crops requiring
fresh pitch re-extraction still need assembler integration. It does not create
or independently approve rights, labels, corpus partitions, or a singer. U35/U36/
Beta GO remain open; GitHub CI is deferred and `.github` is unchanged.

2026-09-24 — U35 off-grid derived-clip revalidation. Schema-3 dataset
configurations now optionally pin an executable fresh-pitch extractor by exact
file hash. When a crop start is off the parent label's analysis hop, assembly
re-runs pitch extraction during every rights/data refresh, verifies the extractor
did not change around execution, and includes its digest in the child binding;
aligned crops do not invoke the executable. A signed-label integration fixture
exercises an actual one-sample-offset crop, captured extractor tampering, and
fresh dataset assembly; missing or modified extractors fail before snapshot
publication. `seam_voice_model_training_tests` passes (457 tests, 1 skipped;
56.79 s). The extractor is bounded, not sandboxed, and still requires an
operator-selected trusted binary. Lawful corpus and voice-quality acceptance
remain open; GitHub CI is deferred and `.github` is unchanged.

2026-09-24 — U35 hash-locked training-check environment. Added a composite
macOS Apple-Silicon/Python-3.11 requirements input and a 63-package lock with
SHA-256 hashes for binary artifacts, spanning acoustic/vocoder training and
ONNX export checks. Rebuilt a new isolated venv using `uv pip sync --require-hashes
--strict`; `pip check` reported no broken requirements. The venv fingerprint
captured 63 distributions, 31,209 files and 863,305,636 bytes. The full voice
model training suite passed (457 tests, 1 skipped), and the real pinned DiffSinger
smoke check passed at revision `336cf01b57f2ad44c6b37a79cf33993043291759`.
The lock is not portable to Intel macOS, Windows or Linux; trusted upstream code
still executes without sandboxing. This establishes repeatable dependency
resolution for this target, not lawful corpus admission, a qualified singer or
Beta GO. GitHub CI is deferred and `.github` is unchanged.

2026-09-24 — U35 vocoder resume environment identity. Complete and partial
vocoder checkpoints now include the same bounded fingerprint of the active
Python runtime and installed distribution contents used by acoustic training.
The existing exact-resume metadata comparison therefore rejects package-file
drift before restoring either checkpoint type. Focused regressions cover both
complete and partial resume mismatch. The 457-test voice-model suite passes
(1 skipped). An actual two-epoch `train_vocoder` CLI diagnostic then completed
continuous and separate-process resume paths; model/optimizer/RNG state and
held-out WAV bytes match exactly. Both retained receipts carry the same
environment digest `0a279cc99a7743c168b079fdb3d5eaf53e72ca079d8aa19f0eac30244a129726`
(63 distributions, 31,209 files, 863,305,636 bytes). The fixture's pitch was
unresolved and spectral distance was 3.970544, as expected for untrained
synthetic data; this is not singer qualification. Check artifacts remain local
under `build/neural-runtime/vocoder-env-identity-check-20260924/` (about 1.5 GiB).
This binds software identity on the macOS Apple-Silicon/Python-3.11 locked target;
it does not promise bitwise reproduction across hardware or qualify training
data/model quality. GitHub CI is deferred and `.github` is unchanged.

2026-09-24 — U37 full-graph seeded replay check. The merged acoustic ONNX
diagnostic now executes each dynamic-shape case in a fresh session and creates a
second session for the same 16-frame/1-step request, requiring exact mel-byte
replay. `check_diffsinger_model ... --check-onnx` passed at the pinned DiffSinger
revision: replay maximum absolute difference was 0; dynamic execution passed at
frame/step pairs 16/1, 3/4 and 23/8, and offline graph inspection passed.
This verifies deterministic seeded sampling across fresh ONNX Runtime sessions
for the synthetic architecture fixture, matching the worker's one-request
session lifecycle. PyTorch-vs-ONNX stochastic stream parity, native installed
worker parity, a vocoder-integrated singer and musical qualification remain open.
GitHub CI is deferred and `.github` is unchanged.

2026-09-25 — Relocated-media render assertion strengthened. The reopened
standalone regression now also captures the Ready render, mutes the relocated
ProjectCopy through the normal application command, waits for the new project
revision to render, and asserts the PCM changes. This verifies the reopened
backing media contributes to output rather than merely existing beside a
successful vocal render. Release `seam_u2_tests` passes 1/1 (7.94 seconds), and
`git diff --check` passes. Final test source SHA-256:
`7fd2737e1902148e90db0f85d0cd9f4d40a42eed370014775f8893a44cd0d6b2`.
The earlier 1.12-second result corresponds to the preceding test revision;
this run supersedes it. GitHub CI remains deferred and `.github` is unchanged.

2026-09-24 — U37 native fresh-session replay. The C++ ONNX Runtime probe now
recreates a session from the exact captured graph bytes and replays the same
request, rejecting any nonfinite or non-identical seeded mel. The full
`check_diffsinger_model --check-onnx --native-probe build/release/seam_onnx_runtime_probe`
workflow passed: native replay error was exactly 0, the graph digest was checked,
native structural inspection passed, and the wrong-digest negative case rejected.
Python and native ONNX Runtime both passed fresh-session replay for the synthetic
DiffSinger fixture. One repeated invocation emitted passing JSON but then aborted
at teardown (exit 134); the next full unpiped invocation exited 0 with the same
result. This remains an intermittent ONNX Runtime shutdown reliability issue, not
a passing result to ignore. This does not establish Torch random-stream parity,
installed production worker/host acceptance, a real singer or musical quality.
GitHub CI is deferred and `.github` is unchanged.

2026-09-24 — U34 live in-block seconds-timeline mapping. Realtime score-preview
playback now retains bounded borrowed references to transport events from the
current CLAP process block and remaps each frame from the latest event anchor,
instead of applying only the block-start transport to the entire block. The
audio callback path adds no allocation; malformed transport updates make the
score preview inaudible from that sample until a later valid transport anchor,
while live-input voice rendering continues. Added a frame-exact mapper regression
for samples before, at and after an in-block seconds correction. Release
`seam_clap_offline_host_tests`, `seam_host_timeline_capture_tests` and
`seam_host_transport_publication_tests` pass (3/3); the plugin/host targets build
and `git diff --check` passes. The loaded-host fixture verifies fail-closed
offline invalidation but does not assert realtime in-block audio against a
commercial DAW; beat-only event continuity and tempo-ramp playback are not
qualified. GitHub CI remains deferred and `.github` is unchanged.

2026-09-24 — U34 event-overflow fail-closed repair. Independent loaded-plugin
review found that the 1,024-event guard discarded the entire list and continued
with block-start score mapping, so a transport seek in event 1,025 could escape
the capture and leave stale score audio audible. Overflow now publishes an
incomplete-history marker, mutes realtime score mapping for the block, and
returns `CLAP_PROCESS_ERROR` in offline mode while invalidating later offline
blocks. Extracted the fixed-state `HostTimelineBlockCursor` so exact sample
anchors, malformed-history silence and valid-anchor recovery have direct tests.
The loaded CLAP harness exercises 1,024 events with a transport update, exactly
1,024 ordinary events, and 1,025 events with a seek; offline output clears at
the boundary and remains rejected after owner-thread drain until Final rebind.
Release CLAP plugin/host targets build and the offline-host, timeline-capture,
and transport-publication CTests pass 3/3; `git diff --check` passes. This
closes the reproduced overflow defect only. A realtime in-block audio oracle in
the repository, commercial-DAW qualification, beat-only continuity, ramp
support, and U34/U13/Beta GO acceptance remain open. GitHub CI stays deferred;
`.github` is unchanged.

2026-09-24 — U34 overflow recovery-demand correction. Independent exact-snapshot
review found that the overflow-muted realtime block could return `CLAP_PROCESS_SLEEP`
while the host transport was playing; a conforming host may then stop calling
`process()` before the next valid transport block. The plugin now returns
`CLAP_PROCESS_CONTINUE` for that playing overflow block, without restoring stale
score audio or depending on a `request_process` wakeup. The loaded-plugin test
drains the owner callback and calls the next event-free block only after observing
CONTINUE, matching CLAP's host contract; it verifies the next block restores
audible score output. The Release offline-host CTest passes with this regression.
Commercial-DAW behavior and U34/Beta GO remain unqualified. GitHub CI and
`.github` remain untouched.

2026-09-24 — U37 exported-worker WAV artifact validation. The optional captured-
model replay previously treated the presence of any `.wav` path as successful
native authoring export. It now validates each regular WAV's RIFF/chunk sizes,
encoding, channel count, 48 kHz clock, block alignment, frame payload, and PCM
content; float PCM must be finite and normalized, and the exported set must
contain at least one non-silent file. This prevents truncated, malformed,
wrong-rate, or silent artifacts from satisfying the end-to-end replay gate.
Focused Python tests pass (6/6), including PCM24 and float32 acceptance and
silent/truncated/wrong-rate/nonfinite/out-of-range rejection. The expensive
captured-model/native-render invocation then exited 0 from the pinned local
DiffSinger and SingingVocoders checkouts. Its production-render report contained
two 48 kHz non-silent WAVs totaling 96,000 frames, both accepted by the new
validator; request/worker/bundle/project hashes and inference-step binding also
matched. This end-to-end run uses generated oscillator data and a synthetic
vocoder whose pitch-following diagnostic fails, so it verifies the gate and
execution path only—not a qualified singer or musical quality. GitHub CI is
deferred and `.github` remains unchanged.

2026-09-24 — U20 Japanese /f/ inventory increment. The authored Japanese song
now includes ふ, whose resolver output requires an explicit `/f/` onset; the
original singer recipe and package manifest declare that phone's own bounded
frication source. The checked-in recipe was regenerated through the production
encoder, and the documented fixture-regeneration command now performs an
explicit atomic replacement only when its output path matches the fixture. The
full installed-singer authoring, render, tune, save/reopen and export journey
passes 5/5 in the local Debug build. This is engineering integration for one
Japanese onset, not acoustic/listening qualification or broad phoneme coverage.
GitHub CI is deferred and `.github` remains unchanged.

2026-09-24 — U20 Japanese voiced/unvoiced fricative contrast increment. The
installed original-singer song now places ふ and ゔ consecutively. The recipe
binds unvoiced `/f/` noise separately from `/v/` noise-plus-voicing and its own
same-phone resonance pose; the package declares both phones. The journey asserts
that Japanese resolution marks `/f/` unvoiced and `/v/` voiced, then carries the
phrase through installation, rendering, tuning, save/reopen and export. The full
journey passes 5/5 in the local Debug build, including a normal run against the
regenerated canonical recipe. This remains a single contrast with screening
parameters, not listening or phonetic qualification, broad inventory completion,
or U20/Beta GO. GitHub CI is deferred and `.github` remains unchanged.

2026-09-24 — U21 Studio campaign resume after restart. The producer UI previously
enabled campaign resume only when the current controller held an in-memory path,
so a valid persisted campaign became inaccessible after closing Studio. The
campaign action now remains available as “Open / resume”; selecting a campaign
JSON reads bounded retained bytes, hashes those exact bytes, revalidates the
producer epoch/generation after the modal, and passes the immutable identity to
the shared campaign-advance service. A fresh-controller regression reopens the
producer, adopts the selected campaign, verifies its retained preflight, and
completes both batches as unapproved MarkerReview takes. The Release Studio and
test targets build, focused Studio campaign and export CTests pass, and
`git diff --check` passes. This verifies the controller/service resume path and
control availability, not a physical AppKit picker interaction or U21/Beta GO.
GitHub CI remains deferred and `.github` is unchanged.

2026-09-24 — U20 matched-note Japanese fricative PCM regression. Added a
production `ArticulatedStream` test that resolves `ふ` and `ゔ`, renders both at
the same MIDI pitch, note duration, `/u/` nucleus and shared frication-noise
configuration, and verifies the recipe-bound `/f/` and voiced `/v/` routes
produce materially different PCM while preserving the authored F0. The focused
voice-design test binary passes 46/46 in Release, and `git diff --check` passes.
This shows the distinction reaches rendered samples; it is not a phonetic
intelligibility or listening qualification. U20/Beta GO and acoustic review
remain open. GitHub CI is deferred and `.github` remains unchanged.

2026-09-24 — U22 five-vowel source-filter starter patch. Studio's New Voice
action previously created only one `/a/` pose; it now uses the Designer session's
shared starter factory to create editable `/a i u e o/` neutral poses with
bounded formant defaults. The accessibility description and status explicitly
state that this is an unqualified starter with no consonant inventory. The
Designer regression constructs all five tracts and asynchronously auditions
each; the focused Release Designer CTest passes, and the native macOS Studio
target builds. This enables an immediate five-vowel patch workflow, not a
complete language inventory, qualified singer, recording route or U22/Beta GO.
GitHub CI remains deferred and `.github` is unchanged.

2026-09-24 — U22 Japanese starter now shares the installed-song screening
recipe. Studio's New Voice action and the original-singer journey call the same
`makeJapaneseStarterRecipe` factory, so a new draft immediately has the partial
phone models exercised by the 40.5-second Japanese song, rather than only five
vowels. Studio continues to label it partial and unqualified; the defaults are
not phonetic qualification and the recipe does not cover all Japanese phones.
The Designer regression validates and constructs all 13 declared resonance
poses, checks the consonant model families, and auditions the five vowels. The
Release Designer CTest and full installed-singer song journey CTest both pass;
the native macOS Studio target builds and `git diff --check` passes. This proves
shared recipe and renderability consistency, not listening quality, a complete
Japanese inventory, voice creation without recordings at product quality, or
U22/Beta GO. GitHub CI remains deferred and `.github` is unchanged.

2026-09-24 — U22 built-in Japanese symbol-set starter and compiler coverage.
Expanded `makeJapaneseStarterRecipe` from the demo song's subset to the full
symbol families currently emitted by SEAM's built-in Japanese phonemizer:
vowels, oral and moraic nasals, voiced/voiceless stops and fricatives, both
unvoiced affricates plus the voiced affricate, approximants, contracted
palatalized phones, closure events and breath. Studio now describes the action
as covering the built-in adapter's symbol set while keeping the draft marked
screening/unqualified. A production-path regression resolves a 60-plus-token
phone hint containing every supported family, compiles its score and verifies
the recipe through `ArticulationPlan` and `ArticulatedStream`; the Designer
regression validates/constructs each declared pose and auditions all five
vowels. The canonical installed-song fixture was regenerated using the
production encoder after the shared factory changed. Release Designer, voice
design and full installed-singer journey CTests pass (3/3); the native macOS
Studio target builds and `git diff --check` passes. This is symbol routing and
screening-default coverage, not correct phonetic realization, qualified
pronunciation, expressive-quality evidence, or U22/Beta GO. GitHub CI remains
deferred and `.github` is unchanged.

2026-09-24 — U22 Japanese inventory/source-of-truth regression. Added a public,
sorted unique symbol inventory generated from the built-in kana table plus its
explicit phone-hint event symbols; strict phone-hint parsing now consumes that
same inventory rather than maintaining a second private set. The procedural
coverage regression enumerates the exported inventory, builds a score context
for every symbol, and verifies all symbols are admitted by both the production
articulation planner and stream. This means a future mora-table symbol addition
will fail the voice-source coverage check until the starter can render it. The
focused phonemizer, voice-design, Designer and installed-singer journey CTests
pass (4/4); the native macOS Studio target builds and `git diff --check` passes.
Studio still lacks editable controls for affricate, approximant and breath
source parameters; completing that is a remaining U22 authoring gap. No
pronunciation-quality or Beta GO claim is made. GitHub CI remains deferred and
`.github` is unchanged.

2026-09-24 — U22 Studio articulation-source parameter controls. The Designer
control list now exposes editable burst/tail spectra and timing for unvoiced
affricates; burst/tail spectra, closure voicing/low-pass and tail voicing for
voiced affricates; transition duration for approximants; and center, bandwidth
and gain for breath sources. Keyboard/drag adjustments and accessible numeric
edits route through the existing revision-guarded canonical recipe edit, undo
and dirty-state path. Control values, descriptions and accepted-range text are
generated for the added families, and source selection bounds were tightened so
new control rows cannot be mistaken for a frication or plosive. The Release
macOS Studio target builds; Designer, voice-design and installed-singer journey
CTest pass (3/3), and `git diff --check` passes. Dedicated source-specific
audition for affricates, approximants and breaths remains open, so editing these
values is not yet a complete listen-and-adjust loop or acoustic qualification.
GitHub CI remains deferred and `.github` is unchanged.

2026-09-24 — U22 Designer phrase audition coverage expanded. Added a one-second
CV preview that resolves recipe-bound phones and the selected vowel style,
compiles explicit phone timing, and renders through the same
`ArticulatedStream` path used by singing. It now covers nasal, palatalized,
unvoiced/voiced affricate, approximant, breath and closure gestures; declared
closure is intentionally silent. The background Designer session tags results
with epoch/revision/pose/pitch and phone identity, so changed selections or
recipes cannot publish stale audio. Studio keyboard and accessible render/play
actions expose previews for the editable nasal, palatalized, affricate,
approximant and breath controls. The regression renders each listed gesture
family from the Japanese starter, checks finite bounded and deterministic PCM,
rejects unsupported phones and invalid pitch/pose, checks cancellation, and
verifies session publication/invalidation. The native macOS Studio and focused
Designer test targets build; the Designer CTest passes, and `git diff --check`
passes. This closes the mechanical listen-and-adjust path for these source
families, not perceptual pronunciation qualification or U22 / Beta GO. GitHub
CI remains deferred and `.github` is unchanged.

2026-09-24 — R9 flatness/level component ablation result reconciled. The
previously staged plan was already executed after its recorded Developer-2
clearance; both isolated e9 arms and a four-arm evaluation exist in the
external corpus workspace. Independent local receipt loading verified the two
new checkpoints, exact config/parent/dataset bindings, complete 267-update
coverage and zero source/timestep/noise mismatches against base. The frozen
primary errors were base 2.8325, combined 3.7984, flatness-only 3.9403 and
level-only 3.8243 nats; both isolated arms also fail the frozen UV RMS-range
and voiced-flatness guardrails. The declared rule therefore stops this
auxiliary family at the tested setting; no weight/layer sweep is opened. The
durable receipt and limits are in
`FLATNESS_COMPONENT_ABLATION_RESULTS_2026-09-22.md`. This is not singer
qualification; listening remains NOT_REVIEWED, `releaseEligible=false`, and
`singerQualified=false`. Developer-2 subsequently audited the saved artifacts
and approved the frozen STOP decision and closure at this tested setting only;
this does not authorize relaunch, retaining either term, singer qualification
or release. The 267-step match means source/draw/coverage parity, not 267
empirically equivalent optimizer updates (the protocol's empirical optimizer
equivalence is 24 updates at each equal-coefficient corner). Saved evaluation
receipts also lack evaluator-source, pitch-executable and complete runtime
identity hashes; add those before any future experiment. See
`FLATNESS_COMPONENT_ABLATION_RESULTS_2026-09-22.md` for exact audit scope and
limitations. Follow-up evaluator work adds schema-v3 provenance fields for the
evaluator source, provenance-helper source, pitch executable and Python/numpy/
scipy/ONNX Runtime/platform versions. Two dependency-light regression tests,
Python byte-compilation and `git diff --check` pass; no evaluation/training was
run, and this does not alter the frozen e9 receipt. GitHub CI remains deferred
and `.github` is unchanged.

2026-09-25 — U6 installed-singer delivery regression extended to master/stems.
The 40.5-second authored Japanese development song now exports with both
`includeMaster` and `includeStems`; its ordinary standalone export journey
requires a committed export state, the exact authored track-ID stem path and one
durable receipt entry whose SHA-256 matches both the returned export receipt
and recomputed file bytes. Decoded master and stem must each be 48 kHz stereo;
both cover the full 40.5-second score with at most 250 ms natural tail, and
their deterministic PCM is identical because the fixture has one vocal track
and no backing media (both project conditions are asserted). The durable receipt's
frame/channel metadata is also compared to the decoded stem. Both are finite and
non-silent. The initial strict run
also corrected a stale 39.5-second comment: the authored ticks at 120 BPM sum
to 40.5 seconds. After correction, the Release
`seam_original_singer_song_journey_tests` target builds and the full CTest
passes (1/1, 19.28 seconds). Developer-2 independently approved the focused
source review at SHA-256
`ee9cec1135ebe81332f1a0d7cb4bf6f828f5575a1be197dbc646b71e115215c3`; that
 review did not rerun the test. This verifies a code-owned development fixture
 and export route, not whole U6 acceptance, installed-app interaction, qualified
 singer, perceptual song acceptance, or Beta GO. GitHub CI remains deferred and
 `.github` is unchanged.

2026-09-25 — U6 score-rest regression follow-up, local only; GitHub CI remains
deferred. Review found that the installed-singer test's song data did not contain
the rest described in its comments. The fixture now contains an actual 0.5-second
score gap between its two sections, bringing the authored duration from 40.5 to
41 seconds. The end-to-end export check locates that interval through the project
tempo map and asserts that the center 100 ms in decoded master PCM is near digital
silence (`peak < 1e-4`), while retaining the full-song master/stem coverage and
receipt assertions. The Release target builds and the focused CTest passes 1/1
in 16.80 seconds; `git diff --check` passes. Source SHA-256:
`89b699e0801485db8cbce7ed69aa9f5e4788f4612ba18a6248e13aa262eb350b`.
This regression checks score-gap propagation for the code-owned synthetic
development fixture; it does not establish singer quality, human usability,
whole U6 acceptance, or Beta GO. The previous independent review applied to the
 prior source hash and does not cover this rest change. `.github` was not modified.

2026-09-25 — Portable WAV backing-media project export, local only; GitHub CI
remains deferred. “Include Project” now packages referenced WAV media by content
hash as `media/<sha256>.wav`, rewrites the exported project's audio tracks to
relative `ProjectCopy` paths, and retains the live project unchanged. Relative
source paths resolve only with the explicit project-directory setting. Packaging
validates WAV content hash plus source rate/channel/frame metadata, deduplicates
identical files, caps each WAV at the existing 512 MiB decoder limit and all
packaged media at 2 GiB, then stages and verifies the captured bytes before any
render begins. Rendering and the exported project consume that staged snapshot;
a mutation before capture rejects without replacing an existing export, while a
mutation after capture cannot make rendered audio disagree with bundled media.
The macOS packaging choice is offered for projects with
procedural recipes or backing tracks and now explains that WAV backing media is
included while sample banks and neural models remain external. The project JSON
format notes were updated accordingly. The regression covers relative-path
resolution across symlink/parent traversal and a symlink leaf, content-hash
deduplication, missing-base rejection, project immutability, receipt metadata,
portable project references and re-render, source mutation before capture with
existing-export preservation, and source mutation after capture with exact
render/package snapshot parity. Release `seam_export_tests` passes 1/1 (4.67 seconds); the
`seam_standalone_authoring` and native Studio targets build; `git diff --check`
passes. This narrows one portability gap but does not make bank/neural-backed
projects self-contained or imply redistribution rights, singer quality, U6
acceptance, or Beta GO. `.github` was not modified. Developer-2's first review
returned NEEDS CHANGES on three
points: lexical/canonical media paths could diverge across symlinks; rendering
used mutable original paths instead of the verified package snapshot; and the
old mutation test could fail during stem rendering before exercising package
staging. Repairs now canonicalize each package source once, stage and verify
those bytes before rendering, route all master/stem renders to the staged files,
and rewrite packaged references to those same bytes. Regressions now cover a
symlink-parent `..` path plus a symlink leaf, deduplication, pre-capture mutation
with preservation of an existing export/receipt, and post-capture mutation with
exact master-versus-bundled-snapshot parity. The affected Release export suite
passes 1/1 (4.67 seconds), native Studio and standalone-authoring targets build,
and `git diff --check` passes. Developer-2 re-review approved this scoped
change at the supplied hashes and confirmed all three findings were addressed.
The approval did not rerun builds or tests and does not approve U6 or Beta GO.
The new success regression compares decoded master PCM with the pre-mutation
reference mix and separately checks the bundled WAV hash; it does not compare
master PCM directly with the backing WAV or independently compare the
mutation-run stem PCM. Future strengthening may add a path-resolution fixture
whose lexical and filesystem targets contain different bytes and an audible
symlink-leaf case. These are nonblocking coverage suggestions, not open
production blockers. `.github` remains unchanged.

2026-09-25 — Portable WAV export symlink regression strengthened. The path
fixture now gives `link/../backing.wav` a filesystem target with different WAV
bytes from its lexical-collapse target; an audible symlink-leaf track exercises
that path through packaging/rendering, while a muted duplicate of that leaf
continues to verify content-hash deduplication. The reference render uses the
same canonical file targets and the packaged-project replay must match it. The
Release `seam_export_tests` target rebuilds, its focused CTest passes 1/1 (3.75
seconds), and `git diff --check` passes. The approved production implementation
is unchanged. This added regression is under focused Developer-2 review at test
source SHA-256 `d5cc2bc8e3dc98c5da424dcb555dc1f8b27d4347d98e19dd99fb55e12e274313`;
it is not yet independently reviewed and is not U6 or Beta GO acceptance.
GitHub CI remains deferred; `.github` is unchanged.

2026-09-25 — Relocated ProjectCopy proof corrected after independent review.
The prior revision's Ready/non-silent/NotFound checks could pass a partial
Preview that omitted the backing clip. The test now checks the persisted hash;
the exact audio track path and hash in the publication's `sourceProject`; zero
diagnostics for that track; `trackCount == 2` for vocal plus backing; and a PCM
difference after muting the backing through the application command. It also
asserts the old directory is absent and document identity names the relocated
project. Release `seam_u2_tests` passes 1/1 (1.19 seconds) and
`git diff --check` passes. Final test source SHA-256:
`9ca6daeb09cb97d861f0e598b779f3de182b07e9bece9f94ee93bd797206f4e4`.
This remains an in-process integration test, not an actual Finder flow or fresh
process restart. Developer-2 re-review is requested; no acceptance claim is
made until that review returns. GitHub CI is deferred and `.github` is unchanged.

2026-09-25 — Muted-render wait made fail-closed. Developer-2 confirmed the
original relocation false pass was closed, then identified that the second
render's revision/Ready conditions only guarded the PCM comparison. The test
now explicitly fails unless the muted render matches the expected revision and
reaches Ready before comparing PCM. Release `seam_u2_tests` passes 1/1 (1.21
seconds) and `git diff --check` passes. Final test source SHA-256:
`eb0735f263595d0c0b334a78912742022fa657ca0bb54505d104077e6f407017`.
Focused re-review is requested; this test remains local app-layer evidence, not
fresh-process or Finder acceptance. GitHub CI remains deferred and `.github` is
unchanged.

Developer-2 approved the final muted-render assertion follow-up at test source
SHA-256 `eb0735f263595d0c0b334a78912742022fa657ca0bb54505d104077e6f407017`.
The review confirms null, stale-revision, non-Ready and timed-out muted renders
cannot skip the PCM contribution comparison. This approval is limited to
in-process relocation and render-path coverage; it does not imply fresh-process,
Finder, general portability or Beta GO acceptance.

2026-09-25 — U22 source-edit audition regression expanded. A single workflow
test now proves that changing the recipe's unvoiced-affricate burst, voiced-
affricate tail voicing, approximant transition, or breath spectrum changes the
corresponding phrase audition; undo restores the exact baseline PCM for each
family, editing invalidates the stale preview, and save/reopen preserves the
changed PCM exactly. The Release Designer CTest
passes 1/1 (1.60 seconds); the Designer, voice-design and installed-singer
journey integration tests pass 3/3 (24.66 seconds), and the macOS Studio target
builds. `git diff --check` passes. This verifies parameter-to-audio wiring,
undo, and recipe persistence, not phonetic intelligibility or listening
quality. U22 recording and imported-audio authoring parity and Beta GO remain
open. GitHub CI remains deferred; `.github` is unchanged.

2026-09-25 — U22 native external-WAV take entry. The Studio now exposes
Cmd/Ctrl+R “Import WAV” beside live recording. It requires an open producer
workspace and selected inventory row, uses a WAV-filtered native picker, captures
the producer epoch/generation/selection before the modal, and revalidates that
context before routing the chosen file through the existing inspect-and-import
path. Import remains an unapproved take subject to marker review; cancellation
does not mutate the project. The macOS Studio builds, the full Release `seam_tests`
CTest passes 1/1 (28.96 seconds), and `git diff --check` passes. This verifies
compilation and the shared controller/import regression, not live picker
interaction, physical-device recording or perceptual qualification. U22's
complete microphone/import parity lifecycle and Beta GO remain open. GitHub CI
is deferred; `.github` is unchanged.

2026-09-25 — U22 WAV import made pointer- and accessibility-discoverable. The
existing keyboard action is now a visible disabled/enabled button with pointer
activation and a semantic Button action in both ordinary production and
generation-only views. Its context-bearing semantic ID is rebuilt from the
producer session epoch, generation and selection; the modal stays guarded while
the file picker is open. The macOS Studio target builds, the full Release
`seam_tests` CTest passes 1/1 (29.08 seconds), and `git diff --check` passes.
This is compile and lower-level suite evidence; VoiceOver interaction and
screen-reader traversal remain unverified. No GitHub CI was run or changed.

2026-09-25 — U22 imported-WAV inspection moved off the UI thread. The native
import action now starts the controller's existing guarded production worker;
the worker snapshots the selected project/row, inspects the WAV, checks
cancellation before the commit boundary, and publishes only the resulting
unapproved take. Raw repository import now accepts a stop token through staging
and durable save. Escape cancellation keeps the visible producer unchanged when
it wins before commit; an already committed result remains eligible for owner-
thread adoption. The native UI integration regression now exercises this async
route through completion and verifies the imported take/review state. Studio and
test targets build; full Release `seam_tests` passes 1/1 (27.24 seconds), and
`git diff --check` passes. No live picker, physical mic or human listening
evidence is claimed. GitHub CI remains deferred; `.github` is unchanged.

2026-09-25 — U22 cancellation propagated through large WAV inspection. Bounded
file hashing now accepts a stop token and detects short/growing files while
streaming; path-based WAV loading reads in 64-KiB chunks and passes cancellation
through PCM decoding. Dry-take inspection propagates that token through both
identity hashes, frame-wise statistics, and the existing cancellable pitch
analyzer, without allocating a second mono-mix buffer. Regression checks cover
pre-cancelled hash/read/inspection and assert peak/RMS/DC results remain exactly
equal to the established mono-mix analyzer. The async producer-import flow now
passes its cancellation token into inspection as well as raw asset staging and
durable save. macOS Studio and full test targets build; Release `seam_tests`
passes 1/1 (29.68 seconds), and `git diff --check` passes. Cancellation cannot
interrupt a single OS-level read or allocation, but it is observed between
64-KiB file chunks and throughout decode/analysis. No GitHub CI was run or
changed; `.github` remains untouched.

2026-09-25 — U22 microphone take publication moved off the UI thread. After a
recording is safely written and its WAV digest captured, producer-workspace
recordings now enter the same cancellable background inspection/import path as
externally imported WAVs. The in-memory capture and exact saved path/hash remain
pending until the owner thread adopts a successful durable import; failed or
cancelled attempts retain them for retry, and successful publication alone
acknowledges the capture. This removes synchronous WAV inspection, technical
metadata generation, and repository commit from the recording-stop action.
Release and Debug macOS Studio plus `seam_tests` targets build; full
`seam_tests` CTest passes in Release (28.81 seconds) and Debug (195.28 seconds),
and `git diff --check` passes. This is code/build/test evidence, not a live
microphone, device-disconnect, or picker/recording parity journey. U22 and Beta
GO remain incomplete. GitHub CI remains deferred; `.github` was not touched.

2026-09-25 — U22 recording shutdown now drains its publication before exit.
After moving producer recording import to a worker, the application shutdown
sequence still cancelled pending production work before finishing the capture;
an active recording could then start its import too late for final state
adoption. Shutdown now first finishes the capture, waits by owner-thread polling
for that capture's cancellable inspection/import to settle, and acknowledges it
only after successful project adoption. Unrelated in-flight production
operations retain cancel-on-exit behavior. Release and Debug native Studio
targets build; Release recording-input, studio-campaign, and full `seam_tests`
CTest targets pass (3/3, 32.35 seconds), and Debug recording-input/studio-
campaign tests pass (2/2, 6.20 seconds). `git diff --check` passes. These are
local build/test results, not a closed-app live recording or device-failure
journey. U22 and Beta GO remain open. GitHub CI remains deferred; `.github` was
not touched.

2026-09-25 — U22 pending-recording cancellation/discard boundary. During
microphone-originated asynchronous publication, Escape now requests worker
cancellation while retaining the in-memory capture and exact saved WAV. X
cannot discard the capture until the worker resolves, preventing a durable
repository commit from racing with an apparent discard. If cancellation arrives
after the transaction's commit boundary, successful adoption still owns the
result; if cancellation wins first, the capture remains retryable/discardable.
The macOS Studio builds in Release and Debug, all 17 macOS source-contract
tests pass, and `git diff --check` passes. This is source-contract/build
evidence, not a live keyboard/device race test. U22/Beta GO remain open;
GitHub CI remains deferred and `.github` was not touched.

2026-09-25 — Native pronunciation-hint editing now matches the registered
English, Korean, and Japanese phone-hint validators. The macOS Edit menu and
VoiceOver/keyboard descriptions use language-neutral labels; non-empty input is
validated against the selected note's language before commit, invalid input
stays open for correction, and empty input still clears the hint. Native UI
regressions cover accepted English and Korean hints, rejected English/Japanese
input, clear/undo, and stale-edit protection. Release `seam_tests` CTest passes
1/1 (28.57 seconds), and `git diff --check` passes. This closes the manual
per-note hint entry path only; it does not provide automatic Japanese dictionary
resource packaging or human pronunciation qualification. Full-scope Beta GO
remains open. GitHub CI remains deferred; `.github` was not touched.

2026-09-25 — U24 pronunciation-hint validation feedback. A rejected phone hint
now exposes the language-specific parser error through the native accessibility
text field and changes the visible bounded-input label to `INVALID PHONE HINT`;
editing again or cancelling clears the error. The regression verifies that an
invalid English hint stays active, its reason is available in the accessibility
tree, and correcting the text in the same editor commits successfully. Release
`seam_tests` CTest passes 1/1 (27.80 seconds), and `git diff --check` passes.
This improves correction feedback, not language-resource qualification or the
full U24/Beta GO gate. GitHub CI remains deferred; `.github` was untouched.

2026-09-25 — U40 performance-take acceptance continuity regression. The
standalone comparison lifecycle now asserts that immediately after accepting an
audible alternate take, the published audio is non-null and represents the
accepted selection, the character-performance read model advances with that
audible publication, and the handoff eventually reaches a canonical render for
the current document revision. Release `seam_tests` CTest passes 1/1 (1,114
cases, 27.09 seconds); `git diff --check` passes. This verifies the in-process
publication/read-model transition, not physical audio-device output or an
installed-app accept-click journey; U40 and Beta GO remain open. GitHub CI
remains deferred; `.github` was not touched.

2026-09-25 — U22 macOS Voice Designer accessibility/runtime smoke. The freshly
rebuilt Studio opened directly into the Designer; the native accessibility
surface created an unsaved Japanese source-filter draft, edited aspiration from
0.05 to 0.12, rendered a one-second vowel preview, pinned reference A, changed
aspiration to 0.30, and rendered current B while retaining A. Undo restored
the prior 0.12 recipe and invalidated the current audition. Existing lower-level
tests prove those parameter changes alter the rendered PCM. The file
picker was dismissed without selecting a workspace; the test draft was not
saved and no microphone permission or physical audio output was used. This
proves the local macOS semantic-control/render/A-B/undo interaction only, not
audible listening quality, physical recording, producer import, or the full
U22 creator journey. U22 and Beta GO remain open. GitHub CI remains deferred;
`.github` was not touched.

2026-09-25 — U22-to-song procedural singer install handoff. Standalone now has
File → “Install Procedural Singer…” for signed `.seamsinger` packages, separate
from sample-bank installation and from the subsequent explicit track-selection
command. The controller installs only into a configured installed-singer root,
requires an explicitly trusted signing key, preserves existing singer versions,
and does not alter the open song. A lifecycle regression publishes a signed
source-filter singer, exercises the native command through the file-dialog
contract, verifies it appears as a trusted/renderable installed offer with
declared language/capabilities, and confirms the project bytes/revision/undo
state remain unchanged. Release `seam_tests` CTest passes 1/1 (27.13 seconds),
the macOS Studio target builds, and `git diff --check` passes. This closes the
standalone procedural-package installation entry point only; it does not
complete the Studio candidate-export/install experience, select or sing with
the newly installed resource in an end-user journey, or qualify voice quality.
U22 and full-scope Beta GO remain open. GitHub CI remains deferred; `.github`
was not touched.

2026-09-25 — U22 designed-voice-to-song journey now uses the standalone installer,
local only; GitHub CI remains deferred. The end-to-end regression previously
installed the signed `.seamsinger` package by calling the distribution API
directly, which left the new native install command outside the actual
designer-to-song journey. It now publishes the Designer-saved recipe, sends the
package through `ApplicationCommand::InstallProceduralSinger` and the
`InstallProceduralSinger` file-dialog contract with the signing key explicitly
trusted, verifies that installation alone leaves the selected track unbound,
then selects the installed singer separately and exports the authored lyric song
from that installed identity. The focused Release
`seam_original_singer_song_journey_tests` CTest passes 1/1 (17.62 seconds),
`git diff --check` passes, and `.github` remains untouched. This closes the
controller-level signed-install handoff in the designed-voice singing regression;
it does not prove native visual/accessibility interaction or perceptual voice
quality, and U22/full-scope Beta GO remain open.

2026-09-25 — U22 saved Designer recipe can now be published as a signed singer
package through a guarded session API, local only; GitHub CI remains deferred.
`VoiceDesignerSession::publishSavedSinger` refuses a missing/dirty/busy draft,
re-reads and identity-checks the persisted recipe before signing, rejects an
output path that would replace the recipe, and requires explicit signer and
distribution metadata. Regressions verify a clean saved recipe publishes to a
package trusted by the corresponding public key, with recipe identity preserved;
unsaved edits and out-of-session file replacement are refused before package
or staging output is created. Release `seam_voice_designer_tests` passes 1/1,
the macOS Voicebank Studio app builds, and `git diff --check` passes; `.github`
was not touched. This is the publish operation/API, not a native publish dialog
or a complete user-facing Studio candidate-export/install journey. Signature
validity does not establish voice quality or approval; U22/full-scope Beta GO
remain open.

2026-09-25 — U22 saved Designer recipe now has a native macOS signed-publish
workflow, local only; GitHub CI remains deferred. Voice Designer exposes a
publish action for a clean saved recipe and requires explicit release version,
display name and language, a selected private signing-key JSON, and a
`.seamsinger` destination. The command rechecks the Designer epoch/revision
after each modal, wipes the in-memory private-key bytes on exit, publishes via
the guarded session API and reports that signing is not quality approval.
Release `seam_voicebank_studio_native` and `seam_voice_designer_tests` build;
the focused Release test passes 1/1 (1.55 seconds), and `git diff --check`
passes. The native dialog/signing flow was not manually exercised this turn;
Windows remains TODO, and U22/full-scope Beta GO remain open. `.github` was
not touched.

2026-09-25 — U22 designed-voice-to-song regression now exercises the guarded
Designer publication boundary, local only; GitHub CI remains deferred. The
journey previously loaded the saved recipe and called the lower-level
distribution publisher directly, bypassing `VoiceDesignerSession`'s clean,
saved, identity-checked publication contract. It now publishes through
`designer.publishSavedSinger`, then installs through the standalone command,
selects the installed singer separately, and exports the song from that singer.
The Release `seam_original_singer_song_journey_tests` target builds and its
focused CTest passes 1/1 (16.98 seconds); `git diff --check` passes. This closes
the controller/session-level saved-recipe-to-song regression path, not native
publish-dialog automation, voice-quality review, or U22/full-scope Beta GO.
`.github` was not touched.

2026-09-25 — U22 native signed-publish workflow live smoke and status repair,
local only; GitHub CI remains deferred. Running a separately identified copy of
the rebuilt macOS Studio, the flow created a voice in the Designer, saved it,
entered explicit version/name/language, selected an ephemeral signing key,
and published a `.seamsinger`. The first run exposed that success feedback was
stored in the audition-status field and cleared by the next idle-audition poll.
Publication feedback now has its own status, is accessible and visible after
repaint, and is tied to the published Designer epoch/revision so an edit clears
the stale notice; the live run verified both behaviors, including disabling
Publish while the draft is dirty. `seam_bank_tool verify-singer` independently
confirmed signature validity and trust for the generated package. Release
`seam_voicebank_studio_native` builds; the Designer and end-to-end singer-song
focused Release CTests pass 2/2 (18.88 seconds), and `git diff --check` passes.
The test-only bundle, private key and outputs were moved to Trash. This is a
macOS workflow smoke, not quality approval, producer listening qualification,
Windows support or U22/full-scope Beta GO. `.github` was not touched.

2026-09-25 — Bounded evaluation-tool provenance capture, local only; GitHub CI
remains deferred. Acoustic evaluation provenance now hashes the pitch
executable in fixed-size chunks, rejects non-regular or over-512-MiB files,
and checks the open file identity/size/mtime before and after hashing so a
concurrently changed executable cannot receive a misleading digest. Regressions
cover oversized input and mutation during hashing; the acoustic-evaluation and
training-environment focused suites pass 7/7, and `git diff --check` passes.
This hardens receipt capture only: it does not rerun or replace the frozen
ablation, add model-quality evidence, qualify a singer, or close U35/Beta GO.
`.github` remains untouched.

2026-09-25 — U35 training-environment capture now binds actual numerical import
origins, local only; GitHub CI remains deferred. Environment receipts now
inventory both active `purelib` and `platlib` roots, reject package roots or
loaded NumPy/SciPy/PyTorch/ONNX Runtime modules outside the active environment,
and require each loaded numerical module's origin to appear in the hashed
distribution inventory. Regressions cover system-path leakage, untracked module
origins, and a distinct `platlib` install. Focused training-environment,
evaluation-provenance, vocoder-command, and review suites pass 19/19;
`git diff --check` passes. This strengthens local reproducibility admission; it
does not qualify trained audio or close U35/U36/Beta GO. `.github` remains
untouched.

2026-09-25 — U22 Voice Designer now offers explicit install after signed
publication, local only; GitHub CI remains deferred. Studio retains the exact
published package path and the public key selected for signing, and enables
installation only while the saved Designer epoch/revision still matches that
package. The action requires that explicit key as the trusted signer, installs
into the same per-user singer root used by standalone, refuses replacement,
and reports installation separately from quality approval. The Release native
Studio and Designer test targets build; `seam_voice_designer_tests` passes 1/1,
the macOS source-contract suite passes 18/18, and `git diff --check` passes.
This completes the in-Studio publish-to-install handoff only; choosing the
installed singer in a song, perceptual qualification, U22 closure and full Beta
GO remain open. `.github` is unchanged.

2026-09-25 — U22 published-singer installation moved behind the guarded
Voice Designer session API, local only; GitHub CI remains deferred. The session
now rechecks the saved recipe on disk, expected Designer epoch/revision, exact
recipe digest and singer identity, and explicit trusted signer before invoking
the transactional non-replacing installer. Regressions verify successful
install, duplicate-install refusal, stale-revision refusal without creating a
destination, and refusal of a different but valid package signed by the same
key. Release and Debug Studio/Designer targets build; the focused Designer
CTest passes 1/1 in each configuration, the macOS source-contract suite passes
18/18, and `git diff --check` passes. First Release test run exposed and then
fixed a missing directory in the new test fixture; final reruns pass. This is
session/install-boundary verification, not full UI journey or voice-quality
qualification; U22/Beta GO remain open. `.github` is unchanged.

2026-09-25 — U22 install now pins the exact published package bytes, local
only; GitHub CI remains deferred. The publish result's container digest is
retained with the package path and signer key, compared during Designer-session
admission, and passed as an expected-digest constraint into the transactional
installer so a same-signer package replacement between admission and install is
refused. Regressions cover a separately published version with the same recipe
and signer but a different package digest, alongside stale-revision, wrong
recipe, duplicate-install, and successful-install cases. Release and Debug
Studio/Designer targets build; focused Designer CTest passes in both
configurations, Release `seam_tests` passes (1,119 cases), the macOS
source-contract suite passes 18/18, and `git diff --check` passes. An earlier
aggregate run failed 8 cases; a direct rerun and the final post-rebuild CTest
passed, so the transient failure remains disclosed. Full user UI/song journey,
voice quality, U22 and Beta GO remain open. `.github` is unchanged.

2026-09-25 — U22 signed-publish-to-install native UI smoke now passes in an
isolated macOS user home, local only; GitHub CI remains deferred. A disposable
app-bundle copy and signing key exercised the actual Designer dialogs, signed
`.seamsinger` publication, and explicit install into the temporary standalone
singer root. CLI verification confirmed the signature/trust, Japanese language,
41 declared phones, and matching signed/installed manifests and recipe digest.
The smoke also caught that the post-install status used the internal recipe ID
instead of the published display name; Studio now retains and reports that
display name. Release Studio builds, `seam_voice_designer_tests` passes 1/1,
the macOS source-contract suite passes 18/18, and `git diff --check` passes.
This is not a standalone song-selection/render journey or perceptual voice
qualification; U22 and full-scope Beta GO remain open. `.github` is unchanged.

2026-09-25 — U22 standalone selection and one-note export now pass in the live
macOS editor, local only; GitHub CI remains deferred. Using a separate app copy
and isolated user-data root, the installed signed singer appeared in File →
Select Installed Singer, was assigned to a new vocal track, and resolved as a
Ready `seam.source-filter.v1` route. A Japanese `あ` note produced one phoneme
with zero warnings; Final export committed a 3-second, 48 kHz stereo WAV whose
SHA-256 matches its receipt and whose PCM is non-silent (mean -57.5 dBFS,
peak -36.3 dBFS). The smoke exposed two UI defects: singer-menu errors were
discarded, and the chooser's 128-character style-label limit rejected the
longer candidate label. Relevant menu actions now surface errors, and the
chooser has a strict 1,024-character bound; the live selection/export succeeds.
Release standalone builds, `seam_original_singer_song_journey_tests` passes,
the macOS source-contract suite passes 19/19, and `git diff --check` passes.
This single-note procedural smoke is not a reviewed song, an intelligibility
or musical-quality result, neural-model qualification, or full U22/Beta GO.
`.github` remains unchanged.

2026-09-25 — Initial procedural singer selection added to the macOS New Project
flow, local only; GitHub CI remains deferred. The form now offers the exact
trusted/renderable installed procedural identities and their declared styles
alongside the existing sample-voicebank selector. Selecting one seeds the new
vocal track with its recipe identity/path/style; the controller revalidates the
exact identity, style, path, trust and renderer compatibility against the live
catalogue immediately before project replacement, refusing stale dialog
choices. The feature does not install or approve the singer, and the two singer
families are mutually exclusive; disabling initial track creation clears both
selectors. Release `seam_editor_native` and `seam_tests` build, aggregate
`seam_tests` CTest passes 1/1, the macOS source-contract suite passes 19/19,
and `git diff --check` passes. Live AppKit smoke in an isolated app-support
root selected the installed engineering fixture in New Project, saved
`InitialSingerSmoke2.seam`, and verified its exact procedural recipe identity,
content hash, path and style. The full original-singer song journey test now
starts through that same initial-singer project-creation request before running
the authored lyric-song render, tuning, export and reopen assertions. The
Designer-created, signed, installed-singer journey also now enters through New
Project after deleting the producer-side recipe and package, then renders and
exports through the installed resource and compares the reopened-project
master digest. Focused CTest passes 1/1. The smoke exposed a stale
`BANK_MISSING` diagnostic carried
over from the startup document. Project replacement now clears old diagnostics;
the regression asserts `BANK_MISSING` exists before replacement and is absent
after creating a project bound to a procedural singer. The Release aggregate
suite passed after the fix. This is
lifecycle and synthetic-fixture render evidence, not live GUI audio output,
perceptual voice quality, or a reviewed song. Windows support remains TODO;
U22 and full-scope Beta GO remain open. `.github` was not touched.

Follow-up live recheck after the diagnostics fix: one cold launch against the
disposable app-support root showed only “No Procedural Singer” despite the
signed fixture at `Data/Singers/voice-draft/0.0.1-test`. After re-publishing
and installing that same recipe through the current package tool, the rebuilt
app exposed both catalog entries; after removing the added second version and
relaunching, the original singleton remained visible in both File → Select
Installed Singer and New Project. Selecting it and saving
`CatalogOnlyOriginalSmoke.seam` preserved its exact recipe identity, hash,
path and style; the live diagnostics panel no longer showed the old
`BANK_MISSING` issue. At that point the project had no notes, so this first
recheck proved catalog, selection, save and diagnostic-reset behavior only.
The initial empty-catalog result was not reproduced, but its trigger remains
unknown; a repeatability/causality check is still warranted.

2026-09-25 — Extended the isolated macOS New Project smoke to one live note
and final audio export, local only; GitHub CI remains deferred. Added Japanese
`あ` (MIDI 72, 240 ticks) to the selected installed fixture singer; the editor
reported one phoneme token, zero warnings, and READY preview/render status.
Export Audio committed a Final PCM24 stereo/48 kHz WAV and receipt at
`/tmp/seam-new-project-live.L9487u/NewProjectOneNoteSmoke.wav`; the WAV SHA-256
matches the committed receipt (`09efe20f…188af`). Saving the project preserved
the note, Japanese lyric, and exact installed recipe identity/hash/path/style
in `CatalogOnlyOriginalSmoke.seam`. Independent `afinfo`/SoX inspection
confirmed 0.25 seconds of non-silent PCM (RMS amplitude 0.004522); the WAV
digest is `09efe20f442f54b6b5284de331db1df918c5fd164810ca9285612210dfe188af`.
Reopening the saved project through File → Open Recent restored the note and
lyric, showed the installed recipe as Ready, and returned one phoneme token,
zero warnings and READY preview status.
This is a real macOS GUI selection→note→
render/export persistence smoke using a synthetic engineering fixture; it is
not independently listened-to voice-quality evidence, a song-quality result,
neural-model qualification, or full U22/Beta GO. `.github` remains untouched.

2026-09-25 — U22 macOS recording failures now provide actionable recovery
guidance, local only; GitHub CI remains deferred. CoreAudio input initialization
and start failures map the SDK's explicit unauthorized/permission statuses to
Microphone privacy settings, missing/bad-device statuses to Sound → Input, and
the not-ready status to reconnect/reselect guidance. A missing default device
now produces a complete user-facing message rather than a zero status detail.
The Studio already routes these errors into its visible recording status; the
existing Info.plist usage string explains that microphone access is only for
explicit recording. Release `seam_editor_native` and `seam_tests` build; the
Release aggregate `seam_tests` CTest passes 1/1, and the macOS source-contract
suite passes 20/20. This change was not verified by denying real TCC access or
disconnecting physical hardware; injected-device tests and that hardware run
remain separate evidence. `git diff --check` passes; `.github` is untouched.

2026-09-25 — U22 recording status now identifies the selected macOS input
device, not just the CoreAudio backend, local only; GitHub CI remains deferred.
The capture adapter stores the device's CoreAudio display name when opening the
default input and exposes it through `AudioInputDeviceInfo`; Voicebank Studio
shows backend/name in the MIC banner, a Unicode-safe 44-column label, and the
accessible operation status (`CAPTURING FROM …`). A recording-session
regression verifies that the device name is preserved through the session.
Release `seam_voicebank_studio_native` and `seam_tests` build, aggregate Release
CTest passes 1/1, the macOS source-contract suite passes 20/20, and
`git diff --check` passes. No physical microphone was opened in this check.
The real device/TCC workflow remains an outstanding U22 acceptance item;
`.github` remains untouched.

2026-09-25 — U23 phrase duplication now transfers time-scoped performance
ownership and accepted generated-performance selections, local only; GitHub CI
remains deferred. `CopyNotePerformanceCommand` derives and requires a single
common translation for the mapped phrase, projects manual ownership and
accepted selections through the shared `transformRegionPerformance` boundary,
clips source ranges to the selected phrase window, translates them to the
duplicate, and adjusts accepted `sourceTickOffset` so copied notes reuse the
corresponding source-take span. Range/offset overflow, capacity exhaustion,
nonuniform mappings, and post-merge collisions reject before mutation. Tests
cover translated manual and accepted ranges, retained take identity, source
offset, nonuniform refusal, and exact undo/redo through actual phrase
duplication. Release `seam_performance_command_tests` passes 21/21 and
`seam_performance_edit_preservation_tests` passes 5/5; both focused CTest
targets pass 2/2. The broader `seam_tests` attempt could not complete because
the host data volume had only 121 MiB available: 751 cases passed, while 370
cases were reported failed. The captured tail showed repeated temporary-
directory creation failures with `No space left on device`; CTest also could
not write `LastTestsFailed.log`. This is an environmental aggregate failure,
not an acceptance pass. No cleanup was performed. `git diff --check` passes;
`.github` remains untouched.

2026-09-25 — U23 phrase-copy boundary coverage extended; GitHub CI remains
deferred. A Release command-level regression now checks manual ownership and
accepted-performance ranges that cross the selected phrase end, as well as
ranges that begin exactly outside it. The crossing scopes are clipped to the
phrase window before translation; the outside-only scopes are not duplicated,
original scopes remain unchanged, accepted source offsets still resolve to the
original take span, and undo/redo restores exact project snapshots. Release
`seam_performance_command_tests` passes 22/22 and
`seam_performance_edit_preservation_tests` passes 5/5 (focused CTest 2/2).
The broader Release `seam_tests` CTest also passes 1/1 (27.86 seconds). This
closes a boundary-coverage gap only; it does not mark U23 complete. No
production feature logic changed in this slice. `.github` remains untouched.

2026-09-25 — U25 selected-note vibrato visualization slice; GitHub CI remains
deferred. Selected notes with persisted vibrato now show a bounded envelope
trace in the piano roll. The trace uses the project tempo map for note duration,
the stored onset/fade/depth/period/phase values, and a capped sample count; it
does not change note geometry or appear for unselected notes. A raster regression
checks the visible change, stable geometry, and selection behavior. The focused
test passes in Debug and Release; the full Release `seam_tests` aggregate passes
1122/1122, and `git diff --check` passes. This is visualization evidence only:
interactive vibrato handles, full keyboard/accessibility parity, actual audio
review, and U25 acceptance remain open. `.github` was not touched.

2026-09-25 — U25 selected-note vibrato onset and depth handles; GitHub CI
remains deferred. The bounded piano-roll envelope now presents single-selection
onset and depth handles; dragging updates a visible, non-persistent curve
preview, and release submits only the corresponding vibrato field through the
existing `VibratoModel`/performance-command path. Drag state captures the note,
selection, revision, and geometry; selection/document/layout drift cancels
without mutation. Regression coverage exercises both handles, preview before
commit, one-edit undo/redo, and stale-selection rejection. The full unfiltered
`seam_tests` aggregate passes 1124/1124 in Debug and Release; `git diff --check`
passes. This closes pointer-based onset/depth editing only. Vibrato fade/period/
phase handles, keyboard-only handle navigation, full assistive-technology
qualification, listening review, and U25 acceptance remain open. `.github` was
not touched.

2026-09-25 — U25 vibrato fade-envelope handles; GitHub CI remains deferred.
Selected-note onset/depth editing now shares the piano-roll overlay with
fade-in-end and fade-out-start handles. Fade drags update a live preview and
commit only their own parameter through `VibratoModel`, clamping each fade to
the remaining legal span so the combined fade duration stays valid. Tests cover
both endpoints, untouched counterpart values, preview isolation, exact undo,
and full-project preservation. Depth drag also now applies pointer displacement
from the grabbed curve point, so merely grabbing it cannot jump its value; the
regression checks this before a deliberate depth change. The full unfiltered
`seam_tests` aggregate passes 1125/1125 in Debug and Release; `git diff --check`
passes. Period and phase remain inspector-only controls; creating zero-length
fades from the canvas, keyboard handle navigation, broader accessibility
qualification, listening review, and U25 acceptance remain open. `.github` was
not touched.

2026-09-25 — U25 vibrato period/rate handle; GitHub CI remains deferred. The
selected-note envelope now exposes a period handle at the end of its first full
cycle when that cycle fits within the active vibrato span with enough pointer
separation from onset. Dragging previews the adjusted period and commits it
through `VibratoModel`, with undo/redo coverage. When the cycle cannot fit, the
canvas hides this handle and leaves the existing inspector control as the
fallback. Release `seam_tests` passes 1125/1125. The first Debug aggregate run
overlapped Release and produced render-coordinator readiness timeouts; rerunning
Debug alone passes 1125/1125, including those coordinator cases. `git diff --check`
passes. This adds pointer editing for onset, depth, fades, and period;
phase remains inspector-only, and keyboard handle navigation, broad
accessibility qualification, listening review, and full U25 acceptance remain
open. `.github` was not touched.

2026-09-25 — U25 vibrato phase handle; GitHub CI remains deferred. The first
visible vibrato cycle now includes a separate compact phase track with a
draggable phase marker. Its preview changes the curve without mutating the
project; release applies only `phaseTurns` through `VibratoModel`, preserving
the same undo/redo and stale-target protections as the other handles. The phase
marker is omitted at zero phase, where it would collide with the onset grip, so
the inspector remains the fallback. Full unfiltered Debug and Release
`seam_tests` each pass 1125/1125; the period/phase/onset/depth/fade controller
regression passes in both. `git diff --check` passes. Keyboard handle
navigation, broader accessibility qualification, listening review, and full
U25 acceptance remain open. `.github` was not touched.

2026-09-25 — U25 keyboard vibrato-handle navigation; GitHub CI remains
deferred. `Alt+V` now enters selected-note handle focus, Left/Right cycles the
currently available onset/depth/fade/period/phase controls, Up/Down adjusts the
focused parameter in bounded steps through the same drag-preview and
`VibratoModel` transaction path, and Escape exits. The selected note's
accessibility description announces these keys, and a visible focus ring tracks
the active handle. The new keyboard regression passes in Debug and verifies
focus, adjustment, raster focus indication, undo, and exit. Release
`seam_tests` builds successfully. The Debug aggregate reports 1120 passed and 6
failed: five temporary-directory creation cases report `No space left on
device`, and one durability assertion fails in that exhausted run. Therefore
this turn does not claim an aggregate pass; the vibrato pointer and keyboard
regressions pass in Debug. Release aggregate tests were not rerun after this
keyboard change. `git diff --check` passes. Direct accessibility-action
controls, broader assistive-technology qualification, listening review, and
full U25 acceptance remain open. `.github` was not touched.

2026-09-25 — U25 semantic vibrato-handle controls; GitHub CI remains deferred.
For exactly one selected vocal note with enabled vibrato, the semantic tree now
publishes individually named onset, depth, fade, period, and phase controls as
focusable buttons with current values and keyboard guidance. Set Focus and
Activate route to the existing keyboard focus state; Up/Down then applies the
same bounded, undoable `VibratoModel` edit as keyboard and pointer interaction.
Unavailable handles are omitted, and stale IDs are refused after deselection.
Tests cover semantic discovery, direct focus, keyboard adjustment, undo, and
stale-target refusal. Full Debug and Release `seam_tests` pass 1126/1126 each;
`git diff --check` passes. This verifies the in-process semantic contract, not
independent VoiceOver/assistive-technology qualification. Broader accessibility
qualification, listening review, and U25 acceptance remain open. `.github` was
not touched.

2026-09-25 — U25 zero-length vibrato fade creation; GitHub CI remains
deferred. Enabled notes now expose fade-in and fade-out grips even when the
corresponding fade length is zero: the zero endpoints use a separate top-edge
track so they remain distinguishable from the onset handle. Onset, fade,
period, and phase drags now apply pointer displacement relative to the grabbed
handle and captured source value, preventing value jumps when users grab away
from a marker center and enabling fade creation from zero. Regression coverage
checks stable no-motion preview, creation of both fades, counterpart
preservation, semantic visibility, and undo/redo. Full Debug and Release
`seam_tests` pass 1127/1127 each; `git diff --check` passes. This closes
pointer-based zero-fade creation only; broader accessibility qualification,
listening review, and full U25 acceptance remain open. `.github` was not
touched.

2026-09-25 — U25 macOS AppKit accessibility bridge; GitHub CI remains deferred.
The native AppKit hierarchy now invalidates its cached snapshot and posts a
focused-element-changed notification whenever a successful accessibility
action changes semantic focus, including Activate callbacks that move focus.
The AppKit integration fixture exposes the selected note's vibrato period
control and verifies its native button role, title, value, help, focus action,
focused-element query, press action, and snapshot refresh after focus moves.
Its note geometry now meets the editor's minimum hit-target dimensions instead
of relying on an unrenderable tiny vibrato handle. The local `seam_tests`
runner also accepts an optional test-name substring; the no-argument path still
runs the complete suite, and an unmatched filter exits nonzero. Full unfiltered
Debug `seam_tests` passes 1129/1129; the focused AppKit regression passes 1/1
after its final focus-refresh assertion; Release `seam_native_ui` builds, and
`git diff --check` passes. This exercises AppKit in-process, not independent
VoiceOver/user qualification. Broader assistive-technology qualification,
listening review, and full U25 acceptance remain open. `.github` was not
touched.

2026-09-25 — U26 persisted Japanese lyric-reading foundation; GitHub CI remains
deferred. Schema 20 adds a nullable, bounded `readingHint` to each lyric token;
older project schemas migrate with no reading. The undoable Japanese-reading
command edits this field independently of visible lyrics and note-level phone
hints. Surface or language changes clear a stale reading in the same transaction.
The Japanese phonemizer consumes an authored reading for pronunciation while
preserving the displayed surface, and resolver input identities/budgets include
it. MIDI and USTX export report its loss rather than silently implying it
survives interchange. Focused pronunciation, project-state, MIDI and USTX
CTest targets pass 4/4 in Debug and Release; full unfiltered `seam_tests`
passes 1/1 in each configuration. After the last USTX diagnostic-deduplication
edit, the focused USTX target passed again in both configurations and
`git diff --check` passed. This is a model, persistence and resolver slice:
native editing of the field, dictionary trust/shipping decisions, language
review, U26 acceptance and Beta GO remain open. `.github` was not touched.

2026-09-28 — Synthetic end-to-end source-alignment timing regression; U16 and
Beta GO remain open. A frozen source alignment and measured acoustic-analysis
sidecar now pass through `PhraseRenderPipeline` at compressed and expanded
tempos. The regression checks exact placement/render extent including the
3,600-frame pre-utterance, preserves the MIDI 60 score pitch after rendering,
and checks sample correlation through the mapped measured-unvoiced interval.
The Release and Debug performance-snapshot suites pass, as do adjacent Release
synthesis-quality, performance-compiler and formant-expression targets;
`git diff --check` passes. This is deterministic synthetic-signal evidence,
not natural-voice perceptual acceptance: the multilingual fixed corpus,
listening/reviewer evidence and the associated full milestone gates remain
open. `.github` was not touched.

2026-09-28 — U16 release-edge timing supplement, verified and pushed in
`97423695`; GitHub CI remains deferred and `.github` was not touched. The
frozen source now has a voiced segment immediately before the authored release
marker and a deterministic unvoiced tail after it. At both compressed and
expanded tempos, the end-to-end snapshot test confirms the measured voicing
boundary and correlates the rendered release tail above 0.99 with the
alignment-mapped source tail, while preserving the exact render extent and
score-pitch assertions. The focused performance-snapshot CTest passes in
Release and Debug. This closes only a numerical synthetic release-mapping gap;
short/long natural vowels, intelligibility, fixed-corpus and listening/reviewer
qualification, U16 acceptance and Beta GO remain open.

2026-09-28 — U16 mapped release coverage across all exposed classical
renderers, verified and pushed in `de4112f6`; GitHub CI remains deferred and
`.github` was not touched. The compressed/expanded frozen-alignment fixture
now runs six combinations: Classic PSOLA, Spectral Classic and Stretch at
320/80 BPM. Every combination passes exact placement extent, the score-pitch
check, measured-voicing/no-fallback checks, interior unvoiced preservation and
alignment-mapped release-tail correlation above 0.99. Focused Release and
Debug performance-snapshot CTest both pass. This broadens deterministic
synthetic coverage only; natural short/long vowels, breath intelligibility,
fixed-corpus listening/reviewer qualification, U16 acceptance and Beta GO
remain open.

2026-09-29 — U22 recording-to-Studio import parity fixture, verified and
pushed in `ceb3d5db`; GitHub CI remains deferred and `.github` was not
touched. A fake physical-input backend now drives `RecordingInputSession`,
publishes a PCM24 WAV, then imports that exact file through Studio's
asynchronous raw-WAV path. A second producer workspace uses the standard
inspect-then-import path; the test compares source hash, inspection receipt,
marker-review queue state, absence of human review, in-memory project state,
and recovered durable project state. Release target build and focused CTest
pass (1/1); `git diff --check` passes. This closes only fake-device
capture/export/import parity. CoreAudio/TCC, physical microphone failure and
permission journeys, and native UI orchestration remain unverified; U22 and
full-scope Beta GO remain open.

2026-09-29 — Read-only export-set evidence inspection, verified and pushed in
`46e0ca50`; GitHub CI remains deferred and `.github` was not touched.
`ExportService::inspectSetReadOnly` distinguishes missing, incomplete,
recovery-required and committed output. A committed result requires a valid
receipt, safe receipt-owned paths, all owned files present, and matching file
hashes. Journaled publication is reported without locking, cleanup, rollback,
or receipt rewriting. Release `seam_export_tests` builds and passes 1/1; its
filesystem snapshot regression confirms journal and transaction paths are
unchanged. This is a reusable read-only export API, not yet per-job evidence
or a Studio request-detail UI; U22 and full-scope Beta GO remain open.

2026-09-29 — Read-only per-generation-job evidence, verified and pushed in
`c4b63355`; GitHub CI remains deferred and `.github` was not touched.
`inspectGenerationJobOutputReadOnly` now distinguishes an unpublished job,
incomplete package/output, valid prepared package, pending export recovery and
candidate bytes verified against the frozen job. Candidate validation is
shared with the run path, receipt hashing propagates cancellation, and the API
never renders, locks, recovers or collects. Release `seam_export_tests` builds
and passes 1/1; regressions cover all five states, cancellation, tamper
rejection, and journal persistence. The Debug build directory is not configured.
No Studio request-detail UI is wired yet; output verification is not collection
or human review, and U22/full-scope Beta GO remain open.
