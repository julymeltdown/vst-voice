#include "seam/core/sha256.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace seam::core {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

std::uint32_t loadBigEndian(const std::byte* input) noexcept {
  return (static_cast<std::uint32_t>(std::to_integer<unsigned char>(input[0])) << 24U) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned char>(input[1])) << 16U) |
         (static_cast<std::uint32_t>(std::to_integer<unsigned char>(input[2])) << 8U) |
         static_cast<std::uint32_t>(std::to_integer<unsigned char>(input[3]));
}

void storeBigEndian(std::uint32_t value, std::byte* output) noexcept {
  output[0] = static_cast<std::byte>((value >> 24U) & 0xffU);
  output[1] = static_cast<std::byte>((value >> 16U) & 0xffU);
  output[2] = static_cast<std::byte>((value >> 8U) & 0xffU);
  output[3] = static_cast<std::byte>(value & 0xffU);
}

}  // namespace

Sha256::Sha256() noexcept
    : state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
             0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U} {}

void Sha256::transform(const std::byte* block) noexcept {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t index = 0; index < 16U; ++index) {
    words[index] = loadBigEndian(block + index * 4U);
  }
  for (std::size_t index = 16U; index < words.size(); ++index) {
    const auto s0 = std::rotr(words[index - 15U], 7) ^
                    std::rotr(words[index - 15U], 18) ^
                    (words[index - 15U] >> 3U);
    const auto s1 = std::rotr(words[index - 2U], 17) ^
                    std::rotr(words[index - 2U], 19) ^
                    (words[index - 2U] >> 10U);
    words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
  }

  auto a = state_[0];
  auto b = state_[1];
  auto c = state_[2];
  auto d = state_[3];
  auto e = state_[4];
  auto f = state_[5];
  auto g = state_[6];
  auto h = state_[7];

  for (std::size_t index = 0; index < words.size(); ++index) {
    const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const auto choose = (e & f) ^ ((~e) & g);
    const auto temporary1 = h + sum1 + choose + kRoundConstants[index] + words[index];
    const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    const auto temporary2 = sum0 + majority;

    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(std::span<const std::byte> bytes) noexcept {
  if (finalized_ || bytes.empty()) return;
  totalBytes_ += static_cast<std::uint64_t>(bytes.size());
  std::size_t cursor = 0U;
  if (bufferedBytes_ != 0U) {
    const auto copied = std::min(buffer_.size() - bufferedBytes_, bytes.size());
    std::copy_n(bytes.begin(), static_cast<std::ptrdiff_t>(copied),
                buffer_.begin() + static_cast<std::ptrdiff_t>(bufferedBytes_));
    bufferedBytes_ += copied;
    cursor += copied;
    if (bufferedBytes_ == buffer_.size()) {
      transform(buffer_.data());
      bufferedBytes_ = 0U;
    }
  }
  while (cursor + buffer_.size() <= bytes.size()) {
    transform(bytes.data() + cursor);
    cursor += buffer_.size();
  }
  if (cursor < bytes.size()) {
    const auto remaining = bytes.size() - cursor;
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
                static_cast<std::ptrdiff_t>(remaining), buffer_.begin());
    bufferedBytes_ = remaining;
  }
}

void Sha256::update(std::string_view text) noexcept {
  update(std::as_bytes(std::span{text.data(), text.size()}));
}

void Sha256::finalizeInPlace() noexcept {
  if (finalized_) return;
  const auto bitCount = totalBytes_ * 8ULL;
  buffer_[bufferedBytes_++] = std::byte{0x80};
  if (bufferedBytes_ > 56U) {
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(bufferedBytes_),
              buffer_.end(), std::byte{0});
    transform(buffer_.data());
    bufferedBytes_ = 0U;
  }
  std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(bufferedBytes_),
            buffer_.begin() + 56, std::byte{0});
  for (std::size_t index = 0U; index < 8U; ++index) {
    buffer_[63U - index] = static_cast<std::byte>((bitCount >> (index * 8U)) & 0xffU);
  }
  transform(buffer_.data());
  bufferedBytes_ = 0U;
  finalized_ = true;
}

std::array<std::byte, 32> Sha256::digest() const noexcept {
  auto copy = *this;
  copy.finalizeInPlace();
  std::array<std::byte, 32> result{};
  for (std::size_t index = 0U; index < copy.state_.size(); ++index) {
    storeBigEndian(copy.state_[index], result.data() + index * 4U);
  }
  return result;
}

std::string Sha256::hexDigest() const {
  const auto bytes = digest();
  std::ostringstream stream;
  stream << std::hex << std::setfill('0');
  for (const auto byte : bytes) {
    stream << std::setw(2) << static_cast<unsigned>(std::to_integer<unsigned char>(byte));
  }
  return stream.str();
}

std::string sha256Hex(std::span<const std::byte> bytes) {
  Sha256 hash;
  hash.update(bytes);
  return hash.hexDigest();
}

std::string sha256Hex(std::string_view text) {
  Sha256 hash;
  hash.update(text);
  return hash.hexDigest();
}

core::Result<std::string> sha256File(const std::filesystem::path& path,
                                     std::uint64_t maximumBytes) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (status.type() == std::filesystem::file_type::symlink) {
    return core::failure<std::string>(
        core::ErrorCode::Conflict, "Unable to hash a symbolic link",
        path.string());
  }
  if (error || !std::filesystem::is_regular_file(status)) {
    return core::failure<std::string>(core::ErrorCode::IoError,
                                      "Unable to hash a non-regular file",
                                      path.string());
  }
  const auto bytes = std::filesystem::file_size(path, error);
  if (error) {
    return core::failure<std::string>(core::ErrorCode::IoError,
                                      "Unable to inspect file before hashing",
                                      error.message());
  }
  if (bytes > maximumBytes) {
    return core::failure<std::string>(core::ErrorCode::Unsupported,
                                      "File exceeds the hashing size limit",
                                      path.string());
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return core::failure<std::string>(core::ErrorCode::IoError,
                                      "Unable to open file for hashing",
                                      path.string());
  }
  Sha256 hash;
  std::array<char, 64U * 1024U> buffer{};
  while (stream) {
    stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = stream.gcount();
    if (count > 0) {
      hash.update(std::as_bytes(std::span{buffer.data(), static_cast<std::size_t>(count)}));
    }
  }
  if (!stream.eof()) {
    return core::failure<std::string>(core::ErrorCode::IoError,
                                      "Unable to read file while hashing",
                                      path.string());
  }
  return hash.hexDigest();
}

}  // namespace seam::core
