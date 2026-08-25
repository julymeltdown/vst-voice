# macOS package

The standalone application, `.clap`, package-shaped `.vst3`, and `.component` bundles are installed by the signed product package. Offline Beta manual/support documents are included under `/Library/Application Support/ProjectSEAM/Documentation`. Release signing requires an Apple Developer ID Application certificate; PKG signing requires a Developer ID Installer certificate; notarization requires a configured `notarytool` keychain profile. The scripts fail closed when credentials are absent.
