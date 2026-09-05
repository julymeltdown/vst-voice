from __future__ import annotations

from dataclasses import asdict, dataclass
import json
from pathlib import Path
import platform
import sys
import tempfile

from .contract_types import CorpusError
from .corpus_contract import verify_corpus
from .packet_io import digest_bytes, read_bounded, write_new
from .process_capture import Command, ExecutableIdentity, capture_command


@dataclass(frozen=True, slots=True)
class RunSettings:
    root: Path
    corpus: Path
    output_parent: Path
    driver: Path
    analyzer: Path
    build_evidence: Path
    source_evidence: Path


def run_corpus(settings: RunSettings) -> Path:
    verified = verify_corpus(settings.root, settings.corpus)
    driver = ExecutableIdentity.capture(settings.driver)
    analyzer = ExecutableIdentity.capture(settings.analyzer)
    build_bytes = read_bounded(settings.build_evidence)
    source_bytes = read_bounded(settings.source_evidence)
    packet = Path(tempfile.mkdtemp(prefix=verified.contract.id + "-",
                                   dir=settings.output_parent)).resolve()
    inputs = packet / "inputs"
    for asset in verified.assets:
        write_new(inputs / asset.path, asset.payload)
    write_new(packet / "corpus.json", verified.contract_bytes)
    write_new(packet / "build-evidence", build_bytes)
    write_new(packet / "source-evidence", source_bytes)
    audio_lock = {path.removeprefix(verified.contract.bank_root + "/"):
                  verified.asset(path).sha256 for path in verified.audio}
    write_new(packet / "audio-lock.json", (json.dumps(audio_lock, indent=2) + "\n").encode())
    identity = {
        "schema_version": 1, "evidence_class": "auditory-diagnostic",
        "corpus_id": verified.contract.id,
        "corpus_sha256": digest_bytes(verified.contract_bytes),
        "build_evidence_sha256": digest_bytes(build_bytes),
        "source_evidence_sha256": digest_bytes(source_bytes),
        "driver": asdict(driver), "analyzer": asdict(analyzer),
        "system": platform.system(), "release": platform.release(),
        "machine": platform.machine(),
        "python": sys.version,
        "assets": [{"path": asset.path, "sha256": asset.sha256,
                    "bytes": len(asset.payload)} for asset in verified.assets],
    }
    write_new(packet / "input-provenance.json", (json.dumps(identity, indent=2) + "\n").encode())
    outputs = []
    try:
        for case in verified.contract.cases:
            for mode in ("bank", "raw"):
                stem = case.id + "-" + mode
                destination = packet / stem
                argv = (driver.path, str(inputs / case.project),
                        str(inputs / verified.contract.manifest),
                        str(packet / "audio-lock.json"), str(destination), mode)
                capture_command(Command(argv, driver, stem + "-render"), packet)
                for name in ("dry.wav", "diagnostics.json", "saved-project.seam"):
                    read_bounded(destination / name)
                analyze_argv = (analyzer.path, "analyze", str(destination / "dry.wav"),
                                str(destination / "analysis"))
                capture_command(Command(analyze_argv, analyzer, stem + "-analyze"), packet)
                read_bounded(destination / "analysis/analysis.json")
                for path in sorted(destination.rglob("*")):
                    if path.is_file():
                        payload = read_bounded(path)
                        outputs.append({"path": path.relative_to(packet).as_posix(),
                                        "sha256": digest_bytes(payload), "bytes": len(payload)})
        write_new(packet / "output-provenance.json", (json.dumps({
            "schema_version": 1, "evidence_class": "auditory-diagnostic",
            "artifacts": outputs,
        }, indent=2) + "\n").encode())
    except (CorpusError, OSError) as error:
        write_new(packet / "execution-error.json",
                  (json.dumps({"error": str(error)}, indent=2) + "\n").encode())
        raise
    return packet
