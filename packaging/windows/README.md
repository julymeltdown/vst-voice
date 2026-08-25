# Windows package

The Windows payload is an x64 standalone application plus CLAP and package-shaped VST3 artifacts. The payload root contains `Standalone\seam_editor_native.exe`, `Standalone\Resources`, `RELEASE_IDENTITY.json`, the plug-in files, `Documentation\` with the offline Beta manual/support documents, notices, and the SBOM. The NSIS build requires an explicit product version, build ID, and 40-character source commit; those values are written into the installer resources and uninstall registry record.

The installer writes the application under `Program Files\ProjectSEAM` and plug-ins under the system CLAP/VST3 roots. Runtime projects, settings, caches, banks, recovery, and logs remain under the per-user `LOCALAPPDATA\ProjectSEAM` roots resolved by the application. Uninstall removes only installed application and plug-in bytes.

Authenticode signing is performed separately and fails closed when the Windows signing certificate or timestamp service is unavailable. A signed binary is never simulated with a self-signed release claim.
