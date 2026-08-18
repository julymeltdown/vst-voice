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
struct UnitSpan {
  UnitKind kind{UnitKind::Sustain};
  std::uint32_t begin{0}, end{0}, loopBegin{0}, loopEnd{0};
  std::int16_t rootKey{60}, fromKey{-1}, toKey{-1};
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
  const LiveVoicebankResource* acquireForAudio() noexcept;
  void releaseFromAudio() noexcept;
  [[nodiscard]] std::uint64_t generation() const noexcept;
 private:
  std::array<std::shared_ptr<const LiveVoicebankResource>,3> slots_{};
  std::atomic<int> published_{-1};
  std::atomic<int> audio_{-1};
  std::atomic<std::uint64_t> generation_{0};
};

enum class EventType : std::uint8_t {
  NoteOn, NoteOff, NoteChoke, PitchBend, Pressure, Timbre, Brightness,
  Midi1
};
struct LiveEvent {
  std::uint32_t sampleOffset{0};
  EventType type{EventType::NoteOn};
  std::int32_t noteId{-1};
  std::int16_t channel{0};
  std::int16_t key{60};
  float value{0.0F};
  std::array<std::uint8_t,3> midi{{0,0,0}};
};
struct LiveStats {
  std::uint64_t noteOns{0}, noteOffs{0}, steals{0}, transitionHits{0},
      transitionFallbacks{0}, midiEvents{0}, expressionEvents{0}, renderedFrames{0};
};
class LiveVoiceEngine {
 public:
  LiveVoiceEngine();
  void configure(std::uint32_t sampleRate, std::uint32_t outputChannels) noexcept;
  bool publishResource(std::shared_ptr<const LiveVoicebankResource> resource) noexcept;
  void process(std::span<const LiveEvent> events, float* const* outputs,
               std::uint32_t channels, std::uint32_t frames) noexcept;
  void reset() noexcept;
  [[nodiscard]] LiveStats stats() const noexcept;
  // Compatibility surface for the pre-12C CLAP editor.
  void noteOn(std::int32_t noteId, std::int32_t key, float velocity) noexcept;
  void noteOff(std::int32_t noteId, std::int32_t key) noexcept;
  float renderSample() noexcept;
 private:
  struct Voice {
    bool active{false}, releasing{false};
    std::int32_t noteId{-1}; std::int16_t channel{0}, key{60};
    float velocity{0.8F}, pressure{1.0F}, timbre{0.0F}, brightness{0.0F};
    double position{0.0}, increment{1.0};
    float envelope{0.0F}, lastSample{0.0F}, tailSample{0.0F};
    std::uint32_t tailRemaining{0}, tailLength{0};
    std::uint64_t age{0}; const UnitSpan* sustain{nullptr}; const UnitSpan* release{nullptr};
  };
  void applyEvent(const LiveEvent& event, const LiveVoicebankResource* resource) noexcept;
  void renderRange(float* const* outputs, std::uint32_t channels,
                   std::uint32_t begin, std::uint32_t end,
                   const LiveVoicebankResource* resource) noexcept;
  float renderVoice(Voice& voice, const LiveVoicebankResource& resource) noexcept;
  Voice* allocateVoice() noexcept;
  const UnitSpan* choose(UnitKind kind, int key, int from=-1) const noexcept;
  ResourcePublisher publisher_;
  const LiveVoicebankResource* resource_{nullptr};
  std::array<Voice,kMaxVoices> voices_{};
  std::array<float,16> channelBend_{};
  std::uint32_t sampleRate_{48000}, outputChannels_{2};
  std::uint64_t ageCounter_{0}; LiveStats stats_{};
};
} // namespace seam::phase12c
