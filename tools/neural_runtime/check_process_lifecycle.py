"""Repeat a trusted diagnostic command, retaining every exit and output.

This is a local qualification runner, not an untrusted-program sandbox. Commands
must check their own inference results. A clean exit alone proves no audio quality.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def run(command, output, attempts, timeout):
    if not command or not 1 <= attempts <= 100 or not 0 < timeout <= 3600:
        raise ValueError("Require a command, 1..100 attempts and timeout in (0, 3600]")
    output = Path(output)
    output.mkdir(mode=0o700)  # Never overwrite a previous qualification run.
    report = {"schemaVersion": 1, "command": command, "cwd": str(Path.cwd()),
              "requestedAttempts": attempts, "timeoutSeconds": timeout,
              "attempts": [], "passed": False, "releaseEligible": False}
    for number in range(1, attempts + 1):
        started = time.monotonic()
        stdout = output / f"{number:03d}.stdout"
        stderr = output / f"{number:03d}.stderr"
        timed_out, error, code = False, None, None
        with stdout.open("xb") as out, stderr.open("xb") as err:
            try:
                result = subprocess.run(command, stdout=out, stderr=err, timeout=timeout)
                code = result.returncode
            except subprocess.TimeoutExpired:
                timed_out = True
            except OSError as exc:
                error = str(exc)
        attempt = {"number": number, "exitCode": code, "timedOut": timed_out,
                   "launchError": error, "elapsedSeconds": time.monotonic() - started}
        for label, path in (("stdout", stdout), ("stderr", stderr)):
            with path.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            attempt[label] = {"path": path.name, "bytes": path.stat().st_size,
                              "sha256": digest}
        report["attempts"].append(attempt)
        # Keep all failures. A later successful attempt never erases one.
        report["passed"] = (len(report["attempts"]) == attempts and
                            all(item["exitCode"] == 0 and not item["timedOut"]
                                and item["launchError"] is None for item in report["attempts"]))
        pending = output / "report.pending"
        pending.write_text(json.dumps(report, indent=2) + "\n")
        pending.replace(output / "report.json")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="New directory; never overwritten")
    parser.add_argument("--attempts", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=300)
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- followed by trusted executable and arguments")
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    try:
        report = run(command, args.output, args.attempts, args.timeout)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"{exc}\n")
    print(json.dumps(report))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
