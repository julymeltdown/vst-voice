"""Corpus admission tests use a counting renderer boundary, never audio claims."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.singing_quality.corpus_contract import CorpusError, verify_corpus
from tools.singing_quality.runner import RunSettings, run_corpus


class CorpusContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.payloads = {
            "bank/manifest.json": json.dumps({"units": [{"audio": "audio/voice.wav"}]}).encode(),
            "bank/audio/voice.wav": b"frozen audio fixture",
            "bank/provenance.json": b'{"source":"source.wav"}',
            "bank/NOTICE.md": b"Technical fixture source notice",
            "bank/README.md": b"Technical fixture only",
            "source.wav": b"original source fixture",
            "song.seam": b'{"notes":[1,2,3]}',
            "short.seam": b'{"notes":[1,2]}',
            "melody-notice.md": b"Original diagnostic composition",
        }
        for name, payload in self.payloads.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
        self.contract = self.root / "corpus.json"
        self.spec = {
            "schema_version": 1, "id": "u1-test", "purpose": "diagnostic",
            "bank_root": "bank", "manifest": "bank/manifest.json",
            "provenance": "bank/provenance.json", "notice": "bank/NOTICE.md",
            "bank_readme": "bank/README.md", "source": "source.wav",
            "melody_notice": "melody-notice.md",
            "cases": [{"id": "song", "project": "song.seam"},
                      {"id": "unequal-rests", "project": "short.seam"}],
            "assets": [{"path": name, "sha256": hashlib.sha256(data).hexdigest()}
                       for name, data in self.payloads.items()],
        }
        self.write_contract()

    def write_contract(self) -> None:
        self.contract.write_text(json.dumps(self.spec), encoding="utf-8")

    def assert_rejected_before_render(self, code: str) -> None:
        settings = RunSettings(self.root, self.contract, self.root, self.root / "driver",
                               self.root / "analyzer", self.root / "build", self.root / "source")
        # When: the renderer is reachable only after full corpus admission.
        with patch("subprocess.run") as render, self.assertRaises(CorpusError) as caught:
            run_corpus(settings)
        # Then: admission fails without invoking the renderer.
        self.assertEqual(code, caught.exception.code)
        render.assert_not_called()

    def test_missing_audio_rejects_before_render(self) -> None:
        # Given: an asset named by the lock is absent.
        (self.root / "bank/audio/voice.wav").unlink()
        self.assert_rejected_before_render("asset_missing")

    def test_wrong_audio_hash_rejects_before_render(self) -> None:
        # Given: the source bytes differ from their locked hash.
        (self.root / "bank/audio/voice.wav").write_bytes(b"changed audio")
        self.assert_rejected_before_render("asset_hash")

    def test_changed_project_rejects_before_render(self) -> None:
        # Given: a saved melody changes after the lock is written.
        (self.root / "song.seam").write_bytes(b"changed project")
        self.assert_rejected_before_render("asset_hash")

    def test_missing_provenance_rejects_before_render(self) -> None:
        # Given: source attribution is absent even though render inputs exist.
        (self.root / "bank/provenance.json").unlink()
        self.assert_rejected_before_render("asset_missing")

    def test_unlisted_bank_wav_rejects_before_render(self) -> None:
        # Given: the resource directory contains an unlocked recording.
        (self.root / "bank/audio/extra.wav").write_bytes(b"unlisted")
        self.assert_rejected_before_render("unlisted_bank_asset")

    def test_unlisted_manifest_audio_rejects_before_render(self) -> None:
        # Given: a hash-valid manifest references an audio path outside the lock.
        path = self.root / "bank/manifest.json"
        path.write_text('{"units":[{"audio":"audio/extra.wav"}]}', encoding="utf-8")
        self.spec["assets"][0]["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.write_contract()
        self.assert_rejected_before_render("unlisted_manifest_audio")

    def test_symlink_audio_rejects_before_render(self) -> None:
        # Given: even identical bytes must not be admitted through a symlink.
        path = self.root / "bank/audio/voice.wav"
        path.unlink()
        target = self.root / "identical.wav"
        target.write_bytes(self.payloads["bank/audio/voice.wav"])
        path.symlink_to(target)
        self.assert_rejected_before_render("asset_symlink")

    def test_escaping_contract_path_rejects_before_render(self) -> None:
        # Given: a lock attempts to reference a parent path.
        self.spec["source"] = "../source.wav"
        self.write_contract()
        self.assert_rejected_before_render("asset_path")

    def test_valid_corpus_freezes_verified_bytes(self) -> None:
        # Given: all referenced resources match the lock.
        # When: the corpus is admitted and the original file later changes.
        verified = verify_corpus(self.root, self.contract)
        (self.root / "song.seam").write_bytes(b"later change")
        # Then: the admitted project bytes retain the verified identity.
        self.assertEqual(self.payloads["song.seam"], verified.asset("song.seam").payload)


if __name__ == "__main__":
    unittest.main()
