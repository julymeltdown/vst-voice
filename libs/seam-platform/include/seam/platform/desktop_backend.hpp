#pragma once

#include "seam/platform/audio_callback.hpp"

#include <string_view>

namespace seam::platform {

struct DesktopWindowOptions final {
  std::string_view title{"Project SEAM"};
  int width{1440};
  int height{900};
  bool highDpi{true};
};

class IDesktopBackend {
public:
  virtual ~IDesktopBackend() = default;
  virtual bool initialize(const DesktopWindowOptions& options,
                          IAudioProcessor& audioProcessor) = 0;
  virtual int run() = 0;
  virtual void requestFrame() noexcept = 0;
};

// Phase 1 keeps editor/application code independent of the final shell.
// The production adapter will implement this interface with iPlug2 + Skia.
class HeadlessDesktopBackend final : public IDesktopBackend {
public:
  bool initialize(const DesktopWindowOptions&, IAudioProcessor&) override { return true; }
  int run() override { return 0; }
  void requestFrame() noexcept override {}
};

}  // namespace seam::platform
