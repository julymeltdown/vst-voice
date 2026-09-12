#include "seam/authoring/japanese_reading_capture.hpp"
#include "seam/platform/helper_process.hpp"
#include "seam/authoring/japanese_reading_response.hpp"
#include "seam/application/lyric_commands.hpp"
#include "seam/core/sha256.hpp"
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <unordered_map>

namespace seam::authoring {
core::Result<JapaneseReadingCapture> JapaneseReadingCapture::prepare(const application::EditorSession& session,
    domain::RegionId regionId, std::span<const domain::NoteId> requested, StagedJapaneseReadingResource resource) {
  const auto fail = [](const char* message) { return core::failure<JapaneseReadingCapture>(core::ErrorCode::InvalidArgument, message); };
  const auto* region = session.project().findRegion(regionId);
  if (!region || region->notes.size() > 10000U || region->lyrics.size() > 10000U || requested.empty() || requested.size() > 10000U)
    return fail("Choose a bounded reading phrase in one region");
  std::unordered_set<domain::NoteId> selected(requested.begin(), requested.end());
  if (selected.size() != requested.size()) return fail("Reading note selection contains duplicate identities");
  std::unordered_map<domain::LyricTokenId, const domain::LyricToken*> lyrics;
  for (const auto& lyric : region->lyrics) if (!lyrics.emplace(lyric.id, &lyric).second) return fail("Reading lyric identities are ambiguous");
  std::vector<const domain::Note*> ordered; std::unordered_set<domain::NoteId> ids;
  for (const auto& note : region->notes) {
    if (!note.validate() || !ids.insert(note.id).second) return fail("Reading note identity or timing is invalid");
    ordered.push_back(&note);
  }
  std::sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
    return a->startTick == b->startTick ? a->id < b->id : a->startTick < b->startTick;
  });
  std::string source; std::vector<JapaneseReadingOwner> owners;
  const domain::Note* previous = nullptr; bool leftSelection = false;
  for (const auto* note : ordered) {
    if (!selected.contains(note->id)) { if (previous) leftSelection = true; continue; }
    if (leftSelection) return fail("Reading phrase must not skip intervening notes");
    if (previous && note->startTick < previous->endTick()) return fail("Choose a non-overlapping phrase for contextual reading");
    const auto found = lyrics.find(note->lyricTokenId);
    if (found == lyrics.end() || found->second->surface.empty() || found->second->surface.size() > 4096U)
      return fail("Reading note has no bounded lyric source");
    const auto& lyric = *found->second;
    if (lyric.language != domain::Language::Japanese && lyric.language != domain::Language::Unspecified)
      return fail("Japanese reading requires Japanese or unspecified lyric language");
    if (previous && domain::continuesSharedLyric(*previous, *note)) {
      owners.push_back({note->id, lyric.id, owners.back().byteOffset, owners.back().byteLength, note->phoneticHint.has_value()});
    } else {
      if (previous && note->startTick > previous->endTick()) source += ' '; // Preserve a rest as a lexical separator.
      const auto text = domain::toUtf8(lyric.surface);
      if (source.size() > 4096U || text.size() > 4096U - source.size()) return fail("Reading phrase exceeds 4096 UTF-8 bytes");
      const auto decoded = domain::fromUtf8(text);
      if (!decoded || decoded.value() != lyric.surface || text.find('\0') != std::string::npos) return fail("Reading lyric contains invalid Unicode or NUL");
      owners.push_back({note->id, lyric.id, source.size(), text.size(), note->phoneticHint.has_value()}); source += text;
    }
    previous = note;
  }
  if (owners.size() != requested.size()) return fail("Reading selection contains missing notes");
  auto context = session.capturePerformanceJob(); if (!context) return core::Result<JapaneseReadingCapture>{context.error()};
  return JapaneseReadingCapture{std::move(context.value()), session.revision(), regionId, std::move(resource), std::move(source), std::move(owners)};
}
bool JapaneseReadingCapture::matches(const application::EditorSession& session, domain::RegionId region,
    const phonemizer::JapaneseReadingIdentity& currentIdentity) const {
  return region == region_ && session.revision() == revision_ && currentIdentity == resource_.resource().identity() &&
      session.project() == context_.sourceProject() && static_cast<bool>(session.validatePerformanceJob(context_));
}
core::Result<JapaneseReadingReview> JapaneseReadingCapture::bind(phonemizer::JapaneseReadingResult result, std::stop_token stop) const {
  const auto valid = phonemizer::validateJapaneseReading(source_, result, resource_.resource().identity(), stop);
  if (!valid) return core::Result<JapaneseReadingReview>{valid.error()};
  const auto projections = phonemizer::projectJapaneseReadingPhones(result, stop);
  if (!projections) return core::Result<JapaneseReadingReview>{projections.error()};
  if (!owners_.empty() && result.tokens.size() > 4194304U / owners_.size())
    return core::failure<JapaneseReadingReview>(core::ErrorCode::Unsupported, "Reading ownership query exceeds work budget");
  std::vector<JapaneseReadingBinding> bindings;
  for (const auto& token : result.tokens) {
    if (stop.stop_requested()) return core::failure<JapaneseReadingReview>(core::ErrorCode::Conflict, "Reading binding cancelled");
    JapaneseReadingBinding binding; std::unordered_set<domain::LyricTokenId> lyrics;
    std::size_t coveredEnd = token.byteOffset;
    for (const auto& owner : owners_) {
      const auto first = std::max(token.byteOffset, owner.byteOffset);
      const auto last = std::min(token.byteOffset + token.byteLength, owner.byteOffset + owner.byteLength);
      if (first >= last) continue;
      if (first > coveredEnd) binding.crossesUnownedText = true;
      coveredEnd = std::max(coveredEnd, last);
      binding.notes.push_back(owner.note); lyrics.insert(owner.lyric);
      binding.touchesExplicitHint = binding.touchesExplicitHint || owner.explicitHint;
    }
    binding.crossesUnownedText = binding.crossesUnownedText || coveredEnd != token.byteOffset + token.byteLength;
    binding.crossesLyrics = lyrics.size() > 1U;
    bindings.push_back(std::move(binding));
  }
  return JapaneseReadingReview{std::move(result), std::move(bindings), std::move(projections.value())};
}
core::Result<JapaneseReadingReview> JapaneseReadingCapture::read(std::stop_token stop) const {
  const auto& resource = resource_.resource();
  const auto current = resource.revalidate(stop); if (!current) return core::Result<JapaneseReadingReview>{current.error()};
  platform::HelperProcessRequest request{resource.spec().executable, {"--read-stdin", resource.spec().dictionaryDirectory.string()}};
  request.standardInput = source_;
  request.maximumResidentBytes = 512U * 1024U * 1024U;
  request.maximumCpuTime = std::chrono::milliseconds{5000};
  const auto output = platform::runBoundedHelperProcess(request, stop); if (!output) return core::Result<JapaneseReadingReview>{output.error()};
  const auto retained = resource.revalidate(stop); if (!retained) return core::Result<JapaneseReadingReview>{retained.error()};
  auto result = decodeJapaneseReadingResponse(source_, output.value().standardOutput, resource.identity(), stop);
  if (!result) return core::Result<JapaneseReadingReview>{result.error()};
  return bind(std::move(result.value()), stop);
}

core::Result<void> JapaneseReadingCapture::validateApplyPlan(const JapaneseReadingReview& review) const {
  if (review.reading.identity != resource_.resource().identity() ||
      review.reading.tokens.size() != review.bindings.size() ||
      review.phoneProjections.size() != review.reading.tokens.size())
    return core::failure(core::ErrorCode::Conflict, "Japanese reading result identity or shape is stale");

  std::unordered_set<domain::NoteId> ownerIds;
  ownerIds.reserve(owners_.size());
  for (const auto& owner : owners_) ownerIds.insert(owner.note);

  std::unordered_map<domain::NoteId, std::string> phonesByNote;
  phonesByNote.reserve(owners_.size());
  for (std::size_t index = 0U; index < review.reading.tokens.size(); ++index) {
    const auto& token = review.reading.tokens[index];
    const auto& binding = review.bindings[index];
    const auto& projection = review.phoneProjections[index];
    if (token.status != phonemizer::JapaneseReadingStatus::Known ||
        projection.readingTokenIndex != index || projection.phones.empty() ||
        binding.notes.size() != 1U || binding.touchesExplicitHint ||
        binding.crossesLyrics || binding.crossesUnownedText)
      return core::failure(core::ErrorCode::Conflict, "Only complete single-note readings without explicit hints can be applied");
    const auto note = binding.notes.front();
    if (!ownerIds.contains(note))
      return core::failure(core::ErrorCode::Conflict, "Japanese reading result references an unselected note");
    auto& target = phonesByNote[note];
    for (const auto& phone : projection.phones) {
      if (phone.empty() || phone.find_first_of(" \t\r\n\0") != std::string::npos || phone.size() > 256U)
        return core::failure(core::ErrorCode::InvalidArgument, "Projected phone is invalid");
      if (target.size() > 4096U - phone.size() - (target.empty() ? 0U : 1U))
        return core::failure(core::ErrorCode::InvalidArgument, "Projected phone hint exceeds bounds");
      if (!target.empty()) target.push_back(' ');
      target += phone;
    }
  }
  if (phonesByNote.size() != owners_.size())
    return core::failure(core::ErrorCode::Conflict, "Every selected note needs one unambiguous known reading");
  for (const auto& owner : owners_) {
    if (!phonesByNote.contains(owner.note))
      return core::failure(core::ErrorCode::Conflict, "Japanese reading note ownership is incomplete");
  }
  return core::success();
}

core::Result<void> JapaneseReadingCapture::apply(JapaneseReadingReview review,
    application::EditorSession& session) const {
  if (!matches(session, region_, resource_.resource().identity()))
    return core::failure(core::ErrorCode::Conflict, "Japanese reading source or document changed before Apply");
  const auto plan = validateApplyPlan(review);
  if (!plan) return plan;
  std::unordered_map<domain::NoteId, std::string> phonesByNote;
  std::vector<std::string> sequence;
  sequence.reserve(review.reading.tokens.size());
  for (std::size_t index = 0U; index < review.reading.tokens.size(); ++index) {
    const auto& binding = review.bindings[index];
    const auto& projection = review.phoneProjections[index];
    auto& target = phonesByNote[binding.notes.front()];
    for (const auto& phone : projection.phones) {
      sequence.push_back(phone);
      if (!target.empty()) target.push_back(' ');
      target += phone;
    }
  }
  std::vector<application::NoteExpressionEdit> edits; edits.reserve(owners_.size());
  for (const auto& owner : owners_) {
    const auto found = phonesByNote.find(owner.note); if (found == phonesByNote.end())
      return core::failure(core::ErrorCode::Conflict, "Japanese reading note ownership is incomplete");
    const auto* note = context_.sourceProject().findNote(owner.note); if (!note)
      return core::failure(core::ErrorCode::NotFound, "Japanese reading source note disappeared");
    if (note->phoneticHint) return core::failure(core::ErrorCode::Conflict, "Explicit phone hint requires manual review");
    edits.push_back({note->id, note->vibrato, found->second});
  }
  core::Sha256 sequenceHash; sequenceHash.update("seam-ja-reading-projection-v1\n");
  for (const auto& phone : sequence) { sequenceHash.update(std::to_string(phone.size())); sequenceHash.update(":"); sequenceHash.update(phone); sequenceHash.update("\n"); }
  domain::PronunciationIdentity identity{domain::Language::Japanese, "open-jtalk-mecab-naist", resource_.resource().identity().engineRevision +
      "-" + resource_.resource().identity().helperSha256.substr(0U, 16U), resource_.resource().identity().dictionarySha256,
      core::sha256Hex(source_), sequenceHash.hexDigest()};
  const auto valid = identity.validate(); if (!valid) return valid;
  return session.executePerformanceResult(context_, std::make_unique<application::ApplyJapaneseReadingHintsCommand>(
      region_, std::move(edits), std::move(identity)));
}
}
