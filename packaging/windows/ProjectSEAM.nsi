Unicode true
RequestExecutionLevel admin
SetCompressor zlib

!ifndef PAYLOAD_ROOT
  !error "PAYLOAD_ROOT is required"
!endif
!ifndef OUTPUT_EXE
  !error "OUTPUT_EXE is required"
!endif

Name "Project SEAM"
OutFile "${OUTPUT_EXE}"
InstallDir "$COMMONAPPDATA\ProjectSEAM"
BrandingText "Project SEAM 0.13.0"
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "0.13.0.0"
VIAddVersionKey "ProductName" "Project SEAM"
VIAddVersionKey "CompanyName" "Project SEAM"
VIAddVersionKey "FileDescription" "Project SEAM plug-in installer"
VIAddVersionKey "FileVersion" "0.13.0"
VIAddVersionKey "LegalCopyright" "Project SEAM"

Section "Project SEAM plug-ins" SEC_MAIN
  SetShellVarContext all

  SetOutPath "$COMMONFILES64\CLAP"
  File "${PAYLOAD_ROOT}\CLAP\ProjectSEAMEditor.clap"

  RMDir /r "$COMMONFILES64\CLAP\ProjectSEAMEditor.resources"
  SetOutPath "$COMMONFILES64\CLAP\ProjectSEAMEditor.resources"
  File /r "${PAYLOAD_ROOT}\CLAP\ProjectSEAMEditor.resources\*.*"

  SetOutPath "$COMMONFILES64\VST3"
  File "${PAYLOAD_ROOT}\VST3\ProjectSEAMEditor.vst3"

  SetOutPath "$COMMONAPPDATA\ProjectSEAM"
  File "${PAYLOAD_ROOT}\THIRD_PARTY_NOTICES.md"
  File "${PAYLOAD_ROOT}\SBOM.spdx.json"
  WriteUninstaller "$COMMONAPPDATA\ProjectSEAM\Uninstall.exe"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "DisplayName" "Project SEAM"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "DisplayVersion" "0.13.0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "Publisher" "Project SEAM"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "UninstallString" '"$COMMONAPPDATA\ProjectSEAM\Uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  Delete "$COMMONFILES64\CLAP\ProjectSEAMEditor.clap"
  RMDir /r "$COMMONFILES64\CLAP\ProjectSEAMEditor.resources"
  Delete "$COMMONFILES64\VST3\ProjectSEAMEditor.vst3"
  Delete "$COMMONAPPDATA\ProjectSEAM\THIRD_PARTY_NOTICES.md"
  Delete "$COMMONAPPDATA\ProjectSEAM\SBOM.spdx.json"
  Delete "$COMMONAPPDATA\ProjectSEAM\Uninstall.exe"
  RMDir "$COMMONAPPDATA\ProjectSEAM"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM"
SectionEnd
