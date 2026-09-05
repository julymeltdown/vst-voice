#include "test_framework.hpp"

#include "seam/domain/dynamics_automation.hpp"
#include "seam/domain/note_vibrato.hpp"
#include "seam/domain/performance_intent.hpp"
#include "seam/formats/project_json.hpp"

#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

using seam::domain::DynamicsAutomation;
using seam::domain::DynamicsAutomationPoint;
using seam::domain::NoteVibrato;
using seam::time::Tick;

TEST_CASE("project note keys are range checked before narrowing to a MIDI byte") {
  const seam::formats::ProjectJsonCodec codec;
  const auto loaded = codec.load(std::filesystem::path{SEAM_PERFORMANCE_SOURCE_ROOT} /
                                "docs/phase3/evidence/phase3-demo.seam.json");
  CHECK(loaded);
  const auto encoded = codec.encode(loaded.value());
  CHECK(encoded);
  auto document = seam::formats::parseJson(encoded.value());
  CHECK(document);
  auto& note = document.value().asObject().at("vocalTracks").asArray().front()
                   .asObject().at("regions").asArray().front()
                   .asObject().at("notes").asArray().front();
  for (const std::int64_t invalid : {128, 256, 316, -196}) {
    note.asObject().at("midiKey") = seam::formats::JsonValue{invalid};
    CHECK(!codec.decode(seam::formats::stringifyJson(document.value())));
  }
  for (const std::int64_t valid : {0, 60, 127}) {
    note.asObject().at("midiKey") = seam::formats::JsonValue{valid};
    const auto decoded = codec.decode(seam::formats::stringifyJson(document.value()));
    CHECK(decoded);
    CHECK(decoded.value().vocalTracks().front().regions.front().notes.front().midiKey == valid);
  }
}

TEST_CASE("canonical note spans reject integer overflow before computing their end") {
  const auto maximum = std::numeric_limits<std::int64_t>::max();
  seam::domain::Note note{
      .id = seam::domain::NoteId{1U},
      .startTick = Tick{maximum - 240},
      .durationTick = Tick{240},
      .lyricTokenId = seam::domain::LyricTokenId{2U},
  };
  CHECK(note.validate());
  CHECK(note.endTick() == Tick{maximum});
  note.startTick = Tick{maximum - 239};
  CHECK(!note.validate());
}

TEST_CASE("canonical region spans reject integer overflow before computing their end") {
  const auto maximum = std::numeric_limits<std::int64_t>::max();
  seam::domain::VocalRegion region;
  region.id = seam::domain::RegionId{3U};
  region.startTick = Tick{maximum - 240};
  region.durationTick = Tick{240};
  CHECK(region.validate());
  region.startTick = Tick{maximum - 239};
  CHECK(!region.validate());
}

TEST_CASE("manual performance ranges are half open and reject invalid spans") {
  const seam::domain::PerformanceTimeRange range{Tick{120}, Tick{480}};
  CHECK(range.validate());
  CHECK(!range.contains(Tick{119}));
  CHECK(range.contains(Tick{120}));
  CHECK(range.contains(Tick{479}));
  CHECK(!range.contains(Tick{480}));
  CHECK(!seam::domain::PerformanceTimeRange(Tick{-1}, Tick{480}).validate());
  CHECK(!seam::domain::PerformanceTimeRange(Tick{480}, Tick{480}).validate());
  CHECK(!seam::domain::PerformanceTimeRange(Tick{481}, Tick{480}).validate());
}

TEST_CASE("manual ownership targets a note identity or a half open time range") {
  seam::domain::ManualPerformanceOwnership ownership;
  CHECK(!ownership.validate());
  ownership.scope = seam::domain::NoteId{7U};
  CHECK(ownership.validate());
  CHECK(ownership.appliesTo(seam::domain::NoteId{7U}, Tick{500}));
  CHECK(!ownership.appliesTo(seam::domain::NoteId{8U}, Tick{500}));
  ownership.scope = seam::domain::PerformanceTimeRange{Tick{120}, Tick{480}};
  CHECK(ownership.validate());
  CHECK(ownership.appliesTo(seam::domain::NoteId{8U}, Tick{120}));
  CHECK(!ownership.appliesTo(seam::domain::NoteId{8U}, Tick{480}));
}

TEST_CASE("only pitch permits additive manual ownership and unknown modes fail") {
  seam::domain::ManualPerformanceOwnership ownership;
  ownership.scope = seam::domain::NoteId{7U};
  ownership.mode = seam::domain::ManualPerformanceMode::PitchOffset;
  CHECK(ownership.validate());
  ownership.channel = seam::domain::PerformanceChannel::Dynamics;
  CHECK(!ownership.validate());
  ownership.mode = seam::domain::ManualPerformanceMode::Replace;
  CHECK(ownership.validate());
  ownership.channel = static_cast<seam::domain::PerformanceChannel>(255);
  CHECK(!ownership.validate());
  ownership.channel = seam::domain::PerformanceChannel::Pitch;
  ownership.mode = static_cast<seam::domain::ManualPerformanceMode>(255);
  CHECK(!ownership.validate());
}

TEST_CASE("proposed performance acceptance rejects every stale captured revision") {
  const seam::domain::PerformanceRevision captured{11U, 12U, 13U};
  CHECK(seam::domain::validatePerformanceAcceptanceRevision(captured, captured));
  for (const auto changed : {seam::domain::PerformanceRevision{12U, 12U, 13U},
                             seam::domain::PerformanceRevision{11U, 13U, 13U},
                             seam::domain::PerformanceRevision{11U, 12U, 14U}}) {
    const auto result = seam::domain::validatePerformanceAcceptanceRevision(captured, changed);
    CHECK(!result);
    CHECK(result.error().code == seam::core::ErrorCode::Conflict);
  }
}

TEST_CASE("historical schema two and three vocal files retain their actual score data") {
  const seam::formats::ProjectJsonCodec codec;
  const auto root = std::filesystem::path{SEAM_PERFORMANCE_SOURCE_ROOT};
  for (const auto path : {"docs/phase2/evidence/phase2-demo.seam.json",
                          "docs/phase3/evidence/phase3-demo.seam.json"}) {
    const auto loaded = codec.load(root / path);
    CHECK(loaded);
    CHECK(loaded.value().noteCount() == 4U);
    const auto& region = loaded.value().vocalTracks().front().regions.front();
    CHECK(region.notes.front().startTick == Tick{1920});
    CHECK(region.notes.front().midiKey == 64U);
    CHECK(loaded.value().settings().hostStartOffsetTick == Tick{0});
    const auto encoded = codec.encode(loaded.value());
    CHECK(encoded);
    const auto roundTrip = codec.decode(encoded.value());
    CHECK(roundTrip);
    CHECK(roundTrip.value() == loaded.value());
  }
}

TEST_CASE("note vibrato defaults are valid and disabled") {
  const NoteVibrato vibrato;
  CHECK(!vibrato.enabled);
  CHECK(vibrato.validate());
}

TEST_CASE("note vibrato validates units and fade span even while disabled") {
  NoteVibrato vibrato;
  vibrato.startFraction = 1.0F;
  vibrato.depthCents = 200.0F;
  vibrato.periodMilliseconds = 5.0F;
  vibrato.fadeInFraction = 0.75F;
  vibrato.fadeOutFraction = 0.25F;
  CHECK(vibrato.validate());
  vibrato.fadeOutFraction = 0.5F;
  CHECK(!vibrato.validate());
  vibrato.fadeOutFraction = 0.25F;
  vibrato.periodMilliseconds = 501.0F;
  CHECK(!vibrato.validate());
  vibrato.periodMilliseconds = 500.0F;
  vibrato.phaseTurns = 1.0F;
  CHECK(!vibrato.validate());
}

TEST_CASE("note vibrato rejects nonfinite values in every parameter") {
  const std::vector<float NoteVibrato::*> parameters{
      &NoteVibrato::startFraction, &NoteVibrato::fadeInFraction,
      &NoteVibrato::fadeOutFraction, &NoteVibrato::depthCents,
      &NoteVibrato::periodMilliseconds, &NoteVibrato::phaseTurns};
  for (const auto parameter : parameters) {
    for (const auto value : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(), -1.0F}) {
      NoteVibrato vibrato;
      vibrato.*parameter = value;
      CHECK(!vibrato.validate());
    }
  }
}

TEST_CASE("typed dynamics has unity default and linear endpoint hold") {
  DynamicsAutomation curve;
  CHECK(curve.validate());
  CHECK_NEAR(curve.valueAt(Tick{0}), 1.0, 1.0e-6);
  CHECK(curve.upsert({Tick{240}, 0.0F}));
  CHECK(curve.upsert({Tick{720}, 2.0F}));
  CHECK_NEAR(curve.valueAt(Tick{-1}), 0.0, 1.0e-6);
  CHECK_NEAR(curve.valueAt(Tick{480}), 1.0, 1.0e-6);
  CHECK_NEAR(curve.valueAt(Tick{960}), 2.0, 1.0e-6);
}

TEST_CASE("typed dynamics edits preserve sorted identity and validate before mutation") {
  DynamicsAutomation curve;
  CHECK(curve.upsert({Tick{720}, 2.0F}));
  CHECK(curve.upsert({Tick{240}, 0.0F}));
  CHECK(curve.upsert({Tick{240}, 1.0F}));
  CHECK(curve.points().size() == 2U);
  CHECK(curve.points().front().tick == Tick{240});
  const auto before = curve;
  CHECK(!curve.upsert({Tick{120}, -0.1F}));
  CHECK(!curve.upsert({Tick{120}, 4.0F}));
  CHECK(!curve.upsert({Tick{-1}, 1.0F}));
  CHECK(!curve.upsert({Tick{120}, std::numeric_limits<float>::quiet_NaN()}));
  CHECK(curve == before);
  CHECK(curve.erase(Tick{240}));
  CHECK(!curve.erase(Tick{240}));
  CHECK(curve.erase(Tick{720}));
  CHECK_NEAR(curve.valueAt(Tick{720}), 1.0, 1.0e-6);
}

TEST_CASE("typed dynamics imported points reject unsorted duplicates and excessive curves") {
  DynamicsAutomation curve;
  CHECK(curve.upsert({Tick{960}, seam::domain::kMaximumDynamicsGain}));
  const auto before = curve;
  CHECK(!curve.replacePoints({{Tick{960}, 1.0F}, {Tick{0}, 0.0F}}));
  CHECK(!curve.replacePoints({{Tick{960}, 1.0F}, {Tick{960}, 0.0F}}));
  CHECK(curve == before);
  std::vector<DynamicsAutomationPoint> points;
  for (std::size_t index = 0; index <= seam::domain::kMaximumDynamicsPoints; ++index) {
    points.push_back({Tick{static_cast<std::int64_t>(index)}, 1.0F});
  }
  CHECK(!curve.replacePoints(points));
  CHECK(curve == before);
  points.pop_back();
  CHECK(curve.replacePoints(points));
  CHECK(!curve.upsert({Tick{static_cast<std::int64_t>(points.size())}, 1.0F}));
  CHECK(curve.upsert({Tick{0}, 0.5F}));
  CHECK(curve.points().size() == points.size());
}
