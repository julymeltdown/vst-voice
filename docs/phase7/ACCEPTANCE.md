# Phase 7 Acceptance — Signed `.seambank` Distribution

- [x] Ed25519 key generation, signing, and verification use the system OpenSSL 3 Crypto API.
- [x] Private and public key files carry a derived SHA-256 key ID and are validated when loaded.
- [x] `.seambank` v1 is deterministic, lexicographically ordered, and data-only.
- [x] Package signatures bind header, table, payload, and embedded public key.
- [x] Every entry has an independent SHA-256 checksum.
- [x] Traversal, hidden paths, symlinks, scripts, executables, duplicates, overlap, and resource-limit violations are rejected.
- [x] `manifest.json` and all manifest-referenced audio files are verified.
- [x] A valid but untrusted signer cannot install a package.
- [x] Installation uses staging, receipt generation, replacement rollback, and final package-digest verification.
- [x] CLI supports key generation, pack, verify, list, and install.
- [x] The Phase 7 demo generates, verifies, and installs a signed synthetic voicebank.
- [x] Tamper and trust-policy regression tests are included.
