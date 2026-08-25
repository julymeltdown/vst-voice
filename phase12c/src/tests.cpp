#include "seam/phase12c/live_voice.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

static std::atomic<unsigned long long> allocationCount{0};
static thread_local bool allocationProbe = false;

#define SEAM_REQUIRE(expression)                                             \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::cerr << "PHASE12C_LIVE_TESTS=FAIL: " << #expression << '\n';       \
      return 1;                                                               \
    }                                                                         \
  } while (false)

void* operator new(std::size_t size) {
  if (allocationProbe) {
    ++allocationCount;
  }
  if (auto* pointer = std::malloc(size)) {
    return pointer;
  }
  throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
  using namespace seam::phase12c;
  auto resource = makeEmbeddedHumanResource();
  SEAM_REQUIRE(resource && resource->valid());
  SEAM_REQUIRE(resource->bytes() < kMaxResourceBytes);

  ResourcePublisher publisher;
  SEAM_REQUIRE(publisher.publish(resource));
  SEAM_REQUIRE(publisher.acquireForAudio() == resource.get());
  publisher.releaseFromAudio();

  LiveVoiceEngine engine;
  engine.configure(48000, 2);
  SEAM_REQUIRE(engine.publishResource(resource));

  constexpr unsigned frameCount = 768;
  std::array<float, frameCount> left{};
  std::array<float, frameCount> right{};
  float* outputs[2] = {left.data(), right.data()};
  std::array<LiveEvent, 6> events{{
      {64, EventType::NoteOn, 1, 0, 60, 0.9F, {}},
      {192, EventType::NoteOn, 2, 0, 62, 0.8F, {}},
      {300, EventType::Pressure, 2, 0, 62, 0.4F, {}},
      {360, EventType::PitchBend, 2, 0, 62, 2.0F, {}},
      {500, EventType::NoteOff, 1, 0, 60, 0.0F, {}},
      {620, EventType::NoteOff, 2, 0, 62, 0.0F, {}},
  }};

  allocationProbe = true;
  const auto before = allocationCount.load();
  engine.process(events, outputs, 2, frameCount);
  allocationProbe = false;
  SEAM_REQUIRE(allocationCount.load() == before);

  for (unsigned frame = 0; frame < 64; ++frame) {
    SEAM_REQUIRE(left[frame] == 0.0F);
  }
  double energy = 0.0;
  for (const auto sample : left) {
    SEAM_REQUIRE(std::isfinite(sample));
    energy += std::abs(sample);
  }
  SEAM_REQUIRE(energy > 1.0);
  SEAM_REQUIRE(engine.stats().transitionHits == 1);

  std::vector<LiveEvent> many;
  many.reserve(33);
  for (int index = 0; index < 33; ++index) {
    many.push_back({0, EventType::NoteOn, index, 1,
                    static_cast<std::int16_t>(48 + index % 24), 0.7F, {}});
  }
  engine.process(many, outputs, 2, frameCount);
  SEAM_REQUIRE(engine.stats().steals >= 1);

  std::array<LiveEvent, 2> midi{{
      {0, EventType::Midi1, -1, 0, 0, 0.0F, {0x90, 64, 100}},
      {500, EventType::Midi1, -1, 0, 0, 0.0F, {0x80, 64, 0}},
  }};
  engine.process(midi, outputs, 2, frameCount);
  SEAM_REQUIRE(engine.stats().midiEvents >= 2);

  auto untrusted = std::make_shared<LiveVoicebankResource>(*resource);
  untrusted->trusted = false;
  SEAM_REQUIRE(!engine.publishResource(untrusted));
  left.fill(1.0F);
  right.fill(1.0F);
  engine.process({}, outputs, 2, frameCount);
  for (const auto sample : left) {
    SEAM_REQUIRE(sample == 0.0F);
  }
  SEAM_REQUIRE(engine.stats().silentFramesNoResource >= frameCount);

  std::cout << "PHASE12C_LIVE_TESTS=PASS energy=" << energy
            << " steals=" << engine.stats().steals << '\n';
  return 0;
}

#undef SEAM_REQUIRE
