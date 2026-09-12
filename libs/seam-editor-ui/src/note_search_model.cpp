#include "seam/ui/note_search_model.hpp"
#include "seam/ui/piano_roll_model.hpp"
#include "seam/phonemizer/language_resolver.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace seam::ui {
core::Result<LyricReplacementPreview> NoteSearchModel::previewLyricDistribution(
    const domain::Project& project, domain::RegionId regionId, std::uint64_t revision,
    const std::vector<domain::NoteId>& selectedNotes, std::string_view utf8Text, std::stop_token stop) {
  if (stop.stop_requested()) return core::failure<LyricReplacementPreview>(core::ErrorCode::Conflict, "Distribution preview cancelled");
  if (utf8Text.size() > maximumTextScalars * 4U)
    return core::failure<LyricReplacementPreview>(core::ErrorCode::InvalidArgument, "Distribution exceeds text bounds");
  auto decoded = domain::fromUtf8(std::string{utf8Text}); if (!decoded) return core::Result<LyricReplacementPreview>{decoded.error()};
  auto plan = PianoRollModel::planLyricDistribution(project, regionId, selectedNotes, std::move(decoded.value()), std::nullopt, stop);
  if (!plan) return core::Result<LyricReplacementPreview>{plan.error()};
  const auto& report = plan.value().report;
  if (report.missingSyllables || report.leftoverSyllables)
    return core::failure<LyricReplacementPreview>(core::ErrorCode::Conflict,
        "Syllable count must match distinct selected lyric tokens",
        "requested=" + std::to_string(report.requestedSyllables) + ", target=" + std::to_string(report.targetLyrics));
  LyricReplacementPreview preview;
  preview.projectId_ = project.id(); preview.regionId_ = regionId; preview.revision_ = revision;
  preview.selectedNotes_ = selectedNotes; std::sort(preview.selectedNotes_->begin(), preview.selectedNotes_->end());
  preview.matchedNotes_ = selectedNotes.size(); preview.replacements_ = report.changedLyrics;
  preview.edits_ = std::move(plan.value().edits);
  const auto& region = *project.findRegion(regionId);
  preview.dependencyRecords_ = region.phonemeOverrides.size() + region.unitSelectionOverrides.size() + region.seamOverrides.size();
  std::unordered_set<domain::LyricTokenId> changed;
  for (const auto& edit : preview.edits_) changed.insert(edit.lyricId);
  for (const auto& note : region.notes) if (changed.contains(note.lyricTokenId)) ++preview.changedNotes_;
  if (stop.stop_requested()) return core::failure<LyricReplacementPreview>(core::ErrorCode::Conflict, "Distribution preview cancelled");
  return core::success(std::move(preview));
}
core::Result<LyricReplacementPreview> NoteSearchModel::previewLyricReplacement(
    const application::EditorSession& session, domain::RegionId regionId,
    std::string_view utf8Query, std::string_view utf8Replacement, std::stop_token stop) {
  return previewLyricReplacement(session.project(), regionId, session.revision(), utf8Query, utf8Replacement, stop);
}

core::Result<LyricReplacementPreview> NoteSearchModel::previewLyricReplacement(
    const domain::Project& project, domain::RegionId regionId, std::uint64_t revision,
    std::string_view utf8Query, std::string_view utf8Replacement, std::stop_token stop) {
  if (utf8Replacement.size() > maximumQueryScalars * 4U)
    return core::failure<LyricReplacementPreview>(core::ErrorCode::InvalidArgument, "Replacement text exceeds bounds");
  auto replacement = domain::fromUtf8(std::string{utf8Replacement});
  if (!replacement) return core::Result<LyricReplacementPreview>{replacement.error()};
  if (replacement.value().size() > maximumQueryScalars)
    return core::failure<LyricReplacementPreview>(core::ErrorCode::InvalidArgument, "Replacement exceeds 256 characters");
  const auto found = search(project, regionId, revision, utf8Query, NoteSearchField::Lyric, stop);
  if (!found) return core::Result<LyricReplacementPreview>{found.error()};
  const auto* region = project.findRegion(regionId);
  LyricReplacementPreview preview; preview.projectId_ = project.id(); preview.regionId_ = regionId;
  preview.revision_ = revision; preview.matchedNotes_ = found.value().hits.size();
  preview.dependencyRecords_ = region->phonemeOverrides.size() + region->unitSelectionOverrides.size() + region->seamOverrides.size();
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region->lyrics) lyrics.emplace(lyric.id, &lyric);
  std::unordered_map<domain::LyricTokenId, bool> changed;
  std::size_t outputScalars = 0U;
  const auto& needle = found.value().query;
  std::vector<std::size_t> prefix(needle.size(), 0U);
  for (std::size_t i = 1U, j = 0U; i < needle.size(); ++i) {
    while (j && needle[i] != needle[j]) j = prefix[j - 1U];
    if (needle[i] == needle[j]) ++j;
    prefix[i] = j;
  }
  for (const auto& hit : found.value().hits) {
    if (stop.stop_requested()) return core::failure<LyricReplacementPreview>(core::ErrorCode::Conflict, "Replacement preview cancelled");
    if (const auto existing = changed.find(hit.lyricId); existing != changed.end()) {
      if (existing->second) ++preview.changedNotes_;
      continue;
    }
    std::u32string after;
    std::size_t copied = 0U, occurrences = 0U;
    const auto append = [&](std::u32string_view text) -> bool {
      if (text.size() > maximumTextScalars - outputScalars) return false;
      after.append(text); outputScalars += text.size(); return true;
    };
    for (std::size_t i = 0U, j = 0U; i < hit.matchedText.size(); ++i) {
      if ((i & 4095U) == 0U && stop.stop_requested()) return core::failure<LyricReplacementPreview>(core::ErrorCode::Conflict, "Replacement preview cancelled");
      while (j && hit.matchedText[i] != needle[j]) j = prefix[j - 1U];
      if (hit.matchedText[i] == needle[j]) ++j;
      if (j == needle.size()) {
        const auto start = i + 1U - j;
        if (!append(std::u32string_view{hit.matchedText}.substr(copied, start - copied)) || !append(replacement.value()))
          return core::failure<LyricReplacementPreview>(core::ErrorCode::InvalidArgument, "Replacement output exceeds text budget");
        copied = i + 1U; ++occurrences; j = 0U; // Non-overlapping replacement semantics.
      }
    }
    if (!append(std::u32string_view{hit.matchedText}.substr(copied)) || after.empty())
      return core::failure<LyricReplacementPreview>(core::ErrorCode::InvalidArgument, "Replacement would produce an empty or oversized lyric");
    const bool differs = after != hit.matchedText; changed.emplace(hit.lyricId, differs);
    if (differs) {
      const auto language = lyrics.at(hit.lyricId)->language;
      preview.edits_.push_back({hit.lyricId, hit.matchedText, std::move(after), language, language});
      ++preview.changedNotes_; preview.replacements_ += occurrences;
    }
  }
  if (stop.stop_requested()) return core::failure<LyricReplacementPreview>(core::ErrorCode::Conflict, "Replacement preview cancelled");
  return core::success(std::move(preview));
}

core::Result<void> LyricReplacementPreview::apply(application::EditorSession& session, std::stop_token stop) const {
  if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Reviewed replacement cancelled");
  if (session.project().id() != projectId_ || session.revision() != revision_ || !matchesSelection(session))
    return core::failure(core::ErrorCode::Conflict, "Search replacement preview is stale");
  const auto* region = session.project().findRegion(regionId_);
  if (!region) return core::failure(core::ErrorCode::NotFound, "Reviewed replacement region is missing");
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region->lyrics) lyrics.emplace(lyric.id, &lyric);
  for (const auto& edit : edits_) {
    const auto found = lyrics.find(edit.lyricId);
    if (found == lyrics.end() || found->second->surface != edit.before || found->second->language != edit.beforeLanguage)
      return core::failure(core::ErrorCode::Conflict, "Reviewed lyric changed after preview");
  }
  if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Reviewed replacement cancelled");
  if (edits_.empty()) return core::success();
  return session.execute(std::make_unique<application::BatchSetLyricsCommand>(edits_));
}

core::Result<NoteSearchResult> NoteSearchModel::search(const domain::Project& project,
    domain::RegionId regionId, std::uint64_t revision, std::string_view utf8Query,
    NoteSearchField field, std::stop_token stop) {
  const auto cancelled = [] { return core::failure<NoteSearchResult>(core::ErrorCode::Conflict, "Note search cancelled"); };
  if (stop.stop_requested()) return cancelled();
  if (utf8Query.empty() || utf8Query.size() > maximumQueryScalars * 4U)
    return core::failure<NoteSearchResult>(core::ErrorCode::InvalidArgument, "Enter a nonempty search query of at most 256 characters");
  auto query = domain::fromUtf8(std::string{utf8Query});
  if (!query) return core::Result<NoteSearchResult>{query.error()};
  if (query.value().empty() || query.value().size() > maximumQueryScalars ||
      (field != NoteSearchField::Lyric && field != NoteSearchField::PronunciationHint &&
       field != NoteSearchField::NoteId && field != NoteSearchField::GeneratedPhoneme &&
       field != NoteSearchField::PronunciationDiagnostic))
    return core::failure<NoteSearchResult>(core::ErrorCode::InvalidArgument, "Invalid note-search query or field");
  const auto* region = project.findRegion(regionId);
  if (!region) return core::failure<NoteSearchResult>(core::ErrorCode::NotFound, "Search region is missing");
  if (region->notes.size() > maximumNotes || region->lyrics.size() > maximumNotes)
    return core::failure<NoteSearchResult>(core::ErrorCode::InvalidArgument, "Note search supports at most 10000 notes and lyric tokens");
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region->lyrics) {
    if (stop.stop_requested()) return cancelled();
    if (!lyrics.emplace(lyric.id, &lyric).second)
      return core::failure<NoteSearchResult>(core::ErrorCode::Conflict, "Search region has duplicate lyric identities");
  }
  std::unordered_map<domain::NoteId, std::u32string> derivedText;
  if (field == NoteSearchField::GeneratedPhoneme || field == NoteSearchField::PronunciationDiagnostic) {
    // Resolve once in region context: per-note resolution would lose continuation
    // and neighboring pronunciation behavior. This bounded call is synchronous.
    const auto resolved = phonemizer::resolvePronunciation(*region, stop);
    if (!resolved) return core::Result<NoteSearchResult>{resolved.error()};
    if (stop.stop_requested()) return cancelled();
    std::size_t derivedScalars = 0U;
    const auto append = [&](domain::NoteId id, std::string_view value, char32_t separator) -> core::Result<void> {
      if (stop.stop_requested()) return core::failure(core::ErrorCode::Conflict, "Note search cancelled");
      if (value.size() > maximumTextScalars * 4U)
        return core::failure(core::ErrorCode::InvalidArgument, "Derived search text exceeds bounds");
      auto decoded = domain::fromUtf8(std::string{value}); if (!decoded) return core::Result<void>{decoded.error()};
      auto& target = derivedText[id];
      const auto extra = decoded.value().size() + (target.empty() ? 0U : 1U);
      if (extra > maximumTextScalars - derivedScalars)
        return core::failure(core::ErrorCode::InvalidArgument, "Derived search text exceeds the 4M-character budget");
      if (!target.empty()) target.push_back(separator);
      target.append(decoded.value()); derivedScalars += extra; return core::success();
    };
    if (field == NoteSearchField::GeneratedPhoneme) {
      for (const auto& token : resolved.value().pronunciation.tokens) {
        const auto added = append(token.key.noteId, token.symbol, U' ');
        if (!added) return core::Result<NoteSearchResult>{added.error()};
      }
    } else {
      for (const auto& warning : resolved.value().pronunciation.warnings) {
        const auto added = append(warning.noteId, warning.message, U'\n');
        if (!added) return core::Result<NoteSearchResult>{added.error()};
      }
    }
  }
  const auto& needle = query.value();
  std::vector<std::size_t> prefix(needle.size(), 0U);
  for (std::size_t i = 1U, j = 0U; i < needle.size(); ++i) {
    while (j && needle[i] != needle[j]) j = prefix[j - 1U];
    if (needle[i] == needle[j]) ++j;
    prefix[i] = j;
  }
  NoteSearchResult result{project.id(), regionId, revision, field, std::move(query.value()), 0U, {}};
  std::size_t scannedScalars = 0U;
  std::unordered_set<domain::NoteId> noteIds;
  for (const auto& note : region->notes) {
    if (stop.stop_requested()) return cancelled();
    const auto validNote = note.validate(); if (!validNote) return core::Result<NoteSearchResult>{validNote.error()};
    if (!noteIds.insert(note.id).second) return core::failure<NoteSearchResult>(core::ErrorCode::Conflict, "Search region has duplicate note identities");
    const auto lyric = lyrics.find(note.lyricTokenId);
    if (lyric == lyrics.end()) return core::failure<NoteSearchResult>(core::ErrorCode::Conflict, "Search note references a missing lyric");
    std::u32string decodedHint;
    const std::u32string* text = &lyric->second->surface;
    if (field == NoteSearchField::PronunciationHint) {
      if (note.phoneticHint && note.phoneticHint->size() > 4096U)
        return core::failure<NoteSearchResult>(core::ErrorCode::InvalidArgument, "Pronunciation hint exceeds supported bounds");
      auto decoded = domain::fromUtf8(note.phoneticHint.value_or(""));
      if (!decoded) return core::Result<NoteSearchResult>{decoded.error()};
      decodedHint = std::move(decoded.value()); text = &decodedHint;
    } else if (field == NoteSearchField::NoteId) {
      const auto id = note.id.toString(); decodedHint.assign(id.begin(), id.end()); text = &decodedHint;
    } else if (field == NoteSearchField::GeneratedPhoneme || field == NoteSearchField::PronunciationDiagnostic) {
      const auto found = derivedText.find(note.id);
      text = found == derivedText.end() ? &decodedHint : &found->second;
    }
    if (text->size() > maximumTextScalars - scannedScalars)
      return core::failure<NoteSearchResult>(core::ErrorCode::InvalidArgument, "Search text exceeds the 4M-character budget");
    scannedScalars += text->size(); ++result.scannedNotes;
    std::optional<std::size_t> match;
    for (std::size_t i = 0U, j = 0U; i < text->size(); ++i) {
      if ((i & 4095U) == 0U && stop.stop_requested()) return cancelled();
      const auto scalar = (*text)[i];
      if (scalar > 0x10ffffU || (scalar >= 0xd800U && scalar <= 0xdfffU))
        return core::failure<NoteSearchResult>(core::ErrorCode::InvalidArgument, "Search text contains invalid Unicode");
      if (match) continue; // Continue validation and cancellation after the first occurrence.
      while (j && scalar != result.query[j]) j = prefix[j - 1U];
      if (scalar == result.query[j]) ++j;
      if (j == result.query.size()) match = i + 1U - j;
    }
    if (match) result.hits.push_back({note.id, note.lyricTokenId, note.startTick, *text, *match});
  }
  if (stop.stop_requested()) return cancelled();
  std::sort(result.hits.begin(), result.hits.end(), [](const auto& a, const auto& b) {
    return a.start != b.start ? a.start < b.start : a.noteId < b.noteId;
  });
  if (stop.stop_requested()) return cancelled();
  return core::success(std::move(result));
}
}
