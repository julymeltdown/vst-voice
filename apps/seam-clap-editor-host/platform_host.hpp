#pragma once

#include <clap/clap.h>

#include <cstdint>
#include <filesystem>
#include <memory>

namespace seam::clap_host {

class HostWindow final {
public:
  HostWindow();
  ~HostWindow();

  HostWindow(const HostWindow&) = delete;
  HostWindow& operator=(const HostWindow&) = delete;

  [[nodiscard]] bool create(std::uint32_t width, std::uint32_t height);
  [[nodiscard]] bool attach(clap_window_t& parent) const noexcept;
  [[nodiscard]] bool pump() noexcept;
  [[nodiscard]] bool capture(const std::filesystem::path& path) const;
  void destroy() noexcept;

  [[nodiscard]] bool available() const noexcept;
  [[nodiscard]] const char* api() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}
