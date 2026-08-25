#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


POSITIVE_GA_CLAIMS = (
    re.compile(r"(?i)(?:now|is|will be)\s+(?:a\s+)?(?:general availability|commercial release)"),
    re.compile(r"(?i)(?<!not\s)(?:general availability|commercial release)\s+(?:is|will be)\s+(?:available|enabled|supported)"),
    re.compile(r"(?i)(?:included|ships|available|supported)\s+(?:as|with|for)\s+official voicebank 01"),
    re.compile(r"(?i)(?:storefront|billing|payments?)\s+(?:is|are)\s+(?:enabled|available|supported)"),
)
REQUIRED_HEADINGS = {
    "eula": ("# Project SEAM External Beta EULA", "To accept"),
    "privacy": ("# Project SEAM External Beta Privacy Notice", "local-private"),
    "manual": ("# Project SEAM External Beta User Manual", "## Supported surface", "## First launch"),
    "limitations": ("# Project SEAM External Beta Known Limitations", "commercial release"),
    "update": ("# Project SEAM External Beta Update and Rollback", "manual and user initiated"),
    "checklist": ("# Project SEAM External Beta Tester Checklist", "Document version"),
    "support": ("# Project SEAM External Beta Support", "## Severity", "## Intake"),
    "security": ("# Project SEAM External Beta Security Response", "key compromise"),
}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_documentation(root: Path, manifest: dict[str, Any]) -> tuple[list[str], dict[str, Any]]:
    errors: list[str] = []
    if manifest.get("schemaVersion") != 1 or manifest.get("purpose") != "offline-beta-documentation":
        errors.append("documentation manifest schema or purpose is invalid")
    if manifest.get("channel") != "external-beta":
        errors.append("documentation manifest channel must be external-beta")
    entries = manifest.get("requiredDocuments")
    if not isinstance(entries, list) or not entries:
        return errors + ["requiredDocuments must be a non-empty array"], {}
    hashes: dict[str, str] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            errors.append("documentation entry must be an object")
            continue
        document_id = entry.get("id")
        relative = entry.get("path")
        expected_version = entry.get("version")
        if not isinstance(document_id, str) or document_id not in REQUIRED_HEADINGS:
            errors.append(f"unknown documentation id: {document_id}")
            continue
        if not isinstance(relative, str) or Path(relative).is_absolute() or ".." in Path(relative).parts:
            errors.append(f"{document_id}: path must remain inside the product root")
            continue
        path = (root / relative).resolve()
        try:
            path.relative_to(root.resolve())
        except ValueError:
            errors.append(f"{document_id}: path escapes product root")
            continue
        if path.is_symlink() or not path.is_file():
            errors.append(f"{document_id}: document does not exist as a regular file")
            continue
        text = path.read_text(encoding="utf-8")
        for heading in REQUIRED_HEADINGS[document_id]:
            if heading not in text:
                errors.append(f"{document_id}: missing required section {heading!r}")
        if not isinstance(expected_version, str) or expected_version not in text:
            errors.append(f"{document_id}: declared document version is missing")
        for pattern in POSITIVE_GA_CLAIMS:
            if pattern.search(text):
                errors.append(f"{document_id}: contains an unsupported commercial/GA claim")
        hashes[relative] = _sha256(path)
    acceptance = manifest.get("acceptance")
    if not isinstance(acceptance, dict) or acceptance.get("networkRequired") is not False or acceptance.get("optionalChoicesAreSeparate") is not True:
        errors.append("documentation acceptance must be offline and separate optional choices")
    return errors, {"schemaVersion": 1, "documents": hashes}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate offline External Beta documentation")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    manifest_path = args.manifest or root / "docs/product/external-beta-documentation.json"
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if not isinstance(manifest, dict):
            raise ValueError("documentation manifest root must be an object")
        errors, result = validate_documentation(root, manifest)
        if errors:
            for error in errors:
                print(f"ERROR: {error}", file=sys.stderr)
            return 3
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print("PHASE13A_OFFLINE_DOCUMENTATION=PASS")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
