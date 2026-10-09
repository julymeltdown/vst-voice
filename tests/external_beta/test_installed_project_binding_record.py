"""Composite linkage policy; real package/install/project replay is covered natively."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
from tools.external_beta import installed_project_binding_record as audit
from tools.external_beta.full_product_report import validate_full_product_report
from tests.external_beta.test_installed_candidate_record import fixture as installed_fixture
from tests.external_beta.test_project_binding_record import fixture as project_fixture


def fixture(recipe=False):
    resource=installed_fixture(); binding=project_fixture()
    binding.update(resourceId=resource['resourceId'],resourceVersion=resource['resourceVersion'],
        resourceContentHash=resource['candidateContentSha256'])
    if recipe:
        resource.update(resourceKind='recipe-original',payloadFamily='recipe',resourceId='package.alias',
            resourceVersion='9.0.0',installedContentHash='b'*64,
            externalDependencies=[dict(kind='render-engine',id='engine',revision='1')])
        binding.update(payloadFamily='recipe',resourceId='recipe.original',resourceVersion='11',storedVoicebankRole='INACTIVE')
    return audit.FIXED|dict(installed=resource,project=binding,resourceLanguages=['ja'])


class InstalledProjectBindingTests(unittest.TestCase):
    def test_family_specific_identities_and_closed_non_authorizing_shape(self):
        for recipe in (False,True):
            record=fixture(recipe);audit.validate_record(record)
            self.assertTrue(validate_full_product_report(record,verify_references=False))
            for change in ({'authorizesRelease':True},{'runtimeAvailability':'AVAILABLE'},
                    {'languageCoverage':'QUALIFIED'},{'command':'untrusted'},{'resourceLanguages':['ko']},
                    {'resourceLanguages':['ja','ja']},{'resourceLanguages':[{}]}):
                with self.subTest(recipe=recipe,change=change),self.assertRaises(ValueError):
                    audit.validate_record(record|change)
            changed=copy.deepcopy(record);changed['project']['resourceContentHash']='f'*64
            with self.assertRaises(ValueError):audit.validate_record(changed)
            changed=copy.deepcopy(record);changed['project']['humanAcceptance']='PASS'
            with self.assertRaises(ValueError):audit.validate_record(changed)
        record=fixture();record['project']['resourceVersion']='wrong'
        with self.assertRaises(ValueError):audit.validate_record(record)
        record=fixture();record['installed']=installed_fixture(1)
        with self.assertRaises(ValueError):audit.validate_record(record)
        record=fixture(True);record['resourceLanguages']=['und','ja']
        audit.validate_record(record) # signed declaration may contain und; note-linked project languages may not.

    def test_replay_pins_fixed_arguments_and_derives_recipe_identity_natively(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);record=root/'record';cli=root/'cli';key=root/'key'
            current=fixture(True);record.write_text(json.dumps(current));cli.write_bytes(b'cli');key.write_bytes(b'key')
            sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
            args=dict(record_path=record,record_sha256=sha(record),project_path=root/'project',
                package_path=root/'package',installed_directory=root/'installed',public_key_path=key,
                public_key_sha256=sha(key),cli_path=cli,cli_sha256=sha(cli))
            with mock.patch.object(audit.installed,'_run',return_value=current) as run:
                result=audit.audit_installed_project_binding_record(**args)
                self.assertTrue(result['passed']);self.assertFalse(result['authorizesRelease'])
                command=run.call_args.args[0]
                self.assertEqual('verify-installed-project-binding',command[1]);self.assertEqual(str(cli),command[0])
                self.assertEqual(str(root/'project'),command[2]);self.assertEqual(str(key),command[-1])
                self.assertNotIn('recipe.original',command) # native derives it, not the retained claim.
            forged=copy.deepcopy(current);forged['project']['resourceId']='forged.recipe'
            record.write_text(json.dumps(forged));args['record_sha256']=sha(record)
            with mock.patch.object(audit.installed,'_run',return_value=current):
                result=audit.audit_installed_project_binding_record(**args)
                self.assertFalse(result['passed']);self.assertIn('fresh native',result['errors'][0])
            with mock.patch.object(audit.installed,'_run',side_effect=AssertionError('must not execute')):
                for name in ('record_sha256','cli_sha256','public_key_sha256'):
                    self.assertFalse(audit.audit_installed_project_binding_record(**(args|{name:'f'*64}))['passed'])


if __name__=='__main__':unittest.main()
