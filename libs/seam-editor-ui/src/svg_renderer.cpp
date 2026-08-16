#include "seam/ui/svg_renderer.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace seam::ui {

std::string SvgEditorRenderer::escape(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (const char character : text) {
    switch (character) {
      case '&': result += "&amp;"; break;
      case '<': result += "&lt;"; break;
      case '>': result += "&gt;"; break;
      case '"': result += "&quot;"; break;
      case '\'': result += "&apos;"; break;
      default: result.push_back(character); break;
    }
  }
  return result;
}

core::Result<EditorRenderStats> SvgEditorRenderer::render(
    const PianoRollModel& model,
    const std::filesystem::path& output,
    std::string_view projectName,
    std::uint64_t revision) const {
  const auto buildStart = std::chrono::steady_clock::now();
  const auto notes = model.visibleNotes();
  const auto buildEnd = std::chrono::steady_clock::now();
  const auto& viewport = model.viewport();

  std::ostringstream svg;
  svg << std::fixed << std::setprecision(2);
  svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << viewport.bounds.width
      << "\" height=\"" << viewport.bounds.height << "\" viewBox=\"0 0 "
      << viewport.bounds.width << ' ' << viewport.bounds.height << "\">\n";
  svg << "<defs><linearGradient id=\"top\" x1=\"0\" x2=\"0\" y1=\"0\" y2=\"1\">"
         "<stop stop-color=\"#27252c\"/><stop offset=\"1\" stop-color=\"#19181d\"/>"
         "</linearGradient></defs>\n";
  svg << "<rect width=\"100%\" height=\"100%\" fill=\"#141318\"/>\n";
  svg << "<rect x=\"0\" y=\"0\" width=\"100%\" height=\"54\" fill=\"url(#top)\"/>\n";
  svg << "<text x=\"24\" y=\"33\" fill=\"#f0edf4\" font-family=\"sans-serif\" font-size=\"18\" font-weight=\"600\">"
      << escape(projectName) << "</text>\n";
  svg << "<text x=\"" << viewport.bounds.width - 250.0
      << "\" y=\"32\" fill=\"#a6a0ad\" font-family=\"monospace\" font-size=\"12\">PHASE 1 · REV "
      << revision << "</text>\n";

  const double top = 88.0;
  const double contentLeft = viewport.keyboardWidth;
  svg << "<rect x=\"0\" y=\"54\" width=\"100%\" height=\"34\" fill=\"#201e24\"/>\n";
  svg << "<rect x=\"0\" y=\"" << top << "\" width=\"" << viewport.keyboardWidth
      << "\" height=\"" << viewport.bounds.height - top << "\" fill=\"#1c1a20\"/>\n";
  svg << "<rect x=\"" << contentLeft << "\" y=\"" << top << "\" width=\""
      << viewport.bounds.width - contentLeft << "\" height=\"" << viewport.bounds.height - top
      << "\" fill=\"#17161b\"/>\n";

  const auto& timeline = model.timeline();
  const auto& pitch = model.pitch();
  const auto quarter = time::Tick{timeline.ppq()};
  const auto visibleStart = timeline.pixelToTick(0.0);
  const auto visibleEnd = timeline.pixelToTick(viewport.bounds.width - contentLeft);
  auto gridTick = time::Tick{(visibleStart.value() / quarter.value()) * quarter.value()};
  if (gridTick < visibleStart) {
    gridTick += quarter;
  }
  std::int64_t quarterIndex = gridTick.value() / quarter.value();
  for (; gridTick <= visibleEnd; gridTick += quarter, ++quarterIndex) {
    const auto x = contentLeft + timeline.tickToPixel(gridTick);
    const bool bar = quarterIndex % 4 == 0;
    svg << "<line x1=\"" << x << "\" y1=\"54\" x2=\"" << x << "\" y2=\""
        << viewport.bounds.height << "\" stroke=\"" << (bar ? "#49424f" : "#2a272e")
        << "\" stroke-width=\"" << (bar ? 1.4 : 0.7) << "\"/>\n";
    if (bar) {
      svg << "<text x=\"" << x + 5.0 << "\" y=\"76\" fill=\"#8d8694\" font-family=\"monospace\" font-size=\"11\">"
          << quarterIndex / 4 + 1 << "</text>\n";
    }
  }

  for (int midi = pitch.topMidiKey(); midi >= 0; --midi) {
    const auto y = top + pitch.midiToPixel(midi);
    if (y > viewport.bounds.height) {
      break;
    }
    const bool blackKey = (midi % 12 == 1 || midi % 12 == 3 || midi % 12 == 6 ||
                           midi % 12 == 8 || midi % 12 == 10);
    const bool cKey = midi % 12 == 0;
    svg << "<rect x=\"0\" y=\"" << y << "\" width=\"" << viewport.keyboardWidth
        << "\" height=\"" << pitch.rowHeight() << "\" fill=\""
        << (blackKey ? "#242129" : "#302d35") << "\"/>\n";
    svg << "<line x1=\"0\" y1=\"" << y << "\" x2=\"100%\" y2=\"" << y
        << "\" stroke=\"" << (cKey ? "#39343e" : "#242229") << "\" stroke-width=\""
        << (cKey ? 1.0 : 0.5) << "\"/>\n";
    if (cKey) {
      svg << "<text x=\"12\" y=\"" << y + 13.0
          << "\" fill=\"#aaa4b0\" font-family=\"monospace\" font-size=\"10\">C"
          << midi / 12 - 1 << "</text>\n";
    }
  }

  for (const auto& note : notes) {
    auto bounds = note.bounds;
    bounds.y += top;
    const auto fill = note.selected ? "#8a536e" : (note.midiKey % 2 == 0 ? "#4d3d55" : "#44384b");
    const auto stroke = note.selected ? "#e2b3ca" : "#76647e";
    svg << "<rect x=\"" << bounds.x << "\" y=\"" << bounds.y << "\" width=\""
        << bounds.width << "\" height=\"" << bounds.height
        << "\" rx=\"2\" fill=\"" << fill << "\" stroke=\"" << stroke
        << "\" stroke-width=\"1\"/>\n";
    if (bounds.width > 26.0 && !note.lyric.empty()) {
      svg << "<text x=\"" << bounds.x + 5.0 << "\" y=\"" << bounds.y + 13.0
          << "\" fill=\"#f3edf5\" font-family=\"sans-serif\" font-size=\"10\">"
          << escape(note.lyric) << "</text>\n";
    }
  }

  svg << "<rect x=\"" << viewport.bounds.width - 250.0 << "\" y=\"10\" width=\"226\" height=\"32\" rx=\"6\" fill=\"#17161b\" stroke=\"#3e3944\"/>\n";
  svg << "<text x=\"" << viewport.bounds.width - 232.0 << "\" y=\"31\" fill=\"#c7c0ca\" font-family=\"monospace\" font-size=\"11\">"
      << notes.size() << " visible notes</text>\n";
  svg << "</svg>\n";

  const auto renderStart = std::chrono::steady_clock::now();
  std::ofstream stream(output, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return core::failure<EditorRenderStats>(core::ErrorCode::IoError,
                                             "Unable to create SVG output",
                                             output.string());
  }
  const auto data = svg.str();
  stream.write(data.data(), static_cast<std::streamsize>(data.size()));
  stream.flush();
  if (!stream) {
    return core::failure<EditorRenderStats>(core::ErrorCode::IoError,
                                             "Unable to write SVG output",
                                             output.string());
  }
  const auto renderEnd = std::chrono::steady_clock::now();

  return core::success(EditorRenderStats{
      .totalNotes = notes.size(),
      .visibleNotes = notes.size(),
      .buildMilliseconds = std::chrono::duration<double, std::milli>(buildEnd - buildStart).count(),
      .renderMilliseconds = std::chrono::duration<double, std::milli>(renderEnd - renderStart).count(),
  });
}

}  // namespace seam::ui
