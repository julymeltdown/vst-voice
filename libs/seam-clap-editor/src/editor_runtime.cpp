#include "seam/clap_editor/editor_runtime.hpp"

#include "human_vowel_data.hpp"

#include "seam/application/render_commands.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
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

float noteSeam(const domain::VocalRegion& region,
               domain::NoteId noteId) noexcept {
  const auto iterator = std::find_if(
      region.seamOverrides.begin(), region.seamOverrides.end(),
      [noteId](const domain::SeamOverride& value) {
        return value.incomingStartKey.noteId == noteId;
      });
  if (iterator == region.seamOverrides.end() ||
      !iterator->seamAmount.has_value()) {
    return 0.55F;
  }
  return std::clamp(*iterator->seamAmount, 0.0F, 1.0F);
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
    : worker_([this](std::stop_token stopToken) { workerLoop(stopToken); }) {}

AsyncPreviewRenderService::~AsyncPreviewRenderService() {
  worker_.request_stop();
  condition_.notify_all();
}

void AsyncPreviewRenderService::submit(domain::Project project,
                                       domain::RegionId regionId,
                                       std::uint64_t revision,
                                       std::uint32_t sampleRate) {
  sampleRate = std::clamp(sampleRate, 8000U, 192000U);
  latestSubmittedRevision_.store(revision, std::memory_order_release);
  {
    std::lock_guard lock(mutex_);
    if (pending_.has_value()) {
      cancelled_.fetch_add(1U, std::memory_order_relaxed);
    }
    pending_ = Request{.project = std::move(project),
                       .regionId = regionId,
                       .revision = revision,
                       .sampleRate = sampleRate};
  }
  submitted_.fetch_add(1U, std::memory_order_relaxed);
  condition_.notify_all();
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
    {
      std::unique_lock lock(mutex_);
      condition_.wait(lock, stopToken,
                      [this] { return pending_.has_value(); });
      if (stopToken.stop_requested()) break;
      request = std::move(*pending_);
      pending_.reset();
    }
    auto rendered = render(request, stopToken);
    if (!rendered || stopToken.stop_requested()) continue;
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
  }
}

std::shared_ptr<RenderedPreview> AsyncPreviewRenderService::render(
    const Request& request, std::stop_token stopToken) {
  const auto* region = request.project.findRegion(request.regionId);
  if (region == nullptr) return {};
  auto output = std::make_shared<RenderedPreview>();
  output->sampleRate = request.sampleRate;
  output->revision = request.revision;

  time::Tick phraseEnd{0};
  for (const auto& note : region->notes) {
    phraseEnd = std::max(phraseEnd, note.startTick + note.durationTick);
  }
  const auto endFrame = request.project.tempoMap().sampleFrameAt(
      region->startTick + phraseEnd, static_cast<double>(request.sampleRate));
  const auto tailFrames = static_cast<time::SampleFrame>(request.sampleRate / 5U);
  if (endFrame < 0 || endFrame > static_cast<time::SampleFrame>(request.sampleRate) * 600) {
    return {};
  }
  const auto frameCount = static_cast<std::size_t>(endFrame + tailFrames);
  output->stereo.assign(frameCount * 2U, 0.0F);

  for (const auto& note : region->notes) {
    if (stopToken.stop_requested() ||
        request.revision !=
            latestSubmittedRevision_.load(std::memory_order_acquire)) {
      cancelled_.fetch_add(1U, std::memory_order_relaxed);
      return {};
    }
    const auto absoluteStart = region->startTick + note.startTick;
    const auto absoluteEnd = absoluteStart + note.durationTick;
    const auto startFrame = request.project.tempoMap().sampleFrameAt(
        absoluteStart, static_cast<double>(request.sampleRate));
    const auto noteEndFrame = request.project.tempoMap().sampleFrameAt(
        absoluteEnd, static_cast<double>(request.sampleRate));
    if (startFrame < 0 || noteEndFrame <= startFrame) continue;
    const auto begin = static_cast<std::size_t>(startFrame);
    const auto duration = static_cast<std::size_t>(noteEndFrame - startFrame);
    const auto ratio = std::pow(2.0, (static_cast<double>(note.midiKey) - kSourceRootMidi) / 12.0) *
                       static_cast<double>(kSourceSampleRate) /
                       static_cast<double>(request.sampleRate);
    const auto seam = noteSeam(*region, note.id);
    const auto attackFrames = std::max<std::size_t>(1U, request.sampleRate / 100U);
    const auto releaseFrames = std::max<std::size_t>(1U, request.sampleRate / 80U);
    double sourcePosition = 0.0;
    for (std::size_t offset = 0U;
         offset < duration && begin + offset < frameCount; ++offset) {
      auto envelope = 1.0F;
      if (offset < attackFrames) {
        envelope *= static_cast<float>(offset) /
                    static_cast<float>(attackFrames);
      }
      const auto remaining = duration - offset;
      if (remaining < releaseFrames) {
        envelope *= static_cast<float>(remaining) /
                    static_cast<float>(releaseFrames);
      }
      const auto boundaryCharacter = 0.92F + seam * 0.08F;
      const auto value = sampleAt(sourcePosition) * envelope *
                         boundaryCharacter * 0.60F;
      sourcePosition += ratio;
      const auto destination = (begin + offset) * 2U;
      output->stereo[destination] = std::clamp(
          output->stereo[destination] + value, -1.0F, 1.0F);
      output->stereo[destination + 1U] = std::clamp(
          output->stereo[destination + 1U] + value, -1.0F, 1.0F);
    }
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
  for (std::size_t index = 0U; index < region->notes.size(); ++index) {
    region->unitSelectionOverrides.push_back(domain::UnitSelectionOverride{
        .startKey = domain::PhonemeKey{
            .noteId = region->notes[index].id, .ordinal = 0U},
        .tokenCount = 1U,
        .unitId = "demo.sample.vowel.a." + std::to_string(index + 1U),
        .renderer = index % 2U == 0U
                        ? domain::UnitRendererKind::ClassicPsola
                        : domain::UnitRendererKind::Raw,
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

EditorRuntime::EditorRuntime(std::optional<domain::Project> project,
                             const std::filesystem::path& characterPackage)
    : session_([&] {
        if (project.has_value()) {
          regionId_ = firstRegionId(*project);
          factory_.synchronizeWith(*project);
          return std::move(*project);
        }
        return makeDefaultProject(factory_, regionId_);
      }()) {
  rebuildController();
  const auto loaded = character_.load(characterPackage);
  if (loaded && controller_) {
    controller_->setCharacterMetadata(character_.displayName(),
                                      character_.styleName());
  }
  requestRender(renderSampleRate_);
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
  controller_->setAudioState(true, "CLAP HOST");
}

void EditorRuntime::rebuildController() {
  configureControllerCallbacks();
}

void EditorRuntime::setRepaintCallback(std::function<void()> callback) {
  std::lock_guard lock(mutex_);
  repaintCallback_ = std::move(callback);
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
  const auto label = "ASYNC " + std::to_string(stats.completed) + "/" +
                     std::to_string(stats.submitted);
  canvas.drawText(ui::Point{left + 112.0, 34.0}, label,
                  native_ui::Color{153, 178, 169, 255}, 7.0);
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

std::uint64_t EditorRuntime::revision() const noexcept {
  std::lock_guard lock(mutex_);
  return session_.revision();
}

void EditorRuntime::requestRender(std::uint32_t sampleRate) {
  std::lock_guard lock(mutex_);
  renderSampleRate_ = std::clamp(sampleRate, 8000U, 192000U);
  renderService_.submit(session_.project(), regionId_, session_.revision(),
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
