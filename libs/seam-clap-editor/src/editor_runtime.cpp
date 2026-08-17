#include "seam/clap_editor/editor_runtime.hpp"

#include "human_vowel_data.hpp"

#include "seam/application/render_commands.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/rendering/region_renderer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <utility>

namespace seam::clap_editor {
namespace {

constexpr std::uint32_t kSourceSampleRate = asset::kSampleRate;
constexpr std::size_t kSourceFrames = asset::kFrameCount;
constexpr double kSourceRootMidi = 67.0;
constexpr std::array<char, 8> kStateMagic{'S', 'E', 'A', 'M', 'E', 'D', '1', '1'};
constexpr std::uint32_t kStateVersion = 1U;
constexpr std::size_t kDigestBytes = 32U;
constexpr std::size_t kMaximumStateBytes = 16U * 1024U * 1024U;


std::filesystem::path previewCacheRoot() {
  if (const auto* configured = std::getenv("SEAM_PREVIEW_CACHE_ROOT");
      configured != nullptr && *configured != '\0') {
    return std::filesystem::path{configured};
  }
#if defined(_WIN32)
  if (const auto* local = std::getenv("LOCALAPPDATA");
      local != nullptr && *local != '\0') {
    return std::filesystem::path{local} / "ProjectSEAM" / "Cache" /
           "PluginPreview";
  }
#elif defined(__APPLE__)
  if (const auto* home = std::getenv("HOME");
      home != nullptr && *home != '\0') {
    return std::filesystem::path{home} / "Library" / "Caches" /
           "ProjectSEAM" / "PluginPreview";
  }
#else
  if (const auto* xdg = std::getenv("XDG_CACHE_HOME");
      xdg != nullptr && *xdg != '\0') {
    return std::filesystem::path{xdg} / "project-seam" / "plugin-preview";
  }
  if (const auto* home = std::getenv("HOME");
      home != nullptr && *home != '\0') {
    return std::filesystem::path{home} / ".cache" / "project-seam" /
           "plugin-preview";
  }
#endif
  std::error_code error;
  auto root = std::filesystem::temp_directory_path(error);
  if (error) root = std::filesystem::current_path(error);
  return root / "project-seam" / "plugin-preview";
}

PreviewStatus previewStatusFor(
    voicebank::VoicebankResolveStatus status) noexcept {
  switch (status) {
    case voicebank::VoicebankResolveStatus::Resolved:
      return PreviewStatus::Ready;
    case voicebank::VoicebankResolveStatus::VersionMismatch:
      return PreviewStatus::VoicebankVersionMismatch;
    case voicebank::VoicebankResolveStatus::ContentHashMissing:
      return PreviewStatus::VoicebankContentHashMissing;
    case voicebank::VoicebankResolveStatus::ContentMismatch:
      return PreviewStatus::VoicebankContentMismatch;
    case voicebank::VoicebankResolveStatus::Untrusted:
      return PreviewStatus::VoicebankUntrusted;
    case voicebank::VoicebankResolveStatus::Missing:
    case voicebank::VoicebankResolveStatus::InvalidReference:
      return PreviewStatus::VoicebankMissing;
  }
  return PreviewStatus::Failed;
}

std::vector<voicebank::VoicebankSearchRoot> runtimeVoicebankRoots(
    std::vector<voicebank::VoicebankSearchRoot> roots) {
  auto defaults = voicebank::defaultVoicebankSearchRoots();
  roots.insert(roots.end(), defaults.begin(), defaults.end());
#ifdef SEAM_SOURCE_PRODUCTION_VOICEBANK
  const auto sourceFixture = std::filesystem::path{SEAM_SOURCE_PRODUCTION_VOICEBANK};
  if (!sourceFixture.empty()) {
    roots.push_back(voicebank::VoicebankSearchRoot{
        .path = sourceFixture,
        .kind = voicebank::VoicebankRootKind::Development,
    });
  }
#endif
  std::vector<voicebank::VoicebankSearchRoot> unique;
  for (auto& root : roots) {
    if (root.path.empty()) continue;
    root.path = root.path.lexically_normal();
    const auto duplicate = std::find_if(
        unique.begin(), unique.end(), [&root](const auto& candidate) {
          return candidate.path == root.path && candidate.kind == root.kind;
        });
    if (duplicate == unique.end()) unique.push_back(std::move(root));
  }
  return unique;
}

float sampleAt(double position) noexcept {
  if (asset::kFrameCount < 2U) return 0.0F;
  const auto loopStart = std::min(asset::kLoopStart, asset::kFrameCount - 2U);
  const auto loopEnd = std::max(loopStart + 2U,
                                std::min(asset::kLoopEnd, asset::kFrameCount));
  if (!std::isfinite(position) || position < 0.0) position = 0.0;
  if (position >= static_cast<double>(loopEnd)) {
    const auto loopLength = static_cast<double>(loopEnd - loopStart);
    position = static_cast<double>(loopStart) +
               std::fmod(position - static_cast<double>(loopStart), loopLength);
  }
  const auto first = std::min(static_cast<std::size_t>(position),
                              asset::kFrameCount - 1U);
  const auto second = std::min(first + 1U, asset::kFrameCount - 1U);
  const auto fraction = static_cast<float>(position - static_cast<double>(first));
  const auto left = static_cast<float>(asset::kPcm[first]) / 32768.0F;
  const auto right = static_cast<float>(asset::kPcm[second]) / 32768.0F;
  return left + (right - left) * fraction;
}

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

std::string voicebankStatusLabel(
    const voicebank::VoicebankResolution& resolution) {
  if (resolution.resolved() && resolution.candidate.has_value()) {
    return "BANK " + resolution.candidate->manifest.displayName + " [" +
           std::string{voicebank::voicebankTrustName(
               resolution.candidate->trust)} + "]";
  }
  return "BANK " + resolution.diagnostic;
}

std::optional<domain::PhonemeKey> primaryPhonemeKey(
    const application::EditorSession& session,
    domain::RegionId regionId) {
  const auto* region = session.project().findRegion(regionId);
  if (region == nullptr || region->notes.empty()) return std::nullopt;
  const auto selected = session.selection().noteIds();
  const auto noteId = selected.empty() ? region->notes.front().id : selected.front();
  return domain::PhonemeKey{.noteId = noteId, .ordinal = 0U};
}

}  // namespace


std::string_view previewStatusName(PreviewStatus status) noexcept {
  switch (status) {
    case PreviewStatus::Empty: return "empty";
    case PreviewStatus::Ready: return "ready";
    case PreviewStatus::VoicebankMissing: return "voicebank-missing";
    case PreviewStatus::VoicebankVersionMismatch: return "voicebank-version-mismatch";
    case PreviewStatus::VoicebankContentHashMissing: return "voicebank-content-hash-missing";
    case PreviewStatus::VoicebankContentMismatch: return "voicebank-content-mismatch";
    case PreviewStatus::VoicebankUntrusted: return "voicebank-untrusted";
    case PreviewStatus::Failed: return "failed";
  }
  return "unknown";
}


RealtimePreviewPublication::ReadHandle::ReadHandle(
    const RealtimePreviewPublication* owner, std::size_t slot,
    const RenderedPreview* value) noexcept
    : owner_(owner), slot_(slot), value_(value) {}

RealtimePreviewPublication::ReadHandle::ReadHandle(ReadHandle&& other) noexcept
    : owner_(other.owner_), slot_(other.slot_), value_(other.value_) {
  other.owner_ = nullptr;
  other.value_ = nullptr;
}

RealtimePreviewPublication::ReadHandle&
RealtimePreviewPublication::ReadHandle::operator=(ReadHandle&& other) noexcept {
  if (this == &other) return *this;
  release();
  owner_ = other.owner_;
  slot_ = other.slot_;
  value_ = other.value_;
  other.owner_ = nullptr;
  other.value_ = nullptr;
  return *this;
}

RealtimePreviewPublication::ReadHandle::~ReadHandle() { release(); }

void RealtimePreviewPublication::ReadHandle::release() noexcept {
  if (owner_ != nullptr) {
    owner_->slots_[slot_].readers.fetch_sub(1U, std::memory_order_release);
  }
  owner_ = nullptr;
  value_ = nullptr;
}

RealtimePreviewPublication::RealtimePreviewPublication() {
  slots_[0].preview = RenderedPreview{};
}

RealtimePreviewPublication::ReadHandle
RealtimePreviewPublication::acquire() const noexcept {
  for (;;) {
    const auto slot = published_.load(std::memory_order_acquire);
    slots_[slot].readers.fetch_add(1U, std::memory_order_acquire);
    if (slot == published_.load(std::memory_order_acquire)) {
      return ReadHandle{this, slot, &slots_[slot].preview};
    }
    slots_[slot].readers.fetch_sub(1U, std::memory_order_release);
  }
}

bool RealtimePreviewPublication::publish(RenderedPreview preview) {
  std::scoped_lock lock(writerMutex_);
  const auto current = published_.load(std::memory_order_acquire);
  for (std::size_t offset = 1U; offset < kSlotCount; ++offset) {
    const auto candidate = (current + offset) % kSlotCount;
    if (slots_[candidate].readers.load(std::memory_order_acquire) != 0U) {
      continue;
    }
    slots_[candidate].preview = std::move(preview);
    published_.store(candidate, std::memory_order_release);
    return true;
  }
  return false;
}

AsyncPreviewRenderService::AsyncPreviewRenderService()
    : cache_(std::make_unique<rendering::PcmCache>(previewCacheRoot())),
      worker_([this](std::stop_token stopToken) { workerLoop(stopToken); }) {}

AsyncPreviewRenderService::~AsyncPreviewRenderService() {
  {
    std::lock_guard lock(mutex_);
    activeStopSource_.request_stop();
  }
  worker_.request_stop();
  condition_.notify_all();
}

void AsyncPreviewRenderService::submit(
    domain::Project project, domain::TrackId trackId,
    domain::RegionId regionId, voicebank::VoicebankResolution resolution,
    std::uint64_t revision, std::uint32_t sampleRate) {
  sampleRate = std::clamp(sampleRate, 8000U, 192000U);
  latestSubmittedRevision_.store(revision, std::memory_order_release);
  {
    std::lock_guard lock(mutex_);
    activeStopSource_.request_stop();
    if (pending_.has_value()) {
      cancelled_.fetch_add(1U, std::memory_order_relaxed);
    }
    pending_ = Request{.project = std::move(project),
                       .trackId = trackId,
                       .regionId = regionId,
                       .resolution = std::move(resolution),
                       .revision = revision,
                       .sampleRate = sampleRate};
  }
  submitted_.fetch_add(1U, std::memory_order_relaxed);
  condition_.notify_all();
}

void AsyncPreviewRenderService::setCompletionCallback(
    std::function<void()> callback) {
  std::lock_guard lock(callbackMutex_);
  completionCallback_ = std::move(callback);
}

std::shared_ptr<const RenderedPreview> AsyncPreviewRenderService::latest() const {
  auto handle = published_.acquire();
  return handle ? std::make_shared<RenderedPreview>(*handle)
                : std::make_shared<RenderedPreview>();
}

RenderServiceStats AsyncPreviewRenderService::stats() const noexcept {
  return RenderServiceStats{
      .submitted = submitted_.load(std::memory_order_relaxed),
      .completed = completed_.load(std::memory_order_relaxed),
      .cancelled = cancelled_.load(std::memory_order_relaxed),
      .stale = stale_.load(std::memory_order_relaxed),
  };
}

void AsyncPreviewRenderService::workerLoop(std::stop_token stopToken) {
  while (!stopToken.stop_requested()) {
    Request request;
    std::stop_token requestToken;
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, stopToken,
                      [this] { return pending_.has_value(); });
      if (stopToken.stop_requested()) break;
      request = std::move(*pending_);
      pending_.reset();
      activeStopSource_ = std::stop_source{};
      requestToken = activeStopSource_.get_token();
    }
    auto rendered = render(request, requestToken);
    if (!rendered || stopToken.stop_requested()) {
      if (requestToken.stop_requested()) {
        cancelled_.fetch_add(1U, std::memory_order_relaxed);
      }
      continue;
    }
    if (request.revision !=
        latestSubmittedRevision_.load(std::memory_order_acquire)) {
      stale_.fetch_add(1U, std::memory_order_relaxed);
      continue;
    }
    if (!published_.publish(std::move(*rendered))) {
      stale_.fetch_add(1U, std::memory_order_relaxed);
      continue;
    }
    completed_.fetch_add(1U, std::memory_order_relaxed);
    std::function<void()> callback;
    {
      std::lock_guard lock(callbackMutex_);
      callback = completionCallback_;
    }
    if (callback) callback();
  }
}

std::shared_ptr<RenderedPreview> AsyncPreviewRenderService::render(
    const Request& request, std::stop_token stopToken) {
  auto output = std::make_shared<RenderedPreview>();
  output->sampleRate = request.sampleRate;
  output->revision = request.revision;
  output->status = previewStatusFor(request.resolution.status);
  output->diagnostic = request.resolution.diagnostic;
  if (!request.resolution.resolved()) return output;

  const auto& candidate = *request.resolution.candidate;
  output->voicebankId = candidate.manifest.id;
  output->voicebankVersion = candidate.manifest.version;
  output->voicebankContentHash = candidate.contentHash;

  rendering::ProductionRegionRenderer renderer;
  auto rendered = renderer.render(
      request.project, candidate.manifest, candidate.bankRoot,
      request.trackId, request.regionId, request.revision,
      request.sampleRate, rendering::RenderQuality::Preview,
      candidate.manifest.styles.empty() ? std::string{}
                                        : candidate.manifest.styles.front(),
      synthesis::PhraseRenderOptions{}, cache_.get(), stopToken);
  if (!rendered) {
    if (rendered.error().code == core::ErrorCode::Conflict &&
        stopToken.stop_requested()) {
      return {};
    }
    output->status = PreviewStatus::Failed;
    output->diagnostic = rendered.error().message;
    if (!rendered.error().context.empty()) {
      output->diagnostic += ": " + rendered.error().context;
    }
    return output;
  }

  output->status = PreviewStatus::Ready;
  output->diagnostic = "Production pipeline render completed";
  output->phraseCount = rendered.value().phrases.size();
  output->unitPlan = rendered.value().unitPlan;
  output->unitCount = rendered.value().unitCount;
  output->fallbackCount = rendered.value().fallbackCount;
  output->cacheHits = rendered.value().cacheHits;
  output->phraseContentHashes.reserve(rendered.value().phrases.size());
  for (const auto& phrase : rendered.value().phrases) {
    output->phraseContentHashes.push_back(phrase.contentHash);
  }
  output->stereo.resize(rendered.value().mono.size() * 2U);
  for (std::size_t index = 0U; index < rendered.value().mono.size(); ++index) {
    const auto sample = rendered.value().mono[index];
    output->stereo[index * 2U] = sample;
    output->stereo[index * 2U + 1U] = sample;
  }
  return output;
}

void LiveSampleInstrument::reset() noexcept {
  voices_ = {};
}

void LiveSampleInstrument::setOutputSampleRate(double sampleRate) noexcept {
  if (std::isfinite(sampleRate) && sampleRate >= 8000.0 &&
      sampleRate <= 192000.0) {
    outputSampleRate_ = sampleRate;
  }
}

LiveVoice* LiveSampleInstrument::allocateVoice() noexcept {
  const auto free = std::find_if(voices_.begin(), voices_.end(),
                                 [](const LiveVoice& voice) {
                                   return !voice.active;
                                 });
  if (free != voices_.end()) return &*free;
  return &*std::min_element(voices_.begin(), voices_.end(),
                            [](const LiveVoice& lhs, const LiveVoice& rhs) {
                              return lhs.envelope < rhs.envelope;
                            });
}

void LiveSampleInstrument::noteOn(std::int32_t noteId, std::int16_t key,
                                  float velocity) noexcept {
  auto* voice = allocateVoice();
  *voice = LiveVoice{
      .active = true,
      .releasing = false,
      .noteId = noteId,
      .key = key,
      .samplePosition = 0.0,
      .pitchRatio =
          std::pow(2.0, (static_cast<double>(key) - kSourceRootMidi) / 12.0) *
          static_cast<double>(kSourceSampleRate) / outputSampleRate_,
      .envelope = 0.0F,
      .velocity = std::clamp(velocity, 0.0F, 1.0F),
  };
}

void LiveSampleInstrument::noteOff(std::int32_t noteId,
                                   std::int16_t key) noexcept {
  for (auto& voice : voices_) {
    if (!voice.active) continue;
    if ((noteId >= 0 && voice.noteId == noteId) ||
        (noteId < 0 && voice.key == key)) {
      voice.releasing = true;
    }
  }
}

void LiveSampleInstrument::choke(std::int32_t noteId,
                                 std::int16_t key) noexcept {
  for (auto& voice : voices_) {
    if (!voice.active) continue;
    if ((noteId >= 0 && voice.noteId == noteId) ||
        (noteId < 0 && voice.key == key)) {
      voice = LiveVoice{};
    }
  }
}

float LiveSampleInstrument::renderSample() noexcept {
  float mixed = 0.0F;
  for (auto& voice : voices_) {
    if (!voice.active) continue;
    if (voice.releasing) {
      voice.envelope = std::max(0.0F, voice.envelope - 0.0018F);
    } else {
      voice.envelope = std::min(1.0F, voice.envelope + 0.0045F);
    }
    if (voice.releasing && voice.envelope <= 0.0F) {
      voice.active = false;
      continue;
    }
    mixed += sampleAt(voice.samplePosition) * voice.envelope *
             voice.velocity * 0.35F;
    voice.samplePosition += voice.pitchRatio;
  }
  return std::clamp(mixed, -1.0F, 1.0F);
}

std::size_t LiveSampleInstrument::activeVoiceCount() const noexcept {
  return static_cast<std::size_t>(std::count_if(
      voices_.begin(), voices_.end(),
      [](const LiveVoice& voice) { return voice.active; }));
}

domain::Project EditorRuntime::makeDefaultProject(
    application::ProjectFactory& factory, domain::RegionId& regionId) {
  auto project = factory.createProject("SEAM / CLAP EDITOR");
  static_cast<void>(project.tempoMap().addOrReplace(time::Tick{0}, 154.0));
  project.settings().characterDisplay = domain::CharacterDisplayMode::Minimal;
  const auto trackId = factory.addVocalTrack(project, "VOICE 01");
  regionId = factory.addRegion(project, trackId, "DAW PHRASE",
                               time::Tick{0}, time::Tick{15360});
  auto* region = project.findRegion(regionId);
  if (region == nullptr) return project;

  const std::array<std::tuple<std::int64_t, std::int64_t, std::uint8_t,
                              const char32_t*>, 8>
      notes{{
          {0, 720, 64U, U"こ"},
          {720, 480, 67U, U"え"},
          {1200, 960, 69U, U"を"},
          {2400, 480, 67U, U"つ"},
          {2880, 720, 64U, U"な"},
          {3600, 960, 62U, U"ぐ"},
          {4800, 720, 64U, U"ま"},
          {5520, 1440, 67U, U"で"},
      }};
  for (const auto& [start, duration, key, lyricText] : notes) {
    auto [lyric, note] = factory.makeNote(
        time::Tick{start}, time::Tick{duration}, key,
        std::u32string{lyricText}, domain::Language::Japanese);
    region->lyrics.push_back(std::move(lyric));
    region->notes.push_back(std::move(note));
  }
  region->sortNotes();
  const std::array<std::tuple<std::uint16_t, const char*,
                              domain::UnitRendererKind>, 8>
      unitOverrides{{
          {2U, "demo.ja.g4.k-o.01", domain::UnitRendererKind::ClassicPsola},
          {1U, "demo.ja.g4.e.01", domain::UnitRendererKind::Raw},
          {1U, "demo.ja.g4.o.01", domain::UnitRendererKind::SpectralClassic},
          {2U, "demo.ja.g4.ts-u.01", domain::UnitRendererKind::Stretch},
          {2U, "demo.ja.g4.n-a.01", domain::UnitRendererKind::ClassicPsola},
          {2U, "demo.ja.g4.g-u.01", domain::UnitRendererKind::Raw},
          {2U, "demo.ja.g4.m-a.01", domain::UnitRendererKind::SpectralClassic},
          {2U, "demo.ja.g4.d-e.01", domain::UnitRendererKind::Stretch},
      }};
  for (std::size_t index = 0U; index < region->notes.size(); ++index) {
    const auto& [tokenCount, unitId, renderer] = unitOverrides[index];
    region->unitSelectionOverrides.push_back(domain::UnitSelectionOverride{
        .startKey = domain::PhonemeKey{
            .noteId = region->notes[index].id, .ordinal = 0U},
        .tokenCount = tokenCount,
        .unitId = unitId,
        .renderer = renderer,
        .locked = true,
    });
  }
  if (!region->notes.empty()) {
    region->seamOverrides.push_back(domain::SeamOverride{
        .incomingStartKey = domain::PhonemeKey{
            .noteId = region->notes.front().id, .ordinal = 0U},
        .seamAmount = 0.55F,
        .overlap = time::Microseconds{9000},
        .phaseReset = 0.65F,
        .envelopeBlend = 0.20F,
        .curve = domain::SeamCurve::HardCharacter,
        .locked = true,
    });
  }
  static_cast<void>(region->pitchAutomation.upsert(
      domain::PitchAutomationPoint{.tick = time::Tick{0},
                                   .cents = -12.0F,
                                   .interpolation =
                                       domain::CurveInterpolation::Smooth}));
  static_cast<void>(region->pitchAutomation.upsert(
      domain::PitchAutomationPoint{.tick = time::Tick{2400},
                                   .cents = 38.0F,
                                   .interpolation =
                                       domain::CurveInterpolation::Linear}));
  return project;
}

domain::RegionId EditorRuntime::firstRegionId(
    const domain::Project& project) noexcept {
  for (const auto& track : project.vocalTracks()) {
    if (!track.regions.empty()) return track.regions.front().id;
  }
  return {};
}

domain::TrackId EditorRuntime::firstTrackId(
    const domain::Project& project) noexcept {
  return project.vocalTracks().empty() ? domain::TrackId{}
                                       : project.vocalTracks().front().id;
}

EditorRuntime::EditorRuntime(
    std::optional<domain::Project> project,
    const std::filesystem::path& characterPackage,
    std::vector<voicebank::VoicebankSearchRoot> voicebankRoots)
    : createdDefault_(!project.has_value()),
      session_([&] {
        if (project.has_value()) {
          trackId_ = firstTrackId(*project);
          regionId_ = firstRegionId(*project);
          factory_.synchronizeWith(*project);
          return std::move(*project);
        }
        auto created = makeDefaultProject(factory_, regionId_);
        trackId_ = firstTrackId(created);
        return created;
      }()),
      voicebankRoots_(runtimeVoicebankRoots(std::move(voicebankRoots))) {
  auto scanned = voicebankCatalog_.scan(voicebankRoots_);
  if (scanned) {
    voicebanks_ = std::move(scanned).value();
  } else {
    voicebankResolution_.status = voicebank::VoicebankResolveStatus::Missing;
    voicebankResolution_.diagnostic = scanned.error().message;
  }
  if (createdDefault_) {
    const auto demo = std::find_if(
        voicebanks_.begin(), voicebanks_.end(), [](const auto& candidate) {
          return candidate.manifest.id == "demo.public-domain.human.production" &&
                 candidate.manifest.version == "0.12.0";
        });
    if (demo != voicebanks_.end()) {
      static_cast<void>(bindVoicebankLocked(*demo));
    }
  }
  refreshVoicebankResolutionLocked();
  rebuildController();
  const auto loaded = character_.load(characterPackage);
  if (loaded && controller_) {
    controller_->setCharacterMetadata(character_.displayName(),
                                      character_.styleName());
  }
  controller_->setDirty(false);
  dirty_ = false;
  requestRender(renderSampleRate_);
}


core::Result<void> EditorRuntime::bindVoicebankLocked(
    const voicebank::VoicebankCandidate& candidate) {
  if (!trackId_.valid()) {
    return core::failure(core::ErrorCode::NotFound,
                         "CLAP editor contains no vocal track for Voicebank binding");
  }
  return session_.execute(
      std::make_unique<application::SetTrackVoicebankCommand>(
          trackId_, domain::VoicebankReference{
                        .id = candidate.manifest.id,
                        .version = candidate.manifest.version,
                        .contentHash = candidate.contentHash,
                    }));
}

void EditorRuntime::refreshVoicebankResolutionLocked() {
  const auto* track = session_.project().findVocalTrack(trackId_);
  if (track == nullptr) {
    voicebankResolution_ = {};
    voicebankResolution_.status = voicebank::VoicebankResolveStatus::InvalidReference;
    voicebankResolution_.diagnostic = "CLAP editor contains no vocal track";
    return;
  }
  voicebankResolution_ = voicebankCatalog_.resolve(
      track->voicebank, voicebanks_,
      voicebank::VoicebankResolveOptions{
          .requireTrustedInstalled = true,
          .allowDevelopmentFixtures = true,
      });
}

core::Result<void> EditorRuntime::refreshVoicebanks() {
  std::lock_guard lock(mutex_);
  auto scanned = voicebankCatalog_.scan(voicebankRoots_);
  if (!scanned) return core::Result<void>{scanned.error()};
  voicebanks_ = std::move(scanned).value();
  refreshVoicebankResolutionLocked();
  if (controller_) {
    const auto ready = voicebankResolution_.resolved();
    controller_->setAudioState(ready, voicebankStatusLabel(voicebankResolution_));
  }
  requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

core::Result<void> EditorRuntime::addVoicebankSearchRoot(
    voicebank::VoicebankSearchRoot root) {
  std::lock_guard lock(mutex_);
  if (root.path.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Voicebank relink root cannot be empty");
  }
  root.path = root.path.lexically_normal();
  const auto existing = std::find_if(
      voicebankRoots_.begin(), voicebankRoots_.end(), [&root](const auto& value) {
        return value.path == root.path && value.kind == root.kind;
      });
  if (existing == voicebankRoots_.end()) voicebankRoots_.push_back(std::move(root));
  auto scanned = voicebankCatalog_.scan(voicebankRoots_);
  if (!scanned) return core::Result<void>{scanned.error()};
  voicebanks_ = std::move(scanned).value();
  refreshVoicebankResolutionLocked();
  if (controller_) {
    const auto ready = voicebankResolution_.resolved();
    controller_->setAudioState(ready, voicebankStatusLabel(voicebankResolution_));
  }
  requestRender(renderSampleRate_);
  requestRepaint();
  return core::success();
}

core::Result<void> EditorRuntime::selectVoicebank(
    std::string_view id, std::string_view version,
    std::optional<std::string_view> contentHash) {
  std::lock_guard lock(mutex_);
  const auto candidate = std::find_if(
      voicebanks_.begin(), voicebanks_.end(),
      [id, version, contentHash](const auto& value) {
        return value.manifest.id == id && value.manifest.version == version &&
               (!contentHash.has_value() || value.contentHash == *contentHash);
      });
  if (candidate == voicebanks_.end()) {
    return core::failure(
        contentHash.has_value() ? core::ErrorCode::Conflict : core::ErrorCode::NotFound,
        contentHash.has_value()
            ? "Requested Voicebank content hash is unavailable for that ID and version"
            : "Requested Voicebank ID and version are unavailable",
        std::string{id} + " " + std::string{version});
  }
  if (contentHash.has_value() && *contentHash != candidate->contentHash) {
    return core::failure(core::ErrorCode::Conflict,
                         "Requested Voicebank content hash does not match",
                         candidate->contentHash);
  }
  const auto bound = bindVoicebankLocked(*candidate);
  if (!bound) return bound;
  refreshVoicebankResolutionLocked();
  if (controller_) {
    controller_->setAudioState(
        true, "BANK " + candidate->manifest.displayName + " [" +
                  std::string{voicebank::voicebankTrustName(candidate->trust)} + "]");
  }
  requestRenderAfterEdit();
  return core::success();
}

voicebank::VoicebankResolution EditorRuntime::voicebankResolution() const {
  std::lock_guard lock(mutex_);
  return voicebankResolution_;
}

std::vector<voicebank::VoicebankCandidate>
EditorRuntime::availableVoicebanks() const {
  std::lock_guard lock(mutex_);
  return voicebanks_;
}

void EditorRuntime::configureControllerCallbacks() {
  native_ui::EditorHostCallbacks callbacks{
      .requestRepaint = [this] { requestRepaint(); },
      .beginTextInput = [this](const native_ui::TextInputRequest& request) {
        if (beginTextInput_) beginTextInput_(request);
      },
      .endTextInput = [this] {
        if (endTextInput_) endTextInput_();
      },
      .setPlaying = [this](bool) { requestRepaint(); },
  };
  controller_ = std::make_unique<native_ui::NativeEditorController>(
      session_, factory_, regionId_, std::move(callbacks));
  controller_->resize(logicalWidth_, logicalHeight_);
  const auto ready = voicebankResolution_.resolved();
  controller_->setAudioState(ready, voicebankStatusLabel(voicebankResolution_));
}

void EditorRuntime::rebuildController() {
  configureControllerCallbacks();
}

void EditorRuntime::setRepaintCallback(std::function<void()> callback) {
  std::lock_guard lock(mutex_);
  repaintCallback_ = std::move(callback);
}

void EditorRuntime::setRenderReadyCallback(std::function<void()> callback) {
  renderService_.setCompletionCallback(std::move(callback));
}

void EditorRuntime::setTextInputCallbacks(
    std::function<void(const native_ui::TextInputRequest&)> begin,
    std::function<void()> end) {
  std::lock_guard lock(mutex_);
  beginTextInput_ = std::move(begin);
  endTextInput_ = std::move(end);
}

void EditorRuntime::resize(double logicalWidth, double logicalHeight) noexcept {
  std::lock_guard lock(mutex_);
  logicalWidth_ = std::max(480.0, logicalWidth);
  logicalHeight_ = std::max(320.0, logicalHeight);
  controller_->resize(logicalWidth_, logicalHeight_);
}

native_ui::EditorSceneState EditorRuntime::sceneState() const {
  auto state = controller_->sceneState();
  state.characterMode = session_.project().settings().characterDisplay;
  state.characterPortrait = character_.portrait(state.characterState);
  if (state.characterName.empty()) state.characterName = character_.displayName();
  if (state.characterStyle.empty()) state.characterStyle = character_.styleName();
  return state;
}

void EditorRuntime::paint(native_ui::RasterCanvas& canvas) noexcept {
  std::lock_guard lock(mutex_);
  painter_.paint(canvas, controller_->pianoRoll(), sceneState());
  const auto seam = primarySeamAmount();
  const auto width = canvas.logicalWidth();
  const auto left = std::max(520.0, width - 360.0);
  canvas.fillRect(ui::Rect{left, 9.0, 330.0, 38.0},
                  native_ui::Color{20, 18, 24, 230});
  canvas.drawText(ui::Point{left + 10.0, 18.0}, "SEAM BOUNDARY",
                  native_ui::Color{201, 190, 201, 255}, 8.0);
  canvas.fillRect(ui::Rect{left + 112.0, 20.0, 190.0, 10.0},
                  native_ui::Color{58, 52, 62, 255});
  canvas.fillRect(ui::Rect{left + 112.0, 20.0, 190.0 * seam, 10.0},
                  native_ui::Color{168, 82, 120, 255});
  const auto stats = renderService_.stats();
  const auto preview = renderService_.latest();
  const auto label = "PROD " + std::string{previewStatusName(preview->status)} +
                     " " + std::to_string(stats.completed) + "/" +
                     std::to_string(stats.submitted);
  canvas.drawText(ui::Point{left + 112.0, 34.0}, label,
                  preview->status == PreviewStatus::Ready
                      ? native_ui::Color{153, 178, 169, 255}
                      : native_ui::Color{205, 126, 126, 255},
                  7.0);
}

void EditorRuntime::requestRenderAfterEdit() {
  dirty_ = true;
  controller_->setDirty(true);
  requestRender(renderSampleRate_);
  requestRepaint();
}

void EditorRuntime::pointerDown(const native_ui::PointerEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  const auto seamLeft = std::max(520.0, logicalWidth_ - 248.0);
  if (event.button == native_ui::PointerButton::Left &&
      event.position.y >= 16.0 && event.position.y <= 42.0 &&
      event.position.x >= seamLeft && event.position.x <= seamLeft + 190.0) {
    const auto value = static_cast<float>(
        std::clamp((event.position.x - seamLeft) / 190.0, 0.0, 1.0));
    static_cast<void>(setPrimarySeamAmount(value));
    return;
  }
  static_cast<void>(controller_->pointerDown(event));
}

void EditorRuntime::pointerMove(const native_ui::PointerEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  static_cast<void>(controller_->pointerMove(event));
}

void EditorRuntime::pointerUp(const native_ui::PointerEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  const auto before = session_.revision();
  static_cast<void>(controller_->pointerUp(event));
  if (session_.revision() != before) requestRenderAfterEdit();
}

void EditorRuntime::scroll(double deltaX, double deltaY, ui::Point anchor,
                           native_ui::InputModifiers modifiers) noexcept {
  std::lock_guard lock(mutex_);
  controller_->scroll(deltaX, deltaY, anchor, modifiers);
}

void EditorRuntime::keyDown(const native_ui::KeyEvent& event) noexcept {
  std::lock_guard lock(mutex_);
  const auto before = session_.revision();
  static_cast<void>(controller_->keyDown(event));
  if (session_.revision() != before) requestRenderAfterEdit();
}

void EditorRuntime::textComposition(
    std::u32string text, ui::CompositionSelection selection) noexcept {
  std::lock_guard lock(mutex_);
  static_cast<void>(controller_->updateTextComposition(std::move(text), selection));
}

void EditorRuntime::textCommit(std::u32string text) noexcept {
  std::lock_guard lock(mutex_);
  const auto before = session_.revision();
  static_cast<void>(controller_->commitTextComposition(std::move(text)));
  if (session_.revision() != before) requestRenderAfterEdit();
}

void EditorRuntime::textCancel() noexcept {
  std::lock_guard lock(mutex_);
  controller_->cancelTextComposition();
}

domain::Project EditorRuntime::projectCopy() const {
  std::lock_guard lock(mutex_);
  return session_.project();
}

core::Result<void> EditorRuntime::replaceProject(domain::Project project) {
  std::lock_guard lock(mutex_);
  const auto replacementTrack = firstTrackId(project);
  const auto replacementRegion = firstRegionId(project);
  if (!replacementTrack.valid() || !replacementRegion.valid()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "CLAP editor state contains no vocal track and region");
  }
  const auto replaced = session_.replaceProject(std::move(project));
  if (!replaced) return replaced;
  trackId_ = replacementTrack;
  regionId_ = replacementRegion;
  factory_.synchronizeWith(session_.project());
  auto scanned = voicebankCatalog_.scan(voicebankRoots_);
  if (scanned) voicebanks_ = std::move(scanned).value();
  refreshVoicebankResolutionLocked();
  rebuildController();
  controller_->setCharacterMetadata(character_.displayName(),
                                    character_.styleName());
  controller_->setDirty(false);
  dirty_ = false;
  renderService_.submit(session_.project(), trackId_, regionId_,
                        voicebankResolution_, session_.revision(),
                        renderSampleRate_);
  requestRepaint();
  return core::success();
}

std::uint64_t EditorRuntime::revision() const noexcept {
  std::lock_guard lock(mutex_);
  return session_.revision();
}

void EditorRuntime::requestRender(std::uint32_t sampleRate) {
  std::lock_guard lock(mutex_);
  renderSampleRate_ = std::clamp(sampleRate, 8000U, 192000U);
  renderService_.submit(session_.project(), trackId_, regionId_,
                        voicebankResolution_, session_.revision(),
                        renderSampleRate_);
}

std::shared_ptr<const RenderedPreview> EditorRuntime::renderedPreview() const {
  return renderService_.latest();
}

RenderServiceStats EditorRuntime::renderStats() const noexcept {
  return renderService_.stats();
}

core::Result<void> EditorRuntime::setPrimarySeamAmount(float value) {
  std::lock_guard lock(mutex_);
  const auto key = primaryPhonemeKey(session_, regionId_);
  if (!key.has_value()) {
    return core::failure(core::ErrorCode::NotFound,
                         "No note is available for seam editing");
  }
  domain::SeamOverride seam{
      .incomingStartKey = *key,
      .seamAmount = std::clamp(value, 0.0F, 1.0F),
      .overlap = time::Microseconds{9000},
      .phaseReset = 0.65F,
      .envelopeBlend = 0.20F,
      .curve = domain::SeamCurve::HardCharacter,
      .locked = true,
  };
  auto result = session_.execute(
      std::make_unique<application::UpsertSeamOverrideCommand>(regionId_, seam));
  if (result) requestRenderAfterEdit();
  return result;
}

float EditorRuntime::primarySeamAmount() const noexcept {
  std::lock_guard lock(mutex_);
  const auto* region = session_.project().findRegion(regionId_);
  if (region == nullptr || region->notes.empty()) return 0.55F;
  const auto key = primaryPhonemeKey(session_, regionId_);
  if (!key.has_value()) return 0.55F;
  const auto* seam = region->findSeamOverride(*key);
  return seam != nullptr && seam->seamAmount.has_value()
             ? std::clamp(*seam->seamAmount, 0.0F, 1.0F)
             : 0.55F;
}

void EditorRuntime::requestRepaint() const {
  if (repaintCallback_) repaintCallback_();
}

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
