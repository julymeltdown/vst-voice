# Validate provenance encoding before creating donor output

Date: 2026-10-15, Asia/Seoul. **Implemented and locally verified the early-encoding issue identified in the exact `f5d967da` review; independent closure pending for this follow-up.** Source base: `5b406fa1e30d3ce85904735b539d3a04f30d11a9`.

JSON permits escaped lone surrogate code points. A nonempty attribution or sourceUrl containing one passed plan validation, but building its UTF-8 notice failed after output.mkdir(). The resulting INCOMPLETE directory blocked a corrected retry. This contradicted the existing validate-before-output contract.

The existing change-notice/attribution byte computation is moved unchanged ahead of output creation in prepare_phone_donors.py. Valid notice, JSON and audio bytes and all policies remain unchanged. Invalid text now fails before any output directory exists. Real write failures after creation still preserve INCOMPLETE and are not automatically deleted.

The new regression first failed against unchanged production because the directory existed after the expected encoding error. That baseline stops at the first high-surrogate attribution case. The final case covers high and low lone surrogates in both provenance fields, an actual Python CLI exit-2 refusal without a traceback or directory, and a corrected same-path preparation plus integrity preflight. Both source-intake suites pass **38 direct Python methods** (23 phone-donor/15 sustain); no native or CTest execution is claimed.

The [evidence index](PHONE_DONOR_EARLY_ENCODING_2026-10-15.evidence.json) binds **14 artifacts/6 preserved files**, including original/final source, exact patches, direct execution receipts/output and a read-only verifier. It rehashes artifacts/pins, checks the baseline failure/final counts and proves the production difference is only an unchanged block moved before mkdir. `--commit SHA` binds both final modified files. It does not rerun tests or archive all dependencies.

The [packet-integrity unit](PHONE_DONOR_PACKET_INTEGRITY_2026-10-15.md) remains under separate review. Durable bank admission, genuine actors, source-rights/EULA disposition, acoustic/QC/listening and full installed/platform/DAW acceptance remain open. No existing packet or original recording is changed. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**, goal **ACTIVE**; `.github` and Windows TODO untouched.
