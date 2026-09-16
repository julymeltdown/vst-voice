# U26 helper process lifecycle primitive

## September 17 continuation: Windows process primitive landed; reading staging remains open

`runBoundedHelperProcess` now has a native Windows implementation. It launches an explicit absolute
executable with `CreateProcessW`, inherits only the three selected standard-I/O handles, supplies an empty
environment, bounds all three streams, observes cancellation and deadlines, applies a job-object memory
ceiling, samples process memory/CPU, and retires the process tree through kill-on-job-close. The dedicated
Windows CI slice builds the helper and its neural/Japanese callers, runs the helper and neural protocol
fixtures, and retains a source-contract check. Strict UTF conversion and secure wide-path reading remove
the previous MSVC-only build failures.

This closes the generic Windows process-backend gap. It does **not** qualify the complete Japanese reading
workflow on Windows. `StagedJapaneseReadingResource` still uses POSIX descriptor-relative staging and its
resource/capture/job tests are gated to Apple/Linux. No shipped Open JTalk resource, signed Windows resource
manifest, private Windows staging implementation, native-language review or installed-host evidence is
claimed. The historical checkpoints below remain accurate for the code state at which each was recorded;
their statements that the Windows backend was then missing are superseded only by this continuation.

Status: the generic bounded process primitive is implemented on macOS, Linux and Windows; fixture-backed
native reading review is locally tested on macOS. Shipping resource trust, private Windows reading staging,
installed-host qualification and U26 acceptance remain open; this is not a sandbox.

## Current continuation: native review, resource ceilings and cancellation retirement

The reading worker is now exposed through the native replacement-review panel:
preparing/ready/failure status, paged contextual-token rows, wrapped read-only
details, accessibility actions and explicit Apply/Cancel/Retry. Apply is disabled
unless the immutable capture validates complete Known, single-note, hint-free
ownership; the command then persists separated phone hints and pronunciation
identity with exact undo/redo. Standalone and CLAP runtimes accept an optional
host-owned callback that returns a verified/staged resource. They do not search
PATH, the working directory or project-controlled paths, and no dictionary is
packaged by this change.

`HelperProcessRequest` now supports optional resident-memory and CPU-time
ceilings. The macOS runner samples `proc_pid_rusage` and the Linux path samples
`/proc` while draining output; an exceeded ceiling kills the owned process group
and returns a bounded diagnostic. These are enforcement checks, not a kernel
sandbox or a guarantee against allocations between samples. A `pollCancelled`
retirement path lets a closed native review join/discard its worker without
retaining a session/resource identity; reopening is rejected until retirement is
complete. Focused helper/native/Japanese and core tests pass in Release; the
platform/supervisor qualification and shipping Open JTalk resource remain open.

## Owner-thread reading worker checkpoint

`JapaneseReadingJob` now owns a single staged `JapaneseReadingCapture` and runs its immutable `read()` operation on a `std::jthread`. The owner may inspect `State::Preparing`, `Ready`, `Cancelled` or `Failed`, request a monotonically increasing request ID, poll only on the owner thread and retrieve a current result only when region, complete project, revision, performance-generation context and staged resource identity still match. The worker owns only a capture copy and writes one completion slot under a mutex; it never touches editor/session/UI objects.

Starting while a previous worker is joinable rejects. Cancel clears any visible result immediately, marks the job Cancelled, requests the worker stop, and requires polling/retirement before a new start. A completed cancelled result is discarded. Worker failure preserves a bounded diagnostic and never becomes current. A successful result received after `EditorSession::replaceProject`, direct source changes, or identity mismatch is rejected before adoption. The request ID is not itself an editor command or global scheduler; the current job deliberately serializes starts and leaves “latest request wins” orchestration to its owner/controller.

The test-only `seam_reading_capture_process_probe` emits typed JSON for 学校/へ and rest-separated 学; the job fixture hashes/stages its executable and dictionary, then exercises real off-thread process/decoder/binding. Tests prove two-note 学校 ownership, explicit-hint flags, no score/history mutation, concurrent-start rejection, cancellation state/retirement/restart, missing staged executable failure, stale replacement rejection and terminal current-result suppression. Release/Debug focused target passes (0.80/1.10 s). Release core currently passes 650 cases (16.54 s at the preceding checkpoint); the job test is included in that target. No UI Apply, persistence or automatic phoneme mutation is performed.

The worker does not impose a hard wall-clock bound beyond the helper's per-request deadline, and staging/verification remain synchronous before `start`. Its cancellation cannot preempt a blocking filesystem or process-system call; the helper's process-group cleanup handles normal cancellation/timeouts. A controller must retire a prior worker before replacing it and must not call `current()` from a paint/audio thread. Windows and non-qualified host process supervision remain unsupported.

## Captured phrase and note-ownership checkpoint

`JapaneseReadingCapture` now prepares an immutable reading source from explicitly requested notes in one region. It sorts by tick/ID, rejects duplicate/missing identities, skipped intervening notes, selected-note overlap, incompatible language and invalid/oversized text. Admission is at most 10000 region notes/lyrics/requested notes and 4096 assembled UTF-8 bytes. Adjacent fragments are concatenated for contextual reading; rests insert a lexical space. Explicit shared-lyric legato continuation reuses the same source span rather than repeating its text. This is not yet automatic long-song chunking or arbitrary polyphonic voice extraction.

Every source span retains its note ID, lyric ID and whether that note has an explicit phone hint. Hinted notes contribute their surface text as lexical context, but returned data touching them is flagged and never applied automatically. `bind` validates source/resource identity and exact token spans, then records all intersected note owners, cross-lyric tokens, explicit-hint contact and inserted/unowned text. Its scan admits at most 4194304 token-owner comparisons. A word split across notes remains one reading token with multiple owners; this does **not** invent a mora-to-note allocation or override creator intent.

`read` operates on the retained staged resource, verifies it before/after bounded helper execution, decodes the response and binds token ownership. It makes no editor calls or commands. `matches` requires the active region, complete captured project, revision, editor performance-generation context and current resource identity to match. A result can still be inspected from an old immutable capture, but that does not authorize adopting it into a changed document. Latest-request arbitration and cancellation/discard state belong to the upcoming worker owner; this capture does not by itself identify which of multiple same-document UI requests is latest. Selection changes alone do not change the explicit captured note set or project revision.

The new regression covers a cross-note 学校 token, explicit-hint preservation, unchanged project/history, identical new sessions, helper identity changes, direct hint edits, same-content document replacement, skipped/duplicate/overlapping notes, shared melisma source reuse and rest separators. The real staged transport probe now builds an eight-note score with 学 and 校 separate, reads the contextual sentence, verifies seven lexical tokens with correct multi-note binding/hint flags, and rejects adoption after replacing the document. It passes Release and Debug without score mutation.

All 650 Release core cases pass (16.54 s); 14 focused Japanese cases pass Release/Debug (0.51/0.55 s). Native/core/focused/transport builds, real probes and diff checks pass. Worker lifecycle/latest-request adoption, helper CPU/memory limits, review/explicit Apply, reading persistence and native-language/resource/host qualification remain open. Changes are local/uncommitted; no U26 or full Beta GO acceptance.

## Private verified staging checkpoint

`StagedJapaneseReadingResource` now creates independent private copies of an already verified helper/dictionary under an explicitly selected application staging parent. It revalidates the originals, creates a unique mode-0700 directory, and copies only the executable and four dictionary files. Source descriptors use non-following/nonblocking opens and regular-file checks; destination creation is exclusive and descriptor-relative. Copying streams through 64 KiB buffers with cancellation and per-file/256 MiB aggregate limits, hashes the bytes actually copied, verifies the completed staged set, and requires unchanged resource identity before publishing it.

Published files have mode 0500 for the helper and 0400 for dictionaries; directories have mode 0500. Shared ownership retains them until the last staged-resource consumer retires. Cleanup restores private directory write access and unlinks only the five known files through held directory descriptors. It does not recursively delete unexpected contents; the root is removed only when its named inode/device still matches the held directory. Failed/pre-cancelled preparation does not publish a stage. There is no durable cache or crash-recovery pruning yet; abnormal process death can leave temporary copies behind.

These are private, read-only-by-permission copies, **not kernel-enforced immutable files**. They isolate execution from ordinary in-place modification or replacement of the installed originals. They do not defend against a malicious owner/root who changes permissions or tampers with the private staging namespace; a trusted staging parent and future supervisor/sandbox threat model remain necessary. Copy/hash filesystem I/O is not preemptible in the middle of a blocking OS call. Windows staging is explicitly unsupported; only macOS runtime behavior was tested.

The development transport probe now executes the staged helper against staged dictionaries and keeps ownership alive until process completion, resource revalidation and typed decoding. Real Japanese transport passes Release/Debug using the pinned helper/dictionary identity. A new regression verifies equal identity, permissions, independence from original inode truncation and atomic replacement, shared lifetime, last-owner cleanup, unchanged originals, and rejection without staging on stale/pre-cancelled inputs. Mid-copy cancellation and injected I/O-failure cleanup are code paths, not separately fault-injected acceptance evidence in this checkpoint.

All 649 Release core cases pass (16.37 s); 13 focused Japanese cases pass Release/Debug (0.38/0.41 s). Native/core/focused/transport builds, actual staged transport probes and diff checks pass. The current real probe consumes about 107 MiB of dictionary copies per prepared stage; prepare once per resource/session rather than per note. Cache/global disk budgets and preparation latency qualification remain open. Changes remain local/uncommitted. Next: helper memory/CPU limits and immutable source/request adoption, followed by resolver/editor integration and host/native-language acceptance.

## Resource verification checkpoint

New `VerifiedJapaneseReadingResource` accepts application-controlled expectations for an absolute executable, its SHA-256, an upstream engine revision and the exact four dictionary hashes. It rejects malformed expectations, non-absolute/NUL/overlong paths, symlink executable/root/file entries, missing or extra dictionary entries (including `dicrc`) and non-regular files. Limits are 64 MiB per helper, 128 MiB per dictionary file and 256 MiB combined. Hashing streams through 64 KiB buffers with cancellation checks, length limits and modification checks; on macOS/Linux it opens with `O_NOFOLLOW|O_NONBLOCK`, verifies regular-file handles, and checks named inode/device identity after hashing. The Windows fallback and cross-platform path/race behavior have not been qualified.

The dictionary identity is a deterministic SHA-256 over a version tag plus ordered filename/hash pairs. `JapaneseReadingIdentity` now also contains the helper executable SHA-256, which typed validation requires; wrapper-binary changes therefore cannot hide behind an unchanged upstream revision and dictionary digest. The returned object stores canonical paths and expected identity privately, and `revalidate` repeats the checks. It does not make files immutable or authenticate the caller's expectations. Those expectations must come from trusted application/release intake data, never a project's self-supplied hashes.

Inspection of the pinned fork's `mecab/src/utils.cpp` and `param.cpp` found that external rc-file parsing and `Param::load` file-content parsing are commented out in this fork, while the latter still opens the dictionary `dicrc` pathname and installs fixed defaults. Requiring a directory containing only `char.bin`, `matrix.bin`, `sys.dic`, `unk.dic` prevents ordinary extra-configuration reads at verification time. This finding is specific to the pinned fork, not stock MeCab or arbitrary future versions.

The development transport probe now accepts `HELPER DICTIONARY EXPECTED_HELPER_SHA256`, verifies resources, uses canonical paths for stdin execution, revalidates after completion and decodes with the derived identity instead of synthetic metadata. It passes in Release and Debug with helper SHA-256 `b19db9b0b592d57896bceee5f11f8b74ee736127b9a51969a2276cb0b7112bd3` and the pinned dictionary. The expected helper digest was obtained from the explicitly built local probe, not a signed release manifest; this is integrity/transport evidence, not release trust.

Tests cover valid/repeated verification, executable/dictionary mismatch, invalid revision, extra configuration, tampering/restoration, oversized sparse file rejection, cancellation, symlink rejection and a FIFO that must reject without opening/blocking. All 648 Release core cases pass (15.68 s); 12 focused Japanese cases pass Release/Debug (0.48/0.42 s); real transport probes pass R/D. Native/core/focused/probe builds and diff checks pass. An initial missing brace was fixed before successful builds/runs.

Important remaining limits: these are point-in-time checks, not race-free execution or immutable staging. Ancestor/path changes and swap/restore attacks around use are not eliminated; pre/post checks alone do not close TOCTOU. Hashing can block on filesystem I/O and is not a hard wall-clock memory/CPU sandbox. Immutable verified copies/handles, trusted packaging, process resource limits and source-bound worker adoption remain required before editor use. Changes remain local/uncommitted; full U26/Beta GO remains incomplete.

## Private stdin and real-reading transport checkpoint

`HelperProcessRequest.standardInput` now carries at most 4096 opaque bytes independently of argv. An AF_UNIX socket pair supplies child stdin; the parent writes nonblocking, polls input readiness together with both output streams, and closes its endpoint after exact delivery to signal EOF. Empty input gives EOF without inheriting host stdin. Parent socket writes suppress SIGPIPE per socket (`SO_NOSIGPIPE` on macOS) or per send (`MSG_NOSIGNAL` on Linux), without changing the parent's process-wide signal disposition. Incomplete delivery or write failure rejects the operation. Bytes accepted by the kernel do not prove the helper consumed them; the reading decoder's exact echoed-source validation remains necessary.

The process regression adds byte-exact UTF-8/newline/NUL transport, simultaneous streamed output, 4096-byte acceptance/4097-byte rejection, empty EOF, early failure and a helper that does not consume input before timeout. This is an opaque process transport; the Japanese reader separately rejects embedded NUL and empty text. Lyrics are not added to argv by this API. They remain in process memory and the reading response, so this is not encryption, protection from privileged inspection or permission to log responses automatically.

The Open JTalk development probe now supports `--read-stdin DICTIONARY`, reads within its input budget before loading the dictionary, and echoes the exact input in JSON. Its legacy `--read` mode remains only for explicit development use. The dictionary checker now uses stdin and passes four pinned dictionary hashes, three reading/span fixtures and empty/oversized/NUL admission rejection.

New `seam_reading_transport_probe` exercises SEAM's runner -> real MeCab -> bounded JSON decoder -> typed validator, including は→ワ and へ→エ. It was run successfully in Release and Debug against the existing temporary dictionary and probe. The transport probe intentionally uses synthetic identity metadata and explicitly reports resource attestation pending; it must not be presented as a verified production reading service.

Verification: two process cases pass Release/Debug (1.18/1.59 s), with an injected parent secret environment variable; all 647 Release core cases pass (16.11 s). Strict dependency probe, native/core/focused/transport builds and diff checks pass. The process cases remain a separate target, not extra core cases. Resource/config verification, memory limits, source-bound adoption and actual editor integration remain open; host/platform qualification assumptions below are unchanged. Changes remain local/uncommitted.

## Contract

`authoring::runBoundedHelperProcess` accepts an explicit absolute executable and argument vector, not a shell command or PATH search. It admits at most 32 arguments/65536 aggregate argument bytes, a 1..60000 ms timeout, up to 1048576 stdout bytes and 65536 stderr bytes; NUL-bearing paths/arguments reject. The caller must choose a trusted application helper, not a path read from a project.

The macOS/POSIX path uses `posix_spawn`, a new process group, empty environment, bounded EOF-terminated stdin, separate output pipes and nonblocking I/O. Spawn action sources are duplicated above descriptors 0..2. macOS uses `POSIX_SPAWN_CLOEXEC_DEFAULT`; the Linux implementation requires glibc 2.34+ `addclosefrom_np` and otherwise reports Unsupported. Windows is not implemented. Only macOS was executed in this checkpoint.

The owner performs at most one 4096-byte read per stream before checking cancellation/deadline again. A 10 ms poll interval avoids a busy writer starving the other stream or lifecycle checks. Either stream exceeding its configured cap rejects the operation without returning partial output. Nonzero/signalled exits reject; successful stdout/stderr are returned separately only after process exit and pipe EOF. The child environment does not inherit loader, MeCab or shell configuration variables.

Cleanup kills the owned process group and reaps the direct child. `waitid(...WNOWAIT)` observes exit without freeing the leader PID before cleanup; already-lost child ownership is checked before signalling. RAII also covers failures and exceptions. This **requires exclusive child-reaping ownership**: a foreign SIGCHLD handler or another `waitpid(-1)` consumer can invalidate that assumption. It is not safe to claim arbitrary plug-in-host compatibility without a qualified supervisor boundary. Descendants that escape the process group are not a contained adversarial workload.

## Verification

The dedicated `seam_reading_process_probe` supports ordinary output, failure, flooding either stream, delay and a forked descendant retaining pipes. `seam_helper_process_tests` checks separate output, EOF stdin, nonzero exit rejection, actual byte-limit errors, timeout, cancellation before/during work, relative/NUL/missing-path rejection and return within a three-second test guard for timeout scenarios. It also opens a non-CLOEXEC parent descriptor and checks that the child does not inherit descriptors 3..255. The suite was run with `SEAM_HELPER_SECRET=must-not-reach-child`; the helper rejects if it inherits that variable.

The process case passes Release/Debug on macOS (1.09/1.23 s). The 647-case Release core suite passes (16.07 s). This new process case is a separate target, not an additional core-case count. Native/core/focused builds and `git diff --check` pass. Descendant pipe timeout is tested, but no independent OS process-tree assertion certifies every descendant's post-cleanup state. No existing user app or process was targeted.

## Remaining execution boundary

- Verify executable and complete dictionary/config identity, address file mutation/TOCTOU, and prevent uncontrolled working-directory/config reads before invoking the real reader.
- Add hard memory/CPU/resource limits and an appropriate process supervisor/sandbox. Current output caps and polling do not bound allocations inside MeCab, and the runner cannot preempt a blocking `posix_spawn` call.
- Implement Windows execution and qualify Linux and real plug-in-host process/signal ownership.
- Qualify the new bounded stdin channel in the eventual production reading service; do not use the development probe's legacy argv-text mode there.
- Connect the reading response decoder, immutable source/request identity, worker retirement and stale-result adoption.
- Connect validated readings to note/lyric ownership and editable reading intent in the resolver/native UI, then complete native-language/resource qualification.

No binary/resource verification, verified reading-service launch, automatic editor mutation, distribution promotion, commit or push occurred. Full Beta GO remains incomplete.
