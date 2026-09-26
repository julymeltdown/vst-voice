#include "seam/native_ui/design/shell_evidence.hpp"

#include <string>
#include <utility>

namespace seam::native_ui::design {

namespace {

formats::JsonValue rect(const ui::Rect& r) {
  return formats::JsonValue::Array{r.x, r.y, r.width, r.height};
}

std::string modeName(DesignMode mode) {
  return mode == DesignMode::Scene ? "scene" : "emo";
}

std::string workspaceName(Workspace workspace) {
  switch (workspace) {
    case Workspace::Voice: return "voice";
    case Workspace::Tune: return "tune";
    case Workspace::Mix: return "mix";
    case Workspace::Export: return "export";
    case Workspace::Sing: break;
  }
  return "sing";
}

std::string rackName(RackPresentation rack) {
  switch (rack) {
    case RackPresentation::Full: return "full";
    case RackPresentation::Rail: return "rail";
    case RackPresentation::Drawer: return "drawer";
  }
  return "unknown";
}

void flatten(const SemanticNode& node, const std::string& parent, formats::JsonValue::Array& out) {
  formats::JsonValue::Array actions;
  for (const auto action : node.actions) actions.emplace_back(static_cast<std::int64_t>(action));
  out.emplace_back(formats::JsonValue::Object{
      {"id", node.id},
      {"parent", parent},
      {"role", std::string{semanticRoleName(node.role)}},
      {"name", node.name},
      {"value", node.value},
      {"description", node.description},
      {"bounds", rect(node.bounds)},
      {"enabled", node.enabled},
      {"focused", node.focused},
      {"selected", node.selected},
      {"actions", std::move(actions)},
  });
  for (const auto& child : node.children) flatten(child, node.id, out);
}

}  // namespace

formats::JsonValue singLayoutEvidence(const SingShell& shell, double deviceScale) {
  const auto& l = shell.layout();
  formats::JsonValue::Object regions{
      {"header", rect(l.header)},         {"wordmark", rect(l.wordmark)},
      {"workspaceTabs", rect(l.workspaceTabs)}, {"modeSwitch", rect(l.modeSwitch)},
      {"transport", rect(l.transport)},   {"outputMeter", rect(l.outputMeter)},
      {"settings", rect(l.settings)},     {"editor", rect(l.editor)},
      {"tools", rect(l.tools)},           {"ruler", rect(l.ruler)},
      {"keyboard", rect(l.keyboard)},     {"grid", rect(l.grid)},
      {"lane", rect(l.lane)},             {"laneTabs", rect(l.laneTabs)},
      {"lanePlot", rect(l.lanePlot)},     {"laneTimePlot", rect(l.laneTimePlot)},
      {"rack", rect(l.rackArea)},         {"singer", rect(l.singer)},
      {"portraitRing", rect(l.portraitRing)}, {"expression", rect(l.expression)},
      {"style", rect(l.style)},           {"status", rect(l.status)},
  };
  formats::JsonValue::Object controls{
      {"classicToggle", rect(l.classicToggle)},     {"trackLabel", rect(l.trackLabel)},
      {"gridLabel", rect(l.gridLabel)},             {"playButton", rect(l.playButton)},
      {"positionReadout", rect(l.positionReadout)}, {"tempoReadout", rect(l.tempoReadout)},
      {"meterReadout", rect(l.meterReadout)},       {"singerChange", rect(l.singerChange)},
  };
  for (std::size_t i = 0U; i < l.workspaceTab.size(); ++i)
    controls.emplace("workspaceTab" + std::to_string(i), rect(l.workspaceTab[i]));
  if (l.workspaceMenuButton.width > 0.0)
    controls.emplace("workspaceMenuButton", rect(l.workspaceMenuButton));
  for (std::size_t i = 0U; i < l.knob.size(); ++i)
    controls.emplace("knob" + std::to_string(i), rect(l.knob[i]));
  if (const auto run = shell.exportRunButton(); run.width > 0.0) controls.emplace("exportRun", rect(run));
  if (l.inspectorButton.width > 0.0) controls.emplace("inspectorButton", rect(l.inspectorButton));
  if (l.inspectorOpen) regions.emplace("inspector", rect(l.inspector));
  return formats::JsonValue::Object{
      {"schema", "seam-ui-geometry-evidence-v1"},
      {"source", "SingShell::layout() of the presented frame"},
      {"presented", shell.presentedLastFrame()},
      {"mode", modeName(shell.mode())},
      {"workspace", workspaceName(shell.workspace())},
      {"logicalSize", formats::JsonValue::Array{l.width, l.height}},
      {"deviceScale", deviceScale},
      {"rack", rackName(l.rack)},
      {"inspectorOpen", l.inspectorOpen},
      {"knobsInOneRow", l.knobsInOneRow},
      {"compactHeader", l.compactHeader},
      {"workspaceLabelsVisible", l.workspaceLabelsVisible},
      {"outputMeterVisible", l.outputMeterVisible},
      {"regions", std::move(regions)},
      {"controls", std::move(controls)},
  };
}

formats::JsonValue semanticEvidence(const AccessibilityTree& tree, std::size_t noteLimit) {
  formats::JsonValue::Array nodes;
  flatten(tree.root(), {}, nodes);
  formats::JsonValue::Array notes;
  for (const auto& note : tree.materializeNotes(0U, noteLimit)) flatten(note, "virtual-notes", notes);
  const auto* focused = tree.focusedNode();
  return formats::JsonValue::Object{
      {"schema", "seam-ui-semantic-evidence-v1"},
      {"source", "accessibility tree the presented shell published for the same frame"},
      {"focused", focused == nullptr ? formats::JsonValue{} : formats::JsonValue{focused->id}},
      {"nodes", std::move(nodes)},
      {"virtualizedNoteCount", static_cast<std::int64_t>(tree.virtualizedNoteCount())},
      {"notes", std::move(notes)},
  };
}

}  // namespace seam::native_ui::design
