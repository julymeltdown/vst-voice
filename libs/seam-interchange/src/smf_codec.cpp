#include "seam/interchange/smf_codec.hpp"
#include "seam/domain/note.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <tuple>
#include <utility>

namespace seam::interchange {
namespace {

bool finitePositive(double value) noexcept {
  return std::isfinite(value) && value > 0.0;
}

class Reader final {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }
  [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
  [[nodiscard]] bool canRead(std::size_t count) const noexcept { return count <= remaining(); }

  [[nodiscard]] core::Result<std::uint8_t> byte() {
    if (!canRead(1U)) return core::failure<std::uint8_t>(core::ErrorCode::ParseError, "SMF is truncated");
    return bytes_[offset_++];
  }
  [[nodiscard]] core::Result<std::uint16_t> u16() {
    if (!canRead(2U)) return core::failure<std::uint16_t>(core::ErrorCode::ParseError, "SMF 16-bit field is truncated");
    const auto value = static_cast<std::uint16_t>(bytes_[offset_] << 8U | bytes_[offset_ + 1U]); offset_ += 2U; return value;
  }
  [[nodiscard]] core::Result<std::uint32_t> u32() {
    if (!canRead(4U)) return core::failure<std::uint32_t>(core::ErrorCode::ParseError, "SMF 32-bit field is truncated");
    const auto value = (static_cast<std::uint32_t>(bytes_[offset_]) << 24U) |
        (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 16U) |
        (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 8U) |
        static_cast<std::uint32_t>(bytes_[offset_ + 3U]); offset_ += 4U; return value;
  }
  [[nodiscard]] core::Result<std::span<const std::uint8_t>> span(std::size_t count) {
    if (!canRead(count)) return core::failure<std::span<const std::uint8_t>>(core::ErrorCode::ParseError, "SMF chunk payload is truncated");
    const auto result = bytes_.subspan(offset_, count); offset_ += count; return result;
  }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t offset_{0U};
};

core::Result<std::uint32_t> vlq(Reader& reader) {
  std::uint32_t value = 0U;
  for (std::size_t bytes = 0U; bytes < 4U; ++bytes) {
    const auto next = reader.byte(); if (!next) return core::Result<std::uint32_t>{next.error()};
    if (value > 0x0fffffffU >> 7U) return core::failure<std::uint32_t>(core::ErrorCode::ParseError, "SMF variable-length delta overflows");
    value = (value << 7U) | static_cast<std::uint32_t>(next.value() & 0x7fU);
    if ((next.value() & 0x80U) == 0U) return value;
  }
  return core::failure<std::uint32_t>(core::ErrorCode::ParseError, "SMF variable-length value exceeds four bytes");
}

void appendU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  output.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
  output.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
  output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
  output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendU16(std::vector<std::uint8_t>& output, std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
  output.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void appendVlq(std::vector<std::uint8_t>& output, std::uint32_t value) {
  std::array<std::uint8_t, 4U> encoded{}; std::size_t count = 1U; encoded[0] = static_cast<std::uint8_t>(value & 0x7fU);
  while ((value >>= 7U) != 0U && count < encoded.size()) encoded[count++] = static_cast<std::uint8_t>(value & 0x7fU);
  for (std::size_t index = count; index-- > 0U;) output.push_back(static_cast<std::uint8_t>(encoded[index] | (index == 0U ? 0U : 0x80U)));
}

bool fourcc(std::span<const std::uint8_t> bytes, std::string_view value) {
  if (bytes.size() != 4U) return false;
  return std::equal(value.begin(), value.end(), bytes.begin());
}

struct ActiveNote final { time::Tick start; std::uint8_t velocity; std::uint8_t channel; };

std::string discardedChannelEvent(std::uint8_t kind, std::uint8_t channel) {
  std::string_view family = "Channel event";
  switch (kind) {
    case 0xa0U: family = "Polyphonic pressure"; break;
    case 0xb0U: family = "Control change"; break;
    case 0xc0U: family = "Program change"; break;
    case 0xd0U: family = "Channel pressure"; break;
    case 0xe0U: family = "Pitch bend"; break;
    default: break;
  }
  return std::string{family} + " on channel " +
      std::to_string(static_cast<unsigned>(channel) + 1U) +
      " was ignored; channel-control import is unsupported";
}

core::Result<void> validateText(std::string_view text, std::size_t& textBytes, const SmfLimits& limits) {
  if (text.size() > limits.maximumTextBytes || text.size() > limits.maximumTextBytes - textBytes || text.find('\0') != std::string_view::npos)
    return core::failure(core::ErrorCode::ParseError, "SMF text payload exceeds bounds");
  textBytes += text.size();
  if (!domain::fromUtf8(std::string{text})) return core::failure(core::ErrorCode::ParseError, "SMF text payload is not valid UTF-8");
  return core::success();
}

}  // namespace

core::Result<void> SmfScore::validate(const SmfLimits& limits) const {
  if (limits.maximumBytes == 0U || limits.maximumTracks == 0U || limits.maximumEvents == 0U ||
      limits.maximumNotes == 0U || limits.maximumTextBytes == 0U || limits.maximumTick <= 0 ||
      ppq == 0U || ppq > 32767U || notes.size() > limits.maximumNotes ||
      tempos.size() > limits.maximumEvents || meters.size() > limits.maximumEvents ||
      texts.size() > limits.maximumEvents || issues.size() > limits.maximumEvents)
    return core::failure(core::ErrorCode::InvalidArgument, "SMF score exceeds declared bounds");
  std::size_t textBytes = 0U;
  for (const auto& note : notes) {
    if (note.start.value() < 0 || note.duration.value() <= 0 || note.start.value() > limits.maximumTick ||
        note.duration.value() > limits.maximumTick - note.start.value() || note.midi > 127U || note.velocity > 127U || note.channel > 15U)
      return core::failure(core::ErrorCode::InvalidArgument, "SMF note is invalid or exceeds tick bounds");
  }
  for (const auto& tempo : tempos) if (tempo.tick.value() < 0 || tempo.tick.value() > limits.maximumTick || !finitePositive(tempo.bpm) || tempo.bpm > 1'000'000.0)
    return core::failure(core::ErrorCode::InvalidArgument, "SMF tempo is invalid");
  for (const auto& meter : meters) if (meter.tick.value() < 0 || meter.tick.value() > limits.maximumTick || meter.numerator == 0U || meter.denominatorPower > 7U)
    return core::failure(core::ErrorCode::InvalidArgument, "SMF meter is invalid");
  for (const auto& text : texts) {
    if (text.tick.value() < 0 || text.tick.value() > limits.maximumTick) return core::failure(core::ErrorCode::InvalidArgument, "SMF text tick is invalid");
    const auto valid = validateText(text.text, textBytes, limits); if (!valid) return valid;
  }
  return core::success();
}

core::Result<SmfScore> decodeSmf(std::span<const std::uint8_t> bytes, SmfLimits limits) {
  using Output = SmfScore;
  if (bytes.empty() || bytes.size() > limits.maximumBytes) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF input exceeds byte bounds");
  Reader reader{bytes};
  const auto header = reader.span(4U); if (!header || !fourcc(header.value(), "MThd")) return core::failure<Output>(core::ErrorCode::ParseError, "SMF header chunk is missing");
  const auto headerLength = reader.u32(); if (!headerLength || headerLength.value() != 6U) return core::failure<Output>(core::ErrorCode::ParseError, "SMF header length must be six bytes");
  const auto format = reader.u16(); const auto trackCount = reader.u16(); const auto division = reader.u16();
  if (!format || !trackCount || !division) return core::failure<Output>(core::ErrorCode::ParseError, "SMF header fields are truncated");
  if ((format.value() != 0U && format.value() != 1U) ||
      (format.value() == 0U && trackCount.value() != 1U) ||
      trackCount.value() == 0U || trackCount.value() > limits.maximumTracks)
    return core::failure<Output>(core::ErrorCode::Unsupported, "Only bounded SMF Type 0/1 files are supported");
  if ((division.value() & 0x8000U) != 0U || (division.value() & 0x7fffU) == 0U)
    return core::failure<Output>(core::ErrorCode::Unsupported, "SMPTE or zero-PPQ SMF timing is unsupported");
  Output result; result.ppq = static_cast<std::uint16_t>(division.value() & 0x7fffU);
  std::size_t eventCount = 0U, textBytes = 0U;
  for (std::size_t track = 0U; track < trackCount.value(); ++track) {
    const auto marker = reader.span(4U); if (!marker || !fourcc(marker.value(), "MTrk")) return core::failure<Output>(core::ErrorCode::ParseError, "SMF track chunk is missing");
    const auto length = reader.u32(); if (!length || length.value() > reader.remaining()) return core::failure<Output>(core::ErrorCode::ParseError, "SMF track length is truncated");
    const auto payload = reader.span(length.value()); if (!payload) return core::Result<Output>{payload.error()};
    Reader trackReader{payload.value()}; std::map<std::pair<std::uint8_t, std::uint8_t>, std::deque<ActiveNote>> active;
    std::uint8_t running = 0U; time::Tick tick{0}; bool ended = false;
    while (trackReader.remaining() > 0U) {
      if (++eventCount > limits.maximumEvents) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF event count exceeds bounds");
      const auto delta = vlq(trackReader); if (!delta) return core::Result<Output>{delta.error()};
      if (static_cast<std::int64_t>(delta.value()) > limits.maximumTick - tick.value()) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF absolute tick exceeds bounds");
      tick += time::Tick{static_cast<std::int64_t>(delta.value())};
      const auto raw = trackReader.byte(); if (!raw) return core::Result<Output>{raw.error()};
      std::uint8_t status = raw.value();
      if (status < 0x80U) {
        if (running < 0x80U || running >= 0xf0U) return core::failure<Output>(core::ErrorCode::ParseError, "SMF running status is unavailable");
        status = running;
        // The byte is the first data byte for a running-status event.
        const auto channel = static_cast<std::uint8_t>(status & 0x0fU); const auto kind = static_cast<std::uint8_t>(status & 0xf0U);
        const auto second = (kind == 0xc0U || kind == 0xd0U) ? core::Result<std::uint8_t>{std::uint8_t{0}} : trackReader.byte();
        if (!second) return core::Result<Output>{second.error()};
        if (second.value() > 127U) return core::failure<Output>(core::ErrorCode::ParseError, "SMF running-status data byte is invalid");
        if (kind == 0x90U || kind == 0x80U) {
          const auto velocity = second.value(); const auto key = raw.value();
          auto& queue = active[{channel, key}];
          if (kind == 0x90U && velocity != 0U) queue.push_back({tick, velocity, channel});
          else if (!queue.empty()) { const auto start = queue.front(); queue.pop_front(); result.notes.push_back({start.start, tick - start.start, key, start.velocity, channel}); }
        } else result.issues.push_back({SmfIssueSeverity::Loss, tick,
                                        discardedChannelEvent(kind, channel)});
        continue;
      }
      if (status < 0xf0U) {
        running = status; const auto channel = static_cast<std::uint8_t>(status & 0x0fU); const auto kind = static_cast<std::uint8_t>(status & 0xf0U);
        const auto first = trackReader.byte(); if (!first) return core::Result<Output>{first.error()};
        const auto needsSecond = kind != 0xc0U && kind != 0xd0U;
        const auto second = needsSecond ? trackReader.byte() : core::Result<std::uint8_t>{std::uint8_t{0}};
        if (!second) return core::Result<Output>{second.error()};
        if (first.value() > 127U || (needsSecond && second.value() > 127U))
          return core::failure<Output>(core::ErrorCode::ParseError, "SMF channel data byte is invalid");
        if ((kind == 0x90U || kind == 0x80U) && first.value() <= 127U) {
          auto& queue = active[{channel, first.value()}];
          if (kind == 0x90U && second.value() != 0U) queue.push_back({tick, second.value(), channel});
          else if (!queue.empty()) { const auto start = queue.front(); queue.pop_front(); result.notes.push_back({start.start, tick - start.start, first.value(), start.velocity, channel}); }
        } else result.issues.push_back({SmfIssueSeverity::Loss, tick,
                                        discardedChannelEvent(kind, channel)});
        continue;
      }
      if (status == 0xffU) {
        running = 0U;
        const auto type = trackReader.byte(); if (!type) return core::Result<Output>{type.error()};
        const auto size = vlq(trackReader); if (!size || size.value() > trackReader.remaining()) return core::failure<Output>(core::ErrorCode::ParseError, "SMF meta-event length is invalid");
        const auto data = trackReader.span(size.value()); if (!data) return core::Result<Output>{data.error()};
        if (type.value() == 0x2fU) { if (size.value() != 0U) return core::failure<Output>(core::ErrorCode::ParseError, "SMF end-of-track payload is invalid"); ended = true; break; }
        if (type.value() == 0x51U) {
          if (size.value() != 3U) return core::failure<Output>(core::ErrorCode::ParseError, "SMF tempo payload is invalid");
          const auto micros = (static_cast<std::uint32_t>(data.value()[0]) << 16U) | (static_cast<std::uint32_t>(data.value()[1]) << 8U) | data.value()[2];
          if (micros == 0U) return core::failure<Output>(core::ErrorCode::ParseError, "SMF tempo cannot be zero");
          result.tempos.push_back({tick, 60'000'000.0 / static_cast<double>(micros)});
        } else if (type.value() == 0x58U) {
          if (size.value() != 4U || data.value()[0] == 0U || data.value()[1] > 7U) return core::failure<Output>(core::ErrorCode::ParseError, "SMF meter payload is invalid");
          result.meters.push_back({tick, data.value()[0], data.value()[1]});
        } else if (type.value() == 0x01U || type.value() == 0x05U) {
          const std::string text{reinterpret_cast<const char*>(data.value().data()), data.value().size()};
          const auto valid = validateText(text, textBytes, limits); if (!valid) return core::Result<Output>{valid.error()};
          result.texts.push_back({tick, text, type.value() == 0x05U});
        } else if (type.value() != 0x00U && type.value() != 0x20U && type.value() != 0x21U)
          result.issues.push_back({SmfIssueSeverity::Loss, tick, "Unsupported SMF meta-event was ignored"});
        continue;
      }
      if (status == 0xf0U || status == 0xf7U) {
        const auto size = vlq(trackReader); if (!size || size.value() > trackReader.remaining()) return core::failure<Output>(core::ErrorCode::ParseError, "SMF SysEx length is invalid");
        const auto ignored = trackReader.span(size.value()); if (!ignored) return core::Result<Output>{ignored.error()};
        result.issues.push_back({SmfIssueSeverity::Loss, tick, "SysEx data was ignored"}); running = 0U; continue;
      }
      if (status == 0xf1U || status == 0xf3U) {
        const auto ignored = trackReader.byte(); if (!ignored || ignored.value() > 127U)
          return core::failure<Output>(core::ErrorCode::ParseError, "SMF system-common data is invalid");
        result.issues.push_back({SmfIssueSeverity::Loss, tick, "System-common MIDI data was ignored"}); running = 0U; continue;
      }
      if (status == 0xf2U) {
        const auto first = trackReader.byte(); const auto second = trackReader.byte();
        if (!first || !second || first.value() > 127U || second.value() > 127U)
          return core::failure<Output>(core::ErrorCode::ParseError, "SMF song-position data is invalid");
        result.issues.push_back({SmfIssueSeverity::Loss, tick, "System-common MIDI data was ignored"}); running = 0U; continue;
      }
      if (status == 0xf6U || status == 0xf8U || status == 0xf9U || status == 0xfaU ||
          status == 0xfbU || status == 0xfcU || status == 0xfdU || status == 0xfeU) {
        result.issues.push_back({SmfIssueSeverity::Loss, tick, "System MIDI status was ignored"}); running = 0U; continue;
      }
      return core::failure<Output>(core::ErrorCode::ParseError, "SMF status byte is invalid");
    }
    if (ended && trackReader.remaining() != 0U)
      return core::failure<Output>(core::ErrorCode::ParseError, "SMF track contains bytes after end-of-track");
    for (auto& [key, queue] : active) for (const auto& start : queue) {
      if (tick <= start.start) continue;
      result.notes.push_back({start.start, tick - start.start, key.second, start.velocity, key.first});
      result.issues.push_back({SmfIssueSeverity::Warning, tick, "Note-off was missing; note was closed at track end"});
    }
  }
  if (reader.remaining() != 0U) return core::failure<Output>(core::ErrorCode::ParseError, "SMF has trailing bytes after declared tracks");
  if (result.notes.size() > limits.maximumNotes) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF note count exceeds bounds");
  if (trackCount.value() > 1U) {
    if (result.issues.size() >= limits.maximumEvents)
      return core::failure<Output>(core::ErrorCode::Unsupported,
          "SMF track-structure diagnostic report exceeds event bounds");
    result.issues.push_back({SmfIssueSeverity::Loss, time::Tick{0},
        std::to_string(trackCount.value()) +
            " source tracks were flattened into one score; track membership "
            "and separation are not retained"});
  }
  std::stable_sort(result.notes.begin(), result.notes.end(), [](const auto& lhs, const auto& rhs) { return std::tie(lhs.start, lhs.channel, lhs.midi, lhs.duration) < std::tie(rhs.start, rhs.channel, rhs.midi, rhs.duration); });
  std::stable_sort(result.tempos.begin(), result.tempos.end(), [](const auto& lhs, const auto& rhs) { return lhs.tick < rhs.tick; });
  std::stable_sort(result.meters.begin(), result.meters.end(), [](const auto& lhs, const auto& rhs) { return lhs.tick < rhs.tick; });
  const auto valid = result.validate(limits); if (!valid) return core::Result<Output>{valid.error()};
  return result;
}

core::Result<std::vector<std::uint8_t>> encodeSmf(const SmfScore& score, SmfLimits limits) {
  using Output = std::vector<std::uint8_t>;
  const auto valid = score.validate(limits); if (!valid) return core::Result<Output>{valid.error()};
  struct Event final { time::Tick tick; std::uint8_t order; std::vector<std::uint8_t> bytes; };
  std::vector<Event> tempoEvents;
  for (const auto& tempo : score.tempos) {
    const auto micros = static_cast<std::uint32_t>(std::clamp(std::llround(60'000'000.0 / tempo.bpm), 1LL, 0xffffffLL));
    tempoEvents.push_back({tempo.tick, 0U, {0U, 0xffU, 0x51U, 3U, static_cast<std::uint8_t>((micros >> 16U) & 0xffU), static_cast<std::uint8_t>((micros >> 8U) & 0xffU), static_cast<std::uint8_t>(micros & 0xffU)}});
  }
  for (const auto& meter : score.meters) tempoEvents.push_back({meter.tick, 0U, {0U, 0xffU, 0x58U, 4U, meter.numerator, meter.denominatorPower, 24U, 8U}});
  for (const auto& text : score.texts) {
    if (text.text.size() > 0x0fffffffU) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF text is too long to encode");
    std::vector<std::uint8_t> bytes{0U, 0xffU, static_cast<std::uint8_t>(text.lyric ? 0x05U : 0x01U)}; appendVlq(bytes, static_cast<std::uint32_t>(text.text.size())); bytes.insert(bytes.end(), text.text.begin(), text.text.end());
    tempoEvents.push_back({text.tick, 0U, std::move(bytes)});
  }
  std::vector<Event> notes;
  for (const auto& note : score.notes) {
    const auto status = static_cast<std::uint8_t>(0x90U | note.channel); const auto off = static_cast<std::uint8_t>(0x80U | note.channel);
    notes.push_back({note.start, 2U, {0U, status, note.midi, note.velocity}});
    notes.push_back({note.start + note.duration, 1U, {0U, off, note.midi, 0U}});
  }
  std::vector<Event> events; events.reserve(tempoEvents.size() + notes.size()); events.insert(events.end(), std::make_move_iterator(tempoEvents.begin()), std::make_move_iterator(tempoEvents.end())); events.insert(events.end(), std::make_move_iterator(notes.begin()), std::make_move_iterator(notes.end()));
  std::stable_sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) { return std::tie(lhs.tick, lhs.order, lhs.bytes) < std::tie(rhs.tick, rhs.order, rhs.bytes); });
  std::vector<std::uint8_t> track; time::Tick previous{0};
  for (auto& event : events) {
    const auto delta = event.tick - previous; if (delta.value() < 0 || delta.value() > 0x0fffffff) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF event delta exceeds VLQ bounds");
    event.bytes[0] = 0U; std::vector<std::uint8_t> prefix; appendVlq(prefix, static_cast<std::uint32_t>(delta.value())); track.insert(track.end(), prefix.begin(), prefix.end()); track.insert(track.end(), event.bytes.begin() + 1, event.bytes.end()); previous = event.tick;
  }
  track.insert(track.end(), {0U, 0xffU, 0x2fU, 0U});
  if (track.size() > std::numeric_limits<std::uint32_t>::max())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Encoded SMF track exceeds four-byte length");
  Output output; output.reserve(14U + track.size()); output.insert(output.end(), {'M', 'T', 'h', 'd'}); appendU32(output, 6U); appendU16(output, 1U); appendU16(output, 1U); appendU16(output, score.ppq); output.insert(output.end(), {'M', 'T', 'r', 'k'}); appendU32(output, static_cast<std::uint32_t>(track.size())); output.insert(output.end(), track.begin(), track.end());
  if (output.size() > limits.maximumBytes) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Encoded SMF exceeds byte bounds");
  return output;
}

}  // namespace seam::interchange
