#include "seam/clap_editor/embedded_view.hpp"

#if defined(_WIN32)

#include "seam/domain/note.hpp"
#include "seam/text/text_engine.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

namespace seam::clap_editor {
namespace {

constexpr wchar_t kClassName[] = L"ProjectSeamClapEditorChild";

native_ui::InputModifiers modifiers() noexcept {
  const auto down = [](int key) {
    return (GetKeyState(key) & static_cast<SHORT>(0x8000)) != 0;
  };
  return native_ui::InputModifiers{
      .shift = down(VK_SHIFT),
      .control = down(VK_CONTROL),
      .alt = down(VK_MENU),
      .command = down(VK_LWIN) || down(VK_RWIN),
  };
}

native_ui::NativeKey keyFor(WPARAM key) noexcept {
  switch (key) {
    case VK_SPACE: return native_ui::NativeKey::Space;
    case VK_RETURN: return native_ui::NativeKey::Enter;
    case VK_ESCAPE: return native_ui::NativeKey::Escape;
    case VK_DELETE: return native_ui::NativeKey::Delete;
    case VK_BACK: return native_ui::NativeKey::Backspace;
    case VK_LEFT: return native_ui::NativeKey::Left;
    case VK_RIGHT: return native_ui::NativeKey::Right;
    case VK_UP: return native_ui::NativeKey::Up;
    case VK_DOWN: return native_ui::NativeKey::Down;
    case 'Z': return native_ui::NativeKey::Z;
    case 'Y': return native_ui::NativeKey::Y;
    case 'C': return native_ui::NativeKey::C;
    case VK_OEM_PLUS:
    case VK_ADD: return native_ui::NativeKey::Plus;
    case VK_OEM_MINUS:
    case VK_SUBTRACT: return native_ui::NativeKey::Minus;
    default: return native_ui::NativeKey::Unknown;
  }
}

std::u32string utf32FromWindowText(HWND edit) {
  const auto length = GetWindowTextLengthW(edit);
  std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
  if (length > 0) {
    const auto copied = GetWindowTextW(edit, value.data(), length + 1);
    if (copied <= 0) return {};
    value.resize(static_cast<std::size_t>(copied));
  } else {
    value.clear();
  }
  if (value.empty()) return {};
  const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                             value.data(),
                                             static_cast<int>(value.size()),
                                             nullptr, 0, nullptr, nullptr);
  if (required <= 0) return {};
  std::string utf8(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), utf8.data(), required,
                          nullptr, nullptr) != required) {
    return {};
  }
  const auto decoded = domain::fromUtf8(utf8);
  return decoded ? decoded.value() : std::u32string{};
}

class Win32EmbeddedView final : public IEmbeddedView {
public:
  explicit Win32EmbeddedView(EditorRuntime& runtime) : runtime_(runtime) {
    runtime_.setRepaintCallback([this] { requestRepaint(); });
    runtime_.setTextInputCallbacks(
        [this](const native_ui::TextInputRequest& request) {
          beginTextInput(request);
        },
        [this] { endTextInput(); });
  }

  ~Win32EmbeddedView() override { destroy(); }

  [[nodiscard]] bool supportsApi(std::string_view api) const noexcept override {
    return api == "win32";
  }

  [[nodiscard]] bool create(std::string_view api, bool floating) override {
    if (floating || !supportsApi(api) || created_) return false;
    instance_ = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC | CS_DBLCLKS;
    windowClass.lpfnWndProc = &Win32EmbeddedView::windowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kClassName;
    classAtom_ = RegisterClassExW(&windowClass);
    if (classAtom_ == 0U && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return false;
    }
    auto textEngine = text::TextEngine::createSystem();
    if (textEngine) textEngine_ = std::move(textEngine.value());
    created_ = true;
    return true;
  }

  void destroy() noexcept override {
    runtime_.setTextInputCallbacks({}, {});
    if (edit_ != nullptr && IsWindow(edit_) != FALSE) DestroyWindow(edit_);
    edit_ = nullptr;
    if (window_ != nullptr && IsWindow(window_) != FALSE) DestroyWindow(window_);
    window_ = nullptr;
    parent_ = nullptr;
    if (classAtom_ != 0U && instance_ != nullptr) {
      static_cast<void>(UnregisterClassW(kClassName, instance_));
    }
    classAtom_ = 0U;
    textEngine_.reset();
    created_ = false;
    visible_ = false;
  }

  [[nodiscard]] bool setScale(double scale) override {
    if (!std::isfinite(scale) || scale < 0.5 || scale > 4.0) return false;
    scale_ = scale;
    runtime_.resize(static_cast<double>(width_) / scale_,
                    static_cast<double>(height_) / scale_);
    requestRepaint();
    return true;
  }

  [[nodiscard]] bool setSize(std::uint32_t width,
                             std::uint32_t height) override {
    if (width < 640U || height < 420U || width > 8192U || height > 8192U) {
      return false;
    }
    width_ = width;
    height_ = height;
    if (!surface_.resize(width_, height_)) return false;
    if (window_ != nullptr) {
      static_cast<void>(SetWindowPos(window_, nullptr, 0, 0,
                                     static_cast<int>(width_),
                                     static_cast<int>(height_),
                                     SWP_NOMOVE | SWP_NOZORDER));
    }
    runtime_.resize(static_cast<double>(width_) / scale_,
                    static_cast<double>(height_) / scale_);
    requestRepaint();
    return true;
  }

  [[nodiscard]] bool setParent(std::uintptr_t parent) override {
    if (!created_ || parent == 0U || window_ != nullptr) return false;
    parent_ = reinterpret_cast<HWND>(parent);
    window_ = CreateWindowExW(0, kClassName, L"", WS_CHILD | WS_CLIPSIBLINGS |
                                                    WS_CLIPCHILDREN,
                              0, 0, static_cast<int>(width_),
                              static_cast<int>(height_), parent_, nullptr,
                              instance_, this);
    if (window_ == nullptr || !surface_.resize(width_, height_)) {
      destroy();
      return false;
    }
    edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                            WS_CHILD | ES_AUTOHSCROLL, 0, 0, 160, 26, window_,
                            nullptr, instance_, nullptr);
    if (edit_ != nullptr) ShowWindow(edit_, SW_HIDE);
    runtime_.resize(static_cast<double>(width_) / scale_,
                    static_cast<double>(height_) / scale_);
    requestRepaint();
    return true;
  }

  [[nodiscard]] bool show() override {
    if (window_ == nullptr) return false;
    ShowWindow(window_, SW_SHOW);
    visible_ = true;
    requestRepaint();
    return true;
  }

  [[nodiscard]] bool hide() override {
    if (window_ == nullptr) return false;
    ShowWindow(window_, SW_HIDE);
    visible_ = false;
    return true;
  }

  void onTimer() noexcept override {
    runtime_.tick();
    if (visible_ && repaint_.exchange(false, std::memory_order_acq_rel) &&
        window_ != nullptr) {
      InvalidateRect(window_, nullptr, FALSE);
      UpdateWindow(window_);
    }
  }

  void requestRepaint() noexcept override {
    repaint_.store(true, std::memory_order_release);
    if (window_ != nullptr) PostMessageW(window_, WM_APP + 1U, 0U, 0);
  }

  [[nodiscard]] std::string_view apiName() const noexcept override {
    return "win32";
  }

private:
  static LRESULT CALLBACK windowProcedure(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32EmbeddedView*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
      self = static_cast<Win32EmbeddedView*>(create->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
    }
    return self != nullptr ? self->handle(window, message, wParam, lParam)
                           : DefWindowProcW(window, message, wParam, lParam);
  }

  LRESULT handle(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
      case WM_PAINT: {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(window, &paint);
        paintAndPresent(dc);
        EndPaint(window, &paint);
        return 0;
      }
      case WM_SIZE: {
        const auto width = static_cast<std::uint32_t>(LOWORD(lParam));
        const auto height = static_cast<std::uint32_t>(HIWORD(lParam));
        if (width > 0U && height > 0U) {
          width_ = width;
          height_ = height;
          if (surface_.resize(width_, height_)) {
            runtime_.resize(static_cast<double>(width_) / scale_,
                            static_cast<double>(height_) / scale_);
          }
        }
        requestRepaint();
        return 0;
      }
      case WM_LBUTTONDOWN:
      case WM_LBUTTONDBLCLK:
        SetCapture(window);
        runtime_.pointerDown(native_ui::PointerEvent{
            .position = logical(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)),
            .button = native_ui::PointerButton::Left,
            .modifiers = modifiers(),
            .clickCount = message == WM_LBUTTONDBLCLK ? 2 : 1,
        });
        return 0;
      case WM_LBUTTONUP:
        ReleaseCapture();
        runtime_.pointerUp(native_ui::PointerEvent{
            .position = logical(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)),
            .button = native_ui::PointerButton::Left,
            .modifiers = modifiers(),
            .clickCount = 1,
        });
        return 0;
      case WM_MOUSEMOVE:
        runtime_.pointerMove(native_ui::PointerEvent{
            .position = logical(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)),
            .button = (wParam & MK_LBUTTON) != 0U
                          ? native_ui::PointerButton::Left
                          : native_ui::PointerButton::NoButton,
            .modifiers = modifiers(),
            .clickCount = 1,
        });
        return 0;
      case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ScreenToClient(window, &point);
        runtime_.scroll(0.0, -static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam)),
                        logical(point.x, point.y), modifiers());
        return 0;
      }
      case WM_KEYDOWN:
        if (!runtime_.textInputActive()) {
          runtime_.keyDown(native_ui::KeyEvent{
              .key = keyFor(wParam),
              .modifiers = modifiers(),
              .repeat = (lParam & (1LL << 30)) != 0,
          });
        }
        return 0;
      case WM_COMMAND:
        if (reinterpret_cast<HWND>(lParam) == edit_ &&
            HIWORD(wParam) == EN_CHANGE && runtime_.textInputActive()) {
          runtime_.textComposition(utf32FromWindowText(edit_), {});
          return 0;
        }
        break;
      case WM_APP + 1U:
        if (visible_) InvalidateRect(window, nullptr, FALSE);
        return 0;
      default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
  }

  ui::Point logical(int x, int y) const noexcept {
    return ui::Point{static_cast<double>(x) / scale_,
                     static_cast<double>(y) / scale_};
  }

  void beginTextInput(const native_ui::TextInputRequest& request) {
    if (edit_ == nullptr) return;
    const auto utf8 = domain::toUtf8(request.currentText);
    const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                               utf8.data(),
                                               static_cast<int>(utf8.size()),
                                               nullptr, 0);
    std::wstring wide;
    if (required > 0) {
      wide.resize(static_cast<std::size_t>(required));
      static_cast<void>(MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
          static_cast<int>(utf8.size()), wide.data(), required));
    }
    SetWindowTextW(edit_, wide.c_str());
    const auto physical = [this](double value) {
      return static_cast<int>(std::lround(value * scale_));
    };
    SetWindowPos(edit_, HWND_TOP, physical(request.logicalBounds.x),
                 physical(request.logicalBounds.y),
                 std::max(80, physical(request.logicalBounds.width)),
                 std::max(24, physical(request.logicalBounds.height)),
                 SWP_SHOWWINDOW);
    SetFocus(edit_);
  }

  void endTextInput() noexcept {
    if (edit_ != nullptr) ShowWindow(edit_, SW_HIDE);
    if (window_ != nullptr) SetFocus(window_);
  }

  void paintAndPresent(HDC dc) noexcept {
    if (dc == nullptr || surface_.pixels().empty()) return;
    native_ui::RasterCanvas canvas{surface_, scale_, textEngine_.get()};
    runtime_.paint(canvas);
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = static_cast<LONG>(surface_.width());
    bitmap.bmiHeader.biHeight = -static_cast<LONG>(surface_.height());
    bitmap.bmiHeader.biPlanes = 1U;
    bitmap.bmiHeader.biBitCount = 32U;
    bitmap.bmiHeader.biCompression = BI_RGB;
    static_cast<void>(StretchDIBits(
        dc, 0, 0, static_cast<int>(surface_.width()),
        static_cast<int>(surface_.height()), 0, 0,
        static_cast<int>(surface_.width()),
        static_cast<int>(surface_.height()), surface_.pixels().data(),
        &bitmap, DIB_RGB_COLORS, SRCCOPY));
  }

  EditorRuntime& runtime_;
  HINSTANCE instance_{nullptr};
  HWND parent_{nullptr};
  HWND window_{nullptr};
  HWND edit_{nullptr};
  ATOM classAtom_{0U};
  native_ui::PixelSurface surface_;
  std::unique_ptr<text::TextEngine> textEngine_;
  std::uint32_t width_{960U};
  std::uint32_t height_{680U};
  double scale_{1.0};
  bool created_{false};
  bool visible_{false};
  std::atomic<bool> repaint_{true};
};

}  // namespace

std::unique_ptr<IEmbeddedView> createEmbeddedView(EditorRuntime& runtime) {
  return std::make_unique<Win32EmbeddedView>(runtime);
}

}  // namespace seam::clap_editor

#endif
