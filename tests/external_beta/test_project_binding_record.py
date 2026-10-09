"""Policy-only record checks; real native codec/replay cases live in CTest."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
from tools.external_beta import project_binding_record as audit
from tools.external_beta.full_product_report import validate_full_product_report


def fixture():
    return audit.FIXED | dict(codecSchemaVersion=20,sourceSchemaVersion=20, storedVoicebankRole="SELECTED",projectSha256="a"*64, projectId="0000000000000001", trackId="0000000000000002",
        regionId="0000000000000003", payloadFamily="sample", resourceId="fixture.singer", resourceVersion="1",
        resourceContentHash="b"*64, languages=["ja"], noteCount=2, linkedLyricCount=1, unusedLyricCount=0)


class ProjectBindingTests(unittest.TestCase):
    def test_closed_record_is_reference_evidence_only(self):
        current=fixture(); audit.validate_record(current)
        audit.validate_record(current|{"codecSchemaVersion":21,"sourceSchemaVersion":21})
        for change in ({"humanAcceptance":"PASS"},{"resourceAdmission":"AVAILABLE"},{"playback":"PASS"},
                {"phonemization":"PASS"},{"authorizesRelease":True},{"command":"other"},
                {"codecSchemaVersion":19},{"sourceSchemaVersion":21},{"sourceSchemaVersion":True},{"projectSha256":"bad"},{"projectId":"0"*16},{"codecSchemaVersion":True},
                {"payloadFamily":[]},{"resourceId":"a\0b"},{"languages":["und"]},
                {"languages":["ja","en"]},{"languages":["ja","ja"]},{"languages":[{}]},
                {"noteCount":0},{"linkedLyricCount":3},{"unusedLyricCount":-1},
                {"noteCount":True},{"noteCount":250001}):
            with self.subTest(change=change),self.assertRaises(ValueError):
                audit.validate_record(current|change)
        self.assertTrue(validate_full_product_report(current,verify_references=False))

    def test_pins_comparison_and_fixed_command(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary); record=root/'record'; cli=root/'cli'; project=root/'project'
            cli.write_bytes(b'cli');record.write_text(json.dumps(fixture()))
            sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
            args=dict(record_path=record,record_sha256=sha(record),project_path=project,cli_path=cli,cli_sha256=sha(cli))
            with mock.patch.object(audit,'_run',return_value=fixture()) as run:
                result=audit.audit_project_binding_record(**args)
                self.assertTrue(result['passed']);self.assertFalse(result['authorizesRelease'])
                command=run.call_args.args[0]
                self.assertEqual([str(cli),'verify-project-binding',str(project)],command[:3])
                self.assertEqual('ja',command[-1])
            with mock.patch.object(audit,'_run',return_value=fixture()|{'noteCount':3}):
                result=audit.audit_project_binding_record(**args)
                self.assertFalse(result['passed']);self.assertIn('fresh native',result['errors'][0])
            with mock.patch.object(audit,'_run',side_effect=AssertionError('must not execute')):
                for name in ('record_sha256','cli_sha256'):
                    self.assertFalse(audit.audit_project_binding_record(**(args|{name:'f'*64}))['passed'])
                self.assertFalse(audit.audit_project_binding_record(**(args|{'timeout_seconds':float('nan')}))['passed'])
                record.write_text(json.dumps(fixture()|{'path':'untrusted'}))
                self.assertFalse(audit.audit_project_binding_record(**(args|{'record_sha256':sha(record)}))['passed'])


if __name__=='__main__':
    unittest.main()
