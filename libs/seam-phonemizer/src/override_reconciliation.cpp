#include "seam/phonemizer/override_reconciliation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace seam::phonemizer {
namespace {

constexpr std::size_t kLimit = 256U;

bool sameSound(const domain::PhonemeToken& a, const domain::PhonemeToken& b) {
  return a.symbol == b.symbol && a.role == b.role && a.voiced == b.voiced;
}

core::Result<void> validateTokens(domain::NoteId noteId,
                                 std::span<const domain::PhonemeToken> tokens) {
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    const auto& token = tokens[index];
    const auto valid = token.validate();
    if (!valid) return valid;
    if (token.key.noteId != noteId || token.key.ordinal != index ||
        token.symbol.size() > 1024U || token.locked ||
        token.timing != domain::PhonemeTiming{}) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Reconciliation requires bounded unedited base tokens in ordinal order");
    }
    switch (token.role) {
      case domain::PhonemeRole::Onset:
      case domain::PhonemeRole::Nucleus:
      case domain::PhonemeRole::Coda:
      case domain::PhonemeRole::Geminate:
      case domain::PhonemeRole::Breath:
      case domain::PhonemeRole::Silence: break;
      default: return core::failure(core::ErrorCode::InvalidArgument, "Unknown base phoneme role");
    }
  }
  return core::success();
}

}  // namespace

core::Result<std::vector<ReconciledPhonemeEdit>> reconcilePhonemeOverrides(
    domain::NoteId noteId, std::span<const domain::PhonemeToken> before,
    std::span<const domain::PhonemeToken> after,
    std::span<const domain::PhonemeOverride> edits) {
  using Output = std::vector<ReconciledPhonemeEdit>;
  if (!noteId.valid() || before.size() > kLimit || after.size() > kLimit || edits.size() > kLimit) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Reconciliation input exceeds bounds");
  }
  const auto oldValid = validateTokens(noteId, before);
  if (!oldValid) return core::Result<Output>{oldValid.error()};
  const auto newValid = validateTokens(noteId, after);
  if (!newValid) return core::Result<Output>{newValid.error()};
  std::unordered_set<std::uint16_t> ordinals;
  for (const auto& edit : edits) {
    const auto valid = edit.validate();
    if (!valid) return core::Result<Output>{valid.error()};
    if (edit.key.noteId != noteId || !ordinals.insert(edit.key.ordinal).second ||
        (edit.symbol && edit.symbol->size() > 1024U)) {
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Invalid reconciliation edit identity");
    }
  }

  const auto n = before.size();
  const auto m = after.size();
  const auto width = m + 1U;
  std::vector<std::size_t> prefix((n + 1U) * width);
  std::vector<std::size_t> suffix((n + 1U) * width);
  const auto at = [width](std::size_t i, std::size_t j) { return i * width + j; };
  for (std::size_t i = 0; i < n; ++i) {
    for (std::size_t j = 0; j < m; ++j) {
      prefix[at(i + 1U, j + 1U)] = sameSound(before[i], after[j])
          ? prefix[at(i, j)] + 1U
          : std::max(prefix[at(i, j + 1U)], prefix[at(i + 1U, j)]);
    }
  }
  for (std::size_t i = n; i-- > 0U;) {
    for (std::size_t j = m; j-- > 0U;) {
      suffix[at(i, j)] = sameSound(before[i], after[j])
          ? suffix[at(i + 1U, j + 1U)] + 1U
          : std::max(suffix[at(i + 1U, j)], suffix[at(i, j + 1U)]);
    }
  }
  const auto optimum = prefix[at(n, m)];
  Output result;
  result.reserve(edits.size());
  for (const auto& edit : edits) {
    ReconciledPhonemeEdit item{edit, std::nullopt, EditCorrespondence::RemovedOrAmbiguous};
    const auto i = static_cast<std::size_t>(edit.key.ordinal);
    if (i >= n) {
      item.correspondence = EditCorrespondence::MissingOriginal;
    } else {
      bool skippable = false;
      for (std::size_t j = 0; j <= m; ++j) {
        if (prefix[at(i, j)] + suffix[at(i + 1U, j)] == optimum) skippable = true;
      }
      std::optional<std::size_t> candidate;
      bool ambiguous = false;
      for (std::size_t j = 0; j < m && !skippable; ++j) {
        if (sameSound(before[i], after[j]) &&
            prefix[at(i, j)] + 1U + suffix[at(i + 1U, j + 1U)] == optimum) {
          if (candidate) ambiguous = true;
          candidate = j;
        }
      }
      if (!skippable && !ambiguous && candidate) {
        item.rebound = edit;
        item.rebound->key = after[*candidate].key;
        item.correspondence = EditCorrespondence::Matched;
      }
    }
    result.push_back(std::move(item));
  }
  return core::success(std::move(result));
}

core::Result<std::optional<domain::PhonemeKey>> reconcilePhonemeSpan(
    domain::NoteId noteId, std::span<const domain::PhonemeToken> before,
    std::span<const domain::PhonemeToken> after,
    domain::PhonemeKey start, std::uint16_t tokenCount) {
  using Output = std::optional<domain::PhonemeKey>;
  if (!noteId.valid() || start.noteId != noteId || tokenCount == 0U || tokenCount > kLimit ||
      before.size() > kLimit || after.size() > kLimit) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                "Invalid phoneme span identity or count");
  }
  // Validate the base sequences even if the saved span no longer exists.
  const auto oldValid = validateTokens(noteId, before);
  if (!oldValid) return core::Result<Output>{oldValid.error()};
  const auto newValid = validateTokens(noteId, after);
  if (!newValid) return core::Result<Output>{newValid.error()};
  const auto first = static_cast<std::size_t>(start.ordinal);
  const auto count = static_cast<std::size_t>(tokenCount);
  if (first >= before.size() || count > before.size() - first) {
    return core::success(Output{});
  }
  std::vector<domain::PhonemeOverride> probes;
  probes.reserve(count);
  for (std::size_t offset = 0U; offset < count; ++offset) {
    probes.push_back({.key = before[first + offset].key});
  }
  const auto matched = reconcilePhonemeOverrides(noteId, before, after, probes);
  if (!matched) return core::Result<Output>{matched.error()};
  Output rebound;
  for (std::size_t offset = 0U; offset < count; ++offset) {
    const auto& edit = matched.value()[offset];
    if (!edit.rebound) return core::success(Output{});
    if (!rebound) rebound = edit.rebound->key;
    if (static_cast<std::size_t>(edit.rebound->key.ordinal) !=
        static_cast<std::size_t>(rebound->ordinal) + offset) {
      return core::success(Output{});
    }
  }
  return core::success(rebound);
}

core::Result<RegionPhonemeCorrespondence> reconcileRegionPhonemes(
    std::span<const domain::PhonemeToken> before,
    std::span<const domain::PhonemeToken> after) {
  using Output = RegionPhonemeCorrespondence;
  struct Group final { std::size_t start; std::size_t count; };
  using Groups = std::unordered_map<domain::NoteId, Group>;
  if (before.size() > 4096U || after.size() > 4096U) {
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Region phoneme count exceeds bounds");
  }
  const auto group = [](std::span<const domain::PhonemeToken> tokens) -> core::Result<Groups> {
    Groups result;
    for (std::size_t i = 0U; i < tokens.size();) {
      const auto id = tokens[i].key.noteId;
      auto end = i + 1U;
      while (end < tokens.size() && tokens[end].key.noteId == id) ++end;
      if (end - i > kLimit || result.contains(id)) {
        return core::failure<Groups>(core::ErrorCode::InvalidArgument,
                                     "Region note groups must be unique and bounded");
      }
      const auto valid = validateTokens(id, tokens.subspan(i, end - i));
      if (!valid) return core::Result<Groups>{valid.error()};
      result.emplace(id, Group{i, end - i});
      i = end;
    }
    return core::success(std::move(result));
  };
  const auto oldGroups = group(before);
  if (!oldGroups) return core::Result<Output>{oldGroups.error()};
  const auto newGroups = group(after);
  if (!newGroups) return core::Result<Output>{newGroups.error()};
  Output result;
  for (const auto& token : before) result.before_.push_back(token.key);
  for (const auto& token : after) result.after_.push_back(token.key);
  result.targets_.resize(before.size());
  for (const auto& [id, oldGroup] : oldGroups.value()) {
    const auto found = newGroups.value().find(id);
    if (found == newGroups.value().end()) continue;
    const auto& newGroup = found->second;
    std::vector<domain::PhonemeOverride> probes;
    probes.reserve(oldGroup.count);
    for (std::size_t i = 0U; i < oldGroup.count; ++i) {
      probes.push_back({.key = before[oldGroup.start + i].key});
    }
    const auto matched = reconcilePhonemeOverrides(id,
        before.subspan(oldGroup.start, oldGroup.count),
        after.subspan(newGroup.start, newGroup.count), probes);
    if (!matched) return core::Result<Output>{matched.error()};
    for (std::size_t i = 0U; i < oldGroup.count; ++i) {
      if (matched.value()[i].rebound) {
        result.targets_[oldGroup.start + i] =
            newGroup.start + matched.value()[i].rebound->key.ordinal;
      }
    }
  }
  return core::success(std::move(result));
}

std::optional<domain::PhonemeKey> RegionPhonemeCorrespondence::mapSpan(
    domain::PhonemeKey start, std::uint16_t count) const {
  const auto found = std::find(before_.begin(), before_.end(), start);
  if (found == before_.end() || count == 0U) return std::nullopt;
  const auto index = static_cast<std::size_t>(found - before_.begin());
  if (count > before_.size() - index || !targets_[index]) return std::nullopt;
  const auto first = *targets_[index];
  for (std::size_t i = 0U; i < count; ++i) {
    if (!targets_[index + i] || *targets_[index + i] != first + i) return std::nullopt;
  }
  return after_[first];
}

std::optional<domain::PhonemeKey> RegionPhonemeCorrespondence::mapBoundary(
    domain::PhonemeKey incoming) const {
  const auto found = std::find(before_.begin(), before_.end(), incoming);
  if (found == before_.end()) return std::nullopt;
  const auto index = static_cast<std::size_t>(found - before_.begin());
  if (!targets_[index]) return std::nullopt;
  const auto target = *targets_[index];
  if (index == 0U) {
    if (target != 0U) return std::nullopt;
  } else if (!targets_[index - 1U] || *targets_[index - 1U] + 1U != target) {
    return std::nullopt;
  }
  return after_[target];
}

}  // namespace seam::phonemizer
