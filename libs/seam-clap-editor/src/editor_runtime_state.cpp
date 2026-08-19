#include "seam/clap_editor/editor_runtime.hpp"

#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace seam::clap_editor {
namespace {
constexpr std::array<char, 8> kStateMagic{'S', 'E', 'A', 'M', 'E', 'D', '1', '1'};
constexpr std::uint32_t kStateVersion = 1U;
constexpr std::size_t kDigestBytes = 32U;
constexpr std::size_t kMaximumStateBytes = 16U * 1024U * 1024U;
bool readU32(std::span<const std::byte> bytes, std::size_t& cursor,
             std::uint32_t& value) noexcept {
  if (cursor > bytes.size() || bytes.size() - cursor < 4U) return false;
  value = 0U;
  for (std::uint32_t index = 0U; index < 4U; ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<unsigned char>(bytes[cursor + index]))
             << (index * 8U);
  }
  cursor += 4U;
  return true;
}
}  // namespace

core::Result<std::vector<std::byte>> encodeEditorState(
    const domain::Project& project) {
  formats::ProjectJsonCodec codec;
  const auto encoded = codec.encode(project);
  if (!encoded) return core::Result<std::vector<std::byte>>{encoded.error()};
  if (encoded.value().size() > kMaximumStateBytes) {
    return core::failure<std::vector<std::byte>>(
        core::ErrorCode::Unsupported, "CLAP editor state exceeds 16 MiB");
  }
  core::Sha256 hash;
  hash.update(encoded.value());
  const auto bytesDigest = hash.digest();
  const auto totalSize = kStateMagic.size() + 8U + kDigestBytes +
                         encoded.value().size();
  std::vector<std::byte> output(totalSize);
  std::size_t cursor = 0U;
  for (const auto value : kStateMagic) {
    output[cursor++] = static_cast<std::byte>(value);
  }
  const auto writeU32 = [&output, &cursor](std::uint32_t value) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
      output[cursor++] = static_cast<std::byte>((value >> shift) & 0xffU);
    }
  };
  writeU32(kStateVersion);
  writeU32(static_cast<std::uint32_t>(encoded.value().size()));
  std::copy(bytesDigest.begin(), bytesDigest.end(), output.begin() +
            static_cast<std::ptrdiff_t>(cursor));
  cursor += bytesDigest.size();
  const auto jsonBytes = std::as_bytes(
      std::span{encoded.value().data(), encoded.value().size()});
  std::copy(jsonBytes.begin(), jsonBytes.end(), output.begin() +
            static_cast<std::ptrdiff_t>(cursor));
  return output;
}

core::Result<domain::Project> decodeEditorState(
    std::span<const std::byte> bytes) {
  const auto minimum = kStateMagic.size() + 8U + kDigestBytes;
  if (bytes.size() < minimum || bytes.size() > kMaximumStateBytes + minimum) {
    return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                         "CLAP editor state size is invalid");
  }
  for (std::size_t index = 0U; index < kStateMagic.size(); ++index) {
    if (bytes[index] != static_cast<std::byte>(kStateMagic[index])) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                           "CLAP editor state magic is invalid");
    }
  }
  std::size_t cursor = kStateMagic.size();
  std::uint32_t version = 0U;
  std::uint32_t jsonSize = 0U;
  if (!readU32(bytes, cursor, version) || version != kStateVersion ||
      !readU32(bytes, cursor, jsonSize)) {
    return core::failure<domain::Project>(core::ErrorCode::Unsupported,
                                         "CLAP editor state version is unsupported");
  }
  if (cursor + kDigestBytes > bytes.size()) {
    return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                         "CLAP editor state digest is truncated");
  }
  const auto expectedDigest = bytes.subspan(cursor, kDigestBytes);
  cursor += kDigestBytes;
  if (jsonSize > kMaximumStateBytes ||
      cursor > bytes.size() || bytes.size() - cursor != jsonSize) {
    return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                         "CLAP editor state payload size is invalid");
  }
  const auto payload = bytes.subspan(cursor, jsonSize);
  core::Sha256 hash;
  hash.update(payload);
  const auto actualDigest = hash.digest();
  if (!std::equal(actualDigest.begin(), actualDigest.end(),
                  expectedDigest.begin(), expectedDigest.end())) {
    return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                         "CLAP editor state checksum mismatch");
  }
  const std::string json{
      reinterpret_cast<const char*>(payload.data()), payload.size()};
  formats::ProjectJsonCodec codec;
  return codec.decode(json);
}

}  // namespace seam::clap_editor
