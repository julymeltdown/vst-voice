# Third-party source directory

Third-party production source may be placed here only after its exact immutable
revision, source hash, build/distribution mode, and license file are registered
in `manifest.yml` and accepted by `tools/license-auditor/audit.py`.

Current distributed source:

- `clap/include/clap/clap.h` — pinned, consolidated CLAP 1.2.10 MIT ABI subset used by the Phase 10 render-player plug-in.
- `stb/stb_truetype.h` — pinned MIT-selected TrueType/TTC rasterizer used only
  behind Project SEAM's bounded trusted-system-font API.

Font files are not third-party source dependencies and must never be committed
or distributed from this directory.
