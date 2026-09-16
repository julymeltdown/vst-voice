#include "seam/authoring/japanese_reading_resource.hpp"
#include "seam/core/sha256.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <cstdio>
#include <memory>
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seam::authoring {
namespace {
struct FileCloser final {
  void operator()(FILE* file) const noexcept {
    if (file != nullptr) static_cast<void>(std::fclose(file));
  }
};

bool hex(std::string_view value, std::size_t count) {
  return value.size() == count && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
core::Result<void> checkFile(const std::filesystem::path& path, std::string_view expected,
    std::uintmax_t maximumBytes, std::uintmax_t& remaining, std::stop_token stop) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) || error)
    return core::failure(core::ErrorCode::InvalidArgument, "Reading resource must be a regular non-symlink file");
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0U || size > maximumBytes || size > remaining)
    return core::failure(core::ErrorCode::InvalidArgument, "Reading resource file exceeds size bounds");
  const auto before = std::filesystem::last_write_time(path, error);
  if (error) return core::failure(core::ErrorCode::IoError, "Cannot inspect reading resource time");
#if defined(__APPLE__) || defined(__linux__)
  const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (descriptor < 0) return core::failure(core::ErrorCode::IoError, "Cannot open reading resource without following links");
  struct stat opened{};
  if (::fstat(descriptor, &opened) != 0 || !S_ISREG(opened.st_mode)) {
    ::close(descriptor); return core::failure(core::ErrorCode::InvalidArgument, "Reading resource handle is not a regular file");
  }
  std::unique_ptr<FILE, FileCloser> input{::fdopen(descriptor, "rb")};
  if (!input) { ::close(descriptor); return core::failure(core::ErrorCode::IoError, "Cannot read resource handle"); }
#else
  std::unique_ptr<FILE, FileCloser> input{std::fopen(path.string().c_str(), "rb")};
  if (!input) return core::failure(core::ErrorCode::IoError, "Cannot open reading resource");
#endif
  core::Sha256 hash; std::array<char, 65536U> buffer{}; std::uintmax_t readBytes = 0U;
  for (;;) {
    if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Reading resource verification cancelled");
    const auto count = std::fread(buffer.data(), 1U, buffer.size(), input.get());
    if (count == 0U) break;
    if (count > 0) {
      const auto bytes = static_cast<std::uintmax_t>(count);
      if (bytes > size - readBytes) return core::failure(core::ErrorCode::Conflict, "Reading resource changed during verification");
      readBytes += bytes; hash.update(std::string_view(buffer.data(), static_cast<std::size_t>(count)));
    }
  }
  if (!std::feof(input.get()) || std::ferror(input.get()) || readBytes != size || hash.hexDigest() != expected)
    return core::failure(core::ErrorCode::Conflict, "Reading resource hash or length mismatch");
#if defined(__APPLE__) || defined(__linux__)
  struct stat named{};
  if (::lstat(path.c_str(), &named) != 0 || named.st_dev != opened.st_dev || named.st_ino != opened.st_ino || !S_ISREG(named.st_mode))
    return core::failure(core::ErrorCode::Conflict, "Reading resource path changed during verification");
#endif
  const auto after = std::filesystem::last_write_time(path, error);
  if (error || before != after || !std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) || error)
    return core::failure(core::ErrorCode::Conflict, "Reading resource changed during verification");
  remaining -= size;
  return core::success();
}
}
core::Result<VerifiedJapaneseReadingResource> VerifiedJapaneseReadingResource::verify(JapaneseReadingResourceSpec spec, std::stop_token stop) {
  using Output = VerifiedJapaneseReadingResource;
  const auto fail = [](const char* message) { return core::failure<Output>(core::ErrorCode::InvalidArgument, message); };
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Reading resource verification cancelled");
  if (!spec.executable.is_absolute() || !spec.dictionaryDirectory.is_absolute() ||
      spec.executable.string().size() > 4096U || spec.dictionaryDirectory.string().size() > 4096U ||
      spec.executable.string().find('\0') != std::string::npos || spec.dictionaryDirectory.string().find('\0') != std::string::npos ||
      !hex(spec.executableSha256, 64U) || !hex(spec.engineRevision, 40U)) return fail("Invalid reading resource expectations");
  for (const auto& digest : spec.dictionarySha256) if (!hex(digest, 64U)) return fail("Invalid dictionary hash expectation");
  std::error_code error;
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(spec.executable, error)) || error)
    return fail("Reading executable path must not be a symlink");
  if (!std::filesystem::is_directory(std::filesystem::symlink_status(spec.dictionaryDirectory, error)) || error)
    return fail("Dictionary root must be a non-symlink directory");
  spec.executable = std::filesystem::canonical(spec.executable, error); if (error) return fail("Reading executable is unavailable");
  spec.dictionaryDirectory = std::filesystem::canonical(spec.dictionaryDirectory, error); if (error) return fail("Dictionary directory is unavailable");
  std::size_t entries = 0U;
  std::filesystem::directory_iterator iterator(spec.dictionaryDirectory, error), end;
  if (error) return fail("Cannot inspect dictionary directory");
  for (; iterator != end; iterator.increment(error)) {
    if (error) return fail("Cannot enumerate dictionary directory");
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Reading resource verification cancelled");
    if (++entries > 4U) return fail("Dictionary contains unexpected files or configuration");
    const auto name = iterator->path().filename().string();
    if (std::find(kJapaneseDictionaryFiles.begin(), kJapaneseDictionaryFiles.end(), name) == kJapaneseDictionaryFiles.end())
      return fail("Dictionary contains unexpected files or configuration");
  }
  if (error || entries != 4U) return fail("Dictionary file set is incomplete");
  std::uintmax_t remaining = 256U * 1024U * 1024U;
  const auto executable = checkFile(spec.executable, spec.executableSha256, 64U * 1024U * 1024U, remaining, stop);
  if (!executable) return core::Result<Output>{executable.error()};
  core::Sha256 dictionaryIdentity; dictionaryIdentity.update("seam-japanese-dictionary-v1\n");
  for (std::size_t i = 0U; i < 4U; ++i) {
    const auto checked = checkFile(spec.dictionaryDirectory / kJapaneseDictionaryFiles[i], spec.dictionarySha256[i],
        128U * 1024U * 1024U, remaining, stop);
    if (!checked) return core::Result<Output>{checked.error()};
    dictionaryIdentity.update(kJapaneseDictionaryFiles[i]); dictionaryIdentity.update(":");
    dictionaryIdentity.update(spec.dictionarySha256[i]); dictionaryIdentity.update("\n");
  }
  const auto identity = phonemizer::JapaneseReadingIdentity{spec.engineRevision, dictionaryIdentity.hexDigest(), spec.executableSha256};
  return Output{std::move(spec), identity};
}
core::Result<void> VerifiedJapaneseReadingResource::revalidate(std::stop_token stop) const {
  const auto current = verify(spec_, stop);
  if (!current) return core::Result<void>{current.error()};
  if (current.value().identity() != identity_) return core::failure(core::ErrorCode::Conflict, "Reading resource identity changed");
  return core::success();
}
}
