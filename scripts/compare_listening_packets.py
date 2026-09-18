#!/usr/bin/env python3
'''Compare a newly rendered listening packet against a retained reference packet.

Section 6.3 of the revised plan asks for a compact, durable, versioned listening reference set:
compare new output alongside the reference, never regenerate the reference in place, and treat a byte
difference as a request for investigation rather than a verdict. A hash cannot tell us a voice got
worse, so this tool reports what changed and refuses to rank the two.

Retained Reference Set & Promotion:
Reference sets bind per-item metadata: score identity, recipe identity and hash, resource identity,
engine/compiler/render revision, render settings, the WAV digest, and acoustic measurements (peak,
rms, clipped samples, spectral distance, f0 RMSE, and level).
Use --promote-reference DEST with required --reason REASON to write a new versioned reference beside
the old one. In-place overwriting and regeneration are strictly rejected.

ASR Triage Runner:
Use --asr-triage --asr-model LOCAL_DIRECTORY for real transcription of hash-verified audio and
generated negative controls. Optional dependencies are required; no model is downloaded implicitly.
Outputs record model file hashes and runtime versions. Recognition is diagnostic, never acceptance.

Exit 0 when every case matches the reference exactly, 3 when output differs or is missing (reported
per case, not as a single pass/fail), and 2 for a malformed input.
'''
import argparse
from datetime import datetime, timezone
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.singing_quality.asr_triage import (  # noqa: E402
    DEFAULT_ASR_MODEL, DEFAULT_DECODING_SETTINGS, PINNED_NEGATIVE_CONTROLS,
    FasterWhisperBackend, screen_packet,
)

REFERENCE_SET_FORMAT_ID = 'com.project-seam.listening-reference-set'
REQUIRED_MANIFEST_KEYS = ('packetId', 'sourceCommit', 'cases', 'artifacts')
REQUIRED_REFERENCE_SET_KEYS = ('formatId', 'referenceSetId', 'items')

IDENTITY_KEYS = (
    'recipeHash', 'recipeIdentity', 'scoreIdentity', 'resourceIdentity',
    'engineRevision', 'renderRevision', 'sampleRate', 'channels', 'frames',
    'spectralDistance', 'f0Rmse', 'levelDb', 'peak', 'rms', 'clippedSamples'
)


def is_reference_set(manifest: dict) -> bool:
    return manifest.get('formatId') == REFERENCE_SET_FORMAT_ID or 'referenceSetId' in manifest


def load_manifest(path: Path) -> dict:
    raw = path.read_bytes()
    if len(raw) > 64 * 1024 * 1024:
        raise ValueError('manifest exceeds 64 MiB')
    value = json.loads(raw.decode('utf-8'))
    if not isinstance(value, dict):
        raise ValueError('manifest must be a JSON object')
    if is_reference_set(value):
        for key in REQUIRED_REFERENCE_SET_KEYS:
            if key not in value:
                raise ValueError('reference set manifest is missing ' + key)
        if not isinstance(value['items'], list):
            raise ValueError('reference set items must be a list')
    else:
        for key in REQUIRED_MANIFEST_KEYS:
            if key not in value:
                raise ValueError('manifest is missing ' + key)
        if not isinstance(value['cases'], list):
            raise ValueError('cases must be a list')
    value['_path'] = str(path.resolve())
    return value


def bind_reference_item(output_entry: dict, case_id: str, manifest: dict) -> dict:
    score_identity = output_entry.get('scoreIdentity') or case_id
    recipe_identity = output_entry.get('recipeIdentity') or output_entry.get('variant') or 'baseline'
    recipe_hash = output_entry.get('recipeHash') or ''
    resource_identity = output_entry.get('resourceIdentity') or manifest.get('resourceIdentity') or 'character-01'
    engine_revision = output_entry.get('engineRevision') or manifest.get('sourceCommit') or manifest.get('engineRevision') or ''
    render_revision = output_entry.get('renderRevision') or manifest.get('packetId') or 'r1'

    render_settings = output_entry.get('renderSettings')
    if not isinstance(render_settings, dict):
        render_settings = {
            'sampleRate': output_entry.get('sampleRate', 48000),
            'channels': output_entry.get('channels', 2),
            'hopSize': output_entry.get('hopSize', 256),
        }

    wav_sha256 = output_entry.get('wavSha256') or output_entry.get('sha256') or ''

    measurements = {
        'sampleRate': output_entry.get('sampleRate', 48000),
        'channels': output_entry.get('channels', 2),
        'frames': output_entry.get('frames', 0),
        'durationSeconds': output_entry.get('durationSeconds', 0.0),
        'peak': output_entry.get('peak', 0.0),
        'rms': output_entry.get('rms', 0.0),
        'clippedSamples': output_entry.get('clippedSamples', 0),
    }
    for key in ('spectralDistance', 'f0Rmse', 'levelDb'):
        if key in output_entry:
            measurements[key] = output_entry[key]
        elif 'measurements' in output_entry and isinstance(output_entry['measurements'], dict) and key in output_entry['measurements']:
            measurements[key] = output_entry['measurements'][key]

    rel_path = output_entry.get('path', f"{case_id}/{recipe_identity}/master.wav")
    item = {
        'path': rel_path,
        'scoreIdentity': score_identity,
        'recipeIdentity': recipe_identity,
        'recipeHash': recipe_hash,
        'resourceIdentity': resource_identity,
        'engineRevision': engine_revision,
        'renderRevision': render_revision,
        'renderSettings': render_settings,
        'wavSha256': wav_sha256,
        'sha256': wav_sha256,
        'variant': recipe_identity,
        'measurements': measurements,
    }
    item.update(measurements)
    return item


def bind_reference_manifest(manifest: dict, reference_set_id: str | None = None,
                            promoted_from: str | None = None, reason: str | None = None) -> dict:
    set_id = reference_set_id or manifest.get('referenceSetId') or manifest.get('packetId') or 'reference-set'
    items = []
    if 'items' in manifest:
        for it in manifest['items']:
            items.append(bind_reference_item(it, it.get('scoreIdentity', 'default'), manifest))
    elif 'cases' in manifest:
        for case in manifest['cases']:
            cid = case.get('id', 'default')
            for out in case.get('outputs', []):
                items.append(bind_reference_item(out, cid, manifest))
    return {
        'formatId': REFERENCE_SET_FORMAT_ID,
        'schemaVersion': 1,
        'referenceSetId': set_id,
        'createdAt': datetime.now(timezone.utc).isoformat(),
        'promotedFrom': promoted_from or manifest.get('referenceSetId') or manifest.get('packetId') or '(root)',
        'sourceCommit': manifest.get('sourceCommit') or manifest.get('engineRevision') or '',
        'reason': reason or 'Retained reference set binding',
        'items': items,
        'verdict': 'UNRANKED',
    }


def promote_reference(source: dict, dest: Path, reason: str, reference_manifest: dict | None = None) -> dict:
    if not isinstance(reason, str) or not reason.strip():
        raise ValueError('--reason is required for --promote-reference')
    if dest.exists():
        raise ValueError(f'destination reference already exists (cannot overwrite in place): {dest}')
    if reference_manifest and '_path' in reference_manifest and Path(reference_manifest['_path']).resolve() == dest.resolve():
        raise ValueError('cannot promote reference onto itself in place')

    promoted = bind_reference_manifest(
        source,
        reference_set_id=dest.stem,
        promoted_from=(reference_manifest.get('referenceSetId') or reference_manifest.get('packetId') if reference_manifest else None),
        reason=reason.strip(),
    )
    raw = json.dumps(promoted, indent=2) + '\n'
    with dest.open('x', encoding='utf-8') as stream:
        stream.write(raw)
    return promoted


def run_asr_triage(manifest: dict, audio_root: Path | None = None,
                   model: Path | None = None,
                   decoding_settings: dict | None = None,
                   expected_text: dict | None = None, progress=None) -> dict:
    if model is None:
        raise ValueError('--asr-model is required: provide a local faster-whisper model directory')
    settings = {} if decoding_settings is None else decoding_settings
    if not isinstance(settings, dict) or set(settings) - {'language'}:
        raise ValueError('ASR settings permit only language; decoding and unprompted controls are fixed')
    if audio_root is None:
        if '_path' not in manifest:
            raise ValueError('ASR needs an audio root or a loaded manifest path')
        audio_root = Path(manifest['_path']).parent
    backend = FasterWhisperBackend(Path(model), settings.get('language', 'ja'))
    return screen_packet(manifest, audio_root, backend, expected_text, progress)


def index_outputs(manifest: dict) -> dict:
    '''Every case output keyed by case id, variant and role, so two packets line up.'''
    indexed = {}
    if is_reference_set(manifest):
        for item in manifest.get('items', []):
            relative = item.get('path')
            if not isinstance(relative, str) or not relative:
                raise ValueError('a reference item has no path')
            score_id = item.get('scoreIdentity') or 'default'
            variant = item.get('recipeIdentity') or item.get('variant') or 'baseline'
            role = Path(relative).name
            indexed[(score_id, variant, role)] = item
        return indexed
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
        expected_sha = expected.get('wavSha256') or expected.get('sha256')
        actual_sha = actual.get('wavSha256') or actual.get('sha256')
        if expected_sha == actual_sha:
            findings.append({'case': label, 'status': 'identical'})
            continue
        # A changed hash is a request for investigation. The identity fields are reported so the
        # change can be attributed to a recipe, rate, layout or length change rather than guessed.
        differences = []
        for name in IDENTITY_KEYS:
            exp_val = expected.get(name)
            act_val = actual.get(name)
            if exp_val is None and 'measurements' in expected and isinstance(expected['measurements'], dict):
                exp_val = expected['measurements'].get(name)
            if act_val is None and 'measurements' in actual and isinstance(actual['measurements'], dict):
                act_val = actual['measurements'].get(name)
            if exp_val is not None and act_val is not None and exp_val != act_val:
                differences.append(name)
        finding = {'case': label, 'status': 'changed',
                   'referenceSha256': expected_sha, 'candidateSha256': actual_sha}
        if differences:
            finding['identityChanged'] = differences
            finding['referenceIdentity'] = {name: (expected.get(name) if expected.get(name) is not None else expected.get('measurements', {}).get(name)) for name in differences}
            finding['candidateIdentity'] = {name: (actual.get(name) if actual.get(name) is not None else actual.get('measurements', {}).get(name)) for name in differences}
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
    if is_reference_set(reference):
        items_to_check = reference.get('items', [])
        for item in items_to_check:
            score_id = item.get('scoreIdentity')
            if only_case is not None and score_id != only_case:
                continue
            relative = item.get('path')
            if not isinstance(relative, str) or not relative:
                raise ValueError('a reference item has no path')
            if relative in seen:
                continue
            seen.add(relative)
            locate = relative[len(only_case) + 1:] if only_case and relative.startswith(only_case + '/') else relative
            actual = root / locate
            expected_sha = item.get('wavSha256') or item.get('sha256')
            if not actual.is_file():
                findings.append({'case': relative, 'status': 'missing'})
                continue
            if hashlib.sha256(actual.read_bytes()).hexdigest() == expected_sha:
                findings.append({'case': relative, 'status': 'identical'})
                continue
            findings.append({'case': relative, 'status': 'changed',
                             'referenceSha256': expected_sha})
        return findings
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
            expected_sha = output.get('wavSha256') or output.get('sha256')
            if hashlib.sha256(actual.read_bytes()).hexdigest() == expected_sha:
                findings.append({'case': relative, 'status': 'identical'})
                continue
            findings.append({'case': relative, 'status': 'changed',
                             'referenceSha256': expected_sha})
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('reference', type=Path, help='Retained packet manifest or reference set. Never written to.')
    parser.add_argument('candidate', type=Path, nargs='?', help='Newly rendered packet manifest. Omit with --rerender-root or --asr-triage.')
    parser.add_argument('--report', type=Path, help='Write the findings here; must be a new file.')
    parser.add_argument('--only-case', help='Compare only this case id, for a partial rerender.')
    parser.add_argument('--rerender-root', type=Path,
                        help='Directory holding re-rendered artifacts at the reference manifest paths.')
    parser.add_argument('--allow-changes', action='store_true',
                        help='Exit 0 even when output differs, for a deliberate revision review.')
    parser.add_argument('--promote-reference', type=Path,
                        help='Promote candidate as a new retained reference set at DEST beside the old one. Requires --reason. In-place overwrite is rejected.')
    parser.add_argument('--reason',
                        help='Explicit reason explaining the promotion of a new reference set. Required when --promote-reference is used.')
    parser.add_argument('--asr-triage', action='store_true',
                        help='Transcribe real audio with generated controls. Requires a local model; never qualifies a singer.')
    parser.add_argument('--asr-model', type=Path,
                        help=f'Local model directory, e.g. a snapshot of {DEFAULT_ASR_MODEL}.')
    parser.add_argument('--asr-decoding-settings', type=Path,
                        help='Optional JSON object containing only language: ja, en, or ko. Other decoding is fixed.')
    parser.add_argument('--asr-expected-text', type=Path,
                        help='Optional JSON mapping case IDs to intended text for diagnostic character error rates.')
    arguments = parser.parse_args()
    if arguments.report is not None and arguments.report.exists():
        print('LISTENING_COMPARISON=INVALID report already exists')
        return 2
    if arguments.promote_reference is not None:
        if not arguments.reason or not arguments.reason.strip():
            print('LISTENING_COMPARISON=INVALID --reason is required for --promote-reference')
            return 2
    try:
        reference = load_manifest(arguments.reference)
        candidate = load_manifest(arguments.candidate) if arguments.candidate else None
        target_for_triage = candidate if candidate is not None else reference
        findings = []
        if arguments.rerender_root is not None:
            findings = compare_artifacts(reference, arguments.rerender_root, arguments.only_case)
        elif candidate is not None:
            findings = compare(reference, candidate)
        elif not arguments.asr_triage and arguments.promote_reference is None:
            raise ValueError('a candidate manifest or --rerender-root is required')
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print('LISTENING_COMPARISON=INVALID ' + str(error))
        return 2

    if arguments.promote_reference is not None:
        source_for_promotion = candidate if candidate is not None else reference
        try:
            promoted = promote_reference(
                source=source_for_promotion,
                dest=arguments.promote_reference,
                reason=arguments.reason or '',
                reference_manifest=reference,
            )
            print(f"PROMOTED_REFERENCE={arguments.promote_reference} items={len(promoted['items'])} (reason: {arguments.reason})")
        except ValueError as err:
            print(f"LISTENING_COMPARISON=INVALID {err}")
            return 2

    triage_result = None
    if arguments.asr_triage:
        decoding_overrides = None
        if arguments.asr_decoding_settings is not None:
            try:
                decoding_overrides = json.loads(arguments.asr_decoding_settings.read_text(encoding='utf-8'))
            except (OSError, json.JSONDecodeError) as err:
                print(f"LISTENING_COMPARISON=INVALID invalid asr decoding settings: {err}")
                return 2
        try:
            expected = (json.loads(arguments.asr_expected_text.read_text(encoding='utf-8'))
                        if arguments.asr_expected_text else None)
            triage_result = run_asr_triage(
                manifest=target_for_triage, audio_root=arguments.rerender_root,
                model=arguments.asr_model, decoding_settings=decoding_overrides,
                expected_text=expected, progress=lambda message: print(message, flush=True),
            )
        except (OSError, ValueError, RuntimeError) as error:
            print('ASR_TRIAGE=NOT_RUN ' + str(error))
            return 2
        ctrl_status = 'BREACH' if triage_result['summary']['controlsBreached'] else 'OBSERVED_NO_TEXT'
        print(f"ASR_TRIAGE=TRIAGE items_evaluated={triage_result['summary']['totalItems']} flagged={triage_result['summary']['flaggedItems']} model={triage_result['model']} controls={ctrl_status}")
    counts = {}
    for finding in findings:
        counts[finding['status']] = counts.get(finding['status'], 0) + 1
    ref_name = reference.get('referenceSetId') or reference.get('packetId') or str(arguments.reference)
    cand_name = candidate.get('referenceSetId') or candidate.get('packetId') if candidate else '(rerender)'
    report = {
        'formatId': 'com.project-seam.listening-comparison',
        'schemaVersion': 1,
        'reference': ref_name,
        'candidate': cand_name,
        'referencePacket': reference.get('packetId') or reference.get('referenceSetId') or ref_name,
        'candidatePacket': candidate.get('packetId') or candidate.get('referenceSetId') if candidate else '(rerender)',
        'referenceCommit': reference.get('sourceCommit') or reference.get('engineRevision') or '(unspecified)',
        'candidateCommit': candidate.get('sourceCommit') or candidate.get('engineRevision') if candidate else '(rerender)',
        'counts': counts,
        'findings': findings,
        # A comparison says what changed. It deliberately does not say which packet is better,
        # because no hash and no distance metric can establish that.
        'verdict': 'UNRANKED',
    }
    if triage_result is not None:
        report['asrTriage'] = triage_result
    if arguments.report is not None:
        if arguments.report.exists():
            print('LISTENING_COMPARISON=INVALID report already exists')
            return 2
        with arguments.report.open('x', encoding='utf-8') as stream:
            stream.write(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + chr(10))
    print('REFERENCE=' + ref_name)
    print('CANDIDATE=' + cand_name)
    if triage_result is not None and triage_result['summary']['controlsBreached']:
        return 3
    for status in ('changed', 'missing', 'added'):
        for finding in findings:
            if finding['status'] != status:
                continue
            detail = finding['case']
            if finding.get('identityChanged'):
                detail += ' (identity changed: ' + ', '.join(finding['identityChanged']) + ')'
            print(status.upper() + ' ' + detail)
    if findings or candidate is not None or arguments.rerender_root is not None:
        print('LISTENING_COMPARISON=' + ('IDENTICAL' if not counts.get('changed') and not counts.get('missing')
                                           and not counts.get('added') else 'DIFFERENCES') +
              ' identical=' + str(counts.get('identical', 0)) +
              ' changed=' + str(counts.get('changed', 0)) +
              ' missing=' + str(counts.get('missing', 0)) +
              ' added=' + str(counts.get('added', 0)) +
              ' reference=' + ref_name)
        changed = counts.get('changed', 0) or counts.get('missing', 0) or counts.get('added', 0)
        if not changed or arguments.allow_changes:
            return 0
        return 3
    return 0


if __name__ == '__main__':
    sys.exit(main())
