#include "seam/phonemizer/pronunciation_resolver.hpp"

#include "seam/core/sha256.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include "seam/phonemizer/japanese_phonemizer.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>

namespace seam::phonemizer {
namespace {

void field(core::Sha256& hash, std::string_view value) {
  hash.update(std::to_string(value.size()));
  hash.update(":");
  hash.update(value);
}
template <typename T> void number(core::Sha256& hash, T value) {
  field(hash, std::to_string(value));
}
void timing(core::Sha256& hash, const domain::PhonemeTiming& value) {
  number(hash, value.startOffset.has_value());
  if (value.startOffset) number(hash, *value.startOffset);
  number(hash, value.endOffset.has_value());
  if (value.endOffset) number(hash, *value.endOffset);
}

}  // namespace

std::string pronunciationSequenceHash(std::span<const domain::PhonemeToken> tokens) {
  core::Sha256 hash;
  field(hash, "seam-pronunciation-sequence-v2");
  number(hash, tokens.size());
  for (const auto& token : tokens) {
    field(hash, token.key.noteId.toString());
    field(hash, token.contextId);
    field(hash, token.lyricOwner.toString());
    number(hash, token.key.ordinal);
    field(hash, token.symbol);
    number(hash, static_cast<int>(token.role));
    number(hash, token.voiced);
    number(hash, token.locked);
    timing(hash, token.timing);
  }
  return hash.hexDigest();
}

core::Result<ResolvedPronunciation> resolveJapanesePronunciation(const domain::VocalRegion& region, std::stop_token stop) {
  using Output = ResolvedPronunciation;
  const auto cancelled = [] { return core::failure<Output>(core::ErrorCode::Conflict, "Pronunciation resolution cancelled"); };
  if (stop.stop_requested()) return cancelled();
  if (region.notes.size() > kMaximumPronunciationNotes || region.lyrics.size() > kMaximumPronunciationNotes ||
      region.phonemeOverrides.size() > 4096U) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Pronunciation input exceeds collection bounds");
  }
  std::size_t characters = 0U;
  std::unordered_set<domain::LyricTokenId> lyricIds;
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region.lyrics) {
    if (stop.stop_requested()) return cancelled();
    if (!lyric.id.valid() || !lyricIds.insert(lyric.id).second || lyric.surface.size() > 4096U ||
        lyric.surface.size() > 65536U - characters) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Pronunciation lyrics exceed identity or text bounds");
    }
    characters += lyric.surface.size();
    lyrics.emplace(lyric.id, &lyric);
    for (const auto cp : lyric.surface) {
      if (cp > 0x10ffffU || (cp >= 0xd800U && cp <= 0xdfffU)) {
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "Pronunciation lyric contains invalid Unicode");
      }
    }
  }
  std::vector<const domain::Note*> notes;
  std::unordered_set<domain::NoteId> ids;
  std::unordered_map<domain::NoteId, const domain::Note*> noteIndex;
  std::size_t referencedCharacters = 0U;
  for (const auto& note : region.notes) {
    if (stop.stop_requested()) return cancelled();
    const auto valid = note.validate();
    if (!valid) return core::Result<Output>{valid.error()};
    if (note.phoneticHint) {
      if (note.phoneticHint->size() > 65536U - referencedCharacters)
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "Pronunciation hint expansion exceeds bounds");
      referencedCharacters += note.phoneticHint->size();
      const auto hint = parseJapanesePhoneHint(*note.phoneticHint);
      if (!hint) return core::Result<Output>{hint.error()};
    }
    if (!ids.insert(note.id).second) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Repeated pronunciation note identity");
    }
    const auto lyricEntry = lyrics.find(note.lyricTokenId);
    if (const auto* lyric = lyricEntry == lyrics.end() ? nullptr : lyricEntry->second) {
      if (lyric->surface.size() > 65536U - referencedCharacters) {
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "Repeated lyric expansion exceeds bounds");
      }
      referencedCharacters += lyric->surface.size();
    }
    notes.push_back(&note);
    noteIndex.emplace(note.id, &note);
  }
  std::sort(notes.begin(), notes.end(), [](const auto* a, const auto* b) {
    return a->startTick == b->startTick ? a->id < b->id : a->startTick < b->startTick;
  });
  core::Sha256 input;
  field(input, "seam-ja-input-v2");
  field(input, SEAM_JAPANESE_RESOURCE_HASH);
  field(input, region.id.toString());
  number(input, notes.size());
  for (const auto* note : notes) {
    if (stop.stop_requested()) return cancelled();
    field(input, note->id.toString());
    number(input, note->startTick.value());
    number(input, note->durationTick.value());
    number(input, static_cast<int>(note->articulation));
    field(input, note->lyricTokenId.toString());
    number(input, note->phoneticHint.has_value());
    if (note->phoneticHint) field(input, *note->phoneticHint);
    const auto lyricEntry = lyrics.find(note->lyricTokenId);
    const auto* lyric = lyricEntry == lyrics.end() ? nullptr : lyricEntry->second;
    number(input, lyric != nullptr);
    if (lyric) {
      number(input, static_cast<int>(lyric->language));
      number(input, lyric->surface.size());
      for (const auto cp : lyric->surface) number(input, static_cast<std::uint32_t>(cp));
    }
  }
  number(input, region.phonemeOverrides.size());
  for (const auto& edit : region.phonemeOverrides) {
    if (stop.stop_requested()) return cancelled();
    const auto valid = edit.validate();
    if (!valid) return core::Result<Output>{valid.error()};
    if (edit.symbol && edit.symbol->size() > 1024U) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Pronunciation override symbol exceeds bounds");
    }
    field(input, edit.key.noteId.toString());
    number(input, edit.key.ordinal);
    number(input, edit.symbol.has_value());
    if (edit.symbol) field(input, *edit.symbol);
    timing(input, edit.timing);
    number(input, edit.locked);
    number(input, edit.unresolved);
    number(input, edit.sourceContextId.has_value());
    if (edit.sourceContextId) field(input, *edit.sourceContextId);
  }
  if (stop.stop_requested()) return cancelled();
  auto effective = region;
  if (std::any_of(region.phonemeOverrides.begin(), region.phonemeOverrides.end(),
                  [](const auto& edit) { return edit.sourceContextId.has_value(); })) {
    auto baseRegion = region;
    baseRegion.phonemeOverrides.clear();
    const auto base = resolveJapanesePronunciation(baseRegion, stop);
    if (!base) return core::Result<Output>{base.error()};
    for (auto& edit : effective.phonemeOverrides) {
      if (stop.stop_requested()) return cancelled();
      if (edit.sourceContextId && edit.sourceContextId != phonemeEditContextId(base.value(), edit.key)) {
        edit.unresolved = true;
      }
    }
  }
  JapaneseKanaPhonemizer adapter;
  auto prepared = adapter.phonemize(effective, stop, kMaximumPronunciationTokens);
  if (!prepared) return core::Result<Output>{prepared.error()};
  auto resolved = std::move(prepared.value());
  if (resolved.tokens.size() > kMaximumPronunciationTokens) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Resolved pronunciation exceeds token bounds");
  }
  for (std::size_t start = 0U; start < resolved.tokens.size();) {
    if (stop.stop_requested()) return cancelled();
    auto end = start + 1U;
    const auto noteId = resolved.tokens[start].key.noteId;
    while (end < resolved.tokens.size() && resolved.tokens[end].key.noteId == noteId) ++end;
    const auto foundNote = noteIndex.find(noteId);
    const auto* note = foundNote == noteIndex.end() ? nullptr : foundNote->second;
    if (!note) return core::failure<Output>(core::ErrorCode::InvariantViolation,
                                           "Resolved token has no owning note");
    core::Sha256 context;
    field(context, "seam-phoneme-context-v1");
    field(context, SEAM_JAPANESE_RESOURCE_HASH);
    field(context, region.id.toString());
    field(context, noteId.toString());
    field(context, note->lyricTokenId.toString());
    number(context, end - start);
    for (auto i = start; i < end; ++i) {
      field(context, resolved.tokens[i].symbol);
      number(context, static_cast<int>(resolved.tokens[i].role));
      number(context, resolved.tokens[i].voiced);
    }
    const auto digest = context.hexDigest();
    for (auto i = start; i < end; ++i) {
      core::Sha256 address;
      field(address, "seam-phoneme-token-v1");
      field(address, digest);
      number(address, resolved.tokens[i].key.ordinal);
      resolved.tokens[i].contextId = address.hexDigest();
      resolved.tokens[i].lyricOwner = note->lyricTokenId;
    }
    start = end;
  }
  domain::PronunciationIdentity identity{domain::Language::Japanese, "seam-builtin-ja", "2",
      SEAM_JAPANESE_RESOURCE_HASH, input.hexDigest(), pronunciationSequenceHash(resolved.tokens)};
  if (stop.stop_requested()) return cancelled();
  return core::success(Output{std::move(identity), std::move(resolved)});
}

Result inspectJapanesePronunciation(const domain::VocalRegion& region) {
  auto resolved = resolveJapanesePronunciation(region);
  if (resolved) return std::move(resolved).value().pronunciation;
  Result result;
  result.warnings.push_back({.code = WarningCode::ResolutionFailure,
      .noteId = {}, .characterIndex = 0U, .message = resolved.error().message});
  return result;
}

std::optional<std::string> phonemeEditContextId(
    const ResolvedPronunciation& base, domain::PhonemeKey key) {
  const domain::PhonemeToken* last = nullptr;
  for (const auto& token : base.pronunciation.tokens) {
    if (token.key == key && !token.contextId.empty()) return token.contextId;
    if (token.key.noteId == key.noteId) last = &token;
  }
  if (!last || last->contextId.empty() || key.ordinal <= last->key.ordinal) return std::nullopt;
  core::Sha256 hash;
  field(hash, "seam-appended-phoneme-slot-v1");
  field(hash, last->contextId);
  number(hash, key.ordinal);
  return hash.hexDigest();
}

core::Result<void> rebindTransferredPhonemeContexts(
    const domain::VocalRegion& source, domain::VocalRegion& destination,
    std::span<const domain::PerformanceNoteRemap> mapping) {
  if (mapping.size() > 10000U) {
    return core::failure(core::ErrorCode::InvalidArgument, "Phoneme transfer map exceeds bounds");
  }
  std::unordered_map<domain::NoteId, domain::NoteId> sources;
  for (const auto& pair : mapping) {
    if (!source.findNote(pair.source) || !destination.findNote(pair.target) ||
        !sources.emplace(pair.target, pair.source).second) {
      return core::failure(core::ErrorCode::InvalidArgument, "Invalid or repeated phoneme transfer target");
    }
  }
  auto overrides = destination.phonemeOverrides;
  if (std::none_of(overrides.begin(), overrides.end(),
                   [](const auto& edit) { return edit.sourceContextId && !edit.unresolved; })) {
    return core::success();
  }
  auto oldRegion = source;
  auto newRegion = destination;
  oldRegion.phonemeOverrides.clear();
  newRegion.phonemeOverrides.clear();
  const auto oldBase = resolvePronunciation(oldRegion);
  const auto newBase = resolvePronunciation(newRegion);
  for (auto& edit : overrides) {
    if (!edit.sourceContextId || edit.unresolved) continue;
    const auto found = sources.find(edit.key.noteId);
    if (found == sources.end() || !oldBase || !newBase) { edit.unresolved = true; continue; }
    const domain::PhonemeKey oldKey{found->second, edit.key.ordinal};
    const auto* original = source.findPhonemeOverride(oldKey);
    if (!original || original->unresolved || original->sourceContextId != edit.sourceContextId ||
        original->symbol != edit.symbol || original->timing != edit.timing || original->locked != edit.locked ||
        original->sourceContextId != phonemeEditContextId(oldBase.value(), oldKey)) {
      edit.unresolved = true;
      continue;
    }
    const auto oldTokens = oldBase.value().pronunciation.tokensForNote(oldKey.noteId);
    const auto newTokens = newBase.value().pronunciation.tokensForNote(edit.key.noteId);
    const auto* oldLyric = source.findLyric(source.findNote(oldKey.noteId)->lyricTokenId);
    const auto* newLyric = destination.findLyric(destination.findNote(edit.key.noteId)->lyricTokenId);
    if (!oldLyric || !newLyric || oldLyric->language != newLyric->language ||
        !hasPronunciationService(oldLyric->language)) {
      edit.unresolved = true;
      continue;
    }
    const auto warned = [](const auto& resolved, domain::NoteId id) {
      return std::any_of(resolved.pronunciation.warnings.begin(), resolved.pronunciation.warnings.end(),
                         [id](const auto& warning) { return warning.noteId == id; });
    };
    const bool sameSounds = oldTokens.size() == newTokens.size() &&
        std::equal(oldTokens.begin(), oldTokens.end(), newTokens.begin(), [](const auto& a, const auto& b) {
          return a.symbol == b.symbol && a.role == b.role && a.voiced == b.voiced;
        });
    const auto context = phonemeEditContextId(newBase.value(), edit.key);
    if (!sameSounds || !context || warned(oldBase.value(), oldKey.noteId) || warned(newBase.value(), edit.key.noteId)) {
      edit.unresolved = true;
      continue;
    }
    edit.sourceContextId = context;
  }
  destination.phonemeOverrides.swap(overrides);
  return core::success();
}

core::Result<void> validateTransferredRenderEdits(
    const domain::VocalRegion& source, domain::VocalRegion& destination,
    std::span<const domain::PerformanceNoteRemap> mapping) {
  if (mapping.size() > 10000U || destination.unitSelectionOverrides.size() > 4096U || destination.seamOverrides.size() > 4096U) {
    return core::failure(core::ErrorCode::InvalidArgument, "Render edit transfer exceeds bounds");
  }
  std::unordered_map<domain::NoteId, domain::NoteId> sources;
  for (const auto& pair : mapping) {
    if (!source.findNote(pair.source) || !destination.findNote(pair.target) || !sources.emplace(pair.target, pair.source).second) {
      return core::failure(core::ErrorCode::InvalidArgument, "Invalid render edit transfer map");
    }
  }
  if (destination.unitSelectionOverrides.empty() && destination.seamOverrides.empty()) return core::success();
  const auto oldResolved = resolvePronunciation(source);
  const auto newResolved = resolvePronunciation(destination);
  const auto oldKey = [&](domain::PhonemeKey key) -> std::optional<domain::PhonemeKey> {
    const auto found = sources.find(key.noteId);
    if (found == sources.end()) return std::nullopt;
    return domain::PhonemeKey{found->second, key.ordinal};
  };
  const auto same = [&](const auto& a, const auto& b) {
    const auto mapped = oldKey(b.key);
    if (!mapped || *mapped != a.key || a.symbol != b.symbol || a.role != b.role || a.voiced != b.voiced) return false;
    const auto* oldLyric = source.findLyric(source.findNote(a.key.noteId)->lyricTokenId);
    const auto* newLyric = destination.findLyric(destination.findNote(b.key.noteId)->lyricTokenId);
    if (!oldLyric || !newLyric || oldLyric->language != newLyric->language ||
        !hasPronunciationService(oldLyric->language)) return false;
    const auto warned = [](const auto& warnings, auto id) {
      return std::any_of(warnings.begin(), warnings.end(), [id](const auto& warning) { return warning.noteId == id; });
    };
    return !warned(oldResolved.value().pronunciation.warnings, a.key.noteId) &&
           !warned(newResolved.value().pronunciation.warnings, b.key.noteId);
  };
  const auto validRelationship = [&](domain::PhonemeKey key, std::size_t count, bool seam) {
    const auto mapped = oldKey(key);
    if (!mapped || !oldResolved || !newResolved) return false;
    const auto& oldTokens = oldResolved.value().pronunciation.tokens;
    const auto& newTokens = newResolved.value().pronunciation.tokens;
    const auto a = std::find_if(oldTokens.begin(), oldTokens.end(), [&](const auto& token) { return token.key == *mapped; });
    const auto b = std::find_if(newTokens.begin(), newTokens.end(), [&](const auto& token) { return token.key == key; });
    if (a == oldTokens.end() || b == newTokens.end()) return false;
    if (seam) {
      if (!same(*a, *b)) return false;
      if (a == oldTokens.begin() || b == newTokens.begin()) return a == oldTokens.begin() && b == newTokens.begin();
      return same(*std::prev(a), *std::prev(b));
    }
    if (count == 0U || count > static_cast<std::size_t>(oldTokens.end() - a) || count > static_cast<std::size_t>(newTokens.end() - b)) return false;
    return std::equal(a, a + static_cast<std::ptrdiff_t>(count), b, same);
  };
  for (auto& edit : destination.unitSelectionOverrides) {
    const auto key = oldKey(edit.startKey);
    const auto* original = key ? source.findUnitSelectionOverride(*key) : nullptr;
    auto payload = edit;
    if (key) payload.startKey = *key;
    if (!original || payload != *original || original->unresolved || !validRelationship(edit.startKey, edit.tokenCount, false)) edit.unresolved = true;
  }
  for (auto& edit : destination.seamOverrides) {
    const auto key = oldKey(edit.incomingStartKey);
    const auto* original = key ? source.findSeamOverride(*key) : nullptr;
    auto payload = edit;
    if (key) payload.incomingStartKey = *key;
    if (!original || payload != *original || original->unresolved || !validRelationship(edit.incomingStartKey, 1U, true)) edit.unresolved = true;
  }
  return core::success();
}

}  // namespace seam::phonemizer
