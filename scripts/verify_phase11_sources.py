#!/usr/bin/env python3
"""Verify Phase 11 source, packaging and rights-boundary contracts."""
from __future__ import annotations
import argparse, json, re, sys
from pathlib import Path

REQUIRED = (
    'libs/seam-clap-editor/src/plugin_entry.cpp',
    'libs/seam-clap-editor/src/editor_runtime.cpp',
    'libs/seam-clap-editor/src/embedded_view_x11.cpp',
    'libs/seam-clap-editor/src/embedded_view_win32.cpp',
    'libs/seam-clap-editor/src/embedded_view_appkit.mm',
    'assets/demo-human-voicebank-public-domain/provenance.json',
    'assets/demo-human-voicebank-public-domain/source/talking.wav',
    'assets/demo-human-voicebank-public-domain/audio/human-vowel-demo.wav',
    'packaging/macos/ProjectSEAMEditor-Info.plist',
    'scripts/package_macos_clap.sh',
    'scripts/sign_notarize_macos.sh',
    'scripts/package_macos_installer.sh',
    'scripts/package_windows_plugin.ps1',
    'scripts/sign_windows_plugin.ps1',
    'scripts/run_clap_validator.sh',
    'scripts/build_clap_wrappers.sh',
    'docs/phase11/HOST_CERTIFICATION_MATRIX.md',
    'docs/legal/VOICE_PROVIDER_CONTRACT_REQUIREMENTS.md',
    'docs/voicebank/OFFICIAL_VOICEBANK_ACCEPTANCE.md',
)

def main() -> int:
    ap=argparse.ArgumentParser(); ap.add_argument('--root',type=Path,default=Path.cwd()); args=ap.parse_args()
    root=args.root.resolve(); errors=[]
    for rel in REQUIRED:
        if not (root/rel).is_file(): errors.append(f'missing: {rel}')
    try:
        prov=json.loads((root/'assets/demo-human-voicebank-public-domain/provenance.json').read_text())
        manifest=json.loads((root/'assets/demo-human-voicebank-public-domain/manifest.json').read_text())
        if prov.get('officialVoicebank') is not False or prov.get('contractedSinger') is not False:
            errors.append('public-domain provenance must remain non-official/non-contracted')
        if manifest.get('official') is not False or manifest.get('contractedSinger') is not False:
            errors.append('demo manifest must remain non-official/non-contracted')
        for key in ('sourceSha256','derivedSha256'):
            if not re.fullmatch(r'[0-9a-f]{64}',str(prov.get(key,''))): errors.append(f'invalid {key}')
    except Exception as exc: errors.append(f'provenance parse failed: {exc}')
    plugin=(root/'libs/seam-clap-editor/src/plugin_entry.cpp').read_text(errors='replace')
    for token in ('CLAP_EXT_GUI','CLAP_EXT_NOTE_PORTS','acquireRenderedPreview','CLAP_EVENT_NOTE_ON'):
        if token not in plugin: errors.append(f'plugin contract missing {token}')
    appkit=(root/'libs/seam-clap-editor/src/embedded_view_appkit.mm').read_text(errors='replace')
    win=(root/'libs/seam-clap-editor/src/embedded_view_win32.cpp').read_text(errors='replace')
    if 'NSView' not in appkit or 'NSTextField' not in appkit: errors.append('AppKit embedded editor contract incomplete')
    if 'CreateWindowExW' not in win or 'EDIT' not in win: errors.append('Win32 embedded editor contract incomplete')
    if errors:
        for e in errors: print(f'[phase11-source] ERROR: {e}',file=sys.stderr)
        return 1
    print('[phase11-source] clapGui=present')
    print('[phase11-source] noteInput=present')
    print('[phase11-source] asyncRender=present')
    print('[phase11-source] publicDomainDemo=non-official')
    print('[phase11-source] packagingPipelines=present')
    print('[phase11-source] status=PASS')
    return 0
if __name__=='__main__': raise SystemExit(main())
