#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = ["PyYAML==6.0.3"]
# ///

from __future__ import annotations

import argparse
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.creator_scope.ustx_study_bridge import (  # noqa: E402
    export_ustx,
    import_ustx,
)
from tools.creator_scope.ustx_study_contracts import BridgeError  # noqa: E402
from tools.creator_scope.ustx_to_seam import ImportOptions  # noqa: E402


class Arguments(argparse.Namespace):
    command: str = ""
    source: Path = Path()
    target: Path = Path()
    report: Path = Path()
    voicebank_id: str = "study.unresolved.voicebank"
    voicebank_version: str = "0.0.0-study"
    voicebank_content_hash: str = ""
    character_id: str = "study.unresolved.character"
    character_version: str = "0.0.0-study"
    language: str = "ja"


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run the Project SEAM study-only USTX/schema-7 bridge"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    importer = subparsers.add_parser(
        "import-ustx", help="convert a trusted USTX 0.9 fixture to SEAM schema 7"
    )
    _ = importer.add_argument("source", type=Path)
    _ = importer.add_argument("target", type=Path)
    _ = importer.add_argument("--report", type=Path, required=True)
    _ = importer.add_argument("--voicebank-id", default="study.unresolved.voicebank")
    _ = importer.add_argument("--voicebank-version", default="0.0.0-study")
    _ = importer.add_argument("--voicebank-content-hash", default="")
    _ = importer.add_argument("--character-id", default="study.unresolved.character")
    _ = importer.add_argument("--character-version", default="0.0.0-study")
    _ = importer.add_argument(
        "--language", choices=("ja", "ko", "en", "und"), default="ja"
    )
    exporter = subparsers.add_parser(
        "export-ustx", help="convert a SEAM schema-7 study project to USTX 0.9"
    )
    _ = exporter.add_argument("source", type=Path)
    _ = exporter.add_argument("target", type=Path)
    _ = exporter.add_argument("--report", type=Path, required=True)
    return parser


def main(argv: list[str] | None = None) -> int:
    arguments = _parser().parse_args(argv, namespace=Arguments())
    try:
        if arguments.command == "import-ustx":
            report = import_ustx(
                arguments.source,
                arguments.target,
                arguments.report,
                ImportOptions(
                    voicebank_id=arguments.voicebank_id,
                    voicebank_version=arguments.voicebank_version,
                    voicebank_content_hash=arguments.voicebank_content_hash,
                    character_id=arguments.character_id,
                    character_version=arguments.character_version,
                    language=arguments.language,
                ),
            )
        else:
            report = export_ustx(arguments.source, arguments.target, arguments.report)
    except BridgeError as error:
        print(f"STUDY_BRIDGE=FAIL error={error}", file=sys.stderr)
        return 3
    loss_count = report.get("lossCount", 0)
    print(
        f"STUDY_BRIDGE=PASS direction={report['direction']} "
        f"losses={loss_count} output={arguments.target} report={arguments.report}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
