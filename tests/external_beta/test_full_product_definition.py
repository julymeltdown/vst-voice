from __future__ import annotations

import hashlib
import copy
import json
import tempfile
import unittest
from pathlib import Path

from tests.external_beta.release_gate_fixtures import candidate
from tools.external_beta import release_gate
from tools.external_beta.full_product_contract_profile import validate_profile
from tools.external_beta.full_product_contract_validation import validate_registry


ROOT = Path(__file__).resolve().parents[2]
CONTRACT = ROOT / "docs/product/full-product-beta-contract.json"


def definition() -> release_gate.JsonObject:
    return release_gate.load_candidate(CONTRACT)


def criterion(contract: release_gate.JsonObject, identifier: str) -> release_gate.JsonObject:
    profile = contract["evaluationProfile"]
    assert isinstance(profile, dict)
    rows = profile["criteria"]
    assert isinstance(rows, list)
    return next(row for row in rows if isinstance(row, dict) and row.get("id") == identifier)


def rebound_errors(contract: release_gate.JsonObject) -> tuple[str, ...]:
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "full-product.json"
        path.write_text(json.dumps(contract), encoding="utf-8")
        acceptance = release_gate.load_candidate(ROOT / "docs/product/external-beta-acceptance.json")
        acceptance["fullProductContract"] = {"locator": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        release = candidate()
        root = release["candidateRoot"]
        assert isinstance(root, dict)
        release["acceptanceContractSha256"] = release_gate.sha256_json(acceptance)
        root["acceptanceContractSha256"] = release["acceptanceContractSha256"]
        root["sha256"] = release_gate.candidate_root_sha256(root)
        return release_gate.evaluate_ready(release, acceptance, archive_verified=True).errors


def resolved_shape(row: release_gate.JsonObject) -> None:
    spec = row["resultSpec"]
    assert isinstance(spec, dict)
    cells = copy.deepcopy(spec["cells"])
    assert isinstance(cells, list)
    reference = {"locator": "schema-fixture.json", "sha256": "a" * 64}
    for cell in cells:
        assert isinstance(cell, dict)
        cell["value"] = {"cpuModel": "fixture-cpu", "logicalCpuCount": 4, "ramBytes": 1024, "osId": "fixture-os", "osVersion": "1", "toolchainId": "fixture-toolchain", "profile": copy.deepcopy(reference)} if cell["valueType"] == "machine-profile" else 1
        cell["bindings"] = {**{key: copy.deepcopy(reference) for key in ("machineProfile", "workload", "resourceMatrix")}, **{key: {"id": "fixture-identity", "version": "1", "sha256": "b" * 64} for key in ("backend", "provider", "precision")}}
    row.update({"status": "RESOLVED", "value": {"schemaVersion": 1, "resultType": spec["resultType"], "cells": cells}, "measurement": copy.deepcopy(reference), "independentReview": copy.deepcopy(reference)})


class FullProductDefinitionTests(unittest.TestCase):
    def test_result_schema_requires_dimensioned_qualification_cells(self) -> None:
        from jsonschema import Draft202012Validator

        schema = release_gate.load_candidate(ROOT / "docs/product/full-product-beta-evidence.schema.json")
        Draft202012Validator.check_schema(schema)
        properties, definitions = schema["properties"], schema["$defs"]
        assert isinstance(properties, dict) and isinstance(definitions, dict)
        empirical = properties["empiricalResults"]
        assert isinstance(empirical, dict)
        validator = Draft202012Validator({**empirical, "$defs": definitions})
        contract = definition()
        profile = contract["evaluationProfile"]
        assert isinstance(profile, dict)
        criteria = profile["criteria"]
        assert isinstance(criteria, list)
        values = {}
        for row in criteria:
            if isinstance(row, dict) and row.get("kind") == "empirical":
                resolved_shape(row)
                values[row["id"]] = row["value"]
        self.assertTrue(validator.is_valid(values))
        values["neural-budgets"]["cells"].pop()
        self.assertFalse(validator.is_valid(values))

    def test_result_schema_requires_continuity_artifacts_and_exact_case_checks(self) -> None:
        from jsonschema import Draft202012Validator

        schema = release_gate.load_candidate(ROOT / "docs/product/full-product-beta-evidence.schema.json")
        definitions = schema["$defs"]
        assert isinstance(definitions, dict)
        check_schema = definitions["checkResult"]
        assert isinstance(check_schema, dict)
        validator = Draft202012Validator({**check_schema, "$defs": definitions})
        reference = {"locator": "shape-only.json", "sha256": "a" * 64}
        record = {"id": "fp.check.chunk-continuity.v1", "protocolSha256": "b" * 64, "rawEvidence": [reference]}
        self.assertFalse(validator.is_valid(record))
        record["continuity"] = {key: copy.deepcopy(reference) for key in ("wholeRender", "chunkedRender", "partitionManifest", "phonemeAlignment", "modulationPhase", "neighborContextChange", "dependencyInvalidation")}
        self.assertTrue(validator.is_valid(record))
        record["continuity"].pop("modulationPhase")
        self.assertFalse(validator.is_valid(record))
        branches = schema["properties"]["cases"]["items"]["allOf"]
        mapped = {branch["if"]["properties"]["id"]["const"]: branch["then"]["properties"]["observations"]["items"]["allOf"][1]["properties"]["checkResults"]["items"]["properties"]["id"]["enum"] for branch in branches}
        for case in definition()["cases"]:
            self.assertEqual(case["workload"]["checkIds"], mapped[case["id"]])

    def test_counterbalancing_independence_song_completion_and_bounce_cannot_be_waived(self) -> None:
        changes = (("assisted-comparison", "design", "ONE_PREPARED_DEMO"), ("independent-review", "producerReviewerRelation", "SAME_PERSON"), ("producer-acceptance", "songExtent", "PREPARED_EXCERPT"), ("host-acceptance", "forbiddenSuccessfulBounceStates", []))
        for identifier, key, replacement in changes:
            with self.subTest(identifier=identifier):
                contract = definition()
                protocols = contract["protocolCatalog"]
                assert isinstance(protocols, dict)
                protocol = protocols[identifier]
                assert isinstance(protocol, dict)
                constraints = protocol["constraints"]
                assert isinstance(constraints, dict)
                constraints[key] = replacement
                errors = rebound_errors(contract)
                self.assertTrue(any(identifier in error and "protocol" in error for error in errors), errors)
                self.assertFalse(any("content digest" in error for error in errors))

    def test_each_empirical_kind_accepts_its_typed_shape_without_authorizing_go(self) -> None:
        names = ("acoustic-boundaries", "expression-tolerances", "pronunciation-scoring", "identity-rubric", "generation-budgets", "neural-budgets", "resource-limits", "cancellation-budgets", "reference-machines", "source-volume", "reproducibility-tolerances")
        for identifier in names:
            with self.subTest(identifier=identifier):
                contract = definition()
                resolved_shape(criterion(contract, identifier))
                result = validate_profile(contract)
                self.assertEqual((), result.errors)
                self.assertTrue(result.pending)
                self.assertTrue(any("semantic validator unavailable" in error for error in rebound_errors(contract)))

    def test_empirical_grid_rejects_missing_duplicate_unknown_dimensions_and_bindings(self) -> None:
        for mutation in ("missing", "duplicate", "unknown", "unit", "comparison", "platform", "cache", "provider", "precision"):
            with self.subTest(mutation=mutation):
                contract = definition()
                row = criterion(contract, "neural-budgets")
                resolved_shape(row)
                value = row["value"]
                assert isinstance(value, dict)
                cells = value["cells"]
                assert isinstance(cells, list)
                first = cells[0]
                assert isinstance(first, dict)
                if mutation == "missing":
                    cells.pop()
                elif mutation == "duplicate":
                    cells[-1] = copy.deepcopy(first)
                elif mutation == "unknown":
                    first["id"] = "unknown-cell"
                elif mutation in {"platform", "cache"}:
                    dimensions = first["dimensions"]
                    assert isinstance(dimensions, dict)
                    dimensions[mutation] = "wrong-dimension"
                elif mutation in {"provider", "precision"}:
                    bindings = first["bindings"]
                    assert isinstance(bindings, dict)
                    bindings.pop(mutation)
                else:
                    first[mutation] = "seconds" if mutation == "unit" else "minimum"
                errors = validate_profile(contract).errors
                self.assertTrue(any("neural-budgets" in error for error in errors), errors)

    def test_expression_grid_requires_each_control_and_bound(self) -> None:
        contract = definition()
        row = criterion(contract, "expression-tolerances")
        resolved_shape(row)
        value = row["value"]
        assert isinstance(value, dict)
        cells = value["cells"]
        assert isinstance(cells, list)
        self.assertEqual(52, len(cells))
        cells.pop()
        self.assertTrue(any("dimension coverage" in error for error in validate_profile(contract).errors))

    def test_continuity_protocol_requires_partition_phase_and_neighbor_invalidation(self) -> None:
        for key, replacement in (("partitionLocations", []), ("requiredObservations", ["PHONEME_COUNT"]), ("phaseAuthority", "RESET_PER_CHUNK"), ("requiredInvalidation", [])):
            with self.subTest(key=key):
                contract = definition()
                checks = contract["checkCatalog"]
                assert isinstance(checks, dict)
                check = checks["fp.check.chunk-continuity.v1"]
                assert isinstance(check, dict)
                check[key] = replacement
                errors = validate_registry(contract).errors
                self.assertTrue(any("chunk-continuity" in error for error in errors), errors)

    def test_freeform_rule_cannot_replace_mandatory_protocol(self) -> None:
        for identifier in ("assisted-comparison", "producer-acceptance", "host-acceptance", "rights-acceptance"):
            with self.subTest(identifier=identifier):
                contract = definition()
                row = criterion(contract, identifier)
                row["rule"] = "A prepared demo and producer self-review are sufficient."
                errors = rebound_errors(contract)
                self.assertTrue(any(identifier in error and "protocol" in error for error in errors), errors)
                self.assertFalse(any("content digest" in error for error in errors))

    def test_scalar_cannot_resolve_a_dimensioned_empirical_budget(self) -> None:
        for identifier in ("expression-tolerances", "generation-budgets", "neural-budgets"):
            with self.subTest(identifier=identifier):
                contract = definition()
                row = criterion(contract, identifier)
                row.update({"status": "RESOLVED", "value": 123, "measurement": {"locator": "measurement.json", "sha256": "a" * 64}, "independentReview": {"locator": "review.json", "sha256": "b" * 64}})
                errors = validate_profile(contract).errors
                self.assertTrue(any(identifier in error and "typed result" in error for error in errors), errors)

    def test_continuity_checks_are_required_for_melody_vibrato_and_language_context(self) -> None:
        identifiers = ("R1.cross-note-melody", "R1.melisma-articulation", "R2.vibrato", "R7.edit-reconciliation", "R7.ja-pronunciation", "R7.en-pronunciation", "R7.ko-pronunciation")
        for identifier in identifiers:
            with self.subTest(identifier=identifier):
                contract = definition()
                cases = contract["cases"]
                assert isinstance(cases, list)
                row = next(case for case in cases if isinstance(case, dict) and case.get("id") == identifier)
                workload = row["workload"]
                assert isinstance(workload, dict)
                workload["checkIds"] = []
                errors = validate_registry(contract).errors
                self.assertTrue(any(identifier in error and "checkIds" in error for error in errors), errors)


if __name__ == "__main__":
    unittest.main()
