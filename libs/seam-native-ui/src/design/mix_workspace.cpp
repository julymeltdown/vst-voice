#include "seam/native_ui/design/shell_workspace.hpp"

namespace seam::native_ui::design {
namespace {

// Placeholder until the MIX workspace lands; it publishes a panel and refuses every action.
class MixWorkspace final : public ShellWorkspace {
public:
  [[nodiscard]] std::string_view idPrefix() const noexcept override { return "shell.mix."; }
  void paint(paint::Canvas2D&, const DesignTokens&, const NativeEditorController&,
             const EditorSceneState&, ui::Rect) const override {}
  core::Result<void> pointerDown(NativeEditorController&, const PointerEvent&, ui::Rect) override {
    return core::success();
  }
  core::Result<void> pointerMove(NativeEditorController&, const PointerEvent&, ui::Rect) override {
    return core::success();
  }
  core::Result<void> pointerUp(NativeEditorController&, const PointerEvent&, ui::Rect) override {
    return core::success();
  }
  void semantics(const NativeEditorController&, const EditorSceneState&, ui::Rect area,
                 std::vector<SemanticNode>& out) const override {
    out.push_back(SemanticNode{.id = "shell.mix.panel", .role = SemanticRole::Panel,
                               .name = "MIX", .bounds = area,
                               .actions = {SemanticAction::SetFocus}});
  }
  core::Result<void> perform(NativeEditorController&, std::string_view, SemanticAction) override {
    return core::failure(core::ErrorCode::Unsupported, "This element does not support that action");
  }
};

}  // namespace

std::unique_ptr<ShellWorkspace> makeMixWorkspace() { return std::make_unique<MixWorkspace>(); }

}  // namespace seam::native_ui::design
