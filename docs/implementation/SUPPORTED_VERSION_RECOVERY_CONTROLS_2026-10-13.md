# Supported-version recovery controls

Date: 2026-10-13, Asia/Seoul. Base: `cb3a19a3903ffcd9bb143d808b0ae9868fcf7088`. [Evidence index](SUPPORTED_VERSION_RECOVERY_CONTROLS_2026-10-13.evidence.json) binds one exact tested/base source pair, focused rebuild/test logs and the rebuilt review executable. Review is PENDING.

Independent technical APPROVE for 8b92033e closes all four preceding code/report P3s. One additional P3 asked for complements of its unknown-version probes: damaged fields under recognized schema/policy versions must continue recovering supported data. This adds those controls without changing the already correct library behavior.

- A supported project schema with the same numeric projectId corruption returns ParseError, recovers the predecessor and preserves damaged bytes.
- Each changed journal format, schemaVersion and action is tested under both marker 2 and no marker. Direct verification returns the exact InvariantViolation binding error, recovery returns the predecessor, and each modified journal stays byte-identical.
- All earlier unknown-schema/policy, depth/read-limit, supported corruption, save refusal and review-transition cases remain in the same target and pass.

Focused rebuild exits 0; CTest **1/1 PASS, 22 native cases**, CTest-reported 1.67 seconds. This is test-only work: no libs source/header changed, and those source bytes still match the previously retained full dependent rebuild at 8b92033e. No new full build, full suite, Python execution, physical UI, listening, rights or human acceptance is claimed. The existing implementation passes these controls on their first execution; no invented baseline defect or fix is reported.

This pins the known-version half of the compatibility boundary. It adds no universal future-format migration or archive authentication. Actual source admission still requires genuine disposition/producer/reviewer/mapping/listening/failed-QC policy inputs. Frozen kit/media/null worksheet/stash remain unchanged; .github untouched and Windows TODO retained. Alpha remains 0/20 recorded PASS, External Beta BLOCKED, full development goal ACTIVE.
