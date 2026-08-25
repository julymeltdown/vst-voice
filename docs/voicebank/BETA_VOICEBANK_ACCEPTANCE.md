# External Beta Voicebank acceptance gate

The External Beta uses one rights-cleared, non-official voicebank. This gate is
separate from the GA-only Official Voicebank 01 gate in
`OFFICIAL_VOICEBANK_ACCEPTANCE.md` and from the Phase 13B public-domain
fixtures. Passing this gate never changes the Phase 13B result and Phase 13B
does not substitute for these Beta-bank rows.

The accepted dossier must identify the exact `(voicebankId, version,
contentSha256)` package, signed entry manifest, delegated bank-purpose key,
root policy epoch, installed provenance tree, source and derived asset hashes,
deterministic inventory, recording sessions, marker/pitch QA, coverage,
renderer listening, hostile-package validation, clean-install receipt, and
canonical reference-song receipt.

Rights approval is public only as a redacted approval hash plus reviewer,
date, scope, territory, and explicit permissions for recording/source use,
transformation, redistribution in a local singing voicebank, and end-user
rendered audio. Private contracts, raw recordings, and personal data stay
outside the repository. Provider identity disclosure is recorded separately
from rights validity.

The gate rejects `official=true`, `official.voice.01`, character-associated
content, missing or ambiguous rights, placeholder evidence, evidence outside
the approved evidence root, hash mismatches, cross-purpose signatures, stale
or replayed trust epochs, self-authorized delegated-key changes, linked
installed leaves, and mutable receipt-only trust claims.

The checked-in Beta dossier is intentionally `BLOCKED`: it is a contract and
lock template, not a claim that a real performer, package, or release evidence
exists. The real `.seambank` and private rights records are external release
inputs.
