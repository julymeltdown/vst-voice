#include "seam/clap_editor/embedded_view.hpp"

#if defined(__linux__)

#include "seam/domain/note.hpp"
#include "seam/text/text_engine.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace seam::clap_editor {
namespace {

native_ui::InputModifiers modifiers(unsigned int state) noexcept {
  return native_ui::InputModifiers{
      .shift = (state & ShiftMask) != 0U,
      .control = (state & ControlMask) != 0U,
      .alt = (state & Mod1Mask) != 0U,
      .command = (state & Mod4Mask) != 0U,
  };
}

native_ui::PointerButton button(unsigned int value) noexcept {
  switch (value) {
    case Button1: return native_ui::PointerButton::Left;
    case Button2: return native_ui::PointerButton::Middle;
    case Button3: return native_ui::PointerButton::Right;
    default: return native_ui::PointerButton::NoButton;
  }
}

native_ui::NativeKey keyFor(KeySym key) noexcept {
  switch (key) {
    case XK_space: return native_ui::NativeKey::Space;
    case XK_Return:
    case XK_KP_Enter: return native_ui::NativeKey::Enter;
    case XK_Escape: return native_ui::NativeKey::Escape;
    case XK_Delete: return native_ui::NativeKey::Delete;
    case XK_BackSpace: return native_ui::NativeKey::Backspace;
    case XK_Left: return native_ui::NativeKey::Left;
    case XK_Right: return native_ui::NativeKey::Right;
    case XK_Up: return native_ui::NativeKey::Up;
    case XK_Down: return native_ui::NativeKey::Down;
    case XK_z:
    case XK_Z: return native_ui::NativeKey::Z;
    case XK_y:
    case XK_Y: return native_ui::NativeKey::Y;
    case XK_c:
    case XK_C: return native_ui::NativeKey::C;
    case XK_plus:
    case XK_equal:
    case XK_KP_Add: return native_ui::NativeKey::Plus;
    case XK_minus:
    case XK_KP_Subtract: return native_ui::NativeKey::Minus;
    default: return native_ui::NativeKey::Unknown;
  }
}

class X11EmbeddedView final : public IEmbeddedView {
public:
  explicit X11EmbeddedView(EditorRuntime& runtime) : runtime_(runtime) {
    runtime_.setRepaintCallback([this] { requestRepaint(); });
  }

  ~X11EmbeddedView() override { destroy(); }

  bool supportsApi(std::string_view api) const noexcept override {
    return api == "x11";
  }

  bool create(std::string_view api, bool floating) override {
    if (floating || !supportsApi(api) || created_) return false;
    created_ = true;
    return true;
  }

  void destroy() noexcept override {
    if (xic_ != nullptr) {
      XDestroyIC(xic_);
      xic_ = nullptr;
    }
    if (xim_ != nullptr) {
      XCloseIM(xim_);
      xim_ = nullptr;
    }
    if (display_ != nullptr && gc_ != nullptr) {
      XFreeGC(display_, gc_);
      gc_ = nullptr;
    }
    if (display_ != nullptr && window_ != 0U) {
      XDestroyWindow(display_, window_);
      window_ = 0U;
    }
    if (display_ != nullptr) {
      XCloseDisplay(display_);
      display_ = nullptr;
    }
    textEngine_.reset();
    created_ = false;
    visible_ = false;
  }

  bool setScale(double scale) override {
    if (!std::isfinite(scale) || scale < 0.5 || scale > 4.0) return false;
    scale_ = scale;
    runtime_.resize(static_cast<double>(width_) / scale_,
                    static_cast<double>(height_) / scale_);
    requestRepaint();
    return true;
  }

  bool setSize(std::uint32_t width, std::uint32_t height) override {
    if (width < 640U || height < 420U || width > 8192U || height > 8192U) {
      return false;
    }
    width_ = width;
    height_ = height;
    if (!surface_.resize(width_, height_)) return false;
    if (display_ != nullptr && window_ != 0U) {
      XResizeWindow(display_, window_, width_, height_);
    }
    runtime_.resize(static_cast<double>(width_) / scale_,
                    static_cast<double>(height_) / scale_);
    requestRepaint();
    return true;
  }

  bool setParent(std::uintptr_t parent) override {
    if (!created_ || parent == 0U || display_ != nullptr) return false;
    static_cast<void>(XSetLocaleModifiers(""));
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) return false;
    screen_ = DefaultScreen(display_);
    visual_ = DefaultVisual(display_, screen_);
    depth_ = DefaultDepth(display_, screen_);
    window_ = XCreateSimpleWindow(display_, static_cast<Window>(parent), 0, 0,
                                  width_, height_, 0U,
                                  BlackPixel(display_, screen_),
                                  BlackPixel(display_, screen_));
    if (window_ == 0U) {
      destroy();
      return false;
    }
    XSelectInput(display_, window_, ExposureMask | StructureNotifyMask |
                                      KeyPressMask | ButtonPressMask |
                                      ButtonReleaseMask | PointerMotionMask |
                                      FocusChangeMask);
    gc_ = XCreateGC(display_, window_, 0UL, nullptr);
    if (gc_ == nullptr || !surface_.resize(width_, height_)) {
      destroy();
      return false;
    }
    xim_ = XOpenIM(display_, nullptr, nullptr, nullptr);
    if (xim_ != nullptr) {
      xic_ = XCreateIC(xim_, XNInputStyle,
                       XIMPreeditNothing | XIMStatusNothing,
                       XNClientWindow, window_, XNFocusWindow, window_, nullptr);
    }
    auto text = text::TextEngine::createSystem();
    if (text) textEngine_ = std::move(text.value());
    runtime_.resize(static_cast<double>(width_) / scale_,
                    static_cast<double>(height_) / scale_);
    requestRepaint();
    return true;
  }

  bool show() override {
    if (display_ == nullptr || window_ == 0U) return false;
    XMapWindow(display_, window_);
    XFlush(display_);
    visible_ = true;
    requestRepaint();
    return true;
  }

  bool hide() override {
    if (display_ == nullptr || window_ == 0U) return false;
    XUnmapWindow(display_, window_);
    XFlush(display_);
    visible_ = false;
    return true;
  }

  void onTimer() noexcept override {
    requestRepaint();
    if (display_ == nullptr || window_ == 0U) return;
    while (XPending(display_) > 0) {
      XEvent event{};
      XNextEvent(display_, &event);
      if (XFilterEvent(&event, window_) != False) continue;
      handle(event);
    }
    if (visible_ && repaint_.exchange(false, std::memory_order_acq_rel)) {
      paintAndPresent();
    }
  }

  void requestRepaint() noexcept override {
    repaint_.store(true, std::memory_order_release);
  }

  std::string_view apiName() const noexcept override { return "x11"; }

private:
  ui::Point logical(int x, int y) const noexcept {
    return ui::Point{static_cast<double>(x) / scale_,
                     static_cast<double>(y) / scale_};
  }

  int clickCount(const XButtonEvent& event) noexcept {
    const auto closeTime = lastClickTime_ != 0U &&
                           event.time - lastClickTime_ <= 350U;
    const auto closePoint = std::abs(event.x - lastClickX_) <= 4 &&
                            std::abs(event.y - lastClickY_) <= 4;
    if (closeTime && closePoint && event.button == lastClickButton_) {
      clicks_ = std::min(3, clicks_ + 1);
    } else {
      clicks_ = 1;
    }
    lastClickTime_ = event.time;
    lastClickX_ = event.x;
    lastClickY_ = event.y;
    lastClickButton_ = event.button;
    return clicks_;
  }

  void handleKey(XKeyEvent event) noexcept {
    KeySym symbol = NoSymbol;
    std::string utf8;
    if (xic_ != nullptr) {
      std::vector<char> buffer(128U, '\0');
      Status status = XLookupNone;
      auto count = Xutf8LookupString(xic_, &event, buffer.data(),
                                     static_cast<int>(buffer.size() - 1U),
                                     &symbol, &status);
      if (status == XBufferOverflow && count > 0) {
        buffer.assign(static_cast<std::size_t>(count) + 1U, '\0');
        count = Xutf8LookupString(xic_, &event, buffer.data(),
                                  static_cast<int>(buffer.size() - 1U),
                                  &symbol, &status);
      }
      if (count > 0 && (status == XLookupChars || status == XLookupBoth)) {
        utf8.assign(buffer.data(), static_cast<std::size_t>(count));
      }
    } else {
      char buffer[64]{};
      const auto count = XLookupString(&event, buffer,
                                       static_cast<int>(sizeof(buffer)),
                                       &symbol, nullptr);
      if (count > 0) utf8.assign(buffer, static_cast<std::size_t>(count));
    }
    if (runtime_.controller().textInputActive()) {
      if (symbol == XK_Escape) {
        runtime_.textCancel();
        return;
      }
      if (!utf8.empty()) {
        const auto decoded = domain::fromUtf8(utf8);
        if (decoded) runtime_.textCommit(decoded.value());
      }
      return;
    }
    static_cast<void>(runtime_.keyDown(native_ui::KeyEvent{
        .key = keyFor(symbol),
        .modifiers = modifiers(event.state),
        .repeat = false,
    }));
  }

  void handle(XEvent& event) noexcept {
    switch (event.type) {
      case Expose:
        requestRepaint();
        break;
      case ConfigureNotify: {
        const auto width = static_cast<std::uint32_t>(std::max(1, event.xconfigure.width));
        const auto height = static_cast<std::uint32_t>(std::max(1, event.xconfigure.height));
        if (width != width_ || height != height_) {
          width_ = width;
          height_ = height;
          if (surface_.resize(width_, height_)) {
            runtime_.resize(static_cast<double>(width_) / scale_,
                            static_cast<double>(height_) / scale_);
            requestRepaint();
          }
        }
        break;
      }
      case ButtonPress: {
        if (event.xbutton.button == Button4 || event.xbutton.button == Button5 ||
            event.xbutton.button == 6U || event.xbutton.button == 7U) {
          const auto vertical = event.xbutton.button == Button4 ? -72.0
                                : event.xbutton.button == Button5 ? 72.0 : 0.0;
          const auto horizontal = event.xbutton.button == 6U ? -72.0
                                  : event.xbutton.button == 7U ? 72.0 : 0.0;
          runtime_.scroll(horizontal, vertical,
                          logical(event.xbutton.x, event.xbutton.y),
                          modifiers(event.xbutton.state));
        } else {
          static_cast<void>(runtime_.pointerDown(native_ui::PointerEvent{
              .position = logical(event.xbutton.x, event.xbutton.y),
              .button = button(event.xbutton.button),
              .modifiers = modifiers(event.xbutton.state),
              .clickCount = clickCount(event.xbutton),
          }));
        }
        break;
      }
      case ButtonRelease:
        static_cast<void>(runtime_.pointerUp(native_ui::PointerEvent{
            .position = logical(event.xbutton.x, event.xbutton.y),
            .button = button(event.xbutton.button),
            .modifiers = modifiers(event.xbutton.state),
            .clickCount = clicks_,
        }));
        break;
      case MotionNotify:
        static_cast<void>(runtime_.pointerMove(native_ui::PointerEvent{
            .position = logical(event.xmotion.x, event.xmotion.y),
            .button = (event.xmotion.state & Button1Mask) != 0U
                          ? native_ui::PointerButton::Left
                          : native_ui::PointerButton::NoButton,
            .modifiers = modifiers(event.xmotion.state),
            .clickCount = 1,
        }));
        break;
      case KeyPress:
        handleKey(event.xkey);
        break;
      case FocusIn:
        if (xic_ != nullptr) XSetICFocus(xic_);
        break;
      case FocusOut:
        if (xic_ != nullptr) XUnsetICFocus(xic_);
        break;
      default:
        break;
    }
  }

  void paintAndPresent() noexcept {
    if (surface_.pixels().empty()) return;
    native_ui::RasterCanvas canvas{surface_, scale_, textEngine_.get()};
    runtime_.paint(canvas);
    const auto bytes = surface_.pixels().size() * sizeof(std::uint32_t);
    auto* data = static_cast<char*>(std::malloc(bytes));
    if (data == nullptr) return;
    std::memcpy(data, surface_.pixels().data(), bytes);
    auto* image = XCreateImage(display_, visual_, static_cast<unsigned int>(depth_),
                               ZPixmap, 0, data, width_, height_, 32, 0);
    if (image == nullptr) {
      std::free(data);
      return;
    }
    XPutImage(display_, window_, gc_, image, 0, 0, 0, 0, width_, height_);
    XFlush(display_);
    XDestroyImage(image);
  }

  EditorRuntime& runtime_;
  Display* display_{nullptr};
  int screen_{0};
  Visual* visual_{nullptr};
  int depth_{0};
  Window window_{0U};
  GC gc_{nullptr};
  XIM xim_{nullptr};
  XIC xic_{nullptr};
  native_ui::PixelSurface surface_;
  std::unique_ptr<text::TextEngine> textEngine_;
  std::uint32_t width_{960U};
  std::uint32_t height_{680U};
  double scale_{1.0};
  bool created_{false};
  bool visible_{false};
  std::atomic<bool> repaint_{true};
  Time lastClickTime_{0U};
  int lastClickX_{0};
  int lastClickY_{0};
  unsigned int lastClickButton_{0U};
  int clicks_{0};
};

}  // namespace

std::unique_ptr<IEmbeddedView> createEmbeddedView(EditorRuntime& runtime) {
  return std::make_unique<X11EmbeddedView>(runtime);
}

}  // namespace seam::clap_editor

#endif
