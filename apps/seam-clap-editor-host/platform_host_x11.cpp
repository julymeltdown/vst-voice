#include "platform_host.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <thread>

namespace seam::clap_host {

struct HostWindow::Impl final {
  Display* display{nullptr};
  Window parent{0U};
  std::uint32_t width{0U};
  std::uint32_t height{0U};
};

HostWindow::HostWindow() : impl_(std::make_unique<Impl>()) {}

HostWindow::~HostWindow() { destroy(); }

bool HostWindow::create(std::uint32_t width, std::uint32_t height) {
  if (impl_ == nullptr || width == 0U || height == 0U || available()) {
    return false;
  }
  impl_->display = XOpenDisplay(nullptr);
  if (impl_->display == nullptr) return false;
  impl_->width = width;
  impl_->height = height;
  impl_->parent = XCreateSimpleWindow(
      impl_->display, DefaultRootWindow(impl_->display), 0, 0,
      static_cast<unsigned int>(width), static_cast<unsigned int>(height), 0U,
      BlackPixel(impl_->display, DefaultScreen(impl_->display)),
      WhitePixel(impl_->display, DefaultScreen(impl_->display)));
  if (impl_->parent == 0U) {
    destroy();
    return false;
  }
  XSelectInput(impl_->display, impl_->parent, StructureNotifyMask | ExposureMask);
  XMapWindow(impl_->display, impl_->parent);
  XFlush(impl_->display);
  return true;
}

bool HostWindow::attach(clap_window_t& parent) const noexcept {
  if (!available()) return false;
  parent = clap_window_t{};
  parent.api = CLAP_WINDOW_API_X11;
  parent.x11 = impl_->parent;
  return true;
}

bool HostWindow::pump() noexcept {
  if (!available()) return false;
  while (XPending(impl_->display) > 0) {
    XEvent event{};
    XNextEvent(impl_->display, &event);
    if (event.type == DestroyNotify && event.xdestroywindow.window == impl_->parent) {
      destroy();
      return false;
    }
  }
  XSync(impl_->display, False);
  return true;
}

bool HostWindow::capture(const std::filesystem::path& path) const {
  if (!available()) return false;
  XWindowAttributes attributes{};
  if (XGetWindowAttributes(impl_->display, impl_->parent, &attributes) == 0) {
    return false;
  }
  auto* image = XGetImage(
      impl_->display, impl_->parent, 0, 0,
      static_cast<unsigned int>(std::max(0, attributes.width)),
      static_cast<unsigned int>(std::max(0, attributes.height)), AllPlanes,
      ZPixmap);
  if (image == nullptr) return false;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << "P6\n" << attributes.width << ' ' << attributes.height
         << "\n255\n";
  for (int y = 0; y < attributes.height; ++y) {
    for (int x = 0; x < attributes.width; ++x) {
      const auto pixel = XGetPixel(image, x, y);
      const std::array<char, 3> rgb{
          static_cast<char>((pixel >> 16U) & 0xffU),
          static_cast<char>((pixel >> 8U) & 0xffU),
          static_cast<char>(pixel & 0xffU),
      };
      output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
  }
  XDestroyImage(image);
  return static_cast<bool>(output);
}

void HostWindow::destroy() noexcept {
  if (impl_ == nullptr) return;
  if (impl_->display != nullptr && impl_->parent != 0U) {
    XDestroyWindow(impl_->display, impl_->parent);
    XFlush(impl_->display);
  }
  if (impl_->display != nullptr) XCloseDisplay(impl_->display);
  impl_->display = nullptr;
  impl_->parent = 0U;
}

bool HostWindow::available() const noexcept {
  return impl_ != nullptr && impl_->display != nullptr && impl_->parent != 0U;
}

const char* HostWindow::api() const noexcept { return CLAP_WINDOW_API_X11; }

}
