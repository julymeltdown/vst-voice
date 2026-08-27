#pragma once

#include "seam/application/editor_session.hpp"
#include "seam/application/project_factory.hpp"
#include "seam/domain/project.hpp"
#include "seam/time/tick.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seam::test::native_ui_design {

struct Viewport final {
  std::string_view id;
  std::uint32_t width;
  std::uint32_t height;
};

inline constexpr std::array<Viewport, 6U> kTargetViewports{{
    {"compact", 480U, 320U},
    {"small", 720U, 450U},
    {"medium", 960U, 600U},
    {"wide", 1188U, 768U},
    {"desktop", 1280U, 800U},
    {"large", 1440U, 900U},
}};

inline constexpr std::array<double, 4U> kTimelineZooms{{25.0, 50.0, 100.0,
                                                          200.0}};
inline constexpr std::array<double, 2U> kBackingScales{{1.0, 2.0}};

struct Fixture final {
  application::ProjectFactory factory{7400U};
  domain::RegionId regionId{};
  std::vector<domain::NoteId> noteIds;
  application::EditorSession session;

  Fixture() : session(makeProject()) {}

  [[nodiscard]] domain::Project makeProject() {
    auto project = factory.createProject("프로젝트 SEAM / 日本語 / 中文");
    const auto trackId = factory.addVocalTrack(project, "Voice / 보이스 / 歌声");
    regionId = factory.addRegion(project, trackId,
                                 "Long multilingual design fixture",
                                 time::Tick{0}, time::Tick{15360});
    auto* region = project.findRegion(regionId);

    const std::array<std::u32string, 5U> lyrics{{
        U"가나다라마바사",
        U"こんにちは世界",
        U"中文歌词很长",
        U"A\u0301 family emoji 👨‍👩‍👧‍👦",
        U"long lyric that must remain inspectable",
    }};
    for (std::size_t index = 0U; index < lyrics.size(); ++index) {
      auto [lyric, note] = factory.makeNote(
          time::Tick{960 + static_cast<std::int64_t>(index) * 180},
          time::Tick{960}, 64U, std::move(lyrics[index]),
          domain::Language::Japanese);
      noteIds.push_back(note.id);
      region->lyrics.push_back(std::move(lyric));
      region->notes.push_back(std::move(note));
    }
    region->unitSelectionOverrides.push_back(domain::UnitSelectionOverride{
        .startKey = domain::PhonemeKey{.noteId = noteIds.front(), .ordinal = 0U},
        .tokenCount = 20U,
        .unitId = "かな🎤中文Á超長いユニット名とても長い識別子",
        .renderer = domain::UnitRendererKind::ClassicPsola,
        .locked = true,
    });
    region->sortNotes();
    return project;
  }
};

}
