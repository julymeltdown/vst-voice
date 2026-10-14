#include "procedural_continuity_commands.hpp"
#include "seam/build/version.hpp"
#include "seam/core/sha256.hpp"
#include "seam/rendering/project_renderer.hpp"
#include "seam/rendering/render_pipeline.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cmath>
#include <map>
#include <memory>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace seam::voicebank_cli {
namespace {
using Json = formats::JsonValue;
using Marker = voice_design::ProceduralPhoneMarker;
constexpr std::uint64_t kMaximumFrames = 1920000U;
constexpr std::size_t kMaximumChunks = 64U;
constexpr std::uint64_t kMaximumPrefixFrames = 64000000U;

Json markerJson(const Marker& marker) {
  return Json::Object{{"key", marker.key.toString()}, {"phone", marker.phone},
      {"startFrame", marker.ownedSpan.start}, {"endFrame", marker.ownedSpan.end},
      {"startClipped", marker.startClipped}, {"endClipped", marker.endClipped},
      {"gestureKindNativeEnum", static_cast<std::int64_t>(marker.kind)}, {"palatalized", marker.palatalized}};
}
Json::Array markersJson(std::span<const Marker> markers) {
  Json::Array result;
  for (const auto& marker : markers) result.push_back(markerJson(marker));
  return result;
}
bool mergeMatches(std::span<const Marker> whole, const std::vector<std::vector<Marker>>& parts) {
  std::map<domain::PhonemeKey, Marker> merged, expected;
  for (const auto& marker : whole) if (!expected.emplace(marker.key, marker).second) return false;
  for (const auto& part : parts) for (const auto& marker : part) {
    const auto [it, added] = merged.emplace(marker.key, marker);
    if (!added) {
      auto& previous = it->second;
      if (previous.phone != marker.phone || previous.kind != marker.kind ||
          previous.palatalized != marker.palatalized || previous.ownedSpan.end != marker.ownedSpan.start ||
          !previous.endClipped || !marker.startClipped) return false;
      previous.ownedSpan.end = marker.ownedSpan.end; previous.endClipped = marker.endClipped;
    }
  }
  return merged == expected;
}
struct Difference final {
  double maximum{0.0}; std::int64_t differentWords{0};
};
core::Result<Difference> compare(std::span<const float> expected, std::span<const float> actual) {
  if (expected.size() != actual.size()) return core::failure<Difference>(core::ErrorCode::Conflict,
      "Continuity PCM frame counts differ");
  Difference result;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    if (!std::isfinite(actual[i]) || !std::isfinite(expected[i])) return core::failure<Difference>(
        core::ErrorCode::InvalidArgument, "Continuity PCM is non-finite");
    result.maximum = std::max(result.maximum, std::abs(static_cast<double>(expected[i]) - actual[i]));
    result.differentWords += std::bit_cast<std::uint32_t>(expected[i]) != std::bit_cast<std::uint32_t>(actual[i]);
  }
  return result;
}
// Preserve every finite float word, including signed zero and values outside [-1,1].
// The ordinary PCM24 writer's rounding/clamping is deliberately outside this measurement.
std::vector<std::byte> floatWav(std::span<const float> samples, std::uint32_t rate) {
  std::vector<std::byte> bytes;
  bytes.reserve(44U + samples.size() * 4U);
  const auto text = [&](std::string_view value) { for (const char c : value) bytes.push_back(static_cast<std::byte>(c)); };
  const auto word = [&](std::uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<std::byte>((value >> (8U * i)) & 255U));
  };
  text("RIFF"); word(static_cast<std::uint32_t>(36U + samples.size() * 4U), 4); text("WAVEfmt ");
  word(16, 4); word(3, 2); word(1, 2); word(rate, 4); word(rate * 4U, 4); word(4, 2); word(32, 2);
  text("data"); word(static_cast<std::uint32_t>(samples.size() * 4U), 4);
  for (const float sample : samples) word(std::bit_cast<std::uint32_t>(sample), 4);
  return bytes;
}
#if !defined(_WIN32)
struct Fd final {
  int value{-1}; explicit Fd(int fd) : value(fd) {} ~Fd() { if (value >= 0) ::close(value); }
  Fd(const Fd&) = delete; Fd& operator=(const Fd&) = delete;
};
bool sameNode(const struct stat& a, const struct stat& b) { return a.st_dev == b.st_dev && a.st_ino == b.st_ino; }
bool sameFileState(const struct stat& a, const struct stat& b) {
#if defined(__APPLE__)
  const auto am = a.st_mtimespec, bm = b.st_mtimespec, ac = a.st_ctimespec, bc = b.st_ctimespec;
#else
  const auto am = a.st_mtim, bm = b.st_mtim, ac = a.st_ctim, bc = b.st_ctim;
#endif
  return sameNode(a, b) && S_ISREG(a.st_mode) && S_ISREG(b.st_mode) && a.st_size == b.st_size &&
      am.tv_sec == bm.tv_sec && am.tv_nsec == bm.tv_nsec && ac.tv_sec == bc.tv_sec && ac.tv_nsec == bc.tv_nsec;
}
// Flat files through a held directory. A refused write leaves diagnostic partial
// outputs; no complete observation is printed and a retry needs a new directory.
class OutputRoot final {
public:
  static core::Result<std::unique_ptr<OutputRoot>> create(const std::filesystem::path& destination) {
    std::error_code error;
    const auto parent = std::filesystem::canonical(destination.parent_path(), error);
    if (error || destination.filename().empty()) return core::failure<std::unique_ptr<OutputRoot>>(
        core::ErrorCode::InvalidArgument, "Continuity output requires an existing parent and a new directory");
    auto result = std::unique_ptr<OutputRoot>(new OutputRoot(parent / destination.filename()));
    result->parent_ = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (result->parent_ < 0 || ::mkdirat(result->parent_, result->path_.filename().c_str(), 0700) != 0)
      return core::failure<std::unique_ptr<OutputRoot>>(core::ErrorCode::Conflict,
          "Continuity output directory must be new; existing entries are preserved");
    result->root_ = ::openat(result->parent_, result->path_.filename().c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (result->root_ < 0 || ::fstat(result->root_, &result->identity_) != 0 || !result->unchanged())
      return core::failure<std::unique_ptr<OutputRoot>>(core::ErrorCode::Conflict, "Continuity output directory changed");
    return result;
  }
  ~OutputRoot() { if (root_ >= 0) ::close(root_); if (parent_ >= 0) ::close(parent_); }
  core::Result<void> write(std::string_view name, std::span<const std::byte> bytes, std::stop_token stop) {
    if (!unchanged() || stop.stop_requested()) return core::failure(core::ErrorCode::Conflict,
        "Continuity output changed or cancelled; partial diagnostic files may remain");
    const std::string leaf{name};
    Fd fd{::openat(root_, leaf.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600)};
    if (fd.value < 0) return core::failure(core::ErrorCode::Conflict, "Continuity output file already exists or cannot be created", leaf);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Continuity output cancelled; partial files retained");
      const auto count = ::write(fd.value, bytes.data() + offset, std::min<std::size_t>(65536U, bytes.size() - offset));
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) return core::failure(core::ErrorCode::IoError, "Unable to write continuity output", leaf);
      offset += static_cast<std::size_t>(count);
    }
    if (::fsync(fd.value) != 0) return core::failure(core::ErrorCode::IoError, "Cannot sync continuity output", leaf);
    struct stat before{}, after{}, named{};
    if (::fstat(fd.value, &before) != 0 || before.st_size != static_cast<off_t>(bytes.size()))
      return core::failure(core::ErrorCode::Conflict, "Continuity output size changed", leaf);
    core::Sha256 observed; std::array<std::byte, 65536U> buffer{}; offset = 0;
    while (offset < bytes.size()) {
      const auto count = ::pread(fd.value, buffer.data(), std::min(buffer.size(), bytes.size() - offset), static_cast<off_t>(offset));
      if (count < 0 && errno == EINTR) continue;
      if (count <= 0) return core::failure(core::ErrorCode::IoError, "Cannot observe continuity output", leaf);
      observed.update(std::span{buffer.data(), static_cast<std::size_t>(count)}); offset += static_cast<std::size_t>(count);
    }
    if (::fstat(fd.value, &after) != 0 || ::fstatat(root_, leaf.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
        !sameFileState(before, after) || !sameFileState(after, named) ||
        observed.hexDigest() != core::sha256Hex(bytes) || !unchanged())
      return core::failure(core::ErrorCode::Conflict, "Continuity output changed before observation", leaf);
    files_.emplace(leaf, OwnedFile{after, observed.hexDigest()});
    return core::success();
  }
  core::Result<void> finish() {
    for (const auto& [name, expected] : files_) {
      Fd fd{::openat(root_, name.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK)};
      struct stat opened{}, after{}, named{};
      if (fd.value < 0 || ::fstat(fd.value, &opened) != 0 || !sameFileState(expected.identity, opened))
        return core::failure(core::ErrorCode::Conflict, "Continuity owned output was replaced before completion", name);
      core::Sha256 digest; std::array<std::byte, 65536U> buffer{}; off_t offset = 0;
      while (offset < opened.st_size) {
        const auto count = ::pread(fd.value, buffer.data(), static_cast<std::size_t>(std::min<off_t>(static_cast<off_t>(buffer.size()), opened.st_size - offset)), offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return core::failure(core::ErrorCode::IoError, "Cannot re-observe continuity output", name);
        digest.update(std::span{buffer.data(), static_cast<std::size_t>(count)}); offset += count;
      }
      if (::fstat(fd.value, &after) != 0 || ::fstatat(root_, name.c_str(), &named, AT_SYMLINK_NOFOLLOW) != 0 ||
          !sameFileState(opened, after) || !sameFileState(after, named) || digest.hexDigest() != expected.sha256)
        return core::failure(core::ErrorCode::Conflict, "Continuity owned output changed before completion", name);
    }
    if (::fsync(root_) != 0 || ::fsync(parent_) != 0) return core::failure(core::ErrorCode::IoError, "Cannot sync continuity output directory");
    return unchanged() ? core::success() : core::failure(core::ErrorCode::Conflict, "Continuity output directory changed before completion");
  }
private:
  explicit OutputRoot(std::filesystem::path path) : path_(std::move(path)) {}
  bool unchanged() const {
    struct stat named{}, lexical{};
    return root_ >= 0 && ::fstatat(parent_, path_.filename().c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
        ::lstat(path_.c_str(), &lexical) == 0 && S_ISDIR(named.st_mode) &&
        sameNode(identity_, named) && sameNode(identity_, lexical);
  }
  std::filesystem::path path_; int parent_{-1}, root_{-1}; struct stat identity_{};
  struct OwnedFile { struct stat identity; std::string sha256; };
  std::map<std::string, OwnedFile> files_;
};
#endif
}

core::Result<Json> observeInstalledProceduralContinuity(const CapturedProjectBinding& captured,
    const candidate_packaging::VerifiedInstalledCandidate& installed, const Json& installedObservation,
    const std::filesystem::path& installationDirectory, const std::filesystem::path& destination,
    std::uint32_t maximumChunkFrames, std::stop_token stop) {
#if defined(_WIN32)
  (void)captured; (void)installed; (void)installedObservation; (void)installationDirectory;
  (void)destination; (void)maximumChunkFrames; (void)stop;
  return core::failure<Json>(core::ErrorCode::Unsupported, "Continuity observer requires a verified POSIX target; Windows remains TODO");
#else
  const auto refuse = [](std::string message) { return core::failure<Json>(core::ErrorCode::Conflict, std::move(message)); };
  if (installed.kind != voicebank_production::ResourceCandidateKind::Recipe || !installed.proceduralInstallation)
    return refuse("Continuity observation requires a verified installed recipe; classical crops are not stateful chunks");
  if (maximumChunkFrames == 0U || maximumChunkFrames > kMaximumFrames) return refuse("Continuity chunk frames must be within 1..1920000");
  const auto& project = captured.project();
  const auto* track = project.findVocalTrack(captured.trackId());
  const auto* region = project.findRegion(captured.regionId());
  if (!track || !region || !track->proceduralRecipe || region->notes.size() > 256U)
    return refuse("Continuity observation requires one bounded saved procedural region");
  const auto rate = project.settings().sampleRate;
  if (!std::isfinite(rate) || rate != std::floor(rate) || rate < 8000 || rate > 192000)
    return refuse("Continuity observation requires an integer sample rate within 8000..192000");
  const auto& reference = *track->proceduralRecipe;
  if (track->neuralResource || reference.resource != installed.projectResource || reference.installation != installed.proceduralInstallation)
    return refuse("Continuity saved selection differs from the verified installation");
  std::error_code error;
  const auto root = std::filesystem::canonical(installationDirectory, error);
  if (error) return refuse("Continuity installation directory is unavailable");
  const auto output = std::filesystem::weakly_canonical(destination, error);
  if (error) return refuse("Continuity output parent is unavailable");
  const auto status = std::filesystem::symlink_status(output, error);
  if (error && error != std::errc::no_such_file_or_directory) return refuse("Cannot inspect continuity output destination");
  if (!error && status.type() != std::filesystem::file_type::not_found)
    return refuse("Continuity output directory must be new; existing entries are preserved");
  error.clear();
  const auto inside = output.lexically_relative(root);
  if (!inside.empty() && !inside.is_absolute() && *inside.begin() != "..") return refuse("Continuity output must not modify the installation");
  const auto expected = std::filesystem::canonical(root / installed.proceduralInstallation->recipeEntry, error);
  if (error) return refuse("Verified recipe path is unavailable");
  const auto actual = std::filesystem::canonical(std::filesystem::path{reference.path}.is_absolute() ?
      std::filesystem::path{reference.path} : captured.projectDirectory() / reference.path, error);
  if (error || actual != expected) return refuse("Continuity saved path does not name the verified recipe");
  const auto frozen = rendering::captureProceduralSource(*track,
      rendering::TrackRecipeFileSource{track->id, reference, captured.projectDirectory()}, stop);
  if (!frozen) return core::Result<Json>{frozen.error()};
  if (!frozen.value().admission || frozen.value().admission->origin() != distribution::ProceduralAdmissionOrigin::Installed)
    return refuse("Continuity requires installed procedural admission");
  const auto snapshot = rendering::RenderSnapshotFactory{}.createProcedural(project, frozen.value().resource,
      track->id, region->id, 0U, rendering::RenderQuality::Final, static_cast<std::uint32_t>(rate), frozen.value().style);
  if (!snapshot) return core::Result<Json>{snapshot.error()};
  const auto context = snapshot.value().compiledPerformance->phoneticContext();
  if (!context) return core::Result<Json>{context.error()};
  if (context.value().end - context.value().start > static_cast<time::SampleFrame>(kMaximumFrames))
    return refuse("Continuity region exceeds 1920000 frames");
  const auto chunks = rendering::RenderSnapshotFactory{}.splitOwnedOutput(snapshot.value(), context.value(), maximumChunkFrames, kMaximumChunks);
  if (!chunks) return core::Result<Json>{chunks.error()};
  if (chunks.value().size() < 2U) return refuse("Continuity observation requires at least two forced chunks");
  std::uint64_t prefixBudget = 0;
  for (const auto& chunk : chunks.value()) prefixBudget += static_cast<std::uint64_t>(chunk.ownedFrames->end - context.value().start);
  if (prefixBudget > kMaximumPrefixFrames) return refuse("Continuity restarted-prefix work exceeds 64000000 frames");
  const auto whole = rendering::PhraseRenderPipeline{}.render(snapshot.value(), stop);
  if (!whole) return core::Result<Json>{whole.error()};
  const auto& wholeAudio = whole.value().rendered.audio;
  if (wholeAudio.startFrame != context.value().start || wholeAudio.samples.size() != static_cast<std::size_t>(context.value().end - context.value().start) ||
      whole.value().proceduralMarkers.size() > 128U) return refuse("Continuity whole result exceeds its declared context or marker budget");
  auto resumed = rendering::ProceduralSnapshotStream::create(snapshot.value(), stop);
  if (!resumed) return core::Result<Json>{resumed.error()};
  std::vector<std::vector<float>> restartedPcm, resumedPcm;
  std::vector<std::vector<Marker>> restartedMarkers, resumedMarkers;
  std::vector<float> restartedJoined, resumedJoined;
  Json::Array windows, markerParts, boundaries;
  for (std::size_t index = 0; index < chunks.value().size(); ++index) {
    const auto& chunk = chunks.value()[index]; const auto window = *chunk.ownedFrames;
    if (chunk.compiledPerformance != snapshot.value().compiledPerformance) return refuse("Continuity chunks lost the single immutable compiled plan");
    const auto restarted = rendering::PhraseRenderPipeline{}.render(chunk, stop);
    if (!restarted) return core::Result<Json>{restarted.error()};
    const auto checkpointBefore = resumed.value().position();
    const auto continued = resumed.value().render(chunk, stop);
    if (!continued) return core::Result<Json>{continued.error()};
    const auto& a = restarted.value().rendered.audio; const auto& b = continued.value().audio;
    if (a.startFrame != window.start || b.startFrame != window.start || resumed.value().position() != window.end)
      return refuse("Continuity chunk frame origin or checkpoint differs from owned output");
    const auto expectedPcm = std::span{wholeAudio.samples}.subspan(static_cast<std::size_t>(window.start - context.value().start), static_cast<std::size_t>(window.end - window.start));
    const auto da = compare(expectedPcm, a.samples), db = compare(expectedPcm, b.samples);
    if (!da) return core::Result<Json>{da.error()};
    if (!db) return core::Result<Json>{db.error()};
    windows.emplace_back(Json::Object{{"index", static_cast<std::int64_t>(index)}, {"startFrame", window.start}, {"endFrame", window.end},
        {"chunkContentSha256", chunk.contentHash}, {"sharedCompiledContextSha256", snapshot.value().contentHash},
        {"checkpointBefore", checkpointBefore}, {"checkpointAfter", resumed.value().position()},
        {"restartedMaxAbsDifference", da.value().maximum}, {"resumedMaxAbsDifference", db.value().maximum},
        {"restartedDifferentFloat32Words", da.value().differentWords}, {"resumedDifferentFloat32Words", db.value().differentWords}});
    markerParts.emplace_back(Json::Object{{"index", static_cast<std::int64_t>(index)},
        {"restarted", markersJson(restarted.value().proceduralMarkers)}, {"resumed", markersJson(continued.value().markers)}});
    if (index + 1U < chunks.value().size()) {
      Json::Array insidePhones;
      for (const auto& marker : whole.value().proceduralMarkers) if (marker.ownedSpan.start < window.end && window.end < marker.ownedSpan.end)
        insidePhones.push_back(markerJson(marker));
      boundaries.emplace_back(Json::Object{{"frame", window.end}, {"insidePlannedPhones", std::move(insidePhones)}});
    }
    restartedJoined.insert(restartedJoined.end(), a.samples.begin(), a.samples.end());
    resumedJoined.insert(resumedJoined.end(), b.samples.begin(), b.samples.end());
    restartedPcm.push_back(a.samples); resumedPcm.push_back(b.samples);
    restartedMarkers.push_back(restarted.value().proceduralMarkers); resumedMarkers.push_back(continued.value().markers);
  }
  const auto da = compare(wholeAudio.samples, restartedJoined), db = compare(wholeAudio.samples, resumedJoined);
  if (!da) return core::Result<Json>{da.error()};
  if (!db) return core::Result<Json>{db.error()};
  auto owned = OutputRoot::create(output); if (!owned) return core::Result<Json>{owned.error()};
  Json::Array artifacts;
  const auto saveWav = [&](std::string name, std::span<const float> samples) -> core::Result<void> {
    const auto bytes = floatWav(samples, snapshot.value().sampleRate);
    const auto written = owned.value()->write(name, bytes, stop); if (!written) return written;
    artifacts.emplace_back(Json::Object{{"path", name}, {"sha256", core::sha256Hex(bytes)},
        {"frames", static_cast<std::int64_t>(samples.size())}, {"channels", std::int64_t{1}}, {"encoding", "IEEE_FLOAT32_UNCLAMPED"}});
    return core::success();
  };
  for (const auto& [name, samples] : std::array<std::pair<std::string, std::span<const float>>, 3>{
      {{"whole.wav", wholeAudio.samples}, {"restarted-joined.wav", restartedJoined}, {"resumed-joined.wav", resumedJoined}}}) {
    const auto saved = saveWav(name, samples); if (!saved) return core::Result<Json>{saved.error()};
  }
  for (std::size_t i = 0; i < restartedPcm.size(); ++i) {
    const auto a = saveWav("restarted-" + std::to_string(i) + ".wav", restartedPcm[i]); if (!a) return core::Result<Json>{a.error()};
    const auto b = saveWav("resumed-" + std::to_string(i) + ".wav", resumedPcm[i]); if (!b) return core::Result<Json>{b.error()};
  }
  Json::Array scoreNotes;
  for (const auto& note : snapshot.value().compiledPerformance->notes()) scoreNotes.emplace_back(Json::Object{
      {"noteId", note.id.toString()}, {"startFrame", note.startFrame}, {"endFrame", note.endFrame},
      {"midiKey", static_cast<std::int64_t>(note.midiKey)}, {"articulationNativeEnum", static_cast<std::int64_t>(note.articulation)},
      {"vibratoEnabled", note.vibrato.enabled}, {"vibratoPhaseTurns", static_cast<double>(note.vibrato.phaseTurns)}});
  const Json partition{Json::Object{{"schemaVersion", std::int64_t{1}}, {"timingSource", "SHARED_COMPILED_PLAN_PROJECTION_NOT_ACOUSTIC_MEASUREMENT"},
      {"scoreNotes", std::move(scoreNotes)},
      {"wholeMarkers", markersJson(whole.value().proceduralMarkers)}, {"chunkMarkers", std::move(markerParts)}, {"boundaries", std::move(boundaries)}}};
  const auto partitionText = formats::stringifyJson(partition, true) + "\n";
  const auto partitionBytes = std::as_bytes(std::span{partitionText});
  const auto saved = owned.value()->write("partition.json", partitionBytes, stop); if (!saved) return core::Result<Json>{saved.error()};
  artifacts.emplace_back(Json::Object{{"path", "partition.json"}, {"sha256", core::sha256Hex(partitionBytes)},
      {"frames", std::int64_t{0}}, {"channels", std::int64_t{0}}, {"encoding", "JSON_DERIVED_PLAN_PROJECTION"}});
#if defined(__APPLE__)
  constexpr auto platform = "macos";
#else
  constexpr auto platform = "linux";
#endif
  const Json record{Json::Object{{"schemaVersion", std::int64_t{1}}, {"recordType", "seam.u45.installed-procedural-continuity.v1"},
      {"result", "ContinuityObserved"}, {"scope", "SELECTED_PROCEDURAL_SNAPSHOT_API"}, {"evidenceScope", "ENGINEERING_ONLY"},
      {"installed", installedObservation}, {"project", captured.record()}, {"backend", "procedural"}, {"host", "seam_voicebank_cli"},
      {"platform", platform}, {"sourceCommit", std::string{build::kSourceCommit}}, {"buildId", std::string{build::kBuildId}},
      {"executableIdentity", "EXTERNAL_PIN_REQUIRED"}, {"sampleRate", static_cast<std::int64_t>(snapshot.value().sampleRate)},
      {"startFrame", context.value().start}, {"endFrame", context.value().end}, {"chunkFrames", static_cast<std::int64_t>(maximumChunkFrames)},
      {"compiledContextSha256", snapshot.value().contentHash}, {"chunks", std::move(windows)}, {"artifacts", std::move(artifacts)},
      {"engineeringExpectation", "EXACT_FLOAT32_WORDS_EQUAL"}, {"measuredBeforeEncoder", true},
      {"restartedMaxAbsDifference", da.value().maximum}, {"resumedMaxAbsDifference", db.value().maximum},
      {"restartedDifferentFloat32Words", da.value().differentWords}, {"resumedDifferentFloat32Words", db.value().differentWords},
      {"exactFloat32WordsEqual", da.value().differentWords == 0 && db.value().differentWords == 0},
      {"restartedMergedMarkerProjectionMatches", mergeMatches(whole.value().proceduralMarkers, restartedMarkers)},
      {"resumedMergedMarkerProjectionMatches", mergeMatches(whole.value().proceduralMarkers, resumedMarkers)},
      {"acousticF0Measurement", "NOT_RUN"}, {"acousticPhaseMeasurement", "NOT_RUN"}, {"neighborInvalidation", "NOT_RUN"},
      {"schedulerExecution", "NOT_RUN_NO_CACHE_USED"}, {"standaloneAudioBlocks", "NOT_RUN"}, {"windowsExecution", "NOT_RUN"},
      {"canonicalTolerance", "UNRESOLVED"}, {"humanAcceptance", "NOT_RUN"}, {"qualification", "NOT_QUALIFIED"},
      {"authorizesRelease", false}, {"releaseEligible", false}}};
  const auto text = formats::stringifyJson(record, true) + "\n";
  if (text.size() > 64U * 1024U) return refuse("Continuity observation exceeds 64 KiB; partial diagnostic artifacts retained");
  const auto summary = owned.value()->write("observation.json", std::as_bytes(std::span{text}), stop);
  if (!summary) return core::Result<Json>{summary.error()};
  const auto finished = owned.value()->finish(); if (!finished) return core::Result<Json>{finished.error()};
  return record;
#endif
}
}
