#!/usr/bin/env python3
from pathlib import Path
import sys
root = Path(__file__).resolve().parents[1]
paths = [root / "apps" / "seam-editor-native", root / "libs" / "seam-standalone"]
text = "\n".join(
    p.read_text(encoding="utf-8")
    for directory in paths
    for p in directory.rglob("*")
    if p.suffix in {".cpp", ".hpp"}
)
errors=[]
for token in ("makeDemoTimeline", "std::sin", "official.voice.01", 'contentHash = "demo"',
              "SEAM_SOURCE_PRODUCTION_VOICEBANK", "previewCacheRoot"):
    if token in text:
        errors.append(f"standalone retains demo audio or fake bank token: {token}")
required=("AuthoringRuntime", "MultichannelRingBufferAudioProcessor")
for token in required:
    if token not in text:
        errors.append(f"standalone production path missing token: {token}")
for token in ("ProductionRuntimeMode", "makeProductionConfiguration",
              "ApplicationPaths", "bindFirstAvailableVoicebank = false",
              "allowDevelopmentVoicebanks = config.allowDevelopmentVoicebanks"):
    if token not in text:
        errors.append(f"standalone production path missing release-policy token: {token}")
if errors:
    print("\n".join(errors), file=sys.stderr)
    raise SystemExit(1)
print("STANDALONE_PRODUCTION_PATH=PASS")
