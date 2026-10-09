# Resource candidate publication, signing and installation

Publication, packaging and installation are distinct operations. A schema-3
candidate records every payload/evidence file and its hash and byte count, its
canonical resource kind, applicability and external dependencies. A signature
proves publisher authenticity; it does not grant rights, human approval, acoustic
qualification or release eligibility.

| Canonical kind | Payload family | Current operation |
|---|---|---|
| `sample-real`, `sample-procedural` | sample | Publish from reviewed producer generation, package and install |
| `recipe-original` | recipe | Publish declared canonical recipe, package and install as a procedural singer |
| `neural-original` | model | Publish declared payload, package and verify; model installation is refused |

Model packaging is an engineering byte/identity contract, not learned singing or
runtime admission. This increment relaxes schema 3 only for model payload entries
under `graphs/` with role `model-graph`; older readers refuse them. `.onnx` entries
are opaque declared graph bytes; packaging does
not parse ONNX or establish graph validity. Executable and linked payloads remain
refused. Sample and procedural family verifiers explicitly refuse `.onnx` entries,
even in generic signed packages. Both family packers apply that same check before
writing output, preserving an earlier package on refusal. Model candidates reserve root `resource.json` and
cannot supply a runtime registry record. Limits remain 512 MiB per entry, 2 GiB
total payload and 3 GiB per archive. Dictionary and character candidate production are not supplied
by these commands. Preserve their full-scope obligations.

## Exact identity at each step

`publish-sample` returns a candidate digest. `inspect-candidate` reads a candidate
without changing it. Legacy schemas 1/2 retain their version and historical fields,
report no complete dependency set, and cannot pass typed packaging. Re-export from
the retained manifest and pinned producer generation/project digest, with approvals
still in force; do not relabel an old descriptor as schema 3.

`package-candidate CANDIDATE_DIRECTORY CANDIDATE_SHA256 OUTPUT_PACKAGE PRIVATE_KEY`
requires the caller's captured candidate digest in both the CLI and library API. Packaging checks the directory,
signs into private staging, verifies that signed entries exactly match the declared
set, and checks the family manifest against the descriptor. Sample content identity
is recalculated from signed bytes. The final signed package is verified again before
create-new publication. An existing output is never overwritten.

`verify-candidate-package PACKAGE PUBLIC_KEY` checks the signature/trust, descriptor,
exact entry set and family identity. `resourceKind` remains canonical in CLI output;
`payloadFamily` separately reports sample/recipe/model.

`install-candidate PACKAGE PACKAGE_SHA256 INSTALL_ROOT PUBLIC_KEY [--replace]`
requires the package digest reported by packaging and explicit trust. It verifies the
typed descriptor before entering the atomic family installer. The installer pins the
package digest and, for samples, the reviewed content hash before publication.
Generic legacy signed-container installation remains a separate lower-level API;
a generic container is not automatically a typed candidate package.

Studio uses these same checks. It pins the published candidate digest when signing,
retains the returned package identity, and requires that exact candidate/package
pair for installation in the same Studio session. A previously signed package can
be installed through `install-candidate` with its captured digest. Packaging and installation warnings remain visible in Studio
status; cancellation after commit does not claim nothing was written.

## Publication and recovery boundaries

The macOS/Linux packaging implementation uses a random private 0700 staging directory,
held parent/staging identities, create-new rename anchored to held directory handles and identity
checks before cleanup. Redirected parents or staging are refused; unrelated replacement
paths are preserved. A failed attempt may conservatively retain a provisional directory
when ownership can no longer be confirmed. Do not clean crash-left paths by name alone.

Cancellation before commit refuses publication. Directory-sync failure after commit
returns a committed result with `durabilityConfirmed = false` and a diagnostic. The
package is already present: retain and verify it rather than assuming the attempt wrote
nothing. fsync is not a claim of F_FULLFSYNC or measured power-loss survival. The
post-install descriptor recheck is likewise a postcommit disclosure; failure to reconfirm
it reports `descriptorReconfirmed = false`, independently of directory-sync durability,
and does not imply rollback. Studio refuses to admit that result for use. A complete typed partial-success/recovery workflow remains
open.

These are engineering APIs. Windows stays TODO; the new private staging backend refuses
unsupported platforms. Linux source support is not Linux runtime evidence from this Mac.
Current verification and outstanding limits are recorded in the root execution ledger.

## Remaining engineering work

This increment does not complete U14. Model runtime installation, dictionary and
character candidates, qualified production assets and the complete typed
post-publication recovery flow remain open. Studio still uses its existing error
path for a committed install whose catalog scan fails; that is not rollback.

Sample verification currently copies the signed bank into private scratch storage
to recompute its content identity. Repeated verification can be costly for large
banks and has no fine-grained cancellation inside that copy/hash step. Measure and
reduce duplicate passes before claiming large-bank responsiveness. No large-bank
performance result is claimed here.

The explicit CLI `--replace` option retains the family installer's existing atomic
replacement policy. It does not pin the prior content identity and can break songs
that reference that prior content; Studio does not request replacement. A future
replacement workflow needs a captured prior-content identity and recovery UX.
Private staging protects this typed packaging flow; the generic packer's separate
predictable temporary-name behavior and same-owner concurrent payload changes need
separate hardening. Identity checks and directory handles do not establish safety
against an adversary with control of this process or its owned directories.
