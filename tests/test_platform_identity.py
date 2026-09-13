from __future__ import annotations

import json
import unittest
from pathlib import Path

from tools.external_beta.full_product_contract_registry import (
    HOST_TUPLES,
    PLATFORMS as CONTRACT_PLATFORMS,
)
from tools.external_beta.host_evidence import PLATFORMS as HOST_EVIDENCE_PLATFORMS
from tools.external_beta.install_evidence import PLATFORMS as INSTALL_EVIDENCE_PLATFORMS
from tools.external_beta.product_soak import PLATFORMS as PRODUCT_SOAK_PLATFORMS
from tools.phase13a.payload_surfaces import PayloadPlatform
from tools.phase13a.update_contract import PLATFORMS as UPDATE_PLATFORMS
from tools.platform_identity import (
    PlatformIdentityError,
    deployment_platform,
    deployment_platform_for_host,
    deployment_platforms,
    host_platforms,
    identity_for_deployment,
    product_contract_platform,
    product_contract_platforms,
)


ROOT = Path(__file__).resolve().parents[1]


class PlatformIdentityTests(unittest.TestCase):
    def test_windows_deployment_name_maps_to_the_contract_spelling(self) -> None:
        self.assertEqual("windows-x86_64", product_contract_platform("windows-x64"))
        self.assertEqual("macos-arm64", product_contract_platform("macos-arm64"))
        self.assertEqual(
            "windows-x86_64", product_contract_platform(PayloadPlatform.WINDOWS_X64)
        )
        self.assertNotEqual("windows-x64", product_contract_platform("windows-x64"))

    def test_contract_spelling_is_refused_as_a_deployment_platform(self) -> None:
        # The two namespaces share no arithmetic: the contract spelling must not be
        # accepted where a payload or update descriptor declares its platform.
        with self.assertRaises(PlatformIdentityError):
            product_contract_platform("windows-x86_64")
        with self.assertRaises(PlatformIdentityError):
            identity_for_deployment("windows-x86_64")
        self.assertNotIn("windows-x86_64", deployment_platforms())
        self.assertEqual("windows-x64", deployment_platform("windows-x86_64"))

    def test_deployment_only_platform_has_no_contract_identity(self) -> None:
        self.assertIn("linux-x64", deployment_platforms())
        with self.assertRaises(PlatformIdentityError):
            product_contract_platform("linux-x64")
        with self.assertRaises(PlatformIdentityError):
            deployment_platform_for_host("linux", "arm64")

    def test_unknown_and_malformed_platforms_are_refused(self) -> None:
        for value in ("", "windows", "win32-x64", "WINDOWS-X64", "macos-arm64 ", None, 64):
            with self.subTest(namespace="deployment", value=value):
                with self.assertRaises(PlatformIdentityError):
                    product_contract_platform(value)  # type: ignore[arg-type]
        for value in ("", "macos-x86_64", "windows-arm64", "windows-x64 ", None):
            with self.subTest(namespace="contract", value=value):
                with self.assertRaises(PlatformIdentityError):
                    deployment_platform(value)  # type: ignore[arg-type]
        for name, architecture in (
            ("linux", "arm64"),
            ("windows", "arm64"),
            ("macos", "x86_64"),
            ("macos", ""),
        ):
            with self.subTest(name=name, architecture=architecture):
                with self.assertRaises(PlatformIdentityError):
                    deployment_platform_for_host(name, architecture)

    def test_host_pairs_translate_to_deployment_platforms(self) -> None:
        self.assertEqual("macos-arm64", deployment_platform_for_host("macos", "arm64"))
        self.assertEqual("windows-x64", deployment_platform_for_host("windows", "x86_64"))

    def test_mapping_is_one_to_one_over_the_contract_platforms(self) -> None:
        contract = product_contract_platforms()
        self.assertEqual(tuple(dict.fromkeys(contract)), contract)
        for name in contract:
            deployment = deployment_platform(name)
            self.assertEqual(name, product_contract_platform(deployment))
        self.assertEqual(
            len(contract), len({deployment_platform(name) for name in contract})
        )

    def test_python_tables_agree_with_the_single_owner(self) -> None:
        identity = host_platforms()
        self.assertEqual(identity, HOST_EVIDENCE_PLATFORMS)
        self.assertEqual(identity, INSTALL_EVIDENCE_PLATFORMS)
        self.assertEqual(identity, PRODUCT_SOAK_PLATFORMS)
        self.assertEqual(set(deployment_platforms()), UPDATE_PLATFORMS)
        self.assertEqual(CONTRACT_PLATFORMS, product_contract_platforms())
        self.assertLessEqual(
            {str(platform) for platform in PayloadPlatform}, deployment_platforms()
        )
        for platform in PayloadPlatform:
            self.assertEqual(str(platform), identity_for_deployment(str(platform)).deployment)

    def test_contract_host_tuples_use_the_contract_spelling(self) -> None:
        self.assertEqual(len(HOST_TUPLES), 9)
        for host_tuple in HOST_TUPLES:
            platform = host_tuple.split("/")[0]
            self.assertIn(platform, CONTRACT_PLATFORMS)
        self.assertNotIn("windows-x64", {tuple_.split("/")[0] for tuple_ in HOST_TUPLES})

    def test_host_matrix_document_only_uses_supported_pairs(self) -> None:
        matrix = json.loads(
            (ROOT / "docs/product/external-beta-host-matrix.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            [(row.get("platform"), row.get("architecture")) for row in matrix["targetPlatforms"]],
            list(host_platforms().items()),
        )
        self.assertEqual(9, len(matrix["targets"]))
        for row in matrix["targets"]:
            self.assertEqual(
                host_platforms().get(row.get("platform")), row.get("architecture")
            )


if __name__ == "__main__":
    unittest.main()
