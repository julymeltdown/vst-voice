#include "seam/phonemizer/language_resolver.hpp"

#include "seam/phonemizer/english_phonemizer.hpp"
#include "seam/phonemizer/korean_phonemizer.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"
#include "seam/core/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace seam::phonemizer {
namespace {

void field(core::Sha256& hash, std::string_view value) {
  hash.update(std::to_string(value.size()));
  hash.update(":");
  hash.update(value);
}

template <typename T>
void number(core::Sha256& hash, T value) {
  field(hash, std::to_string(value));
}

using LyricIndex = std::unordered_map<domain::LyricTokenId, const domain::LyricToken*>;

core::Result<LyricIndex> admitPronunciation(
    const domain::VocalRegion& region, std::stop_token stop) {
  using Output = LyricIndex;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
  // This precedes every dispatch index, including cancelled or mixed-language
  // requests. The admitted index is reused by selection and identity hashing.
  if (region.notes.size() > kMaximumPronunciationNotes ||
      region.lyrics.size() > kMaximumPronunciationNotes ||
      region.phonemeOverrides.size() > 4096U) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Pronunciation input exceeds collection bounds");
  }
  std::size_t characters = 0U;
  LyricIndex lyricIndex;
  for (const auto& lyric : region.lyrics) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
    if (!lyric.id.valid() || lyric.surface.size() > 4096U ||
        lyric.surface.size() > 65536U - characters ||
        (lyric.readingHint && (lyric.readingHint->empty() ||
         lyric.readingHint->size() > 4096U ||
         lyric.readingHint->size() > 65536U - characters - lyric.surface.size()))) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
          "Pronunciation lyrics exceed identity or text bounds");
    }
    characters += lyric.surface.size() + (lyric.readingHint ? lyric.readingHint->size() : 0U);
    if (lyric.readingHint) {
      for (const auto value : *lyric.readingHint) {
        const auto scalar = static_cast<std::uint32_t>(value);
        if (scalar > 0x10ffffU || (scalar >= 0xd800U && scalar <= 0xdfffU)) {
          return core::failure<Output>(core::ErrorCode::InvalidArgument,
              "Pronunciation reading hint contains invalid Unicode");
        }
      }
    }
    if (!lyricIndex.emplace(lyric.id, &lyric).second) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
          "Pronunciation lyrics repeat an identity");
    }
    for (const auto cp : lyric.surface) {
      if (cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU)) {
        return core::failure<Output>(core::ErrorCode::InvalidArgument,
            "Pronunciation lyric contains invalid Unicode");
      }
    }
  }
  std::unordered_set<domain::NoteId> noteIds;
  std::size_t referencedCharacters = 0U;
  for (const auto& note : region.notes) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
    const auto valid = note.validate();
    if (!valid) return core::Result<Output>{valid.error()};
    if (!noteIds.insert(note.id).second) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
          "Repeated pronunciation note identity");
    }
    if (note.phoneticHint) {
      if (note.phoneticHint->size() > 65536U - referencedCharacters) {
        return core::failure<Output>(core::ErrorCode::InvalidArgument,
            "Pronunciation hint expansion exceeds bounds");
      }
      referencedCharacters += note.phoneticHint->size();
    }
    const auto found = lyricIndex.find(note.lyricTokenId);
    if (found != lyricIndex.end()) {
      const auto textSize = found->second->surface.size() +
          (found->second->readingHint ? found->second->readingHint->size() : 0U);
      if (textSize > 65536U - referencedCharacters) {
        return core::failure<Output>(core::ErrorCode::InvalidArgument,
            "Repeated lyric expansion exceeds bounds");
      }
      referencedCharacters += textSize;
    }
  }
  for (const auto& edit : region.phonemeOverrides) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
    if (edit.symbol && edit.symbol->size() > 1024U) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument,
          "Pronunciation override symbol exceeds bounds");
    }
    const auto valid = edit.validate();
    if (!valid) return core::Result<Output>{valid.error()};
  }
  return lyricIndex;
}

core::Result<std::string> inputHash(const domain::VocalRegion& region,
    const LyricIndex& lyrics, std::string_view tag, std::string_view resource,
    std::stop_token stop) {
  const auto cancelled = [] {
    return core::failure<std::string>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
  };
  if (stop.stop_requested()) return cancelled();
  core::Sha256 hash;
  field(hash, tag);
  field(hash, resource);
  field(hash, region.id.toString());
  std::vector<const domain::Note*> notes;
  notes.reserve(region.notes.size());
  for (const auto& note : region.notes) {
    if (stop.stop_requested()) return cancelled();
    notes.push_back(&note);
  }
  std::stable_sort(notes.begin(), notes.end(), [](const auto* lhs, const auto* rhs) {
    return lhs->startTick == rhs->startTick ? lhs->id < rhs->id : lhs->startTick < rhs->startTick;
  });
  if (stop.stop_requested()) return cancelled();
  number(hash, notes.size());
  for (const auto* note : notes) {
    if (stop.stop_requested()) return cancelled();
    field(hash, note->id.toString());
    number(hash, note->startTick.value());
    number(hash, note->durationTick.value());
    number(hash, static_cast<int>(note->articulation));
    field(hash, note->lyricTokenId.toString());
    number(hash, note->phoneticHint.has_value());
    if (note->phoneticHint) field(hash, *note->phoneticHint);
    const auto found = lyrics.find(note->lyricTokenId);
    number(hash, found != lyrics.end());
    if (found != lyrics.end()) {
      number(hash, static_cast<int>(found->second->language));
      number(hash, found->second->surface.size());
      for (const auto value : found->second->surface) {
        if (stop.stop_requested()) return cancelled();
        number(hash, static_cast<std::uint32_t>(value));
      }
      number(hash, found->second->readingHint.has_value());
      if (found->second->readingHint) {
        number(hash, found->second->readingHint->size());
        for (const auto value : *found->second->readingHint) {
          if (stop.stop_requested()) return cancelled();
          number(hash, static_cast<std::uint32_t>(value));
        }
      }
    }
  }
  number(hash, region.phonemeOverrides.size());
  for (const auto& edit : region.phonemeOverrides) {
    if (stop.stop_requested()) return cancelled();
    field(hash, edit.key.noteId.toString());
    number(hash, edit.key.ordinal);
    number(hash, edit.symbol.has_value());
    if (edit.symbol) field(hash, *edit.symbol);
    number(hash, edit.timing.startOffset.has_value());
    if (edit.timing.startOffset) number(hash, *edit.timing.startOffset);
    number(hash, edit.timing.endOffset.has_value());
    if (edit.timing.endOffset) number(hash, *edit.timing.endOffset);
    number(hash, edit.locked);
    number(hash, edit.unresolved);
    number(hash, edit.sourceContextId.has_value());
    if (edit.sourceContextId) field(hash, *edit.sourceContextId);
  }
  if (stop.stop_requested()) return cancelled();
  return hash.hexDigest();
}

template <typename Adapter>
core::Result<ResolvedPronunciation> resolveLanguage(
    const domain::VocalRegion& region, const LyricIndex& lyrics,
    std::stop_token stop, Adapter adapter, domain::Language language,
    std::string_view resolverId, std::string_view resource, std::string_view hashTag) {
  using Output = ResolvedPronunciation;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
  std::optional<domain::VocalRegion> effective;
  if (std::any_of(region.phonemeOverrides.begin(), region.phonemeOverrides.end(),
      [](const auto& edit) { return edit.sourceContextId.has_value(); })) {
    auto baseRegion = region;
    baseRegion.phonemeOverrides.clear();
    const auto base = resolveLanguage(baseRegion, lyrics, stop, adapter, language, resolverId, resource, hashTag);
    if (!base) return core::Result<Output>{base.error()};
    effective = region;
    for (auto& edit : effective->phonemeOverrides) {
      if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
      if (edit.sourceContextId && edit.sourceContextId != phonemeEditContextId(base.value(), edit.key)) {
        edit.unresolved = true;
      }
    }
  }
  const auto resolved = adapter.phonemize(effective ? *effective : region, stop, kMaximumPronunciationTokens);
  if (!resolved) return core::Result<Output>{resolved.error()};
  auto pronunciation = std::move(resolved).value();
  std::unordered_map<domain::NoteId, const domain::Note*> notes;
  for (const auto& note : region.notes) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
    notes.emplace(note.id, &note);
  }
  for (std::size_t start = 0U; start < pronunciation.tokens.size();) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
    const auto noteId = pronunciation.tokens[start].key.noteId;
    auto end = start + 1U;
    while (end < pronunciation.tokens.size() && pronunciation.tokens[end].key.noteId == noteId) ++end;
    const auto found = notes.find(noteId);
    if (found == notes.end()) return core::failure<Output>(core::ErrorCode::InvariantViolation,
        "Resolved language token has no owning note");
    core::Sha256 context;
    field(context, "seam-language-phoneme-context-v1");
    field(context, resource);
    field(context, region.id.toString());
    field(context, noteId.toString());
    field(context, found->second->lyricTokenId.toString());
    number(context, end - start);
    for (auto index = start; index < end; ++index) {
      if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
      field(context, pronunciation.tokens[index].symbol);
      number(context, static_cast<int>(pronunciation.tokens[index].role));
      number(context, pronunciation.tokens[index].voiced);
    }
    const auto digest = context.hexDigest();
    for (auto index = start; index < end; ++index) {
      if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
      core::Sha256 address;
      field(address, "seam-language-phoneme-token-v1");
      field(address, digest);
      number(address, pronunciation.tokens[index].key.ordinal);
      pronunciation.tokens[index].contextId = address.hexDigest();
      pronunciation.tokens[index].lyricOwner = found->second->lyricTokenId;
    }
    start = end;
  }
  const auto input = inputHash(region, lyrics, hashTag, resource, stop);
  if (!input) return core::Result<Output>{input.error()};
  domain::PronunciationIdentity identity{language, std::string{resolverId}, language == domain::Language::English ? "7" : "3",
      std::string{resource}, input.value(), pronunciationSequenceHash(pronunciation.tokens)};
  const auto valid = identity.validate();
  if (!valid) return core::Result<Output>{valid.error()};
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
  return core::success(Output{std::move(identity), std::move(pronunciation)});
}

core::Result<ResolvedPronunciation> resolveRegisteredLanguage(
    const domain::VocalRegion& region, const LyricIndex& lyrics,
    domain::Language language, std::stop_token stop) {
  switch (language) {
    case domain::Language::English:
      return resolveLanguage(region, lyrics, stop, EnglishPhonemizer{}, language,
          "seam-builtin-en", SEAM_ENGLISH_RESOURCE_HASH, "seam-en-input-v7");
    case domain::Language::Korean:
      return resolveLanguage(region, lyrics, stop, KoreanHangulPhonemizer{}, language,
          "seam-builtin-ko", SEAM_KOREAN_RESOURCE_HASH, "seam-ko-input-v3");
    case domain::Language::Japanese: return resolveJapanesePronunciation(region, stop);
    case domain::Language::Unspecified: break;
  }
  return core::failure<ResolvedPronunciation>(core::ErrorCode::Unsupported,
      "No pronunciation service is registered for this language");
}

Result inspectFailure(const core::Error& error) {
  Result result;
  result.warnings.push_back({.code = WarningCode::ResolutionFailure, .noteId = {},
      .characterIndex = 0U, .message = error.message});
  return result;
}

}  // namespace

core::Result<void> validatePhoneHintForLanguage(
    domain::Language language, std::string_view text) {
  if (language == domain::Language::Unspecified) language = domain::Language::Japanese;
  switch (language) {
    case domain::Language::English: {
      const auto parsed = parseEnglishPhoneHint(text);
      return parsed ? core::success() : core::Result<void>{parsed.error()};
    }
    case domain::Language::Korean: {
      const auto parsed = parseKoreanPhoneHint(text);
      return parsed ? core::success() : core::Result<void>{parsed.error()};
    }
    case domain::Language::Japanese: {
      const auto parsed = parseJapanesePhoneHint(text);
      return parsed ? core::success() : core::Result<void>{parsed.error()};
    }
    case domain::Language::Unspecified: break;
  }
  return core::failure(core::ErrorCode::Unsupported,
      "No registered hint validator for this note language");
}

core::Result<ResolvedPronunciation> resolveEnglishPronunciation(
    const domain::VocalRegion& region, std::stop_token stop) {
  const auto lyrics = admitPronunciation(region, stop);
  if (!lyrics) return core::Result<ResolvedPronunciation>{lyrics.error()};
  return resolveRegisteredLanguage(region, lyrics.value(), domain::Language::English, stop);
}

core::Result<ResolvedPronunciation> resolveKoreanPronunciation(
    const domain::VocalRegion& region, std::stop_token stop) {
  const auto lyrics = admitPronunciation(region, stop);
  if (!lyrics) return core::Result<ResolvedPronunciation>{lyrics.error()};
  return resolveRegisteredLanguage(region, lyrics.value(), domain::Language::Korean, stop);
}

Result inspectEnglishPronunciation(const domain::VocalRegion& region) {
  const auto resolved = resolveEnglishPronunciation(region);
  return resolved ? std::move(resolved).value().pronunciation : inspectFailure(resolved.error());
}

Result inspectKoreanPronunciation(const domain::VocalRegion& region) {
  const auto resolved = resolveKoreanPronunciation(region);
  return resolved ? std::move(resolved).value().pronunciation : inspectFailure(resolved.error());
}

namespace {
core::Result<domain::Language> languageForRegion(
    const domain::VocalRegion& region, const LyricIndex& lyrics, std::stop_token stop) {
  std::optional<domain::Language> selected;
  for (const auto& note : region.notes) {
    if (stop.stop_requested()) return core::failure<domain::Language>(
        core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
    const auto found = lyrics.find(note.lyricTokenId);
    if (found == lyrics.end() || found->second->language == domain::Language::Unspecified) continue;
    if (!selected) selected = found->second->language;
    else if (*selected != found->second->language)
      return core::failure<domain::Language>(core::ErrorCode::Unsupported,
          "A region cannot mix explicit English, Korean and Japanese pronunciation services",
          std::string{kMixedPronunciationLanguagesContext});
  }
  return selected.value_or(domain::Language::Japanese);
}
}  // namespace

core::Result<ResolvedPronunciation> resolvePronunciation(
    const domain::VocalRegion& region, std::stop_token stop) {
  const auto lyrics = admitPronunciation(region, stop);
  if (!lyrics) return core::Result<ResolvedPronunciation>{lyrics.error()};
  const auto language = languageForRegion(region, lyrics.value(), stop);
  if (!language) return core::Result<ResolvedPronunciation>{language.error()};
  return resolveRegisteredLanguage(region, lyrics.value(), language.value(), stop);
}

core::Result<ResolvedPronunciation> resolvePronunciationForLanguage(
    const domain::VocalRegion& region, domain::Language language,
    std::stop_token stop) {
  const auto admitted = admitPronunciation(region, stop);
  if (!admitted) return core::Result<ResolvedPronunciation>{admitted.error()};
  if (language == domain::Language::Unspecified) language = domain::Language::Japanese;
  const auto& lyrics = admitted.value();
  for (const auto& note : region.notes) {
    if (stop.stop_requested()) return core::failure<ResolvedPronunciation>(
        core::ErrorCode::Conflict, "Pronunciation resolution cancelled");
    const auto found = lyrics.find(note.lyricTokenId);
    if (found != lyrics.end() && found->second->language != domain::Language::Unspecified &&
        found->second->language != language)
      return core::failure<ResolvedPronunciation>(core::ErrorCode::Unsupported,
          "Region lyric language does not match the selected pronunciation resource");
  }
  return resolveRegisteredLanguage(region, lyrics, language, stop);
}

Result inspectPronunciation(const domain::VocalRegion& region) {
  const auto resolved = resolvePronunciation(region);
  return resolved ? std::move(resolved).value().pronunciation : inspectFailure(resolved.error());
}

}  // namespace seam::phonemizer
