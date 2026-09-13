#include "seam/voice_design/procedural_candidate.hpp"
#include "seam/voice_design/articulated_stream.hpp"
#include "seam/voice_design/frication_gesture_stream.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/core/sha256.hpp"
#include "seam/voice_design/frication_source.hpp"
#include "seam/voice_design/plosive_source.hpp"
#include "seam/voice_design/vocal_tract.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <unordered_set>

namespace seam::voice_design {
core::Result<ProceduralCandidate> parseProceduralCandidateMetadata(std::string_view metadataJson,
    const synthesis::ProceduralSingerResource& expectedRecipe,
    std::stop_token stopToken) {
  using Output = ProceduralCandidate;
  const auto fail = [](std::string message) { return core::failure<Output>(core::ErrorCode::InvalidArgument, std::move(message)); };
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Candidate loading cancelled"); };
  if (stopToken.stop_requested()) return cancelled();
  const auto parsed = formats::parseJson(metadataJson, {.maximumInputBytes = 4U * 1024U * 1024U,
      .maximumDepth = 4U, .maximumNodes = 100000U, .maximumStringBytes = 256U, .maximumCollectionEntries = 16384U});
  if (!parsed) return core::Result<Output>{parsed.error()};
  const auto& root = parsed.value();
  if (!root.isObject() || !root.find("schemaVersion") || !root.find("schemaVersion")->isInteger()) return fail("Candidate metadata has an invalid shape");
  const auto version = root.find("schemaVersion")->asInt64();
  const bool mixed = version>=2 && version<=8;
  if ((version != 1 && !mixed) || root.asObject().size() != (version==8 ? 24U : version==7 ? 23U : version==6 ? 22U : version>=4 ? 21U : mixed ? 20U : 17U)) return fail("Candidate metadata version or shape is unsupported");
  const auto recipe = decodeVoiceRecipeResource(expectedRecipe, stopToken,true,true);
  if (!recipe) return core::Result<Output>{recipe.error()};
  for (const auto* field : {"formatId", "approval", "markerSemantics", "audioSha256", "renderContentHash",
      "renderAbi", "recipeId", "recipeVersion", "recipeHash", "style"}) {
    const auto* value = root.find(field);
    if (!value || !value->isString() || value->asString().empty()) return fail("Candidate string field is missing or invalid");
  }
  for (const auto* field : {"schemaVersion", "sampleRate", "frameCount", "scoreOriginFrame", "proceduralRevision", "compilerRevision"})
    if (!root.find(field) || !root.find(field)->isInteger()) return fail("Candidate integer field is missing or invalid");
  const auto str = [&](const char* key) -> const std::string& { return root.find(key)->asString(); };
  const auto number = [&](const char* key) { return root.find(key)->asInt64(); };
  if (str("formatId") != "com.project-seam.procedural-candidate" ||
      str("approval") != "unapproved" || str("markerSemantics") != (mixed ? "planned-articulated-gestures" : "planned-vowel-gestures")) return fail("Candidate format or approval claim is unsupported");
  if (mixed) for (const auto* field : {"articulationPlanRevision", "fricationRevision", "fricationStreamRevision"}) {
    const auto* value = root.find(field);
    if (!value || !value->isInteger() || value->asInt64() <= 0 || value->asInt64() > 0xffffffffLL)
      return fail("Candidate articulation revision is invalid");
  }
  const auto rate = number("sampleRate"), frames = number("frameCount"), origin = number("scoreOriginFrame");
  if (rate < 8000 || rate > 384000 || frames <= 0 || frames > 32LL * 1024LL * 1024LL ||
      origin < 0 || origin > (1LL << 52) - frames || number("proceduralRevision") <= 0 ||
      number("proceduralRevision") > 0xffffffffLL || number("compilerRevision") <= 0 || number("compilerRevision") > 0xffffffffLL)
    return fail("Candidate dimensions or revisions exceed bounds");
  const auto hashValid = [](const std::string& hash) { return hash.size() == 64U && std::all_of(hash.begin(), hash.end(),
      [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }); };
  if (!hashValid(str("audioSha256")) || !hashValid(str("renderContentHash")) ||
      str("recipeId") != expectedRecipe.identity.id || str("recipeVersion") != expectedRecipe.identity.version ||
      str("recipeHash") != expectedRecipe.identity.contentHash) return fail("Candidate identity differs from its expected recipe");
  const auto* markers = root.find("markers");
  if (!markers || !markers->isArray() || markers->asArray().empty()) return fail("Candidate has no planned gesture markers");
  Output result{.recipe = expectedRecipe, .audioSha256 = str("audioSha256"), .renderContentHash = str("renderContentHash"),
      .renderAbi = str("renderAbi"), .style = str("style"), .metadataJson = std::string{metadataJson}, .proceduralRevision = static_cast<std::uint32_t>(number("proceduralRevision")),
      .compilerRevision = static_cast<std::uint32_t>(number("compilerRevision")), .scoreOriginFrame = origin,
      .sampleRate = static_cast<std::uint32_t>(rate), .frameCount = frames, .markers = {}, .audio = {}};
  result.schemaVersion = static_cast<std::uint32_t>(version);
  if (version>=4) {
    const auto* revision=root.find("plosiveRevision");
    if (!revision || !revision->isInteger() || revision->asInt64()<=0 || revision->asInt64()>0xffffffffLL)
      return fail("Candidate plosive revision is invalid");
    result.plosiveRevision=static_cast<std::uint32_t>(revision->asInt64());
  }
  if (mixed) {
    result.articulationPlanRevision = static_cast<std::uint32_t>(number("articulationPlanRevision"));
    result.fricationRevision = static_cast<std::uint32_t>(number("fricationRevision"));
    result.fricationStreamRevision = static_cast<std::uint32_t>(number("fricationStreamRevision"));
  }
  if (version==5 && ((expectedRecipe.identity.version!="5" && expectedRecipe.identity.version!="6") || result.proceduralRevision<8U ||
      result.articulationPlanRevision<8U || result.fricationStreamRevision<3U))
    return fail("Voiced-frication candidate requires its versioned recipe and mixed renderer revisions");
  if (version==6) {
    const auto* revision=root.find("voicedPlosiveRevision");
    if (!revision || !revision->isInteger() || revision->asInt64()!=VoicedPlosiveSource::algorithmRevision ||
        // A schema-seven recipe carries everything a schema-six recipe carries plus unvoiced
        // affricates, so it satisfies this candidate's requirement just as version six does.
        (expectedRecipe.identity.version!="6" && expectedRecipe.identity.version!="7" && expectedRecipe.identity.version!="8") ||
        result.proceduralRevision<10U || result.articulationPlanRevision<9U || result.fricationStreamRevision<3U)
      return fail("Voiced-stop candidate requires its recipe and source/renderer revisions");
    result.voicedPlosiveRevision=static_cast<std::uint32_t>(revision->asInt64());
  }
  if (version==7) {
    const auto* revision=root.find("affricateRevision");
    if (!revision || !revision->isInteger() ||
        revision->asInt64()!=static_cast<std::int64_t>(ArticulationPlan::kAffricateModelRevision) ||
        (expectedRecipe.identity.version!="7" && expectedRecipe.identity.version!="8") ||
        result.proceduralRevision<ArticulatedStream::algorithmRevision ||
        result.articulationPlanRevision<ArticulationPlan::algorithmRevision ||
        result.fricationStreamRevision<FricationGestureStream::algorithmRevision)
      return fail("Affricate candidate requires its recipe and source/renderer revisions");
    result.affricateRevision=static_cast<std::uint32_t>(revision->asInt64());
  }
  if (version==8) {
    const auto* revision=root.find("approximantRevision");
    if (!revision || !revision->isInteger() ||
        revision->asInt64()!=static_cast<std::int64_t>(ArticulationPlan::kApproximantModelRevision) ||
        expectedRecipe.identity.version!="8" ||
        result.proceduralRevision<ArticulatedStream::algorithmRevision ||
        result.articulationPlanRevision<ArticulationPlan::algorithmRevision)
      return fail("Approximant candidate requires its recipe and renderer revisions");
    result.approximantRevision=static_cast<std::uint32_t>(revision->asInt64());
  }
  bool hasVowel = false, hasFrication = false, hasNasal=false, hasPlosive=false, hasVoicedFrication=false, hasVoicedPlosive=false, hasAffricate=false, hasApproximant=false;
  std::unordered_set<std::string> checkedVowelPoses;
  std::unordered_set<std::string> keys;
  time::SampleFrame previousEnd = 0;
  for (const auto& entry : markers->asArray()) {
    if (stopToken.stop_requested()) return cancelled();
    if (!entry.isObject() || entry.asObject().size() != (mixed ? 5U : 4U) || !entry.find("key") || !entry.find("key")->isString() ||
        !entry.find("phone") || !entry.find("phone")->isString() || !entry.find("startFrame") || !entry.find("startFrame")->isInteger() ||
        !entry.find("endFrame") || !entry.find("endFrame")->isInteger()) return fail("Candidate marker shape is invalid");
    const auto& key = entry.find("key")->asString();
    const auto colon = key.find(':');
    if (colon == std::string::npos) return fail("Candidate phoneme key is invalid");
    std::uint64_t note = 0U; unsigned ordinal = 0U;
    const auto a = std::from_chars(key.data(), key.data() + colon, note, 16);
    const auto b = std::from_chars(key.data() + colon + 1U, key.data() + key.size(), ordinal);
    if (a.ec != std::errc{} || b.ec != std::errc{} || a.ptr != key.data() + colon || b.ptr != key.data() + key.size() ||
        note == 0U || ordinal >= 16384U || key != domain::PhonemeKey{domain::NoteId{note}, static_cast<std::uint16_t>(ordinal)}.toString() || !keys.insert(key).second)
      return fail("Candidate phoneme key is noncanonical or duplicated");
    const auto& phone = entry.find("phone")->asString();
    auto kind = ProceduralGestureKind::OralVowel;
    if (mixed) {
      const auto* value = entry.find("kind");
      if (!value || !value->isString() || (value->asString() != "oral-vowel" && value->asString() != "frication" &&
          !(version>=3 && value->asString()=="nasal") && !(version>=4 && value->asString()=="plosive") &&
          !(version>=5 && value->asString()=="voiced-frication") && !(version>=6 && value->asString()=="voiced-plosive") &&
          !(version>=7 && value->asString()=="affricate") &&
          !(version>=8 && value->asString()=="approximant"))) return fail("Candidate gesture kind is unsupported");
      if (value->asString() == "frication") kind = ProceduralGestureKind::Frication;
      if (value->asString() == "nasal") kind = ProceduralGestureKind::Nasal;
      if (value->asString() == "plosive") kind = ProceduralGestureKind::Plosive;
      if (value->asString() == "voiced-frication") kind = ProceduralGestureKind::VoicedFrication;
      if (value->asString() == "voiced-plosive") kind = ProceduralGestureKind::VoicedPlosive;
      if (value->asString() == "affricate") kind = ProceduralGestureKind::Affricate;
      if (value->asString() == "approximant") kind = ProceduralGestureKind::Approximant;
    }
    const auto start = entry.find("startFrame")->asInt64(), end = entry.find("endFrame")->asInt64();
    if (start < previousEnd || end <= start || end > frames) return fail("Candidate marker bounds are invalid");
    if (kind == ProceduralGestureKind::OralVowel) {
      // Use the same vowel vocabulary as score compilation, including English
      // stress suffixes and Korean vowels. A marker still needs its exact pose;
      // accepting a symbol never permits relabeling another phone's audio.
      if (!phonemizer::isVowelSymbol(phone)) return fail("Candidate vowel symbol is unsupported");
      if (checkedVowelPoses.insert(phone).second) {
        const auto tract=VocalTract::create(recipe.value(),phone,result.style,result.sampleRate);
        if (!tract) return core::Result<Output>{tract.error()};
      }
      hasVowel = true;
    } else if (kind==ProceduralGestureKind::Nasal) {
      if (!phonemizer::isNasalSymbol(phone)) return fail("Candidate nasal symbol is unsupported");
      if (checkedVowelPoses.insert(phone).second) {
        const auto tract=VocalTract::create(recipe.value(),phone,result.style,result.sampleRate);
        if (!tract) return core::Result<Output>{tract.error()};
      }
      hasNasal=true;
    } else if (kind==ProceduralGestureKind::Plosive || kind==ProceduralGestureKind::VoicedPlosive) {
      const auto pose = std::find_if(recipe.value().plosives.begin(), recipe.value().plosives.end(), [&](const auto& value) {
        return value.phone == phone && value.style == result.style;
      });
      if (pose == recipe.value().plosives.end()) return fail("Candidate plosive is not bound to its recipe style");
      const bool voiced=kind==ProceduralGestureKind::VoicedPlosive;
      if (voiced!=pose->voicedClosure.has_value()) return fail("Candidate stop voicing differs from its recipe");
      const auto burst = static_cast<time::SampleFrame>(std::llround(pose->burstMilliseconds * result.sampleRate / 1000.0));
      if (end-start <= burst || end-start > static_cast<time::SampleFrame>(result.sampleRate)*2)
        return fail("Candidate plosive has no bounded closure and burst");
      const auto source = PlosiveSource::create({pose->source, static_cast<std::uint32_t>(end-start-burst),
          static_cast<std::uint32_t>(burst)}, result.sampleRate, origin+start);
      if (!source) return core::Result<Output>{source.error()};
      if (voiced) {
        const auto checked=VoicedPlosiveSource::create({{pose->source,static_cast<std::uint32_t>(end-start-burst),
            static_cast<std::uint32_t>(burst)},pose->voicedClosure->gain,pose->voicedClosure->lowpassHz},result.sampleRate,origin+start);
        if (!checked) return core::Result<Output>{checked.error()};
        hasVoicedPlosive=true;
      }
      hasPlosive=true;
    } else if (kind==ProceduralGestureKind::Affricate) {
      const auto pose = std::find_if(recipe.value().affricates.begin(), recipe.value().affricates.end(), [&](const auto& value) {
        return value.phone == phone && value.style == result.style;
      });
      if (pose == recipe.value().affricates.end()) return fail("Candidate affricate is not bound to its recipe style");
      const auto burst = static_cast<time::SampleFrame>(std::llround(pose->burstMilliseconds * result.sampleRate / 1000.0));
      const auto minimumTail = static_cast<time::SampleFrame>(std::llround(
          ArticulationPlan::kMinimumAffricateTailMilliseconds * result.sampleRate / 1000.0));
      if (burst <= 0 || end-start <= burst + minimumTail)
        return fail("Candidate affricate has no bounded closure, burst and frication tail");
      const auto release = PlosiveSource::create({pose->burst,
          static_cast<std::uint32_t>(end-start-burst-minimumTail), static_cast<std::uint32_t>(burst)},
          result.sampleRate, origin+start);
      if (!release) return core::Result<Output>{release.error()};
      const auto tail = FricationSource::create(pose->tail, result.sampleRate, origin+end-minimumTail);
      if (!tail) return core::Result<Output>{tail.error()};
      hasAffricate=true;
    } else if (kind==ProceduralGestureKind::Approximant) {
      const auto pose = std::find_if(recipe.value().approximants.begin(), recipe.value().approximants.end(), [&](const auto& value) {
        return value.phone == phone && value.style == result.style;
      });
      if (pose == recipe.value().approximants.end()) return fail("Candidate approximant is not bound to its recipe style");
      if (phonemizer::isVowelSymbol(phone) || phonemizer::isNasalSymbol(phone))
        return fail("Approximant marker conflicts with a vowel or nasal identity");
      const auto tract=VocalTract::create(recipe.value(),phone,result.style,result.sampleRate);
      if (!tract) return core::Result<Output>{tract.error()};
      if (end-start < 1) return fail("Candidate approximant has no bounded transition span");
      hasApproximant=true;
    } else {
      const auto pose = std::find_if(recipe.value().frications.begin(), recipe.value().frications.end(), [&](const auto& value) {
        return value.phone == phone && value.style == result.style;
      });
      if (pose == recipe.value().frications.end()) return fail("Candidate frication is not bound to its recipe style");
      const bool voiced=kind==ProceduralGestureKind::VoicedFrication;
      if (voiced!=pose->voicingGain.has_value()) return fail("Candidate frication voicing differs from its frozen recipe");
      if (voiced) {
        if (phonemizer::isVowelSymbol(phone) || phonemizer::isNasalSymbol(phone)) return fail("Voiced-frication marker conflicts with a vowel or nasal identity");
        const auto tract=VocalTract::create(recipe.value(),phone,result.style,result.sampleRate);
        if (!tract) return core::Result<Output>{tract.error()};
        hasVoicedFrication=true;
      }
      const auto source = FricationSource::create(pose->source, result.sampleRate, origin + start);
      if (!source) return core::Result<Output>{source.error()};
      hasFrication = true;
    }
    result.markers.push_back({{domain::NoteId{note}, static_cast<std::uint16_t>(ordinal)}, phone, {start, end}, false, false, kind});
    previousEnd = end;
  }
  const bool syllabicOnly=version==3 && std::all_of(result.markers.begin(),result.markers.end(),[](const auto& marker) {
    return marker.kind==ProceduralGestureKind::Nasal && marker.phone=="N";
  });
  if (syllabicOnly) {
    std::unordered_set<domain::NoteId> notes;
    for (const auto& marker:result.markers) if (!notes.insert(marker.key.noteId).second)
      return fail("Syllabic nasal candidate requires one gesture per note");
  }
  if (mixed && ((!hasVowel && !syllabicOnly) || (version==2?!hasFrication:version==3?!hasNasal:version==4?!hasPlosive:version==5?!hasVoicedFrication:version==6?!hasVoicedPlosive:version==7?!hasAffricate:!hasApproximant))) return fail("Articulated candidate lacks its required voiced and consonant gesture kinds");
  return result;
}

core::Result<ProceduralCandidate> loadProceduralCandidate(const std::filesystem::path& metadataPath,
    const std::filesystem::path& audioPath, const synthesis::ProceduralSingerResource& expectedRecipe,
    std::stop_token stopToken) {
  if (stopToken.stop_requested()) return core::failure<ProceduralCandidate>(core::ErrorCode::Conflict, "Candidate loading cancelled");
  const auto text = core::readTextFileLimited(metadataPath, 4U * 1024U * 1024U);
  if (!text) return core::Result<ProceduralCandidate>{text.error()};
  return loadProceduralCandidateFromMetadata(text.value(), audioPath, expectedRecipe, stopToken);
}

core::Result<ProceduralCandidate> loadProceduralCandidateFromMetadata(std::string_view metadataJson,
    const std::filesystem::path& audioPath, const synthesis::ProceduralSingerResource& expectedRecipe,
    std::stop_token stopToken) {
  using Output = ProceduralCandidate;
  const auto fail = [](std::string message) { return core::failure<Output>(core::ErrorCode::InvalidArgument, std::move(message)); };
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Candidate loading cancelled"); };
  if (stopToken.stop_requested()) return cancelled();
  auto parsed = parseProceduralCandidateMetadata(metadataJson, expectedRecipe, stopToken);
  if (!parsed) return parsed;
  auto result = std::move(parsed.value());
  const auto bytes = core::readFileBytesLimited(audioPath, static_cast<std::uint64_t>(result.frameCount) * 4U + 4096U);
  if (!bytes) return core::Result<Output>{bytes.error()};
  const auto data = std::span<const std::byte>{bytes.value()};
  core::Sha256 digest;
  for (std::size_t offset = 0U; offset < data.size(); offset += 65536U) {
    if (stopToken.stop_requested()) return cancelled();
    digest.update(data.subspan(offset, std::min<std::size_t>(65536U, data.size() - offset)));
  }
  if (digest.hexDigest() != result.audioSha256) return fail("Candidate audio digest mismatch");
  const auto u16 = [&](std::size_t offset) { return std::to_integer<unsigned>(data[offset]) | (std::to_integer<unsigned>(data[offset + 1U]) << 8U); };
  const auto u32 = [&](std::size_t offset) { return static_cast<std::uint32_t>(u16(offset)) | (static_cast<std::uint32_t>(u16(offset + 2U)) << 16U); };
  std::size_t formatCount = 0U;
  for (std::size_t position = 12U; position + 8U <= data.size();) {
    const auto length = static_cast<std::size_t>(u32(position + 4U));
    if (length > data.size() - position - 8U) return fail("Candidate WAV chunk exceeds input bounds");
    const auto tag = std::string_view{reinterpret_cast<const char*>(data.data() + position), 4U};
    if (tag == "fmt ") {
      if (++formatCount != 1U || length < 16U || u16(position + 8U) != 3U || u16(position + 22U) != 32U)
        return fail("Candidate WAV must use unambiguous IEEE Float32 encoding");
    }
    position += 8U + length + length % 2U;
  }
  if (formatCount != 1U) return fail("Candidate WAV format is missing");
  auto audio = voicebank::readWav(bytes.value(), audioPath.string());
  if (!audio) return core::Result<Output>{audio.error()};
  if (audio.value().channels != 1U || audio.value().bitsPerSample != 32U || audio.value().sampleRate != result.sampleRate ||
      audio.value().frameCount() != static_cast<std::size_t>(result.frameCount) || std::any_of(audio.value().interleaved.begin(), audio.value().interleaved.end(),
          [](float value) { return !std::isfinite(value); })) return fail("Candidate decoded audio differs from metadata");
  if (stopToken.stop_requested()) return cancelled();
  result.audio = std::make_shared<const voicebank::AudioBuffer>(std::move(audio).value());
  return result;
}
}
