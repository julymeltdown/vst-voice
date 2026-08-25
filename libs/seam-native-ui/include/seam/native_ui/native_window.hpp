#pragma once

#include "seam/core/result.hpp"
#include "seam/native_ui/editor_controller.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>

namespace seam::native_ui {

struct NativeWindowConfig final {
  std::string title{"Project SEAM"};
  std::uint32_t width{1440};
  std::uint32_t height{900};
  double scale{1.0};
  bool restoreLastDocument{true};
  std::uint32_t minimumWidth{480U};
  std::uint32_t minimumHeight{320U};
  std::chrono::milliseconds autoCloseAfter{0};
  std::optional<std::filesystem::path> screenshotPath;
};

[[nodiscard]] inline std::uint32_t nativeWindowMinimumPhysicalWidth(
    const NativeWindowConfig& config) noexcept {
  if (!std::isfinite(config.scale) || config.scale <= 0.0) return 0U;
  const auto physical = std::ceil(static_cast<double>(config.minimumWidth) *
                                  config.scale);
  return physical >=
                 static_cast<double>(std::numeric_limits<std::uint32_t>::max())
             ? std::numeric_limits<std::uint32_t>::max()
             : static_cast<std::uint32_t>(physical);
}

[[nodiscard]] inline std::uint32_t nativeWindowMinimumPhysicalHeight(
    const NativeWindowConfig& config) noexcept {
  if (!std::isfinite(config.scale) || config.scale <= 0.0) return 0U;
  const auto physical = std::ceil(static_cast<double>(config.minimumHeight) *
                                  config.scale);
  return physical >=
                 static_cast<double>(std::numeric_limits<std::uint32_t>::max())
             ? std::numeric_limits<std::uint32_t>::max()
             : static_cast<std::uint32_t>(physical);
}

[[nodiscard]] inline bool nativeWindowConfigSizeIsValid(
    const NativeWindowConfig& config) noexcept {
  if (config.width < 320U || config.height < 240U || config.width > 8192U ||
      config.height > 8192U || config.minimumWidth == 0U ||
      config.minimumHeight == 0U || config.minimumWidth > 8192U ||
      config.minimumHeight > 8192U || !std::isfinite(config.scale) ||
      config.scale < 0.5 || config.scale > 4.0) {
    return false;
  }
  return config.width >= nativeWindowMinimumPhysicalWidth(config) &&
         config.height >= nativeWindowMinimumPhysicalHeight(config);
}

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
  [[nodiscard]] virtual const AccessibilityTree* accessibilityTree()
      const noexcept {
    return nullptr;
  }
  [[nodiscard]] virtual core::Result<void> dispatchAccessibility(
      std::string_view, SemanticAction) noexcept {
    return core::failure(core::ErrorCode::Unsupported,
                         "Native accessibility dispatch is unavailable");
  }
  [[nodiscard]] virtual core::Result<void> setAccessibilityValue(
      std::string_view, std::string_view) {
    return core::failure(core::ErrorCode::Unsupported,
                         "Native accessibility value setting is unavailable");
  }
  virtual void openProjectPath(const std::filesystem::path&) noexcept {}
  [[nodiscard]] virtual std::optional<std::filesystem::path> documentPath()
      const noexcept {
    return std::nullopt;
  }
  [[nodiscard]] virtual bool requestClose() noexcept { return true; }
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
  virtual void saveRestorationState() noexcept {}
  [[nodiscard]] virtual PixelSurface snapshot() const = 0;
  [[nodiscard]] virtual std::string backendName() const = 0;
};

[[nodiscard]] std::unique_ptr<INativeWindow> createNativeWindow();

}  // namespace seam::native_ui
