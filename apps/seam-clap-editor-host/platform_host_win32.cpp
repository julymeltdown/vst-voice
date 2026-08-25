#include "platform_host.hpp"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

namespace seam::clap_host {
namespace {

constexpr wchar_t kClassName[] = L"ProjectSeamClapHostWindow";

struct WindowState final {
  HINSTANCE instance{nullptr};
  HWND window{nullptr};
  HWND content{nullptr};
  ATOM classAtom{0U};
  std::uint32_t width{0U};
  std::uint32_t height{0U};
};

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam,
                                 LPARAM lParam) {
  auto* state = reinterpret_cast<WindowState*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
    state = static_cast<WindowState*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(state));
    state->window = window;
  }
  if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
  if (message == WM_SIZE && state->content != nullptr) {
    state->width = static_cast<std::uint32_t>(LOWORD(lParam));
    state->height = static_cast<std::uint32_t>(HIWORD(lParam));
    MoveWindow(state->content, 0, 0, static_cast<int>(state->width),
               static_cast<int>(state->height), TRUE);
    return 0;
  }
  if (message == WM_NCDESTROY) state->window = nullptr;
  return DefWindowProcW(window, message, wParam, lParam);
}

}

struct HostWindow::Impl final {
  WindowState state;
};

HostWindow::HostWindow() : impl_(std::make_unique<Impl>()) {}

HostWindow::~HostWindow() { destroy(); }

bool HostWindow::create(std::uint32_t width, std::uint32_t height) {
  if (impl_ == nullptr || width == 0U || height == 0U || available()) {
    return false;
  }
  auto& state = impl_->state;
  state.instance = GetModuleHandleW(nullptr);
  state.width = width;
  state.height = height;
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.style = CS_OWNDC;
  windowClass.lpfnWndProc = &windowProcedure;
  windowClass.hInstance = state.instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  windowClass.lpszClassName = kClassName;
  state.classAtom = RegisterClassExW(&windowClass);
  if (state.classAtom == 0U && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    destroy();
    return false;
  }
  state.window = CreateWindowExW(
      0, kClassName, L"Project SEAM CLAP host", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(width),
      static_cast<int>(height), nullptr, nullptr, state.instance, &state);
  if (state.window == nullptr) {
    destroy();
    return false;
  }
  state.content = CreateWindowExW(
      0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
                          WS_CLIPCHILDREN,
      0, 0, static_cast<int>(width), static_cast<int>(height), state.window,
      nullptr, state.instance, nullptr);
  if (state.content == nullptr) {
    destroy();
    return false;
  }
  ShowWindow(state.window, SW_SHOW);
  UpdateWindow(state.window);
  return available();
}

bool HostWindow::attach(clap_window_t& parent) const noexcept {
  if (!available()) return false;
  parent = clap_window_t{};
  parent.api = CLAP_WINDOW_API_WIN32;
  parent.win32 = impl_->state.content;
  return true;
}

bool HostWindow::pump() noexcept {
  if (!available()) return false;
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
    if (message.message == WM_QUIT) {
      impl_->state.window = nullptr;
      return false;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return available();
}

bool HostWindow::capture(const std::filesystem::path& path) const {
  if (!available()) return false;
  RECT bounds{};
  if (GetClientRect(impl_->state.content, &bounds) == FALSE) return false;
  const auto width = bounds.right - bounds.left;
  const auto height = bounds.bottom - bounds.top;
  if (width <= 0 || height <= 0) return false;
  const auto source = GetDC(impl_->state.content);
  if (source == nullptr) return false;
  const auto memory = CreateCompatibleDC(source);
  const auto bitmap = CreateCompatibleBitmap(source, width, height);
  if (memory == nullptr || bitmap == nullptr) {
    if (bitmap != nullptr) DeleteObject(bitmap);
    if (memory != nullptr) DeleteDC(memory);
    ReleaseDC(impl_->state.content, source);
    return false;
  }
  const auto previous = SelectObject(memory, bitmap);
  const auto copied = BitBlt(memory, 0, 0, width, height, source, 0, 0,
                             SRCCOPY | CAPTUREBLT);
  SelectObject(memory, previous);
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1U;
  info.bmiHeader.biBitCount = 32U;
  info.bmiHeader.biCompression = BI_RGB;
  const auto read = GetDIBits(memory, bitmap, 0U, static_cast<UINT>(height),
                              pixels.data(), &info, DIB_RGB_COLORS);
  DeleteObject(bitmap);
  DeleteDC(memory);
  ReleaseDC(impl_->state.content, source);
  if (copied == FALSE || read != static_cast<UINT>(height)) return false;
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << "P6\n" << width << ' ' << height << "\n255\n";
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto offset =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x)) * 4U;
      const std::array<char, 3> rgb{
          static_cast<char>(pixels[offset + 2U]),
          static_cast<char>(pixels[offset + 1U]),
          static_cast<char>(pixels[offset]),
      };
      output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
  }
  return static_cast<bool>(output);
}

void HostWindow::destroy() noexcept {
  if (impl_ == nullptr) return;
  auto& state = impl_->state;
  if (state.content != nullptr && IsWindow(state.content) != FALSE) {
    DestroyWindow(state.content);
  }
  state.content = nullptr;
  if (state.window != nullptr && IsWindow(state.window) != FALSE) {
    DestroyWindow(state.window);
  }
  state.window = nullptr;
  if (state.classAtom != 0U && state.instance != nullptr) {
    static_cast<void>(UnregisterClassW(kClassName, state.instance));
  }
  state.classAtom = 0U;
}

bool HostWindow::available() const noexcept {
  return impl_ != nullptr && impl_->state.window != nullptr &&
         impl_->state.content != nullptr;
}

const char* HostWindow::api() const noexcept { return CLAP_WINDOW_API_WIN32; }

}

#endif
