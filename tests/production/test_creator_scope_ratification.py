from __future__ import annotations

import copy
import hashlib
import os
import subprocess
from pathlib import Path
from tempfile import TemporaryDirectory
import sys
import unittest
from unittest import mock

from tools.creator_scope import evidence as evidence_module
from tools.creator_scope.evidence import JsonObject, artifact_errors
from tools.creator_scope.verifier import verify_repository
from tests.production.creator_scope_fixtures import (
    REPOSITORY_ROOT,
    complete_record,
    copy_contract,
    evidence,
    write_record,
)


class CreatorScopeRatificationTests(unittest.TestCase):
    def test_artifact_hash_uses_the_file_opened_before_path_replacement(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            relative = Path("docs/evidence/original.json")
            artifact_path = root / relative
            artifact_path.parent.mkdir(parents=True)
            original = b'{"identity":"original"}\n'
            replacement = root / "replacement.json"
            artifact_path.write_bytes(original)
            replacement.write_bytes(b'{"identity":"replacement"}\n')
            artifact: JsonObject = {
                "path": relative.as_posix(),
                "sha256": hashlib.sha256(original).hexdigest(),
            }
            real_open = os.open
            path_was_replaced = False

            def open_then_replace(
                path: str | os.PathLike[str],
                flags: int,
                mode: int = 0o777,
                *,
                dir_fd: int | None = None,
            ) -> int:
                nonlocal path_was_replaced
                descriptor = real_open(path, flags, mode, dir_fd=dir_fd)
                if Path(path) == Path(relative.name) and not path_was_replaced:
                    replacement.replace(artifact_path)
                    path_was_replaced = True
                return descriptor

            with mock.patch.object(
                evidence_module.os,
                "open",
                side_effect=open_then_replace,
            ):
                errors = artifact_errors(root, artifact, "artifact")

            self.assertTrue(path_was_replaced)
            self.assertEqual(
                b'{"identity":"replacement"}\n', artifact_path.read_bytes()
            )
            self.assertEqual((), errors)

    def test_artifact_directory_path_is_rejected_without_crashing(self) -> None:
        with TemporaryDirectory() as directory:
            errors = artifact_errors(
                Path(directory),
                {"path": ".", "sha256": "0" * 64},
                "artifact",
            )

        self.assertNotEqual((), errors)

    def test_cli_reports_truthful_not_run_state(self) -> None:
        completed = subprocess.run(
            [
                sys.executable,
                str(REPOSITORY_ROOT / "scripts/verify_creator_scope_ratification.py"),
                "--root",
                str(REPOSITORY_ROOT),
            ],
            check=False,
            capture_output=True,
            text=True,
        )

        self.assertEqual(0, completed.returncode, completed.stderr)
        self.assertEqual(
            "CREATOR_SCOPE_CONTRACT=PASS state=NOT_RUN schema8_authorized=false\n",
            completed.stdout,
        )

    def test_current_not_run_record_is_truthful_and_not_authorized(self) -> None:
        result = verify_repository(REPOSITORY_ROOT)

        self.assertEqual((), result.errors)
        self.assertEqual("NOT_RUN", result.state)
        self.assertFalse(result.schema8_authorized)

    def test_complete_evidence_can_authorize_schema8(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            copy_contract(root)
            write_record(root, complete_record(root))

            result = verify_repository(root)

        self.assertEqual((), result.errors)
        self.assertEqual("PASS", result.state)
        self.assertTrue(result.schema8_authorized)

    def test_cross_field_forgery_stays_blocked(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            copy_contract(root)
            complete = complete_record(root)
            candidates: list[tuple[str, JsonObject]] = []

            summary_forgery = copy.deepcopy(complete)
            summary = summary_forgery["summary"]
            assert isinstance(summary, dict)
            summary["continuationCount"] = 4
            candidates.append(("summary", summary_forgery))

            citation_forgery = copy.deepcopy(complete)
            hypotheses = citation_forgery["hypotheses"]
            assert isinstance(hypotheses, list)
            first_hypothesis = hypotheses[0]
            assert isinstance(first_hypothesis, dict)
            first_hypothesis["observedSessionIds"] = ["CSR-004", "CSR-005"]
            candidates.append(("session-citation", citation_forgery))

            approval_forgery = copy.deepcopy(complete)
            approvals = approval_forgery["approvals"]
            assert isinstance(approvals, list)
            second_approval = approvals[1]
            assert isinstance(second_approval, dict)
            second_approval["reviewerId"] = "research-reviewer-one"
            candidates.append(("approval-identity", approval_forgery))

            hash_forgery = copy.deepcopy(complete)
            sessions = hash_forgery["sessions"]
            assert isinstance(sessions, list)
            first_session = sessions[0]
            assert isinstance(first_session, dict)
            session_evidence = first_session["evidence"]
            assert isinstance(session_evidence, list)
            first_evidence = session_evidence[0]
            assert isinstance(first_evidence, dict)
            first_evidence["sha256"] = "0" * 64
            candidates.append(("evidence-hash", hash_forgery))

            for label, candidate in candidates:
                with self.subTest(label=label):
                    write_record(root, candidate)

                    result = verify_repository(root)

                    self.assertNotEqual((), result.errors)
                    self.assertFalse(result.schema8_authorized)

    def test_duplicate_authorization_key_cannot_authorize_schema8(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            copy_contract(root)
            write_record(root, complete_record(root))
            record_path = (
                root / "docs/product/creator-beta/creator-scope-ratification.json"
            )
            source = record_path.read_text(encoding="utf-8")
            forged = source.replace(
                '"schema8Authorization": true',
                '"schema8Authorization": false, "schema8Authorization": true',
                1,
            )
            self.assertNotEqual(source, forged)
            record_path.write_text(forged, encoding="utf-8")

            result = verify_repository(root)

        self.assertNotEqual((), result.errors)
        self.assertFalse(result.schema8_authorized)

    def test_open_p0_or_p1_issue_blocks_authorization(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            copy_contract(root)
            payload = complete_record(root)
            sessions = payload["sessions"]
            assert isinstance(sessions, list)
            first_session = sessions[0]
            assert isinstance(first_session, dict)
            first_session["issues"] = [
                {
                    "id": "CSR-I001",
                    "severity": "P1",
                    "status": "OPEN",
                    "summary": "Creator cannot complete the core journey.",
                    "evidence": [evidence(root, "issue-p1")],
                }
            ]
            write_record(root, payload)

            result = verify_repository(root)

        self.assertTrue(any("P0/P1" in error for error in result.errors), result.errors)
        self.assertFalse(result.schema8_authorized)

    def test_unlinked_p0_task_blocker_cannot_authorize_schema8(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            copy_contract(root)
            payload = complete_record(root)
            sessions = payload["sessions"]
            assert isinstance(sessions, list)
            first_session = sessions[0]
            assert isinstance(first_session, dict)
            tasks = first_session["tasks"]
            assert isinstance(tasks, list)
            first_task = tasks[0]
            assert isinstance(first_task, dict)
            first_task["success"] = False
            first_task["blockerSeverity"] = "P0"
            write_record(root, payload)

            result = verify_repository(root)

        self.assertNotEqual((), result.errors)
        self.assertFalse(result.schema8_authorized)

    def test_schema_invalid_complete_record_cannot_authorize_schema8(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            copy_contract(root)
            payload = complete_record(root)
            payload["unexpectedAuthorizationField"] = True
            write_record(root, payload)

            result = verify_repository(root)

        self.assertNotEqual((), result.errors)
        self.assertFalse(result.schema8_authorized)

    def test_withdrawn_consent_cannot_count_as_a_completed_session(self) -> None:
        with TemporaryDirectory() as directory:
            root = Path(directory)
            copy_contract(root)
            payload = complete_record(root)
            sessions = payload["sessions"]
            assert isinstance(sessions, list)
            first_session = sessions[0]
            assert isinstance(first_session, dict)
            consent = first_session["consent"]
            assert isinstance(consent, dict)
            consent["status"] = "WITHDRAWN"
            write_record(root, payload)

            result = verify_repository(root)

        self.assertNotEqual((), result.errors)
        self.assertFalse(result.schema8_authorized)


if __name__ == "__main__":
    _ = unittest.main()
