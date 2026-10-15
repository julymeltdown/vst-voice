# Remove duplicated external Beta Python discovery

Date: 2026-10-15, Asia/Seoul. **PASS: one redundant registration removed without discovery coverage loss.** Source base: `05fe461d2ab280cf288beed785fc5757f3d503f7`.

The root CMake registration `seam_external_beta_python_contract_tests` used explicit `test_*.py`; Phase 13A's `seam_external_beta_contract_tests` used unittest's default `test*.py` with verbose output. Both loaded exactly the same **443 unique test IDs**. The full run at `3b3388df` spent 71.57 and 71.46 seconds on these two registrations. Repeated execution added no distinct coverage in that inventory.

Removed the duplicate `add_test` and its root property-list entry from [CMakeLists.txt](../../CMakeLists.txt). [Phase 13A](../../phase13a/CMakeLists.txt) remains included and owns the surviving registration and its custom contract target. Use `ctest --test-dir build/release -R '^seam_external_beta_contract_tests$' --output-on-failure` for the complete external Beta Python suite. Repository search found no source or CI consumer of the removed name; historical evidence is preserved.

The full dependent Release rebuild passed in 24.780763500 seconds. CTest's before/after inventories changed from **240 to 239 registrations**: exactly the duplicate disappeared, and all remaining names, commands and properties were identical. Discovery-only enumeration ran no test methods and produced identical explicit/default sorted ID lists. The surviving suite then passed every one of those **443 cases**, individually matched to verbose success lines. The existing CMake source-commit identity lane passed **17 cases**. Both focused CTest lanes passed in 78.571411042 seconds with stable source/Git observations.

The [evidence index](PYTHON_CTEST_DEDUPLICATION_2026-10-15.evidence.json) binds **65 artifacts and 6 preserved files**, including inventories, ID parity, all 47 selected Python test sources, baseline/final CMake files, build/test output and a read-only verifier. `--commit SHA` binds final selected files to an integrated commit. Transitive modules, interpreter/environment and all native executables are not archived. The full **239-lane suite was not rerun**, so total wall-time saving is not freshly measured; the removed duplicate's prior 71.57-second cost is the measured reference.

No case, assertion, source approval or canonical acceptance requirement was weakened. Independent review of this registration unit is pending. Alpha **0/20**, U45 **HELD**, Beta **BLOCKED**, full goal **ACTIVE**; `.github` and Windows TODO untouched.
