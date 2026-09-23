#include "test_framework.hpp"

#include "seam/clap_editor/editor_runtime.hpp"
#include <clap/clap.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <type_traits>

// These wire constants come from CLAP 1.2.10's pinned events.h, not from
// another struct built with our consolidated header. Equal sizeof alone
// cannot detect an expression_id moved behind the note address fields.
// https://github.com/free-audio/clap/blob/195b42a004144fab0b3cf95e9c067187d15365b7/include/clap/events.h#L174-L205
static_assert(std::is_standard_layout_v<clap_event_note_expression_t>);
static_assert(std::is_trivially_copyable_v<clap_event_note_expression_t>);
static_assert(std::is_same_v<clap_note_expression, std::int32_t>);
static_assert(sizeof(clap_event_header_t) == 16U);
static_assert(sizeof(clap_event_note_expression_t) == 40U);
static_assert(offsetof(clap_event_note_expression_t, header) == 0U);
static_assert(offsetof(clap_event_note_expression_t, expression_id) == 16U);
static_assert(offsetof(clap_event_note_expression_t, note_id) == 20U);
static_assert(offsetof(clap_event_note_expression_t, port_index) == 24U);
static_assert(offsetof(clap_event_note_expression_t, channel) == 26U);
static_assert(offsetof(clap_event_note_expression_t, key) == 28U);
static_assert(offsetof(clap_event_note_expression_t, value) == 32U);
static_assert(CLAP_NOTE_EXPRESSION_VOLUME == 0);
static_assert(CLAP_NOTE_EXPRESSION_PAN == 1);
static_assert(CLAP_NOTE_EXPRESSION_TUNING == 2);
static_assert(CLAP_NOTE_EXPRESSION_VIBRATO == 3);
static_assert(CLAP_NOTE_EXPRESSION_EXPRESSION == 4);
static_assert(CLAP_NOTE_EXPRESSION_BRIGHTNESS == 5);
static_assert(CLAP_NOTE_EXPRESSION_PRESSURE == 6);

TEST_CASE("CLAP expression event decodes the pinned upstream wire layout") {
  std::array<std::byte, 40> wire{};
  const auto write = [&wire](std::size_t offset, auto value) {
    std::memcpy(wire.data() + offset, &value, sizeof(value));
  };
  write(0U, std::uint32_t{40U});
  write(4U, std::uint32_t{128U});
  write(8U, std::uint16_t{0U});
  write(10U, std::uint16_t{4U});
  write(16U, std::int32_t{6});
  write(20U, std::int32_t{1079});
  write(24U, std::int16_t{0});
  write(26U, std::int16_t{3});
  write(28U, std::int16_t{64});
  write(32U, 0.75);

  clap_event_note_expression_t event{};
  std::memcpy(&event, wire.data(), wire.size());
  CHECK(event.header.size == sizeof(event));
  CHECK(event.header.time == 128U);
  CHECK(event.header.space_id == CLAP_CORE_EVENT_SPACE_ID);
  CHECK(event.header.type == CLAP_EVENT_NOTE_EXPRESSION);
  CHECK(event.expression_id == CLAP_NOTE_EXPRESSION_PRESSURE);
  CHECK(event.note_id == 1079);
  CHECK(event.port_index == 0);
  CHECK(event.channel == 3);
  CHECK(event.key == 64);
  CHECK(event.value == 0.75);
}

TEST_CASE("live engine dispatches expression and MIDI events") {
  seam::live_voice::VoiceEngine::resetCallPathEvidence();
  seam::live_voice::VoiceEngine instrument;
  CHECK(instrument.publishVoicebankResource(
      seam::phase12c::makeEmbeddedHumanResource()));
  instrument.noteOn(1, 60, 0.9F);
  instrument.dispatchLiveEvent(seam::live_voice::LiveEvent{
      .type = seam::live_voice::EventType::PitchBend,
      .noteId = 1,
      .key = 60,
      .value = 2.0F,
  });
  instrument.dispatchLiveEvent(seam::live_voice::LiveEvent{
      .type = seam::live_voice::EventType::Pressure,
      .noteId = 1,
      .key = 60,
      .value = 0.7F,
  });
  instrument.dispatchLiveEvent(seam::live_voice::LiveEvent{
      .type = seam::live_voice::EventType::Midi1,
      .midi = {0x90U, 64U, 100U},
  });
  // The human fixture begins quietly and the live attack ramps for 8 ms
  // (384 frames at 48 kHz). Observe beyond the ramp before testing energy.
  std::array<float, 512> left{};
  std::array<float, 512> right{};
  float* outputs[2] = {left.data(), right.data()};
  instrument.renderLiveRange(outputs, 2U, 0U, 512U);
  double energy = 0.0;
  for (const auto sample : left) {
    CHECK(std::isfinite(sample));
    energy += std::abs(static_cast<double>(sample));
  }
  if (energy <= 1.0) std::cerr << "Live dispatch energy over 512 attack frames: " << energy << '\n';
  CHECK(energy > 1.0);
  CHECK(instrument.activeVoiceCount() == 2U);
  CHECK(seam::live_voice::VoiceEngine::callPathEvidence() > 0U);

  instrument.reset();
  std::array<seam::live_voice::LiveEvent, 1025> storm{};
  for (std::size_t index = 0U; index < storm.size(); ++index) {
    storm[index] = seam::live_voice::LiveEvent{
        .type = seam::live_voice::EventType::NoteOn,
        .noteId = static_cast<std::int32_t>(index),
        .key = static_cast<std::int16_t>(48U + (index % 24U)),
        .value = 0.7F,
    };
  }
  instrument.process(storm, outputs, 2U, 64U);
  CHECK(instrument.stats().eventOverflows >= 1U);
  CHECK(instrument.activeVoiceCount() <= seam::phase12c::kMaxVoices);

  seam::clap_editor::EditorRuntime missingBank(
      std::nullopt, std::filesystem::path{"assets/character-01"}, {});
  missingBank.noteOn(77, 60, 0.8F);
  for (int frame = 0; frame < 128; ++frame) {
    CHECK(missingBank.renderLiveSample() == 0.0F);
  }
}

TEST_CASE("live expression mapping changes pan and vibrato output independently") {
  using namespace seam;
  live_voice::VoiceEngine instrument;
  CHECK(instrument.publishVoicebankResource(phase12c::makeEmbeddedHumanResource()));
  std::array<float, 512> left{};
  std::array<float, 512> right{};
  float* outputs[2] = {left.data(), right.data()};
  const std::array<live_voice::LiveEvent, 2> panEvents{{
      {.type = live_voice::EventType::NoteOn, .noteId = 1, .channel = 0,
       .key = 60, .value = 0.9F},
      {.sampleOffset = 0U, .type = live_voice::EventType::Pan, .noteId = 1,
       .channel = 0, .key = 60, .value = -1.0F},
  }};
  instrument.process(panEvents, outputs, 2U, 512U);
  double leftEnergy = 0.0;
  double rightEnergy = 0.0;
  for (std::size_t index = 64U; index < left.size(); ++index) {
    leftEnergy += std::abs(static_cast<double>(left[index]));
    rightEnergy += std::abs(static_cast<double>(right[index]));
  }
  CHECK(leftEnergy > 1.0);
  CHECK(leftEnergy > rightEnergy * 8.0);

  instrument.reset();
  CHECK(instrument.publishVoicebankResource(phase12c::makeEmbeddedHumanResource()));
  const std::array<live_voice::LiveEvent, 2> vibratoEvents{{
      {.type = live_voice::EventType::NoteOn, .noteId = 2, .channel = 0,
       .key = 60, .value = 0.9F},
      {.sampleOffset = 128U, .type = live_voice::EventType::Vibrato, .noteId = 2,
       .channel = 0, .key = 60, .value = 1.0F},
  }};
  std::array<float, 512> vibratoLeft{};
  std::array<float, 512> vibratoRight{};
  float* vibratoOutputs[2] = {vibratoLeft.data(), vibratoRight.data()};
  instrument.process(vibratoEvents, vibratoOutputs, 2U, 512U);
  double postDifference = 0.0;
  for (std::size_t index = 256U; index < vibratoLeft.size(); ++index)
    postDifference += std::abs(static_cast<double>(vibratoLeft[index] - vibratoRight[index]));
  CHECK(postDifference < 1.0e-6); // Center pan remains centered while vibrato is active.

  live_voice::VoiceEngine baseline;
  CHECK(baseline.publishVoicebankResource(phase12c::makeEmbeddedHumanResource()));
  std::array<float, 512> baselineLeft{};
  std::array<float, 512> baselineRight{};
  float* baselineOutputs[2] = {baselineLeft.data(), baselineRight.data()};
  const std::array<live_voice::LiveEvent, 1> baselineEvents{{
      {.type = live_voice::EventType::NoteOn, .noteId = 2, .channel = 0,
       .key = 60, .value = 0.9F},
  }};
  baseline.process(baselineEvents, baselineOutputs, 2U, 512U);
  double vibratoDifference = 0.0;
  for (std::size_t index = 256U; index < vibratoLeft.size(); ++index)
    vibratoDifference += std::abs(static_cast<double>(vibratoLeft[index] - baselineLeft[index]));
  CHECK(vibratoDifference > 1.0e-4);
}

TEST_CASE("live note tuning uses the full wildcard address independently of channel bend") {
  using namespace seam;
  const auto run = [](live_voice::LiveEvent expression) {
    live_voice::VoiceEngine engine;
    CHECK(engine.publishResource(phase12c::makeEmbeddedHumanResource()));
    std::array<float, 512> left{}, right{};
    float* output[]{left.data(), right.data()};
    const std::array<live_voice::LiveEvent, 5> events{{
        {.type = phase12c::EventType::NoteOn, .noteId = 7, .channel = 0, .key = 60, .value = 0.8F},
        {.type = phase12c::EventType::Pan, .noteId = 7, .channel = 0, .key = 60, .value = -1.0F},
        {.type = phase12c::EventType::NoteOn, .noteId = 8, .channel = 0, .key = 64, .value = 0.8F},
        {.type = phase12c::EventType::Pan, .noteId = 8, .channel = 0, .key = 64, .value = 1.0F},
        expression}};
    engine.process(events, output, 2U, 512U);
    CHECK(engine.activeVoiceCount() == 2U);
    return std::array{left, right};
  };
  live_voice::LiveEvent tuning{.sampleOffset = 128U, .type = phase12c::EventType::PitchBend,
      .noteId = 7, .channel = 0, .key = 60, .value = 0.0F, .port = -1};
  const auto neutral = run(tuning);
  tuning.value = 12.0F;
  const auto changed = run(tuning);
  double leftDifference{}, rightDifference{}, rightEnergy{};
  for (std::size_t index = 128U; index < 512U; ++index) {
    leftDifference += std::abs(static_cast<double>(neutral[0][index] - changed[0][index]));
    rightDifference += std::abs(static_cast<double>(neutral[1][index] - changed[1][index]));
    rightEnergy += std::abs(static_cast<double>(neutral[1][index]));
  }
  CHECK(leftDifference > 0.1);
  CHECK(rightEnergy > 0.1);
  CHECK(rightDifference < 1.0e-6);
  tuning.key = 64; // note ID alone cannot override a conflicting key.
  CHECK(run(tuning) == neutral);
  tuning.noteId = -1; tuning.key = 60; tuning.channel = -1;
  CHECK(run(tuning) == changed);
}

TEST_CASE("live sustain release panic wildcard choke and zero velocity obey their distinct contracts") {
  using namespace seam;
  live_voice::VoiceEngine engine;
  CHECK(engine.publishResource(phase12c::makeEmbeddedHumanResource()));
  std::array<float, 4096> mono{};
  float* outputs[]{mono.data()};
  const auto midi = [&](std::array<std::uint8_t, 3> message) {
    engine.dispatch({.type = phase12c::EventType::Midi1, .midi = message});
  };
  midi({0x90, 60, 100}); midi({0xb0, 64, 127}); midi({0x80, 60, 0});
  engine.process({}, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 1U);
  midi({0xb0, 64, 0});
  engine.process({}, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 0U);
  engine.dispatch({.type = phase12c::EventType::NoteOn, .noteId = 1, .key = 60, .value = 0.8F});
  engine.dispatch({.type = phase12c::EventType::NoteOn, .noteId = 2, .key = 64, .value = 0.0F});
  CHECK(engine.activeVoiceCount() == 2U); // CLAP velocity zero is not MIDI note-off.
  engine.dispatch({.type = phase12c::EventType::NoteChoke, .noteId = -1, .channel = -1, .key = -1, .port = -1});
  engine.process({}, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 0U);
  midi({0x90, 60, 100}); midi({0xb0, 64, 127}); midi({0xb0, 123, 0});
  engine.process({}, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 1U);
  midi({0xb0, 120, 0});
  engine.process({}, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 0U);
}

TEST_CASE("MIDI repeated-key release targets one held MIDI voice without choking CLAP notes") {
  using namespace seam;
  live_voice::VoiceEngine engine;
  CHECK(engine.publishResource(phase12c::makeEmbeddedHumanResource()));
  std::array<float, 4096> mono{};
  float* outputs[]{mono.data()};
  const auto midi = [&](std::array<std::uint8_t, 3> message) {
    engine.dispatch({.type = phase12c::EventType::Midi1, .midi = message});
  };
  midi({0x90, 60, 100});
  midi({0x90, 60, 100});
  engine.dispatch({.type = phase12c::EventType::NoteOn, .noteId = 77,
      .channel = 0, .key = 60, .value = 0.8F});
  CHECK(engine.activeVoiceCount() == 3U);

  midi({0x80, 60, 0});
  engine.process({}, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 2U);
  midi({0x90, 60, 0}); // MIDI zero-velocity note-on is also one note-off.
  engine.process({}, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 1U);
  // An extra MIDI note-off cannot steal a CLAP-owned note at the same key.
  midi({0x80, 60, 0});
  engine.process({}, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 1U);
  engine.dispatch({.type = phase12c::EventType::NoteOff, .noteId = 77,
      .channel = 0, .key = 60});
  engine.process({}, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 0U);
}

TEST_CASE("ordinary CLAP and MIDI notes remain polyphonic across distinct sample offsets") {
  using namespace seam;
  for (const bool midi : {false, true}) {
    for (const std::uint32_t secondOffset : {1U, 256U, 480U}) {
      const auto resource = phase12c::makeEmbeddedHumanResource();
      live_voice::VoiceEngine combined, first, second;
      CHECK(combined.publishResource(resource));
      CHECK(first.publishResource(resource));
      CHECK(second.publishResource(resource));
      const auto event = [&](std::int32_t id, std::int16_t key, std::uint32_t offset, bool on) {
        live_voice::LiveEvent result{.sampleOffset = offset,
            .type = on ? phase12c::EventType::NoteOn : phase12c::EventType::NoteOff,
            .noteId = id, .channel = 0, .key = key, .value = 0.6F};
        if (midi) {
          result.type = phase12c::EventType::Midi1;
          result.noteId = -1;
          result.midi = {static_cast<std::uint8_t>(on ? 0x90U : 0x80U),
              static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(on ? 96U : 0U)};
        }
        return result;
      };
      const std::array<live_voice::LiveEvent, 2> bothEvents{{event(101, 60, 0U, true),
          event(202, 64, secondOffset, true)}};
      std::array<float, 4096> bothPcm{}, firstPcm{}, secondPcm{};
      float* bothOutput[]{bothPcm.data()};
      float* firstOutput[]{firstPcm.data()};
      float* secondOutput[]{secondPcm.data()};
      combined.process(bothEvents, bothOutput, 1U, 4096U);
      first.process(std::span<const live_voice::LiveEvent>{bothEvents}.first(1U), firstOutput, 1U, 4096U);
      second.process(std::span<const live_voice::LiveEvent>{bothEvents}.last(1U), secondOutput, 1U, 4096U);
      CHECK(combined.activeVoiceCount() == 2U);
      CHECK(combined.stats().transitionHits == 0U);
      CHECK(combined.stats().transitionFallbacks == 0U);
      for (std::size_t frame = 0U; frame < bothPcm.size(); ++frame) {
        CHECK(std::isfinite(bothPcm[frame]));
        CHECK_NEAR(bothPcm[frame], std::clamp(firstPcm[frame] + secondPcm[frame], -1.0F, 1.0F), 1.0e-7);
      }
      combined.dispatch(event(101, 60, 0U, false));
      combined.process({}, bothOutput, 1U, 4096U);
      CHECK(combined.activeVoiceCount() == 1U);
      combined.dispatch(event(202, 64, 0U, false));
      combined.process({}, bothOutput, 1U, 4096U);
      CHECK(combined.activeVoiceCount() == 0U);
    }
  }
}

TEST_CASE("live legato replacement requires an explicit monophonic policy") {
  using namespace seam;
  live_voice::VoiceEngine engine;
  engine.setVoiceMode(live_voice::VoiceMode::MonophonicLegato);
  CHECK(engine.publishResource(phase12c::makeEmbeddedHumanResource()));
  std::array<float, 4096> mono{};
  float* outputs[]{mono.data()};
  const std::array<live_voice::LiveEvent, 2> events{{
      {.type = phase12c::EventType::NoteOn, .noteId = 1, .key = 60, .value = 0.8F},
      {.type = phase12c::EventType::NoteOn, .noteId = 2, .key = 62, .value = 0.8F}}};
  for (unsigned iteration = 0U; iteration < 2U; ++iteration) {
    engine.process(events, outputs, 1U, 4096U);
    CHECK(engine.stats().transitionHits == 1U);
    CHECK(engine.activeVoiceCount() == 1U);
    engine.reset(); // Reset clears performance state, not the configured policy.
  }
  engine.setVoiceMode(live_voice::VoiceMode::Polyphonic);
  engine.process(events, outputs, 1U, 4096U);
  CHECK(engine.activeVoiceCount() == 2U);
  CHECK(engine.stats().transitionHits == 0U);
}

TEST_CASE("MIDI vibrato timbre and pressure persist for new voices and reset per channel") {
  using namespace seam;
  for (const auto message : {std::array<std::uint8_t, 3>{0xb0, 1, 127},
      std::array<std::uint8_t, 3>{0xb0, 74, 127}, std::array<std::uint8_t, 3>{0xd0, 0, 0}}) {
    const auto run = [&](unsigned scenario) {
      live_voice::VoiceEngine engine;
      CHECK(engine.publishResource(phase12c::makeEmbeddedHumanResource()));
      const auto midi = [&](std::array<std::uint8_t, 3> bytes) {
        engine.dispatch({.type = phase12c::EventType::Midi1, .midi = bytes});
      };
      if (scenario == 1U || scenario == 3U || scenario == 4U) midi(message);
      if (scenario == 3U) midi({0xb0, 121, 0});
      if (scenario == 4U) engine.reset();
      if (scenario == 5U) {
        auto otherChannel = message;
        otherChannel[0] |= 1U;
        midi(otherChannel);
      }
      midi({0x90, 60, 100});
      if (scenario == 2U) midi(message);
      if (scenario == 6U) { midi(message); midi({0xb0, 121, 0}); }
      std::array<float, 4096> pcm{};
      float* outputs[]{pcm.data()};
      engine.process({}, outputs, 1U, 4096U);
      CHECK(engine.activeVoiceCount() == 1U);
      return pcm;
    };
    const auto baseline = run(0U);
    const auto beforeNote = run(1U);
    CHECK(beforeNote == run(2U));
    CHECK(baseline == run(3U));
    CHECK(baseline == run(4U));
    CHECK(baseline == run(5U));
    CHECK(baseline == run(6U));
    double difference = 0.0;
    for (std::size_t frame = 0U; frame < baseline.size(); ++frame) {
      CHECK(std::isfinite(beforeNote[frame]));
      difference += std::abs(static_cast<double>(beforeNote[frame] - baseline[frame]));
    }
    CHECK(difference > 1.0e-3);
  }
}
