#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace seam::phase12c {

constexpr std::size_t kMaxVoices = 32;
constexpr std::size_t kMaxResourceBytes = 256u * 1024u * 1024u;

enum class UnitKind : std::uint8_t { Attack, Sustain, Transition, Release };

enum class VoiceStage : std::uint8_t {
  Inactive,
  Attack,
  Transition,
  Sustain,
  Release,
};

struct UnitSpan {
  UnitKind kind{UnitKind::Sustain};
  std::uint32_t begin{0};
  std::uint32_t end{0};
  std::uint32_t loopBegin{0};
  std::uint32_t loopEnd{0};
  std::int16_t rootKey{60};
  std::int16_t fromKey{-1};
  std::int16_t toKey{-1};
};

struct LiveVoicebankResource {
  std::uint32_t sampleRate{48000};
  std::vector<float> mono;
  std::vector<UnitSpan> units;
  std::string contentHash;
  bool trusted{false};

  [[nodiscard]] std::size_t bytes() const noexcept;
  [[nodiscard]] bool valid() const noexcept;
};

std::shared_ptr<const LiveVoicebankResource> makeEmbeddedHumanResource();

class ResourcePublisher {
 public:
  bool publish(std::shared_ptr<const LiveVoicebankResource> resource) noexcept;
  void clear() noexcept;
  const LiveVoicebankResource* acquireForAudio() noexcept;
  void releaseFromAudio() noexcept;
  [[nodiscard]] std::uint64_t generation() const noexcept;

 private:
  std::array<std::shared_ptr<const LiveVoicebankResource>, 3> slots_{};
  std::atomic<int> published_{-1};
  std::atomic<int> audio_{-1};
  std::atomic<std::uint64_t> generation_{0};
};

enum class EventType : std::uint8_t {
  NoteOn,
  NoteOff,
  NoteChoke,
  PitchBend,
  Pressure,
  Timbre,
  Brightness,
  Midi1,
};

struct LiveEvent {
  std::uint32_t sampleOffset{0};
  EventType type{EventType::NoteOn};
  std::int32_t noteId{-1};
  std::int16_t channel{0};
  std::int16_t key{60};
  float value{0.0F};
  std::array<std::uint8_t, 3> midi{{0, 0, 0}};
};

struct LiveStats {
  std::uint64_t noteOns{0};
  std::uint64_t noteOffs{0};
  std::uint64_t steals{0};
  std::uint64_t transitionHits{0};
  std::uint64_t transitionFallbacks{0};
  std::uint64_t midiEvents{0};
  std::uint64_t expressionEvents{0};
  std::uint64_t renderedFrames{0};
  std::uint64_t silentFramesNoResource{0};
};

class LiveVoiceEngine {
 public:
  LiveVoiceEngine();

  void configure(std::uint32_t sampleRate,
                 std::uint32_t outputChannels) noexcept;
  bool publishResource(
      std::shared_ptr<const LiveVoicebankResource> resource) noexcept;
  void clearResource() noexcept;

  void process(std::span<const LiveEvent> events,
               float* const* outputs,
               std::uint32_t channels,
               std::uint32_t frames) noexcept;
  void reset() noexcept;
  [[nodiscard]] LiveStats stats() const noexcept;

  // Compatibility surface used by the pre-12C CLAP editor adapter.
  void noteOn(std::int32_t noteId,
              std::int32_t key,
              float velocity) noexcept;
  void noteOff(std::int32_t noteId, std::int32_t key) noexcept;
  float renderSample() noexcept;

 private:
  struct Voice {
    bool active{false};
    bool releasing{false};
    std::int32_t noteId{-1};
    std::int16_t channel{0};
    std::int16_t key{60};
    float velocity{0.8F};
    float pressure{1.0F};
    float timbre{0.0F};
    float brightness{0.0F};
    double position{0.0};
    double increment{1.0};
    float envelope{0.0F};
    float lastSample{0.0F};
    float tailSample{0.0F};
    std::uint32_t tailRemaining{0};
    std::uint32_t tailLength{0};
    std::uint32_t legatoFadeInRemaining{0};
    std::uint32_t legatoFadeOutRemaining{0};
    std::uint32_t legatoFadeLength{0};
    std::uint64_t age{0};
    VoiceStage stage{VoiceStage::Inactive};
    const UnitSpan* current{nullptr};
    const UnitSpan* attack{nullptr};
    const UnitSpan* sustain{nullptr};
    const UnitSpan* transition{nullptr};
    const UnitSpan* release{nullptr};
  };

  void applyEvent(const LiveEvent& event,
                  const LiveVoicebankResource* resource) noexcept;
  void renderRange(float* const* outputs,
                   std::uint32_t channels,
                   std::uint32_t begin,
                   std::uint32_t end,
                   const LiveVoicebankResource* resource) noexcept;
  float renderVoice(Voice& voice,
                    const LiveVoicebankResource& resource) noexcept;
  void advanceStage(Voice& voice) noexcept;
  void beginRelease(Voice& voice, bool choke) noexcept;
  Voice* allocateVoice() noexcept;
  Voice* findLegatoSource(std::int16_t channel) noexcept;
  const UnitSpan* choose(UnitKind kind,
                         int key,
                         int from = -1,
                         int to = -1) const noexcept;

  ResourcePublisher publisher_;
  const LiveVoicebankResource* resource_{nullptr};
  std::array<Voice, kMaxVoices> voices_{};
  std::array<float, 16> channelBend_{};
  std::uint32_t sampleRate_{48000};
  std::uint32_t outputChannels_{2};
  std::uint64_t ageCounter_{0};
  LiveStats stats_{};
};

}  // namespace seam::phase12c
