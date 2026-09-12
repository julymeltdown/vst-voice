#!/usr/bin/env python3
"""Development intake verification, not the shipping reading adapter."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("probe", type=Path)
    parser.add_argument("dictionary", type=Path)
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parents[2] / "tests/fixtures/pronunciation/openjtalk-intake.json"
    data = json.loads(fixture.read_text(encoding="utf-8"))
    for name, expected in data["dictionarySha256"].items():
        digest = hashlib.sha256()
        with (args.dictionary / name).open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != expected:
            raise RuntimeError(f"Dictionary identity mismatch: {name}")
    for case in data["cases"]:
        result = subprocess.run([str(args.probe.resolve()), "--read-stdin", str(args.dictionary.resolve())], input=case["text"],
                                capture_output=True, text=True, encoding="utf-8", timeout=10, check=True)
        response = json.loads(result.stdout)
        if response["schemaVersion"] != 1 or response["source"] != case["text"]:
            raise RuntimeError("Source mismatch")
        source = case["text"].encode("utf-8")
        end = 0
        for token in response["tokens"]:
            start, length = token["byteOffset"], token["byteLength"]
            if start < end or length <= 0 or start + length > len(source):
                raise RuntimeError("Invalid token span")
            if source[end:start].decode("utf-8").strip() or source[start:start+length].decode("utf-8") != token["surface"]:
                raise RuntimeError("Missing or mismatched source span")
            if token["status"] != "known" and (token["reading"] is not None or token["pronunciation"] is not None):
                raise RuntimeError("Unknown token invented a reading")
            end = start + length
        if source[end:].decode("utf-8").strip():
            raise RuntimeError("Omitted source tail")
        actual = [[token["surface"], token["reading"], token["pronunciation"]] for token in response["tokens"]]
        if actual != case["tokens"]:
            raise RuntimeError(f"Reading mismatch for {case['text']!r}: {actual!r}")
    for text in ("", "あ" * 1366, "a\0b"):
        result = subprocess.run([str(args.probe.resolve()), "--read-stdin", str(args.dictionary.resolve())], input=text.encode("utf-8"),
                                capture_output=True, timeout=10)
        if result.returncode != 2 or result.stdout:
            raise RuntimeError("Probe input admission failed")
    print(f"PASS: four dictionary hashes, {len(data['cases'])} stdin reading/span cases, three input admission cases; native review pending")


if __name__ == "__main__":
    main()
