from __future__ import annotations

import hashlib
import json
from pathlib import Path


def _object(path: Path) -> dict[str, object] | None:
    if path.is_symlink() or not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def validate_release_trust(payload: Path) -> tuple[bool, tuple[str, ...]]:
    declaration = _object(payload / "Trust/release-trust-roots.json")
    if declaration is None:
        return False, ("release trust-root declaration is invalid",)
    test_only = declaration.get("testOnly") is True
    roots = declaration.get("roots")
    if (
        declaration.get("schemaVersion") != 1
        or declaration.get("purpose") != "project-seam-release-trust-roots"
        or not isinstance(declaration.get("testOnly"), bool)
        or not isinstance(roots, list)
        or len(roots) != 1
        or not isinstance(roots[0], dict)
    ):
        return test_only, ("release trust-root declaration is invalid",)
    root = roots[0]
    if (
        root.get("purpose") != "update-root"
        or root.get("publicKeyFile") != "update-root-public-key.json"
        or not isinstance(root.get("keyId"), str)
    ):
        return test_only, ("update trust root declaration is invalid",)
    key = _object(payload / "Trust/update-root-public-key.json")
    if key is None:
        return test_only, ("update trust root file is missing or invalid",)
    public_key = key.get("publicKey")
    try:
        public_bytes = bytes.fromhex(public_key) if isinstance(public_key, str) else b""
    except ValueError:
        public_bytes = b""
    key_id = hashlib.sha256(public_bytes).hexdigest()
    if (
        key.get("type") != "ed25519-public"
        or key.get("schemaVersion") != 1
        or len(public_bytes) != 32
        or key.get("keyId") != key_id
        or root.get("keyId") != key_id
    ):
        return test_only, ("update trust root key identity is invalid",)
    return test_only, ()
