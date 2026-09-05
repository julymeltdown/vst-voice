from __future__ import annotations

import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tests.external_beta.cohort_fixtures import closed_cohort
from tests.external_beta.release_gate_fixtures import candidate
from tools.external_beta import release_gate
from tools.external_beta.full_product_contract_profile import validate_profile
from tools.external_beta.full_product_contract_validation import validate_registry

ROOT = Path(__file__).resolve().parents[2]
FULL_CONTRACT = ROOT / "docs/product/full-product-beta-contract.json"


def bound_contract_candidate(contract: release_gate.JsonObject) -> release_gate.JsonObject:
    result = candidate()
    result["acceptanceContractSha256"] = release_gate.sha256_json(contract)
    root = result["candidateRoot"]
    assert isinstance(root, dict)
    root["acceptanceContractSha256"] = result["acceptanceContractSha256"]
    root["sha256"] = release_gate.candidate_root_sha256(root)
    return result


def contract_with_reference(path: Path) -> release_gate.JsonObject:
    contract = release_gate.load_candidate(ROOT / "docs/product/external-beta-acceptance.json")
    contract["fullProductContract"] = {"locator": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    return contract


class FullProductAdmissionTests(unittest.TestCase):
    def test_legacy_eight_pass_rows_cannot_reach_ready_with_verified_archive(self) -> None:
        # Given the actual previously passing legacy fixture, including its archive.
        legacy = candidate()
        self.assertEqual(8, len(legacy["requirements"]))
        # When the complete READY gate evaluates it.
        result = release_gate.evaluate_ready(legacy, archive_verified=True)
        # Then EB-009 is the admission blocker, without an incidental archive error.
        self.assertFalse(result.passed)
        self.assertIn("EB-009-full-product", result.blocked_ids)
        self.assertFalse(any("archive audit" in error for error in result.errors))

    def test_forged_ninth_pass_row_cannot_reach_ready_or_closed(self) -> None:
        # Given all legacy proof and a fabricated full-product summary.
        forged = candidate()
        forged["cohort"] = closed_cohort()
        records = forged["evidence"]
        requirements = forged["requirements"]
        assert isinstance(records, list) and isinstance(requirements, dict)
        record = copy.deepcopy(records[0])
        assert isinstance(record, dict)
        record.update({"recordId": "forged-full-product", "requirementId": "EB-009-full-product"})
        records.append(record)
        requirements["EB-009-full-product"] = {"status": "PASS", "evidenceRecordIds": ["forged-full-product"]}
        for state in ("EXTERNAL_BETA_READY", "EXTERNAL_BETA_CLOSED"):
            with self.subTest(state=state):
                # When either production promotion boundary evaluates it.
                result = release_gate.evaluate_gate(forged, state, archive_verified=True)
                # Then unimplemented semantic validation remains an explicit error.
                self.assertFalse(result.passed)
                self.assertIn("EB-009-full-product", result.blocked_ids)
                self.assertTrue(any("semantic validator unavailable" in error for error in result.errors))


class FullProductContractIntegrityTests(unittest.TestCase):
    def test_canonical_definition_is_valid_but_has_unearned_final_criteria(self) -> None:
        full = release_gate.load_candidate(FULL_CONTRACT)
        registry = validate_registry(full)
        profile = validate_profile(full)
        self.assertTrue(registry.passed, registry.errors)
        self.assertEqual(83, len(registry.case_ids))
        self.assertEqual((), profile.errors)
        self.assertIn("neural-budgets", profile.pending)
        self.assertIn("resource matrix", profile.pending)

    def test_rebound_acceptance_cannot_omit_the_content_digest(self) -> None:
        for reference in (None, str(FULL_CONTRACT), {"locator": str(FULL_CONTRACT)}):
            with self.subTest(reference=reference):
                contract = contract_with_reference(FULL_CONTRACT)
                contract["fullProductContract"] = reference
                result = release_gate.evaluate_ready(bound_contract_candidate(contract), contract, archive_verified=True)
                self.assertTrue(any("full-product contract reference" in error for error in result.errors), result.errors)

    def test_same_path_contract_replacement_is_rehashed_even_after_outer_rebinding(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "contract.json"
            path.write_bytes(FULL_CONTRACT.read_bytes())
            contract = contract_with_reference(path)
            path.write_bytes(path.read_bytes() + b"\n")
            result = release_gate.evaluate_ready(bound_contract_candidate(contract), contract, archive_verified=True)
            self.assertTrue(any("full-product contract content digest" in error for error in result.errors), result.errors)
            self.assertFalse(any("candidate acceptance contract digest" in error for error in result.errors))

    def test_changed_full_contract_invalidates_old_candidate_identity(self) -> None:
        contract = contract_with_reference(FULL_CONTRACT)
        old_candidate = bound_contract_candidate(contract)
        reference = contract["fullProductContract"]
        assert isinstance(reference, dict)
        reference["sha256"] = "0" * 64
        result = release_gate.evaluate_ready(old_candidate, contract, archive_verified=True)
        self.assertTrue(any("candidate acceptance contract digest" in error for error in result.errors), result.errors)
        self.assertTrue(any("full-product contract content digest" in error for error in result.errors), result.errors)

    def test_registry_rejects_missing_duplicate_unknown_and_deferred_rows(self) -> None:
        mutations = ("missing", "duplicate", "unknown", "deferred", "not-applicable")
        for section in ("requirements", "cases", "workPackages"):
            for mutation in mutations:
                with self.subTest(section=section, mutation=mutation), tempfile.TemporaryDirectory() as directory:
                    full = release_gate.load_candidate(FULL_CONTRACT)
                    rows = full[section]
                    assert isinstance(rows, list)
                    first = rows[0]
                    assert isinstance(first, dict)
                    if mutation == "missing":
                        rows.pop()
                    elif mutation == "duplicate":
                        rows.append(copy.deepcopy(first))
                    elif mutation == "unknown":
                        first["id"] = "UNKNOWN-substitute"
                    elif mutation == "deferred":
                        first["beforeBetaGO"] = False
                    else:
                        first["status"] = "NOT_APPLICABLE"
                    path = Path(directory) / "contract.json"
                    path.write_text(json.dumps(full), encoding="utf-8")
                    contract = contract_with_reference(path)
                    result = release_gate.evaluate_ready(bound_contract_candidate(contract), contract, archive_verified=True)
                    self.assertTrue(any(f"full-product {section}" in error for error in result.errors), result.errors)

    def test_case_dimensions_and_review_roles_cannot_be_empty(self) -> None:
        for dimension in ("language", "resource", "backend", "platform", "host", "independentReviewRoles"):
            with self.subTest(dimension=dimension), tempfile.TemporaryDirectory() as directory:
                full = release_gate.load_candidate(FULL_CONTRACT)
                rows = full["cases"]
                assert isinstance(rows, list)
                first = rows[0]
                assert isinstance(first, dict)
                dimensions = first if dimension == "independentReviewRoles" else first["dimensions"]
                assert isinstance(dimensions, dict)
                dimensions[dimension] = []
                path = Path(directory) / "contract.json"
                path.write_text(json.dumps(full), encoding="utf-8")
                contract = contract_with_reference(path)
                result = release_gate.evaluate_ready(bound_contract_candidate(contract), contract, archive_verified=True)
                self.assertTrue(any(dimension in error for error in result.errors), result.errors)

    def test_unresolved_profile_and_resource_matrix_block_final_acceptance(self) -> None:
        contract = contract_with_reference(FULL_CONTRACT)
        result = release_gate.evaluate_ready(bound_contract_candidate(contract), contract, archive_verified=True)
        self.assertTrue(any("unresolved final criteria" in error for error in result.errors), result.errors)
        self.assertTrue(any("resource matrix" in error for error in result.errors), result.errors)

    def test_case_cannot_replace_its_proof_with_unrelated_criteria_or_artifacts(self) -> None:
        for field in ("criteriaIds", "rawArtifactKinds"):
            with self.subTest(field=field), tempfile.TemporaryDirectory() as directory:
                full = release_gate.load_candidate(FULL_CONTRACT)
                rows = full["cases"]
                assert isinstance(rows, list)
                first = rows[0]
                assert isinstance(first, dict)
                if field == "criteriaIds":
                    first[field] = ["rights-acceptance"]
                else:
                    workload = first["workload"]
                    assert isinstance(workload, dict)
                    workload[field] = ["slider-screenshot", "independent-review"]
                path = Path(directory) / "contract.json"
                path.write_text(json.dumps(full), encoding="utf-8")
                contract = contract_with_reference(path)
                result = release_gate.evaluate_ready(bound_contract_candidate(contract), contract, archive_verified=True)
                self.assertTrue(any(field in error for error in result.errors), result.errors)

    def test_fixed_floors_cannot_be_relaxed_by_rebinding_contract(self) -> None:
        for identifier, replacement in (("phrases-per-language", 59), ("song-count", 2), ("creator-count", 4), ("pitch-median", 31), ("pitch-within-50", 89), ("timing-displacement", 2), ("classical-small-edit-p95", 501), ("legacy-preview-p95", 500)):
            with self.subTest(identifier=identifier), tempfile.TemporaryDirectory() as directory:
                full = release_gate.load_candidate(FULL_CONTRACT)
                profile = full["evaluationProfile"]
                assert isinstance(profile, dict)
                criteria = profile["criteria"]
                assert isinstance(criteria, list)
                row = next(item for item in criteria if isinstance(item, dict) and item.get("id") == identifier)
                assert isinstance(row, dict)
                row["value"] = replacement
                path = Path(directory) / "contract.json"
                path.write_text(json.dumps(full), encoding="utf-8")
                contract = contract_with_reference(path)
                result = release_gate.evaluate_ready(bound_contract_candidate(contract), contract, archive_verified=True)
                self.assertTrue(any(identifier in error and "floor" in error for error in result.errors), result.errors)

    def test_closed_evidence_schema_declares_and_requires_full_product_report(self) -> None:
        from jsonschema import Draft202012Validator

        schema = release_gate.load_candidate(ROOT / "docs/product/external-beta-evidence-record.schema.json")
        Draft202012Validator.check_schema(schema)
        validator = Draft202012Validator(schema)
        records = candidate()["evidence"]
        assert isinstance(records, list)
        record = copy.deepcopy(records[0])
        assert isinstance(record, dict)
        record.update({"schemaVersion": 1, "requirementId": "EB-009-full-product"})
        self.assertFalse(validator.is_valid(record))
        record["fullProductReport"] = {"locator": "archive/full-product.json", "sha256": "a" * 64}
        self.assertEqual([], list(validator.iter_errors(record)))
        reference = record["fullProductReport"]
        assert isinstance(reference, dict)
        reference.pop("sha256")
        self.assertFalse(validator.is_valid(record))

    def test_typed_evidence_schema_is_valid_and_forbids_unknown_fields(self) -> None:
        from jsonschema import Draft202012Validator

        schema = release_gate.load_candidate(ROOT / "docs/product/full-product-beta-evidence.schema.json")
        Draft202012Validator.check_schema(schema)
        properties = schema["properties"]
        assert isinstance(properties, dict)
        requirements = properties["requirements"]
        assert isinstance(requirements, dict)
        validator = Draft202012Validator(requirements)
        rows = [{"id": f"R{number}", "status": "NOT_RUN", "caseIds": requirement["caseIds"]} for number, requirement in enumerate(release_gate.load_candidate(FULL_CONTRACT)["requirements"], 1)]
        self.assertTrue(validator.is_valid(rows))
        for mutation in ("duplicate", "unknown", "deferred", "not-applicable", "missing"):
            with self.subTest(mutation=mutation):
                changed = copy.deepcopy(rows)
                if mutation == "duplicate":
                    changed[-1] = copy.deepcopy(changed[0])
                elif mutation == "unknown":
                    changed[0]["id"] = "R999"
                elif mutation == "missing":
                    changed.pop()
                else:
                    changed[0]["status"] = "DEFERRED" if mutation == "deferred" else "NOT_APPLICABLE"
                self.assertFalse(validator.is_valid(changed))


if __name__ == "__main__":
    unittest.main()
