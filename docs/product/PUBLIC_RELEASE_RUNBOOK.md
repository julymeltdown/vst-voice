# Project SEAM Public Production Runbook

This runbook orders evidence for one real public candidate. Runtime records live
under `out/public-release/<candidate-lineage>/`; the checked-in acceptance
contracts remain evidence-free and `BLOCKED`.

1. Select one candidate lineage and create
   `out/public-release/<candidate-lineage>/candidate.json` in state `DRAFT`.
2. Restore and audit the same-lineage External Beta candidate. A READY audit
   proves only `EXTERNAL_BETA_READY`; public activation requires a reproduced
   `EXTERNAL_BETA_CLOSED` audit including completed cohort evidence.
3. Freeze source, bank source, public document digests, SBOM, trust policy,
   toolchain, and unsigned payloads exactly once. Compute `FreezeRoot` before
   signing; a payload change creates a new lineage.
4. Sign macOS, Windows, and bank descendants without rebuilding. Compute
   `ArtifactRoot` from `FreezeRoot` and delivered signed hashes.
5. Collect clean-installed macOS and Windows trees, bank identity, the Apple
   Silicon UA matrix, the independent `PW-001` through `PW-020` Windows matrix,
   update lifecycle, hash-bound support intake/withdrawal/deletion, incident drill, rollback, and separate
   terminal-revoke rehearsal evidence.
6. Validate the Windows contract with
   [verify_public_windows_standalone_contract.py](../../scripts/verify_public_windows_standalone_contract.py).
   A UA record cannot satisfy a PW row and a PW record cannot satisfy a UA row.
7. Put every referenced raw evidence record in
   `out/public-release/<candidate-lineage>/archive/`, build its immutable
   manifest, restore it elsewhere, and recompute every file hash.
8. Compute `EvidenceRoot` from `ArtifactRoot`, installed-tree hashes, the
   evidence index, the restored archive-manifest hash, and `externalBetaSha256`
   (the canonical JSON digest of the complete `externalBeta` replay object).
9. Only after `EvidenceRoot` is terminal may the distinct independent release,
   content-rights, security/privacy, macOS, Windows, musician, accessibility,
   and archive reviewers sign approval envelopes. The release manager cannot
   occupy a reviewer slot and instead signs the separate operation envelope.
   Every policy key binds its key ID, role, and signer or actor identity;
   arbitrary signer strings are rejected. Approval envelopes stay
   outside `FreezeRoot`, `ArtifactRoot`, and `EvidenceRoot`; the final operation
   envelope references their hashes, avoiding an approval digest cycle.
10. Run [run_public_release_audit.py](../../scripts/run_public_release_audit.py):

    ```bash
    python3 scripts/run_public_release_audit.py \
      --candidate out/public-release/<candidate-lineage>/candidate.json \
      --archive-manifest out/public-release/<candidate-lineage>/archive-manifest.json \
      --archive-root out/public-release/<candidate-lineage>/archive \
      --state PUBLIC_ACTIVE
    ```

11. Publish the signed `PUBLIC_ACTIVE` operation only when the restored audit
    passes with no blocked category. A missing machine, bank, endpoint, target
    row, raw file, reviewer, or operation record remains `NOT_RUN` or `BLOCKED`.
12. A signed `DISTRIBUTION_PAUSED` operation stops new acquisition and normal
    updates. Resume requires a complete new quorum signed after the pause.
    `SUPERSEDED` ends acquisition for that lineage. Terminal `REVOKED` is
    irreversible; recovery requires a new candidate root.

The public audit validates contracts and restored bytes; it never collects or
manufactures evidence. Test fixtures may prove the validator's behavior but
are never eligible release records.

The predecessor and signed decision shapes are defined by
[public-release-replay.schema.json](public-release-replay.schema.json).
`externalBeta` carries the predecessor root ID/digest, its same-lineage mapping,
the exact `acceptanceContract` file reference, and `releaseAudit` containing
candidate/archive-manifest file references plus the restored archive directory.
Include these files, the Beta contract, full-product report and its transitive
raw inputs in the restored public archive. All references use `locator` and
`sha256`; predecessor paths are relative to the public archive root. Beta
full-product contract references resolve relative to the restored Beta root.

The trusted public acceptance contract can pin a reviewed Beta acceptance
revision with `externalBetaAcceptanceContractSha256` (exact file digest). When
absent, the current canonical Beta acceptance file is required. This selects a
policy revision, not a synthetic bypass: the complete fixed registry, criteria,
archive anchor, EB-001–EB-009 and required cohort checks still execute. Changes
to this pin require a new public contract/freeze identity and affected approval.

[run_public_release_operation.py](../../scripts/run_public_release_operation.py)
accepts `--snapshot`, `--decision`, `--acceptance-contract` and optional `--output`.
Advancing decisions and RESUME include `acceptanceContractSha256` and the same
hash-bound `releaseAudit` inputs, signed with all other decision fields. Relative
operation paths are based on the decision file's directory; keep the restored
archive below that directory. The candidate must identify the target state and
the snapshot's exact lineage/EvidenceRoot. Every advancement replays the full
audit; a prior receipt, `gatePassed`, or `archiveVerified` flag cannot authorize
it. PAUSE, SUPERSEDE and REVOKE retain their existing signed authority checks and
do not require damaged evidence to be repaired before stopping distribution.

Direct Python gate calls also require `archive_manifest` and `evidence_root`.
The old `archive_verified` argument is accepted for source compatibility but
does not establish verification. The gate CLI supports `--archive-manifest`
and `--archive-root` for the same real replay path as the audit CLI.
