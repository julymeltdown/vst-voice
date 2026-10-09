#include "project_binding_commands.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/project_json.hpp"
#include "seam/rendering/singer_route.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string_view>

namespace seam::voicebank_cli {
namespace {
using Json = formats::JsonValue;
bool hex(std::string_view value, std::size_t size) {
  return value.size() == size && std::all_of(value.begin(), value.end(), [](char ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
  });
}
int fail(std::string_view message) { std::cerr << "error: " << message << '\n'; return 2; }
std::string_view languageName(domain::Language language) {
  switch (language) {
    case domain::Language::English: return "en";
    case domain::Language::Japanese: return "ja";
    case domain::Language::Korean: return "ko";
    case domain::Language::Unspecified: return "und";
  }
  return "und";
}
int verify(int argc, char** argv) {
  if (argc != 11) { printProjectBindingUsage(); return 1; }
#if defined(_WIN32)
  return fail("Project binding verification requires the POSIX held-input reader; Windows remains TODO");
#else
  const std::string_view pin{argv[3]}, trackId{argv[4]}, regionId{argv[5]}, family{argv[6]},
      resourceId{argv[7]}, version{argv[8]}, contentHash{argv[9]}, requestedLanguages{argv[10]};
  if (!hex(pin, 64U) || !hex(trackId, 16U) || !hex(regionId, 16U) || !hex(contentHash, 64U) ||
      resourceId.empty() || resourceId.size() > 256U || version.empty() || version.size() > 256U ||
      (family != "sample" && family != "recipe" && family != "model"))
    return fail("Expected project/resource binding has invalid identity fields");
  std::set<std::string> expectedLanguages;
  std::string canonicalLanguages;
  if (requestedLanguages.size() > 8U) return fail("Language set exceeds its bound");
  std::size_t start = 0;
  while (start < requestedLanguages.size()) {
    const auto end = requestedLanguages.find(',', start);
    const auto language = requestedLanguages.substr(start, end == std::string_view::npos ? end : end - start);
    if (language != "en" && language != "ja" && language != "ko") return fail("Unsupported declared language");
    expectedLanguages.emplace(language);
    if (end == std::string_view::npos) break;
    start = end + 1U;
  }
  for (const auto& language : expectedLanguages) {
    if (!canonicalLanguages.empty()) canonicalLanguages += ',';
    canonicalLanguages += language;
  }
  if (canonicalLanguages.empty() || canonicalLanguages != requestedLanguages)
    return fail("Languages must be an exact sorted set drawn from en,ja,ko");
  const auto bytes = core::readFileBytesLimited(argv[2], 64ULL * 1024ULL * 1024ULL);
  if (!bytes) return fail(bytes.error().message);
  if (core::sha256Hex(bytes.value()) != pin) return fail("Project bytes differ from the caller's SHA-256 pin");
  const std::string_view text{reinterpret_cast<const char*>(bytes.value().data()), bytes.value().size()};
  const auto decoded = formats::ProjectJsonCodec{}.decode(text);
  if (!decoded) return fail(decoded.error().message);
  // The codec maps unknown language strings to und. Reject lossy language
  // normalization anywhere in the captured project, including unused tokens.
  // The same native parser rejects duplicate keys before either interpretation.
  const auto raw = formats::parseJson(text);
  if (!raw) return fail(raw.error().message);
  for (const auto& rawTrack : raw.value().find("vocalTracks")->asArray())
    for (const auto& rawRegion : rawTrack.find("regions")->asArray())
      for (const auto& lyric : rawRegion.find("lyrics")->asArray()) {
        const auto& language = lyric.find("language")->asString();
        if (language != "en" && language != "ja" && language != "ko" && language != "und")
          return fail("Unknown raw lyric language would be normalized by the project codec");
      }

  const domain::VocalTrack* track = nullptr;
  for (const auto& candidate : decoded.value().vocalTracks())
    if (candidate.id.toString() == trackId) track = &candidate;
  if (!track) return fail("Selected vocal track is absent from the decoded project");
  const domain::VocalRegion* region = nullptr;
  for (const auto& candidate : track->regions)
    if (candidate.id.toString() == regionId) region = &candidate;
  if (!region || region->notes.empty()) return fail("Selected region is absent from the track or has no notes");
  const auto selected = rendering::selectedSingerResource(*track);
  if (!selected) return fail(selected.error().message);
  const auto& resource = selected.value();
  const std::string_view actualFamily = resource.kind == domain::SingerResourceKind::Neural ? "model" :
      resource.kind == domain::SingerResourceKind::Procedural ? "recipe" : "sample";
  if (actualFamily != family || resource.id != resourceId || resource.version != version || resource.contentHash != contentHash)
    return fail("Decoded selected singer reference differs from the expected binding");
  std::map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region->lyrics) lyrics.emplace(lyric.id, &lyric);
  std::set<domain::LyricTokenId> linked;
  std::set<std::string> observedLanguages;
  for (const auto& note : region->notes) {
    const auto lyric = lyrics.find(note.lyricTokenId);
    if (lyric == lyrics.end()) return fail("Every selected-region note must reference a lyric token");
    const std::string language{languageName(lyric->second->language)};
    if (language == "und") return fail("Selected note language is unspecified or unknown to the codec");
    observedLanguages.insert(language);
    linked.insert(note.lyricTokenId);
  }
  if (observedLanguages != expectedLanguages) return fail("Note-linked language set differs from the expected binding");
  Json::Array languages;
  for (const auto& language : observedLanguages) languages.emplace_back(language);
  // Only reference semantics are verified. Never open a recipe/media path from
  // the project, resolve a model helper, render audio or infer installation.
  const Json record{Json::Object{
      {"schemaVersion", std::int64_t{1}}, {"recordType", "seam.u45.project-binding-verification.v1"},
      {"result", "ProjectBindingVerified"}, {"evidenceScope", "ENGINEERING_ONLY"},
      {"bindingScope", "SELECTED_REGION_NOTE_REFERENCES"}, {"projectSha256", std::string{pin}},
      {"projectId", decoded.value().id().toString()},
      {"sourceSchemaVersion", raw.value().find("schemaVersion")->asInt64()}, {"codecSchemaVersion", std::int64_t{formats::ProjectJsonCodec::kSchemaVersion}},
      {"trackId", track->id.toString()}, {"regionId", region->id.toString()},
      {"payloadFamily", std::string{actualFamily}}, {"resourceId", resource.id},
      {"resourceVersion", resource.version}, {"resourceContentHash", resource.contentHash},
      {"languages", std::move(languages)}, {"noteCount", static_cast<std::int64_t>(region->notes.size())},
      {"linkedLyricCount", static_cast<std::int64_t>(linked.size())},
      {"unusedLyricCount", static_cast<std::int64_t>(region->lyrics.size() - linked.size())},
      {"storedVoicebankRole", actualFamily == "sample" ? "SELECTED" : "INACTIVE"},
      {"languageEvidence", "DECLARED_LYRIC_LANGUAGE"}, {"resourceAdmission", "NOT_CHECKED"},
      {"runtimeAvailability", "NOT_CHECKED"}, {"phonemization", "NOT_RUN"}, {"playback", "NOT_RUN"},
      {"hostExecution", "NOT_RUN"},
      {"qualification", "NOT_QUALIFIED"}, {"humanAcceptance", "NOT_RUN"},
      {"authorizesRelease", false}, {"releaseEligible", false}}};
  std::cout << formats::stringifyJson(record, true) << '\n';
  return 0;
#endif
}
}
std::optional<int> runProjectBindingCommand(int argc, char** argv) {
  if (argc >= 2 && std::string_view{argv[1]} == "verify-project-binding") return verify(argc, argv);
  return std::nullopt;
}
void printProjectBindingUsage() {
  std::cerr << "  verify-project-binding PROJECT SHA256 TRACK_ID REGION_ID FAMILY RESOURCE_ID VERSION CONTENT_HASH LANGUAGES\n";
}
}
