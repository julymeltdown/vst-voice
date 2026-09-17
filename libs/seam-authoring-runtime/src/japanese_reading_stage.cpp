#include "seam/authoring/japanese_reading_stage.hpp"
#include "seam/core/sha256.hpp"
#include <array>
#include <algorithm>
#include <cerrno>
#include <optional>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#endif
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
#elif defined(_WIN32)
  std::wstring directory;
  bool created{false};
  ~Owner() {
    if (!directory.empty() && created) {
      const auto dictDir = directory + L"\\dictionary";
      for (const auto* name : kJapaneseDictionaryFiles) {
        std::wstring wname(name, name + std::char_traits<char>::length(name));
        const auto filePath = dictDir + L"\\" + wname;
        ::SetFileAttributesW(filePath.c_str(), FILE_ATTRIBUTE_NORMAL);
        ::DeleteFileW(filePath.c_str());
      }
      ::SetFileAttributesW(dictDir.c_str(), FILE_ATTRIBUTE_NORMAL);
      ::RemoveDirectoryW(dictDir.c_str());
      const auto readerPath = directory + L"\\reader";
      ::SetFileAttributesW(readerPath.c_str(), FILE_ATTRIBUTE_NORMAL);
      ::DeleteFileW(readerPath.c_str());
      ::SetFileAttributesW(directory.c_str(), FILE_ATTRIBUTE_NORMAL);
      ::RemoveDirectoryW(directory.c_str());
    }
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
#elif defined(_WIN32)
struct WindowsHandle final {
  HANDLE handle{INVALID_HANDLE_VALUE};
  ~WindowsHandle() { if (handle != INVALID_HANDLE_VALUE) ::CloseHandle(handle); }
};

core::Result<void> copyVerifiedWindows(const std::filesystem::path& source,
    const std::filesystem::path& destination, std::string_view expected,
    std::size_t maximum, std::size_t& remaining, std::stop_token stop) {
  maximum = std::min(maximum, remaining);
  WindowsHandle input{::CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (input.handle == INVALID_HANDLE_VALUE)
    return core::failure(core::ErrorCode::InvalidArgument, "Cannot stage a bounded regular reading resource");
  BY_HANDLE_FILE_INFORMATION info{};
  if (!::GetFileInformationByHandle(input.handle, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
      info.nNumberOfLinks > 1U)
    return core::failure(core::ErrorCode::InvalidArgument, "Reading resource is not a regular file");
  LARGE_INTEGER fileSize{};
  if (!::GetFileSizeEx(input.handle, &fileSize) || fileSize.QuadPart <= 0 ||
      static_cast<std::uintmax_t>(fileSize.QuadPart) > maximum)
    return core::failure(core::ErrorCode::InvalidArgument, "Cannot stage a bounded regular reading resource");

  WindowsHandle output{::CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
      FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (output.handle == INVALID_HANDLE_VALUE)
    return core::failure(core::ErrorCode::IoError, "Cannot exclusively create staged reading resource");

  std::array<char, 65536U> buffer{};
  core::Sha256 digest;
  std::size_t total = 0U;
  for (;;) {
    if (stop.stop_requested())
      return core::failure(core::ErrorCode::Conflict, "Reading resource staging cancelled");
    DWORD bytesRead = 0U;
    if (!::ReadFile(input.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
      return core::failure(core::ErrorCode::IoError, "Cannot read staging source");
    if (bytesRead == 0U) break;
    const auto bytes = static_cast<std::size_t>(bytesRead);
    if (bytes > maximum - total)
      return core::failure(core::ErrorCode::InvalidArgument, "Staging source grew beyond bounds");
    total += bytes;
    digest.update(std::string_view(buffer.data(), bytes));
    DWORD written = 0U;
    while (written < bytesRead) {
      if (stop.stop_requested())
        return core::failure(core::ErrorCode::Conflict, "Reading resource staging cancelled");
      DWORD chunk = 0U;
      if (!::WriteFile(output.handle, buffer.data() + written, bytesRead - written, &chunk, nullptr) || chunk == 0U)
        return core::failure(core::ErrorCode::IoError, "Cannot write staged resource");
      written += chunk;
    }
  }
  if (!::FlushFileBuffers(output.handle))
    return core::failure(core::ErrorCode::IoError, "Cannot flush staged resource");
  if (total != static_cast<std::uintmax_t>(fileSize.QuadPart) || digest.hexDigest() != expected)
    return core::failure(core::ErrorCode::Conflict, "Staging source does not match its verified identity");
  ::SetFileAttributesW(destination.c_str(), FILE_ATTRIBUTE_READONLY);
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
#if defined(_WIN32)
  std::error_code error;
  const auto canonical = std::filesystem::canonical(parent, error);
  if (error || !std::filesystem::is_directory(canonical, error))
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Staging parent is unavailable");
  static std::atomic<std::uint64_t> counter{0U};
  const auto uniqueName = L"seam-reading-" + std::to_wstring(::GetCurrentProcessId()) + L"-" +
                          std::to_wstring(counter.fetch_add(1U, std::memory_order_relaxed));
  auto owner = std::make_shared<Owner>();
  const auto stageDir = canonical / uniqueName;
  if (!::CreateDirectoryW(stageDir.c_str(), nullptr))
    return core::failure<Output>(core::ErrorCode::IoError, "Cannot create private reading staging directory");
  owner->created = true;
  owner->directory = stageDir.wstring();
  const auto dictDir = stageDir / "dictionary";
  if (!::CreateDirectoryW(dictDir.c_str(), nullptr))
    return core::failure<Output>(core::ErrorCode::IoError, "Cannot create private staged dictionary");

  auto spec = source.spec();
  std::size_t remaining = 256U * 1024U * 1024U;
  const auto executable = copyVerifiedWindows(spec.executable, stageDir / "reader",
      spec.executableSha256, 64U * 1024U * 1024U, remaining, stop);
  if (!executable) return core::Result<Output>{executable.error()};
  for (std::size_t i = 0; i < 4U; ++i) {
    const auto copied = copyVerifiedWindows(spec.dictionaryDirectory / kJapaneseDictionaryFiles[i],
        dictDir / kJapaneseDictionaryFiles[i], spec.dictionarySha256[i], 128U * 1024U * 1024U, remaining, stop);
    if (!copied) return core::Result<Output>{copied.error()};
  }
  spec.executable = stageDir / "reader";
  spec.dictionaryDirectory = dictDir;
  auto verified = VerifiedJapaneseReadingResource::verify(std::move(spec), stop);
  if (!verified) return core::Result<Output>{verified.error()};
  if (verified.value().identity() != source.identity())
    return core::failure<Output>(core::ErrorCode::Conflict, "Staged reading identity differs");
  ::SetFileAttributesW(dictDir.c_str(), FILE_ATTRIBUTE_READONLY);
  ::SetFileAttributesW(stageDir.c_str(), FILE_ATTRIBUTE_READONLY);
  owner->verified.emplace(std::move(verified.value()));
  return Output{std::move(owner)};
#elif defined(__APPLE__) || defined(__linux__)
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
