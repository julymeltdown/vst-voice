#include "seam/clap_editor/embedded_view.hpp"

namespace seam::clap_editor {
namespace {
class UnavailableEmbeddedView final : public IEmbeddedView {
public:
  explicit UnavailableEmbeddedView(EditorRuntime&) {}
  [[nodiscard]] bool supportsApi(std::string_view) const noexcept override {
    return false;
  }
  [[nodiscard]] bool create(std::string_view, bool) override { return false; }
  void destroy() noexcept override {}
  [[nodiscard]] bool setScale(double) override { return false; }
  [[nodiscard]] bool setSize(std::uint32_t, std::uint32_t) override {
    return false;
  }
  [[nodiscard]] bool setParent(std::uintptr_t) override { return false; }
  [[nodiscard]] bool show() override { return false; }
  [[nodiscard]] bool hide() override { return true; }
  void onTimer() noexcept override {}
  void requestRepaint() noexcept override {}
  [[nodiscard]] std::string_view apiName() const noexcept override { return {}; }
};
}  // namespace

std::unique_ptr<IEmbeddedView> createEmbeddedView(EditorRuntime& runtime) {
  return std::make_unique<UnavailableEmbeddedView>(runtime);
}
}  // namespace seam::clap_editor
