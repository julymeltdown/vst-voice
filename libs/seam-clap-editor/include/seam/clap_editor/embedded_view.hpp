#pragma once

#include "seam/clap_editor/editor_runtime.hpp"
#include "seam/core/result.hpp"
#include "seam/native_ui/pixel_surface.hpp"

#include <clap/clap.h>

#include <cstdint>
#include <memory>
#include <string_view>

namespace seam::clap_editor {

class IEmbeddedView {
public:
  virtual ~IEmbeddedView() = default;
  [[nodiscard]] virtual bool supportsApi(std::string_view api) const noexcept = 0;
  [[nodiscard]] virtual bool create(std::string_view api, bool floating) = 0;
  virtual void destroy() noexcept = 0;
  [[nodiscard]] virtual bool setScale(double scale) = 0;
  [[nodiscard]] virtual bool setSize(std::uint32_t width,
                                     std::uint32_t height) = 0;
  [[nodiscard]] virtual bool setParent(std::uintptr_t parent) = 0;
  [[nodiscard]] virtual bool show() = 0;
  [[nodiscard]] virtual bool hide() = 0;
  virtual void onTimer() noexcept = 0;
  virtual void requestRepaint() noexcept = 0;
  [[nodiscard]] virtual std::string_view apiName() const noexcept = 0;
  [[nodiscard]] virtual native_ui::PixelSurface snapshot() const { return {}; }
};

[[nodiscard]] std::unique_ptr<IEmbeddedView> createEmbeddedView(
    EditorRuntime& runtime);

}  // namespace seam::clap_editor
