#include "seam/formats/project_json.hpp"

#include "seam/core/file_io.hpp"

#include <charconv>
#include <cmath>
#include <optional>
#include <sstream>
#include <system_error>

namespace seam::formats {
namespace {

using Object = JsonValue::Object;
using Array = JsonValue::Array;

core::Result<void> validateProjectPath(const std::filesystem::path& path,
                                       bool allowMissing) {
  if (path.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Project path is empty");
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory ||
      status.type() == std::filesystem::file_type::not_found) {
    return allowMissing
               ? core::success()
               : core::failure(core::ErrorCode::NotFound,
                               "Project file does not exist", path.string());
  }
  if (error) {
    return core::failure(core::ErrorCode::IoError,
                         "Unable to inspect project path", error.message());
  }
  if (status.type() == std::filesystem::file_type::symlink) {
    return core::failure(core::ErrorCode::Conflict,
                         "Project path cannot be a symbolic link",
                         path.string());
  }
  if (!std::filesystem::is_regular_file(status)) {
    return core::failure(core::ErrorCode::IoError,
                         "Project path is not a regular file", path.string());
  }
  return core::success();
}

JsonValue idValue(std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << value;
  return JsonValue{stream.str()};
}

template <typename Tag>
JsonValue idValue(core::Id<Tag> id) {
  return idValue(id.value());
}

core::Result<std::uint64_t> parseIdValue(const JsonValue& value, std::string_view field) {
  if (!value.isString()) {
    return core::failure<std::uint64_t>(core::ErrorCode::ParseError,
                                        std::string(field) + " must be a hexadecimal string");
  }
  std::uint64_t result = 0;
  const auto& text = value.asString();
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result, 16);
  if (error != std::errc{} || end != text.data() + text.size() || result == 0) {
    return core::failure<std::uint64_t>(core::ErrorCode::ParseError,
                                        std::string(field) + " is invalid");
  }
  return result;
}

template <typename Tag>
core::Result<core::Id<Tag>> parseId(const JsonValue& value, std::string_view field) {
  auto parsed = parseIdValue(value, field);
  if (!parsed) {
    return core::Result<core::Id<Tag>>{parsed.error()};
  }
  return core::Id<Tag>{parsed.value()};
}

core::Result<const JsonValue*> required(const JsonValue& object,
                                        std::string_view key,
                                        bool (*predicate)(const JsonValue&),
                                        std::string_view expected) {
  const auto* value = object.find(key);
  if (value == nullptr) {
    return core::failure<const JsonValue*>(core::ErrorCode::ParseError,
                                           "Missing required field: " + std::string(key));
  }
  if (!predicate(*value)) {
    return core::failure<const JsonValue*>(core::ErrorCode::ParseError,
                                           "Field '" + std::string(key) + "' must be " +
                                               std::string(expected));
  }
  return value;
}

bool isString(const JsonValue& value) { return value.isString(); }
bool isNumber(const JsonValue& value) { return value.isNumber(); }
bool isArray(const JsonValue& value) { return value.isArray(); }
bool isObject(const JsonValue& value) { return value.isObject(); }

std::string languageName(domain::Language language) {
  switch (language) {
    case domain::Language::Japanese: return "ja";
    case domain::Language::Korean: return "ko";
    case domain::Language::English: return "en";
    case domain::Language::Unspecified: return "und";
  }
  return "und";
}

domain::Language parseLanguage(std::string_view value) {
  if (value == "ja") return domain::Language::Japanese;
  if (value == "ko") return domain::Language::Korean;
  if (value == "en") return domain::Language::English;
  return domain::Language::Unspecified;
}

std::string articulationName(domain::NoteArticulation articulation) {
  switch (articulation) {
    case domain::NoteArticulation::Normal: return "normal";
    case domain::NoteArticulation::Legato: return "legato";
    case domain::NoteArticulation::Staccato: return "staccato";
  }
  return "normal";
}

domain::NoteArticulation parseArticulation(std::string_view value) {
  if (value == "legato") return domain::NoteArticulation::Legato;
  if (value == "staccato") return domain::NoteArticulation::Staccato;
  return domain::NoteArticulation::Normal;
}

std::string characterModeName(domain::CharacterDisplayMode mode) {
  switch (mode) {
    case domain::CharacterDisplayMode::Full: return "full";
    case domain::CharacterDisplayMode::Minimal: return "minimal";
    case domain::CharacterDisplayMode::Off: return "off";
  }
  return "minimal";
}

domain::CharacterDisplayMode parseCharacterMode(std::string_view value) {
  if (value == "full") return domain::CharacterDisplayMode::Full;
  if (value == "off") return domain::CharacterDisplayMode::Off;
  return domain::CharacterDisplayMode::Minimal;
}

domain::MediaOwnership parseMediaOwnership(std::string_view value) {
  return value == "project-copy" ? domain::MediaOwnership::ProjectCopy
                                  : domain::MediaOwnership::ExternalReference;
}

bool validMediaOwnership(std::string_view value) {
  return value == "project-copy" || value == "external-reference";
}

JsonValue encodeRoutingMatrix(const domain::RoutingMatrix& matrix) {
  Array gains;
  gains.reserve(matrix.gains.size());
  for (const auto gain : matrix.gains) {
    gains.emplace_back(static_cast<double>(gain));
  }
  return JsonValue{Object{
      {"sourceChannels", JsonValue{static_cast<std::int64_t>(matrix.sourceChannels)}},
      {"destinationChannels", JsonValue{static_cast<std::int64_t>(matrix.destinationChannels)}},
      {"gains", JsonValue{std::move(gains)}},
  }};
}

core::Result<domain::RoutingMatrix> decodeRoutingMatrix(
    const JsonValue& value, std::string_view field) {
  if (!value.isObject()) {
    return core::failure<domain::RoutingMatrix>(core::ErrorCode::ParseError,
        std::string(field) + " must be an object");
  }
  const auto* sourceChannels = value.find("sourceChannels");
  const auto* destinationChannels = value.find("destinationChannels");
  const auto* gains = value.find("gains");
  if (sourceChannels == nullptr || destinationChannels == nullptr || gains == nullptr ||
      !sourceChannels->isNumber() || !destinationChannels->isNumber() || !gains->isArray()) {
    return core::failure<domain::RoutingMatrix>(core::ErrorCode::ParseError,
        std::string(field) + " fields are invalid");
  }
  const auto sourceValue = sourceChannels->asInt64();
  const auto destinationValue = destinationChannels->asInt64();
  if (sourceValue <= 0 || sourceValue > domain::kMaximumAudioChannels ||
      destinationValue <= 0 || destinationValue > domain::kMaximumAudioChannels) {
    return core::failure<domain::RoutingMatrix>(core::ErrorCode::ParseError,
        std::string(field) + " channel count is invalid");
  }
  domain::RoutingMatrix matrix{
      .sourceChannels = static_cast<std::uint8_t>(sourceValue),
      .destinationChannels = static_cast<std::uint8_t>(destinationValue),
      .gains = {},
  };
  matrix.gains.reserve(gains->asArray().size());
  for (const auto& gain : gains->asArray()) {
    if (!gain.isNumber()) {
      return core::failure<domain::RoutingMatrix>(core::ErrorCode::ParseError,
          std::string(field) + " contains a non-numeric gain");
    }
    matrix.gains.push_back(static_cast<float>(gain.asNumber()));
  }
  const auto validation = matrix.validate();
  if (!validation) return core::Result<domain::RoutingMatrix>{validation.error()};
  return matrix;
}

JsonValue encodeTrackOutputRoute(const domain::TrackOutputRoute& route) {
  return JsonValue{Object{
      {"busId", idValue(route.bus)},
      {"matrix", encodeRoutingMatrix(route.matrix)},
  }};
}

core::Result<domain::TrackOutputRoute> decodeTrackOutputRoute(
    const JsonValue& value, std::string_view field) {
  if (!value.isObject()) {
    return core::failure<domain::TrackOutputRoute>(core::ErrorCode::ParseError,
        std::string(field) + " must be an object");
  }
  const auto* busId = value.find("busId");
  const auto* matrix = value.find("matrix");
  if (busId == nullptr || matrix == nullptr) {
    return core::failure<domain::TrackOutputRoute>(core::ErrorCode::ParseError,
        std::string(field) + " is incomplete");
  }
  auto parsedBus = parseId<domain::BusTag>(*busId, "outputRoute.busId");
  if (!parsedBus) return core::Result<domain::TrackOutputRoute>{parsedBus.error()};
  auto parsedMatrix = decodeRoutingMatrix(*matrix, "outputRoute.matrix");
  if (!parsedMatrix) return core::Result<domain::TrackOutputRoute>{parsedMatrix.error()};
  domain::TrackOutputRoute result{
      .bus = parsedBus.value(),
      .matrix = std::move(parsedMatrix.value()),
  };
  const auto validation = result.validate();
  if (!validation) return core::Result<domain::TrackOutputRoute>{validation.error()};
  return result;
}

JsonValue encodeRouting(const domain::ProjectRouting& routing) {
  Array buses;
  for (const auto& bus : routing.buses) {
    buses.emplace_back(Object{
        {"id", idValue(bus.id)},
        {"name", JsonValue{bus.name}},
        {"channelCount", JsonValue{static_cast<std::int64_t>(bus.channelCount)}},
        {"gainDb", JsonValue{static_cast<double>(bus.gainDb)}},
        {"muted", JsonValue{bus.muted}},
        {"solo", JsonValue{bus.solo}},
    });
  }
  Array sends;
  for (const auto& send : routing.sends) {
    sends.emplace_back(Object{
        {"sourceBus", idValue(send.sourceBus)},
        {"destinationBus", idValue(send.destinationBus)},
        {"matrix", encodeRoutingMatrix(send.matrix)},
        {"gainDb", JsonValue{static_cast<double>(send.gainDb)}},
        {"enabled", JsonValue{send.enabled}},
    });
  }
  Array deviceRoutes;
  for (const auto& route : routing.deviceRoutes) {
    deviceRoutes.emplace_back(Object{
        {"sourceBus", idValue(route.sourceBus)},
        {"matrix", encodeRoutingMatrix(route.matrix)},
        {"gainDb", JsonValue{static_cast<double>(route.gainDb)}},
        {"enabled", JsonValue{route.enabled}},
    });
  }
  return JsonValue{Object{
      {"deviceOutputChannels", JsonValue{static_cast<std::int64_t>(routing.deviceOutputChannels)}},
      {"masterBus", idValue(routing.masterBus)},
      {"buses", JsonValue{std::move(buses)}},
      {"sends", JsonValue{std::move(sends)}},
      {"deviceRoutes", JsonValue{std::move(deviceRoutes)}},
  }};
}

core::Result<domain::ProjectRouting> decodeRouting(const JsonValue& value) {
  if (!value.isObject()) {
    return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                 "Project routing must be an object");
  }
  const auto* deviceChannels = value.find("deviceOutputChannels");
  const auto* masterBus = value.find("masterBus");
  const auto* buses = value.find("buses");
  const auto* sends = value.find("sends");
  const auto* deviceRoutes = value.find("deviceRoutes");
  if (deviceChannels == nullptr || masterBus == nullptr || buses == nullptr ||
      sends == nullptr || deviceRoutes == nullptr || !deviceChannels->isNumber() ||
      !masterBus->isString() || !buses->isArray() || !sends->isArray() ||
      !deviceRoutes->isArray()) {
    return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                 "Project routing fields are invalid");
  }
  const auto channelValue = deviceChannels->asInt64();
  if (channelValue <= 0 || channelValue > domain::kMaximumAudioChannels) {
    return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                 "Device output channel count is invalid");
  }
  auto parsedMaster = parseId<domain::BusTag>(*masterBus, "routing.masterBus");
  if (!parsedMaster) return core::Result<domain::ProjectRouting>{parsedMaster.error()};

  domain::ProjectRouting routing;
  routing.deviceOutputChannels = static_cast<std::uint8_t>(channelValue);
  routing.masterBus = parsedMaster.value();
  routing.buses.clear();
  routing.sends.clear();
  routing.deviceRoutes.clear();

  for (const auto& busValue : buses->asArray()) {
    if (!busValue.isObject()) {
      return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                   "Audio bus must be an object");
    }
    const auto* id = busValue.find("id");
    const auto* name = busValue.find("name");
    const auto* channels = busValue.find("channelCount");
    const auto* gainDb = busValue.find("gainDb");
    const auto* muted = busValue.find("muted");
    const auto* solo = busValue.find("solo");
    if (id == nullptr || name == nullptr || channels == nullptr || gainDb == nullptr ||
        muted == nullptr || solo == nullptr || !id->isString() || !name->isString() ||
        !channels->isNumber() || !gainDb->isNumber() || !muted->isBool() || !solo->isBool()) {
      return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                   "Audio bus fields are invalid");
    }
    auto parsedId = parseId<domain::BusTag>(*id, "routing.bus.id");
    if (!parsedId) return core::Result<domain::ProjectRouting>{parsedId.error()};
    const auto count = channels->asInt64();
    if (count <= 0 || count > domain::kMaximumAudioChannels) {
      return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                   "Audio bus channel count is invalid");
    }
    routing.buses.push_back(domain::AudioBus{
        .id = parsedId.value(),
        .name = name->asString(),
        .channelCount = static_cast<std::uint8_t>(count),
        .gainDb = static_cast<float>(gainDb->asNumber()),
        .muted = muted->asBool(),
        .solo = solo->asBool(),
    });
  }

  for (const auto& sendValue : sends->asArray()) {
    if (!sendValue.isObject()) {
      return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                   "Bus send must be an object");
    }
    const auto* source = sendValue.find("sourceBus");
    const auto* destination = sendValue.find("destinationBus");
    const auto* matrix = sendValue.find("matrix");
    const auto* gainDb = sendValue.find("gainDb");
    const auto* enabled = sendValue.find("enabled");
    if (source == nullptr || destination == nullptr || matrix == nullptr ||
        gainDb == nullptr || enabled == nullptr || !source->isString() ||
        !destination->isString() || !gainDb->isNumber() || !enabled->isBool()) {
      return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                   "Bus send fields are invalid");
    }
    auto parsedSource = parseId<domain::BusTag>(*source, "routing.send.sourceBus");
    auto parsedDestination = parseId<domain::BusTag>(*destination, "routing.send.destinationBus");
    auto parsedMatrix = decodeRoutingMatrix(*matrix, "routing.send.matrix");
    if (!parsedSource) return core::Result<domain::ProjectRouting>{parsedSource.error()};
    if (!parsedDestination) return core::Result<domain::ProjectRouting>{parsedDestination.error()};
    if (!parsedMatrix) return core::Result<domain::ProjectRouting>{parsedMatrix.error()};
    routing.sends.push_back(domain::BusSend{
        .sourceBus = parsedSource.value(),
        .destinationBus = parsedDestination.value(),
        .matrix = std::move(parsedMatrix.value()),
        .gainDb = static_cast<float>(gainDb->asNumber()),
        .enabled = enabled->asBool(),
    });
  }

  for (const auto& routeValue : deviceRoutes->asArray()) {
    if (!routeValue.isObject()) {
      return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                   "Device route must be an object");
    }
    const auto* source = routeValue.find("sourceBus");
    const auto* matrix = routeValue.find("matrix");
    const auto* gainDb = routeValue.find("gainDb");
    const auto* enabled = routeValue.find("enabled");
    if (source == nullptr || matrix == nullptr || gainDb == nullptr || enabled == nullptr ||
        !source->isString() || !gainDb->isNumber() || !enabled->isBool()) {
      return core::failure<domain::ProjectRouting>(core::ErrorCode::ParseError,
                                                   "Device route fields are invalid");
    }
    auto parsedSource = parseId<domain::BusTag>(*source, "routing.deviceRoute.sourceBus");
    auto parsedMatrix = decodeRoutingMatrix(*matrix, "routing.deviceRoute.matrix");
    if (!parsedSource) return core::Result<domain::ProjectRouting>{parsedSource.error()};
    if (!parsedMatrix) return core::Result<domain::ProjectRouting>{parsedMatrix.error()};
    routing.deviceRoutes.push_back(domain::DeviceOutputRoute{
        .sourceBus = parsedSource.value(),
        .matrix = std::move(parsedMatrix.value()),
        .gainDb = static_cast<float>(gainDb->asNumber()),
        .enabled = enabled->asBool(),
    });
  }
  const auto validation = routing.validate();
  if (!validation) return core::Result<domain::ProjectRouting>{validation.error()};
  return routing;
}

JsonValue encodeTempo(const time::TempoMap& map) {
  Array events;
  for (const auto& event : map.events()) {
    events.emplace_back(Object{{"tick", JsonValue{event.tick.value()}},
                               {"bpm", JsonValue{event.bpm}}});
  }
  return JsonValue{std::move(events)};
}

JsonValue encodeMeter(const time::MeterMap& map) {
  Array events;
  for (const auto& event : map.events()) {
    events.emplace_back(Object{{"tick", JsonValue{event.tick.value()}},
                               {"numerator", JsonValue{static_cast<std::int64_t>(event.numerator)}},
                               {"denominator", JsonValue{static_cast<std::int64_t>(event.denominator)}}});
  }
  return JsonValue{std::move(events)};
}

JsonValue encodeProject(const domain::Project& project) {
  Array vocalTracks;
  for (const auto& track : project.vocalTracks()) {
    Array regions;
    for (const auto& region : track.regions) {
      Array lyrics;
      for (const auto& lyric : region.lyrics) {
        lyrics.emplace_back(Object{{"id", idValue(lyric.id)},
                                   {"surface", JsonValue{domain::toUtf8(lyric.surface)}},
                                   {"language", JsonValue{languageName(lyric.language)}}});
      }
      Array notes;
      for (const auto& note : region.notes) {
        Object noteObject{{"id", idValue(note.id)},
                          {"startTick", JsonValue{note.startTick.value()}},
                          {"durationTick", JsonValue{note.durationTick.value()}},
                          {"midiKey", JsonValue{static_cast<std::int64_t>(note.midiKey)}},
                          {"lyricId", idValue(note.lyricTokenId)},
                          {"articulation", JsonValue{articulationName(note.articulation)}}};
        if (note.slurGroup.has_value()) {
          noteObject.emplace("slurGroup", idValue(*note.slurGroup));
        }
        notes.emplace_back(std::move(noteObject));
      }
      Array phonemeOverrides;
      for (const auto& overrideValue : region.phonemeOverrides) {
        Object overrideObject{{"noteId", idValue(overrideValue.key.noteId)},
                              {"ordinal", JsonValue{static_cast<std::int64_t>(
                                  overrideValue.key.ordinal)}},
                              {"locked", JsonValue{overrideValue.locked}}};
        if (overrideValue.symbol.has_value()) {
          overrideObject.emplace("symbol", JsonValue{*overrideValue.symbol});
        }
        if (overrideValue.timing.startOffset.has_value()) {
          overrideObject.emplace("startOffsetUs",
                                 JsonValue{*overrideValue.timing.startOffset});
        }
        if (overrideValue.timing.endOffset.has_value()) {
          overrideObject.emplace("endOffsetUs",
                                 JsonValue{*overrideValue.timing.endOffset});
        }
        phonemeOverrides.emplace_back(std::move(overrideObject));
      }
      Array unitSelectionOverrides;
      for (const auto& overrideValue : region.unitSelectionOverrides) {
        Object overrideObject{
            {"noteId", idValue(overrideValue.startKey.noteId)},
            {"ordinal", JsonValue{static_cast<std::int64_t>(
                overrideValue.startKey.ordinal)}},
            {"tokenCount", JsonValue{static_cast<std::int64_t>(
                overrideValue.tokenCount)}},
            {"unitId", JsonValue{overrideValue.unitId}},
            {"renderer", JsonValue{std::string(
                domain::unitRendererKindName(overrideValue.renderer))}},
            {"locked", JsonValue{overrideValue.locked}},
        };
        if (overrideValue.loopPrint.has_value()) {
          overrideObject.emplace("loopPrint", JsonValue{*overrideValue.loopPrint});
        }
        if (overrideValue.sourcePitchResidual.has_value()) {
          overrideObject.emplace("sourcePitchResidual",
                                 JsonValue{*overrideValue.sourcePitchResidual});
        }
        unitSelectionOverrides.emplace_back(std::move(overrideObject));
      }
      Array seamOverrides;
      for (const auto& overrideValue : region.seamOverrides) {
        Object seamObject{
            {"noteId", idValue(overrideValue.incomingStartKey.noteId)},
            {"ordinal", JsonValue{static_cast<std::int64_t>(
                overrideValue.incomingStartKey.ordinal)}},
            {"curve", JsonValue{std::string(domain::seamCurveName(overrideValue.curve))}},
            {"locked", JsonValue{overrideValue.locked}},
        };
        if (overrideValue.seamAmount.has_value()) {
          seamObject.emplace("seamAmount", JsonValue{static_cast<double>(
              *overrideValue.seamAmount)});
        }
        if (overrideValue.overlap.has_value()) {
          seamObject.emplace("overlapUs", JsonValue{*overrideValue.overlap});
        }
        if (overrideValue.phaseReset.has_value()) {
          seamObject.emplace("phaseReset", JsonValue{static_cast<double>(
              *overrideValue.phaseReset)});
        }
        if (overrideValue.envelopeBlend.has_value()) {
          seamObject.emplace("envelopeBlend", JsonValue{static_cast<double>(
              *overrideValue.envelopeBlend)});
        }
        seamOverrides.emplace_back(std::move(seamObject));
      }
      Array pitchAutomation;
      for (const auto& point : region.pitchAutomation.points()) {
        pitchAutomation.emplace_back(Object{
            {"tick", JsonValue{point.tick.value()}},
            {"cents", JsonValue{static_cast<double>(point.cents)}},
            {"interpolation", JsonValue{std::string(
                domain::curveInterpolationName(point.interpolation))}},
        });
      }
      regions.emplace_back(Object{{"id", idValue(region.id)},
                                  {"name", JsonValue{region.name}},
                                  {"startTick", JsonValue{region.startTick.value()}},
                                  {"durationTick", JsonValue{region.durationTick.value()}},
                                  {"lyrics", JsonValue{std::move(lyrics)}},
                                  {"notes", JsonValue{std::move(notes)}},
                                  {"phonemeOverrides", JsonValue{std::move(phonemeOverrides)}},
                                  {"unitSelectionOverrides",
                                   JsonValue{std::move(unitSelectionOverrides)}},
                                  {"seamOverrides", JsonValue{std::move(seamOverrides)}},
                                  {"pitchAutomation", JsonValue{std::move(pitchAutomation)}}});
    }
    vocalTracks.emplace_back(Object{
        {"id", idValue(track.id)},
        {"name", JsonValue{track.name}},
        {"voicebank", JsonValue{Object{{"id", JsonValue{track.voicebank.id}},
                                        {"version", JsonValue{track.voicebank.version}},
                                        {"contentHash", JsonValue{track.voicebank.contentHash}}}}},
        {"character", JsonValue{Object{{"id", JsonValue{track.character.id}},
                                        {"version", JsonValue{track.character.version}}}}},
        {"gainDb", JsonValue{static_cast<double>(track.gainDb)}},
        {"pan", JsonValue{static_cast<double>(track.pan)}},
        {"muted", JsonValue{track.muted}},
        {"solo", JsonValue{track.solo}},
        {"outputRoute", encodeTrackOutputRoute(track.outputRoute)},
        {"regions", JsonValue{std::move(regions)}}});
  }

  Array audioTracks;
  for (const auto& track : project.audioTracks()) {
    audioTracks.emplace_back(Object{{"id", idValue(track.id)},
                                    {"name", JsonValue{track.name}},
                                    {"mediaPath", JsonValue{track.mediaPath}},
                                    {"mediaHash", JsonValue{track.mediaHash}},
                                    {"mediaOwnership", JsonValue{std::string(
                                        domain::mediaOwnershipName(track.mediaOwnership))}},
                                    {"originalFilename", JsonValue{track.originalFilename}},
                                    {"sourceSampleRate", JsonValue{static_cast<std::int64_t>(
                                        track.sourceSampleRate)}},
                                    {"sourceChannels", JsonValue{static_cast<std::int64_t>(
                                        track.sourceChannels)}},
                                    {"sourceFrameCount", JsonValue{static_cast<std::int64_t>(
                                        track.sourceFrameCount)}},
                                    {"trimStartFrame", JsonValue{static_cast<std::int64_t>(
                                        track.trimStartFrame)}},
                                    {"trimEndFrame", track.trimEndFrame.has_value()
                                                         ? JsonValue{static_cast<std::int64_t>(*track.trimEndFrame)}
                                                         : JsonValue{nullptr}},
                                    {"startTick", JsonValue{track.startTick.value()}},
                                    {"gainDb", JsonValue{static_cast<double>(track.gainDb)}},
                                    {"pan", JsonValue{static_cast<double>(track.pan)}},
                                    {"muted", JsonValue{track.muted}},
                                    {"solo", JsonValue{track.solo}},
                                    {"outputRoute", encodeTrackOutputRoute(track.outputRoute)}});
  }

  return JsonValue{Object{
      {"formatId", JsonValue{"com.project-seam.project"}},
      {"schemaVersion", JsonValue{static_cast<std::int64_t>(ProjectJsonCodec::kSchemaVersion)}},
      {"projectId", idValue(project.id())},
      {"name", JsonValue{project.name()}},
      {"ppq", JsonValue{static_cast<std::int64_t>(project.ppq())}},
      {"tempoMap", encodeTempo(project.tempoMap())},
      {"meterMap", encodeMeter(project.meterMap())},
      {"settings", JsonValue{Object{
          {"sampleRate", JsonValue{project.settings().sampleRate}},
          {"characterDisplay", JsonValue{characterModeName(project.settings().characterDisplay)}},
          {"snapEnabled", JsonValue{project.settings().snapEnabled}},
          {"snapGrid", JsonValue{project.settings().snapGrid.value()}},
          {"hostStartOffsetTick",
           JsonValue{project.settings().hostStartOffsetTick.value()}}}}},
      {"routing", encodeRouting(project.routing())},
      {"vocalTracks", JsonValue{std::move(vocalTracks)}},
      {"audioTracks", JsonValue{std::move(audioTracks)}}}};
}

core::Result<domain::Project> decodeProject(const JsonValue& root) {
  if (!root.isObject()) {
    return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                          "Project JSON root must be an object");
  }
  const auto format = required(root, "formatId", isString, "a string");
  const auto schema = required(root, "schemaVersion", isNumber, "a number");
  const auto projectIdValue = required(root, "projectId", isString, "a string");
  const auto name = required(root, "name", isString, "a string");
  const auto ppqValue = required(root, "ppq", isNumber, "a number");
  const auto tempo = required(root, "tempoMap", isArray, "an array");
  const auto meter = required(root, "meterMap", isArray, "an array");
  const auto settings = required(root, "settings", isObject, "an object");
  const auto* routingValue = root.find("routing");
  const auto vocalTracks = required(root, "vocalTracks", isArray, "an array");
  const auto audioTracks = required(root, "audioTracks", isArray, "an array");
  if (!format || !schema || !projectIdValue || !name || !ppqValue || !tempo || !meter ||
      !settings || !vocalTracks || !audioTracks) {
    if (!format) return core::Result<domain::Project>{format.error()};
    if (!schema) return core::Result<domain::Project>{schema.error()};
    if (!projectIdValue) return core::Result<domain::Project>{projectIdValue.error()};
    if (!name) return core::Result<domain::Project>{name.error()};
    if (!ppqValue) return core::Result<domain::Project>{ppqValue.error()};
    if (!tempo) return core::Result<domain::Project>{tempo.error()};
    if (!meter) return core::Result<domain::Project>{meter.error()};
    if (!settings) return core::Result<domain::Project>{settings.error()};
    if (!vocalTracks) return core::Result<domain::Project>{vocalTracks.error()};
    return core::Result<domain::Project>{audioTracks.error()};
  }
  if (format.value()->asString() != "com.project-seam.project") {
    return core::failure<domain::Project>(core::ErrorCode::Unsupported,
                                          "Unsupported project format");
  }
  const auto schemaVersion = schema.value()->asInt64();
  if (schemaVersion < 1 || schemaVersion > ProjectJsonCodec::kSchemaVersion) {
    return core::failure<domain::Project>(core::ErrorCode::Unsupported,
                                          "Unsupported project schema version");
  }
  auto projectId = parseId<domain::ProjectTag>(*projectIdValue.value(), "projectId");
  if (!projectId) return core::Result<domain::Project>{projectId.error()};
  const auto ppq = ppqValue.value()->asInt64();
  if (ppq <= 0 || ppq > 32767) {
    return core::failure<domain::Project>(core::ErrorCode::ParseError, "Invalid PPQ");
  }
  domain::Project project{projectId.value(), name.value()->asString(), static_cast<time::Ppq>(ppq)};

  for (const auto& eventValue : tempo.value()->asArray()) {
    if (!eventValue.isObject()) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Tempo event must be an object");
    }
    const auto* tickValue = eventValue.find("tick");
    const auto* bpmValue = eventValue.find("bpm");
    if (tickValue == nullptr || bpmValue == nullptr || !tickValue->isNumber() || !bpmValue->isNumber()) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Tempo event fields are invalid");
    }
    const auto result = project.tempoMap().addOrReplace(time::Tick{tickValue->asInt64()},
                                                        bpmValue->asNumber());
    if (!result) return core::Result<domain::Project>{result.error()};
  }

  for (const auto& eventValue : meter.value()->asArray()) {
    if (!eventValue.isObject()) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Meter event must be an object");
    }
    const auto* tickValue = eventValue.find("tick");
    const auto* numeratorValue = eventValue.find("numerator");
    const auto* denominatorValue = eventValue.find("denominator");
    if (tickValue == nullptr || numeratorValue == nullptr || denominatorValue == nullptr ||
        !tickValue->isNumber() || !numeratorValue->isNumber() || !denominatorValue->isNumber()) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Meter event fields are invalid");
    }
    const auto result = project.meterMap().addOrReplace(
        time::Tick{tickValue->asInt64()},
        static_cast<std::uint8_t>(numeratorValue->asInt64()),
        static_cast<std::uint8_t>(denominatorValue->asInt64()));
    if (!result) return core::Result<domain::Project>{result.error()};
  }

  const auto* sampleRate = settings.value()->find("sampleRate");
  const auto* characterDisplay = settings.value()->find("characterDisplay");
  const auto* snapEnabled = settings.value()->find("snapEnabled");
  const auto* snapGrid = settings.value()->find("snapGrid");
  const auto* hostStartOffsetTick = settings.value()->find("hostStartOffsetTick");
  if (sampleRate == nullptr || characterDisplay == nullptr || snapEnabled == nullptr || snapGrid == nullptr ||
      !sampleRate->isNumber() || !characterDisplay->isString() || !snapEnabled->isBool() ||
      !snapGrid->isNumber()) {
    return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                          "Project settings are invalid");
  }
  if (schemaVersion >= 5 &&
      (hostStartOffsetTick == nullptr || !hostStartOffsetTick->isNumber())) {
    return core::failure<domain::Project>(
        core::ErrorCode::ParseError,
        "Schema 5 project is missing hostStartOffsetTick");
  }
  project.settings().sampleRate = sampleRate->asNumber();
  project.settings().characterDisplay = parseCharacterMode(characterDisplay->asString());
  project.settings().snapEnabled = snapEnabled->asBool();
  project.settings().snapGrid = time::Tick{snapGrid->asInt64()};
  project.settings().hostStartOffsetTick =
      schemaVersion >= 5
          ? time::Tick{hostStartOffsetTick != nullptr &&
                                hostStartOffsetTick->isNumber()
                            ? hostStartOffsetTick->asInt64()
                            : 0}
          : time::Tick{0};
  if (schemaVersion >= 4) {
    if (routingValue == nullptr) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Schema 4 project is missing routing");
    }
    auto parsedRouting = decodeRouting(*routingValue);
    if (!parsedRouting) return core::Result<domain::Project>{parsedRouting.error()};
    project.routing() = std::move(parsedRouting.value());
  }

  for (const auto& trackValue : vocalTracks.value()->asArray()) {
    if (!trackValue.isObject()) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Vocal track must be an object");
    }
    const auto* idJson = trackValue.find("id");
    const auto* trackName = trackValue.find("name");
    const auto* voicebank = trackValue.find("voicebank");
    const auto* character = trackValue.find("character");
    const auto* gainDb = trackValue.find("gainDb");
    const auto* pan = trackValue.find("pan");
    const auto* muted = trackValue.find("muted");
    const auto* solo = trackValue.find("solo");
    const auto* outputRoute = trackValue.find("outputRoute");
    const auto* regions = trackValue.find("regions");
    if (idJson == nullptr || trackName == nullptr || voicebank == nullptr || character == nullptr ||
        gainDb == nullptr || pan == nullptr || muted == nullptr || solo == nullptr || regions == nullptr ||
        !idJson->isString() || !trackName->isString() || !voicebank->isObject() ||
        !character->isObject() || !gainDb->isNumber() || !pan->isNumber() || !muted->isBool() ||
        !solo->isBool() || !regions->isArray()) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Vocal track fields are invalid");
    }
    auto trackId = parseId<domain::TrackTag>(*idJson, "track.id");
    if (!trackId) return core::Result<domain::Project>{trackId.error()};
    const auto* voicebankId = voicebank->find("id");
    const auto* voicebankVersion = voicebank->find("version");
    const auto* voicebankHash = voicebank->find("contentHash");
    const auto* characterId = character->find("id");
    const auto* characterVersion = character->find("version");
    if (voicebankId == nullptr || voicebankVersion == nullptr || voicebankHash == nullptr ||
        characterId == nullptr || characterVersion == nullptr || !voicebankId->isString() ||
        !voicebankVersion->isString() || !voicebankHash->isString() || !characterId->isString() ||
        !characterVersion->isString()) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Voicebank or character reference is invalid");
    }
    domain::VocalTrack track{
        .id = trackId.value(),
        .name = trackName->asString(),
        .voicebank = {voicebankId->asString(), voicebankVersion->asString(), voicebankHash->asString()},
        .character = {characterId->asString(), characterVersion->asString()},
        .regions = {},
        .gainDb = static_cast<float>(gainDb->asNumber()),
        .pan = static_cast<float>(pan->asNumber()),
        .muted = muted->asBool(),
        .solo = solo->asBool(),
        .outputRoute = domain::TrackOutputRoute{
            .bus = project.routing().masterBus,
            .matrix = domain::RoutingMatrix::monoToStereo(
                static_cast<float>(pan->asNumber())),
        },
    };
    if (schemaVersion >= 4) {
      if (outputRoute == nullptr) {
        return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                              "Schema 4 vocal track is missing outputRoute");
      }
      auto parsedRoute = decodeTrackOutputRoute(*outputRoute, "vocalTrack.outputRoute");
      if (!parsedRoute) return core::Result<domain::Project>{parsedRoute.error()};
      track.outputRoute = std::move(parsedRoute.value());
    }

    for (const auto& regionValue : regions->asArray()) {
      if (!regionValue.isObject()) {
        return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                              "Region must be an object");
      }
      const auto* regionIdJson = regionValue.find("id");
      const auto* regionName = regionValue.find("name");
      const auto* startTick = regionValue.find("startTick");
      const auto* durationTick = regionValue.find("durationTick");
      const auto* lyrics = regionValue.find("lyrics");
      const auto* notes = regionValue.find("notes");
      const auto* phonemeOverrides = regionValue.find("phonemeOverrides");
      const auto* unitSelectionOverrides = regionValue.find("unitSelectionOverrides");
      const auto* seamOverrides = regionValue.find("seamOverrides");
      const auto* pitchAutomation = regionValue.find("pitchAutomation");
      if (regionIdJson == nullptr || regionName == nullptr || startTick == nullptr ||
          durationTick == nullptr || lyrics == nullptr || notes == nullptr ||
          !regionIdJson->isString() || !regionName->isString() || !startTick->isNumber() ||
          !durationTick->isNumber() || !lyrics->isArray() || !notes->isArray()) {
        return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                              "Region fields are invalid");
      }
      auto regionId = parseId<domain::RegionTag>(*regionIdJson, "region.id");
      if (!regionId) return core::Result<domain::Project>{regionId.error()};
      domain::VocalRegion region{
          .id = regionId.value(),
          .name = regionName->asString(),
          .startTick = time::Tick{startTick->asInt64()},
          .durationTick = time::Tick{durationTick->asInt64()},
          .lyrics = {},
          .notes = {},
          .phonemeOverrides = {},
          .unitSelectionOverrides = {},
          .seamOverrides = {},
          .pitchAutomation = {},
      };
      for (const auto& lyricValue : lyrics->asArray()) {
        if (!lyricValue.isObject()) {
          return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                "Lyric must be an object");
        }
        const auto* lyricIdJson = lyricValue.find("id");
        const auto* surface = lyricValue.find("surface");
        const auto* language = lyricValue.find("language");
        if (lyricIdJson == nullptr || surface == nullptr || language == nullptr ||
            !lyricIdJson->isString() || !surface->isString() || !language->isString()) {
          return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                "Lyric fields are invalid");
        }
        auto lyricId = parseId<domain::LyricTag>(*lyricIdJson, "lyric.id");
        auto surfaceText = domain::fromUtf8(surface->asString());
        if (!lyricId) return core::Result<domain::Project>{lyricId.error()};
        if (!surfaceText) return core::Result<domain::Project>{surfaceText.error()};
        region.lyrics.push_back(domain::LyricToken{
            lyricId.value(), std::move(surfaceText).value(), parseLanguage(language->asString())});
      }
      for (const auto& noteValue : notes->asArray()) {
        if (!noteValue.isObject()) {
          return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                "Note must be an object");
        }
        const auto* noteIdJson = noteValue.find("id");
        const auto* noteStart = noteValue.find("startTick");
        const auto* noteDuration = noteValue.find("durationTick");
        const auto* midiKey = noteValue.find("midiKey");
        const auto* lyricIdJson = noteValue.find("lyricId");
        const auto* articulation = noteValue.find("articulation");
        if (noteIdJson == nullptr || noteStart == nullptr || noteDuration == nullptr ||
            midiKey == nullptr || lyricIdJson == nullptr || articulation == nullptr ||
            !noteIdJson->isString() || !noteStart->isNumber() || !noteDuration->isNumber() ||
            !midiKey->isNumber() || !lyricIdJson->isString() || !articulation->isString()) {
          return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                "Note fields are invalid");
        }
        auto noteId = parseId<domain::NoteTag>(*noteIdJson, "note.id");
        auto lyricId = parseId<domain::LyricTag>(*lyricIdJson, "note.lyricId");
        if (!noteId) return core::Result<domain::Project>{noteId.error()};
        if (!lyricId) return core::Result<domain::Project>{lyricId.error()};
        domain::Note note{
            .id = noteId.value(),
            .startTick = time::Tick{noteStart->asInt64()},
            .durationTick = time::Tick{noteDuration->asInt64()},
            .midiKey = static_cast<std::uint8_t>(midiKey->asInt64()),
            .lyricTokenId = lyricId.value(),
            .articulation = parseArticulation(articulation->asString()),
            .slurGroup = std::nullopt,
        };
        if (const auto* slurGroup = noteValue.find("slurGroup"); slurGroup != nullptr) {
          auto parsedSlur = parseIdValue(*slurGroup, "note.slurGroup");
          if (!parsedSlur) return core::Result<domain::Project>{parsedSlur.error()};
          note.slurGroup = parsedSlur.value();
        }
        region.notes.push_back(std::move(note));
      }
      if (phonemeOverrides != nullptr) {
        if (!phonemeOverrides->isArray()) {
          return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                "Phoneme overrides must be an array");
        }
        for (const auto& overrideJson : phonemeOverrides->asArray()) {
          if (!overrideJson.isObject()) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                  "Phoneme override must be an object");
          }
          const auto* overrideNoteId = overrideJson.find("noteId");
          const auto* ordinal = overrideJson.find("ordinal");
          const auto* locked = overrideJson.find("locked");
          if (overrideNoteId == nullptr || ordinal == nullptr || locked == nullptr ||
              !overrideNoteId->isString() || !ordinal->isNumber() || !locked->isBool()) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                  "Phoneme override fields are invalid");
          }
          auto parsedNoteId = parseId<domain::NoteTag>(*overrideNoteId,
                                                        "phonemeOverride.noteId");
          if (!parsedNoteId) {
            return core::Result<domain::Project>{parsedNoteId.error()};
          }
          const auto ordinalValue = ordinal->asInt64();
          if (ordinalValue < 0 || ordinalValue > 65535) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                  "Phoneme override ordinal is invalid");
          }
          domain::PhonemeOverride overrideValue{
              .key = domain::PhonemeKey{
                  parsedNoteId.value(), static_cast<std::uint16_t>(ordinalValue)},
              .symbol = std::nullopt,
              .timing = {},
              .locked = locked->asBool(),
          };
          if (const auto* symbol = overrideJson.find("symbol"); symbol != nullptr) {
            if (!symbol->isString()) {
              return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                    "Phoneme override symbol must be a string");
            }
            overrideValue.symbol = symbol->asString();
          }
          if (const auto* startOffset = overrideJson.find("startOffsetUs");
              startOffset != nullptr) {
            if (!startOffset->isNumber()) {
              return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                    "Phoneme start offset must be a number");
            }
            overrideValue.timing.startOffset = startOffset->asInt64();
          }
          if (const auto* endOffset = overrideJson.find("endOffsetUs");
              endOffset != nullptr) {
            if (!endOffset->isNumber()) {
              return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                                    "Phoneme end offset must be a number");
            }
            overrideValue.timing.endOffset = endOffset->asInt64();
          }
          region.phonemeOverrides.push_back(std::move(overrideValue));
        }
      } else if (schemaVersion >= 2) {
        return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                              "Schema 2+ region is missing phonemeOverrides");
      }

      if (unitSelectionOverrides != nullptr) {
        if (!unitSelectionOverrides->isArray()) {
          return core::failure<domain::Project>(core::ErrorCode::ParseError,
              "Unit selection overrides must be an array");
        }
        for (const auto& overrideJson : unitSelectionOverrides->asArray()) {
          if (!overrideJson.isObject()) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                "Unit selection override must be an object");
          }
          const auto* overrideNoteId = overrideJson.find("noteId");
          const auto* ordinal = overrideJson.find("ordinal");
          const auto* tokenCount = overrideJson.find("tokenCount");
          const auto* unitId = overrideJson.find("unitId");
          const auto* renderer = overrideJson.find("renderer");
          const auto* loopPrint = overrideJson.find("loopPrint");
          const auto* sourcePitchResidual = overrideJson.find("sourcePitchResidual");
          const auto* locked = overrideJson.find("locked");
          if (overrideNoteId == nullptr || ordinal == nullptr || tokenCount == nullptr ||
              unitId == nullptr || locked == nullptr || !overrideNoteId->isString() ||
              !ordinal->isNumber() || !tokenCount->isNumber() || !unitId->isString() ||
              (renderer != nullptr && !renderer->isString()) ||
              (loopPrint != nullptr && !loopPrint->isNumber()) ||
              (sourcePitchResidual != nullptr && !sourcePitchResidual->isNumber()) ||
              !locked->isBool()) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                "Unit selection override fields are invalid");
          }
          auto parsedNoteId = parseId<domain::NoteTag>(
              *overrideNoteId, "unitSelectionOverride.noteId");
          if (!parsedNoteId) return core::Result<domain::Project>{parsedNoteId.error()};
          const auto ordinalValue = ordinal->asInt64();
          const auto tokenCountValue = tokenCount->asInt64();
          if (ordinalValue < 0 || ordinalValue > 65535 || tokenCountValue <= 0 ||
              tokenCountValue > 65535) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                "Unit selection override range is invalid");
          }
          const auto parseNormalized = [](const JsonValue* value,
                                          std::string_view field)
              -> core::Result<std::optional<float>> {
            if (value == nullptr) {
              return core::success(std::optional<float>{});
            }
            const auto parsed = value->asNumber();
            if (!std::isfinite(parsed) || parsed < 0.0 || parsed > 1.0) {
              return core::failure<std::optional<float>>(
                  core::ErrorCode::ParseError,
                  std::string(field) + " must be finite and within [0, 1]");
            }
            return core::success(std::optional<float>{static_cast<float>(parsed)});
          };
          auto parsedLoopPrint = parseNormalized(loopPrint, "loopPrint");
          if (!parsedLoopPrint) {
            return core::Result<domain::Project>{parsedLoopPrint.error()};
          }
          auto parsedSourcePitchResidual = parseNormalized(
              sourcePitchResidual, "sourcePitchResidual");
          if (!parsedSourcePitchResidual) {
            return core::Result<domain::Project>{
                parsedSourcePitchResidual.error()};
          }
          region.unitSelectionOverrides.push_back(domain::UnitSelectionOverride{
              .startKey = domain::PhonemeKey{
                  parsedNoteId.value(), static_cast<std::uint16_t>(ordinalValue)},
              .tokenCount = static_cast<std::uint16_t>(tokenCountValue),
              .unitId = unitId->asString(),
              .renderer = renderer == nullptr
                  ? domain::UnitRendererKind::Inherit
                  : domain::parseUnitRendererKind(renderer->asString()),
              .loopPrint = parsedLoopPrint.value(),
              .sourcePitchResidual = parsedSourcePitchResidual.value(),
              .locked = locked->asBool(),
          });
        }
      } else if (schemaVersion >= 3) {
        return core::failure<domain::Project>(core::ErrorCode::ParseError,
            "Schema 3 region is missing unitSelectionOverrides");
      }

      if (seamOverrides != nullptr) {
        if (!seamOverrides->isArray()) {
          return core::failure<domain::Project>(core::ErrorCode::ParseError,
              "Seam overrides must be an array");
        }
        for (const auto& overrideJson : seamOverrides->asArray()) {
          if (!overrideJson.isObject()) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                "Seam override must be an object");
          }
          const auto* overrideNoteId = overrideJson.find("noteId");
          const auto* ordinal = overrideJson.find("ordinal");
          const auto* curve = overrideJson.find("curve");
          const auto* locked = overrideJson.find("locked");
          const auto* seamAmount = overrideJson.find("seamAmount");
          const auto* overlap = overrideJson.find("overlapUs");
          const auto* phaseReset = overrideJson.find("phaseReset");
          const auto* envelopeBlend = overrideJson.find("envelopeBlend");
          if (overrideNoteId == nullptr || ordinal == nullptr || curve == nullptr ||
              locked == nullptr || !overrideNoteId->isString() || !ordinal->isNumber() ||
              !curve->isString() || !locked->isBool() ||
              (seamAmount != nullptr && !seamAmount->isNumber()) ||
              (overlap != nullptr && !overlap->isNumber()) ||
              (phaseReset != nullptr && !phaseReset->isNumber()) ||
              (envelopeBlend != nullptr && !envelopeBlend->isNumber())) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                "Seam override fields are invalid");
          }
          auto parsedNoteId = parseId<domain::NoteTag>(
              *overrideNoteId, "seamOverride.noteId");
          if (!parsedNoteId) return core::Result<domain::Project>{parsedNoteId.error()};
          const auto ordinalValue = ordinal->asInt64();
          if (ordinalValue < 0 || ordinalValue > 65535) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                "Seam override ordinal is invalid");
          }
          domain::SeamOverride value{
              .incomingStartKey = domain::PhonemeKey{
                  parsedNoteId.value(), static_cast<std::uint16_t>(ordinalValue)},
              .seamAmount = std::nullopt,
              .overlap = std::nullopt,
              .phaseReset = std::nullopt,
              .envelopeBlend = std::nullopt,
              .curve = domain::parseSeamCurve(curve->asString()),
              .locked = locked->asBool(),
          };
          if (seamAmount != nullptr) {
            value.seamAmount = static_cast<float>(seamAmount->asNumber());
          }
          if (overlap != nullptr) value.overlap = overlap->asInt64();
          if (phaseReset != nullptr) {
            value.phaseReset = static_cast<float>(phaseReset->asNumber());
          }
          if (envelopeBlend != nullptr) {
            value.envelopeBlend = static_cast<float>(envelopeBlend->asNumber());
          }
          region.seamOverrides.push_back(std::move(value));
        }
      } else if (schemaVersion >= 3) {
        return core::failure<domain::Project>(core::ErrorCode::ParseError,
            "Schema 3 region is missing seamOverrides");
      }

      if (pitchAutomation != nullptr) {
        if (!pitchAutomation->isArray()) {
          return core::failure<domain::Project>(core::ErrorCode::ParseError,
              "Pitch automation must be an array");
        }
        for (const auto& pointJson : pitchAutomation->asArray()) {
          if (!pointJson.isObject()) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                "Pitch automation point must be an object");
          }
          const auto* tick = pointJson.find("tick");
          const auto* cents = pointJson.find("cents");
          const auto* interpolation = pointJson.find("interpolation");
          if (tick == nullptr || cents == nullptr || interpolation == nullptr ||
              !tick->isNumber() || !cents->isNumber() || !interpolation->isString()) {
            return core::failure<domain::Project>(core::ErrorCode::ParseError,
                "Pitch automation point fields are invalid");
          }
          const auto inserted = region.pitchAutomation.upsert(
              domain::PitchAutomationPoint{
                  .tick = time::Tick{tick->asInt64()},
                  .cents = static_cast<float>(cents->asNumber()),
                  .interpolation = domain::parseCurveInterpolation(
                      interpolation->asString()),
              });
          if (!inserted) return core::Result<domain::Project>{inserted.error()};
        }
      } else if (schemaVersion >= 3) {
        return core::failure<domain::Project>(core::ErrorCode::ParseError,
            "Schema 3 region is missing pitchAutomation");
      }
      region.sortNotes();
      track.regions.push_back(std::move(region));
    }
    project.vocalTracks().push_back(std::move(track));
  }

  for (const auto& trackValue : audioTracks.value()->asArray()) {
    if (!trackValue.isObject()) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Audio track must be an object");
    }
    const auto* idJson = trackValue.find("id");
    const auto* trackName = trackValue.find("name");
    const auto* mediaPath = trackValue.find("mediaPath");
    const auto* mediaHash = trackValue.find("mediaHash");
    const auto* mediaOwnership = trackValue.find("mediaOwnership");
    const auto* originalFilename = trackValue.find("originalFilename");
    const auto* sourceSampleRate = trackValue.find("sourceSampleRate");
    const auto* sourceChannels = trackValue.find("sourceChannels");
    const auto* sourceFrameCount = trackValue.find("sourceFrameCount");
    const auto* trimStartFrame = trackValue.find("trimStartFrame");
    const auto* trimEndFrame = trackValue.find("trimEndFrame");
    const auto* startTick = trackValue.find("startTick");
    const auto* gainDb = trackValue.find("gainDb");
    const auto* pan = trackValue.find("pan");
    const auto* muted = trackValue.find("muted");
    const auto* solo = trackValue.find("solo");
    const auto* outputRoute = trackValue.find("outputRoute");
    if (idJson == nullptr || trackName == nullptr || mediaPath == nullptr || startTick == nullptr ||
        gainDb == nullptr || muted == nullptr || !idJson->isString() || !trackName->isString() ||
        !mediaPath->isString() || !startTick->isNumber() || !gainDb->isNumber() || !muted->isBool() ||
        (schemaVersion >= 4 && (pan == nullptr || solo == nullptr || outputRoute == nullptr ||
         !pan->isNumber() || !solo->isBool())) ||
        (schemaVersion >= 6 &&
         (mediaHash == nullptr || mediaOwnership == nullptr || originalFilename == nullptr ||
          sourceSampleRate == nullptr || sourceChannels == nullptr || sourceFrameCount == nullptr ||
          !mediaHash->isString() || !mediaOwnership->isString() ||
          !originalFilename->isString() || !sourceSampleRate->isNumber() ||
          !sourceChannels->isNumber() || !sourceFrameCount->isNumber() ||
          !validMediaOwnership(mediaOwnership->asString())))) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Audio track fields are invalid");
    }
    if (schemaVersion >= 6 &&
        (sourceSampleRate->asInt64() < 0 || sourceChannels->asInt64() < 0 ||
         sourceChannels->asInt64() > domain::kMaximumAudioChannels ||
         sourceFrameCount->asInt64() < 0 ||
         (trimStartFrame != nullptr && !trimStartFrame->isInteger()) ||
         (trimEndFrame != nullptr && !trimEndFrame->isNull() &&
          !trimEndFrame->isInteger()))) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Audio track media identity is invalid");
    }
    auto trackId = parseId<domain::TrackTag>(*idJson, "audioTrack.id");
    if (!trackId) return core::Result<domain::Project>{trackId.error()};
    domain::AudioTrack track{
        .id = trackId.value(),
        .name = trackName->asString(),
        .mediaPath = mediaPath->asString(),
        .mediaHash = mediaHash != nullptr && mediaHash->isString()
                         ? mediaHash->asString()
                         : std::string{},
        .mediaOwnership = mediaOwnership != nullptr && mediaOwnership->isString()
                              ? parseMediaOwnership(mediaOwnership->asString())
                              : domain::MediaOwnership::ExternalReference,
        .originalFilename = originalFilename != nullptr && originalFilename->isString()
                                ? originalFilename->asString()
                                : std::string{},
        .sourceSampleRate = sourceSampleRate != nullptr && sourceSampleRate->isNumber()
                                ? static_cast<std::uint32_t>(sourceSampleRate->asInt64())
                                : 0U,
        .sourceChannels = static_cast<std::uint16_t>(
            sourceChannels != nullptr && sourceChannels->isNumber()
                ? sourceChannels->asInt64()
                : 0),
        .sourceFrameCount = sourceFrameCount != nullptr && sourceFrameCount->isNumber()
                                ? static_cast<std::uint64_t>(sourceFrameCount->asInt64())
                                : 0U,
        .trimStartFrame = trimStartFrame != nullptr && trimStartFrame->isInteger()
                              ? static_cast<std::uint64_t>(trimStartFrame->asInt64())
                              : 0U,
        .trimEndFrame = trimEndFrame != nullptr && trimEndFrame->isInteger()
                            ? std::optional<std::uint64_t>{
                                  static_cast<std::uint64_t>(trimEndFrame->asInt64())}
                            : std::nullopt,
        .startTick = time::Tick{startTick->asInt64()},
        .gainDb = static_cast<float>(gainDb->asNumber()),
        .pan = pan == nullptr ? 0.0F : static_cast<float>(pan->asNumber()),
        .muted = muted->asBool(),
        .solo = solo == nullptr ? false : solo->asBool(),
        .outputRoute = domain::TrackOutputRoute{
            .bus = project.routing().masterBus,
            .matrix = domain::RoutingMatrix::monoToStereo(
                pan == nullptr ? 0.0F : static_cast<float>(pan->asNumber())),
        },
    };
    if (schemaVersion >= 4) {
      auto parsedRoute = decodeTrackOutputRoute(*outputRoute, "audioTrack.outputRoute");
      if (!parsedRoute) return core::Result<domain::Project>{parsedRoute.error()};
      track.outputRoute = std::move(parsedRoute.value());
    }
    project.audioTracks().push_back(std::move(track));
  }

  const auto validation = project.validate();
  if (!validation) {
    return core::Result<domain::Project>{validation.error()};
  }
  return project;
}

}  // namespace

core::Result<std::string> ProjectJsonCodec::encode(const domain::Project& project) const {
  const auto validation = project.validate();
  if (!validation) {
    return core::Result<std::string>{validation.error()};
  }
  return stringifyJson(encodeProject(project), true);
}

core::Result<domain::Project> ProjectJsonCodec::decode(std::string_view json) const {
  auto parsed = parseJson(json, JsonParseLimits{
      .maximumInputBytes = 64U * 1024U * 1024U,
      .maximumDepth = 64U,
      .maximumNodes = 1'000'000U,
      .maximumStringBytes = 4U * 1024U * 1024U,
      .maximumCollectionEntries = 250'000U,
  });
  if (!parsed) {
    return core::Result<domain::Project>{parsed.error()};
  }
  try {
    return decodeProject(parsed.value());
  } catch (const std::exception& exception) {
    return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                          "Project JSON contains a value of the wrong type",
                                          exception.what());
  }
}

core::Result<void> ProjectJsonCodec::save(
    const domain::Project& project, const std::filesystem::path& path) const {
  auto encoded = encode(project);
  if (!encoded) return core::Result<void>{encoded.error()};
  const auto target = validateProjectPath(path, true);
  if (!target) return target;
  auto backup = path;
  backup += ".bak";
  const auto backupTarget = validateProjectPath(backup, true);
  if (!backupTarget) return backupTarget;
  return core::durableAtomicWriteText(
      path, encoded.value(), core::AtomicWriteOptions{
          .backupPath = std::move(backup),
          .maximumBackupBytes = 64ULL * 1024ULL * 1024ULL,
          .faultInjector = {},
      });
}

core::Result<domain::Project> ProjectJsonCodec::load(
    const std::filesystem::path& path) const {
  const auto target = validateProjectPath(path, false);
  if (!target) return core::Result<domain::Project>{target.error()};
  auto content = core::readTextFileLimited(path, 64ULL * 1024ULL * 1024ULL);
  if (!content) return core::Result<domain::Project>{content.error()};
  return decode(content.value());
}

}  // namespace seam::formats
