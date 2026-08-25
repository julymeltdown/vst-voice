#include "platform_host.hpp"

namespace seam::clap_host {

struct HostWindow::Impl final {};

HostWindow::HostWindow() : impl_(std::make_unique<Impl>()) {}
HostWindow::~HostWindow() = default;

bool HostWindow::create(std::uint32_t, std::uint32_t) { return false; }
bool HostWindow::attach(clap_window_t&) const noexcept { return false; }
bool HostWindow::pump() noexcept { return false; }
bool HostWindow::capture(const std::filesystem::path&) const { return false; }
void HostWindow::destroy() noexcept {}
bool HostWindow::available() const noexcept { return false; }
const char* HostWindow::api() const noexcept { return ""; }

}
