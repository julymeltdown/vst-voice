Unicode true
RequestExecutionLevel admin
SetCompressor zlib

!include "FileFunc.nsh"
!include "LogicLib.nsh"

!ifndef PAYLOAD_ROOT
  !error "PAYLOAD_ROOT is required"
!endif
!ifndef OUTPUT_EXE
  !error "OUTPUT_EXE is required"
!endif
!ifndef PRODUCT_VERSION
  !error "PRODUCT_VERSION is required"
!endif
!ifndef BUILD_ID
  !error "BUILD_ID is required"
!endif
!ifndef SOURCE_COMMIT
  !error "SOURCE_COMMIT is required"
!endif

!ifdef UNINSTALLER_SIGN_COMMAND
  !uninstfinalize '${UNINSTALLER_SIGN_COMMAND} "%1"'
!endif

Name "Project SEAM"
OutFile "${OUTPUT_EXE}"
InstallDir "$PROGRAMFILES64\ProjectSEAM"
BrandingText "Project SEAM ${PRODUCT_VERSION}"
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${PRODUCT_VERSION}.0"
VIAddVersionKey "ProductName" "Project SEAM"
VIAddVersionKey "CompanyName" "Project SEAM"
VIAddVersionKey "FileDescription" "Project SEAM standalone editor and plug-ins"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "ProductVersion" "${PRODUCT_VERSION}"
VIAddVersionKey "BuildId" "${BUILD_ID}"
VIAddVersionKey "SourceCommit" "${SOURCE_COMMIT}"
VIAddVersionKey "LegalCopyright" "Project SEAM"

Function .onInit
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=seam_installer_verifier.exe "${PAYLOAD_ROOT}\Tools\seam_installer_verifier.exe"
  ${GetParameters} $0
  ${GetOptions} $0 "/HANDOFF=" $1
  ${GetOptions} $0 "/MANIFEST=" $2
  ${GetOptions} $0 "/POLICY=" $3
  ${GetOptions} $0 "/HANDOFFSHA256=" $4
  ${GetOptions} $0 "/STAGINGROOT=" $5
  ${GetOptions} $0 "/CANDIDATE=" $7
  ${If} $1 == ""
  ${OrIf} $2 == ""
  ${OrIf} $3 == ""
  ${OrIf} $4 == ""
  ${OrIf} $5 == ""
  ${OrIf} $7 == ""
    MessageBox MB_OK|MB_ICONSTOP "Project SEAM installer trust inputs are incomplete." /SD IDOK
    Abort
  ${EndIf}
  nsExec::ExecToStack '"$PLUGINSDIR\seam_installer_verifier.exe" --handoff "$1" --manifest "$2" --policy "$3" --staging-root "$5" --expected-candidate "$7" --expected-handoff-sha256 "$4"'
  Pop $R0
  Pop $R1
  ${If} $R0 != 0
    MessageBox MB_OK|MB_ICONSTOP "Project SEAM installer handoff verification failed: $R1" /SD IDOK
    Abort
  ${EndIf}
FunctionEnd

Section "Project SEAM" SEC_MAIN
  SetShellVarContext all

  SetOutPath "$INSTDIR"
  File /oname=ProjectSEAM.exe "${PAYLOAD_ROOT}\Standalone\seam_editor_native.exe"
  File "${PAYLOAD_ROOT}\RELEASE_IDENTITY.json"
  File "${PAYLOAD_ROOT}\release-payload-manifest.json"
  File "${PAYLOAD_ROOT}\release-dependency-closure.json"
  File "${PAYLOAD_ROOT}\Tools\seam_installer_verifier.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  SetOutPath "$INSTDIR\Resources"
  File /r "${PAYLOAD_ROOT}\Standalone\Resources\*.*"

  SetOutPath "$COMMONFILES64\CLAP"
  File "${PAYLOAD_ROOT}\CLAP\ProjectSEAMEditor.clap"

  RMDir /r "$COMMONFILES64\CLAP\ProjectSEAMEditor.resources"
  SetOutPath "$COMMONFILES64\CLAP\ProjectSEAMEditor.resources"
  File /r "${PAYLOAD_ROOT}\CLAP\ProjectSEAMEditor.resources\*.*"

  SetOutPath "$COMMONFILES64\VST3"
  SetOutPath "$COMMONFILES64\VST3\ProjectSEAMEditor.vst3"
  File /r "${PAYLOAD_ROOT}\VST3\ProjectSEAMEditor.vst3\*.*"

  SetOutPath "$APPDATA\ProjectSEAM"
  File "${PAYLOAD_ROOT}\THIRD_PARTY_NOTICES.md"
  File "${PAYLOAD_ROOT}\SBOM.spdx.json"
  SetOutPath "$APPDATA\ProjectSEAM\Trust"
  File /r "${PAYLOAD_ROOT}\Trust\*.*"
  SetOutPath "$APPDATA\ProjectSEAM\Ownership"
  File /r "${PAYLOAD_ROOT}\Ownership\*.*"
  SetOutPath "$APPDATA\ProjectSEAM\Notices"
  File /r "${PAYLOAD_ROOT}\Notices\*.*"
  SetOutPath "$APPDATA\ProjectSEAM\Documentation"
  File /r "${PAYLOAD_ROOT}\Documentation\*.*"

  CreateDirectory "$SMPROGRAMS\Project SEAM"
  CreateShortCut "$SMPROGRAMS\Project SEAM\Project SEAM.lnk" "$INSTDIR\ProjectSEAM.exe"
  CreateShortCut "$DESKTOP\Project SEAM.lnk" "$INSTDIR\ProjectSEAM.exe"

  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "DisplayName" "Project SEAM"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "Publisher" "Project SEAM"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "BuildId" "${BUILD_ID}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "SourceCommit" "${SOURCE_COMMIT}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM" "NoRepair" 1
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  Delete "$DESKTOP\Project SEAM.lnk"
  Delete "$SMPROGRAMS\Project SEAM\Project SEAM.lnk"
  RMDir "$SMPROGRAMS\Project SEAM"
  Delete "$COMMONFILES64\CLAP\ProjectSEAMEditor.clap"
  RMDir /r "$COMMONFILES64\CLAP\ProjectSEAMEditor.resources"
  RMDir /r "$COMMONFILES64\VST3\ProjectSEAMEditor.vst3"
  Delete "$APPDATA\ProjectSEAM\THIRD_PARTY_NOTICES.md"
  Delete "$APPDATA\ProjectSEAM\SBOM.spdx.json"
  RMDir /r "$APPDATA\ProjectSEAM\Documentation"
  RMDir /r "$APPDATA\ProjectSEAM\Trust"
  RMDir /r "$APPDATA\ProjectSEAM\Ownership"
  RMDir /r "$APPDATA\ProjectSEAM\Notices"
  RMDir "$APPDATA\ProjectSEAM"
  Delete "$INSTDIR\ProjectSEAM.exe"
  Delete "$INSTDIR\RELEASE_IDENTITY.json"
  Delete "$INSTDIR\release-payload-manifest.json"
  Delete "$INSTDIR\release-dependency-closure.json"
  Delete "$INSTDIR\seam_installer_verifier.exe"
  RMDir /r "$INSTDIR\Resources"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM"
SectionEnd
