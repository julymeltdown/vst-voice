import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class MacOSSourceContractTests(unittest.TestCase):
    def test_bundle_identity_and_document_type_are_declared(self) -> None:
        plist_path = ROOT / "packaging/macos/ProjectSEAM-App-Info.plist.in"
        plist = plist_path.read_text()
        self.assertIn("<string>com.project-seam.standalone</string>", plist)
        self.assertIn("<string>APPL</string>", plist)
        self.assertIn("<string>Project SEAM</string>", plist)
        self.assertIn("<string>13.0</string>", plist)
        self.assertIn("<string>@SEAM_BUILD_ID@</string>", plist)
        self.assertIn("<string>com.project-seam.project</string>", plist)
        self.assertIn("<string>seam</string>", plist)

    def test_bundle_signing_and_resource_contract_is_fail_closed(self) -> None:
        entitlements = (ROOT / "packaging/macos/ProjectSEAM.entitlements").read_text()
        script = (ROOT / "scripts/package_macos_standalone.sh").read_text()
        self.assertNotIn("get-task-allow", entitlements)
        self.assertNotIn("allow-jit", entitlements)
        self.assertIn('lipo "$executable" -verify_arch arm64', script)
        self.assertIn("CFBundleExecutable", script)
        self.assertIn("codesign --force --options runtime", script)
        self.assertIn("MACOS_UNSIGNED_STANDALONE=PASS", script)
        self.assertNotIn("SEAM_SOURCE_PRODUCTION_VOICEBANK", script)

    def test_bundle_rejects_placeholder_source_commit_identity(self) -> None:
        script = (ROOT / "scripts/package_macos_standalone.sh").read_text()
        self.assertIn('[[ "$source_commit" =~ ^0+$ ]]', script)
        self.assertIn("placeholder source commit identity is not packageable", script)

    def test_cmake_wires_bundle_delegate_and_owned_resources(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text()
        menu = (ROOT / "libs/seam-platform/src/application_menu_appkit.mm").read_text()
        entry = (ROOT / "apps/seam-editor-native/main.cpp").read_text()
        native_app = (ROOT / "libs/seam-standalone/src/native_editor_app.cpp").read_text()
        application_controller = (ROOT / "libs/seam-standalone/src/application_controller.cpp").read_text()
        self.assertIn("MACOSX_BUNDLE TRUE", cmake)
        self.assertIn('OUTPUT_NAME "Project SEAM"', cmake)
        self.assertIn('MACOSX_BUNDLE_BUNDLE_NAME "Project SEAM"', cmake)
        self.assertIn("macos_application_delegate.mm", cmake)
        self.assertIn("native_project_dialog_appkit.mm", cmake)
        self.assertIn("ProjectSEAM-App-Info.plist.in", cmake)
        self.assertIn("TARGET_BUNDLE_DIR:seam_editor_native", cmake)
        self.assertIn("Relink Voicebank Search Folder", menu)
        self.assertIn('DocumentationSpec{"quick-start", "Quick Start", "QUICK_START.md"}', application_controller)
        self.assertIn("Relink Backing Audio", menu)
        self.assertIn("Audio Settings", menu)
        self.assertIn(".manualsRoot = paths.value().manualsRoot", entry)
        self.assertIn("config_.manualsRoot", native_app)

    def test_native_new_project_form_collects_exact_authoring_choices(self) -> None:
        dialog = (ROOT / "libs/seam-standalone/src/native_project_dialog_appkit.mm").read_text()
        self.assertIn("Create New Project", dialog)
        self.assertIn("Time signature", dialog)
        self.assertIn("Initial Voicebank", dialog)
        self.assertIn("setProjectPath", dialog)
        self.assertIn("suggestedProjectName", dialog)
        self.assertIn("toggleVoicebank", dialog)

    def test_export_set_uses_a_new_destination_save_surface(self) -> None:
        source = (ROOT / "libs/seam-platform/src/file_dialog_appkit.mm").read_text()
        self.assertIn(
            "const bool directory = request.purpose == FileDialogPurpose::RelinkVoicebank",
            source,
        )
        self.assertIn(
            "request.purpose == FileDialogPurpose::ExportSet;",
            source,
        )
        self.assertIn("panel.canCreateDirectories =", source)

    def test_appkit_open_files_ignores_non_project_launch_arguments(self) -> None:
        delegate = (ROOT / "apps/seam-editor-native/macos_application_delegate.mm").read_text()
        entry = (ROOT / "apps/seam-editor-native/main.cpp").read_text()
        self.assertIn('projectPath.extension() != ".seam"', delegate)
        self.assertIn("beginActivityWithOptions", delegate)
        self.assertIn("endActivity", delegate)
        native_app = (ROOT / "libs/seam-standalone/src/native_editor_app.cpp").read_text()
        window = (ROOT / "libs/seam-native-ui/src/native_window_appkit.mm").read_text()
        self.assertIn('frameAutosaveName = @"ProjectSEAM.Editor"', window)
        self.assertIn("setFrameUsingName", window)
        self.assertIn("restoreLastDocument", window)
        self.assertIn("ProjectSEAM.LastDocumentPath", window)
        self.assertIn("persistDocumentPath", window)
        self.assertIn("saveRestorationState", window)
        self.assertIn("documentPath", window)
        self.assertIn('path.extension() != ".seam"', window)
        self.assertIn(
            "if (accepted && window_ != nullptr) window_->saveRestorationState();",
            native_app,
        )
        self.assertIn("render_state=", entry)
        self.assertIn("render_diagnostic=", entry)

    def test_native_runtime_probe_is_fail_closed_and_observable(self) -> None:
        entry = (ROOT / "apps/seam-editor-native/main.cpp").read_text()
        native_app = (ROOT / "libs/seam-standalone/src/native_editor_app.cpp").read_text()
        self.assertIn('"  --play', entry)
        self.assertIn("result.startPaused = false", entry)
        self.assertIn("startTransportWhenReady", entry)
        self.assertIn("return app.startAudioForPlayback()", entry)
        self.assertIn("availableReadFrames() < targetFrames", native_app)
        self.assertIn("stopAudioForPlayback();", native_app)
        self.assertIn("Unable to open startup project", entry)
        self.assertIn("Unable to start requested playback", entry)
        self.assertIn('"audio_frames="', entry)
        self.assertIn('"audio_write_failures="', entry)
        self.assertIn('"audio_xruns="', entry)
        self.assertIn('"callback_intentional_reset="', entry)

    def test_appkit_presentation_preserves_top_left_raster_orientation(self) -> None:
        source = (ROOT / "libs/seam-native-ui/src/native_window_appkit.mm").read_text()
        self.assertIn("CGContextSaveGState(context)", source)
        self.assertIn(
            "CGContextTranslateCTM(context, 0.0, view.bounds.size.height)",
            source,
        )
        self.assertIn("CGContextScaleCTM(context, 1.0, -1.0)", source)
        self.assertIn("CGContextRestoreGState(context)", source)

    def test_appkit_accessibility_exposes_stable_virtual_note_pages(self) -> None:
        source = (ROOT / "libs/seam-native-ui/src/native_window_appkit.mm").read_text()
        self.assertIn("SeamAccessibilityNotesPage", source)
        self.assertIn("accessibilityIdentifier", source)
        self.assertIn("accessibilityRoleDescription", source)
        self.assertIn("accessibilityNotes", source)
        self.assertIn("makeAccessibilityElement(child, element)", source)

    def test_accessibility_focus_and_toolbar_roles_match_the_painted_controls(self) -> None:
        semantics = (ROOT / "libs/seam-native-ui/src/editor_semantics.cpp").read_text()
        appkit = (ROOT / "libs/seam-native-ui/src/native_window_appkit.mm").read_text()
        self.assertIn('"toolbar.controls"', semantics)
        self.assertIn('"toolbar.transport"', semantics)
        self.assertIn("transportBoundsForWidth", semantics)
        self.assertIn("setAccessibilityFocused", appkit)
        self.assertIn("accessibilityFocusedUIElement", appkit)
        self.assertIn("accessibilitySelected", appkit)
        self.assertIn("node.editableValue", appkit)
        self.assertIn("NSAccessibilityTextFieldRole", appkit)
        self.assertIn("accessibilityHelp", appkit)
        self.assertIn("accessibilityIsAttributeSettable", appkit)
        self.assertIn("accessibilitySetValue", appkit)
        self.assertIn("setAccessibilityValue", appkit)
        self.assertIn("NSAccessibilityValueAttribute", appkit)
        self.assertIn("NSAccessibilityFocusedUIElementChangedNotification", appkit)
        self.assertIn("accessibilityAnnouncementPending_", appkit)
        self.assertNotIn("NSAccessibilityShowMenuAction", appkit)

    def test_accessibility_tree_does_not_eagerly_materialize_all_notes(self) -> None:
        tree = (ROOT / "libs/seam-native-ui/src/accessibility_tree.cpp").read_text()
        self.assertIn("EditorSemanticTree::build(state, model, {}, false, false)", tree)
        self.assertIn("model_->noteAt(index)", tree)
        self.assertNotIn("allNotes()", tree)

    def test_note_spatial_queries_prune_non_overlapping_prefixes(self) -> None:
        header = (ROOT / "libs/seam-editor-ui/include/seam/ui/note_spatial_index.hpp").read_text()
        source = (ROOT / "libs/seam-editor-ui/src/note_spatial_index.cpp").read_text()
        self.assertIn("prefixMaximumEnd_", header)
        self.assertIn("prefixMaximumEnd_.resize(notes_.size())", source)
        self.assertIn("std::upper_bound", source)
        self.assertIn("notes_.begin() + firstOffset", source)

    def test_coreaudio_adapts_output_and_input_to_hardware_callback_slices(self) -> None:
        output = (ROOT / "libs/seam-platform/src/coreaudio_audio_device.mm").read_text()
        input_source = (
            ROOT / "libs/seam-platform/src/coreaudio_audio_input_device.mm"
        ).read_text()
        self.assertIn("kAudioDevicePropertyBufferFrameSize", output)
        self.assertIn("maximumFramesPerSlice_", output)
        self.assertNotIn("frameCount > self->config_.blockFrames", output)
        self.assertIn("kAudioDevicePropertyBufferFrameSize", input_source)
        self.assertIn("mono_.assign(maximumFrames", input_source)
        self.assertNotIn("mono_.assign(config.blockFrames", input_source)

    def test_voicebank_studio_exposes_a_fail_closed_recording_probe(self) -> None:
        source = (
            ROOT / "apps/seam-voicebank-studio-native/main.cpp"
        ).read_text()
        self.assertIn('"  --record-ms N', source)
        self.assertIn('"input_callbacks="', source)
        self.assertIn('"input_read_failures="', source)
        self.assertIn('"recorded_frames="', source)
        self.assertIn('"recorded_wav="', source)
        self.assertIn("Voicebank Studio recording failed", source)
        self.assertIn("recording_.recordedFrames() > 0U", source)
        self.assertLess(
            source.index("if (!saved) return saved"),
            source.index("recording_.clear()"),
        )

    def test_demo_projects_persist_computed_voicebank_content_identity(self) -> None:
        for phase in ("2", "3", "4"):
            source = (ROOT / f"apps/seam-phase{phase}-demo/main.cpp").read_text()
            self.assertIn("computeVoicebankContentHash", source)
            self.assertNotIn(f"phase{phase}-synthetic-content", source)

    def test_embedded_clap_view_exposes_the_shared_accessibility_tree(self) -> None:
        embedded = (ROOT / "libs/seam-clap-editor/src/embedded_view_appkit.mm").read_text()
        runtime_header = (ROOT / "libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp").read_text()
        runtime_source = (ROOT / "libs/seam-clap-editor/src/editor_runtime_accessibility.cpp").read_text()
        self.assertIn("SeamClapAccessibilityElement", embedded)
        self.assertIn("SeamClapNotesPage", embedded)
        self.assertIn("accessibilityPerformAction", embedded)
        self.assertIn("NSAccessibilityConfirmAction", embedded)
        self.assertIn("setAccessibilityFocused", embedded)
        self.assertIn("accessibilityFocusedUIElement", embedded)
        self.assertIn("accessibilityAnnouncementPending_", embedded)
        self.assertIn("isAccessibilityEnabled", embedded)
        self.assertIn("NSAccessibilityValueChangedNotification", embedded)
        self.assertIn("accessibilitySnapshotDirty_", embedded)
        self.assertIn("memory_order_acq_rel", embedded)
        self.assertIn("accessibilityFocusedNode", runtime_header)
        self.assertIn("accessibilitySelected", embedded)
        self.assertIn("node.editableValue", embedded)
        self.assertIn("NSAccessibilityTextFieldRole", embedded)
        self.assertIn("accessibilityHelp", embedded)
        self.assertIn("accessibilityIsAttributeSettable", embedded)
        self.assertIn("accessibilitySetValue", embedded)
        self.assertIn("setAccessibilityValue", embedded)
        self.assertIn("NSAccessibilityValueAttribute", embedded)
        self.assertIn("accessibilitySnapshot", runtime_header)
        self.assertIn("materializeNotes", runtime_source)
        self.assertIn("dispatchAccessibility", runtime_source)
        self.assertIn("setAccessibilityValue", runtime_header)
        self.assertIn("setAccessibilityValue", runtime_source)


if __name__ == "__main__":
    unittest.main()
