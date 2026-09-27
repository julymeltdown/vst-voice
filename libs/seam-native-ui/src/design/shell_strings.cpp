#include "seam/native_ui/design/shell_strings.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace seam::native_ui::design {
namespace {

struct Entry final {
  std::string_view key;
  std::string_view english;
};

constexpr Entry kEntries[] = {
#define SEAM_SHELL_STRING(key, english) {#key, english},
#include "seam/native_ui/design/shell_strings.def"
#undef SEAM_SHELL_STRING
};
constexpr std::size_t kCount = sizeof(kEntries) / sizeof(kEntries[0]);

const ShellStringTable& englishTable() {
  static const ShellStringTable table;
  return table;
}

std::atomic<const ShellStringTable*> installed{nullptr};

// Accented stand-ins for ASCII letters, as UTF-8, so pseudo-localized text keeps its word shapes.
std::string_view accented(char c) noexcept {
  switch (c) {
    case 'a': return "\u00e1"; case 'e': return "\u00e9"; case 'i': return "\u00ed";
    case 'o': return "\u00f3"; case 'u': return "\u00fa"; case 'n': return "\u00f1";
    case 'c': return "\u00e7"; case 'y': return "\u00fd"; case 'A': return "\u00c1";
    case 'E': return "\u00c9"; case 'I': return "\u00cd"; case 'O': return "\u00d3";
    case 'U': return "\u00da"; case 'N': return "\u00d1"; case 'C': return "\u00c7";
    default: return {};
  }
}

std::size_t characters(std::string_view utf8) noexcept {
  std::size_t count = 0U;
  for (const auto byte : utf8)
    if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U) ++count;
  return count;
}

}  // namespace

std::size_t shellStringCount() noexcept { return kCount; }

std::string_view shellStringKey(Str id) noexcept {
  const auto index = static_cast<std::size_t>(id);
  return index < kCount ? kEntries[index].key : std::string_view{};
}

std::string_view englishShellString(Str id) noexcept {
  const auto index = static_cast<std::size_t>(id);
  return index < kCount ? kEntries[index].english : std::string_view{};
}

ShellStringTable::ShellStringTable() {
  text_.reserve(kCount);
  for (const auto& entry : kEntries) text_.emplace_back(entry.english);
}

bool ShellStringTable::set(std::string_view key, std::string text) {
  for (std::size_t i = 0U; i < kCount; ++i) {
    if (kEntries[i].key != key) continue;
    text_[i] = std::move(text);
    return true;
  }
  return false;
}

const char* ShellStringTable::text(Str id) const noexcept {
  const auto index = static_cast<std::size_t>(id);
  return index < text_.size() ? text_[index].c_str() : "";
}

ShellStringTable ShellStringTable::pseudoLocalized(double expansion) {
  ShellStringTable table;
  for (std::size_t i = 0U; i < kCount; ++i) {
    const auto english = kEntries[i].english;
    std::string out{"["};
    for (const auto c : english) {
      const auto swap = accented(c);
      if (swap.empty()) out += c;
      else out += swap;
    }
    const auto target = static_cast<std::size_t>(
        std::ceil(static_cast<double>(characters(english)) * (1.0 + std::max(0.0, expansion))));
    while (characters(out) + 1U < target) out += '~';
    out += ']';
    table.text_[i] = std::move(out);
  }
  return table;
}

const char* tr(Str id) noexcept {
  const auto* table = installed.load(std::memory_order_acquire);
  return (table != nullptr ? *table : englishTable()).text(id);
}

std::string formatShellText(std::string_view text, std::initializer_list<std::string_view> args) {
  std::string out;
  out.reserve(text.size() + 16U);
  for (std::size_t i = 0U; i < text.size(); ++i) {
    const auto c = text[i];
    if ((c == '{' || c == '}') && i + 1U < text.size() && text[i + 1U] == c) {
      out += c;
      ++i;
      continue;
    }
    if (c == '{') {
      const auto close = text.find('}', i);
      std::size_t index = 0U;
      auto digits = close != std::string_view::npos && close > i + 1U;
      for (auto j = i + 1U; digits && j < close; ++j) {
        if (text[j] < '0' || text[j] > '9') digits = false;
        else index = index * 10U + static_cast<std::size_t>(text[j] - '0');
      }
      if (digits && index < args.size()) {
        out += *(args.begin() + index);
        i = close;
        continue;
      }
    }
    out += c;
  }
  return out;
}

std::string trf(Str id, std::initializer_list<std::string_view> args) {
  return formatShellText(tr(id), args);
}

ShellPlaceholders shellPlaceholders(std::string_view text) {
  ShellPlaceholders result;
  for (std::size_t i = 0U; i < text.size(); ++i) {
    const auto c = text[i];
    if ((c == '{' || c == '}') && i + 1U < text.size() && text[i + 1U] == c) {
      ++i;
      continue;
    }
    if (c == '}') return ShellPlaceholders{.valid = false};
    if (c != '{') continue;
    const auto close = text.find('}', i);
    if (close == std::string_view::npos || close == i + 1U) return ShellPlaceholders{.valid = false};
    std::size_t index = 0U;
    for (auto j = i + 1U; j < close; ++j) {
      if (text[j] < '0' || text[j] > '9' || close - i > 4U) return ShellPlaceholders{.valid = false};
      index = index * 10U + static_cast<std::size_t>(text[j] - '0');
    }
    result.indices.push_back(index);
    i = close;
  }
  std::sort(result.indices.begin(), result.indices.end());
  result.indices.erase(std::unique(result.indices.begin(), result.indices.end()),
                       result.indices.end());
  return result;
}

void installShellStrings(const ShellStringTable* table) noexcept {
  installed.store(table, std::memory_order_release);
}

ScopedShellStrings::ScopedShellStrings(const ShellStringTable& table) noexcept
    : previous_{installed.load(std::memory_order_acquire)} {
  installShellStrings(&table);
}

ScopedShellStrings::~ScopedShellStrings() { installShellStrings(previous_); }

}  // namespace seam::native_ui::design
