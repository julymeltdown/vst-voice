#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "phase13a"))

from update_contract import canonical_json, ed25519_public_key, ed25519_sign, KEY_ID, sha256_bytes  # noqa: E402


def _signature(command: list[str], payload: bytes) -> bytes:
    with tempfile.NamedTemporaryFile(prefix="seam-signing-payload-", suffix=".json", delete=False) as temporary:
        temporary.write(payload)
        payload_path = Path(temporary.name)
    try:
        environment = os.environ.copy()
        environment["SEAM_SIGNING_PAYLOAD"] = str(payload_path)
        process = subprocess.run(command + [str(payload_path)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=environment, check=False)
        if process.returncode != 0:
            raise ValueError(process.stderr.decode("utf-8", "replace").strip() or "external signer failed")
        try:
            value = base64.b64decode(process.stdout.strip(), validate=True)
        except Exception as exc:
            raise ValueError("external signer must print a base64 signature") from exc
        if len(value) != 64:
            raise ValueError("external signer must return a 64-byte Ed25519 signature")
        return value
    finally:
        payload_path.unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Sign an update manifest through an external signer handle")
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--public-key")
    parser.add_argument("--signer-command", nargs="+")
    parser.add_argument("--test-seed-hex")
    parser.add_argument("--test-only", action="store_true")
    args = parser.parse_args(argv)
    if not isinstance(args.key_id, str) or not KEY_ID.fullmatch(args.key_id):
        print("ERROR: invalid key id", file=sys.stderr)
        return 2
    if args.test_seed_hex and (not args.test_only or args.signer_command):
        print("ERROR: test seed requires --test-only and cannot be combined with an external signer", file=sys.stderr)
        return 2
    if not args.test_seed_hex and not args.signer_command:
        print("ERROR: --signer-command is required for release signing", file=sys.stderr)
        return 2
    if not args.test_seed_hex and not args.public_key:
        print("ERROR: --public-key is required for release signing", file=sys.stderr)
        return 2
    try:
        value = json.loads(args.input.read_text(encoding="utf-8"))
        if not isinstance(value, dict):
            raise ValueError("manifest root must be an object")
        value.pop("signature", None)
        payload = canonical_json(value)
        if args.test_seed_hex:
            seed = bytes.fromhex(args.test_seed_hex)
            if len(seed) != 32:
                raise ValueError("test seed must be 32 bytes")
            signature = ed25519_sign(payload, seed)
            public_key = base64.b64encode(ed25519_public_key(seed)).decode("ascii")
        else:
            signature = _signature(args.signer_command, payload)
            try:
                if len(base64.b64decode(args.public_key.encode("ascii"), validate=True)) != 32:
                    raise ValueError("public key must decode to 32 bytes")
            except (UnicodeEncodeError, ValueError, base64.binascii.Error) as exc:
                raise ValueError("public key must be a 32-byte base64 Ed25519 key") from exc
            public_key = args.public_key
        value["signature"] = {
            "algorithm": "Ed25519",
            "keyId": args.key_id,
            "payloadSha256": sha256_bytes(payload),
            "value": base64.b64encode(signature).decode("ascii"),
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2).encode("utf-8") + b"\n")
        print(json.dumps({"status": "PASS", "keyId": args.key_id, "payloadSha256": sha256_bytes(payload), "testOnly": bool(args.test_seed_hex), "publicKeyProvided": bool(public_key)}, indent=2))
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
