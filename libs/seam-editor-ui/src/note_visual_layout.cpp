#include "seam/ui/note_visual_layout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace seam::ui {
namespace {

struct Pending final {
  std::size_t sourceIndex{0U};
  std::size_t groupIndex{0U};
  std::size_t bandIndex{0U};
};

bool stableBefore(const NoteVisualLayoutItem& lhs,
                  const NoteVisualLayoutItem& rhs) noexcept {
  if (lhs.midiKey != rhs.midiKey) return lhs.midiKey < rhs.midiKey;
  if (lhs.start != rhs.start) return lhs.start < rhs.start;
  if (lhs.end != rhs.end) return lhs.end < rhs.end;
  return lhs.noteId < rhs.noteId;
}

}

std::vector<NoteVisualLayout> layoutNoteVisuals(
    const std::vector<NoteVisualLayoutItem>& items,
    double minimumHitWidth) noexcept {
  std::vector<NoteVisualLayout> result(items.size());
  if (items.empty()) return result;
  std::vector<std::size_t> order(items.size());
  for (std::size_t index = 0U; index < order.size(); ++index) order[index] = index;
  std::sort(order.begin(), order.end(), [&items](std::size_t lhs, std::size_t rhs) {
    return stableBefore(items[lhs], items[rhs]);
  });

  std::size_t cursor = 0U;
  std::size_t groupIndex = 0U;
  while (cursor < order.size()) {
    const auto midi = items[order[cursor]].midiKey;
    std::size_t groupEnd = cursor;
    while (groupEnd < order.size() && items[order[groupEnd]].midiKey == midi) {
      const auto groupStart = groupEnd;
      auto connectedEnd = items[order[groupEnd]].end;
      ++groupEnd;
      while (groupEnd < order.size() && items[order[groupEnd]].midiKey == midi &&
             items[order[groupEnd]].start < connectedEnd) {
        connectedEnd = std::max(connectedEnd, items[order[groupEnd]].end);
        ++groupEnd;
      }

      std::vector<time::Tick> bandEnds;
      std::vector<Pending> pending;
      pending.reserve(groupEnd - groupStart);
      for (std::size_t index = groupStart; index < groupEnd; ++index) {
        const auto sourceIndex = order[index];
        const auto& item = items[sourceIndex];
        std::size_t band = 0U;
        for (; band < bandEnds.size(); ++band) {
          if (bandEnds[band] <= item.start) break;
        }
        if (band == bandEnds.size()) bandEnds.push_back(item.end);
        else bandEnds[band] = item.end;
        pending.push_back(Pending{.sourceIndex = sourceIndex,
                                  .groupIndex = groupIndex,
                                  .bandIndex = band});
      }
      const auto visibleBandCount = std::min<std::size_t>(3U, bandEnds.size());
      const auto hiddenMemberCount = pending.size() > visibleBandCount
                                         ? pending.size() - visibleBandCount
                                         : 0U;
      for (std::size_t index = 0U; index < pending.size(); ++index) {
        const auto& entry = pending[index];
        const auto& item = items[entry.sourceIndex];
        const auto band = visibleBandCount == 0U ? 0U
                                                 : entry.bandIndex % visibleBandCount;
        const auto bandHeight = item.timelineBounds.height /
                                static_cast<double>(std::max<std::size_t>(1U,
                                                                         visibleBandCount));
        const auto paint = Rect{item.timelineBounds.x,
                                item.timelineBounds.y +
                                    static_cast<double>(band) * bandHeight,
                                item.timelineBounds.width,
                                std::max(1.0, bandHeight)};
        const auto hitWidth = std::max(paint.width, minimumHitWidth);
        result[entry.sourceIndex] = NoteVisualLayout{
            .paintBounds = paint,
            .hitBounds = Rect{paint.x - (hitWidth - paint.width) * 0.5,
                              paint.y, hitWidth, paint.height},
            .groupIndex = entry.groupIndex,
            .groupMemberCount = pending.size(),
            .bandIndex = band,
            .visibleBandCount = visibleBandCount,
            .hiddenMemberCount = hiddenMemberCount,
            .hiddenByDensity = entry.bandIndex >= visibleBandCount,
            .drawsOverflowIndicator = hiddenMemberCount != 0U && index == 0U,
        };
      }
      ++groupIndex;
    }
    cursor = groupEnd;
  }
  return result;
}

}
