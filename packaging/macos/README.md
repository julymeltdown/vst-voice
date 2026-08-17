# macOS package

The `.clap` bundle is installed under `/Library/Audio/Plug-Ins/CLAP`. Release signing requires an Apple Developer ID Application certificate; PKG signing requires a Developer ID Installer certificate; notarization requires a configured `notarytool` keychain profile. The scripts fail closed when credentials are absent.
