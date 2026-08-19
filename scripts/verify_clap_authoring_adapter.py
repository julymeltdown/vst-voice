#!/usr/bin/env python3
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
header_path = root / 'libs/seam-clap-editor/include/seam/clap_editor/editor_runtime.hpp'
source_dir = root / 'libs/seam-clap-editor/src'
header = header_path.read_text(encoding='utf-8')
sources = sorted(source_dir.glob('editor_runtime_*.cpp'))
combined = '\n'.join(path.read_text(encoding='utf-8') for path in sources)
errors = []
if 'authoring::AuthoringRuntime' not in header:
    errors.append('EditorRuntime does not own AuthoringRuntime')
for forbidden in ('application::ProjectFactory factory_',
                  'application::EditorSession session_',
                  'authoring::VoicebankSession voicebankSession_',
                  'AsyncPreviewRenderService renderService_'):
    if forbidden in header:
        errors.append(f'CLAP adapter still owns business state: {forbidden}')
if 'renderService_.submit' in combined:
    errors.append('CLAP adapter still submits through AsyncPreviewRenderService')
if '~EditorRuntime() = default;' in header:
    errors.append('EditorRuntime must shut down shared authoring callbacks before member destruction')
if 'EditorRuntime::~EditorRuntime()' not in combined or 'authoring_->shutdown()' not in combined:
    errors.append('CLAP adapter is missing explicit shared-authoring shutdown')
if not sources:
    errors.append('CLAP editor runtime split sources are missing')
for path in sources:
    lines = path.read_text(encoding='utf-8').count('\n') + 1
    if lines > 600:
        errors.append(f'{path.name} exceeds 600 lines: {lines}')
required = {
    'editor_runtime_adapter.cpp', 'editor_runtime_paint.cpp',
    'editor_runtime_input.cpp', 'editor_runtime_state.cpp'
}
missing = required.difference(path.name for path in sources)
if missing:
    errors.append('missing adapter split sources: ' + ', '.join(sorted(missing)))
if errors:
    print('\n'.join(errors), file=sys.stderr)
    raise SystemExit(1)
print('CLAP_AUTHORING_ADAPTER=PASS')
