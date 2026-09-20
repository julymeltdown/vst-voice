"""Derive a per-phone breathiness prior from measured corpus aperiodicity.

The repaired acoustic model accepts an aperiodicity channel, but both inference
paths fill it with zeros unless a project supplies automation. A render therefore
gets no frication conditioning by default. This derives the missing prior from
measured evidence rather than from a hand-picked constant: phones that are
noise-like in the reviewed corpus receive a nonzero prior, and voiced phones
receive essentially none.

This is a measurement-derived default, not a trained parameter and not admitted
supervision. It exists so a score can drive the channel without a human drawing a
curve, and it is deliberately conservative: the prior is the measured mean for
the phone, bounded by the same [0, 1] range the channel contract declares.
"""
import argparse
import json
from pathlib import Path

from .__main__ import load_config, publish_new

REVISION = 1
# Below this, a symbol is treated as effectively periodic and left at exactly zero
# so that voiced rendering is unchanged rather than slightly perturbed.
PERIODIC_MARGIN = 1e-3
PINNED_PERIODIC = "PINNED_PERIODIC"


def derive_prior(estimate: dict, *, minimum_windows: int = 1) -> dict:
    """Return a bounded per-symbol prior from a captured aperiodicity estimate."""
    if (not isinstance(estimate, dict)
            or estimate.get("formatId") != "com.project-seam.aperiodicity-estimate"
            or type(estimate.get("schemaVersion")) is not int or estimate["schemaVersion"] != 1):
        raise ValueError("Expected a captured aperiodicity estimate")
    by_symbol = estimate.get("bySymbol")
    if (not isinstance(by_symbol, dict) or not 1 <= len(by_symbol) <= 4096
            or type(minimum_windows) is not int or not 1 <= minimum_windows <= 1000000):
        raise ValueError("Aperiodicity estimate requires a bounded per-symbol table")
    rows = []
    for symbol in sorted(by_symbol):
        if (not isinstance(symbol, str) or not 1 <= len(symbol.encode()) <= 256
                or any(ord(c) < 32 or ord(c) == 127 for c in symbol)):
            raise ValueError("Prior symbols must be printable and bounded")
        entry = by_symbol[symbol]
        if not isinstance(entry, dict) or set(entry) != {"mean", "windows"}:
            raise ValueError("Aperiodicity estimate rows require exactly mean and windows")
        mean, windows = entry["mean"], entry["windows"]
        if (type(mean) not in (int, float) or not 0.0 <= mean <= 1.0
                or type(windows) is not int or windows < 0):
            raise ValueError("Aperiodicity estimate values must be bounded")
        measured = windows >= minimum_windows
        # Unmeasured or effectively periodic symbols are pinned to zero rather than
        # given a small arbitrary value, so an unqualified phone cannot add noise.
        value = float(mean) if measured and mean > PERIODIC_MARGIN else 0.0
        rows.append(dict(symbol=symbol, breathiness=value, windows=windows,
                         sourceMean=float(mean), measured=measured,
                         periodic=PINNED_PERIODIC if value == 0.0 else "MEASURED"))
    return dict(formatId="com.project-seam.breathiness-prior", schemaVersion=1,
                revision=REVISION, estimator=estimate.get("policy"),
                corpusSha256=estimate.get("corpusSha256"),
                minimumWindows=minimum_windows, symbols=rows,
                supervisionAdmitted=False, singerQualified=False, releaseEligible=False)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--estimate", type=Path, required=True)
    parser.add_argument("--estimate-sha256", required=True)
    parser.add_argument("--minimum-windows", type=int, default=1)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prior = derive_prior(load_config(args.estimate, args.estimate_sha256),
                         minimum_windows=args.minimum_windows)
    publish_new(args.output, prior)
    nonzero = sum(1 for row in prior["symbols"] if row["breathiness"] > 0.0)
    print(json.dumps({"symbols": len(prior["symbols"]), "nonzero": nonzero}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
