#include "seam/native_ui/paint/canvas2d.hpp"

namespace seam::native_ui::paint {

std::shared_ptr<const Image> loadImage(const std::filesystem::path&, ImageLimits) {
  return nullptr;
}
std::unique_ptr<Canvas2D> makeCanvas(PixelSurface&, double) { return nullptr; }
bool vectorBackendAvailable() noexcept { return false; }
std::filesystem::path codeBundleResources() { return {}; }

}  // namespace seam::native_ui::paint
