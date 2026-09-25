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
  if (text.find('\0') != std::string_view::npos)
    return core::failure(core::ErrorCode::ParseError, "SMF text payload contains NUL");
  if (textBytes > limits.maximumTextBytes || text.size() > limits.maximumTextBytes ||
      text.size() > limits.maximumTextBytes - textBytes)
    return core::failure(core::ErrorCode::ParseError, "SMF text payload exceeds bounds");
  textBytes += text.size();
  if (!domain::fromUtf8(std::string{text})) return core::failure(core::ErrorCode::ParseError, "SMF text payload is not valid UTF-8");
  return core::success();
}

}  // namespace

core::Result<void> SmfScore::validate(const SmfLimits& limits) const {
  const auto trackCount = tracks.empty() ? 1U : tracks.size();
  if (limits.maximumBytes == 0U || limits.maximumTracks == 0U || limits.maximumEvents == 0U ||
      limits.maximumSerializedEvents == 0U ||
      limits.maximumNotes == 0U || limits.maximumTextBytes == 0U || limits.maximumTick <= 0 ||
      ppq == 0U || ppq > 32767U || notes.size() > limits.maximumNotes ||
      trackCount == 0U || trackCount > limits.maximumTracks ||
      issues.size() > limits.maximumEvents)
    return core::failure(core::ErrorCode::InvalidArgument, "SMF score exceeds declared bounds");
  // maximumSerializedEvents is a wire-event ceiling, not a separate per-vector ceiling.
  // A note serializes as both note-on and note-off; every track also has an
  // end-of-track event, and names/lyrics/tempo/meter each add one event.
  // Validate cumulatively here, before encodeSmf allocates per-event records.
  std::size_t serializedEvents = trackCount;
  const auto addEvents = [&](std::size_t count) {
    if (serializedEvents > limits.maximumSerializedEvents ||
        count > limits.maximumSerializedEvents - serializedEvents) return false;
    serializedEvents += count;
    return true;
  };
  if (!addEvents(tempos.size()) || !addEvents(meters.size()) ||
      !addEvents(texts.size()) ||
      serializedEvents > limits.maximumSerializedEvents ||
      notes.size() > (limits.maximumSerializedEvents - serializedEvents) / 2U)
    return core::failure(core::ErrorCode::InvalidArgument,
        "SMF serialized event count exceeds bounds");
  serializedEvents += notes.size() * 2U;
  for (const auto& track : tracks)
    if (!track.name.empty() && !addEvents(1U))
      return core::failure(core::ErrorCode::InvalidArgument,
          "SMF serialized event count exceeds bounds");
  std::size_t textBytes = 0U;
  for (const auto& track : tracks) {
    const auto valid = validateText(track.name, textBytes, limits);
    if (!valid) return valid;
  }
  for (const auto& note : notes) {
    if (note.start.value() < 0 || note.duration.value() <= 0 || note.start.value() > limits.maximumTick ||
        note.duration.value() > limits.maximumTick - note.start.value() || note.midi > 127U || note.velocity > 127U || note.channel > 15U ||
        note.track >= trackCount)
      return core::failure(core::ErrorCode::InvalidArgument, "SMF note is invalid or exceeds tick bounds");
  }
  std::vector<const SmfNote*> orderedNotes;
  orderedNotes.reserve(notes.size());
  for (const auto& note : notes) orderedNotes.push_back(&note);
  std::sort(orderedNotes.begin(), orderedNotes.end(), [](const auto* lhs, const auto* rhs) {
    return std::tuple{lhs->track, lhs->channel, lhs->midi, lhs->start,
                      lhs->start + lhs->duration} <
        std::tuple{rhs->track, rhs->channel, rhs->midi, rhs->start,
                   rhs->start + rhs->duration};
  });
  for (std::size_t index = 1U; index < orderedNotes.size(); ++index) {
    const auto& previous = *orderedNotes[index - 1U];
    const auto& current = *orderedNotes[index];
    if (previous.track == current.track && previous.channel == current.channel &&
        previous.midi == current.midi &&
        current.start + current.duration < previous.start + previous.duration)
      return core::failure(core::ErrorCode::Unsupported,
          "SMF cannot preserve nested overlaps for repeated notes on one track and channel");
  }
  for (const auto& tempo : tempos) if (tempo.tick.value() < 0 || tempo.tick.value() > limits.maximumTick || !finitePositive(tempo.bpm) || tempo.bpm > 1'000'000.0)
    return core::failure(core::ErrorCode::InvalidArgument, "SMF tempo is invalid");
  for (const auto& meter : meters) if (meter.tick.value() < 0 || meter.tick.value() > limits.maximumTick || meter.numerator == 0U || meter.denominatorPower > 7U)
    return core::failure(core::ErrorCode::InvalidArgument, "SMF meter is invalid");
  for (const auto& text : texts) {
    if (text.tick.value() < 0 || text.tick.value() > limits.maximumTick || text.track >= trackCount) return core::failure(core::ErrorCode::InvalidArgument, "SMF text tick or track is invalid");
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
  result.tracks.resize(trackCount.value());
  std::vector<bool> trackNameSeen(trackCount.value(), false);
  enum class TextDiagnosticKind : std::size_t {
    InvalidComment, InvalidLyric, RemovedTerminator, EmptyLyric, Count
  };
  struct TextDiagnosticAggregate final {
    std::size_t issueIndex{};
    std::size_t count{};
    time::Tick first{};
    time::Tick last{};
  };
  std::vector<std::array<std::optional<TextDiagnosticAggregate>,
      static_cast<std::size_t>(TextDiagnosticKind::Count)>> textDiagnostics(
          trackCount.value());
  const auto recordTextDiagnostic = [&](std::size_t track,
                                        TextDiagnosticKind kind,
                                        SmfIssueSeverity severity,
                                        time::Tick at) -> core::Result<void> {
    auto& aggregate = textDiagnostics[track][static_cast<std::size_t>(kind)];
    if (!aggregate) {
      if (result.issues.size() >= limits.maximumEvents)
        return core::failure(core::ErrorCode::Unsupported,
            "SMF diagnostic report exceeds event bounds");
      aggregate = TextDiagnosticAggregate{result.issues.size(), 0U, at, at};
      result.issues.push_back({severity, at, {}});
    }
    ++aggregate->count;
    aggregate->last = at;
    std::string subject;
    std::string reason;
    switch (kind) {
      case TextDiagnosticKind::InvalidComment:
        subject = aggregate->count == 1U ? "non-lyric text payload"
                                         : "non-lyric text payloads";
        reason = aggregate->count == 1U
            ? "was not NUL-free UTF-8 and was discarded"
            : "were not NUL-free UTF-8 and were discarded";
        break;
      case TextDiagnosticKind::InvalidLyric:
        subject = aggregate->count == 1U ? "lyric payload" : "lyric payloads";
        reason = aggregate->count == 1U
            ? "was not NUL-free UTF-8 and was discarded"
            : "were not NUL-free UTF-8 and were discarded";
        break;
      case TextDiagnosticKind::RemovedTerminator:
        subject = aggregate->count == 1U
            ? "trailing NUL lyric terminator"
            : "trailing NUL lyric terminators";
        reason = aggregate->count == 1U ? "was removed" : "were removed";
        break;
      case TextDiagnosticKind::EmptyLyric:
        subject = aggregate->count == 1U ? "lyric payload" : "lyric payloads";
        reason = aggregate->count == 1U
            ? "was empty or became empty after removing a trailing NUL and was discarded"
            : "were empty or became empty after removing a trailing NUL and were discarded";
        break;
      case TextDiagnosticKind::Count:
        return core::failure(core::ErrorCode::Internal,
            "Invalid SMF text diagnostic kind");
    }
    auto& issue = result.issues[aggregate->issueIndex];
    issue.tick = aggregate->first;
    issue.message = std::to_string(aggregate->count) + " " + subject + " " +
        reason + " on source track " + std::to_string(track + 1U) +
        "; source ticks " + std::to_string(aggregate->first.value()) + ".." +
        std::to_string(aggregate->last.value()) + " at " +
        std::to_string(result.ppq) + " PPQ";
    return core::success();
  };
  std::size_t eventCount = 0U, textBytes = 0U;
  for (std::size_t track = 0U; track < trackCount.value(); ++track) {
    const auto marker = reader.span(4U); if (!marker || !fourcc(marker.value(), "MTrk")) return core::failure<Output>(core::ErrorCode::ParseError, "SMF track chunk is missing");
    const auto length = reader.u32(); if (!length || length.value() > reader.remaining()) return core::failure<Output>(core::ErrorCode::ParseError, "SMF track length is truncated");
    const auto payload = reader.span(length.value()); if (!payload) return core::Result<Output>{payload.error()};
    Reader trackReader{payload.value()}; std::map<std::pair<std::uint8_t, std::uint8_t>, std::deque<ActiveNote>> active;
    std::size_t activeNoteCount = 0U;
    const auto noteBudgetReached = [&] {
      return result.notes.size() >= limits.maximumNotes ||
          activeNoteCount >= limits.maximumNotes - result.notes.size();
    };
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
          if (kind == 0x90U && velocity != 0U) {
            if (noteBudgetReached())
              return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                            "SMF note count exceeds bounds");
            queue.push_back({tick, velocity, channel});
            ++activeNoteCount;
          } else if (!queue.empty()) {
            const auto start = queue.front(); queue.pop_front();
            --activeNoteCount;
            result.notes.push_back({start.start, tick - start.start, key, start.velocity, channel, static_cast<std::uint16_t>(track)});
          }
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
          if (kind == 0x90U && second.value() != 0U) {
            if (noteBudgetReached())
              return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                            "SMF note count exceeds bounds");
            queue.push_back({tick, second.value(), channel});
            ++activeNoteCount;
          } else if (!queue.empty()) {
            const auto start = queue.front(); queue.pop_front();
            --activeNoteCount;
            result.notes.push_back({start.start, tick - start.start, first.value(), start.velocity, channel, static_cast<std::uint16_t>(track)});
          }
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
          const auto rawText = std::string_view{
              reinterpret_cast<const char*>(data.value().data()), data.value().size()};
          if (textBytes > limits.maximumTextBytes ||
              rawText.size() > limits.maximumTextBytes - textBytes)
            return core::failure<Output>(core::ErrorCode::ParseError,
                "SMF text payload exceeds bounds");
          textBytes += rawText.size();

          auto text = rawText;
          const bool lyric = type.value() == 0x05U;
          // Some MIDI writers encode a C-style terminator inside the meta-event
          // length. Strip exactly one trailing NUL from lyrics; never treat
          // embedded NULs as text or let malformed annotations reject notes.
          const bool strippedTerminator = lyric && !text.empty() && text.back() == '\0';
          if (strippedTerminator) text.remove_suffix(1U);
          if (lyric && text.empty()) {
            const auto recorded = recordTextDiagnostic(track,
                TextDiagnosticKind::EmptyLyric, SmfIssueSeverity::Loss, tick);
            if (!recorded) return core::Result<Output>{recorded.error()};
            continue;
          }
          if (text.find('\0') != std::string_view::npos ||
              !domain::fromUtf8(std::string{text})) {
            const auto kind = lyric ? TextDiagnosticKind::InvalidLyric
                                    : TextDiagnosticKind::InvalidComment;
            const auto recorded = recordTextDiagnostic(track, kind,
                SmfIssueSeverity::Loss, tick);
            if (!recorded) return core::Result<Output>{recorded.error()};
            continue;
          }
          result.texts.push_back({tick, std::string{text}, lyric,
                                  static_cast<std::uint16_t>(track)});
          if (strippedTerminator) {
            const auto recorded = recordTextDiagnostic(track,
                TextDiagnosticKind::RemovedTerminator,
                SmfIssueSeverity::Warning, tick);
            if (!recorded) return core::Result<Output>{recorded.error()};
          }
        } else if (type.value() == 0x03U) {
          const std::string name{reinterpret_cast<const char*>(data.value().data()), data.value().size()};
          if (name.size() > limits.maximumTextBytes ||
              name.size() > limits.maximumTextBytes - textBytes)
            return core::failure<Output>(core::ErrorCode::ParseError,
                "SMF track-name payload exceeds text bounds");
          textBytes += name.size();
          if (!trackNameSeen[track]) {
            trackNameSeen[track] = true;
            if (name.find('\0') == std::string::npos && domain::fromUtf8(name)) {
              result.tracks[track].name = name;
            } else {
              if (result.issues.size() >= limits.maximumEvents)
                return core::failure<Output>(core::ErrorCode::Unsupported,
                    "SMF diagnostic report exceeds event bounds");
              result.issues.push_back({SmfIssueSeverity::Loss, tick,
                  "Track name is not NUL-free UTF-8 and was discarded"});
            }
          } else {
            if (result.issues.size() >= limits.maximumEvents)
              return core::failure<Output>(core::ErrorCode::Unsupported,
                  "SMF diagnostic report exceeds event bounds");
            result.issues.push_back({SmfIssueSeverity::Loss, tick,
                "Additional track-name event was ignored; the first track name is retained"});
          }
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
      --activeNoteCount;
      if (tick <= start.start) continue;
      result.notes.push_back({start.start, tick - start.start, key.second, start.velocity, key.first, static_cast<std::uint16_t>(track)});
      result.issues.push_back({SmfIssueSeverity::Warning, tick, "Note-off was missing; note was closed at track end"});
    }
  }
  if (reader.remaining() != 0U) return core::failure<Output>(core::ErrorCode::ParseError, "SMF has trailing bytes after declared tracks");
  if (result.notes.size() > limits.maximumNotes) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF note count exceeds bounds");
  std::stable_sort(result.notes.begin(), result.notes.end(), [](const auto& lhs, const auto& rhs) { return std::tie(lhs.track, lhs.start, lhs.channel, lhs.midi, lhs.duration) < std::tie(rhs.track, rhs.start, rhs.channel, rhs.midi, rhs.duration); });
  std::stable_sort(result.texts.begin(), result.texts.end(), [](const auto& lhs, const auto& rhs) { return std::tie(lhs.track, lhs.tick) < std::tie(rhs.track, rhs.tick); });
  std::stable_sort(result.tempos.begin(), result.tempos.end(), [](const auto& lhs, const auto& rhs) { return lhs.tick < rhs.tick; });
  std::stable_sort(result.meters.begin(), result.meters.end(), [](const auto& lhs, const auto& rhs) { return lhs.tick < rhs.tick; });
  const auto valid = result.validate(limits); if (!valid) return core::Result<Output>{valid.error()};
  return result;
}

core::Result<std::vector<std::uint8_t>> encodeSmf(const SmfScore& score, SmfLimits limits) {
  using Output = std::vector<std::uint8_t>;
  const auto valid = score.validate(limits); if (!valid) return core::Result<Output>{valid.error()};
  struct Event final {
    time::Tick tick;
    std::uint8_t order;
    std::vector<std::uint8_t> bytes;
    std::uint16_t pairingKey{0U};
    time::Tick pairingEnd{time::Tick{0}};
    bool noteOn{false};
  };
  const auto trackCount = score.tracks.empty() ? 1U : score.tracks.size();
  std::vector<std::vector<Event>> eventsByTrack(trackCount);
  auto& conductorEvents = eventsByTrack.front();
  for (const auto& tempo : score.tempos) {
    const auto micros = static_cast<std::uint32_t>(std::clamp(std::llround(60'000'000.0 / tempo.bpm), 1LL, 0xffffffLL));
    conductorEvents.push_back({tempo.tick, 1U, {0U, 0xffU, 0x51U, 3U, static_cast<std::uint8_t>((micros >> 16U) & 0xffU), static_cast<std::uint8_t>((micros >> 8U) & 0xffU), static_cast<std::uint8_t>(micros & 0xffU)}});
  }
  for (const auto& meter : score.meters) conductorEvents.push_back({meter.tick, 1U, {0U, 0xffU, 0x58U, 4U, meter.numerator, meter.denominatorPower, 24U, 8U}});
  for (std::size_t index = 0U; index < score.tracks.size(); ++index) {
    const auto& name = score.tracks[index].name;
    if (name.empty()) continue;
    std::vector<std::uint8_t> bytes{0U, 0xffU, 0x03U};
    appendVlq(bytes, static_cast<std::uint32_t>(name.size()));
    bytes.insert(bytes.end(), name.begin(), name.end());
    eventsByTrack[index].push_back({time::Tick{0}, 0U, std::move(bytes)});
  }
  for (const auto& text : score.texts) {
    if (text.text.size() > 0x0fffffffU) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF text is too long to encode");
    std::vector<std::uint8_t> bytes{0U, 0xffU, static_cast<std::uint8_t>(text.lyric ? 0x05U : 0x01U)}; appendVlq(bytes, static_cast<std::uint32_t>(text.text.size())); bytes.insert(bytes.end(), text.text.begin(), text.text.end());
    eventsByTrack[text.track].push_back({text.tick, 2U, std::move(bytes)});
  }
  for (const auto& note : score.notes) {
    const auto status = static_cast<std::uint8_t>(0x90U | note.channel); const auto off = static_cast<std::uint8_t>(0x80U | note.channel);
    const auto pairingKey = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(note.channel) * 128U + note.midi);
    eventsByTrack[note.track].push_back({note.start, 4U,
        {0U, status, note.midi, note.velocity}, pairingKey,
        note.start + note.duration, true});
    eventsByTrack[note.track].push_back({note.start + note.duration, 3U, {0U, off, note.midi, 0U}});
  }
  Output output; output.insert(output.end(), {'M', 'T', 'h', 'd'}); appendU32(output, 6U); appendU16(output, 1U); appendU16(output, static_cast<std::uint16_t>(trackCount)); appendU16(output, score.ppq);
  for (auto& events : eventsByTrack) {
    std::stable_sort(events.begin(), events.end(), [](const auto& lhs, const auto& rhs) {
      if (lhs.tick != rhs.tick) return lhs.tick < rhs.tick;
      if (lhs.order != rhs.order) return lhs.order < rhs.order;
      // The decoder pairs overlapping equal-key note-ons FIFO. At one onset,
      // order same-key notes by end tick so that pairing reconstructs their
      // original durations; other equal-priority events preserve input order.
      if (lhs.noteOn && rhs.noteOn) {
        if (lhs.pairingKey != rhs.pairingKey)
          return lhs.pairingKey < rhs.pairingKey;
        return lhs.pairingEnd < rhs.pairingEnd;
      }
      return false;
    });
    std::vector<std::uint8_t> track; time::Tick previous{0};
    for (auto& event : events) {
      const auto delta = event.tick - previous; if (delta.value() < 0 || delta.value() > 0x0fffffff) return core::failure<Output>(core::ErrorCode::InvalidArgument, "SMF event delta exceeds VLQ bounds");
      std::vector<std::uint8_t> prefix; appendVlq(prefix, static_cast<std::uint32_t>(delta.value())); track.insert(track.end(), prefix.begin(), prefix.end()); track.insert(track.end(), event.bytes.begin() + 1, event.bytes.end()); previous = event.tick;
    }
    track.insert(track.end(), {0U, 0xffU, 0x2fU, 0U});
    if (track.size() > std::numeric_limits<std::uint32_t>::max())
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Encoded SMF track exceeds four-byte length");
    output.insert(output.end(), {'M', 'T', 'r', 'k'}); appendU32(output, static_cast<std::uint32_t>(track.size())); output.insert(output.end(), track.begin(), track.end());
    if (output.size() > limits.maximumBytes) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Encoded SMF exceeds byte bounds");
  }
  if (output.size() > limits.maximumBytes) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Encoded SMF exceeds byte bounds");
  return output;
}

}  // namespace seam::interchange
