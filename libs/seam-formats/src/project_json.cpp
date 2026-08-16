#include "seam/formats/project_json.hpp"

#include "seam/core/file_io.hpp"

#include <charconv>
#include <cmath>
#include <sstream>
#include <system_error>

namespace seam::formats {
namespace {

using Object = JsonValue::Object;
using Array = JsonValue::Array;

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
        unitSelectionOverrides.emplace_back(Object{
            {"noteId", idValue(overrideValue.startKey.noteId)},
            {"ordinal", JsonValue{static_cast<std::int64_t>(
                overrideValue.startKey.ordinal)}},
            {"tokenCount", JsonValue{static_cast<std::int64_t>(
                overrideValue.tokenCount)}},
            {"unitId", JsonValue{overrideValue.unitId}},
            {"renderer", JsonValue{std::string(
                domain::unitRendererKindName(overrideValue.renderer))}},
            {"locked", JsonValue{overrideValue.locked}},
        });
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
        {"regions", JsonValue{std::move(regions)}}});
  }

  Array audioTracks;
  for (const auto& track : project.audioTracks()) {
    audioTracks.emplace_back(Object{{"id", idValue(track.id)},
                                    {"name", JsonValue{track.name}},
                                    {"mediaPath", JsonValue{track.mediaPath}},
                                    {"startTick", JsonValue{track.startTick.value()}},
                                    {"gainDb", JsonValue{static_cast<double>(track.gainDb)}},
                                    {"muted", JsonValue{track.muted}}});
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
          {"snapGrid", JsonValue{project.settings().snapGrid.value()}}}}},
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
  if (sampleRate == nullptr || characterDisplay == nullptr || snapEnabled == nullptr || snapGrid == nullptr ||
      !sampleRate->isNumber() || !characterDisplay->isString() || !snapEnabled->isBool() ||
      !snapGrid->isNumber()) {
    return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                          "Project settings are invalid");
  }
  project.settings().sampleRate = sampleRate->asNumber();
  project.settings().characterDisplay = parseCharacterMode(characterDisplay->asString());
  project.settings().snapEnabled = snapEnabled->asBool();
  project.settings().snapGrid = time::Tick{snapGrid->asInt64()};

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
    };

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
          const auto* locked = overrideJson.find("locked");
          if (overrideNoteId == nullptr || ordinal == nullptr || tokenCount == nullptr ||
              unitId == nullptr || locked == nullptr || !overrideNoteId->isString() ||
              !ordinal->isNumber() || !tokenCount->isNumber() || !unitId->isString() ||
              (renderer != nullptr && !renderer->isString()) || !locked->isBool()) {
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
          region.unitSelectionOverrides.push_back(domain::UnitSelectionOverride{
              .startKey = domain::PhonemeKey{
                  parsedNoteId.value(), static_cast<std::uint16_t>(ordinalValue)},
              .tokenCount = static_cast<std::uint16_t>(tokenCountValue),
              .unitId = unitId->asString(),
              .renderer = renderer == nullptr
                  ? domain::UnitRendererKind::Inherit
                  : domain::parseUnitRendererKind(renderer->asString()),
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
    const auto* startTick = trackValue.find("startTick");
    const auto* gainDb = trackValue.find("gainDb");
    const auto* muted = trackValue.find("muted");
    if (idJson == nullptr || trackName == nullptr || mediaPath == nullptr || startTick == nullptr ||
        gainDb == nullptr || muted == nullptr || !idJson->isString() || !trackName->isString() ||
        !mediaPath->isString() || !startTick->isNumber() || !gainDb->isNumber() || !muted->isBool()) {
      return core::failure<domain::Project>(core::ErrorCode::ParseError,
                                            "Audio track fields are invalid");
    }
    auto trackId = parseId<domain::TrackTag>(*idJson, "audioTrack.id");
    if (!trackId) return core::Result<domain::Project>{trackId.error()};
    project.audioTracks().push_back(domain::AudioTrack{
        .id = trackId.value(),
        .name = trackName->asString(),
        .mediaPath = mediaPath->asString(),
        .startTick = time::Tick{startTick->asInt64()},
        .gainDb = static_cast<float>(gainDb->asNumber()),
        .muted = muted->asBool(),
    });
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
  auto backup = path;
  backup += ".bak";
  return core::durableAtomicWriteText(
      path, encoded.value(), core::AtomicWriteOptions{
          .backupPath = std::move(backup),
          .maximumBackupBytes = 64ULL * 1024ULL * 1024ULL,
          .faultInjector = {},
      });
}

core::Result<domain::Project> ProjectJsonCodec::load(
    const std::filesystem::path& path) const {
  auto content = core::readTextFileLimited(path, 64ULL * 1024ULL * 1024ULL);
  if (!content) return core::Result<domain::Project>{content.error()};
  return decode(content.value());
}

}  // namespace seam::formats
