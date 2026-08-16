#include "seam/native_ui/native_window.hpp"

#if !defined(SEAM_NATIVE_X11)

namespace seam::native_ui {
namespace {

class UnavailableWindow final : public INativeWindow {
public:
  core::Result<void> open(const NativeWindowConfig&, INativeWindowClient&) override {
    return core::failure(core::ErrorCode::Unsupported,
                         "No native window backend is available for this platform");
  }
  int run() override { return 1; }
  void requestRepaint() noexcept override {}
  void beginTextInput(const TextInputRequest&) override {}
  void endTextInput() noexcept override {}
  PixelSurface snapshot() const override { return PixelSurface{1, 1}; }
  std::string backendName() const override { return "unavailable"; }
};

}  // namespace

std::unique_ptr<INativeWindow> createNativeWindow() {
  return std::make_unique<UnavailableWindow>();
}

}  // namespace seam::native_ui

#endif
