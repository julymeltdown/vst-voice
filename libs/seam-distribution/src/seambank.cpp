#include "seam/distribution/seambank.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/voicebank/manifest_json.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seam::distribution {
namespace {

constexpr std::array<std::byte, 8U> kMagic{
    std::byte{'S'}, std::byte{'E'}, std::byte{'A'}, std::byte{'M'},
    std::byte{'B'}, std::byte{'N'}, std::byte{'K'}, std::byte{'1'}};
constexpr std::uint64_t kHeaderBytes = 40U;
constexpr std::uint32_t kPublicKeyBytes = 32U;
constexpr std::uint32_t kSignatureBytes = 64U;
constexpr std::size_t kIoBlockBytes = 64U * 1024U;
constexpr std::string_view kSignatureDomain = "SEAM-SEAMBANK-SIGNATURE-V1";

std::atomic<std::uint64_t> gTemporaryCounter{0U};

struct CollectedFile final {
  std::string path;
  std::filesystem::path source;
  std::uint64_t size{0U};
  std::array<std::byte, 32U> digest{};
  std::uint64_t payloadOffset{0U};
};

void appendU16(std::vector<std::byte>& bytes, std::uint16_t value) {
  for (unsigned shift = 0U; shift < 16U; shift += 8U) {
    const auto widened = static_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<std::byte>((widened >> shift) & 0xffU));
  }
}
void appendU32(std::vector<std::byte>& bytes, std::uint32_t value) {
  for (unsigned shift = 0U; shift < 32U; shift += 8U) {
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
  }
}
void appendU64(std::vector<std::byte>& bytes, std::uint64_t value) {
  for (unsigned shift = 0U; shift < 64U; shift += 8U) {
    bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
  }
}

core::Result<std::uint16_t> readU16(std::istream& stream) {
  std::array<unsigned char, 2U> bytes{};
  stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  if (!stream) return core::failure<std::uint16_t>(core::ErrorCode::ParseError, "Truncated seambank integer");
  return static_cast<std::uint16_t>(bytes[0]) |
         static_cast<std::uint16_t>(bytes[1] << 8U);
}
core::Result<std::uint32_t> readU32(std::istream& stream) {
  std::array<unsigned char, 4U> bytes{};
  stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  if (!stream) return core::failure<std::uint32_t>(core::ErrorCode::ParseError, "Truncated seambank integer");
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    value |= static_cast<std::uint32_t>(bytes[index]) << (index * 8U);
  }
  return value;
}
core::Result<std::uint64_t> readU64(std::istream& stream) {
  std::array<unsigned char, 8U> bytes{};
  stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  if (!stream) return core::failure<std::uint64_t>(core::ErrorCode::ParseError, "Truncated seambank integer");
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
  }
  return value;
}

core::Result<std::array<std::byte, 32U>> hashFile(
    const std::filesystem::path& path, std::uint64_t expectedSize) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return core::failure<std::array<std::byte, 32U>>(
        core::ErrorCode::IoError, "Unable to open seambank source asset", path.string());
  }
  core::Sha256 hash;
  std::array<char, kIoBlockBytes> buffer{};
  std::uint64_t consumed = 0U;
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = stream.gcount();
    if (count > 0) {
      consumed += static_cast<std::uint64_t>(count);
      hash.update(std::as_bytes(std::span{buffer.data(), static_cast<std::size_t>(count)}));
    }
  }
  if (!stream.eof() || consumed != expectedSize) {
    return core::failure<std::array<std::byte, 32U>>(
        core::ErrorCode::IoError, "Seambank source asset changed while hashing", path.string());
  }
  return hash.digest();
}

std::filesystem::path temporaryPath(const std::filesystem::path& target) {
  auto result = target;
  const auto counter = gTemporaryCounter.fetch_add(1U, std::memory_order_relaxed);
#ifdef _WIN32
  const auto process = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  const auto process = static_cast<std::uint64_t>(::getpid());
#endif
  result += ".tmp-" + std::to_string(process) + "-" + std::to_string(counter);
  return result;
}

core::Result<void> durableCommit(const std::filesystem::path& temporary,
                                 const std::filesystem::path& target) {
#ifdef _WIN32
  const auto flags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
  if (!MoveFileExW(temporary.c_str(), target.c_str(), flags)) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to atomically publish seambank package",
                         std::to_string(GetLastError()));
  }
#else
  const int descriptor = ::open(temporary.c_str(), O_RDONLY | O_CLOEXEC);
  if (descriptor < 0 || ::fsync(descriptor) != 0) {
    if (descriptor >= 0) ::close(descriptor);
    return core::failure(core::ErrorCode::IoError,
                         "Unable to durably flush seambank package", temporary.string());
  }
  ::close(descriptor);
  if (::rename(temporary.c_str(), target.c_str()) != 0) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to atomically publish seambank package", target.string());
  }
  const auto parent = target.parent_path().empty() ? std::filesystem::path{"."}
                                                   : target.parent_path();
  const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (directory >= 0) {
    static_cast<void>(::fsync(directory));
    ::close(directory);
  }
#endif
  return core::success();
}

std::vector<std::byte> signatureMessage(
    const std::array<std::byte, 32U>& digest) {
  std::vector<std::byte> message;
  const auto domain = std::as_bytes(
      std::span{kSignatureDomain.data(), kSignatureDomain.size()});
  message.reserve(domain.size() + digest.size());
  message.insert(message.end(), domain.begin(), domain.end());
  message.insert(message.end(), digest.begin(), digest.end());
  return message;
}

core::Result<void> writeAndHash(std::ofstream& stream, core::Sha256& hash,
                                std::span<const std::byte> bytes) {
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!stream) return core::failure(core::ErrorCode::IoError, "Unable to write seambank package");
  hash.update(bytes);
  return core::success();
}

core::Result<std::vector<CollectedFile>> collectFiles(
    const std::filesystem::path& sourceDirectory, const SeambankLimits& limits) {
  std::error_code error;
  const auto root = std::filesystem::canonical(sourceDirectory, error);
  if (error || !std::filesystem::is_directory(root)) {
    return core::failure<std::vector<CollectedFile>>(
        core::ErrorCode::NotFound, "Seambank source directory does not exist",
        sourceDirectory.string());
  }
  std::vector<CollectedFile> files;
  std::uint64_t total = 0U;
  for (std::filesystem::recursive_directory_iterator iterator(root), end;
       iterator != end; ++iterator) {
    const auto status = iterator->symlink_status(error);
    if (error) {
      return core::failure<std::vector<CollectedFile>>(
          core::ErrorCode::IoError, "Unable to inspect seambank source asset",
          iterator->path().string());
    }
    if (std::filesystem::is_symlink(status)) {
      return core::failure<std::vector<CollectedFile>>(
          core::ErrorCode::Unsupported, "Seambank source must not contain symbolic links",
          iterator->path().string());
    }
    if (std::filesystem::is_directory(status)) continue;
    if (!std::filesystem::is_regular_file(status)) {
      return core::failure<std::vector<CollectedFile>>(
          core::ErrorCode::Unsupported, "Seambank source contains a non-regular asset",
          iterator->path().string());
    }
    const auto canonical = std::filesystem::canonical(iterator->path(), error);
    if (error) {
      return core::failure<std::vector<CollectedFile>>(
          core::ErrorCode::Unsupported, "Unable to canonicalize seambank source asset",
          iterator->path().string());
    }
    auto relative = std::filesystem::relative(canonical, root, error).generic_string();
    // Editors keep durable backup generations beside manifests. They are not
    // package inputs and must never be signed as distributable assets.
    if (!error && std::filesystem::path{relative}.extension() == ".bak") {
      continue;
    }
    if (error || relative.size() > limits.maximumPathBytes ||
        !isSafeSeambankPath(relative) || !isAllowedSeambankAsset(relative)) {
      return core::failure<std::vector<CollectedFile>>(
          core::ErrorCode::Unsupported, "Seambank source asset path is not allowed", relative);
    }
    const auto size = std::filesystem::file_size(canonical, error);
    if (error || size > limits.maximumEntryBytes ||
        total > limits.maximumPayloadBytes - size) {
      return core::failure<std::vector<CollectedFile>>(
          core::ErrorCode::Unsupported, "Seambank source exceeds package limits", relative);
    }
    auto digest = hashFile(canonical, size);
    if (!digest) return core::Result<std::vector<CollectedFile>>{digest.error()};
    total += size;
    files.push_back(CollectedFile{.path = std::move(relative),
                                  .source = canonical,
                                  .size = size,
                                  .digest = digest.value()});
    if (files.size() > limits.maximumEntries) {
      return core::failure<std::vector<CollectedFile>>(
          core::ErrorCode::Unsupported, "Seambank contains too many assets");
    }
  }
  std::sort(files.begin(), files.end(),
            [](const auto& left, const auto& right) { return left.path < right.path; });
  if (files.empty() ||
      std::none_of(files.begin(), files.end(), [](const auto& file) {
        return file.path == "manifest.json";
      })) {
    return core::failure<std::vector<CollectedFile>>(
        core::ErrorCode::NotFound, "Seambank source requires manifest.json");
  }
  return files;
}

core::Result<void> hashRange(std::ifstream& stream, std::uint64_t bytes,
                             core::Sha256& hash) {
  stream.clear();
  stream.seekg(0, std::ios::beg);
  if (!stream) return core::failure(core::ErrorCode::IoError, "Unable to seek seambank package");
  std::array<char, kIoBlockBytes> buffer{};
  std::uint64_t remaining = bytes;
  while (remaining > 0U) {
    const auto request = static_cast<std::size_t>(
        std::min<std::uint64_t>(remaining, buffer.size()));
    stream.read(buffer.data(), static_cast<std::streamsize>(request));
    if (stream.gcount() != static_cast<std::streamsize>(request)) {
      return core::failure(core::ErrorCode::ParseError, "Truncated signed seambank payload");
    }
    hash.update(std::as_bytes(std::span{buffer.data(), request}));
    remaining -= request;
  }
  return core::success();
}

core::Result<std::array<std::byte, 32U>> hashEntry(
    std::ifstream& stream, const SeambankEntry& entry) {
  stream.clear();
  stream.seekg(static_cast<std::streamoff>(entry.payloadOffset), std::ios::beg);
  if (!stream) return core::failure<std::array<std::byte, 32U>>(core::ErrorCode::IoError, "Unable to seek seambank entry", entry.path);
  core::Sha256 hash;
  std::array<char, kIoBlockBytes> buffer{};
  std::uint64_t remaining = entry.payloadSize;
  while (remaining > 0U) {
    const auto request = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
    stream.read(buffer.data(), static_cast<std::streamsize>(request));
    if (stream.gcount() != static_cast<std::streamsize>(request)) {
      return core::failure<std::array<std::byte, 32U>>(core::ErrorCode::ParseError, "Truncated seambank entry", entry.path);
    }
    hash.update(std::as_bytes(std::span{buffer.data(), request}));
    remaining -= request;
  }
  return hash.digest();
}

core::Result<std::vector<std::byte>> readEntryUnchecked(
    std::ifstream& stream, const SeambankEntry& entry,
    std::uint64_t maximumBytes) {
  if (entry.payloadSize > maximumBytes ||
      entry.payloadSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
    return core::failure<std::vector<std::byte>>(core::ErrorCode::Unsupported,
                                                 "Seambank entry exceeds read limit", entry.path);
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(entry.payloadSize));
  stream.clear();
  stream.seekg(static_cast<std::streamoff>(entry.payloadOffset), std::ios::beg);
  stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!stream || static_cast<std::size_t>(stream.gcount()) != bytes.size()) {
    return core::failure<std::vector<std::byte>>(core::ErrorCode::ParseError,
                                                 "Unable to read seambank entry", entry.path);
  }
  return bytes;
}

const SeambankEntry* findEntry(
    const std::vector<SeambankEntry>& entries, std::string_view path) noexcept {
  const auto iterator = std::lower_bound(
      entries.begin(), entries.end(), path,
      [](const SeambankEntry& entry, std::string_view candidate) {
        return entry.path < candidate;
      });
  return iterator == entries.end() || iterator->path != path ? nullptr
                                                              : &*iterator;
}

core::Result<std::string> characterField(
    const formats::JsonValue& root, std::string_view field) {
  const auto* value = root.find(field);
  if (value == nullptr || !value->isString() || value->asString().empty()) {
    return core::failure<std::string>(
        core::ErrorCode::ParseError,
        "Embedded character manifest is missing an identity field",
        std::string{field});
  }
  return value->asString();
}

core::Result<void> validateCharacterBinding(
    std::ifstream& stream, const voicebank::Manifest& voicebankManifest,
    const std::vector<SeambankEntry>& entries) {
  if (voicebankManifest.characterId.empty()) return core::success();
  const auto* manifestEntry = findEntry(entries, "character/manifest.json");
  if (manifestEntry == nullptr) {
    return core::failure(
        core::ErrorCode::NotFound,
        "Character-bound seambank lacks character/manifest.json");
  }
  auto bytes = readEntryUnchecked(stream, *manifestEntry, 1024U * 1024U);
  if (!bytes) return core::Result<void>{bytes.error()};
  const std::string text(reinterpret_cast<const char*>(bytes.value().data()),
                         bytes.value().size());
  auto parsed = formats::parseJson(text, formats::JsonParseLimits{
      .maximumInputBytes = 1024U * 1024U,
      .maximumDepth = 16U,
      .maximumNodes = 2048U,
      .maximumStringBytes = 64U * 1024U,
      .maximumCollectionEntries = 512U,
  });
  if (!parsed) return core::Result<void>{parsed.error()};
  if (!parsed.value().isObject()) {
    return core::failure(core::ErrorCode::ParseError,
                         "Embedded character manifest root must be an object");
  }
  auto characterId = characterField(parsed.value(), "characterId");
  auto characterVersion = characterField(parsed.value(), "version");
  auto voicebankId = characterField(parsed.value(), "voicebankId");
  if (!characterId) return core::Result<void>{characterId.error()};
  if (!characterVersion) return core::Result<void>{characterVersion.error()};
  if (!voicebankId) return core::Result<void>{voicebankId.error()};
  if (characterId.value() != voicebankManifest.characterId ||
      characterVersion.value() != voicebankManifest.characterVersion ||
      voicebankId.value() != voicebankManifest.id) {
    return core::failure(
        core::ErrorCode::Conflict,
        "Embedded character identity does not match the signed voicebank binding");
  }
  const auto* states = parsed.value().find("states");
  if (states == nullptr || !states->isObject() || states->asObject().empty()) {
    return core::failure(core::ErrorCode::ParseError,
                         "Embedded character manifest has no runtime states");
  }
  for (const auto& [state, asset] : states->asObject()) {
    if (state.empty() || !asset.isString() ||
        !isSafeSeambankPath(asset.asString())) {
      return core::failure(core::ErrorCode::ParseError,
                           "Embedded character state asset is invalid", state);
    }
    const auto packagedPath = "character/" + asset.asString();
    if (findEntry(entries, packagedPath) == nullptr) {
      return core::failure(core::ErrorCode::NotFound,
                           "Embedded character state asset is missing",
                           packagedPath);
    }
  }
  return core::success();
}

}  // namespace

bool isSafeSeambankPath(std::string_view path) noexcept {
  if (path.empty() || path.front() == '/' || path.back() == '/' ||
      path.find('\\') != std::string_view::npos ||
      path.find(':') != std::string_view::npos ||
      path.find('\0') != std::string_view::npos) return false;
  std::size_t start = 0U;
  while (start < path.size()) {
    const auto slash = path.find('/', start);
    const auto segment = path.substr(start, slash == std::string_view::npos
                                               ? path.size() - start
                                               : slash - start);
    if (segment.empty() || segment == "." || segment == ".." ||
        segment.front() == '.') return false;
    start = slash == std::string_view::npos ? path.size() : slash + 1U;
  }
  return true;
}

bool isAllowedSeambankAsset(std::string_view path) noexcept {
  if (!isSafeSeambankPath(path)) return false;
  std::string lower{path};
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
  const auto filename = std::filesystem::path{lower}.filename().string();
  if (filename == "license" || filename == "notice") return true;
  const auto extension = std::filesystem::path{lower}.extension().string();
  static const std::set<std::string> allowed{
      ".json", ".cbor", ".wav", ".bin", ".dat", ".png", ".webp",
      ".ppm", ".pgm", ".txt", ".md", ".license"};
  return allowed.contains(extension);
}

core::Result<SeambankPackageInfo> packSeambank(
    const std::filesystem::path& sourceDirectory,
    const std::filesystem::path& outputPackage,
    const SigningKeyPair& signingKey,
    const PackSeambankOptions& options) {
  auto files = collectFiles(sourceDirectory, options.limits);
  if (!files) return core::Result<SeambankPackageInfo>{files.error()};

  voicebank::ManifestJsonCodec manifestCodec;
  auto manifest = manifestCodec.load(sourceDirectory / "manifest.json");
  if (!manifest) return core::Result<SeambankPackageInfo>{manifest.error()};
  for (const auto& unit : manifest.value().units) {
    const auto asset = unit.audioPath.generic_string();
    if (std::none_of(files.value().begin(), files.value().end(),
                     [&asset](const auto& file) { return file.path == asset; })) {
      return core::failure<SeambankPackageInfo>(
          core::ErrorCode::NotFound, "Manifest references an audio asset absent from the package", asset);
    }
  }

  std::uint64_t tableBytes = 0U;
  std::uint64_t payloadBytes = 0U;
  for (const auto& file : files.value()) {
    tableBytes += 2U + file.path.size() + 8U + 8U + 32U;
    payloadBytes += file.size;
  }
  const auto archiveBytes = kHeaderBytes + tableBytes + payloadBytes +
                            kPublicKeyBytes + kSignatureBytes;
  if (archiveBytes > options.limits.maximumArchiveBytes) {
    return core::failure<SeambankPackageInfo>(core::ErrorCode::Unsupported,
                                               "Seambank archive exceeds size limit");
  }
  std::uint64_t nextOffset = kHeaderBytes + tableBytes;
  for (auto& file : files.value()) {
    file.payloadOffset = nextOffset;
    nextOffset += file.size;
  }

  std::vector<std::byte> header;
  header.reserve(static_cast<std::size_t>(kHeaderBytes));
  header.insert(header.end(), kMagic.begin(), kMagic.end());
  appendU32(header, SeambankPackageInfo::kFormatVersion);
  appendU32(header, static_cast<std::uint32_t>(files.value().size()));
  appendU64(header, tableBytes);
  appendU64(header, payloadBytes);
  appendU32(header, kPublicKeyBytes);
  appendU32(header, kSignatureBytes);
  std::vector<std::byte> table;
  table.reserve(static_cast<std::size_t>(tableBytes));
  for (const auto& file : files.value()) {
    appendU16(table, static_cast<std::uint16_t>(file.path.size()));
    table.insert(table.end(), std::as_bytes(std::span{file.path.data(), file.path.size()}).begin(),
                 std::as_bytes(std::span{file.path.data(), file.path.size()}).end());
    appendU64(table, file.payloadOffset);
    appendU64(table, file.size);
    table.insert(table.end(), file.digest.begin(), file.digest.end());
  }

  std::error_code error;
  if (!outputPackage.parent_path().empty()) {
    std::filesystem::create_directories(outputPackage.parent_path(), error);
    if (error) return core::failure<SeambankPackageInfo>(core::ErrorCode::IoError, "Unable to create seambank output directory", error.message());
  }
  const auto temporary = temporaryPath(outputPackage);
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream) return core::failure<SeambankPackageInfo>(core::ErrorCode::IoError, "Unable to create seambank package", temporary.string());
  core::Sha256 signedHash;
  auto written = writeAndHash(stream, signedHash, header);
  if (!written) return core::Result<SeambankPackageInfo>{written.error()};
  written = writeAndHash(stream, signedHash, table);
  if (!written) return core::Result<SeambankPackageInfo>{written.error()};
  std::array<char, kIoBlockBytes> buffer{};
  for (const auto& file : files.value()) {
    std::ifstream input(file.source, std::ios::binary);
    std::uint64_t remaining = file.size;
    while (remaining > 0U) {
      const auto request = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
      input.read(buffer.data(), static_cast<std::streamsize>(request));
      if (input.gcount() != static_cast<std::streamsize>(request)) {
        stream.close();
        std::filesystem::remove(temporary, error);
        return core::failure<SeambankPackageInfo>(core::ErrorCode::IoError, "Source asset changed while packing", file.path);
      }
      written = writeAndHash(stream, signedHash,
                             std::as_bytes(std::span{buffer.data(), request}));
      if (!written) return core::Result<SeambankPackageInfo>{written.error()};
      remaining -= request;
    }
  }
  written = writeAndHash(stream, signedHash, signingKey.publicKey);
  if (!written) return core::Result<SeambankPackageInfo>{written.error()};
  const auto signedDigest = signedHash.digest();
  const auto signingMessage = signatureMessage(signedDigest);
  auto signature = signEd25519(signingMessage, signingKey.privateKey);
  if (!signature) return core::Result<SeambankPackageInfo>{signature.error()};
  stream.write(reinterpret_cast<const char*>(signature.value().data()),
               static_cast<std::streamsize>(signature.value().size()));
  stream.flush();
  if (!stream) return core::failure<SeambankPackageInfo>(core::ErrorCode::IoError, "Unable to finalize seambank package");
  stream.close();
  auto committed = durableCommit(temporary, outputPackage);
  if (!committed) {
    std::filesystem::remove(temporary, error);
    return core::Result<SeambankPackageInfo>{committed.error()};
  }
  VerifySeambankOptions verification{.limits = options.limits,
                                     .trustedPublicKeys = {signingKey.publicKey},
                                     .requireTrustedSigner = true};
  auto verifiedPackage = verifySeambank(outputPackage, verification);
  if (!verifiedPackage) {
    std::filesystem::remove(outputPackage, error);
    return core::Result<SeambankPackageInfo>{verifiedPackage.error()};
  }
  return verifiedPackage;
}

core::Result<SeambankPackageInfo> verifySeambank(
    const std::filesystem::path& packagePath,
    const VerifySeambankOptions& options) {
  std::error_code error;
  const auto fileSize = std::filesystem::file_size(packagePath, error);
  if (error || fileSize < kHeaderBytes + kPublicKeyBytes + kSignatureBytes ||
      fileSize > options.limits.maximumArchiveBytes) {
    return core::failure<SeambankPackageInfo>(core::ErrorCode::ParseError,
                                               "Seambank package size is invalid", packagePath.string());
  }
  std::ifstream stream(packagePath, std::ios::binary);
  std::array<std::byte, 8U> magic{};
  stream.read(reinterpret_cast<char*>(magic.data()), magic.size());
  if (!stream || magic != kMagic) {
    return core::failure<SeambankPackageInfo>(core::ErrorCode::ParseError, "Seambank magic is invalid");
  }
  auto version = readU32(stream);
  auto entryCount = readU32(stream);
  auto tableBytes = readU64(stream);
  auto payloadBytes = readU64(stream);
  auto publicBytes = readU32(stream);
  auto signatureBytes = readU32(stream);
  if (!version || !entryCount || !tableBytes || !payloadBytes || !publicBytes || !signatureBytes) {
    return core::failure<SeambankPackageInfo>(core::ErrorCode::ParseError, "Seambank header is truncated");
  }
  if (version.value() != SeambankPackageInfo::kFormatVersion ||
      entryCount.value() == 0U || entryCount.value() > options.limits.maximumEntries ||
      payloadBytes.value() > options.limits.maximumPayloadBytes ||
      publicBytes.value() != kPublicKeyBytes || signatureBytes.value() != kSignatureBytes) {
    return core::failure<SeambankPackageInfo>(core::ErrorCode::Unsupported, "Seambank header uses unsupported limits or version");
  }
  const auto payloadStart = kHeaderBytes + tableBytes.value();
  const auto publicOffset = payloadStart + payloadBytes.value();
  const auto signatureOffset = publicOffset + kPublicKeyBytes;
  if (signatureOffset + kSignatureBytes != fileSize || payloadStart < kHeaderBytes) {
    return core::failure<SeambankPackageInfo>(core::ErrorCode::ParseError, "Seambank declared sections do not match file size");
  }

  std::vector<SeambankEntry> entries;
  entries.reserve(entryCount.value());
  std::string previous;
  std::uint64_t expectedOffset = payloadStart;
  const auto tableEnd = payloadStart;
  for (std::uint32_t index = 0U; index < entryCount.value(); ++index) {
    auto pathLength = readU16(stream);
    if (!pathLength || pathLength.value() == 0U ||
        pathLength.value() > options.limits.maximumPathBytes) {
      return core::failure<SeambankPackageInfo>(core::ErrorCode::ParseError, "Seambank entry path length is invalid");
    }
    std::string path(pathLength.value(), '\0');
    stream.read(path.data(), static_cast<std::streamsize>(path.size()));
    auto offset = readU64(stream);
    auto size = readU64(stream);
    std::array<std::byte, 32U> digest{};
    stream.read(reinterpret_cast<char*>(digest.data()), digest.size());
    if (!stream || !offset || !size || !isSafeSeambankPath(path) ||
        !isAllowedSeambankAsset(path) || (!previous.empty() && path <= previous) ||
        size.value() > options.limits.maximumEntryBytes ||
        offset.value() != expectedOffset ||
        offset.value() > publicOffset || size.value() > publicOffset - offset.value()) {
      return core::failure<SeambankPackageInfo>(core::ErrorCode::ParseError, "Seambank entry table is invalid", path);
    }
    expectedOffset += size.value();
    entries.push_back(SeambankEntry{.path = path,
                                    .payloadOffset = offset.value(),
                                    .payloadSize = size.value(),
                                    .sha256 = digest});
    previous = std::move(path);
  }
  if (static_cast<std::uint64_t>(stream.tellg()) != tableEnd || expectedOffset != publicOffset) {
    return core::failure<SeambankPackageInfo>(core::ErrorCode::ParseError, "Seambank table size or payload extent is invalid");
  }

  Ed25519PublicKey publicKey{};
  Ed25519Signature signature{};
  stream.seekg(static_cast<std::streamoff>(publicOffset), std::ios::beg);
  stream.read(reinterpret_cast<char*>(publicKey.data()), publicKey.size());
  stream.read(reinterpret_cast<char*>(signature.data()), signature.size());
  if (!stream) return core::failure<SeambankPackageInfo>(core::ErrorCode::ParseError, "Seambank signature section is truncated");

  core::Sha256 signedHash;
  auto hashed = hashRange(stream, signatureOffset, signedHash);
  if (!hashed) return core::Result<SeambankPackageInfo>{hashed.error()};
  const auto signedDigest = signedHash.digest();
  const auto signingMessage = signatureMessage(signedDigest);
  const auto verified = verifyEd25519(signingMessage, signature, publicKey);
  if (!verified) return core::Result<SeambankPackageInfo>{verified.error()};

  for (const auto& entry : entries) {
    auto digest = hashEntry(stream, entry);
    if (!digest) return core::Result<SeambankPackageInfo>{digest.error()};
    if (digest.value() != entry.sha256) {
      return core::failure<SeambankPackageInfo>(core::ErrorCode::Conflict,
                                                 "Seambank asset checksum mismatch", entry.path);
    }
  }
  const auto manifestIterator = std::find_if(entries.begin(), entries.end(),
      [](const auto& entry) { return entry.path == "manifest.json"; });
  if (manifestIterator == entries.end()) {
    return core::failure<SeambankPackageInfo>(core::ErrorCode::NotFound, "Seambank package has no manifest.json");
  }
  auto manifestBytes = readEntryUnchecked(stream, *manifestIterator, 32U * 1024U * 1024U);
  if (!manifestBytes) return core::Result<SeambankPackageInfo>{manifestBytes.error()};
  const std::string manifestText(reinterpret_cast<const char*>(manifestBytes.value().data()),
                                 manifestBytes.value().size());
  voicebank::ManifestJsonCodec manifestCodec;
  auto manifest = manifestCodec.decode(manifestText);
  if (!manifest) return core::Result<SeambankPackageInfo>{manifest.error()};
  for (const auto& unit : manifest.value().units) {
    const auto asset = unit.audioPath.generic_string();
    if (findEntry(entries, asset) == nullptr) {
      return core::failure<SeambankPackageInfo>(core::ErrorCode::NotFound,
                                                 "Signed manifest references a missing asset", asset);
    }
  }
  const auto characterBinding =
      validateCharacterBinding(stream, manifest.value(), entries);
  if (!characterBinding) {
    return core::Result<SeambankPackageInfo>{characterBinding.error()};
  }

  const auto trusted = std::any_of(options.trustedPublicKeys.begin(),
                                   options.trustedPublicKeys.end(),
                                   [&publicKey](const auto& candidate) { return candidate == publicKey; });
  if (options.requireTrustedSigner && !trusted) {
    return core::failure<SeambankPackageInfo>(core::ErrorCode::Conflict,
                                               "Seambank signer is not trusted",
                                               publicKeyId(publicKey));
  }
  auto packageDigest = core::sha256File(packagePath, options.limits.maximumArchiveBytes);
  if (!packageDigest) return core::Result<SeambankPackageInfo>{packageDigest.error()};
  return SeambankPackageInfo{
      .packagePath = packagePath,
      .formatVersion = version.value(),
      .packageDigest = packageDigest.value(),
      .signerPublicKey = publicKey,
      .signerKeyId = publicKeyId(publicKey),
      .signature = signature,
      .manifest = std::move(manifest.value()),
      .entries = std::move(entries),
      .payloadBytes = payloadBytes.value(),
      .signatureValid = true,
      .signerTrusted = trusted,
  };
}

core::Result<std::vector<std::byte>> readSeambankEntry(
    const std::filesystem::path& packagePath, std::string_view entryPath,
    const VerifySeambankOptions& options) {
  if (!isSafeSeambankPath(entryPath)) {
    return core::failure<std::vector<std::byte>>(core::ErrorCode::InvalidArgument,
                                                 "Unsafe seambank entry path");
  }
  auto package = verifySeambank(packagePath, options);
  if (!package) return core::Result<std::vector<std::byte>>{package.error()};
  const auto iterator = std::find_if(package.value().entries.begin(),
                                    package.value().entries.end(),
                                    [entryPath](const auto& entry) { return entry.path == entryPath; });
  if (iterator == package.value().entries.end()) {
    return core::failure<std::vector<std::byte>>(core::ErrorCode::NotFound,
                                                 "Seambank entry does not exist",
                                                 std::string{entryPath});
  }
  std::ifstream stream(packagePath, std::ios::binary);
  return readEntryUnchecked(stream, *iterator, options.limits.maximumEntryBytes);
}

}  // namespace seam::distribution
