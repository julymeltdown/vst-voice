#include "seam/clap/state_codec.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <limits>

namespace seam::clap {
namespace {
constexpr std::array<std::byte, 8> kMagic{
    std::byte{'S'}, std::byte{'E'}, std::byte{'A'}, std::byte{'M'},
    std::byte{'C'}, std::byte{'L'}, std::byte{'P'}, std::byte{'1'}};
constexpr std::size_t kHeaderBytes = 52U;
constexpr std::size_t kDigestBytes = 32U;

void appendU32(std::vector<std::byte>& out, std::uint32_t value) {
  for (unsigned shift = 0U; shift < 32U; shift += 8U)
    out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}
void appendU64(std::vector<std::byte>& out, std::uint64_t value) {
  for (unsigned shift = 0U; shift < 64U; shift += 8U)
    out.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
}
core::Result<std::uint32_t> readU32(std::span<const std::byte> bytes,
                                    std::size_t& offset) {
  if (offset > bytes.size() || bytes.size() - offset < 4U)
    return core::failure<std::uint32_t>(core::ErrorCode::ParseError,
                                        "CLAP state is truncated");
  std::uint32_t value = 0U;
  for (unsigned shift = 0U; shift < 32U; shift += 8U)
    value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset++])) << shift;
  return value;
}
core::Result<std::uint64_t> readU64(std::span<const std::byte> bytes,
                                    std::size_t& offset) {
  if (offset > bytes.size() || bytes.size() - offset < 8U)
    return core::failure<std::uint64_t>(core::ErrorCode::ParseError,
                                        "CLAP state is truncated");
  std::uint64_t value = 0U;
  for (unsigned shift = 0U; shift < 64U; shift += 8U)
    value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset++])) << shift;
  return value;
}
}  // namespace

core::Result<std::vector<std::byte>> encodeState(const PluginSession& session) {
  const auto validation = session.validate();
  if (!validation) return core::Result<std::vector<std::byte>>{validation.error()};
  const auto payloadBytes = static_cast<std::uint64_t>(session.interleavedSamples.size()) * 4U;
  const auto expected = static_cast<std::uint64_t>(kHeaderBytes) +
                        session.title.size() + payloadBytes + kDigestBytes;
  if (expected > kMaximumStateBytes)
    return core::failure<std::vector<std::byte>>(core::ErrorCode::InvalidArgument,
                                                 "Encoded CLAP state exceeds size limit");

  std::vector<std::byte> out;
  out.reserve(static_cast<std::size_t>(expected));
  out.insert(out.end(), kMagic.begin(), kMagic.end());
  appendU32(out, kStateFormatVersion);
  appendU32(out, session.sampleRate);
  appendU32(out, session.channelCount);
  appendU32(out, 0U);
  appendU64(out, session.frameCount());
  appendU64(out, std::bit_cast<std::uint64_t>(session.masterGainDb));
  appendU32(out, static_cast<std::uint32_t>(session.title.size()));
  appendU64(out, payloadBytes);
  for (const char character : session.title)
    out.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
  for (const float sample : session.interleavedSamples)
    appendU32(out, std::bit_cast<std::uint32_t>(sample));
  core::Sha256 hash;
  hash.update(std::span<const std::byte>{out});
  const auto bytes = hash.digest();
  out.insert(out.end(), bytes.begin(), bytes.end());
  return out;
}

core::Result<PluginSession> decodeState(std::span<const std::byte> bytes) {
  if (bytes.size() < kHeaderBytes + kDigestBytes || bytes.size() > kMaximumStateBytes)
    return core::failure<PluginSession>(core::ErrorCode::ParseError,
                                        "CLAP state size is invalid");
  if (!std::equal(kMagic.begin(), kMagic.end(), bytes.begin()))
    return core::failure<PluginSession>(core::ErrorCode::ParseError,
                                        "CLAP state magic is invalid");
  const auto content = bytes.first(bytes.size() - kDigestBytes);
  core::Sha256 hash;
  hash.update(content);
  const auto expectedDigest = hash.digest();
  if (!std::equal(expectedDigest.begin(), expectedDigest.end(),
                  bytes.end() - static_cast<std::ptrdiff_t>(kDigestBytes)))
    return core::failure<PluginSession>(core::ErrorCode::ParseError,
                                        "CLAP state SHA-256 digest mismatch");

  std::size_t offset = kMagic.size();
  const auto version = readU32(bytes, offset);
  const auto sampleRate = readU32(bytes, offset);
  const auto channels = readU32(bytes, offset);
  const auto reserved = readU32(bytes, offset);
  const auto frames = readU64(bytes, offset);
  const auto gainBits = readU64(bytes, offset);
  const auto titleBytes = readU32(bytes, offset);
  const auto payloadBytes = readU64(bytes, offset);
  if (!version || !sampleRate || !channels || !reserved || !frames ||
      !gainBits || !titleBytes || !payloadBytes)
    return core::failure<PluginSession>(core::ErrorCode::ParseError,
                                        "CLAP state header is truncated");
  if (version.value() != kStateFormatVersion || reserved.value() != 0U ||
      channels.value() == 0U || channels.value() > kMaximumChannels ||
      titleBytes.value() > 4096U || payloadBytes.value() % 4U != 0U ||
      payloadBytes.value() > kMaximumStateBytes ||
      frames.value() > std::numeric_limits<std::uint64_t>::max() / channels.value() ||
      payloadBytes.value() / 4U != frames.value() * channels.value())
    return core::failure<PluginSession>(core::ErrorCode::ParseError,
                                        "CLAP state header fields are inconsistent");
  const auto remaining = static_cast<std::uint64_t>(content.size() - offset);
  if (payloadBytes.value() > remaining || titleBytes.value() > remaining - payloadBytes.value() ||
      static_cast<std::uint64_t>(titleBytes.value()) + payloadBytes.value() != remaining)
    return core::failure<PluginSession>(core::ErrorCode::ParseError,
                                        "CLAP state payload size does not match header");

  PluginSession session;
  session.sampleRate = sampleRate.value();
  session.channelCount = static_cast<std::uint8_t>(channels.value());
  session.masterGainDb = std::bit_cast<double>(gainBits.value());
  session.title.assign(reinterpret_cast<const char*>(bytes.data() + offset),
                       titleBytes.value());
  offset += titleBytes.value();
  session.interleavedSamples.resize(static_cast<std::size_t>(payloadBytes.value() / 4U));
  for (auto& sample : session.interleavedSamples) {
    const auto bits = readU32(bytes, offset);
    if (!bits) return core::Result<PluginSession>{bits.error()};
    sample = std::bit_cast<float>(bits.value());
  }
  const auto validation = session.validate();
  if (!validation) return core::Result<PluginSession>{validation.error()};
  return session;
}

core::Result<void> writeStateFile(const std::filesystem::path& path,
                                  const PluginSession& session) {
  const auto encoded = encodeState(session);
  if (!encoded) return core::Result<void>{encoded.error()};
  return core::durableAtomicWrite(path, encoded.value());
}

core::Result<PluginSession> readStateFile(const std::filesystem::path& path) {
  const auto bytes = core::readFileBytesLimited(path, kMaximumStateBytes);
  if (!bytes) return core::Result<PluginSession>{bytes.error()};
  return decodeState(bytes.value());
}

}  // namespace seam::clap
