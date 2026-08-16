#pragma once

#include "seam/core/result.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace seam::native_ui {

struct NativeWindowConfig final {
  std::string title{"Project SEAM"};
  std::uint32_t width{1440};
  std::uint32_t height{900};
  double scale{1.0};
  std::chrono::milliseconds autoCloseAfter{0};
  std::optional<std::filesystem::path> screenshotPath;
};

class INativeWindowClient {
public:
  virtual ~INativeWindowClient() = default;
  virtual void paint(RasterCanvas& canvas) noexcept = 0;
  virtual void resized(double logicalWidth, double logicalHeight,
                       double scale) noexcept = 0;
  virtual void pointerDown(const PointerEvent& event) noexcept = 0;
  virtual void pointerMove(const PointerEvent& event) noexcept = 0;
  virtual void pointerUp(const PointerEvent& event) noexcept = 0;
  virtual void scroll(double deltaX, double deltaY, ui::Point anchor,
                      InputModifiers modifiers) noexcept = 0;
  virtual void keyDown(const KeyEvent& event) noexcept = 0;
  virtual void textComposition(std::u32string text,
                               ui::CompositionSelection selection) noexcept = 0;
  virtual void textCommit(std::u32string text) noexcept = 0;
  virtual void textCancel() noexcept = 0;
  [[nodiscard]] virtual bool wantsClose() const noexcept = 0;
};

class INativeWindow {
public:
  virtual ~INativeWindow() = default;
  [[nodiscard]] virtual core::Result<void> open(
      const NativeWindowConfig& config, INativeWindowClient& client) = 0;
  [[nodiscard]] virtual int run() = 0;
  virtual void requestRepaint() noexcept = 0;
  virtual void beginTextInput(const TextInputRequest& request) = 0;
  virtual void endTextInput() noexcept = 0;
  [[nodiscard]] virtual PixelSurface snapshot() const = 0;
  [[nodiscard]] virtual std::string backendName() const = 0;
};

[[nodiscard]] std::unique_ptr<INativeWindow> createNativeWindow();

}  // namespace seam::native_ui
