# Third-Party Notices

## Distributed source and binary code

No third-party source code or binary library is copied into the Project SEAM Phase 5 repository build. The implementation uses the C++ standard library and first-party Project SEAM source.

Projects listed under `plannedOrReferenceOnly` in `third_party/manifest.yml` were studied or selected as future candidates but are not included in the build or archive as dependencies.

## Operating-system integrations

The Linux build uses Xlib/XIM headers and links the operating system's X11 client library. The physical audio adapter loads the operating system's PulseAudio Simple shared library at runtime when available. Project SEAM does not redistribute X11 or PulseAudio source or shared libraries in this repository package.

The callback-clock audio fallback is first-party code and does not represent physical speaker output.

## Development tools

CMake, compilers, Python, Ninja, Git, Xvfb, and optional ImageMagick are development or evidence-generation tools and are not incorporated into Project SEAM binaries by this repository.

## First-party concept assets

The images under `assets/character-01/concepts` are internal concept material generated for this project. They are not third-party band artwork, merchandise, logos, voicebanks, or release-ready commercial character assets. Their production replacement and rights review remain required before a public product release.
