# Atomic resource installation

Sample `.seambank` and procedural `.seamsinger` installation share one transaction.
The installed resource is engineering material unless separately qualified. A trusted
signature establishes publisher identity, not listening, rights or release approval.
This transaction is candidate-agnostic: generic signed packages need no `candidate.json`.
Candidate packaging separately requires a complete, verified schema-3 dependency set.

## Transaction

A caller can pin the package SHA-256 and effective content identity. Both pins are
checked before publication. A private, randomly named staging directory is created
with owner-only permissions; existing staging or backup paths are never cleared.
Every signed entry is checked during extraction and again on disk. The staged file
set must equal the signed entries plus the installer-generated receipt. Packages
cannot provide their own receipt. The family manifest/content is checked before commit.

A nonblocking directory lock permits one installer per installation root. A competing
installer receives a busy conflict; retry after the active transaction finishes.
The initial target directory identity is retained and checked before replacement.
Parent identities are checked, and held directory descriptors anchor publication.
On macOS, create-new uses `RENAME_EXCL` and replacement uses `RENAME_SWAP`; Linux has
the corresponding `renameat2` implementation, which is not verified by macOS tests.
The old version is never temporarily absent during a successful replacement.

Cancellation and injected failures before commit preserve existing resource bytes.
Empty ancestors created while preparing a new root may remain. A request to cancel
at the final precommit checkpoint is still honored. Once committed, the return value
reports the installation even if a later sync fails; it never reports that nothing
was installed or advises blindly retrying.

New ancestor entries, staged directories and both rename parents are synchronized.
The final parent-chain sync stops at the filesystem boundary and also covers a retry after an earlier interrupted attempt
left empty ancestors. `durabilityConfirmed` means these `fsync` calls returned success;
it is not a simulated power-loss result or an `F_FULLFSYNC` guarantee. If a postcommit
sync fails, `durabilityConfirmed` is false and the diagnostic names the retained prior
tree when replacing. Studio and the bank CLI preserve this distinction. Windows
installation remains TODO: this atomic backend explicitly refuses unsupported platforms.

## Retained directories and recovery

A failed precommit call removes only its own identified staging tree. An uncertain
committed replacement retains the previous version under the `.staging-...` path
reported in its diagnostic. Older crash-left `.staging-*` and `.backup-*` directories
are never automatically deleted; one may contain the only copy of a prior version.

Before recovery, stop installations into this root and preserve copies of the current
version, the retained directory, their receipts and the original signed packages.
Inspect package digest, resource id/version, content identity and trusted signer from
those receipts, then independently verify the original package and installed bytes.
A receipt alone does not establish integrity. Do not rename a retained directory over
a live version or delete directories based only on their names. Recover from the
verified package through an explicit replacement transaction, keeping the saved copies
until that installation and its dependent projects have been checked. Automated
crash-left inventory/recovery tooling and actual power-loss testing remain separate work.

## Remaining standalone notification gap

The standalone menu handlers currently discard the returned durability diagnostic.
The installer service preserves the result, but a committed installation with failed
sync is not yet shown as uncertain in that editor. This is a known caller gap, not
successful standalone acceptance. The next UI increment must surface a nonfatal,
visible committed-install warning, while avoiding a false warning for an already
installed resource whose durability simply was not rechecked.
