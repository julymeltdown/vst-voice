"""Bounded POSIX invocation of the explicitly selected first-party extractor."""
import json
import os
from pathlib import Path
import selectors
import signal
import subprocess
import time


def extract_pitch(executable: Path, source: Path, *, timeout_seconds: float = 30) -> dict:
    if os.name != "posix":
        raise ValueError("Native feature supervisor currently requires POSIX")
    if type(timeout_seconds) not in (int, float) or not 0 < timeout_seconds <= 60:
        raise ValueError("Feature deadline must be in (0, 60] seconds")
    executable = executable.resolve(strict=True)
    if not executable.is_file():
        raise ValueError("Select a trusted first-party extractor executable")
    deadline = time.monotonic() + timeout_seconds
    child = subprocess.Popen([str(executable), "extract-pitch", str(source.resolve(strict=True))],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
    buffers = {"stdout": bytearray(), "stderr": bytearray()}
    limits = {"stdout": 16 * 1024 * 1024, "stderr": 64 * 1024}
    try:
        with selectors.DefaultSelector() as selector:
            for name, pipe in (("stdout", child.stdout), ("stderr", child.stderr)):
                os.set_blocking(pipe.fileno(), False)
                selector.register(pipe, selectors.EVENT_READ, name)
            while selector.get_map():
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise ValueError("Native pitch extraction timed out")
                for key, _ in selector.select(min(remaining, 0.1)):
                    chunk = os.read(key.fileobj.fileno(), 65536)
                    if not chunk:
                        selector.unregister(key.fileobj)
                        continue
                    target = buffers[key.data]
                    if len(target) + len(chunk) > limits[key.data]:
                        raise ValueError("Native feature output exceeded capture budget")
                    target.extend(chunk)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ValueError("Native pitch extraction timed out")
        try:
            result = child.wait(timeout=remaining)
        except subprocess.TimeoutExpired as error:
            raise ValueError("Native pitch extraction timed out") from error
        if result != 0:
            raise ValueError(f"Native pitch extractor failed with exit code {result}")
    finally:
        # Do not poll/reap an exited leader before killing its group: descendants
        # may still hold the pipes open. Keeping the leader unreaped also keeps
        # its PID from being recycled before this targeted group termination.
        if child.returncode is None:
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            finally:
                child.wait()
        child.stdout.close()
        child.stderr.close()
    def unique(pairs):
        value = {}
        for key, item in pairs:
            if key in value:
                raise ValueError("Duplicate native feature field")
            value[key] = item
        return value
    def invalid(value):
        raise ValueError("Nonfinite native feature constant")
    value = json.loads(buffers["stdout"], object_pairs_hook=unique, parse_constant=invalid)
    if not isinstance(value, dict):
        raise ValueError("Native feature output must be an object")
    return value
