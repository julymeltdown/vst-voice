#pragma once

#include "seam/core/result.hpp"
#include "seam/standalone/native_editor_app.hpp"

#if defined(__APPLE__)

#include <memory>

namespace seam::standalone::macos {

class ApplicationDelegateHandle final {
public:
  ~ApplicationDelegateHandle();

  ApplicationDelegateHandle(const ApplicationDelegateHandle&) = delete;
  ApplicationDelegateHandle& operator=(const ApplicationDelegateHandle&) = delete;
  ApplicationDelegateHandle(ApplicationDelegateHandle&&) noexcept;
  ApplicationDelegateHandle& operator=(ApplicationDelegateHandle&&) noexcept;

  [[nodiscard]] static core::Result<std::unique_ptr<ApplicationDelegateHandle>>
  install(::seam::standalone::NativeEditorApp& app);

private:
  struct Impl;

  explicit ApplicationDelegateHandle(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

}

#endif
