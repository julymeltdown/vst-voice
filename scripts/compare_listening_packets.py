#!/usr/bin/env python3
'''Compare a newly rendered listening packet against a retained reference packet.

Section 6.3 of the revised plan asks for a compact, durable, versioned listening reference set:
compare new output alongside the reference, never regenerate the reference in place, and treat a byte
difference as a request for investigation rather than a verdict. A hash cannot tell us a voice got
worse, so this tool reports what changed and refuses to rank the two.

Exit 0 when every case matches the reference exactly, 3 when output differs or is missing (reported
per case, not as a single pass/fail), and 2 for a malformed input.
'''
import argparse
import hashlib
import json
import sys
from pathlib import Path

REQUIRED_MANIFEST_KEYS = ('packetId', 'sourceCommit', 'cases', 'artifacts')
IDENTITY_KEYS = ('recipeHash', 'sampleRate', 'channels', 'frames')


def load_manifest(path: Path) -> dict:
    raw = path.read_bytes()
    if len(raw) > 64 * 1024 * 1024:
        raise ValueError('manifest exceeds 64 MiB')
    value = json.loads(raw.decode('utf-8'))
    if not isinstance(value, dict):
        raise ValueError('manifest must be a JSON object')
    for key in REQUIRED_MANIFEST_KEYS:
        if key not in value:
            raise ValueError('manifest is missing ' + key)
    if not isinstance(value['cases'], list):
        raise ValueError('cases must be a list')
    return value


def index_outputs(manifest: dict) -> dict:
    '''Every case output keyed by case id, variant and role, so two packets line up.'''
    indexed = {}
    for case in manifest['cases']:
        case_id = case.get('id')
        if not isinstance(case_id, str) or not case_id:
            raise ValueError('a case has no id')
        for output in case.get('outputs', []):
            relative = output.get('path')
            if not isinstance(relative, str) or not relative:
                raise ValueError('a case output has no path')
            # The leaf name distinguishes a master from a candidate or analysis file at the same
            # case and variant, so a packet cannot silently compare the wrong pair.
            role = Path(relative).name
            indexed[(case_id, output.get('variant'), role)] = output
    return indexed


def compare(reference: dict, candidate: dict) -> list:
    '''Return one finding per key: identical, changed, missing or added. Never a ranking.'''
    reference_outputs = index_outputs(reference)
    candidate_outputs = index_outputs(candidate)
    findings = []
    for key in sorted(reference_outputs, key=lambda item: (item[0], str(item[1]), item[2])):
        case_id, variant, role = key
        expected = reference_outputs[key]
        actual = candidate_outputs.get(key)
        label = case_id + '/' + str(variant) + '/' + role
        if actual is None:
            findings.append({'case': label, 'status': 'missing'})
            continue
        if expected.get('sha256') == actual.get('sha256'):
            findings.append({'case': label, 'status': 'identical'})
            continue
        # A changed hash is a request for investigation. The identity fields are reported so the
        # change can be attributed to a recipe, rate, layout or length change rather than guessed.
        differences = [name for name in IDENTITY_KEYS if expected.get(name) != actual.get(name)]
        finding = {'case': label, 'status': 'changed',
                   'referenceSha256': expected.get('sha256'), 'candidateSha256': actual.get('sha256')}
        if differences:
            finding['identityChanged'] = differences
            finding['referenceIdentity'] = {name: expected.get(name) for name in differences}
            finding['candidateIdentity'] = {name: actual.get(name) for name in differences}
        findings.append(finding)
    for key in sorted(candidate_outputs, key=lambda item: (item[0], str(item[1]), item[2])):
        if key not in reference_outputs:
            findings.append({'case': '/'.join(str(part) for part in key), 'status': 'added'})
    return findings


def compare_artifacts(reference: dict, root: Path, only_case: str | None = None) -> list:
    """Compare a retained packet manifest against a re-rendered artifact tree.

    A rerender is what section 6.3 actually needs: the reference stays untouched on disk while new
    output is produced beside it. Files are located by the manifest's own relative paths, so the
    comparison is against the retained packet's declared layout rather than a convention."""
    findings = []
    seen = set()
    for case in reference['cases']:
        if only_case is not None and case.get('id') != only_case:
            continue
        for output in case.get('outputs', []):
            relative = output.get('path')
            if not isinstance(relative, str) or not relative:
                raise ValueError('a case output has no path')
            if relative in seen:
                continue
            seen.add(relative)
            # A partial rerender is written at the case's own root, so the case prefix is dropped
            # when only one case is being compared.
            locate = relative[len(only_case) + 1:] if only_case else relative
            actual = root / locate
            if not actual.is_file():
                findings.append({'case': relative, 'status': 'missing'})
                continue
            if hashlib.sha256(actual.read_bytes()).hexdigest() == output.get('sha256'):
                findings.append({'case': relative, 'status': 'identical'})
                continue
            findings.append({'case': relative, 'status': 'changed',
                             'referenceSha256': output.get('sha256')})
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reference', type=Path, help='Retained packet manifest. Never written to.')
    parser.add_argument('candidate', type=Path, nargs='?', help='Newly rendered packet manifest. Omit with --rerender-root.')
    parser.add_argument('--report', type=Path, help='Write the findings here; must be a new file.')
    parser.add_argument('--only-case', help='Compare only this case id, for a partial rerender.')
    parser.add_argument('--rerender-root', type=Path,
                        help='Directory holding re-rendered artifacts at the reference manifest paths.')
    parser.add_argument('--allow-changes', action='store_true',
                        help='Exit 0 even when output differs, for a deliberate revision review.')
    arguments = parser.parse_args()
    try:
        reference = load_manifest(arguments.reference)
        candidate = load_manifest(arguments.candidate) if arguments.candidate else None
        if arguments.rerender_root is not None:
            findings = compare_artifacts(reference, arguments.rerender_root, arguments.only_case)
        elif candidate is not None:
            findings = compare(reference, candidate)
        else:
            raise ValueError('a candidate manifest or --rerender-root is required')
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print('LISTENING_COMPARISON=INVALID ' + str(error))
        return 2
    counts = {}
    for finding in findings:
        counts[finding['status']] = counts.get(finding['status'], 0) + 1
    report = {
        'formatId': 'com.project-seam.listening-comparison',
        'schemaVersion': 1,
        'referencePacket': reference['packetId'],
        'candidatePacket': candidate['packetId'] if candidate else '(rerender)',
        'referenceCommit': reference['sourceCommit'],
        'candidateCommit': candidate['sourceCommit'] if candidate else '(rerender)',
        'counts': counts,
        'findings': findings,
        # A comparison says what changed. It deliberately does not say which packet is better,
        # because no hash and no distance metric can establish that.
        'verdict': 'UNRANKED',
    }
    if arguments.report is not None:
        if arguments.report.exists():
            print('LISTENING_COMPARISON=INVALID report already exists')
            return 2
        arguments.report.write_text(json.dumps(report, indent=2) + chr(10))
    for status in ('changed', 'missing', 'added'):
        for finding in findings:
            if finding['status'] != status:
                continue
            detail = finding['case']
            if finding.get('identityChanged'):
                detail += ' (identity changed: ' + ', '.join(finding['identityChanged']) + ')'
            print(status.upper() + ' ' + detail)
    print('LISTENING_COMPARISON=' + ('IDENTICAL' if not counts.get('changed') and not counts.get('missing')
                                       and not counts.get('added') else 'DIFFERENCES') +
          ' identical=' + str(counts.get('identical', 0)) +
          ' changed=' + str(counts.get('changed', 0)) +
          ' missing=' + str(counts.get('missing', 0)) +
          ' added=' + str(counts.get('added', 0)))
    changed = counts.get('changed', 0) or counts.get('missing', 0) or counts.get('added', 0)
    if not changed or arguments.allow_changes:
        return 0
    return 3


if __name__ == '__main__':
    sys.exit(main())

