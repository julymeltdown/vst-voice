"""Read the complete frozen corpus before allowing renderer execution."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from .contract_types import CorpusContract, CorpusError, object_value, parse_object, relative_path
from .packet_io import MAXIMUM_PACKET_INPUT_BYTES, digest_bytes, inspect_path, read_bounded

@dataclass(frozen=True, slots=True)
class VerifiedAsset:
    path: str
    sha256: str
    payload: bytes


@dataclass(frozen=True, slots=True)
class VerifiedCorpus:
    contract: CorpusContract
    contract_bytes: bytes
    assets: tuple[VerifiedAsset, ...]
    audio: tuple[str, ...]

    def asset(self, path: str) -> VerifiedAsset:
        for asset in self.assets:
            if asset.path == path:
                return asset
        raise CorpusError("contract_lock", path)


def verify_corpus(root: Path, contract: Path) -> VerifiedCorpus:
    root = root.resolve(strict=True)
    contract_bytes = read_bounded(contract)
    spec = CorpusContract.parse(contract_bytes)
    assets = []
    total = 0
    for entry in spec.assets:
        payload = read_bounded(inspect_path(root, entry.path))
        if digest_bytes(payload) != entry.sha256:
            raise CorpusError("asset_hash", entry.path)
        total += len(payload)
        if total > MAXIMUM_PACKET_INPUT_BYTES:
            raise CorpusError("asset_size", "corpus exceeds 128 MiB")
        assets.append(VerifiedAsset(entry.path, entry.sha256, payload))
    by_path = {asset.path: asset for asset in assets}
    manifest = parse_object(by_path[spec.manifest].payload)
    audio = set()
    for unit in manifest.array("units"):
        path = relative_path(object_value(unit).string("audio"))
        full = spec.bank_root + "/" + path
        if full not in by_path:
            raise CorpusError("unlisted_manifest_audio", full)
        audio.add(full)
    bank = inspect_path(root, spec.bank_root)
    for path in bank.rglob("*"):
        name = path.relative_to(root).as_posix()
        if path.is_symlink():
            raise CorpusError("asset_symlink", name)
        if not path.is_dir() and name not in by_path:
            raise CorpusError("unlisted_bank_asset", name)
    return VerifiedCorpus(spec, contract_bytes, tuple(assets), tuple(sorted(audio)))
