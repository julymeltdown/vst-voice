#include "seam/authoring/japanese_reading_stage.hpp"
#include "seam/core/sha256.hpp"
#include <array>
#include <algorithm>
#include <cerrno>
#include <optional>
#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdlib>
#endif

namespace seam::authoring {
struct StagedJapaneseReadingResource::Owner {
  std::optional<VerifiedJapaneseReadingResource> verified;
#if defined(__APPLE__) || defined(__linux__)
  std::string directory;
  bool created{false};
  int root{-1}, dictionary{-1};
  ~Owner() {
    // Descriptor-relative cleanup removes only the five owned files; no
    // recursive deletion or following a substituted directory symlink.
    if (dictionary >= 0) {
      ::fchmod(dictionary, 0700);
      for (const auto* name : kJapaneseDictionaryFiles) ::unlinkat(dictionary, name, 0);
      ::close(dictionary);
    }
    if (root >= 0) {
      ::fchmod(root, 0700); ::unlinkat(root, "reader", 0); ::unlinkat(root, "dictionary", AT_REMOVEDIR);
      struct stat held{}, named{};
      if (::fstat(root, &held) == 0 && ::lstat(directory.c_str(), &named) == 0 &&
          S_ISDIR(named.st_mode) && held.st_dev == named.st_dev && held.st_ino == named.st_ino) ::rmdir(directory.c_str());
      ::close(root);
    } else if (created) ::rmdir(directory.c_str()); // Only our newly created empty directory.
  }
#endif
};
namespace {
#if defined(__APPLE__) || defined(__linux__)
struct File { int fd{-1}; ~File() { if (fd >= 0) ::close(fd); } };
core::Result<void> copyVerified(const std::filesystem::path& source, int directory, const char* name,
    std::string_view expected, std::size_t maximum, std::size_t& remaining, mode_t mode, std::stop_token stop) {
  maximum = std::min(maximum, remaining);
  File input{::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
  struct stat info{};
  if (input.fd < 0 || ::fstat(input.fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0 ||
      static_cast<std::uintmax_t>(info.st_size) > maximum)
    return core::failure(core::ErrorCode::InvalidArgument, "Cannot stage a bounded regular reading resource");
  File output{::openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600)};
  if (output.fd < 0) return core::failure(core::ErrorCode::IoError, "Cannot exclusively create staged reading resource");
  std::array<char, 65536U> buffer{}; core::Sha256 digest; std::size_t total = 0U;
  for (;;) {
    if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Reading resource staging cancelled");
    const auto count = ::read(input.fd, buffer.data(), buffer.size());
    if (count == 0) break;
    if (count < 0) { if (errno == EINTR) continue; return core::failure(core::ErrorCode::IoError, "Cannot read staging source"); }
    const auto bytes = static_cast<std::size_t>(count);
    if (bytes > maximum - total) return core::failure(core::ErrorCode::InvalidArgument, "Staging source grew beyond bounds");
    total += bytes; digest.update(std::string_view(buffer.data(), bytes));
    std::size_t written = 0U;
    while (written < bytes) {
      if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Reading resource staging cancelled");
      const auto amount = ::write(output.fd, buffer.data() + written, bytes - written);
      if (amount < 0 && errno == EINTR) continue;
      if (amount <= 0) return core::failure(core::ErrorCode::IoError, "Cannot write staged resource");
      written += static_cast<std::size_t>(amount);
    }
  }
  if (total != static_cast<std::uintmax_t>(info.st_size) || digest.hexDigest() != expected)
    return core::failure(core::ErrorCode::Conflict, "Staging source does not match its verified identity");
  if (::fchmod(output.fd, mode) != 0) return core::failure(core::ErrorCode::IoError, "Cannot seal staged resource permissions");
  remaining -= total;
  return core::success();
}
#endif
}
const VerifiedJapaneseReadingResource& StagedJapaneseReadingResource::resource() const noexcept { return *owner_->verified; }
core::Result<StagedJapaneseReadingResource> StagedJapaneseReadingResource::prepare(
    const VerifiedJapaneseReadingResource& source, const std::filesystem::path& parent, std::stop_token stop) {
  using Output = StagedJapaneseReadingResource;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Reading resource staging cancelled");
  const auto current = source.revalidate(stop); if (!current) return core::Result<Output>{current.error()};
  if (!parent.is_absolute() || parent.string().size() > 3800U || parent.string().find('\0') != std::string::npos)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Staging parent must be an absolute bounded application path");
#if defined(__APPLE__) || defined(__linux__)
  std::error_code error; const auto canonical = std::filesystem::canonical(parent, error);
  if (error || !std::filesystem::is_directory(canonical, error) || error) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Staging parent is unavailable");
  auto owner = std::make_shared<Owner>(); owner->directory = (canonical / "seam-reading-XXXXXX").string();
  if (!::mkdtemp(owner->directory.data())) return core::failure<Output>(core::ErrorCode::IoError, "Cannot create private reading staging directory");
  owner->created = true;
  owner->root = ::open(owner->directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (owner->root < 0 || ::mkdirat(owner->root, "dictionary", 0700) != 0)
    return core::failure<Output>(core::ErrorCode::IoError, "Cannot create private staged dictionary");
  owner->dictionary = ::openat(owner->root, "dictionary", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (owner->dictionary < 0) return core::failure<Output>(core::ErrorCode::IoError, "Cannot open private staged dictionary");
  auto spec = source.spec();
  std::size_t remaining = 256U * 1024U * 1024U;
  const auto executable = copyVerified(spec.executable, owner->root, "reader", spec.executableSha256, 64U * 1024U * 1024U, remaining, 0500, stop);
  if (!executable) return core::Result<Output>{executable.error()};
  for (std::size_t i = 0; i < 4U; ++i) {
    const auto copied = copyVerified(spec.dictionaryDirectory / kJapaneseDictionaryFiles[i], owner->dictionary,
        kJapaneseDictionaryFiles[i], spec.dictionarySha256[i], 128U * 1024U * 1024U, remaining, 0400, stop);
    if (!copied) return core::Result<Output>{copied.error()};
  }
  spec.executable = std::filesystem::path(owner->directory) / "reader";
  spec.dictionaryDirectory = std::filesystem::path(owner->directory) / "dictionary";
  auto verified = VerifiedJapaneseReadingResource::verify(std::move(spec), stop);
  if (!verified) return core::Result<Output>{verified.error()};
  if (verified.value().identity() != source.identity()) return core::failure<Output>(core::ErrorCode::Conflict, "Staged reading identity differs");
  if (::fchmod(owner->dictionary, 0500) != 0 || ::fchmod(owner->root, 0500) != 0)
    return core::failure<Output>(core::ErrorCode::IoError, "Cannot seal staging directory permissions");
  owner->verified.emplace(std::move(verified.value()));
  return Output{std::move(owner)};
#else
  return core::failure<Output>(core::ErrorCode::Unsupported, "Private reading staging is not implemented on this platform");
#endif
}
}
