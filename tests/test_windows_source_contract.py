import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class WindowsSourceContractTests(unittest.TestCase):
    def test_unicode_entry_and_single_instance_forwarding_exist(self) -> None:
        entry = (ROOT / "apps/seam-editor-native/windows_application.cpp").read_text()
        window = (ROOT / "libs/seam-native-ui/src/native_window_win32.cpp").read_text()
        accessibility = (ROOT / "libs/seam-native-ui/src/accessibility_win32.cpp").read_text()
        new_project = (ROOT / "libs/seam-standalone/src/native_project_dialog_win32.cpp").read_text()
        self.assertIn("wWinMain", entry)
        self.assertIn("CommandLineToArgvW", entry)
        self.assertIn("CreateMutexW", entry)
        self.assertIn("WM_COPYDATA", window)
        self.assertIn("openProjectPath", window)
        self.assertIn("WM_GETOBJECT", window)
        self.assertIn("UiaReturnRawElementProvider", accessibility)
        self.assertIn("UiaRaiseAutomationEvent", accessibility)
        self.assertIn("UIA_AutomationFocusChangedEventId", accessibility)
        self.assertIn("UiaRaiseAutomationPropertyChangedEvent", accessibility)
        self.assertIn("announcedSnapshot", accessibility)
        self.assertIn("UIA_SelectionItemIsSelectedPropertyId", accessibility)
        self.assertIn("UIA_ToggleToggleStatePropertyId", accessibility)
        self.assertIn("UIA_ValueValuePropertyId", accessibility)
        self.assertIn("UIA_EditControlTypeId", accessibility)
        self.assertIn("int controlType(const SemanticNode& node)", accessibility)
        self.assertIn("UIA_DescriptionPropertyId", accessibility)
        self.assertIn("current->node.description", accessibility)
        self.assertIn("UIA_SelectionItemIsSelectedPropertyId", accessibility)
        self.assertIn("announceFocusedElement", accessibility)
        self.assertIn("IRawElementProviderFragmentRoot", accessibility)
        self.assertIn("UIA_InvokePatternId", accessibility)
        self.assertIn("UIA_TogglePatternId", accessibility)
        self.assertIn("IToggleProvider", accessibility)
        self.assertIn("UIA_SelectionItemPatternId", accessibility)
        self.assertIn("ISelectionItemProvider", accessibility)
        self.assertIn("get_SelectionContainer", accessibility)
        self.assertIn("UIA_SelectionPatternId", accessibility)
        self.assertIn("ISelectionProvider", accessibility)
        self.assertIn("GetSelection", accessibility)
        self.assertIn("get_CanSelectMultiple", accessibility)
        self.assertIn("UIA_ValuePatternId", accessibility)
        self.assertIn("IValueProvider", accessibility)
        self.assertIn("utf8FromWide", accessibility)
        self.assertIn("setAccessibilityValue", accessibility)
        self.assertIn("current->node.editableValue", accessibility)
        self.assertIn("CreateWindowExW", new_project)
        self.assertIn("GetSaveFileNameW", new_project)
        self.assertIn("Time signature", new_project)
        self.assertIn("setProjectPath", new_project)
        self.assertIn("EnableWindow(bank_", new_project)

    def test_embedded_clap_view_uses_the_shared_ui_automation_bridge(self) -> None:
        embedded = (ROOT / "libs/seam-clap-editor/src/embedded_view_win32.cpp").read_text()
        self.assertIn("Win32AccessibilityClient", embedded)
        self.assertIn("Win32AccessibilityBridge", embedded)
        self.assertIn("WM_GETOBJECT", embedded)
        self.assertIn("accessibilityBridge_->invalidate", embedded)
        self.assertIn("dispatchAccessibility", embedded)
        self.assertIn("setAccessibilityValue", embedded)
        bridge = (ROOT / "libs/seam-native-ui/src/accessibility_win32.cpp").read_text()
        self.assertIn("virtualizedNoteCount", bridge)
        self.assertIn("expandPage", bridge)
        self.assertIn("noteLimit", bridge)
        self.assertIn("GetClientRect", bridge)
        self.assertIn("ClientToScreen", bridge)
        self.assertIn("screenRectangle", bridge)
        self.assertIn("logicalPoint", bridge)
        page_start = bridge.index("SemanticNode page{")
        page_end = bridge.index("};", page_start)
        self.assertIn(".actions = {},", bridge[page_start:page_end])
        self.assertIn("UIA_E_ELEMENTNOTAVAILABLE", bridge)

    def test_ui_automation_runtime_ids_follow_stable_semantic_ids(self) -> None:
        bridge = (ROOT / "libs/seam-native-ui/src/accessibility_win32.cpp").read_text()
        self.assertIn("semanticRuntimeId", bridge)
        self.assertIn("current->node.id", bridge)
        self.assertNotIn("LONG index = index_ + 1", bridge)

    def test_ui_automation_get_focus_expands_lazy_note_pages(self) -> None:
        bridge = (ROOT / "libs/seam-native-ui/src/accessibility_win32.cpp").read_text()
        start = bridge.index("AccessibilityElement::GetFocus")
        end = bridge.index("AccessibilityElement::Invoke", start)
        self.assertIn("expandPage(snapshot_", bridge[start:end])
        self.assertIn("noteLimit", bridge[start:end])
        self.assertIn("focusedNode", bridge)

    def test_windows_payload_owns_standalone_and_identity(self) -> None:
        nsi = (ROOT / "packaging/windows/ProjectSEAM.nsi").read_text()
        build_script = (ROOT / "scripts/build_windows_installer.ps1").read_text()
        self.assertIn("${PAYLOAD_ROOT}\\Standalone\\seam_editor_native.exe", nsi)
        self.assertIn("${PAYLOAD_ROOT}\\RELEASE_IDENTITY.json", nsi)
        self.assertIn("!ifndef PRODUCT_VERSION", nsi)
        self.assertIn("!ifndef BUILD_ID", nsi)
        self.assertIn("!ifndef SOURCE_COMMIT", nsi)
        self.assertIn("Standalone\\seam_editor_native.exe", build_script)
        self.assertIn(
            "Payload sourceCommit must be a 40-character hexadecimal commit",
            build_script,
        )

    def test_export_set_uses_a_new_destination_save_dialog(self) -> None:
        source = (ROOT / "libs/seam-platform/src/file_dialog_win32.cpp").read_text()
        self.assertIn(
            "const bool directory = request.purpose == FileDialogPurpose::RelinkVoicebank",
            source,
        )
        self.assertIn(
            "request.purpose == FileDialogPurpose::ExportSet;",
            source,
        )
        self.assertIn("CLSID_FileSaveDialog", source)
        self.assertIn("FOS_PICKFOLDERS", source)

    def test_cmake_wires_unicode_gui_entry_and_version_resource(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text()
        rc = (ROOT / "packaging/windows/ProjectSEAM.rc.in").read_text()
        self.assertIn("windows_application.cpp", cmake)
        self.assertIn("WIN32_EXECUTABLE TRUE", cmake)
        self.assertIn("generated/ProjectSEAM.rc", cmake)
        self.assertIn("ProjectSEAMBuildID", rc)
        self.assertIn("ProjectSEAMSourceCommit", rc)
        self.assertIn("uiautomationcore", cmake)
        self.assertIn("oleacc", cmake)
        self.assertIn("comdlg32", cmake)


if __name__ == "__main__":
    unittest.main()
