#include "seam/native_ui/native_window.hpp"

#if defined(SEAM_NATIVE_WIN32)

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
#include <msctf.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace seam::native_ui {
namespace {

constexpr wchar_t kWindowClassName[] = L"ProjectSeamNativeEditorWindow";

std::wstring wideFromUtf8(std::string_view text) {
  if (text.empty()) return {};
  const auto size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  const auto converted = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      result.data(), size);
  if (converted != size) return {};
  return result;
}

std::string utf8FromWide(std::wstring_view text) {
  if (text.empty()) return {};
  const auto size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  const auto converted = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      result.data(), size, nullptr, nullptr);
  if (converted != size) return {};
  return result;
}

std::wstring wideFromUtf32(const std::u32string& text) {
  return wideFromUtf8(domain::toUtf8(text));
}

core::Result<std::u32string> utf32FromWide(std::wstring_view text) {
  const auto utf8 = utf8FromWide(text);
  if (!text.empty() && utf8.empty()) {
    return core::failure<std::u32string>(
        core::ErrorCode::ParseError, "Unable to convert UTF-16 text to UTF-8");
  }
  return domain::fromUtf8(utf8);
}

InputModifiers modifiersFromKeyboard() noexcept {
  const auto pressed = [](int key) {
    return (GetKeyState(key) & static_cast<SHORT>(0x8000)) != 0;
  };
  return InputModifiers{
      .shift = pressed(VK_SHIFT),
      .control = pressed(VK_CONTROL),
      .alt = pressed(VK_MENU),
      .command = pressed(VK_LWIN) || pressed(VK_RWIN),
  };
}

NativeKey nativeKey(WPARAM key) noexcept {
  switch (key) {
    case VK_SPACE: return NativeKey::Space;
    case VK_RETURN: return NativeKey::Enter;
    case VK_ESCAPE: return NativeKey::Escape;
    case VK_DELETE: return NativeKey::Delete;
    case VK_BACK: return NativeKey::Backspace;
    case VK_LEFT: return NativeKey::Left;
    case VK_RIGHT: return NativeKey::Right;
    case VK_UP: return NativeKey::Up;
    case VK_DOWN: return NativeKey::Down;
    case 'Z': return NativeKey::Z;
    case 'Y': return NativeKey::Y;
    case 'C': return NativeKey::C;
    case 'E': return NativeKey::E;
    case 'N': return NativeKey::N;
    case 'O': return NativeKey::O;
    case 'Q': return NativeKey::Q;
    case 'S': return NativeKey::S;
    case 'R': return NativeKey::R;
    case VK_ADD:
    case VK_OEM_PLUS: return NativeKey::Plus;
    case VK_SUBTRACT:
    case VK_OEM_MINUS: return NativeKey::Minus;
    default: return NativeKey::Unknown;
  }
}

PointerButton pointerButton(UINT message) noexcept {
  switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK: return PointerButton::Left;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK: return PointerButton::Middle;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK: return PointerButton::Right;
    default: return PointerButton::NoButton;
  }
}

class Win32NativeWindow final : public INativeWindow {
public:
  ~Win32NativeWindow() override { close(); }

  core::Result<void> open(const NativeWindowConfig& config,
                          INativeWindowClient& client) override {
    if (window_ != nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "Win32 window is already open");
    }
    if (config.width < 320U || config.height < 240U ||
        config.width > 8192U || config.height > 8192U ||
        !std::isfinite(config.scale) || config.scale < 0.5 ||
        config.scale > 4.0) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Native window dimensions or scale are invalid");
    }

    config_ = config;
    client_ = &client;
    auto textEngine = text::TextEngine::createSystem();
    if (textEngine) textEngine_ = std::move(textEngine.value());
    static_cast<void>(SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(initialized)) {
      comInitialized_ = true;
    } else if (initialized != RPC_E_CHANGED_MODE) {
      return core::failure(core::ErrorCode::Internal,
                           "Unable to initialize COM for the native editor");
    }
    initializeTsf();

    instance_ = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_OWNDC | CS_DBLCLKS;
    windowClass.lpfnWndProc = &Win32NativeWindow::windowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClassName;
    classAtom_ = RegisterClassExW(&windowClass);
    if (classAtom_ == 0U && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      close();
      return core::failure(core::ErrorCode::IoError,
                           "Unable to register the Win32 window class");
    }

    const auto title = wideFromUtf8(config.title);
    RECT bounds{0, 0, static_cast<LONG>(config.width),
                static_cast<LONG>(config.height)};
    static_cast<void>(AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0));
    window_ = CreateWindowExW(
        0, kWindowClassName, title.empty() ? L"Project SEAM" : title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr,
        instance_, this);
    if (window_ == nullptr) {
      close();
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create the Win32 editor window");
    }

    const auto dpi = GetDpiForWindow(window_);
    scale_ = config.scale * static_cast<double>(dpi == 0U ? 96U : dpi) / 96.0;
    const auto resized = surface_.resize(config.width, config.height);
    if (!resized) {
      close();
      return resized;
    }
    createEditOverlay();
    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    client_->resized(static_cast<double>(config.width) / scale_,
                     static_cast<double>(config.height) / scale_, scale_);
    openedAt_ = std::chrono::steady_clock::now();
    repaintRequested_.store(true, std::memory_order_release);
    return core::success();
  }

  int run() override {
    if (window_ == nullptr || client_ == nullptr) return 1;
    MSG message{};
    while (!destroyed_ && !client_->wantsClose()) {
      while (PeekMessageW(&message, nullptr, 0U, 0U, PM_REMOVE) != FALSE) {
        if (message.message == WM_QUIT) {
          destroyed_ = true;
          break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
      if (destroyed_) break;
      if (repaintRequested_.exchange(false, std::memory_order_acq_rel)) {
        InvalidateRect(window_, nullptr, FALSE);
        UpdateWindow(window_);
      }
      if (config_.autoCloseAfter.count() > 0 &&
          std::chrono::steady_clock::now() - openedAt_ >=
              config_.autoCloseAfter) {
        DestroyWindow(window_);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    if (!destroyed_ && window_ != nullptr) DestroyWindow(window_);
    if (config_.screenshotPath.has_value()) {
      const auto written = surface_.writePpm(*config_.screenshotPath);
      if (!written) return 2;
    }
    return 0;
  }

  void requestRepaint() noexcept override {
    repaintRequested_.store(true, std::memory_order_release);
    if (window_ != nullptr) PostMessageW(window_, WM_APP + 1U, 0U, 0);
  }

  void beginTextInput(const TextInputRequest& request) override {
    if (edit_ == nullptr) return;
    textInputActive_ = true;
    textRequest_ = request;
    const auto text = wideFromUtf32(request.currentText);
    SetWindowTextW(edit_, text.c_str());
    SendMessageW(edit_, EM_SETSEL, static_cast<WPARAM>(text.size()),
                 static_cast<LPARAM>(text.size()));
    const auto physical = [this](double value) {
      return static_cast<int>(std::lround(value * scale_));
    };
    SetWindowPos(edit_, HWND_TOP, physical(request.logicalBounds.x),
                 physical(request.logicalBounds.y),
                 std::max(80, physical(request.logicalBounds.width)),
                 std::max(24, physical(request.logicalBounds.height)),
                 SWP_SHOWWINDOW);
    ShowWindow(edit_, SW_SHOW);
    SetFocus(edit_);
    syncTextComposition();
  }

  void endTextInput() noexcept override {
    textInputActive_ = false;
    textRequest_.reset();
    if (edit_ != nullptr) ShowWindow(edit_, SW_HIDE);
    if (window_ != nullptr) SetFocus(window_);
    requestRepaint();
  }

  PixelSurface snapshot() const override { return surface_; }

  std::string backendName() const override {
    return tsfActive_ ? "Win32 software raster + TSF native edit overlay"
                      : "Win32 software raster + native edit overlay";
  }

private:
  static LRESULT CALLBACK windowProcedure(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32NativeWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
      self = static_cast<Win32NativeWindow*>(create->lpCreateParams);
      SetWindowLongPtrW(window, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
    }
    return self == nullptr ? DefWindowProcW(window, message, wParam, lParam)
                           : self->handleMessage(window, message, wParam, lParam);
  }

  static LRESULT CALLBACK editProcedure(HWND window, UINT message,
                                         WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32NativeWindow*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self != nullptr) {
      if (message == WM_GETDLGCODE) return DLGC_WANTALLKEYS;
      if (message == WM_KEYDOWN && wParam == VK_RETURN) {
        self->commitTextInput();
        return 0;
      }
      if (message == WM_KEYDOWN && wParam == VK_ESCAPE) {
        if (self->client_ != nullptr) self->client_->textCancel();
        self->endTextInput();
        return 0;
      }
    }
    const auto previous = self == nullptr ? nullptr : self->oldEditProcedure_;
    return previous == nullptr ? DefWindowProcW(window, message, wParam, lParam)
                               : CallWindowProcW(previous, window, message,
                                                 wParam, lParam);
  }

  LRESULT handleMessage(HWND window, UINT message, WPARAM wParam,
                        LPARAM lParam) noexcept {
    switch (message) {
      case WM_APP + 1U:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
      case WM_PAINT: {
        PAINTSTRUCT paint{};
        const auto device = BeginPaint(window, &paint);
        paintAndPresent(device);
        EndPaint(window, &paint);
        return 0;
      }
      case WM_ERASEBKGND:
        return 1;
      case WM_DPICHANGED: {
        const auto dpi = HIWORD(wParam);
        scale_ = config_.scale * static_cast<double>(dpi) / 96.0;
        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
        if (suggested != nullptr) {
          SetWindowPos(window, nullptr, suggested->left, suggested->top,
                       suggested->right - suggested->left,
                       suggested->bottom - suggested->top,
                       SWP_NOACTIVATE | SWP_NOZORDER);
        }
        requestRepaint();
        return 0;
      }
      case WM_SIZE: {
        const auto width = static_cast<std::uint32_t>(LOWORD(lParam));
        const auto height = static_cast<std::uint32_t>(HIWORD(lParam));
        if (width > 0U && height > 0U &&
            (width != surface_.width() || height != surface_.height())) {
          const auto resized = surface_.resize(width, height);
          if (resized && client_ != nullptr) {
            client_->resized(static_cast<double>(width) / scale_,
                             static_cast<double>(height) / scale_, scale_);
            requestRepaint();
          }
        }
        return 0;
      }
      case WM_COMMAND:
        if (reinterpret_cast<HWND>(lParam) == edit_ &&
            HIWORD(wParam) == EN_CHANGE && textInputActive_) {
          syncTextComposition();
          return 0;
        }
        break;
      case WM_LBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_LBUTTONDBLCLK:
      case WM_MBUTTONDBLCLK:
      case WM_RBUTTONDBLCLK:
        SetCapture(window);
        if (client_ != nullptr) {
          client_->pointerDown(PointerEvent{
              .position = logicalPoint(GET_X_LPARAM(lParam),
                                       GET_Y_LPARAM(lParam)),
              .button = pointerButton(message),
              .modifiers = modifiersFromKeyboard(),
              .clickCount = message == WM_LBUTTONDBLCLK ||
                                    message == WM_MBUTTONDBLCLK ||
                                    message == WM_RBUTTONDBLCLK
                                ? 2
                                : 1,
          });
        }
        return 0;
      case WM_LBUTTONUP:
      case WM_MBUTTONUP:
      case WM_RBUTTONUP:
        ReleaseCapture();
        if (client_ != nullptr) {
          client_->pointerUp(PointerEvent{
              .position = logicalPoint(GET_X_LPARAM(lParam),
                                       GET_Y_LPARAM(lParam)),
              .button = pointerButton(message),
              .modifiers = modifiersFromKeyboard(),
              .clickCount = 1,
          });
        }
        return 0;
      case WM_MOUSEMOVE:
        if (client_ != nullptr) {
          const auto buttons = static_cast<UINT>(wParam);
          client_->pointerMove(PointerEvent{
              .position = logicalPoint(GET_X_LPARAM(lParam),
                                       GET_Y_LPARAM(lParam)),
              .button = (buttons & MK_LBUTTON) != 0U
                            ? PointerButton::Left
                            : (buttons & MK_MBUTTON) != 0U
                                  ? PointerButton::Middle
                                  : (buttons & MK_RBUTTON) != 0U
                                        ? PointerButton::Right
                                        : PointerButton::NoButton,
              .modifiers = modifiersFromKeyboard(),
              .clickCount = 1,
          });
        }
        return 0;
      case WM_MOUSEWHEEL:
      case WM_MOUSEHWHEEL:
        if (client_ != nullptr) {
          POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
          ScreenToClient(window, &point);
          const auto delta = static_cast<double>(GET_WHEEL_DELTA_WPARAM(wParam));
          client_->scroll(message == WM_MOUSEHWHEEL ? -delta : 0.0,
                          message == WM_MOUSEWHEEL ? -delta : 0.0,
                          logicalPoint(point.x, point.y),
                          modifiersFromKeyboard());
        }
        return 0;
      case WM_KEYDOWN:
      case WM_SYSKEYDOWN:
        if (client_ != nullptr && !textInputActive_) {
          client_->keyDown(KeyEvent{
              .key = nativeKey(wParam),
              .modifiers = modifiersFromKeyboard(),
              .repeat = (lParam & (1LL << 30)) != 0,
          });
        }
        return 0;
      case WM_CLOSE:
        if (client_ == nullptr || client_->requestClose()) {
          DestroyWindow(window);
        }
        return 0;
      case WM_DESTROY:
        destroyed_ = true;
        edit_ = nullptr;
        oldEditProcedure_ = nullptr;
        window_ = nullptr;
        PostQuitMessage(0);
        return 0;
      default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
  }

  void initializeTsf() noexcept {
    const auto result = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr,
                                         CLSCTX_INPROC_SERVER,
                                         IID_ITfThreadMgr,
                                         reinterpret_cast<void**>(&threadManager_));
    if (FAILED(result) || threadManager_ == nullptr) return;
    if (SUCCEEDED(threadManager_->Activate(&tfClientId_))) {
      tsfActive_ = true;
    }
  }

  void createEditOverlay() noexcept {
    if (window_ == nullptr || edit_ != nullptr) return;
    edit_ = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_AUTOHSCROLL | WS_TABSTOP, 0, 0, 120, 28, window_,
        nullptr, instance_, nullptr);
    if (edit_ == nullptr) return;
    SetWindowLongPtrW(edit_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    oldEditProcedure_ = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        edit_, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&Win32NativeWindow::editProcedure)));
    SendMessageW(edit_, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                 TRUE);
    ShowWindow(edit_, SW_HIDE);
  }

  void syncTextComposition() noexcept {
    if (edit_ == nullptr || client_ == nullptr || !textInputActive_) return;
    const auto length = GetWindowTextLengthW(edit_);
    std::wstring text(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
    if (length > 0) {
      const auto copied = GetWindowTextW(edit_, text.data(), length + 1);
      if (copied < 0) return;
      text.resize(static_cast<std::size_t>(copied));
    } else {
      text.clear();
    }
    auto decoded = utf32FromWide(text);
    if (!decoded) return;
    DWORD selectionStart = 0U;
    DWORD selectionEnd = 0U;
    SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart),
                 reinterpret_cast<LPARAM>(&selectionEnd));
    const auto prefixLength = std::min<std::size_t>(selectionStart, text.size());
    const auto prefix = utf32FromWide(std::wstring_view{text}.substr(0U, prefixLength));
    const auto selectedLength = std::min<std::size_t>(
        selectionEnd >= selectionStart ? selectionEnd - selectionStart : 0U,
        text.size() - prefixLength);
    const auto selected = utf32FromWide(
        std::wstring_view{text}.substr(prefixLength, selectedLength));
    client_->textComposition(
        std::move(decoded.value()),
        ui::CompositionSelection{
            prefix ? prefix.value().size() : 0U,
            selected ? selected.value().size() : 0U,
        });
    requestRepaint();
  }

  void commitTextInput() noexcept {
    if (edit_ == nullptr || client_ == nullptr || !textInputActive_) return;
    const auto length = GetWindowTextLengthW(edit_);
    std::wstring text(static_cast<std::size_t>(std::max(0, length)) + 1U, L'\0');
    if (length > 0) {
      const auto copied = GetWindowTextW(edit_, text.data(), length + 1);
      if (copied < 0) return;
      text.resize(static_cast<std::size_t>(copied));
    } else {
      text.clear();
    }
    auto decoded = utf32FromWide(text);
    if (!decoded) return;
    client_->textCommit(std::move(decoded.value()));
    endTextInput();
  }

  ui::Point logicalPoint(int x, int y) const noexcept {
    return ui::Point{static_cast<double>(x) / scale_,
                     static_cast<double>(y) / scale_};
  }

  void paintAndPresent(HDC device) noexcept {
    if (client_ == nullptr || surface_.pixels().empty() || device == nullptr) {
      return;
    }
    RasterCanvas canvas{surface_, scale_, textEngine_.get()};
    client_->paint(canvas);
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = static_cast<LONG>(surface_.width());
    bitmap.bmiHeader.biHeight = -static_cast<LONG>(surface_.height());
    bitmap.bmiHeader.biPlanes = 1U;
    bitmap.bmiHeader.biBitCount = 32U;
    bitmap.bmiHeader.biCompression = BI_RGB;
    static_cast<void>(StretchDIBits(
        device, 0, 0, static_cast<int>(surface_.width()),
        static_cast<int>(surface_.height()), 0, 0,
        static_cast<int>(surface_.width()), static_cast<int>(surface_.height()),
        surface_.pixels().data(), &bitmap, DIB_RGB_COLORS, SRCCOPY));
  }

  void close() noexcept {
    if (edit_ != nullptr && IsWindow(edit_) != FALSE) {
      if (oldEditProcedure_ != nullptr) {
        SetWindowLongPtrW(edit_, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(oldEditProcedure_));
      }
      DestroyWindow(edit_);
    }
    edit_ = nullptr;
    oldEditProcedure_ = nullptr;
    if (window_ != nullptr) {
      DestroyWindow(window_);
      window_ = nullptr;
    }
    if (tsfActive_ && threadManager_ != nullptr) {
      static_cast<void>(threadManager_->Deactivate());
      tsfActive_ = false;
    }
    if (threadManager_ != nullptr) {
      threadManager_->Release();
      threadManager_ = nullptr;
    }
    if (classAtom_ != 0U && instance_ != nullptr) {
      static_cast<void>(UnregisterClassW(kWindowClassName, instance_));
      classAtom_ = 0U;
    }
    if (comInitialized_) {
      CoUninitialize();
      comInitialized_ = false;
    }
  }

  NativeWindowConfig config_;
  INativeWindowClient* client_{nullptr};
  HINSTANCE instance_{nullptr};
  HWND window_{nullptr};
  HWND edit_{nullptr};
  WNDPROC oldEditProcedure_{nullptr};
  ATOM classAtom_{0U};
  ITfThreadMgr* threadManager_{nullptr};
  TfClientId tfClientId_{TF_CLIENTID_NULL};
  PixelSurface surface_;
  std::unique_ptr<text::TextEngine> textEngine_;
  double scale_{1.0};
  std::atomic<bool> repaintRequested_{false};
  std::chrono::steady_clock::time_point openedAt_{};
  std::optional<TextInputRequest> textRequest_;
  bool textInputActive_{false};
  bool tsfActive_{false};
  bool comInitialized_{false};
  bool destroyed_{false};
};

}  // namespace

std::unique_ptr<INativeWindow> createNativeWindow() {
  return std::make_unique<Win32NativeWindow>();
}

}  // namespace seam::native_ui

#endif
