#include "seam/phase12c/canonical_evidence.hpp"

#include "seam/build/version.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voicebank/catalog.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string_view>
#include <vector>

namespace seam::phase12c {
namespace {

constexpr std::uint64_t kMaximumEvidenceTreeBytes = 256ULL * 1024ULL * 1024ULL;

core::Result<std::filesystem::path> checkedRoot(
    const std::filesystem::path& value, std::string_view label) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(value, error);
  if (error || status.type() == std::filesystem::file_type::symlink) {
    return core::failure<std::filesystem::path>(core::ErrorCode::Conflict,
        std::string{label} + " cannot be a symbolic link", value.string());
  }
  if (!std::filesystem::is_directory(status)) {
    return core::failure<std::filesystem::path>(core::ErrorCode::NotFound,
        std::string{label} + " must be a directory", value.string());
  }
  return value;
}

core::Result<std::string> treeSha256(const std::filesystem::path& root,
    std::string_view label) {
  std::error_code artifactError;
  const auto artifactStatus = std::filesystem::symlink_status(root, artifactError);
  if (!artifactError && artifactStatus.type() == std::filesystem::file_type::regular) {
    return core::sha256File(root, kMaximumEvidenceTreeBytes);
  }
  const auto checked = checkedRoot(root, label);
  if (!checked) return core::Result<std::string>{checked.error()};

  std::error_code error;
  std::vector<std::filesystem::path> files;
  for (std::filesystem::recursive_directory_iterator iterator{
           checked.value(), std::filesystem::directory_options::none, error};
       iterator != std::filesystem::recursive_directory_iterator{};
       iterator.increment(error)) {
    if (error) {
      return core::failure<std::string>(core::ErrorCode::IoError,
          std::string{label} + " tree cannot be enumerated", error.message());
    }
    const auto& path = iterator->path();
    const auto status = iterator->symlink_status(error);
    if (error) {
      return core::failure<std::string>(core::ErrorCode::IoError,
          std::string{label} + " tree member cannot be inspected", path.string());
    }
    if (status.type() == std::filesystem::file_type::symlink) {
      return core::failure<std::string>(core::ErrorCode::Conflict,
          std::string{label} + " tree cannot contain symbolic links", path.string());
    }
    if (!std::filesystem::is_regular_file(status)) continue;
    files.push_back(path);
  }
  std::sort(files.begin(), files.end(), [&](const auto& lhs, const auto& rhs) {
    return lhs.lexically_relative(checked.value()).generic_string() <
        rhs.lexically_relative(checked.value()).generic_string();
  });

  std::uint64_t total = 0U;
  core::Sha256 hash;
  for (const auto& path : files) {
    std::error_code sizeError;
    const auto size = std::filesystem::file_size(path, sizeError);
    if (sizeError || size > kMaximumEvidenceTreeBytes -
        std::min(total, kMaximumEvidenceTreeBytes)) {
      return core::failure<std::string>(core::ErrorCode::Unsupported,
          std::string{label} + " tree exceeds the bounded evidence size", path.string());
    }
    total += size;
    const auto relative = path.lexically_relative(checked.value()).generic_string();
    if (relative.size() > std::numeric_limits<std::uint32_t>::max()) {
      return core::failure<std::string>(core::ErrorCode::Unsupported,
          std::string{label} + " tree member path is too long", relative);
    }
    const auto length = static_cast<std::uint32_t>(relative.size());
    std::array<std::byte, 4> littleEndian{
        static_cast<std::byte>(length & 0xffU),
        static_cast<std::byte>((length >> 8U) & 0xffU),
        static_cast<std::byte>((length >> 16U) & 0xffU),
        static_cast<std::byte>((length >> 24U) & 0xffU)};
    hash.update(littleEndian);
    hash.update(relative);
    const auto fileHash = core::sha256File(path, kMaximumEvidenceTreeBytes);
    if (!fileHash) return core::Result<std::string>{fileHash.error()};
    std::array<std::byte, 32> raw{};
    if (fileHash.value().size() != raw.size() * 2U) {
      return core::failure<std::string>(core::ErrorCode::InvariantViolation,
          std::string{label} + " member digest has an invalid length", path.string());
    }
    const auto nibble = [](char value) -> int {
      if (value >= '0' && value <= '9') return value - '0';
      if (value >= 'a' && value <= 'f') return value - 'a' + 10;
      if (value >= 'A' && value <= 'F') return value - 'A' + 10;
      return -1;
    };
    for (std::size_t index = 0U; index < raw.size(); ++index) {
      const auto high = nibble(fileHash.value()[index * 2U]);
      const auto low = nibble(fileHash.value()[index * 2U + 1U]);
      if (high < 0 || low < 0) {
        return core::failure<std::string>(core::ErrorCode::InvariantViolation,
            std::string{label} + " member digest is not hexadecimal", path.string());
      }
      raw[index] = static_cast<std::byte>((high << 4) | low);
    }
    hash.update(raw);
  }
  return hash.hexDigest();
}

}  // namespace

core::Result<CanonicalEvidenceIdentity> loadCanonicalEvidenceIdentity(
    const std::filesystem::path& plugin,
    const std::filesystem::path& bank, bool developmentFixture,
    std::string style) {
  const auto pluginHash = treeSha256(plugin, "Canonical CLAP plugin");
  if (!pluginHash) return core::Result<CanonicalEvidenceIdentity>{pluginHash.error()};
  const auto bankRoot = checkedRoot(bank, "Canonical voicebank");
  if (!bankRoot) return core::Result<CanonicalEvidenceIdentity>{bankRoot.error()};
  voicebank::VoicebankCatalog catalog;
  const auto scanned = catalog.scan({voicebank::VoicebankSearchRoot{
      .path = bankRoot.value(),
      .kind = developmentFixture ? voicebank::VoicebankRootKind::Development
                                : voicebank::VoicebankRootKind::Installed,
  }});
  if (!scanned || scanned.value().size() != 1U) {
    return core::failure<CanonicalEvidenceIdentity>(core::ErrorCode::NotFound,
        "Canonical voicebank root must contain exactly one candidate", bank.string());
  }
  const auto& candidate = scanned.value().front();
  if (!developmentFixture && candidate.trust != voicebank::VoicebankTrust::TrustedInstalled) {
    return core::failure<CanonicalEvidenceIdentity>(core::ErrorCode::Conflict,
        "Installed-bank evidence requires a trusted installed resource");
  }
  if (style.empty() && candidate.manifest.styles.size() == 1U) {
    style = candidate.manifest.styles.front();
  }
  if (style.empty() || std::find(candidate.manifest.styles.begin(),
      candidate.manifest.styles.end(), style) == candidate.manifest.styles.end()) {
    return core::failure<CanonicalEvidenceIdentity>(core::ErrorCode::InvalidArgument,
        "Select an exact declared bank style with --style");
  }
  const auto bankHash = treeSha256(bankRoot.value(), "Canonical voicebank");
  if (!bankHash) return core::Result<CanonicalEvidenceIdentity>{bankHash.error()};
  return core::success(CanonicalEvidenceIdentity{
      .pluginSha256 = pluginHash.value(),
      .voicebankId = scanned.value().front().manifest.id,
      .voicebankVersion = scanned.value().front().manifest.version,
      .voicebankTreeSha256 = bankHash.value(),
      .sourceCommit = std::string{build::kSourceCommit},
      .buildId = std::string{build::kBuildId},
      .voicebankContentHash = candidate.contentHash,
      .style = std::move(style),
  });
}

}  // namespace seam::phase12c
