Unicode true
RequestExecutionLevel admin
SetCompressor zlib

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

Section "Project SEAM" SEC_MAIN
  SetShellVarContext all

  SetOutPath "$INSTDIR"
  File /oname=ProjectSEAM.exe "${PAYLOAD_ROOT}\Standalone\seam_editor_native.exe"
  File "${PAYLOAD_ROOT}\RELEASE_IDENTITY.json"
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

  SetOutPath "$COMMONAPPDATA\ProjectSEAM"
  File "${PAYLOAD_ROOT}\THIRD_PARTY_NOTICES.md"
  File "${PAYLOAD_ROOT}\SBOM.spdx.json"
  SetOutPath "$COMMONAPPDATA\ProjectSEAM\Documentation"
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
  Delete "$COMMONAPPDATA\ProjectSEAM\THIRD_PARTY_NOTICES.md"
  Delete "$COMMONAPPDATA\ProjectSEAM\SBOM.spdx.json"
  RMDir /r "$COMMONAPPDATA\ProjectSEAM\Documentation"
  RMDir "$COMMONAPPDATA\ProjectSEAM"
  Delete "$INSTDIR\ProjectSEAM.exe"
  Delete "$INSTDIR\RELEASE_IDENTITY.json"
  RMDir /r "$INSTDIR\Resources"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ProjectSEAM"
SectionEnd
