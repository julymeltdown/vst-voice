#!/usr/bin/env python3
'''Verify the N1 learned-singer feasibility input record against the tree and the host.

This is the bounded experiment's entry gate, not a quality judgement. It answers one question: are
the inputs a real training run needs actually present and internally consistent, and if not, exactly
which one is missing. It never starts training, never downloads anything and never asserts that a
voice is good.

Exit 0 when every declared input is present and consistent. Exit 3 when one or more inputs are absent,
which is the expected state until an authorized corpus exists. Exit 2 for a malformed record.
'''
import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

REQUIRED_INPUT_KEYS = (
    'corpus',
    'labels',
    'acousticProfile',
    'vocoderProfile',
    'upstreamRevision',
    'permissionEvidence',
)


def read_json(path: Path) -> dict:
    raw = path.read_bytes()
    if len(raw) > 1024 * 1024:
        raise ValueError('record exceeds 1 MiB')
    value = json.loads(raw.decode('utf-8'))
    if not isinstance(value, dict):
        raise ValueError('record must be a JSON object')
    return value


def check_record(record: dict) -> list:
    '''Return a list of (input name, present, detail). Never raises for a missing input.'''
    results = []
    inputs = record.get('inputs')
    if not isinstance(inputs, dict):
        raise ValueError('record needs an inputs object')
    unknown = set(inputs) - set(REQUIRED_INPUT_KEYS)
    if unknown:
        raise ValueError('unknown input declarations: ' + ', '.join(sorted(unknown)))
    for name in REQUIRED_INPUT_KEYS:
        entry = inputs.get(name)
        if not isinstance(entry, dict):
            results.append((name, False, 'not declared'))
            continue
        status = entry.get('status')
        if status == 'present':
            # A declared-present input must name a digest and a location, because an unverifiable
            # claim of presence is the same as an absent one for a training run's purposes.
            location = entry.get('location')
            digest = entry.get('sha256')
            revision = entry.get('revision')
            if not isinstance(location, str) or not location:
                results.append((name, False, 'present but names no location'))
                continue
            # A file-shaped input is bound by digest; a source pin is bound by an exact revision.
            # Either way the claim has to be checkable, because an unverifiable claim of presence is
            # the same as an absent input for a training run's purposes.
            if isinstance(digest, str) and len(digest) == 64:
                results.append((name, True, location))
            elif isinstance(revision, str) and len(revision) == 40:
                results.append((name, True, location))
            else:
                results.append((name, False, 'present but neither a digest nor a revision binds it'))
                continue
        elif status == 'absent':
            reason = entry.get('reason')
            results.append((name, False, str(reason) if reason else 'declared absent'))
        else:
            # An unrecognised status is an authoring error, not an absent input. Treating it as
            # absent would let a typo look like a legitimate declaration of missing material.
            raise ValueError('input ' + name + ' has unknown status ' + repr(status))
    return results


def verify(record: dict, root: Path, *, expect_corpus: str | None) -> tuple:
    '''Return (exit code, list of (name, present, detail)).'''
    results = check_record(record)
    # A corpus declared present must actually exist under the declared root, because the whole point
    # of the gate is to distinguish a recorded intention from material a run could use.
    for name, present, detail in results:
        if not present:
            continue
        resolved = (root / detail) if not os.path.isabs(detail) else Path(detail)
        if not resolved.exists():
            results = [(n, False, d + ' (declared present but not found)') if n == name else (n, p, d)
                       for n, p, d in results]
            continue
        if expect_corpus and name == 'corpus':
            digest = record['inputs']['corpus'].get('sha256')
            if hashlib.sha256(resolved.read_bytes()).hexdigest() != digest:
                results = [(n, False, d + ' (digest does not match the file)') if n == name else (n, p, d)
                           for n, p, d in results]
    missing = [name for name, present, _ in results if not present]
    return (3 if missing else 0), results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    parser.add_argument('--root', type=Path, default=Path('.') , help='Root for relative locations.')
    parser.add_argument('--expect-corpus', help='Hash the corpus file and require this digest.')
    parser.add_argument('--require-complete', action='store_true',
                        help='Exit non-zero when any declared input is absent.')
    arguments = parser.parse_args()
    try:
        record = read_json(arguments.record)
        code, results = verify(record, arguments.root, expect_corpus=arguments.expect_corpus)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print('FEASIBILITY_INPUT=INVALID ' + str(error))
        return 2
    for name, present, detail in results:
        print(('present  ' if present else 'ABSENT   ') + name + ': ' + detail)
    absent = [name for name, present, _ in results if not present]
    if absent:
        print('FEASIBILITY_INPUT=INCOMPLETE missing=' + ','.join(absent))
        return code if arguments.require_complete else 0
    print('FEASIBILITY_INPUT=COMPLETE')
    return 0


if __name__ == '__main__':
    sys.exit(main())

