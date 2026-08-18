# Phase 13B — Product content and IP release gates

Phase 13B implements the engineering system that validates Official Voicebank 01
and Character 01 release evidence. It produces deterministic development assets
and a development-only content bundle. It does not create performer contracts,
recording sessions, trademark clearance, IP assignments or legal approvals.

Canonical files:

```text
Official Voicebank dossier  official-voicebank-01-dossier.json
Character dossier           character-01-dossier.json
Product licence dossier     product-release-dossier.json
Mandatory matrix            mandatory-validation-matrix.json
Acceptance                  ACCEPTANCE.md
```

Run:

```bash
python3 -m unittest discover -s tests/phase13b -v
python3 scripts/verify_phase13b_contracts.py --root .
python3 scripts/generate_phase13b_evidence.py --root . --output out/phase13b
```
