import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class Phase13AContractTests(unittest.TestCase):
    def test_dependency_lock_pins_exact_audited_revisions(self):
        value = json.loads((ROOT / 'phase13a/dependency-lock.json').read_text(encoding='utf-8'))
        commits = {item['name']: item['commit'] for item in value['dependencies']}
        self.assertEqual('195b42a004144fab0b3cf95e9c067187d15365b7', commits['clap'])
        self.assertEqual('35f524b771ec09f54c164720bb90f271273b37d3', commits['clap-wrapper'])
        self.assertEqual('3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96', commits['vst3sdk'])
        self.assertEqual('bd98b31feff57a15989fcfab4cd86dc63382b1ac', commits['AudioUnitSDK'])

    def test_mandatory_doc_explicitly_blocks_beta_rc_and_ga(self):
        text = (ROOT / 'docs/phase13a/MANDATORY_VALIDATION_KO.md').read_text(encoding='utf-8')
        for phrase in ('필수', 'NOT_RUN', 'Beta', 'Release Candidate', 'General Availability'):
            self.assertIn(phrase, text)
        self.assertIn('실제 대상 운영체제', text)
        self.assertIn('실제 DAW', text)

    def test_workflow_runs_vst3_validator_and_auval(self):
        text = (ROOT / '.github/workflows/phase13a-plugin-formats.yml').read_text(encoding='utf-8')
        self.assertIn('vst3-validator', text)
        self.assertIn('auval', text)
        self.assertIn('3cdf9ca5d1f5b1b21e0a86832aa4abe55607bd96', text)
        self.assertIn('bd98b31feff57a15989fcfab4cd86dc63382b1ac', text)
        self.assertIn('35f524b771ec09f54c164720bb90f271273b37d3', text)
        self.assertIn('--clap-path', text)
        self.assertIn('verify_phase13a_vst3_packet.py', text)
        self.assertIn('packet.json', text)
        self.assertIn('runner.json', text)

    def test_linux_vst3_validator_installs_required_gui_development_dependencies(self):
        text = (ROOT / '.github/workflows/phase13a-plugin-formats.yml').read_text(encoding='utf-8')
        for package in (
            'libxcb-util-dev',
            'libxcb-cursor-dev',
            'libxcb-keysyms1-dev',
            'libxcb-xkb-dev',
            'libxkbcommon-dev',
            'libxkbcommon-x11-dev',
            'libgtkmm-3.0-dev',
        ):
            self.assertIn(package, text)

    def test_release_gate_is_connected_to_phase13a_matrix(self):
        matrix = json.loads((ROOT / 'docs/phase13a/mandatory-validation-matrix.json').read_text(encoding='utf-8'))
        ids = {item['id'] for item in matrix['targets']}
        required = {
            'linux-vst3-validator', 'windows-vst3-validator', 'macos-vst3-validator',
            'macos-auval', 'windows-installer-clean-os', 'macos-installer-clean-os',
            'windows-authenticode', 'macos-notarization',
            'reaper', 'bitwig-studio', 'cubase', 'ableton-live', 'studio-one',
            'fl-studio', 'logic-pro', 'garageband'
        }
        self.assertTrue(required.issubset(ids), required - ids)
        self.assertTrue(all(item['runtimeResult'] != 'PASS' for item in matrix['targets']))

    def test_build_script_refuses_unverified_dependency_root(self):
        text = (ROOT / 'scripts/build_phase13a_formats.py').read_text(encoding='utf-8')
        self.assertIn('validate_checkout', text)
        self.assertIn('CLAP_SUPPORTS_ALL_NOTE_EXPRESSIONS', text)
        self.assertIn('CLAP_WRAPPER_BUILD_AUV2', text)
        self.assertIn('CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES', text)

    def test_build_script_binds_validator_passes_to_artifact_hashes(self):
        text = (ROOT / 'scripts/build_phase13a_formats.py').read_text(encoding='utf-8')
        self.assertIn('--vst3-validation-result', text)
        self.assertIn('--auval-validation-result', text)
        self.assertIn('read_validation_status', text)
        self.assertIn('validationEvidence', text)

    def test_validator_attachment_updates_existing_build_manifest_without_rebuild(self):
        script = (ROOT / 'scripts/attach_phase13a_validation.py').read_text(encoding='utf-8')
        workflow = (ROOT / '.github/workflows/phase13a-plugin-formats.yml').read_text(encoding='utf-8')
        self.assertIn('read_validation_status', script)
        self.assertIn('build_release_manifest', script)
        self.assertIn('attach_phase13a_validation.py', workflow)
        self.assertIn('--vst3-validation-result', workflow)
        self.assertIn('--auval-validation-result', workflow)
        self.assertIn("'scripts/attach_phase13a_validation.py'", workflow)

    def test_signed_distribution_preserves_vst3_validator_packet(self):
        workflow = (ROOT / '.github/workflows/phase13a-distribution.yml').read_text(encoding='utf-8')
        self.assertIn('verify_phase13a_vst3_packet.py', workflow)
        self.assertIn('runner.json', workflow)
        self.assertIn('packet.json', workflow)

    def test_macos_signing_refreshes_signature_dependent_wrapper_manifests(self):
        signing = (ROOT / 'scripts/sign_macos_plugin_payload.sh').read_text(encoding='utf-8')
        refresh = (ROOT / 'scripts/refresh_phase13a_wrapper_manifests.py').read_text(encoding='utf-8')
        manifests = (ROOT / 'tools/phase13a/distribution_manifest.py').read_text(encoding='utf-8')
        self.assertIn('refresh_phase13a_wrapper_manifests.py', signing)
        self.assertIn('${items[@]:1}', signing)
        self.assertIn('mutableSignaturePaths', refresh + manifests)

    def test_windows_signing_refreshes_wrapper_manifest_after_authenticode(self):
        signing = (ROOT / 'scripts/sign_windows_payload.ps1').read_text(encoding='utf-8')
        refresh = (ROOT / 'scripts/refresh_phase13a_wrapper_manifests.py').read_text(encoding='utf-8')
        self.assertIn('refresh_phase13a_wrapper_manifests.py', signing)
        self.assertIn('--platform windows', signing)
        self.assertIn('choices=("macos", "windows")', refresh)

    def test_signed_distribution_validates_after_signing_before_packaging(self):
        workflow = (ROOT / '.github/workflows/phase13a-distribution.yml').read_text(encoding='utf-8')
        self.assertLess(workflow.index('Sign the exact payload before runtime validation'), workflow.index('Run validators against the signed payload'))
        self.assertLess(workflow.index('Run validators against the signed payload'), workflow.index('Build signed PKG, notarize and staple'))
        self.assertIn('--canonical-clap-sha256', workflow)
        self.assertIn('--expected-sha256', workflow)
        self.assertIn('--component "$target"', workflow)
        self.assertIn('Validate signed Windows VST3 payload', workflow)
        self.assertIn('Attach signed Windows validator evidence', workflow)

    def test_wrapper_compatibility_patches_are_pinned_and_applied(self):
        build = (ROOT / 'scripts/build_phase13a_formats.py').read_text(encoding='utf-8')
        compatibility = (ROOT / 'packaging/phase13a/patches/clap-wrapper-vst3-editor-compatibility.patch').read_text(encoding='utf-8')
        self.assertIn('clap-wrapper-vst3-editor-compatibility.patch', build)
        self.assertIn('_plugin->_ext._params != nullptr', compatibility)
        self.assertIn('newSetup.sampleRate > 192000.0', compatibility)
        self.assertIn('dependencyPatches', build)

    def test_wrapper_clap_path_patch_is_pinned_and_applied(self):
        build = (ROOT / 'scripts/build_phase13a_formats.py').read_text(encoding='utf-8')
        clap_path_patch = (ROOT / 'packaging/phase13a/patches/clap-wrapper-clap-path.patch').read_text(encoding='utf-8')
        self.assertIn('clap-wrapper-clap-path.patch', build)
        self.assertIn('if (!cp.empty())', clap_path_patch)

    def test_clap_entry_lifecycle_keeps_deinit_lock_alive(self):
        plugin = (ROOT / 'libs/seam-clap-editor/src/plugin_entry.cpp').read_text(encoding='utf-8')
        self.assertIn('static auto* mutex = new std::mutex', plugin)
        self.assertIn('static auto* path = new std::filesystem::path', plugin)
        self.assertIn('std::scoped_lock lock(entryMutex())', plugin)

    def test_wrapper_contract_requires_modern_windows_package_and_offline_preflight(self):
        wrapper = (ROOT / 'packaging/phase13a/wrapper-project/CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('CLAP_WRAPPER_WINDOWS_SINGLE_FILE OFF', wrapper)
        self.assertIn('WINDOWS_FOLDER_VST3 TRUE', wrapper)
        self.assertIn('SEAM_WRAPPER_CXX_STANDARD 23', wrapper)
        self.assertIn('CMAKE_OSX_DEPLOYMENT_TARGET "12.0"', wrapper)
        self.assertNotIn('WINDOWS_FOLDER_VST3 FALSE', wrapper)
        self.assertTrue((ROOT / 'tools/phase13a/wrapper_preflight.py').is_file())
        self.assertTrue((ROOT / 'tools/phase13a/wrapper_state.py').is_file())

    def test_wrapper_contract_enables_pic_for_shared_vst3_module(self):
        wrapper = (ROOT / 'packaging/phase13a/wrapper-project/CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('CMAKE_POSITION_INDEPENDENT_CODE ON', wrapper)

    def test_format_builder_discovers_vst3_in_declared_artifact_output(self):
        build = (ROOT / 'scripts/build_phase13a_formats.py').read_text(encoding='utf-8')
        self.assertIn('first_from((wrapper_build, output), "ProjectSEAMEditor.vst3")', build)

    def test_offline_documentation_manifest_is_bundled_by_format_builder(self):
        build = (ROOT / 'scripts/build_phase13a_formats.py').read_text(encoding='utf-8')
        self.assertIn('external-beta-documentation.json', build)
        self.assertIn('Documentation', build)

    def test_separate_future_validation_document_is_mandatory(self):
        text = (ROOT / 'docs/phase13a/MANDATORY_FUTURE_VALIDATION_KO.md').read_text(encoding='utf-8')
        for phrase in ('반드시', '실제 대상', 'NOT_RUN', '7,200초', 'Official Voicebank 01'):
            self.assertIn(phrase, text)

    def test_windows_installer_uses_pinned_permissive_nsis_path(self):
        script = (ROOT / 'packaging/windows/ProjectSEAM.nsi').read_text(encoding='utf-8')
        build = (ROOT / 'scripts/build_windows_installer.ps1').read_text(encoding='utf-8')
        lock = json.loads((ROOT / 'phase13a/tool-lock.json').read_text(encoding='utf-8'))
        self.assertIn('SetCompressor zlib', script)
        self.assertIn('ProjectSEAMEditor.resources', script)
        self.assertIn('NSIS 3.12', build)
        self.assertEqual('3.12', lock['tools'][0]['version'])
        self.assertEqual('Zlib', lock['tools'][0]['license'])

    def test_clean_installer_tests_write_machine_readable_evidence(self):
        windows = (ROOT / 'scripts/test_windows_installer.ps1').read_text(encoding='utf-8')
        macos = (ROOT / 'scripts/test_macos_installer.sh').read_text(encoding='utf-8')
        workflow = (ROOT / '.github/workflows/phase13a-distribution.yml').read_text(encoding='utf-8')
        self.assertIn('EvidenceDirectory', windows)
        self.assertIn('result.json', windows)
        self.assertIn('evidence directory required', macos)
        self.assertIn('result.json', macos)
        self.assertIn('installer-clean', workflow)

    def test_macos_payload_signing_defers_gatekeeper_to_notarized_pkg(self):
        script = (ROOT / 'scripts/sign_macos_plugin_payload.sh').read_text(encoding='utf-8')
        self.assertIn('codesign --verify --deep --strict', script)
        self.assertIn('DEFERRED_TO_NOTARIZED_PKG', script)
        self.assertNotIn('spctl --assess --type execute', script)
        notarize = (ROOT / 'scripts/notarize_macos_installer.sh').read_text(encoding='utf-8')
        self.assertIn('spctl --assess --type install', notarize)

    def test_dependency_fetcher_is_self_contained_and_exact(self):
        text = (ROOT / 'scripts/fetch_phase13a_dependencies.py').read_text(encoding='utf-8')
        self.assertNotIn('from acquire_phase13a_dependencies', text)
        self.assertIn('git', text)
        self.assertIn('checkout', text)
        self.assertIn('validate_checkout', text)

    def test_format_builder_handles_macos_bundle_resources(self):
        text = (ROOT / 'scripts/build_phase13a_formats.py').read_text(encoding='utf-8')
        self.assertIn('Contents" / "Resources', text)
        self.assertIn('resource_directory', text)


if __name__ == '__main__':
    unittest.main()
