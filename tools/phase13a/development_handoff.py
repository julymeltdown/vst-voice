from __future__ import annotations

import base64
import hashlib
import json
import secrets
from dataclasses import dataclass
from datetime import UTC, datetime, timedelta
from pathlib import Path

from tools.phase13a import update_contract
from tools.phase13a.release_payload import PayloadPlatform


ROOT_SEED = bytes.fromhex("00" * 31 + "01")
UPDATE_SEED = bytes.fromhex("11" * 32)


@dataclass(frozen=True, slots=True)
class DevelopmentUpdateContract:
    policy: Path
    manifest: Path
    root_key: Path
    publisher_key_id: str
    now: str
    test_only: bool = True


def _timestamp(value: datetime) -> str:
    return value.astimezone(UTC).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )


def _signed_policy(now: datetime) -> tuple[dict[str, object], bytes, str]:
    root_public = update_contract.ed25519_public_key(ROOT_SEED)
    update_public = update_contract.ed25519_public_key(UPDATE_SEED)
    root_key_id = hashlib.sha256(root_public).hexdigest()
    update_key_id = hashlib.sha256(update_public).hexdigest()
    expires = now + timedelta(days=7)
    policy: dict[str, object] = {
        "schemaVersion": 1,
        "purpose": "update-trust-policy",
        "channel": "external-beta",
        "policyEpoch": 1,
        "rootKeyId": root_key_id,
        "rootPublicKey": base64.b64encode(root_public).decode("ascii"),
        "allowedPlatforms": [
            PayloadPlatform.MACOS_ARM64,
            PayloadPlatform.WINDOWS_X64,
        ],
        "issuedAt": _timestamp(now),
        "notBefore": _timestamp(now),
        "expiresAt": _timestamp(expires),
        "compromiseCutoff": _timestamp(expires - timedelta(hours=1)),
        "delegatedKeys": [
            {
                "keyId": update_key_id,
                "purpose": "update",
                "algorithm": "Ed25519",
                "publicKey": base64.b64encode(update_public).decode("ascii"),
                "notBefore": _timestamp(now),
                "expiresAt": _timestamp(expires),
            }
        ],
    }
    payload = update_contract.canonical_json(policy)
    policy["signature"] = {
        "algorithm": "Ed25519",
        "keyId": root_key_id,
        "payloadSha256": hashlib.sha256(payload).hexdigest(),
        "value": base64.b64encode(
            update_contract.ed25519_sign(payload, ROOT_SEED)
        ).decode("ascii"),
    }
    return policy, root_public, update_key_id


def _signed_manifest(
    package: Path,
    platform: PayloadPlatform,
    now: datetime,
    update_key_id: str,
) -> dict[str, object]:
    expires = now + timedelta(days=2)
    ranges = {
        name: {"min": 1, "max": 8} for name in sorted(update_contract.RANGE_FIELDS)
    }
    manifest: dict[str, object] = {
        "schemaVersion": 1,
        "purpose": "update-manifest",
        "channel": "external-beta",
        "manifestId": (
            f"development-{platform}-{int(now.timestamp())}-{secrets.token_hex(8)}"
        ),
        "manifestEpoch": 2,
        "platform": platform,
        "targetBuild": "0.13.1-development-handoff",
        "targetVersion": "0.13.1",
        "minimumVersion": "0.13.0",
        "issuedAt": _timestamp(now),
        "expiresAt": _timestamp(expires),
        "readRanges": ranges,
        "writeRanges": {
            name: {"min": 8, "max": 8}
            for name in sorted(update_contract.RANGE_FIELDS)
        },
        "downgradePolicy": "REJECT",
        "package": {
            "fileName": package.name,
            "url": f"https://updates.example.invalid/{package.name}",
            "size": package.stat().st_size,
            "sha256": update_contract.sha256_file(package),
        },
        "releaseNotesSha256": hashlib.sha256(
            b"Project SEAM development installer handoff"
        ).hexdigest(),
    }
    payload = update_contract.canonical_json(manifest)
    manifest["signature"] = {
        "algorithm": "Ed25519",
        "keyId": update_key_id,
        "payloadSha256": hashlib.sha256(payload).hexdigest(),
        "value": base64.b64encode(
            update_contract.ed25519_sign(payload, UPDATE_SEED)
        ).decode("ascii"),
    }
    return manifest


def create_development_update_contract(
    package: Path,
    platform: PayloadPlatform,
    output: Path,
    now: datetime | None = None,
) -> DevelopmentUpdateContract:
    package = package.resolve()
    if package.is_symlink() or not package.is_file() or package.stat().st_size == 0:
        raise ValueError("development handoff package must be a non-empty regular file")
    issued = (now or datetime.now(UTC)).astimezone(UTC).replace(microsecond=0)
    policy, root_public, update_key_id = _signed_policy(issued)
    manifest = _signed_manifest(package, platform, issued, update_key_id)
    output.mkdir(parents=True, exist_ok=True)
    policy_path = output / "update-trust-policy.json"
    manifest_path = output / "update-manifest.json"
    root_path = output / "update-root-public-key.json"
    policy_path.write_text(
        json.dumps(policy, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    root_path.write_text(
        json.dumps(
            {
                "type": "ed25519-public",
                "schemaVersion": 1,
                "keyId": hashlib.sha256(root_public).hexdigest(),
                "publicKey": root_public.hex(),
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )
    return DevelopmentUpdateContract(
        policy=policy_path,
        manifest=manifest_path,
        root_key=root_path,
        publisher_key_id=update_key_id,
        now=_timestamp(issued),
    )
