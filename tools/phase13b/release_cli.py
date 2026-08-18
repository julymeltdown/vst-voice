#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from candidate_builder import build_candidate
from common import load_json
from character_assets import generate_character_assets
from character_audit import audit_character
from character_gate import evaluate_character_dossier as evaluate_character_gate
from character_release import evaluate_character_dossier as evaluate_character_release
from content_bundle import create_development_bundle
from release_gate import evaluate_g5
from voicebank_audit import audit_voicebank
from voicebank_gate import evaluate_voicebank_dossier as evaluate_voicebank_gate
from voicebank_release import evaluate_voicebank_dossier as evaluate_voicebank_release


def load(path: Path) -> dict:
    return load_json(path)


def write_result(path: Path | None, payload: dict) -> None:
    text = json.dumps(payload, ensure_ascii=False, sort_keys=True, indent=2) + '\n'
    if path:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')
    print(text, end='')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description='Project SEAM Phase 13B release engineering')
    sub = parser.add_subparsers(dest='command', required=True)

    vb = sub.add_parser('voicebank')
    vb.add_argument('--dossier', type=Path, required=True)
    vb.add_argument('--root', type=Path, required=True)
    vb.add_argument('--output', type=Path)
    vb.add_argument('--expect-blocked', action='store_true')

    ch = sub.add_parser('character')
    ch.add_argument('--dossier', type=Path, required=True)
    ch.add_argument('--root', type=Path, required=True)
    ch.add_argument('--output', type=Path)
    ch.add_argument('--expect-blocked', action='store_true')

    av = sub.add_parser('audit-voicebank')
    av.add_argument('--bank-root', type=Path, required=True)
    av.add_argument('--profile', type=Path, required=True)
    av.add_argument('--output', type=Path)

    ac = sub.add_parser('audit-character')
    ac.add_argument('--character-root', type=Path, required=True)
    ac.add_argument('--profile', type=Path, required=True)
    ac.add_argument('--output', type=Path)

    ga = sub.add_parser('generate-character-assets')
    ga.add_argument('--source', type=Path, required=True)
    ga.add_argument('--output', type=Path, required=True)

    g5 = sub.add_parser('g5')
    g5.add_argument('--voicebank-result', type=Path, required=True)
    g5.add_argument('--character-result', type=Path, required=True)
    g5.add_argument('--matrix', type=Path, action='append', default=[])
    g5.add_argument('--product-dossier', type=Path, required=True)
    g5.add_argument('--root', type=Path, required=True)
    g5.add_argument('--output', type=Path)
    g5.add_argument('--expect-blocked', action='store_true')

    bundle = sub.add_parser('development-bundle')
    bundle.add_argument('--voicebank-root', type=Path, required=True)
    bundle.add_argument('--character-root', type=Path, required=True)
    bundle.add_argument('--output', type=Path, required=True)
    bundle.add_argument('--version', required=True)

    candidate = sub.add_parser('candidate')
    candidate.add_argument('--voicebank-dossier', type=Path, required=True)
    candidate.add_argument('--character-dossier', type=Path, required=True)
    candidate.add_argument('--release-report', type=Path, required=True)
    candidate.add_argument('--output', type=Path, required=True)
    candidate.add_argument('--version', required=True)

    args = parser.parse_args(argv)
    try:
        if args.command == 'voicebank':
            dossier = load(args.dossier)
            release = evaluate_voicebank_release(dossier, args.root)
            gate = evaluate_voicebank_gate(dossier, args.root)
            payload = {'release': release, 'passed': gate.passed, 'errors': gate.errors, 'blockedTargets': gate.blocked_targets}
            write_result(args.output, payload)
            return 0 if (not args.expect_blocked and gate.passed) or (args.expect_blocked and not gate.passed) else 4
        if args.command == 'character':
            dossier = load(args.dossier)
            release = evaluate_character_release(dossier, args.root)
            gate = evaluate_character_gate(dossier, args.root)
            payload = {'release': release, 'passed': gate.passed, 'errors': gate.errors, 'blockedTargets': gate.blocked_targets}
            write_result(args.output, payload)
            return 0 if (not args.expect_blocked and gate.passed) or (args.expect_blocked and not gate.passed) else 4
        if args.command == 'audit-voicebank':
            payload = audit_voicebank(args.bank_root, load(args.profile))
            write_result(args.output, payload)
            return 0 if payload['passed'] else 3
        if args.command == 'audit-character':
            payload = audit_character(args.character_root, load(args.profile))
            write_result(args.output, payload)
            return 0 if payload['passed'] else 3
        if args.command == 'generate-character-assets':
            payload = generate_character_assets(args.source, args.output)
            write_result(None, payload)
            return 0
        if args.command == 'g5':
            result = evaluate_g5(
                load(args.voicebank_result), load(args.character_result),
                [load(path) for path in args.matrix], load(args.product_dossier), args.root)
            write_result(args.output, result)
            return 0 if (args.expect_blocked and not result['passed']) or (not args.expect_blocked and result['passed']) else 4
        if args.command == 'development-bundle':
            payload = create_development_bundle(args.voicebank_root, args.character_root, args.output, version=args.version)
            write_result(None, payload)
            return 0
        if args.command == 'candidate':
            release = load(args.release_report)
            payload = build_candidate(
                output=args.output,
                component_files={
                    'voicebank-dossier.json': args.voicebank_dossier,
                    'character-dossier.json': args.character_dossier,
                    'release-report.json': args.release_report,
                },
                release_result=release,
                product_version=args.version,
            )
            write_result(None, payload)
            return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 3
    return 2


if __name__ == '__main__':
    raise SystemExit(main())
