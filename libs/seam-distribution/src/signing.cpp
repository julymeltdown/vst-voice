#include "seam/distribution/signing.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <memory>
#include <string_view>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace seam::distribution {
namespace {

struct PkeyDeleter final {
  void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};
struct PkeyContextDeleter final {
  void operator()(EVP_PKEY_CTX* context) const noexcept {
    EVP_PKEY_CTX_free(context);
  }
};
struct MdContextDeleter final {
  void operator()(EVP_MD_CTX* context) const noexcept { EVP_MD_CTX_free(context); }
};

using UniquePkey = std::unique_ptr<EVP_PKEY, PkeyDeleter>;
using UniquePkeyContext = std::unique_ptr<EVP_PKEY_CTX, PkeyContextDeleter>;
using UniqueMdContext = std::unique_ptr<EVP_MD_CTX, MdContextDeleter>;

char nibble(unsigned value) noexcept {
  return value < 10U ? static_cast<char>('0' + value)
                     : static_cast<char>('a' + value - 10U);
}

std::string toHex(std::span<const std::byte> bytes) {
  std::string result(bytes.size() * 2U, '0');
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    const auto value = std::to_integer<unsigned>(bytes[index]);
    result[index * 2U] = nibble((value >> 4U) & 0xFU);
    result[index * 2U + 1U] = nibble(value & 0xFU);
  }
  return result;
}

int hexValue(char value) noexcept {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}

template <std::size_t Size>
core::Result<std::array<std::byte, Size>> parseHex(std::string_view text,
                                                   std::string_view field) {
  if (text.size() != Size * 2U) {
    return core::failure<std::array<std::byte, Size>>(
        core::ErrorCode::ParseError, "Signing key has an invalid hex length",
        std::string{field});
  }
  std::array<std::byte, Size> result{};
  for (std::size_t index = 0U; index < Size; ++index) {
    const auto high = hexValue(text[index * 2U]);
    const auto low = hexValue(text[index * 2U + 1U]);
    if (high < 0 || low < 0) {
      return core::failure<std::array<std::byte, Size>>(
          core::ErrorCode::ParseError, "Signing key contains invalid hex",
          std::string{field});
    }
    result[index] = static_cast<std::byte>((high << 4) | low);
  }
  return result;
}

core::Result<formats::JsonValue> loadKeyJson(const std::filesystem::path& path) {
  auto text = core::readTextFileLimited(path, 64U * 1024U);
  if (!text) return core::Result<formats::JsonValue>{text.error()};
  auto parsed = formats::parseJson(text.value(), formats::JsonParseLimits{
      .maximumInputBytes = 64U * 1024U,
      .maximumDepth = 8U,
      .maximumNodes = 64U,
      .maximumStringBytes = 4096U,
      .maximumCollectionEntries = 32U,
  });
  if (!parsed) return parsed;
  if (!parsed.value().isObject()) {
    return core::failure<formats::JsonValue>(core::ErrorCode::ParseError,
                                             "Signing key root must be an object");
  }
  return parsed;
}

core::Result<std::string> requiredString(const formats::JsonValue& root,
                                         std::string_view field) {
  const auto* value = root.find(field);
  if (value == nullptr || !value->isString() || value->asString().empty()) {
    return core::failure<std::string>(core::ErrorCode::ParseError,
                                      "Signing key field is missing",
                                      std::string{field});
  }
  return value->asString();
}

core::Result<void> saveKeyJson(const formats::JsonValue& value,
                               const std::filesystem::path& path,
                               bool privateKey) {
  const auto text = formats::stringifyJson(value, true) + "\n";
  auto result = core::durableAtomicWriteText(path, text);
  if (!result) return result;
#ifndef _WIN32
  if (privateKey && ::chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to restrict private key permissions",
                         path.string());
  }
#else
  static_cast<void>(privateKey);
#endif
  return core::success();
}

UniquePkey privatePkey(const Ed25519PrivateKey& privateKey) {
  return UniquePkey{EVP_PKEY_new_raw_private_key(
      EVP_PKEY_ED25519, nullptr,
      reinterpret_cast<const unsigned char*>(privateKey.data()),
      privateKey.size())};
}

UniquePkey publicPkey(const Ed25519PublicKey& publicKey) {
  return UniquePkey{EVP_PKEY_new_raw_public_key(
      EVP_PKEY_ED25519, nullptr,
      reinterpret_cast<const unsigned char*>(publicKey.data()),
      publicKey.size())};
}

}  // namespace

std::string SigningKeyPair::keyId() const { return publicKeyId(publicKey); }

core::Result<SigningKeyPair> generateSigningKeyPair() {
  UniquePkeyContext context{EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr)};
  if (!context || EVP_PKEY_keygen_init(context.get()) <= 0) {
    return core::failure<SigningKeyPair>(core::ErrorCode::Internal,
                                         "Unable to initialize Ed25519 key generation");
  }
  EVP_PKEY* generated = nullptr;
  if (EVP_PKEY_keygen(context.get(), &generated) <= 0 || generated == nullptr) {
    return core::failure<SigningKeyPair>(core::ErrorCode::Internal,
                                         "Unable to generate Ed25519 key pair");
  }
  UniquePkey key{generated};
  SigningKeyPair result;
  std::size_t privateSize = result.privateKey.size();
  std::size_t publicSize = result.publicKey.size();
  if (EVP_PKEY_get_raw_private_key(
          key.get(), reinterpret_cast<unsigned char*>(result.privateKey.data()),
          &privateSize) <= 0 ||
      EVP_PKEY_get_raw_public_key(
          key.get(), reinterpret_cast<unsigned char*>(result.publicKey.data()),
          &publicSize) <= 0 ||
      privateSize != result.privateKey.size() ||
      publicSize != result.publicKey.size()) {
    return core::failure<SigningKeyPair>(core::ErrorCode::Internal,
                                         "Unable to export Ed25519 key pair");
  }
  return result;
}

core::Result<Ed25519Signature> signEd25519(
    std::span<const std::byte> message, const Ed25519PrivateKey& privateKey) {
  auto key = privatePkey(privateKey);
  UniqueMdContext context{EVP_MD_CTX_new()};
  if (!key || !context ||
      EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) <= 0) {
    return core::failure<Ed25519Signature>(core::ErrorCode::Internal,
                                           "Unable to initialize Ed25519 signing");
  }
  Ed25519Signature signature{};
  std::size_t signatureSize = signature.size();
  if (EVP_DigestSign(
          context.get(), reinterpret_cast<unsigned char*>(signature.data()),
          &signatureSize,
          reinterpret_cast<const unsigned char*>(message.data()),
          message.size()) <= 0 ||
      signatureSize != signature.size()) {
    return core::failure<Ed25519Signature>(core::ErrorCode::Internal,
                                           "Unable to create Ed25519 signature");
  }
  return signature;
}

core::Result<void> verifyEd25519(
    std::span<const std::byte> message, const Ed25519Signature& signature,
    const Ed25519PublicKey& publicKey) {
  auto key = publicPkey(publicKey);
  UniqueMdContext context{EVP_MD_CTX_new()};
  if (!key || !context ||
      EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) <= 0) {
    return core::failure(core::ErrorCode::Internal,
                         "Unable to initialize Ed25519 verification");
  }
  const auto result = EVP_DigestVerify(
      context.get(), reinterpret_cast<const unsigned char*>(signature.data()),
      signature.size(), reinterpret_cast<const unsigned char*>(message.data()),
      message.size());
  if (result != 1) {
    return core::failure(core::ErrorCode::Conflict,
                         "Ed25519 signature verification failed");
  }
  return core::success();
}

std::string publicKeyId(const Ed25519PublicKey& publicKey) {
  return core::sha256Hex(std::span<const std::byte>{publicKey});
}

core::Result<void> savePrivateKey(const SigningKeyPair& keyPair,
                                  const std::filesystem::path& path) {
  formats::JsonValue::Object root;
  root.emplace("type", "ed25519-private");
  root.emplace("schemaVersion", static_cast<std::int64_t>(1));
  root.emplace("keyId", keyPair.keyId());
  root.emplace("privateKey", toHex(keyPair.privateKey));
  root.emplace("publicKey", toHex(keyPair.publicKey));
  return saveKeyJson(formats::JsonValue{std::move(root)}, path, true);
}

core::Result<void> savePublicKey(const Ed25519PublicKey& publicKey,
                                 const std::filesystem::path& path) {
  formats::JsonValue::Object root;
  root.emplace("type", "ed25519-public");
  root.emplace("schemaVersion", static_cast<std::int64_t>(1));
  root.emplace("keyId", publicKeyId(publicKey));
  root.emplace("publicKey", toHex(publicKey));
  return saveKeyJson(formats::JsonValue{std::move(root)}, path, false);
}

core::Result<SigningKeyPair> loadPrivateKey(
    const std::filesystem::path& path) {
  auto root = loadKeyJson(path);
  if (!root) return core::Result<SigningKeyPair>{root.error()};
  auto type = requiredString(root.value(), "type");
  auto privateHex = requiredString(root.value(), "privateKey");
  auto publicHex = requiredString(root.value(), "publicKey");
  auto keyId = requiredString(root.value(), "keyId");
  if (!type) return core::Result<SigningKeyPair>{type.error()};
  if (!privateHex) return core::Result<SigningKeyPair>{privateHex.error()};
  if (!publicHex) return core::Result<SigningKeyPair>{publicHex.error()};
  if (!keyId) return core::Result<SigningKeyPair>{keyId.error()};
  if (type.value() != "ed25519-private") {
    return core::failure<SigningKeyPair>(core::ErrorCode::ParseError,
                                         "Signing key is not a private key");
  }
  auto privateKey = parseHex<32U>(privateHex.value(), "privateKey");
  auto publicKey = parseHex<32U>(publicHex.value(), "publicKey");
  if (!privateKey) return core::Result<SigningKeyPair>{privateKey.error()};
  if (!publicKey) return core::Result<SigningKeyPair>{publicKey.error()};
  SigningKeyPair pair{.privateKey = privateKey.value(),
                      .publicKey = publicKey.value()};
  if (pair.keyId() != keyId.value()) {
    return core::failure<SigningKeyPair>(core::ErrorCode::Conflict,
                                         "Private key ID does not match its public key");
  }
  const auto probe = std::as_bytes(std::span{"seam-key-probe", 14U});
  auto signature = signEd25519(probe, pair.privateKey);
  if (!signature) return core::Result<SigningKeyPair>{signature.error()};
  const auto verified = verifyEd25519(probe, signature.value(), pair.publicKey);
  if (!verified) {
    return core::failure<SigningKeyPair>(core::ErrorCode::Conflict,
                                         "Private and public signing keys do not match");
  }
  return pair;
}

core::Result<Ed25519PublicKey> loadPublicKey(
    const std::filesystem::path& path) {
  auto root = loadKeyJson(path);
  if (!root) return core::Result<Ed25519PublicKey>{root.error()};
  auto type = requiredString(root.value(), "type");
  auto publicHex = requiredString(root.value(), "publicKey");
  auto keyId = requiredString(root.value(), "keyId");
  if (!type) return core::Result<Ed25519PublicKey>{type.error()};
  if (!publicHex) return core::Result<Ed25519PublicKey>{publicHex.error()};
  if (!keyId) return core::Result<Ed25519PublicKey>{keyId.error()};
  if (type.value() != "ed25519-public" && type.value() != "ed25519-private") {
    return core::failure<Ed25519PublicKey>(core::ErrorCode::ParseError,
                                           "Signing key is not a supported public-key file");
  }
  auto publicKey = parseHex<32U>(publicHex.value(), "publicKey");
  if (!publicKey) return publicKey;
  if (publicKeyId(publicKey.value()) != keyId.value()) {
    return core::failure<Ed25519PublicKey>(core::ErrorCode::Conflict,
                                           "Public key ID does not match its bytes");
  }
  return publicKey;
}

}  // namespace seam::distribution
