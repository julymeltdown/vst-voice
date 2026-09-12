#include "project_region_performance_json.hpp"

#include <array>
#include <charconv>
#include <stdexcept>

namespace seam::formats::detail {
namespace {

using Object = JsonValue::Object;
using Array = JsonValue::Array;
using namespace domain;

constexpr std::array<std::string_view, 12U> channels{
    "pitch", "timing", "dynamics", "breathiness", "tension", "airiness",
    "formant", "gender", "style-blend", "growl", "attack", "release"};
constexpr std::array<std::string_view, 3U> resources{"sample", "procedural", "neural"};

std::string hex(std::uint64_t value) {
  std::array<char, 16U> buffer{};
  const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, 16);
  return {buffer.data(), converted.ptr};
}

const JsonValue& field(const JsonValue& object, std::string_view key) {
  const auto* found = object.find(key);
  if (found == nullptr) throw std::invalid_argument("Missing performance field: " + std::string{key});
  return *found;
}

void objectSize(const JsonValue& value, std::size_t size) {
  if (!value.isObject() || value.asObject().size() != size) {
    throw std::invalid_argument("Performance object has missing or unknown fields");
  }
}

const Array& array(const JsonValue& value, std::size_t maximum) {
  if (!value.isArray() || value.asArray().size() > maximum) {
    throw std::invalid_argument("Performance collection exceeds its bound or is not an array");
  }
  return value.asArray();
}

const std::string& string(const JsonValue& value, std::size_t maximum = 1024U) {
  if (!value.isString() || value.asString().size() > maximum) {
    throw std::invalid_argument("Performance identity must be a bounded string");
  }
  return value.asString();
}

std::uint64_t counter(const JsonValue& value) {
  const auto& text = string(value, 16U);
  std::uint64_t result{};
  const auto converted = std::from_chars(text.data(), text.data() + text.size(), result, 16);
  if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size() || hex(result) != text) {
    throw std::invalid_argument("Performance counters require canonical unsigned hexadecimal strings");
  }
  return result;
}

time::Tick tick(const JsonValue& value) {
  if (!value.isInteger()) throw std::invalid_argument("Performance tick must be an integer");
  return time::Tick{value.asInt64()};
}

template <std::size_t N>
std::size_t choice(const JsonValue& value, const std::array<std::string_view, N>& names) {
  const auto& text = string(value, 64U);
  for (std::size_t index = 0U; index < names.size(); ++index) {
    if (text == names[index]) return index;
  }
  throw std::invalid_argument("Unknown performance discriminant");
}

JsonValue encodeRevision(PerformanceRevision revision) {
  return Object{{"musical", JsonValue{hex(revision.musical)}},
                {"pronunciation", JsonValue{hex(revision.pronunciation)}},
                {"ownership", JsonValue{hex(revision.ownership)}}};
}

PerformanceRevision decodeRevision(const JsonValue& value) {
  objectSize(value, 3U);
  return {counter(field(value, "musical")), counter(field(value, "pronunciation")),
          counter(field(value, "ownership"))};
}

JsonValue encodeRange(PerformanceTimeRange range) {
  return Object{{"startTick", JsonValue{range.startTick.value()}},
                {"endTick", JsonValue{range.endTick.value()}}};
}

PerformanceTimeRange decodeRange(const JsonValue& value) {
  objectSize(value, 2U);
  return {tick(field(value, "startTick")), tick(field(value, "endTick"))};
}

JsonValue encodeScope(const PerformanceScope& scope) {
  if (const auto* note = std::get_if<NoteId>(&scope)) {
    return Object{{"kind", JsonValue{std::string{"note"}}}, {"noteId", JsonValue{hex(note->value())}}};
  }
  auto object = encodeRange(std::get<PerformanceTimeRange>(scope));
  object.asObject().emplace("kind", JsonValue{std::string{"range"}});
  return object;
}

PerformanceScope decodeScope(const JsonValue& value) {
  const auto& kind = string(field(value, "kind"), 8U);
  if (kind == "note") {
    objectSize(value, 2U);
    return NoteId{counter(field(value, "noteId"))};
  }
  if (kind == "range") {
    objectSize(value, 3U);
    return PerformanceTimeRange{tick(field(value, "startTick")), tick(field(value, "endTick"))};
  }
  throw std::invalid_argument("Unknown performance scope kind");
}

JsonValue encodePronunciation(const PronunciationIdentity& value) {
  const auto language = value.language == Language::Japanese ? "ja" :
      value.language == Language::English ? "en" : "ko";
  return Object{{"language", JsonValue{std::string{language}}},
      {"resolverId", JsonValue{value.resolverId}}, {"resolverVersion", JsonValue{value.resolverVersion}},
      {"resourceHash", JsonValue{value.resourceHash}}, {"inputHash", JsonValue{value.inputHash}},
      {"sequenceHash", JsonValue{value.sequenceHash}}};
}

PronunciationIdentity decodePronunciation(const JsonValue& value) {
  objectSize(value, 6U);
  constexpr std::array<std::string_view, 3U> names{"ja", "en", "ko"};
  constexpr std::array languages{Language::Japanese, Language::English, Language::Korean};
  return {languages[choice(field(value, "language"), names)],
      string(field(value, "resolverId")), string(field(value, "resolverVersion"), 128U),
      string(field(value, "resourceHash"), 64U), string(field(value, "inputHash"), 64U),
      string(field(value, "sequenceHash"), 64U)};
}

JsonValue encodeResource(const SingerResourceIdentity& value) {
  return Object{{"kind", JsonValue{std::string{resources[static_cast<std::size_t>(value.kind)]}}},
      {"id", JsonValue{value.id}}, {"version", JsonValue{value.version}},
      {"contentHash", JsonValue{value.contentHash}}};
}

SingerResourceIdentity decodeResource(const JsonValue& value) {
  objectSize(value, 4U);
  return {static_cast<SingerResourceKind>(choice(field(value, "kind"), resources)),
      string(field(value, "id")), string(field(value, "version"), 128U),
      string(field(value, "contentHash"), 64U)};
}

JsonValue encodeTake(const PerformanceTake& take) {
  Array lanes;
  for (const auto& lane : take.lanes) {
    Array points;
    for (const auto& point : lane.points) {
      points.emplace_back(Object{{"tick", JsonValue{point.tick.value()}},
          {"value", point.value.has_value() ? JsonValue{*point.value} : JsonValue{nullptr}}});
    }
    lanes.emplace_back(Object{
        {"channel", JsonValue{std::string{channels[static_cast<std::size_t>(lane.channel)]}}},
        {"unit", JsonValue{std::string{performanceChannelUnit(lane.channel)}}},
        {"points", JsonValue{std::move(points)}}});
  }
  return Object{{"id", JsonValue{take.id}}, {"sourceRegionId", JsonValue{hex(take.sourceRegionId.value())}},
      {"capturedRevision", encodeRevision(take.capturedRevision)}, {"resource", encodeResource(take.resource)},
      {"pronunciation", encodePronunciation(take.pronunciation)}, {"generatorId", JsonValue{take.generatorId}},
      {"generatorVersion", JsonValue{take.generatorVersion}}, {"seed", JsonValue{hex(take.seed)}},
      {"range", encodeRange(take.range)},
      {"state", JsonValue{std::string{take.state == PerformanceProposalState::Proposed ? "proposed" : "rejected"}}},
      {"lanes", JsonValue{std::move(lanes)}}};
}

PerformanceTake decodeTake(const JsonValue& value, std::size_t& pointBudget) {
  objectSize(value, 11U);
  constexpr std::array<std::string_view, 2U> states{"proposed", "rejected"};
  PerformanceTake result{.id = string(field(value, "id"), 128U),
      .sourceRegionId = RegionId{counter(field(value, "sourceRegionId"))},
      .capturedRevision = decodeRevision(field(value, "capturedRevision")),
      .resource = decodeResource(field(value, "resource")),
      .pronunciation = decodePronunciation(field(value, "pronunciation")),
      .generatorId = string(field(value, "generatorId")),
      .generatorVersion = string(field(value, "generatorVersion"), 128U),
      .seed = counter(field(value, "seed")), .range = decodeRange(field(value, "range")),
      .state = static_cast<PerformanceProposalState>(choice(field(value, "state"), states))};
  for (const auto& item : array(field(value, "lanes"), channels.size())) {
    objectSize(item, 3U);
    PerformanceLane lane{.channel = static_cast<PerformanceChannel>(choice(field(item, "channel"), channels))};
    if (string(field(item, "unit"), 32U) != performanceChannelUnit(lane.channel)) {
      throw std::invalid_argument("Performance lane unit does not match its channel");
    }
    const auto& points = array(field(item, "points"), kMaximumPerformanceLanePoints);
    if (points.size() > pointBudget) throw std::invalid_argument("Performance point budget exceeded");
    pointBudget -= points.size();
    lane.points.reserve(points.size());
    for (const auto& point : points) {
      objectSize(point, 2U);
      const auto& sample = field(point, "value");
      if (!sample.isNumber() && !sample.isNull()) {
        throw std::invalid_argument("Performance value must be a number or explicit unvoiced null");
      }
      lane.points.push_back({tick(field(point, "tick")),
          sample.isNull() ? std::optional<double>{} : std::optional<double>{sample.asNumber()}});
    }
    result.lanes.push_back(std::move(lane));
  }
  const auto valid = result.validate();
  if (!valid) throw std::invalid_argument(valid.error().message);
  return result;
}

}

JsonValue encodeRegionPerformance(const domain::RegionPerformanceState& state) {
  Array ownership;
  for (const auto& entry : state.ownership) {
    ownership.emplace_back(Object{
        {"channel", JsonValue{std::string{channels[static_cast<std::size_t>(entry.channel)]}}},
        {"scope", encodeScope(entry.scope)}, {"revision", encodeRevision(entry.revision)},
        {"mode", JsonValue{std::string{entry.mode == ManualPerformanceMode::Replace ? "replace" : "pitch-offset"}}}});
  }
  Array takes;
  for (const auto& take : state.takes) takes.push_back(encodeTake(take));
  Array accepted;
  for (const auto& entry : state.accepted) {
    accepted.emplace_back(Object{{"takeId", JsonValue{entry.takeId}},
        {"channel", JsonValue{std::string{channels[static_cast<std::size_t>(entry.channel)]}}},
        {"scope", encodeScope(entry.scope)}, {"sourceTickOffset", JsonValue{entry.sourceTickOffset.value()}}});
  }
  return Object{{"revision", encodeRevision(state.revision)},
      {"pronunciation", state.pronunciation.has_value() ? encodePronunciation(*state.pronunciation) : JsonValue{nullptr}},
      {"ownership", JsonValue{std::move(ownership)}}, {"takes", JsonValue{std::move(takes)}},
      {"accepted", JsonValue{std::move(accepted)}}};
}

core::Result<domain::RegionPerformanceState> decodeRegionPerformance(const JsonValue* value) {
  try {
    if (value == nullptr) throw std::invalid_argument("Schema 8 requires region performance state");
    objectSize(*value, 5U);
    RegionPerformanceState result{.revision = decodeRevision(field(*value, "revision"))};
    const auto& pronunciation = field(*value, "pronunciation");
    if (!pronunciation.isNull()) result.pronunciation = decodePronunciation(pronunciation);
    constexpr std::array<std::string_view, 2U> modes{"replace", "pitch-offset"};
    for (const auto& entry : array(field(*value, "ownership"), kMaximumPerformanceOwnership)) {
      objectSize(entry, 4U);
      result.ownership.push_back({
          static_cast<PerformanceChannel>(choice(field(entry, "channel"), channels)),
          decodeScope(field(entry, "scope")),
          static_cast<ManualPerformanceMode>(choice(field(entry, "mode"), modes)),
          decodeRevision(field(entry, "revision"))});
    }
    auto pointBudget = kMaximumPerformanceStatePoints;
    for (const auto& take : array(field(*value, "takes"), kMaximumPerformanceTakes)) {
      result.takes.push_back(decodeTake(take, pointBudget));
    }
    for (const auto& entry : array(field(*value, "accepted"), kMaximumPerformanceSelections)) {
      objectSize(entry, 4U);
      result.accepted.push_back({string(field(entry, "takeId"), 128U),
          static_cast<PerformanceChannel>(choice(field(entry, "channel"), channels)),
          decodeScope(field(entry, "scope")), tick(field(entry, "sourceTickOffset"))});
    }
    return result;
  } catch (const std::exception& error) {
    return core::failure<domain::RegionPerformanceState>(core::ErrorCode::ParseError, error.what());
  }
}

}
