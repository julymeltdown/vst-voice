#pragma once

#include "seam/core/result.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

namespace seam::distribution {

using Ed25519PrivateKey = std::array<std::byte, 32>;
using Ed25519PublicKey = std::array<std::byte, 32>;
using Ed25519Signature = std::array<std::byte, 64>;

struct SigningKeyPair final {
  Ed25519PrivateKey privateKey{};
  Ed25519PublicKey publicKey{};

  [[nodiscard]] std::string keyId() const;
  friend bool operator==(const SigningKeyPair&, const SigningKeyPair&) = default;
};

[[nodiscard]] core::Result<SigningKeyPair> generateSigningKeyPair();
[[nodiscard]] core::Result<Ed25519Signature> signEd25519(
    std::span<const std::byte> message, const Ed25519PrivateKey& privateKey);
[[nodiscard]] core::Result<void> verifyEd25519(
    std::span<const std::byte> message, const Ed25519Signature& signature,
    const Ed25519PublicKey& publicKey);

[[nodiscard]] std::string publicKeyId(const Ed25519PublicKey& publicKey);
[[nodiscard]] core::Result<void> savePrivateKey(
    const SigningKeyPair& keyPair, const std::filesystem::path& path);
[[nodiscard]] core::Result<void> savePublicKey(
    const Ed25519PublicKey& publicKey, const std::filesystem::path& path);
[[nodiscard]] core::Result<SigningKeyPair> loadPrivateKey(
    const std::filesystem::path& path);
[[nodiscard]] core::Result<Ed25519PublicKey> loadPublicKey(
    const std::filesystem::path& path);

}  // namespace seam::distribution
