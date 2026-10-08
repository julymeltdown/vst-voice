"""Reject verdict drift and confounded controls before collecting human evidence."""
from copy import deepcopy
from pathlib import Path
import subprocess
import sys
import unittest

from tools.singing_quality.listener_packet_contract import (
    VERDICT_SCHEMA, case_definitions, guide_text, validate_cases, validate_guide,
    make_q3_control, validate_q3_control,
)


def project():
    return {
        'tempo': 120, 'recipe': {'identity': 'original-voice'},
        'vocalTracks': [{'regions': [{
            'startTick': 960, 'durationTick': 3000,
            'lyrics': [{'id': 'rest', 'language': 'ja', 'surface': 'pau'},
                       {'id': 'other', 'language': 'ja', 'surface': 'う'}],
            'notes': [{'id': 'n1', 'lyricId': 'rest', 'startTick': 1440,
                       'durationTick': 480, 'midiKey': 74, 'phoneticHint': 'pau'},
                      {'id': 'n2', 'lyricId': 'other', 'startTick': 1920,
                       'durationTick': 960, 'midiKey': 65, 'phoneticHint': None}],
        }]}],
    }


class ListenerVerdictContractTests(unittest.TestCase):
    def test_guide_and_manifest_have_identical_named_meanings(self):
        cases = case_definitions()
        text = guide_text(VERDICT_SCHEMA, cases)
        validate_cases(VERDICT_SCHEMA, cases)
        validate_guide(VERDICT_SCHEMA, cases, text)
        for case in cases:
            for name, meaning in case['verdictOptions'].items():
                self.assertIn(f'**{name}**: {meaning}', text)
                self.assertNotIn(name, ['A', 'B', 'C'])
        self.assertIn('NOT_REVIEWED', text)

    def test_swapped_q3_meanings_are_rejected(self):
        cases = case_definitions()
        answers = cases[2]['verdictOptions']
        answers['INTENTIONAL_REST'], answers['UNINTENDED_HOLE'] = (
            answers['UNINTENDED_HOLE'], answers['INTENTIONAL_REST'])
        with self.assertRaises(ValueError):
            validate_cases(VERDICT_SCHEMA, cases)
        with self.assertRaises(ValueError):
            guide_text(VERDICT_SCHEMA, cases)

    def test_guide_drift_is_rejected(self):
        cases = case_definitions()
        changed = guide_text(VERDICT_SCHEMA, cases).replace('**INTENTIONAL_REST**', '**UNINTENDED_HOLE**')
        with self.assertRaises(ValueError):
            validate_guide(VERDICT_SCHEMA, cases, changed)

    def test_schema_case_question_and_file_drift_are_rejected(self):
        with self.assertRaises(ValueError):
            validate_cases('com.project-seam.listener-verdicts/2', case_definitions())
        for field in ['id', 'question', 'files']:
            cases = case_definitions()
            cases[0][field] = ['unrelated.wav'] if field == 'files' else 'changed'
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_cases(VERDICT_SCHEMA, cases)
        with self.assertRaises(ValueError):
            validate_cases(VERDICT_SCHEMA, case_definitions()[:2])

    def test_returned_definitions_do_not_mutate_the_shared_contract(self):
        cases = case_definitions()
        cases[2]['verdictOptions']['INTENTIONAL_REST'] = 'changed'
        self.assertNotEqual(case_definitions(), cases)


class MatchedRestControlTests(unittest.TestCase):
    def test_only_the_selected_rest_treatment_changes(self):
        authored = project()
        before = deepcopy(authored)
        control = make_q3_control(authored, 'n1')
        self.assertEqual(authored, before)
        expected = deepcopy(before)
        region = expected['vocalTracks'][0]['regions'][0]
        region['notes'][0]['phoneticHint'] = None
        region['lyrics'][0]['surface'] = 'あ'
        self.assertEqual(control, expected)
        validate_q3_control(authored, control, 'n1')

    def test_changed_notes_timing_recipe_or_tempo_are_rejected(self):
        authored = project()
        for field in ['midiKey', 'durationTick', 'startTick', 'recipe', 'tempo', 'other_lyric']:
            control = make_q3_control(authored, 'n1')
            region = control['vocalTracks'][0]['regions'][0]
            if field in ('midiKey', 'durationTick', 'startTick'):
                region['notes'][1][field] += 1
            elif field == 'recipe':
                control['recipe']['identity'] = 'different-voice'
            elif field == 'tempo':
                control['tempo'] = 121
            else:
                region['lyrics'][1]['surface'] = 'い'
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_q3_control(authored, control, 'n1')

    def test_removed_rest_time_slot_is_rejected(self):
        authored = project()
        control = make_q3_control(authored, 'n1')
        control['vocalTracks'][0]['regions'][0]['notes'].pop(0)
        with self.assertRaises(ValueError):
            validate_q3_control(authored, control, 'n1')

    def test_shared_lyric_is_refused_instead_of_changing_another_note(self):
        authored = project()
        authored['vocalTracks'][0]['regions'][0]['notes'][1]['lyricId'] = 'rest'
        with self.assertRaisesRegex(ValueError, 'shared'):
            make_q3_control(authored, 'n1')

    def test_missing_or_duplicate_target_is_refused(self):
        authored = project()
        with self.assertRaisesRegex(ValueError, 'uniquely'):
            make_q3_control(authored, 'absent')
        authored['vocalTracks'][0]['regions'].append(deepcopy(authored['vocalTracks'][0]['regions'][0]))
        with self.assertRaisesRegex(ValueError, 'uniquely'):
            make_q3_control(authored, 'n1')

    def test_voiced_target_or_unsupported_language_is_refused(self):
        authored = project()
        with self.assertRaisesRegex(ValueError, 'pau rest'):
            make_q3_control(authored, 'n2')
        authored['vocalTracks'][0]['regions'][0]['lyrics'][0]['language'] = 'en'
        with self.assertRaisesRegex(ValueError, 'Japanese'):
            make_q3_control(authored, 'n1')

    def test_existing_cli_supports_script_and_module_invocation(self):
        root = Path(__file__).resolve().parents[2]
        for invocation in [[str(root / 'tools/singing_quality/verify_rest_note_is_authored.py')],
                           ['-m', 'tools.singing_quality.verify_rest_note_is_authored']]:
            with self.subTest(invocation=invocation):
                result = subprocess.run([sys.executable, *invocation, '--help'], cwd=root,
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
