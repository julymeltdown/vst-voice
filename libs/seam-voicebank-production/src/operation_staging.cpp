#include "operation_staging_internal.hpp"

#include "seam/core/sha256.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/voicebank_production/project_codec.hpp"

#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <limits>
#include <span>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seam::voicebank_production {
namespace {

constexpr std::uint64_t kDescriptorLimit = 64U * 1024U;

bool safeStagingId(const std::string& value) {
  return !value.empty() && value.size() <= 128U && value != "." && value != ".." &&
      std::all_of(value.begin(), value.end(), [](unsigned char item) {
        return (item >= 'a' && item <= 'z') || (item >= 'A' && item <= 'Z') ||
            (item >= '0' && item <= '9') || item == '-' || item == '_' || item == '.';
      });
}

// Each field is length-prefixed: embedded newlines or delimiters cannot change
// the meaning. Float bits avoid std::to_string's six-decimal precision loss.
std::string descriptorFor(const std::filesystem::path& root, const StagedOperation& staged) {
  std::string text{"SEAM-STAGED-OPERATION\n1\n"};
  const auto field = [&text](std::string_view name, std::string_view value) {
    text.append(name).append(":").append(std::to_string(value.size())).append(":").append(value).append("\n");
  };
  field("workspace", root.generic_string());
  field("stagingId", staged.stagingId);
  field("filename", staged.stagingId + ".wav");
  field("operationVersion", "seam-pcm-ops-1");
  field("operation", toString(staged.request.kind));
  field("channelIndex", std::to_string(staged.request.channelIndex));
  field("targetSampleRate", std::to_string(staged.request.targetSampleRate));
  field("targetPeakBits", std::to_string(std::bit_cast<std::uint32_t>(staged.request.targetPeak)));
  field("startFrame", std::to_string(staged.request.startFrame));
  field("endFrame", std::to_string(staged.request.endFrame));
  for (const auto& [key, value] : operationParameters(staged.request)) {
    field("parameterName", key);
    field("parameterValue", value);
  }
  field("inputSha256", staged.inputSha256);
  field("outputSha256", staged.outputSha256);
  field("takeId", staged.takeId);
  field("parentRevisionId", staged.parentRevisionId);
  field("sourceProjectSha256", staged.sourceProjectSha256);
  return text;
}

class NativeFile final {
public:
  NativeFile() = default;
  NativeFile(const NativeFile&) = delete;
  NativeFile& operator=(const NativeFile&) = delete;
  ~NativeFile() { close(); }
  void close() {
#if defined(_WIN32)
    if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
    value = INVALID_HANDLE_VALUE;
#else
    if (value >= 0) ::close(value);
    value = -1;
#endif
  }
  [[nodiscard]] bool valid() const {
#if defined(_WIN32)
    return value != INVALID_HANDLE_VALUE;
#else
    return value >= 0;
#endif
  }
#if defined(_WIN32)
  HANDLE value{INVALID_HANDLE_VALUE};
#else
  int value{-1};
#endif
};

// POSIX operations are relative to opened, no-follow directory descriptors;
// replacing the visible staging parent cannot redirect a write elsewhere.
// Windows keeps non-reparse directory handles open without delete sharing.
class StageDirectory final {
public:
  core::Result<void> open(const std::filesystem::path& root, bool create) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(root, error);
    if (error || !std::filesystem::is_directory(status) || std::filesystem::is_symlink(status))
      return core::failure(core::ErrorCode::InvalidArgument, "Staging workspace must be a real directory");
    rootPath_ = std::filesystem::canonical(root, error);
    if (error) return core::failure(core::ErrorCode::IoError, "Unable to resolve staging workspace");
    stagePath_ = rootPath_ / "staging";
#if defined(_WIN32)
    root_.value = CreateFileW(rootPath_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (!realDirectory(root_)) return core::failure(core::ErrorCode::InvalidArgument, "Unsafe staging workspace");
    if (create && !CreateDirectoryW(stagePath_.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
      return core::failure(core::ErrorCode::IoError, "Unable to create staging directory");
    directory_.value = CreateFileW(stagePath_.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (!realDirectory(directory_)) return core::failure(core::ErrorCode::InvalidArgument, "Staging parent is not a real directory");
#else
    root_.value = ::open(rootPath_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (!root_.valid()) return core::failure(core::ErrorCode::IoError, "Unable to open staging workspace");
    if (create && mkdirat(root_.value, "staging", 0700) != 0 && errno != EEXIST)
      return core::failure(core::ErrorCode::IoError, "Unable to create staging directory");
    directory_.value = openat(root_.value, "staging", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (!directory_.valid()) return core::failure(core::ErrorCode::InvalidArgument, "Staging parent is not a real directory");
    if (create && fsync(root_.value) != 0)
      return core::failure(core::ErrorCode::IoError, "Unable to synchronize staging parent");
#endif
    return attached();
  }

  [[nodiscard]] const std::filesystem::path& rootPath() const { return rootPath_; }

  core::Result<void> attached() const {
#if defined(_WIN32)
    if (!realDirectory(root_) || !realDirectory(directory_))
      return core::failure(core::ErrorCode::Conflict, "Staging directories changed");
#else
    struct stat rootOpened{}, rootVisible{}, opened{}, visible{};
    if (fstat(root_.value, &rootOpened) != 0 || lstat(rootPath_.c_str(), &rootVisible) != 0 ||
        !S_ISDIR(rootVisible.st_mode) || rootOpened.st_dev != rootVisible.st_dev || rootOpened.st_ino != rootVisible.st_ino ||
        fstat(directory_.value, &opened) != 0 || fstatat(root_.value, "staging", &visible, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISDIR(visible.st_mode) || opened.st_dev != visible.st_dev || opened.st_ino != visible.st_ino)
      return core::failure(core::ErrorCode::Conflict, "Staging directories changed; provisional files are preserved");
#endif
    return core::success();
  }

  core::Result<void> lock(const std::string& id, bool create) {
    const auto name = "." + id + ".lock";
#if defined(_WIN32)
    lock_.value = CreateFileW((stagePath_ / name).c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, create ? OPEN_ALWAYS : OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (!regularFile(lock_)) return core::failure(core::ErrorCode::Conflict, "Staging identifier is busy or unsafe");
#else
    lock_.value = openat(directory_.value, name.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK |
        (create ? O_CREAT : 0), 0600);
    if (!regularFile(lock_) || flock(lock_.value, LOCK_EX | LOCK_NB) != 0)
      return core::failure(core::ErrorCode::Conflict, "Staging identifier is busy or unsafe");
#endif
    return core::success();
  }

  core::Result<void> vacant(const std::string& name) const {
#if defined(_WIN32)
    const auto attributes = GetFileAttributesW((stagePath_ / name).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES)
      return core::failure(core::ErrorCode::Conflict, "Staging output already exists", name);
    if (GetLastError() != ERROR_FILE_NOT_FOUND)
      return core::failure(core::ErrorCode::IoError, "Unable to inspect staging output", name);
#else
    struct stat status{};
    if (fstatat(directory_.value, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) == 0)
      return core::failure(core::ErrorCode::Conflict, "Staging output already exists", name);
    if (errno != ENOENT) return core::failure(core::ErrorCode::IoError, "Unable to inspect staging output", name);
#endif
    return core::success();
  }

  core::Result<void> publish(const std::string& name, std::span<const std::byte> bytes) const {
    // Pending names are deliberately not reused or erased after failure. A
    // crashed attempt never authorizes overwriting a previous attempt's bytes.
    const auto pending = "." + name + ".pending";
    NativeFile file;
#if defined(_WIN32)
    file.value = CreateFileW((stagePath_ / pending).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
#else
    file.value = openat(directory_.value, pending.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
#endif
    if (!file.valid()) return core::failure(core::ErrorCode::Conflict, "Private staging output is occupied or unavailable", pending);
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
      const auto count = std::min<std::size_t>(bytes.size() - offset, 1024U * 1024U);
#if defined(_WIN32)
      DWORD written = 0U;
      if (!WriteFile(file.value, bytes.data() + offset, static_cast<DWORD>(count), &written, nullptr) || written == 0U)
        return core::failure(core::ErrorCode::IoError, "Unable to write private staged output", pending);
      offset += written;
#else
      const auto written = ::write(file.value, bytes.data() + offset, count);
      if (written < 0 && errno == EINTR) continue;
      if (written <= 0) return core::failure(core::ErrorCode::IoError, "Unable to write private staged output", pending);
      offset += static_cast<std::size_t>(written);
#endif
    }
#if defined(_WIN32)
    if (!FlushFileBuffers(file.value)) return core::failure(core::ErrorCode::IoError, "Unable to synchronize staged output", pending);
    file.close();
    if (!SetFileAttributesW((stagePath_ / pending).c_str(), FILE_ATTRIBUTE_READONLY) ||
        !MoveFileExW((stagePath_ / pending).c_str(), (stagePath_ / name).c_str(), MOVEFILE_WRITE_THROUGH))
      return core::failure(core::ErrorCode::Conflict, "Unable to publish new staged output; pending bytes are preserved", name);
#else
    if (fchmod(file.value, 0400) != 0 || fsync(file.value) != 0)
      return core::failure(core::ErrorCode::IoError, "Unable to synchronize staged output", pending);
    if (linkat(directory_.value, pending.c_str(), directory_.value, name.c_str(), 0) != 0)
      return core::failure(core::ErrorCode::Conflict, "Unable to publish new staged output; pending bytes are preserved", name);
    if (unlinkat(directory_.value, pending.c_str(), 0) != 0 || fsync(directory_.value) != 0)
      return core::failure(core::ErrorCode::IoError, "Staged output publication durability is uncertain; inspect before retrying", name);
#endif
    return attached();
  }

  core::Result<std::vector<std::byte>> read(const std::string& name, std::uint64_t limit) const {
    NativeFile file;
    std::uint64_t size = 0U;
#if defined(_WIN32)
    file.value = CreateFileW((stagePath_ / name).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    LARGE_INTEGER length{};
    if (!regularFile(file) || !GetFileSizeEx(file.value, &length) || length.QuadPart < 0)
      return core::failure<std::vector<std::byte>>(core::ErrorCode::InvalidArgument, "Staged file is missing or unsafe", name);
    size = static_cast<std::uint64_t>(length.QuadPart);
#else
    file.value = openat(directory_.value, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat info{};
    if (!regularFile(file) || fstat(file.value, &info) != 0 || info.st_size < 0)
      return core::failure<std::vector<std::byte>>(core::ErrorCode::InvalidArgument, "Staged file is missing or unsafe", name);
    size = static_cast<std::uint64_t>(info.st_size);
#endif
    if (size > limit || size > std::numeric_limits<std::size_t>::max())
      return core::failure<std::vector<std::byte>>(core::ErrorCode::InvalidArgument, "Staged file exceeds its bounded format", name);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::size_t offset = 0U;
    while (offset < bytes.size()) {
      const auto count = std::min<std::size_t>(bytes.size() - offset, 1024U * 1024U);
#if defined(_WIN32)
      DWORD received = 0U;
      if (!ReadFile(file.value, bytes.data() + offset, static_cast<DWORD>(count), &received, nullptr) || received == 0U)
        return core::failure<std::vector<std::byte>>(core::ErrorCode::IoError, "Unable to read exact staged bytes", name);
      offset += received;
#else
      const auto received = ::read(file.value, bytes.data() + offset, count);
      if (received < 0 && errno == EINTR) continue;
      if (received <= 0) return core::failure<std::vector<std::byte>>(core::ErrorCode::IoError, "Unable to read exact staged bytes", name);
      offset += static_cast<std::size_t>(received);
#endif
    }
    // Reject growth as well as the shrink detected by the read loop.
    std::byte extra{};
#if defined(_WIN32)
    DWORD received = 0U;
    if (!ReadFile(file.value, &extra, 1U, &received, nullptr) || received != 0U)
#else
    ssize_t received = 0;
    do { received = ::read(file.value, &extra, 1U); } while (received < 0 && errno == EINTR);
    if (received != 0)
#endif
      return core::failure<std::vector<std::byte>>(core::ErrorCode::Conflict, "Staged file changed while reading", name);
    return bytes;
  }

private:
#if defined(_WIN32)
  static bool realDirectory(const NativeFile& file) {
    BY_HANDLE_FILE_INFORMATION info{};
    return file.valid() && GetFileInformationByHandle(file.value, &info) &&
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
  }
#endif
  static bool regularFile(const NativeFile& file) {
#if defined(_WIN32)
    BY_HANDLE_FILE_INFORMATION info{};
    return file.valid() && GetFileInformationByHandle(file.value, &info) && info.nNumberOfLinks == 1U &&
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0;
#else
    struct stat info{};
    return file.valid() && fstat(file.value, &info) == 0 && S_ISREG(info.st_mode) && info.st_nlink == 1;
#endif
  }
  std::filesystem::path rootPath_;
  std::filesystem::path stagePath_;
  NativeFile root_;
  NativeFile directory_;
  NativeFile lock_;  // Persistent inode: never unlink an interprocess lock.
};

// Same PCM24 wire encoding as WavStreamWriter, but in bounded memory so that
// all filesystem writes use an exclusive file handle, never an ofstream that
// follows a pathname and truncates it. Tests compare bytes with that writer.
core::Result<std::vector<std::byte>> encodeStagedPcm24(const voicebank::AudioBuffer& audio) {
  if (audio.sampleRate < 8000U || audio.sampleRate > 384000U || audio.channels == 0U || audio.channels > 8U ||
      audio.interleaved.empty() || audio.interleaved.size() % audio.channels != 0U ||
      audio.interleaved.size() > (voicebank::kMaximumSupportedWavBytes - 44U) / 3U)
    return core::failure<std::vector<std::byte>>(core::ErrorCode::InvalidArgument, "Staged PCM24 output format or size is invalid");
  const auto dataBytes = static_cast<std::uint32_t>(audio.interleaved.size() * 3U);
  std::vector<std::byte> bytes;
  bytes.reserve(44U + dataBytes);
  const auto word = [&bytes](std::uint32_t value, unsigned int width) {
    for (unsigned int index = 0U; index < width; ++index)
      bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
  };
  const auto literal = [&bytes](std::string_view value) {
    const auto raw = std::as_bytes(std::span{value.data(), value.size()});
    bytes.insert(bytes.end(), raw.begin(), raw.end());
  };
  literal("RIFF"); word(36U + dataBytes, 4U); literal("WAVEfmt "); word(16U, 4U);
  word(1U, 2U); word(audio.channels, 2U); word(audio.sampleRate, 4U);
  word(audio.sampleRate * audio.channels * 3U, 4U); word(audio.channels * 3U, 2U); word(24U, 2U);
  literal("data"); word(dataBytes, 4U);
  for (const auto sample : audio.interleaved) {
    const auto clamped = std::clamp(std::isfinite(sample) ? sample : 0.0F, -1.0F, 1.0F);
    const auto scaled = static_cast<std::int32_t>(std::lround(static_cast<double>(clamped) *
        (clamped < 0.0F ? 8388608.0 : 8388607.0)));
    word(static_cast<std::uint32_t>(scaled), 3U);
  }
  return bytes;
}

}  // namespace

core::Result<void> validateStagedOperation(const std::filesystem::path& root, const StagedOperation& staged) {
  if (!safeStagingId(staged.stagingId) || toString(staged.request.kind).empty() ||
      staged.path.lexically_normal() != (root / "staging" / (staged.stagingId + ".wav")).lexically_normal())
    return core::failure(core::ErrorCode::InvalidArgument, "Staged operation path or identity is invalid");
  StageDirectory directory;
  auto opened = directory.open(root, false);
  if (!opened) return opened;
  auto locked = directory.lock(staged.stagingId, false);
  if (!locked) return locked;
  const auto expected = descriptorFor(directory.rootPath(), staged);
  if (expected.size() > kDescriptorLimit)
    return core::failure(core::ErrorCode::InvalidArgument, "Staged operation descriptor exceeds its format limit");
  const auto descriptor = directory.read(staged.stagingId + ".operation", kDescriptorLimit);
  if (!descriptor) return core::Result<void>{descriptor.error()};
  const auto expectedBytes = std::as_bytes(std::span{expected.data(), expected.size()});
  if (!std::ranges::equal(descriptor.value(), expectedBytes))
    return core::failure(core::ErrorCode::Conflict, "Staged operation differs from its durable execution descriptor");
  const auto audio = directory.read(staged.stagingId + ".wav", voicebank::kMaximumSupportedWavBytes);
  if (!audio) return core::Result<void>{audio.error()};
  if (core::sha256Hex(audio.value()) != staged.outputSha256)
    return core::failure(core::ErrorCode::InvariantViolation, "Staged output differs from its durable execution digest");
  return directory.attached();
}

core::Result<StagedOperation> ProductionProjectRepository::stageOperation(
    const VoicebankProductionProject& project, std::string_view takeId,
    std::string_view expectedParentRevisionId, const OperationRequest& request, std::string stagingId) const {
  if (!safeStagingId(stagingId))
    return core::failure<StagedOperation>(core::ErrorCode::InvalidArgument, "Staging identifier is unsafe");
  const auto durable = recover();
  if (!durable) return core::Result<StagedOperation>{durable.error()};
  const auto projectText = encodeProductionProject(project);
  if (encodeProductionProject(durable.value()) != projectText)
    return core::failure<StagedOperation>(core::ErrorCode::Conflict, "Operation staging requires the exact current durable project");
  const auto permitted = requireTakeSourceExecution(project, takeId);
  if (!permitted) return core::Result<StagedOperation>{permitted.error()};
  const auto take = std::find_if(project.takes.begin(), project.takes.end(),
      [takeId](const auto& value) { return value.takeId == takeId; });
  if (take == project.takes.end())
    return core::failure<StagedOperation>(core::ErrorCode::NotFound, "Operation staging target is unavailable");
  const std::string_view parent = take->derivedRevisionIds.empty() ? std::string_view{} : take->derivedRevisionIds.back();
  if (parent != expectedParentRevisionId)
    return core::failure<StagedOperation>(core::ErrorCode::Conflict, "Operation staging parent is stale");
  auto inputDigest = take->rawAssetSha256;
  for (const auto& id : take->derivedRevisionIds) {
    const auto revision = std::find_if(project.derivedRevisions.begin(), project.derivedRevisions.end(),
        [&id](const auto& value) { return value.revisionId == id; });
    if (revision == project.derivedRevisions.end() || revision->inputSha256 != inputDigest)
      return core::failure<StagedOperation>(core::ErrorCode::Conflict, "Operation staging lineage is incomplete");
    inputDigest = revision->outputSha256;
  }
  const auto input = std::find_if(project.assets.begin(), project.assets.end(),
      [&inputDigest](const auto& value) { return value.sha256 == inputDigest; });
  if (input == project.assets.end())
    return core::failure<StagedOperation>(core::ErrorCode::NotFound, "Operation staging audio is unavailable");
  const auto verified = assetStore_.verify(*input);
  if (!verified) return core::Result<StagedOperation>{verified.error()};

  StageDirectory directory;
  const auto opened = directory.open(root_, true);
  if (!opened) return core::Result<StagedOperation>{opened.error()};
  const auto locked = directory.lock(stagingId, true);
  if (!locked) return core::Result<StagedOperation>{locked.error()};
  for (const auto& name : {stagingId + ".wav", stagingId + ".operation",
                         "." + stagingId + ".wav.pending", "." + stagingId + ".operation.pending"}) {
    const auto vacant = directory.vacant(name);
    if (!vacant) return core::Result<StagedOperation>{vacant.error()};
  }
  const auto audio = voicebank::readWav(assetStore_.pathFor(*input));
  if (!audio) return core::Result<StagedOperation>{audio.error()};
  // Bound a resample before DSP allocation, not merely after WAV encoding.
  if (request.kind == OperationKind::Resample &&
      (request.targetSampleRate < 8000U || request.targetSampleRate > 384000U ||
       std::ceil(static_cast<double>(audio.value().frameCount()) * request.targetSampleRate /
           audio.value().sampleRate) * audio.value().channels >
           static_cast<double>((voicebank::kMaximumSupportedWavBytes - 44U) / 3U)))
    return core::failure<StagedOperation>(core::ErrorCode::InvalidArgument, "Staged resample exceeds the supported WAV format");
  const auto processed = applyOperation(audio.value(), request);
  if (!processed) return core::Result<StagedOperation>{processed.error()};
  const auto bytes = encodeStagedPcm24(processed.value());
  if (!bytes) return core::Result<StagedOperation>{bytes.error()};
  StagedOperation staged{.stagingId = std::move(stagingId), .inputSha256 = input->sha256,
      .outputSha256 = core::sha256Hex(bytes.value()), .path = {}, .request = request,
      .takeId = std::string{takeId}, .parentRevisionId = std::string{expectedParentRevisionId},
      .sourceProjectSha256 = core::sha256Hex(projectText)};
  staged.path = root_ / "staging" / (staged.stagingId + ".wav");
  const auto descriptor = descriptorFor(directory.rootPath(), staged);
  if (descriptor.size() > kDescriptorLimit)
    return core::failure<StagedOperation>(core::ErrorCode::InvalidArgument, "Staged operation descriptor exceeds its format limit");
  const auto written = directory.publish(staged.stagingId + ".wav", bytes.value());
  if (!written) return core::Result<StagedOperation>{written.error()};
  const auto published = directory.read(staged.stagingId + ".wav", voicebank::kMaximumSupportedWavBytes);
  if (!published) return core::Result<StagedOperation>{published.error()};
  if (core::sha256Hex(published.value()) != staged.outputSha256)
    return core::failure<StagedOperation>(core::ErrorCode::Conflict, "Published staging bytes differ from the executed operation");
  // Descriptor publication is the completion marker. A WAV without this exact
  // durable record remains an inspectable orphan, never a committable operation.
  const auto recorded = directory.publish(staged.stagingId + ".operation",
      std::as_bytes(std::span{descriptor.data(), descriptor.size()}));
  if (!recorded) return core::Result<StagedOperation>{recorded.error()};
  const auto reopenedDescriptor = directory.read(staged.stagingId + ".operation", kDescriptorLimit);
  if (!reopenedDescriptor) return core::Result<StagedOperation>{reopenedDescriptor.error()};
  if (!std::ranges::equal(reopenedDescriptor.value(), std::as_bytes(std::span{descriptor.data(), descriptor.size()})))
    return core::failure<StagedOperation>(core::ErrorCode::Conflict, "Published execution descriptor changed");
  const auto attached = directory.attached();
  if (!attached) return core::Result<StagedOperation>{attached.error()};
  return staged;
}

}  // namespace seam::voicebank_production
