# Project SEAM Public Production Runbook

This runbook orders evidence for one real public candidate. Runtime records live
under `out/public-release/<candidate-lineage>/`; the checked-in acceptance
contracts remain evidence-free and `BLOCKED`.

1. Select one candidate lineage and create
   `out/public-release/<candidate-lineage>/candidate.json` in state `DRAFT`.
2. Restore and audit the same-lineage External Beta candidate. Public work may
   continue only after the predecessor state is `EXTERNAL_BETA_CLOSED`.
3. Freeze source, bank source, public document digests, SBOM, trust policy,
   toolchain, and unsigned payloads exactly once. Compute `FreezeRoot` before
   signing; a payload change creates a new lineage.
4. Sign macOS, Windows, and bank descendants without rebuilding. Compute
   `ArtifactRoot` from `FreezeRoot` and delivered signed hashes.
5. Collect clean-installed macOS and Windows trees, bank identity, the Apple
   Silicon UA matrix, the independent `PW-001` through `PW-020` Windows matrix,
   update lifecycle, support intake, incident drill, rollback, and separate
   terminal-revoke rehearsal evidence.
6. Validate the Windows contract with
   [verify_public_windows_standalone_contract.py](../../scripts/verify_public_windows_standalone_contract.py).
   A UA record cannot satisfy a PW row and a PW record cannot satisfy a UA row.
7. Put every referenced raw evidence record in
   `out/public-release/<candidate-lineage>/archive/`, build its immutable
   manifest, restore it elsewhere, and recompute every file hash.
8. Compute `EvidenceRoot` from `ArtifactRoot`, installed-tree hashes, the
   evidence index, and the restored archive-manifest hash.
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
