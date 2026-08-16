#include "seam/distribution/installer.hpp"
#include "seam/distribution/seambank.hpp"
#include "seam/distribution/signing.hpp"

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
