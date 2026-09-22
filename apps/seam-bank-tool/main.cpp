#include "seam/distribution/installer.hpp"
#include "seam/distribution/procedural_package.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"
#include "seam/voice_design/recipe_resource.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void usage() {
  std::cout
      << "Project SEAM signed voicebank package tool\n\n"
      << "Usage:\n"
      << "  seam_bank_tool keygen PRIVATE_KEY.json PUBLIC_KEY.json\n"
      << "  seam_bank_tool pack SOURCE_DIR OUTPUT.seambank PRIVATE_KEY.json\n"
      << "  seam_bank_tool verify PACKAGE.seambank [--public-key KEY.json]\n"
      << "  seam_bank_tool list PACKAGE.seambank [--public-key KEY.json]\n"
      << "  seam_bank_tool install PACKAGE.seambank INSTALL_ROOT --public-key KEY.json [--replace]\n";
  std::cout
      << "  seam_bank_tool publish-singer RECIPE.json STAGING_DIR OUTPUT.seamsinger PRIVATE_KEY.json\n"
      << "      --version VERSION [--language LANG] [--display-name NAME] [--style STYLE]...\n"
      << "      [--install-root DIR --public-key KEY.json [--replace]]\n";
  std::cout
      << "  seam_bank_tool verify-singer PACKAGE.seamsinger [--public-key KEY.json]\n";
}

void printError(const seam::core::Error& error) {
  std::cerr << "error: " << error.message;
  if (!error.context.empty()) std::cerr << " (" << error.context << ')';
  std::cerr << '\n';
}

seam::core::Result<seam::distribution::VerifySeambankOptions> verificationOptions(
    int argc, char** argv, int start, bool requireTrusted) {
  seam::distribution::VerifySeambankOptions options;
  options.requireTrustedSigner = requireTrusted;
  for (int index = start; index < argc; ++index) {
    if (std::string_view{argv[index]} == "--public-key" && index + 1 < argc) {
      auto key = seam::distribution::loadPublicKey(argv[++index]);
      if (!key) return seam::core::Result<seam::distribution::VerifySeambankOptions>{key.error()};
      options.trustedPublicKeys.push_back(key.value());
    } else if (std::string_view{argv[index]} != "--replace") {
      return seam::core::failure<seam::distribution::VerifySeambankOptions>(
          seam::core::ErrorCode::InvalidArgument, "Unknown command option", argv[index]);
    }
  }
  if (requireTrusted && options.trustedPublicKeys.empty()) {
    return seam::core::failure<seam::distribution::VerifySeambankOptions>(
        seam::core::ErrorCode::InvalidArgument,
        "Installation requires at least one --public-key trust anchor");
  }
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || std::string_view{argv[1]} == "--help" ||
      std::string_view{argv[1]} == "help") {
    usage();
    return 0;
  }
  const std::string_view command{argv[1]};
  if (command == "keygen") {
    if (argc != 4) { usage(); return 2; }
    auto pair = seam::distribution::generateSigningKeyPair();
    if (!pair) { printError(pair.error()); return 3; }
    auto privateSaved = seam::distribution::savePrivateKey(pair.value(), argv[2]);
    if (!privateSaved) { printError(privateSaved.error()); return 4; }
    auto publicSaved = seam::distribution::savePublicKey(pair.value().publicKey, argv[3]);
    if (!publicSaved) { printError(publicSaved.error()); return 5; }
    std::cout << "keyId=" << pair.value().keyId() << '\n';
    return 0;
  }
  if (command == "pack") {
    if (argc != 5) { usage(); return 2; }
    auto key = seam::distribution::loadPrivateKey(argv[4]);
    if (!key) { printError(key.error()); return 3; }
    auto package = seam::distribution::packSeambank(argv[2], argv[3], key.value());
    if (!package) { printError(package.error()); return 4; }
    std::cout << "voicebank=" << package.value().manifest.id << '\n'
              << "version=" << package.value().manifest.version << '\n'
              << "entries=" << package.value().entries.size() << '\n'
              << "digest=" << package.value().packageDigest << '\n'
              << "signer=" << package.value().signerKeyId << '\n';
    return 0;
  }
  // A procedural singer is a different package family from a sample bank, so it needs its own
  // verification entry point rather than being read as a bank.
  if (command == "verify-singer") {
    if (argc < 3) { usage(); return 2; }
    auto options = verificationOptions(argc, argv, 3, false);
    if (!options) { printError(options.error()); return 3; }
    auto package = seam::distribution::verifyProceduralPackage(argv[2], options.value());
    if (!package) { printError(package.error()); return 4; }
    std::cout << "singer=" << package.value().manifest.id << '\n'
              << "version=" << package.value().manifest.version << '\n'
              << "language=" << package.value().manifest.language << '\n'
              << "engine=" << package.value().manifest.engineId << '\n'
              << "engineRevision=" << package.value().manifest.engineRevision << '\n'
              << "styles=" << package.value().manifest.styles.size() << '\n'
              << "phones=" << package.value().manifest.phones.size() << '\n'
              << "signatureValid=true\n"
              << "signerTrusted=" << (package.value().container.signerTrusted ? "true" : "false") << '\n'
              << "signer=" << package.value().container.signerKeyId << '\n'
              << "digest=" << package.value().container.packageDigest << '\n';
    return 0;
  }
  if (command == "verify" || command == "list") {
    if (argc < 3) { usage(); return 2; }
    auto options = verificationOptions(argc, argv, 3, false);
    if (!options) { printError(options.error()); return 3; }
    auto package = seam::distribution::verifySeambank(argv[2], options.value());
    if (!package) { printError(package.error()); return 4; }
    std::cout << "voicebank=" << package.value().manifest.id << '\n'
              << "version=" << package.value().manifest.version << '\n'
              << "signatureValid=true\n"
              << "signerTrusted=" << (package.value().signerTrusted ? "true" : "false") << '\n'
              << "signer=" << package.value().signerKeyId << '\n'
              << "digest=" << package.value().packageDigest << '\n';
    if (command == "list") {
      for (const auto& entry : package.value().entries) {
        std::cout << entry.payloadSize << '\t' << entry.path << '\n';
      }
    }
    return 0;
  }
  // A designed voice becomes an installable singer. The recipe is the only creative input: the
  // manifest's identity, styles and phone coverage are derived from it, so this command cannot
  // advertise support the recipe does not carry.
  if (command == "publish-singer") {
    if (argc < 6) { usage(); return 2; }
    auto key = seam::distribution::loadPrivateKey(argv[5]);
    if (!key) { printError(key.error()); return 3; }
    auto resource = seam::voice_design::loadVoiceRecipeResource(argv[2]);
    if (!resource) { printError(resource.error()); return 4; }
    seam::distribution::PublishProceduralSingerOptions options;
    std::optional<std::filesystem::path> installRoot;
    std::vector<std::string> trustedKeys;
    bool replace = false;
    for (int index = 6; index < argc; ++index) {
      const std::string_view flag{argv[index]};
      const auto value = [&]() -> const char* { return index + 1 < argc ? argv[index + 1] : nullptr; };
      if (flag == "--version" && value() != nullptr) options.version = argv[++index];
      else if (flag == "--language" && value() != nullptr) options.language = argv[++index];
      else if (flag == "--display-name" && value() != nullptr) options.displayName = argv[++index];
      else if (flag == "--style" && value() != nullptr) options.styles.emplace_back(argv[++index]);
      else if (flag == "--install-root" && value() != nullptr) installRoot = argv[++index];
      else if (flag == "--public-key" && value() != nullptr) trustedKeys.emplace_back(argv[++index]);
      else if (flag == "--replace") replace = true;
      else { printError({seam::core::ErrorCode::InvalidArgument, "Unknown command option", std::string{flag}}); return 2; }
    }
    auto published = seam::distribution::publishProceduralSingerFromRecipe(
        resource.value(), argv[3], argv[4], key.value(), options);
    if (!published) { printError(published.error()); return 5; }
    std::cout << "singer=" << published.value().manifest.id << '\n'
              << "version=" << published.value().manifest.version << '\n'
              << "language=" << published.value().manifest.language << '\n'
              << "styles=" << published.value().manifest.styles.size() << '\n'
              << "phones=" << published.value().manifest.phones.size() << '\n'
              << "digest=" << published.value().container.packageDigest << '\n'
              << "signer=" << published.value().container.signerKeyId << '\n';
    if (!installRoot.has_value()) return 0;
    // Installing is a separate, explicitly requested step so publishing and admitting a singer stay
    // distinguishable in the caller's log.
    if (trustedKeys.empty()) {
      printError({seam::core::ErrorCode::InvalidArgument,
                  "Installing requires at least one --public-key trust anchor", {}});
      return 6;
    }
    seam::distribution::VerifySeambankOptions verification;
    verification.requireTrustedSigner = true;
    for (const auto& path : trustedKeys) {
      auto publicKey = seam::distribution::loadPublicKey(path);
      if (!publicKey) { printError(publicKey.error()); return 7; }
      verification.trustedPublicKeys.push_back(publicKey.value());
    }
    seam::distribution::InstallProceduralOptions installOptions;
    installOptions.verification = std::move(verification);
    installOptions.replaceExisting = replace;
    auto installed = seam::distribution::installProceduralPackage(
        argv[4], *installRoot, installOptions);
    if (!installed) { printError(installed.error()); return 8; }
    std::cout << "installed=" << installed.value().installDirectory.string() << '\n'
              << "rendered=" << installed.value().renderIdentity.id << '\n'
              << "contentHash=" << installed.value().renderIdentity.contentHash << '\n';
    return 0;
  }
  if (command == "install") {
    if (argc < 5) { usage(); return 2; }
    auto options = verificationOptions(argc, argv, 4, true);
    if (!options) { printError(options.error()); return 3; }
    const auto replace = [&]() {
      for (int index = 4; index < argc; ++index) {
        if (std::string_view{argv[index]} == "--replace") return true;
      }
      return false;
    }();
    auto installed = seam::distribution::installSeambank(
        argv[2], argv[3], seam::distribution::InstallSeambankOptions{
                              .verification = std::move(options.value()),
                              .replaceExisting = replace});
    if (!installed) { printError(installed.error()); return 4; }
    std::cout << "installed=" << installed.value().installDirectory.string() << '\n'
              << "digest=" << installed.value().packageDigest << '\n'
              << "signer=" << installed.value().signerKeyId << '\n';
    return 0;
  }
  usage();
  return 2;
}
