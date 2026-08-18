#!/usr/bin/env python3
from pathlib import Path
import argparse
import json
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'phase13b'))
from character_assets import generate_character_assets  # noqa: E402


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=ROOT / 'assets/character-01/source/canonical-lowpoly.jpeg')
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/character-01/production-development')
    args = parser.parse_args(argv)
    result = generate_character_assets(args.source, args.output)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
