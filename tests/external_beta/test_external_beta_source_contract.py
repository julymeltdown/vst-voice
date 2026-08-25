from __future__ import annotations

import importlib.util
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class ExternalBetaSourceContractTests(unittest.TestCase):
    def test_beta_contract_artifacts_and_checked_in_inputs_exist(self) -> None:
        required = (
            "docs/voicebank/BETA_VOICEBANK_ACCEPTANCE.md",
            "docs/voicebank/beta-voicebank-dossier.schema.json",
            "docs/voicebank/beta-voicebank-01-dossier.json",
            "docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json",
            "docs/voicebank/templates/beta-recording-session-log.json",
            "docs/voicebank/templates/beta-retake-closure.json",
            "packaging/voicebanks/beta-voicebank-01.lock.json",
            "docs/product/fixtures/external-beta-song.seam",
            "docs/product/fixtures/external-beta-song-media.lock.json",
            "docs/product/fixtures/external-beta-song-expected.json",
            "tools/external_beta/voicebank_gate.py",
            "tools/external_beta/voicebank_production.py",
            "tools/external_beta/standalone_evidence.py",
            "tools/external_beta/product_soak.py",
            "scripts/run_external_beta_standalone_journey.py",
            "scripts/run_external_beta_product_soak.py",
            "scripts/verify_phase12c_live_contracts.py",
            "scripts/verify_phase12c_evidence.py",
            "docs/phase12c/LIVE_ARTICULATION.md",
            "docs/product/external-beta-standalone-matrix.json",
            "docs/product/external-beta-standalone-record.schema.json",
            "docs/product/external-beta-product-soak.schema.json",
            "docs/product/external-beta-fault-matrix.json",
            "docs/product/external-beta-install-matrix.json",
            "docs/product/external-beta-install-record-template.json",
            "docs/product/external-beta-install-record.schema.json",
            "docs/product/external-beta-host-matrix.json",
            "docs/product/external-beta-host-record-template.json",
            "docs/product/external-beta-host-record.schema.json",
            "tools/external_beta/install_evidence.py",
            "scripts/run_external_beta_install_evidence.py",
            "tools/external_beta/host_evidence.py",
            "scripts/run_external_beta_host_evidence.py",
            "tools/external_beta/evidence_archive.py",
            "tools/external_beta/evidence_audit.py",
            "scripts/run_external_beta_evidence_audit.py",
            "docs/product/external-beta-evidence-archive.schema.json",
            "docs/product/EXTERNAL_BETA_RUNBOOK.md",
            "tools/external_beta/cohort_gate.py",
            "scripts/run_external_beta_cohort_gate.py",
            "docs/product/external-beta-cohort.schema.json",
            "docs/product/external-beta-cohort-record-template.json",
            "tools/external_beta/release_audit.py",
            "scripts/run_external_beta_release_audit.py",
            "docs/product/external-beta-release-audit.schema.json",
            "tools/external_beta/operations.py",
            "scripts/run_external_beta_operation.py",
            "docs/product/external-beta-operation-decision.schema.json",
            "docs/product/external-beta-release-authorization.schema.json",
            "docs/product/external-beta-build-manifest.schema.json",
            "docs/product/EXTERNAL_BETA_RELEASE_AUTHORIZATION.md",
            "docs/product/external-beta-predecessor-state-fixtures.json",
            "libs/seam-distribution/include/seam/distribution/trust_policy.hpp",
            "libs/seam-distribution/src/trust_policy.cpp",
            "libs/seam-distribution/include/seam/distribution/update_manifest.hpp",
            "libs/seam-distribution/src/update_manifest.cpp",
            "libs/seam-distribution/src/sealed_handoff.cpp",
            "libs/seam-distribution/include/seam/distribution/signer_provider.hpp",
            "libs/seam-standalone/include/seam/standalone/update_controller.hpp",
            "libs/seam-standalone/src/update_controller.cpp",
            "tests/test_update_controller.cpp",
            "libs/seam-native-ui/include/seam/native_ui/update_panel.hpp",
            "libs/seam-native-ui/src/update_panel.cpp",
            "tests/test_trust_policy.cpp",
            "tools/external_beta/sign_update_manifest.py",
            "packaging/windows/installer-ownership.json",
            "packaging/macos/installer-ownership.json",
            "tests/phase13a/test_windows_installer_contract.py",
            "tests/phase13a/test_macos_installer_contract.py",
            "tests/external_beta/test_candidate_root.py",
            "docs/beta/EULA.md",
            "docs/beta/PRIVACY.md",
            "docs/beta/USER_MANUAL.md",
            "docs/beta/KNOWN_LIMITATIONS.md",
            "docs/beta/UPDATE_AND_ROLLBACK.md",
            "docs/beta/SUPPORT.md",
            "docs/beta/SECURITY_RESPONSE.md",
            "docs/beta/BETA_TESTER_CHECKLIST.md",
            "docs/manual/EULA.md",
            "tests/external_beta/test_beta_documentation.py",
            "tests/test_update_manifest.cpp",
            "libs/seam-standalone/include/seam/standalone/eula_acceptance.hpp",
            "libs/seam-standalone/src/eula_acceptance.cpp",
            "tests/test_eula_acceptance.cpp",
            "libs/seam-platform/include/seam/platform/application_menu.hpp",
            "libs/seam-platform/include/seam/platform/crash_capture.hpp",
            "libs/seam-platform/src/crash_capture.cpp",
            "libs/seam-platform/src/crash_capture_appkit.mm",
            "libs/seam-platform/src/crash_capture_win32.cpp",
            "libs/seam-platform/src/crash_capture_unavailable.cpp",
            "libs/seam-authoring-runtime/include/seam/authoring/support_bundle.hpp",
            "libs/seam-authoring-runtime/src/support_bundle.cpp",
            "libs/seam-native-ui/include/seam/native_ui/recovery_support_panel.hpp",
            "libs/seam-native-ui/src/recovery_support_panel.cpp",
            "tests/test_crash_recovery.cpp",
            "tests/test_support_bundle.cpp",
            "docs/product/external-beta-support-bundle.schema.json",
            "docs/product/external-beta-documentation.json",
            "tools/phase13a/documentation_contract.py",
            "tests/phase13a/test_offline_documentation.py",
        )
        for relative in required:
            self.assertTrue((ROOT / relative).is_file(), relative)
        dossier = json.loads((ROOT / "docs/voicebank/beta-voicebank-01-dossier.json").read_text(encoding="utf-8"))
        lock = json.loads((ROOT / "packaging/voicebanks/beta-voicebank-01.lock.json").read_text(encoding="utf-8"))
        expected = json.loads((ROOT / "docs/product/fixtures/external-beta-song-expected.json").read_text(encoding="utf-8"))
        self.assertEqual("BLOCKED", dossier["status"])
        self.assertFalse(dossier["official"])
        self.assertFalse(dossier["characterAssociated"])
        self.assertEqual("BLOCKED", lock["status"])
        self.assertEqual("BLOCKED", expected["status"])

    def test_checked_in_inventory_is_valid_for_current_generator(self) -> None:
        module_path = ROOT / "tools/voicebank-script-generator/main.py"
        spec = importlib.util.spec_from_file_location("seam_voicebank_script_generator", module_path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        inventory = json.loads((ROOT / "docs/voicebank/BETA_JAPANESE_CVVC_INVENTORY.json").read_text(encoding="utf-8"))
        self.assertEqual([], module.validate_inventory(inventory))

    def test_external_beta_acceptance_points_eb003_to_beta_gate(self) -> None:
        acceptance = json.loads((ROOT / "docs/product/external-beta-acceptance.json").read_text(encoding="utf-8"))
        requirement = next(item for item in acceptance["requirements"] if item["id"] == "EB-003-beta-bank")
        self.assertEqual("docs/voicebank/beta-voicebank-dossier.schema.json", requirement["gate"])

    def test_external_beta_acceptance_points_eb004_to_install_matrix(self) -> None:
        acceptance = json.loads((ROOT / "docs/product/external-beta-acceptance.json").read_text(encoding="utf-8"))
        requirement = next(item for item in acceptance["requirements"] if item["id"] == "EB-004-signed-install")
        self.assertEqual("docs/product/external-beta-install-matrix.json", requirement["gate"])

    def test_external_beta_acceptance_points_eb006_to_host_matrix(self) -> None:
        acceptance = json.loads((ROOT / "docs/product/external-beta-acceptance.json").read_text(encoding="utf-8"))
        requirement = next(item for item in acceptance["requirements"] if item["id"] == "EB-006-host-matrix")
        self.assertEqual("docs/product/external-beta-host-matrix.json", requirement["gate"])

    def test_external_beta_acceptance_points_eb007_to_archive_schema(self) -> None:
        acceptance = json.loads((ROOT / "docs/product/external-beta-acceptance.json").read_text(encoding="utf-8"))
        requirement = next(item for item in acceptance["requirements"] if item["id"] == "EB-007-provenance-archive")
        self.assertEqual("docs/product/external-beta-evidence-archive.schema.json", requirement["gate"])

    def test_external_beta_acceptance_points_eb008_to_cohort_schema(self) -> None:
        acceptance = json.loads((ROOT / "docs/product/external-beta-acceptance.json").read_text(encoding="utf-8"))
        requirement = next(item for item in acceptance["requirements"] if item["id"] == "EB-008-defect-review")
        self.assertEqual("docs/product/external-beta-cohort.schema.json", requirement["gate"])

    def test_external_beta_acceptance_points_eb001_and_eb002_to_release_schemas(self) -> None:
        acceptance = json.loads((ROOT / "docs/product/external-beta-acceptance.json").read_text(encoding="utf-8"))
        requirements = {item["id"]: item for item in acceptance["requirements"]}
        self.assertEqual("docs/product/external-beta-release-authorization.schema.json", requirements["EB-001-contract"]["gate"])
        self.assertEqual("docs/product/external-beta-build-manifest.schema.json", requirements["EB-002-identity"]["gate"])


if __name__ == "__main__":
    unittest.main()
