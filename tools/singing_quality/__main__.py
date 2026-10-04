from __future__ import annotations

import argparse
from pathlib import Path
import sys

from .contract_types import CorpusError
from .runner import RunSettings, check_frozen_limits, run_corpus


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect a diagnostic dry-vocal comparison packet.")
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--corpus", type=Path, required=True)
    parser.add_argument("--output-parent", type=Path, required=True)
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--analyzer", type=Path, required=True)
    parser.add_argument("--build-evidence", type=Path, required=True,
                        help="Current compiler/configuration evidence file retained verbatim")
    parser.add_argument("--source-evidence", type=Path, required=True,
                        help="Current source HEAD and working-diff evidence retained verbatim")
    parser.add_argument("--require-limits", action="store_true",
                        help=("Exit non-zero when any retained case misses its own frozen "
                              "acoustic limits. Off by default: a packet is diagnostic "
                              "evidence and is produced even when it reads badly."))
    args = parser.parse_args()
    try:
        packet = run_corpus(RunSettings(args.root, args.corpus, args.output_parent,
                                       args.driver, args.analyzer, args.build_evidence,
                                       args.source_evidence))
    except (CorpusError, OSError) as error:
        print(f"singing-quality: {error}", file=sys.stderr)
        return 2
    print(packet)
    if args.require_limits:
        verdict = check_frozen_limits(packet)
        if not verdict.ok:
            for case in verdict.failing:
                print(f"singing-quality: {case} is outside its frozen limits", file=sys.stderr)
            for case in verdict.missing:
                print(f"singing-quality: {case} has no measurement verdict", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
