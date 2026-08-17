#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, json, sys
from pathlib import Path

def digest(path: Path) -> str:
    h=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024), b''): h.update(block)
    return h.hexdigest()

def require_text(path: Path, needles: list[str], errors: list[str]) -> None:
    try: text=path.read_text(encoding='utf-8')
    except OSError as ex:
        errors.append(f'missing source: {path}: {ex}'); return
    for needle in needles:
        if needle not in text: errors.append(f'{path}: missing contract token {needle!r}')

def main() -> int:
    ap=argparse.ArgumentParser(); ap.add_argument('--root',type=Path,required=True); args=ap.parse_args()
    root=args.root.resolve(); errors=[]
    require_text(root/'libs/seam-clap-editor/src/embedded_view_win32.cpp',
                 ['CreateWindowExW','WS_CHILD','SetWindowTextW','GetWindowTextW'],errors)
    require_text(root/'libs/seam-clap-editor/src/embedded_view_appkit.mm',
                 ['NSView','addSubview','NSTextField','controlTextDidChange'],errors)
    require_text(root/'libs/seam-clap-editor/src/embedded_view_x11.cpp',
                 ['XCreateSimpleWindow','XMapWindow','XNextEvent'],errors)
    require_text(root/'libs/seam-clap-editor/src/plugin_entry.cpp',
                 ['CLAP_EXT_GUI','CLAP_EXT_NOTE_PORTS','acquireRenderedPreview'],errors)
    require_text(root/'scripts/sign_notarize_macos.sh',
                 ['codesign','notarytool','stapler'],errors)
    require_text(root/'scripts/build_clap_wrappers.sh',
                 ['CLAP_WRAPPER_ROOT','VST3_SDK_ROOT','CLAP_WRAPPER_BUILD_AUV2'],errors)
    manifest_path=root/'assets/demo-human-voicebank-public-domain/manifest.json'
    provenance_path=root/'assets/demo-human-voicebank-public-domain/provenance.json'
    try:
        manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
        provenance=json.loads(provenance_path.read_text(encoding='utf-8'))
    except Exception as ex:
        errors.append(f'invalid human demo metadata: {ex}'); manifest={}; provenance={}
    if manifest.get('official') is not False: errors.append('human demo must retain official=false')
    if manifest.get('contractedSinger') is not False: errors.append('human demo must retain contractedSinger=false')
    source=root/'assets/demo-human-voicebank-public-domain/source/talking.wav'
    derived=root/'assets/demo-human-voicebank-public-domain/audio/human-vowel-demo.wav'
    if source.is_file() and digest(source)!=provenance.get('sourceSha256'): errors.append('source WAV hash mismatch')
    if derived.is_file() and digest(derived)!=provenance.get('derivedSha256'): errors.append('derived WAV hash mismatch')
    if not source.is_file() or not derived.is_file(): errors.append('human demo WAV missing')
    if errors:
        for e in errors: print('[phase11-contract] ERROR:',e,file=sys.stderr)
        return 1
    print('[phase11-contract] platformSources=PASS')
    print('[phase11-contract] humanDemoProvenance=PASS')
    print('[phase11-contract] officialVoicebankClaim=false')
    return 0
if __name__=='__main__': raise SystemExit(main())
