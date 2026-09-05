from __future__ import annotations

from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import time

from .contract_types import CorpusError
from .packet_io import digest_bytes, read_bounded, write_new


@dataclass(frozen=True, slots=True)
class ExecutableIdentity:
    path: str
    sha256: str

    @classmethod
    def capture(cls, path: Path) -> ExecutableIdentity:
        resolved = path.resolve(strict=True)
        return cls(str(resolved), digest_bytes(read_bounded(resolved)))


@dataclass(frozen=True, slots=True)
class Command:
    argv: tuple[str, ...]
    executable: ExecutableIdentity
    record_stem: str


@dataclass(frozen=True, slots=True)
class CommandRecord:
    argv: tuple[str, ...]
    executable: ExecutableIdentity
    started_utc: str
    elapsed_seconds: float
    exit_code: int | None
    failure: str


def capture_command(command: Command, packet: Path) -> CommandRecord:
    if ExecutableIdentity.capture(Path(command.executable.path)) != command.executable:
        raise CorpusError("executable_changed", command.executable.path)
    log_root = packet / "commands"
    log_root.mkdir(exist_ok=True)
    started = datetime.now(timezone.utc).isoformat()
    start = time.monotonic()
    exit_code = None
    failure = ""
    try:
        with (log_root / (command.record_stem + ".stdout")).open("xb") as stdout:
            with (log_root / (command.record_stem + ".stderr")).open("xb") as stderr:
                completed = subprocess.run(command.argv, cwd=packet, stdin=subprocess.DEVNULL,
                                           stdout=stdout, stderr=stderr, timeout=180, check=False)
        exit_code = completed.returncode
    except subprocess.TimeoutExpired:
        failure = "process_timeout"
    except OSError as error:
        failure = "process_launch: " + str(error)
    record = CommandRecord(command.argv, command.executable, started,
                           time.monotonic() - start, exit_code, failure)
    write_new(log_root / (command.record_stem + ".json"),
              (json.dumps(asdict(record), indent=2) + "\n").encode())
    if failure or exit_code != 0:
        raise CorpusError("process_exit",
            f"{command.record_stem}: {exit_code}, {failure}; "
            f"record={(log_root / (command.record_stem + '.json')).resolve()}; "
            f"stderr={(log_root / (command.record_stem + '.stderr')).resolve()}")
    current = ExecutableIdentity.capture(Path(command.executable.path))
    if current != command.executable:
        raise CorpusError("executable_changed", command.executable.path)
    return record
