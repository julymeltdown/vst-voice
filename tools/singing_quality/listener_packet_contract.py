"""Shared packet-003 questions, named answers, guide and matched Q3 edit.

These are diagnostic review definitions, never musical or release acceptance.
Change VERDICT_SCHEMA when changing a question or a named answer's meaning.
"""
from copy import deepcopy
import json

VERDICT_SCHEMA = 'com.project-seam.listener-verdicts/3'


def case_definitions():
    """Return fresh definitions so callers cannot mutate the canonical meanings."""
    return [
        {
            'id': 'q1-lead-timing',
            'question': 'Do the transitions sound intentional, early or rushed, or indistinguishable?',
            'files': ['q1-legato-run.wav', 'q1-detached-run.wav'],
            'verdictOptions': {
                'INTENTIONAL_PHRASING': 'The transitions sound like intentional phrasing.',
                'EARLY_OR_RUSHED': 'The transitions sound early or rushed.',
                'NO_AUDIBLE_DISTINCTION': 'I cannot hear a distinction relevant to this question.',
            },
        },
        {
            'id': 'q2-harmonic-balance',
            'question': 'Does the solo note and pitch range sound voice-like or thin/reedy?',
            'files': ['q2-single-weak-fundamental.wav', 'q2-pitch-range.wav'],
            'verdictOptions': {
                'VOICE_LIKE_BRIGHTNESS': 'The brightness sounds like a voice timbre.',
                'THIN_OR_REEDY': 'The balance sounds thin or reedy for a sung voice.',
                'NO_AUDIBLE_DISTINCTION': 'I cannot hear a distinction relevant to this question.',
            },
        },
        {
            'id': 'q3-rest-treatment',
            'question': 'Compared with the matched vowel control, how does the authored rest sound?',
            'files': ['q3-authored-rest.wav', 'q3-vowel-control.wav'],
            'verdictOptions': {
                'INTENTIONAL_REST': 'The silence sounds like an intentional rest or breath.',
                'UNINTENDED_HOLE': 'The silence sounds like an unintended hole in the phrase.',
                'NO_AUDIBLE_DISTINCTION': 'I cannot hear a distinction relevant to this question.',
            },
        },
    ]


def canonical_json(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, allow_nan=False,
                      separators=(',', ':'))


def validate_cases(schema, cases):
    if schema != VERDICT_SCHEMA or canonical_json(cases) != canonical_json(case_definitions()):
        raise ValueError('listener verdict schema, questions, files or named meanings differ')


def guide_text(schema, cases):
    validate_cases(schema, cases)
    lines = ['# Listening packet 003', '', f'Verdict schema: `{schema}`', '',
             'Status: NOT_REVIEWED. This diagnostic packet grants no musical or release acceptance.',
             'Record the named verdict, never an A/B/C letter. Retain the packet identity with your answers.',
             'Q1 changes written gaps; Q2 compares solo and range contexts. Only Q3 is a matched rest/vowel pair.', '']
    for case in cases:
        lines += [f"## {case['id']}", '', case['question'], '',
                  'Listen in this order: ' + ', '.join(f'`{name}`' for name in case['files']) + '.', '']
        lines += [f'- **{name}**: {meaning}' for name, meaning in case['verdictOptions'].items()]
        lines += ['', 'Reviewer verdict: NOT_REVIEWED', 'Reviewer notes:', '']
    return '\n'.join(lines) + '\n'


def validate_guide(schema, cases, text):
    if text != guide_text(schema, cases):
        raise ValueError('listener guide differs from the versioned verdict contract')


def make_q3_control(project, note_id):
    """Replace one Japanese authored rest with あ, preserving the full time slot.

    Reject ambiguous/shared lyrics instead of changing another note implicitly.
    This is a controlled project edit, not proof that either render is correct.
    """
    control = deepcopy(project)
    found = [(region, note) for track in control['vocalTracks'] for region in track['regions']
             for note in region['notes'] if note['id'] == note_id]
    if len(found) != 1:
        raise ValueError('Q3 requires one uniquely identified target note')
    region, note = found[0]
    lyrics = [item for item in region['lyrics'] if item['id'] == note['lyricId']]
    if len(lyrics) != 1:
        raise ValueError('Q3 requires one uniquely identified target lyric')
    lyric = lyrics[0]
    if sum(n['lyricId'] == note['lyricId'] for n in region['notes']) != 1:
        raise ValueError('Q3 target lyric is shared by other notes')
    if lyric.get('language', 'ja') != 'ja':
        raise ValueError('Q3 vowel control currently requires a Japanese target lyric')
    hint = note.get('phoneticHint')
    if hint != 'pau' and not (hint in (None, '') and lyric.get('surface') == 'pau'):
        raise ValueError('Q3 target is not an authored pau rest')
    note['phoneticHint'] = None
    lyric['surface'] = 'あ'
    # Reject non-JSON/non-finite inputs before they become retained evidence.
    canonical_json(control)
    return control


def validate_q3_control(authored, control, note_id):
    expected = make_q3_control(authored, note_id)
    if canonical_json(control) != canonical_json(expected):
        raise ValueError('Q3 control changes more than the target rest/vowel treatment')
