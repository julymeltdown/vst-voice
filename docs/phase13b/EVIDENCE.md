# Phase 13B evidence

## Engineering evidence commands

```bash
python3 -m unittest discover -s tests/phase13b -v
python3 scripts/verify_phase13b_contracts.py --root .
python3 scripts/generate_phase13b_evidence.py --root . --output out/phase13b
cmake --build --preset dev
ctest --preset dev --output-on-failure
python3 scripts/verify_master_branch.py --root .
python3 tools/license-auditor/audit.py --root .
git diff --check
git fsck --full
```

## Checked-in evidence

```text
docs/phase13b/evidence/character-development-assets.json
docs/phase13b/evidence/development-content-bundle.json
docs/phase13b/evidence/official-voicebank-01-result.json
docs/phase13b/evidence/character-01-result.json
docs/phase13b/evidence/g5-release-gate.json
docs/phase13b/evidence/phase13b-summary.json
```

The deterministic ZIPs are intentionally marked development-only or blocked.
They are not public release artifacts.

## Baseline verdict

```text
Engineering implementation  PASS after the commands above pass
Official Voicebank 01       BLOCKED
Character 01                BLOCKED
Product G5                  BLOCKED
```

The blocked product verdict is expected. A passing software test does not
create contracts, recordings, trademark clearance, IP assignments or legal
approval. See `MANDATORY_FUTURE_VALIDATION.md` for all required future tests.
