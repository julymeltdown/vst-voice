#pragma once

// The ids that publish an entry of a list that is rebuilt, and the identity each one carries:
//
//   voicebank.card.<index>@<identity>   a card of the voice browser
//   support.item.<index>@<identity>     a report, or a file of the bundle, in the support panel
//   audio.device.<index>@<identity>     an output device of the audio settings
//
// Each names the entry by where it stands (the index, which is the order the list is shown in) and by
// what it is (sixteen hexadecimal digits). A list changes under its readers: a device is plugged in or
// removed, a voicebank is installed or refreshed, a report is written or deleted, and the entries after
// the one that changed move to other places. An id that a client kept from an earlier frame, or a press
// that was aimed at an earlier frame, must never act on the entry that has taken the place, so the
// identity is part of the id and the controller checks it before it acts. The diagnostics panel's ids
// follow the same rule (see diagnostic_ids.hpp).

#include "seam/authoring/voicebank_browser.hpp"
#include "seam/core/result.hpp"
#include "seam/native_ui/editor_scene.hpp"
#include "seam/native_ui/recovery_support_panel.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace seam::native_ui {

inline constexpr std::string_view kVoicebankCardIdPrefix = "voicebank.card.";
inline constexpr std::string_view kSupportItemIdPrefix = "support.item.";
inline constexpr std::string_view kAudioDeviceIdPrefix = "audio.device.";

// FNV-1a over 64 bits. A text is led by its length, so that two different sets of fields cannot run
// together into the same bytes. It tells entries apart that differ by accident; it does not resist a
// deliberately built collision.
class IdentityHash final {
public:
  void number(std::uint64_t value) noexcept {
    for (unsigned shift = 0U; shift < 64U; shift += 8U) byte(static_cast<unsigned char>(value >> shift));
  }
  void text(std::string_view value) noexcept {
    number(value.size());
    for (const char c : value) byte(static_cast<unsigned char>(c));
  }
  [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
  void byte(unsigned char value) noexcept {
    hash_ ^= value;
    hash_ *= 1099511628211ULL;
  }
  std::uint64_t hash_{14695981039346656037ULL};
};

// A bank card is its id, version and content plus its current selection capability; a support entry
// is its name and digest plus whether this mode permits selection; an audio device is its id. Fields
// that only change presentation or which entry is selected are left out.
[[nodiscard]] inline std::uint64_t voicebankCardIdentity(const authoring::VoicebankCard& card) noexcept {
  IdentityHash hash;
  hash.text(card.id);
  hash.text(card.version);
  hash.text(card.contentHash);
  // Selection capability changes whether the published control can activate or receive focus.
  // An id held from the frame before an untrusted card became selectable (or vice versa) must not
  // carry its old action set into the current card.
  hash.number(card.selectable ? 1U : 0U);
  return hash.value();
}

[[nodiscard]] inline std::uint64_t supportItemIdentity(const RecoverySupportItemView& item,
                                                       bool selectable) noexcept {
  IdentityHash hash;
  hash.text(item.name);
  hash.text(item.sha256);
  // Preview files can receive focus but cannot be selected; report rows can. Keep an id from one
  // mode from carrying the other mode's action set into the current list.
  hash.number(selectable ? 1U : 0U);
  return hash.value();
}

[[nodiscard]] inline std::uint64_t audioDeviceIdentity(
    const EditorSceneState::AudioDeviceOption& device) noexcept {
  IdentityHash hash;
  hash.text(device.id);
  return hash.value();
}

struct ListEntryId final {
  // Where the entry stood in the list when the id was made.
  std::size_t index{0U};
  // The entry's identity (voicebankCardIdentity and its kin) when the id was made.
  std::uint64_t identity{0U};
};

[[nodiscard]] inline std::string listEntryId(std::string_view prefix, std::size_t index,
                                             std::uint64_t identity) {
  static constexpr std::string_view kDigits = "0123456789abcdef";
  std::string out{prefix};
  out += std::to_string(index);
  out.push_back('@');
  for (int shift = 60; shift >= 0; shift -= 4)
    out.push_back(kDigits[static_cast<std::size_t>((identity >> shift) & 0xfU)]);
  return out;
}

[[nodiscard]] inline std::string voicebankCardId(std::size_t index, const authoring::VoicebankCard& card) {
  return listEntryId(kVoicebankCardIdPrefix, index, voicebankCardIdentity(card));
}

[[nodiscard]] inline std::string supportItemId(std::size_t index, const RecoverySupportItemView& item,
                                               bool selectable) {
  return listEntryId(kSupportItemIdPrefix, index, supportItemIdentity(item, selectable));
}

[[nodiscard]] inline std::string audioDeviceId(std::size_t index,
                                               const EditorSceneState::AudioDeviceOption& device) {
  return listEntryId(kAudioDeviceIdPrefix, index, audioDeviceIdentity(device));
}

// Takes such an id apart. A malformed one (no index, no identity, an identity that is not sixteen
// hexadecimal digits) is InvalidArgument; whether it still names an entry is for the caller, against the
// list as it stands.
[[nodiscard]] inline core::Result<ListEntryId> parseListEntryId(std::string_view prefix, std::string_view id) {
  const auto malformed = [] {
    return core::failure<ListEntryId>(core::ErrorCode::InvalidArgument,
                                      "List entry accessibility id is malformed");
  };
  if (!id.starts_with(prefix)) return malformed();
  const auto rest = id.substr(prefix.size());
  const auto at = rest.find('@');
  if (at == std::string_view::npos || at == 0U) return malformed();
  if (at > 1U && rest.front() == '0') return malformed();
  ListEntryId parsed;
  const auto indexEnd = rest.data() + at;
  const auto index = std::from_chars(rest.data(), indexEnd, parsed.index);
  if (index.ec != std::errc{} || index.ptr != indexEnd) return malformed();
  const auto digits = rest.substr(at + 1U);
  if (digits.size() != 16U) return malformed();
  for (const char digit : digits)
    if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
      return malformed();
  const auto identity = std::from_chars(digits.data(), digits.data() + digits.size(), parsed.identity, 16);
  if (identity.ec != std::errc{} || identity.ptr != digits.data() + digits.size()) return malformed();
  return core::success(parsed);
}

}  // namespace seam::native_ui
