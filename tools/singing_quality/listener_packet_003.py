"""Build/verify a portable diagnostic packet from six explicitly supplied projects.

Run with python -m tools.singing_quality.listener_packet_003. Verification checks
retained consistency, not an independent build attestation or musical acceptance.
"""
from __future__ import annotations

import argparse
from copy import deepcopy
import json
from pathlib import Path
import re
import sys

import numpy as np

from .contract_types import CorpusError, parse_object, relative_path
from .listener_packet_contract import (VERDICT_SCHEMA, canonical_json, case_definitions,
                                      guide_text, validate_cases, validate_guide, validate_q3_control)
from .packet_io import digest_bytes, inspect_path, read_bounded, write_new
from .process_capture import Command, ExecutableIdentity, capture_command
from .verify_listener_packet_002 import parse_wav

FORMAT = 'com.project-seam.listener-packet-003'
SETTINGS = {'sampleRate': 48000, 'command': 'bake-project', 'audioSurface': 'dry-procedural-candidate'}
NAMES = tuple(name for case in case_definitions() for name in case['files'])
TOOL_MODULES = ('listener_packet_003.py', 'listener_packet_contract.py', 'verify_listener_packet_002.py',
                'packet_io.py', 'process_capture.py', 'contract_types.py')


def read_json(path):
    return parse_object(read_bounded(path)).fields


def save_json(path, value):
    write_new(path, (json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + '\n').encode())


def region_of(project):
    tracks = project['vocalTracks']
    if len(tracks) != 1 or len(tracks[0]['regions']) != 1:
        raise ValueError('diagnostic input requires exactly one track and region')
    region = tracks[0]['regions'][0]
    if not region['notes']:
        raise ValueError('diagnostic input has no notes')
    return tracks[0], region


def note_key(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9a-fA-F]{1,16}', value):
        raise ValueError('invalid note identity')
    return f'{int(value, 16):016x}'


def inventory(root):
    result = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink():
            raise ValueError('packet contains a symlink')
        if path.is_dir():
            continue
        name = path.relative_to(root).as_posix()
        if name != 'manifest.json':
            result[name] = digest_bytes(read_bounded(path))
    return result


def markers_by_note(metadata, project):
    _, region = region_of(project)
    expected = {note_key(n['id']) for n in region['notes']}
    if len(expected) != len(region['notes']):
        raise ValueError('duplicate project note identity')
    found = {}
    keys = set()
    for marker in metadata['markers']:
        key, ordinal = marker['key'].split(':')
        key = note_key(key)
        identity = (key, int(ordinal))
        if identity in keys or key not in expected:
            raise ValueError('duplicate or unexpected marker identity')
        keys.add(identity)
        if not isinstance(marker['phone'], str) or not marker['phone']:
            raise ValueError('empty marker phone')
        if not 0 <= marker['startFrame'] < marker['endFrame'] <= metadata['frameCount']:
            raise ValueError('marker span is outside audio')
        normalized = dict(marker)
        normalized.setdefault('palatalized', False)
        normalized.setdefault('kind', 'oral-vowel')
        found.setdefault(key, []).append(normalized)
    if set(found) != expected:
        raise ValueError('marker inventory omits a project note')
    return found


def validate_design_projects(projects):
    legato, detached, solo, pitch_range = (projects[name] for name in NAMES[:4])
    for value in (legato, detached, solo, pitch_range):
        if value['tempoMap'] != [{'bpm': 120, 'tick': 0}]:
            raise ValueError('Q1/Q2 require the declared 120 BPM diagnostic grid')
        if region_of(value)[1]['startTick'] != 0:
            raise ValueError('Q1/Q2 require a zero-origin diagnostic region')
    _, region = region_of(legato)
    notes = region['notes']
    if ([n['midiKey'] for n in notes] != [67,64,62,60,62,64,67]
            or any(n['startTick'] != i*1920 or n['durationTick'] != 1920 for i,n in enumerate(notes))):
        raise ValueError('Q1 legato score differs from the declared run')
    expected = deepcopy(legato)
    other = region_of(expected)[1]
    for i,note in enumerate(other['notes']):
        note['startTick'] = i*2880
    other['durationTick'] = 6*2880 + 1920
    if canonical_json(expected) != canonical_json(detached):
        raise ValueError('Q1 changes more than the written gaps')
    _, region = region_of(pitch_range)
    notes = region['notes']
    if ([n['midiKey'] for n in notes] != [55,60,67,72,79]
            or any(n['startTick'] != i*1920 or n['durationTick'] != 1920 for i,n in enumerate(notes))):
        raise ValueError('Q2 pitch range differs from the declared run')
    if any(lyric['surface'] != 'あ' or lyric['language'] != 'ja' for lyric in region['lyrics']):
        raise ValueError('Q2 must keep the vowel identical across pitches')
    expected = deepcopy(pitch_range)
    other = region_of(expected)[1]
    selected = other['notes'][2]
    selected['startTick'] = 0
    other['notes'] = [selected]
    other['lyrics'] = [lyric for lyric in other['lyrics'] if lyric['id'] == selected['lyricId']]
    other['durationTick'] = 1920
    if canonical_json(expected) != canonical_json(solo):
        raise ValueError('Q2 solo is not the same note extracted from the pitch range')


def harmonic_balance(mono, rate, midi):
    """Middle-half median Hann-window peaks near written H1/H2; not a pitch oracle."""
    window = mono[len(mono)//4:3*len(mono)//4]
    fundamental = 440.0 * 2 ** ((midi - 69) / 12.0)
    freqs = np.fft.rfftfreq(32768, 1.0 / rate)
    bands = [np.abs(freqs - harmonic * fundamental) < 10 for harmonic in (1, 2)]
    values = [[], []]
    for offset in range(0, len(window) - 4096 + 1, 512):
        segment = window[offset:offset+4096]
        spectrum = np.abs(np.fft.rfft((segment-segment.mean()) * np.hanning(4096), 32768))
        for band, amplitudes in zip(bands, values):
            amplitudes.append(float(spectrum[band].max()))
    if not values[0]:
        raise ValueError('Q2 harmonic measurement requires a longer signal')
    h1, h2 = (float(np.median(v)) for v in values)
    if not np.isfinite([h1, h2]).all() or h1 <= 1e-8 or h2 <= 1e-8:
        raise ValueError('Q2 harmonic measurement has absent/nonfinite partials')
    return {'h1': h1, 'h2': h2, 'h2OverH1': h2/h1}


def onset_leads(project, metadata):
    # validate_design_projects establishes the 120 BPM / 960 PPQ grid and zero
    # region origin. Native candidate markers are already origin-relative.
    markers = markers_by_note(metadata, project)
    return [(note['startTick'] * 25 - min(m['startFrame'] for m in markers[note_key(note['id'])]))
            for note in region_of(project)[1]['notes']]


def require_question_properties(leads, harmonics):
    # Require at least one positive lead after the boundary-clipped first note
    # in each run. Do not infer lead existence from the renderer or from r2.
    failures = [name for name, values in leads.items() if not any(v > 0 for v in values[1:])]
    if failures or harmonics['h2OverH1'] <= 1:
        raise ValueError('question properties absent: ' + json.dumps({
            'q1NoLead': failures, 'q1LeadFrames': leads, 'q2': harmonics}, sort_keys=True))


def verify(packet, expected_manifest_sha256=None):
    manifest_bytes = read_bounded(packet / 'manifest.json')
    manifest_hash = digest_bytes(manifest_bytes)
    if expected_manifest_sha256 is not None and expected_manifest_sha256 != manifest_hash:
        raise ValueError('expected manifest SHA-256 differs')
    manifest = parse_object(manifest_bytes).fields
    if manifest['formatId'] != FORMAT or manifest['schemaVersion'] != 1:
        raise ValueError('unsupported packet format')
    if manifest['listeningStatus'] != 'NOT_REVIEWED' or manifest['releaseEligible'] is not False:
        raise ValueError('diagnostic packet cannot grant acceptance')
    validate_cases(manifest['verdictSchema'], manifest['cases'])
    if manifest['settings'] != SETTINGS:
        raise ValueError('render settings differ')
    if inventory(packet) != manifest['fileHashes']:
        raise ValueError('packet file inventory/hash mismatch')
    if manifest['toolingSha256'] != {name:digest_bytes(read_bounded(packet / 'inputs/tooling' / name))
                                     for name in TOOL_MODULES}:
        raise ValueError('tooling identity mismatch')
    validate_guide(manifest['verdictSchema'], manifest['cases'],
                   read_bounded(packet / 'GUIDE.md').decode())
    evidence = read_json(packet / 'inputs/source-evidence.json')
    renderer_hash = digest_bytes(read_bounded(packet / 'inputs/renderer'))
    build_hash = digest_bytes(read_bounded(packet / 'inputs/build-evidence'))
    if (evidence['rendererSha256'] != renderer_hash or evidence['buildEvidenceSha256'] != build_hash
            or manifest['rendererSha256'] != renderer_hash
            or manifest['sourceCommit'] != evidence['sourceCommit']
            or not re.fullmatch(r'[0-9a-f]{40}', evidence['sourceCommit'])):
        raise ValueError('source/build/renderer identity mismatch')
    if [a['file'] for a in manifest['artifacts']] != list(NAMES):
        raise ValueError('artifact inventory differs from verdict contract')
    projects, metadata, audio = {}, {}, {}
    recipe_identity = None
    for artifact in manifest['artifacts']:
        name = artifact['file']
        stem = Path(name).stem
        project_path = packet / 'inputs' / stem / 'project.seam'
        project_bytes = read_bounded(project_path)
        project = parse_object(project_bytes).fields
        track, _ = region_of(project)
        recipe = track['proceduralRecipe']
        recipe_bytes = read_bounded(inspect_path(project_path.parent, recipe['path']))
        recipe_hash = digest_bytes(recipe_bytes)
        if recipe_hash != recipe['contentHash']:
            raise ValueError('recipe content identity mismatch')
        identity = canonical_json(recipe)
        if recipe_identity is not None and identity != recipe_identity:
            raise ValueError('cases use different recipe resources')
        recipe_identity = identity
        if (artifact['projectSha256'] != digest_bytes(project_bytes)
                or artifact['recipeSha256'] != recipe_hash
                or artifact['rendererSha256'] != renderer_hash
                or artifact['settingsSha256'] != digest_bytes(canonical_json(SETTINGS).encode())):
            raise ValueError('artifact input identity mismatch')
        raw = read_bounded(packet / name)
        mono, rate = parse_wav(raw)
        meta = read_json(inspect_path(packet / 'renders' / stem, artifact['candidateMetadata']))
        if (meta['formatId'] != 'com.project-seam.procedural-candidate'
                or meta['audioSha256'] != digest_bytes(raw)
                or artifact['sha256'] != digest_bytes(raw)
                or meta['recipeHash'] != recipe_hash or rate != SETTINGS['sampleRate']
                or meta['sampleRate'] != rate or meta['frameCount'] != len(mono)):
            raise ValueError('rendered audio/metadata identity mismatch')
        if np.sqrt(np.mean(mono * mono)) <= 0.001 or np.max(np.abs(mono)) <= 0.01:
            raise ValueError('diagnostic audio lacks signal')
        markers_by_note(meta, project)
        projects[name], metadata[name], audio[name] = project, meta, mono
    validate_design_projects(projects)
    authored_name, control_name = NAMES[-2:]
    target = manifest['q3NoteId']
    validate_q3_control(projects[authored_name], projects[control_name], target)
    authored, control = metadata[authored_name], metadata[control_name]
    # Every native renderer/candidate identity field stays equal except audio,
    # content hash and markers, whose actual differences are checked below.
    # Export schema is feature-dependent: removing the last closure changes
    # schema 11 to an earlier schema and omits closureRevision. This is not a
    # renderer switch (export_service.cpp candidateSchema). Preserve both raw
    # metadata files; compare all other renderer identities and normalized
    # non-target markers, including palatalized=false when omitted pre-v9.
    excluded = {'audioSha256', 'renderContentHash', 'markers', 'schemaVersion', 'closureRevision'}
    if ('closureRevision' in authored and 'closureRevision' in control
            and authored['closureRevision'] != control['closureRevision']):
        raise ValueError('Q3 closure renderer revision differs')
    if {k:v for k,v in authored.items() if k not in excluded} != {k:v for k,v in control.items() if k not in excluded}:
        raise ValueError('Q3 renderer identity or audio extent differs')
    a = markers_by_note(authored, projects[authored_name])
    b = markers_by_note(control, projects[control_name])
    key = note_key(target)
    if {k:v for k,v in a.items() if k != key} != {k:v for k,v in b.items() if k != key}:
        raise ValueError('Q3 non-target resolved markers changed')
    if len(a[key]) != 1 or len(b[key]) != 1 or a[key][0]['phone'] != 'pau' or b[key][0]['phone'] != 'a':
        raise ValueError('Q3 target did not resolve to pau versus a')
    start, end = a[key][0]['startFrame'], a[key][0]['endFrame']
    if (start, end) != (b[key][0]['startFrame'], b[key][0]['endFrame']):
        raise ValueError('Q3 target time slot changed')
    rest_rms = float(np.sqrt(np.mean(audio[authored_name][start:end] ** 2)))
    vowel_rms = float(np.sqrt(np.mean(audio[control_name][start:end] ** 2)))
    if rest_rms >= 0.002 or vowel_rms < 0.002:
        raise ValueError('Q3 rest/vowel signal property failed')
    common = ('renderAbi', 'compilerRevision', 'recipeHash', 'recipeId', 'recipeVersion', 'style')
    if any([metadata[name][k] for k in common] != [authored[k] for k in common] for name in NAMES):
        raise ValueError('cross-case renderer/compiler/recipe identity differs')
    for left, right in (NAMES[:2], NAMES[2:4]):
        fields = common + ('proceduralRevision', 'markerSemantics', 'schemaVersion')
        if any(metadata[left].get(k) != metadata[right].get(k) for k in fields):
            raise ValueError('paired renderer identity differs')
    leads = {name:onset_leads(projects[name], metadata[name]) for name in NAMES[:2]}
    harmonics = harmonic_balance(audio[NAMES[2]], SETTINGS['sampleRate'], 67)
    require_question_properties(leads, harmonics)
    return {'status': 'CONSISTENT_DIAGNOSTIC', 'manifestSha256': manifest_hash,
            'q1LeadFrames': leads, 'q2Harmonics': harmonics, 'q3RestRms': rest_rms,
            'q3VowelRms': vowel_rms, 'listeningStatus': 'NOT_REVIEWED',
            'limitation': 'Retained consistency, not independent build attestation or musical acceptance.'}


def build(request_path, output):
    request = read_json(request_path)
    if set(request['projects']) != set(NAMES):
        raise ValueError('request requires all six named projects')
    def supplied(value):
        return (request_path.parent / value).resolve(strict=True)
    cli = supplied(request['renderer'])
    executable = ExecutableIdentity.capture(cli)
    source_bytes = read_bounded(supplied(request['sourceEvidence']))
    evidence = parse_object(source_bytes).fields
    build_bytes = read_bounded(supplied(request['buildEvidence']))
    if evidence['rendererSha256'] != executable.sha256 or evidence['buildEvidenceSha256'] != digest_bytes(build_bytes):
        raise ValueError('supplied build evidence does not name this renderer/build log')
    # Validate all inputs before creating output or launching a renderer.
    staged = {}
    for name in NAMES:
        source = supplied(request['projects'][name])
        raw = read_bounded(source)
        project = parse_object(raw).fields
        track, _ = region_of(project)
        recipe = track['proceduralRecipe']
        recipe_path = relative_path(recipe['path'])
        recipe_bytes = read_bounded(inspect_path(source.parent, recipe_path))
        if digest_bytes(recipe_bytes) != recipe['contentHash']:
            raise ValueError('recipe content identity mismatch')
        staged[name] = (raw, project, recipe_path, recipe_bytes)
    validate_design_projects({name: values[1] for name, values in staged.items()})
    validate_q3_control(staged[NAMES[-2]][1], staged[NAMES[-1]][1], request['q3NoteId'])
    output.mkdir(parents=False, exist_ok=False)
    output = output.resolve()
    write_new(output / 'inputs/source-evidence.json', source_bytes)
    write_new(output / 'inputs/build-evidence', build_bytes)
    write_new(output / 'inputs/renderer', read_bounded(cli))
    tooling_hashes = {}
    for name in TOOL_MODULES:
        raw = read_bounded(Path(__file__).parent / name)
        write_new(output / 'inputs/tooling' / name, raw)
        tooling_hashes[name] = digest_bytes(raw)
    artifacts = []
    for name in NAMES:
        raw, project, recipe_path, recipe_bytes = staged[name]
        stem = Path(name).stem
        input_root = output / 'inputs' / stem
        write_new(input_root / 'project.seam', raw)
        write_new(input_root / recipe_path, recipe_bytes)
        render_root = output / 'renders' / stem
        render_root.parent.mkdir(exist_ok=True)
        capture_command(Command((str(cli), 'bake-project', str(input_root / 'project.seam'),
                                 str(render_root), str(SETTINGS['sampleRate'])), executable, stem), output)
        wavs = sorted(render_root.glob('candidates/*.wav'))
        if len(wavs) != 1:
            raise ValueError('renderer must produce exactly one candidate')
        candidate = wavs[0].with_suffix('.json')
        read_json(candidate)
        audio = read_bounded(wavs[0])
        write_new(output / name, audio)
        artifacts.append({'file':name, 'sha256':digest_bytes(audio),
                          'projectSha256':digest_bytes(raw), 'recipeSha256':digest_bytes(recipe_bytes),
                          'rendererSha256':executable.sha256,
                          'settingsSha256':digest_bytes(canonical_json(SETTINGS).encode()),
                          'candidateMetadata':candidate.relative_to(render_root).as_posix()})
    cases = case_definitions()
    write_new(output / 'GUIDE.md', guide_text(VERDICT_SCHEMA, cases).encode())
    manifest = {'formatId':FORMAT, 'schemaVersion':1, 'sourceCommit':evidence['sourceCommit'],
                'rendererSha256':executable.sha256, 'settings':SETTINGS,
                'verdictSchema':VERDICT_SCHEMA, 'cases':cases, 'artifacts':artifacts,
                'q3NoteId':request['q3NoteId'], 'listeningStatus':'NOT_REVIEWED',
                'releaseEligible':False, 'toolingSha256':tooling_hashes, 'fileHashes':inventory(output)}
    save_json(output / 'manifest.json', manifest)
    return verify(output)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='operation', required=True)
    create = sub.add_parser('build')
    create.add_argument('--request', required=True, type=Path)
    create.add_argument('--output', required=True, type=Path)
    check = sub.add_parser('verify')
    check.add_argument('packet', type=Path)
    check.add_argument('--expected-manifest-sha256')
    args = parser.parse_args(argv)
    try:
        result = (build(args.request.resolve(), args.output) if args.operation == 'build'
                  else verify(args.packet, args.expected_manifest_sha256))
        print(json.dumps(result, sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, TypeError, IndexError, CorpusError) as error:
        print('LISTENER_PACKET=FAIL: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
