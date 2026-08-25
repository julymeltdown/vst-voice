#include "seam/native_ui/native_window.hpp"

#if defined(SEAM_NATIVE_X11)

#include "seam/domain/note.hpp"
#include "seam/text/text_engine.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace seam::native_ui {
namespace {

InputModifiers modifiersFromState(unsigned int state) noexcept {
  return InputModifiers{
      .shift = (state & ShiftMask) != 0U,
      .control = (state & ControlMask) != 0U,
      .alt = (state & Mod1Mask) != 0U,
      .command = (state & Mod4Mask) != 0U,
  };
}

PointerButton pointerButton(unsigned int button) noexcept {
  switch (button) {
    case Button1: return PointerButton::Left;
    case Button2: return PointerButton::Middle;
    case Button3: return PointerButton::Right;
    default: return PointerButton::NoButton;
  }
}

NativeKey nativeKey(KeySym key) noexcept {
  switch (key) {
    case XK_space: return NativeKey::Space;
    case XK_Return:
    case XK_KP_Enter: return NativeKey::Enter;
    case XK_Escape: return NativeKey::Escape;
    case XK_Delete: return NativeKey::Delete;
    case XK_BackSpace: return NativeKey::Backspace;
    case XK_Left: return NativeKey::Left;
    case XK_Right: return NativeKey::Right;
    case XK_Up: return NativeKey::Up;
    case XK_Down: return NativeKey::Down;
    case XK_z:
    case XK_Z: return NativeKey::Z;
    case XK_y:
    case XK_Y: return NativeKey::Y;
    case XK_c:
    case XK_C: return NativeKey::C;
    case XK_B: return NativeKey::B;
    case XK_e:
    case XK_E: return NativeKey::E;
    case XK_n:
    case XK_N: return NativeKey::N;
    case XK_o:
    case XK_O: return NativeKey::O;
    case XK_P: return NativeKey::P;
    case XK_q:
    case XK_Q: return NativeKey::Q;
    case XK_s:
    case XK_S: return NativeKey::S;
    case XK_r:
    case XK_R: return NativeKey::R;
    case XK_v:
    case XK_V: return NativeKey::V;
    case XK_i:
    case XK_I: return NativeKey::I;
    case XK_l:
    case XK_L: return NativeKey::L;
    case XK_plus:
    case XK_equal:
    case XK_KP_Add: return NativeKey::Plus;
    case XK_minus:
    case XK_KP_Subtract: return NativeKey::Minus;
    default: return NativeKey::Unknown;
  }
}

class X11NativeWindow final : public INativeWindow {
public:
  ~X11NativeWindow() override { close(); }

  core::Result<void> open(const NativeWindowConfig& config,
                          INativeWindowClient& client) override {
    if (display_ != nullptr) {
      return core::failure(core::ErrorCode::Conflict,
                           "X11 window is already open");
    }
    if (!nativeWindowConfigSizeIsValid(config)) {
      return core::failure(core::ErrorCode::InvalidArgument,
                           "Native window dimensions or scale are invalid");
    }

    static_cast<void>(XSetLocaleModifiers(""));
    display_ = XOpenDisplay(nullptr);
    if (display_ == nullptr) {
      return core::failure(core::ErrorCode::IoError,
                           "Unable to open the X11 display",
                           "Set DISPLAY or run under a desktop session");
    }
    config_ = config;
    client_ = &client;
    auto textEngine = text::TextEngine::createSystem();
    if (textEngine) textEngine_ = std::move(textEngine.value());
    scale_ = config.scale;
    screen_ = DefaultScreen(display_);
    visual_ = DefaultVisual(display_, screen_);
    depth_ = DefaultDepth(display_, screen_);
    window_ = XCreateSimpleWindow(
        display_, RootWindow(display_, screen_), 0, 0, config.width,
        config.height, 0, BlackPixel(display_, screen_),
        BlackPixel(display_, screen_));
    if (window_ == 0U) {
      close();
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create the X11 window");
    }
    XStoreName(display_, window_, config.title.c_str());
    XSizeHints sizeHints{};
    sizeHints.flags = PMinSize;
    sizeHints.min_width = static_cast<int>(
        nativeWindowMinimumPhysicalWidth(config));
    sizeHints.min_height = static_cast<int>(
        nativeWindowMinimumPhysicalHeight(config));
    XSetWMNormalHints(display_, window_, &sizeHints);
    XSelectInput(display_, window_, ExposureMask | StructureNotifyMask |
                                      KeyPressMask | ButtonPressMask |
                                      ButtonReleaseMask | PointerMotionMask |
                                      FocusChangeMask);
    wmDelete_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
    if (wmDelete_ != None) {
      XSetWMProtocols(display_, window_, &wmDelete_, 1);
    }
    gc_ = XCreateGC(display_, window_, 0, nullptr);
    if (gc_ == nullptr) {
      close();
      return core::failure(core::ErrorCode::IoError,
                           "Unable to create the X11 graphics context");
    }

    xim_ = XOpenIM(display_, nullptr, nullptr, nullptr);
    if (xim_ != nullptr) {
      xic_ = XCreateIC(xim_, XNInputStyle,
                       XIMPreeditNothing | XIMStatusNothing,
                       XNClientWindow, window_, XNFocusWindow, window_, nullptr);
    }

    const auto resized = surface_.resize(config.width, config.height);
    if (!resized) {
      close();
      return resized;
    }
    XMapWindow(display_, window_);
    XFlush(display_);
    client_->resized(static_cast<double>(config.width) / scale_,
                     static_cast<double>(config.height) / scale_, scale_);
    openedAt_ = std::chrono::steady_clock::now();
    repaintRequested_.store(true, std::memory_order_release);
    return core::success();
  }

  int run() override {
    if (display_ == nullptr || client_ == nullptr) return 1;
    bool closeRequested = false;
    auto nextFrame = std::chrono::steady_clock::now();
    while (!closeRequested && !client_->wantsClose()) {
      while (XPending(display_) > 0) {
        XEvent event{};
        XNextEvent(display_, &event);
        if (XFilterEvent(&event, window_) != False) continue;
        handleEvent(event, closeRequested);
      }
      const auto now = std::chrono::steady_clock::now();
      if (now >= nextFrame) {
        repaintRequested_.store(true, std::memory_order_release);
        nextFrame = now + std::chrono::milliseconds{16};
      }
      if (repaintRequested_.exchange(false, std::memory_order_acq_rel)) {
        paintAndPresent();
      }
      if (config_.autoCloseAfter.count() > 0 &&
          std::chrono::steady_clock::now() - openedAt_ >=
              config_.autoCloseAfter) {
        closeRequested = true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    if (repaintRequested_.exchange(false, std::memory_order_acq_rel)) {
      paintAndPresent();
    }
    if (config_.screenshotPath.has_value()) {
      const auto written = surface_.writePpm(*config_.screenshotPath);
      if (!written) return 2;
    }
    return 0;
  }

  void requestRepaint() noexcept override {
    repaintRequested_.store(true, std::memory_order_release);
  }

  void beginTextInput(const TextInputRequest& request) override {
    textInputActive_ = true;
    textRequest_ = request;
    textBuffer_ = request.currentText;
    textCursor_ = textBuffer_.size();
    if (xic_ != nullptr) XSetICFocus(xic_);
    requestRepaint();
  }

  void endTextInput() noexcept override {
    textInputActive_ = false;
    textRequest_.reset();
    textBuffer_.clear();
    textCursor_ = 0U;
    requestRepaint();
  }

  PixelSurface snapshot() const override { return surface_; }

  std::string backendName() const override {
    return "X11 software raster + XIM";
  }

private:
  void close() noexcept {
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
  }

  ui::Point logicalPoint(int x, int y) const noexcept {
    return ui::Point{static_cast<double>(x) / scale_,
                     static_cast<double>(y) / scale_};
  }

  int clickCount(const XButtonEvent& event) noexcept {
    const auto closeInTime = lastClickTime_ != 0U &&
                             event.time - lastClickTime_ <= 350U;
    const auto closeInSpace = std::abs(event.x - lastClickX_) <= 4 &&
                              std::abs(event.y - lastClickY_) <= 4;
    const auto sameButton = event.button == lastClickButton_;
    if (closeInTime && closeInSpace && sameButton) {
      clickCount_ = std::min(3, clickCount_ + 1);
    } else {
      clickCount_ = 1;
    }
    lastClickTime_ = event.time;
    lastClickX_ = event.x;
    lastClickY_ = event.y;
    lastClickButton_ = event.button;
    return clickCount_;
  }

  void handleEvent(XEvent& event, bool& closeRequested) noexcept {
    switch (event.type) {
      case Expose:
        requestRepaint();
        break;
      case ConfigureNotify: {
        const auto width = static_cast<std::uint32_t>(
            std::max(1, event.xconfigure.width));
        const auto height = static_cast<std::uint32_t>(
            std::max(1, event.xconfigure.height));
        if (width != surface_.width() || height != surface_.height()) {
          const auto resized = surface_.resize(width, height);
          if (resized && client_ != nullptr) {
            client_->resized(static_cast<double>(width) / scale_,
                             static_cast<double>(height) / scale_, scale_);
            requestRepaint();
          }
        }
        break;
      }
      case ButtonPress: {
        const auto modifiers = modifiersFromState(event.xbutton.state);
        if (event.xbutton.button == Button4 || event.xbutton.button == Button5 ||
            event.xbutton.button == 6U || event.xbutton.button == 7U) {
          const auto vertical = event.xbutton.button == Button4
                                    ? -72.0
                                    : event.xbutton.button == Button5 ? 72.0 : 0.0;
          const auto horizontal = event.xbutton.button == 6U
                                      ? -72.0
                                      : event.xbutton.button == 7U ? 72.0 : 0.0;
          client_->scroll(horizontal, vertical,
                          logicalPoint(event.xbutton.x, event.xbutton.y),
                          modifiers);
          break;
        }
        client_->pointerDown(PointerEvent{
            .position = logicalPoint(event.xbutton.x, event.xbutton.y),
            .button = pointerButton(event.xbutton.button),
            .modifiers = modifiers,
            .clickCount = clickCount(event.xbutton),
        });
        break;
      }
      case ButtonRelease:
        client_->pointerUp(PointerEvent{
            .position = logicalPoint(event.xbutton.x, event.xbutton.y),
            .button = pointerButton(event.xbutton.button),
            .modifiers = modifiersFromState(event.xbutton.state),
            .clickCount = clickCount_,
        });
        break;
      case MotionNotify:
        client_->pointerMove(PointerEvent{
            .position = logicalPoint(event.xmotion.x, event.xmotion.y),
            .button = (event.xmotion.state & Button1Mask) != 0U
                          ? PointerButton::Left
                          : PointerButton::NoButton,
            .modifiers = modifiersFromState(event.xmotion.state),
            .clickCount = 1,
        });
        break;
      case KeyPress:
        handleKeyPress(event.xkey);
        break;
      case FocusIn:
        if (xic_ != nullptr) XSetICFocus(xic_);
        break;
      case FocusOut:
        if (xic_ != nullptr) XUnsetICFocus(xic_);
        break;
      case ClientMessage:
        if (wmDelete_ != None &&
            static_cast<Atom>(event.xclient.data.l[0]) == wmDelete_) {
          closeRequested = client_ == nullptr || client_->requestClose();
        }
        break;
      default:
        break;
    }
  }

  std::pair<KeySym, std::string> lookupUtf8(XKeyEvent& event) noexcept {
    KeySym key = NoSymbol;
    if (xic_ == nullptr) {
      char buffer[64]{};
      const auto count = XLookupString(&event, buffer,
                                       static_cast<int>(sizeof(buffer)),
                                       &key, nullptr);
      return {key, std::string{buffer, buffer + std::max(0, count)}};
    }
    std::vector<char> buffer(128U, '\0');
    Status status = XLookupNone;
    auto count = Xutf8LookupString(xic_, &event, buffer.data(),
                                   static_cast<int>(buffer.size() - 1U),
                                   &key, &status);
    if (status == XBufferOverflow && count > 0) {
      buffer.assign(static_cast<std::size_t>(count) + 1U, '\0');
      count = Xutf8LookupString(xic_, &event, buffer.data(), count,
                                &key, &status);
    }
    if (count <= 0 || (status != XLookupChars && status != XLookupBoth)) {
      return {key, {}};
    }
    return {key, std::string{buffer.data(), static_cast<std::size_t>(count)}};
  }

  void updateComposition() noexcept {
    if (client_ == nullptr) return;
    client_->textComposition(textBuffer_,
                             ui::CompositionSelection{textCursor_, 0U});
    requestRepaint();
  }

  void handleTextKey(KeySym key, const std::string& utf8,
                     const InputModifiers& modifiers) noexcept {
    if (key == XK_Escape) {
      client_->textCancel();
      endTextInput();
      return;
    }
    if (key == XK_Return || key == XK_KP_Enter) {
      client_->textCommit(textBuffer_);
      endTextInput();
      return;
    }
    if (key == XK_BackSpace) {
      if (textCursor_ > 0U && !textBuffer_.empty()) {
        textBuffer_.erase(textCursor_ - 1U, 1U);
        --textCursor_;
        updateComposition();
      }
      return;
    }
    if (key == XK_Delete) {
      if (textCursor_ < textBuffer_.size()) {
        textBuffer_.erase(textCursor_, 1U);
        updateComposition();
      }
      return;
    }
    if (key == XK_Left) {
      if (textCursor_ > 0U) --textCursor_;
      updateComposition();
      return;
    }
    if (key == XK_Right) {
      if (textCursor_ < textBuffer_.size()) ++textCursor_;
      updateComposition();
      return;
    }
    if (modifiers.control || modifiers.alt || modifiers.command || utf8.empty()) {
      return;
    }
    const auto decoded = domain::fromUtf8(utf8);
    if (!decoded || decoded.value().empty()) return;
    textBuffer_.insert(textCursor_, decoded.value());
    textCursor_ += decoded.value().size();
    updateComposition();
  }

  void handleKeyPress(XKeyEvent& event) noexcept {
    auto [key, utf8] = lookupUtf8(event);
    const auto modifiers = modifiersFromState(event.state);
    if (textInputActive_) {
      handleTextKey(key, utf8, modifiers);
      return;
    }
    client_->keyDown(KeyEvent{
        .key = nativeKey(key),
        .modifiers = modifiers,
        .repeat = false,
    });
  }

  void paintAndPresent() noexcept {
    if (client_ == nullptr || surface_.pixels().empty()) return;
    RasterCanvas canvas{surface_, scale_, textEngine_.get()};
    client_->paint(canvas);

    const auto byteCount = surface_.pixels().size() * sizeof(std::uint32_t);
    auto* imageData = static_cast<char*>(std::malloc(byteCount));
    if (imageData == nullptr) return;
    std::memcpy(imageData, surface_.pixels().data(), byteCount);
    auto* image = XCreateImage(display_, visual_, static_cast<unsigned int>(depth_),
                               ZPixmap, 0, imageData, surface_.width(),
                               surface_.height(), 32, 0);
    if (image == nullptr) {
      std::free(imageData);
      return;
    }
    XPutImage(display_, window_, gc_, image, 0, 0, 0, 0,
              surface_.width(), surface_.height());
    XFlush(display_);
    XDestroyImage(image);
  }

  NativeWindowConfig config_;
  INativeWindowClient* client_{nullptr};
  Display* display_{nullptr};
  int screen_{0};
  Visual* visual_{nullptr};
  int depth_{0};
  Window window_{0U};
  GC gc_{nullptr};
  Atom wmDelete_{None};
  XIM xim_{nullptr};
  XIC xic_{nullptr};
  PixelSurface surface_;
  std::unique_ptr<text::TextEngine> textEngine_;
  double scale_{1.0};
  std::atomic<bool> repaintRequested_{false};
  std::chrono::steady_clock::time_point openedAt_{};
  bool textInputActive_{false};
  std::optional<TextInputRequest> textRequest_;
  std::u32string textBuffer_;
  std::size_t textCursor_{0U};
  Time lastClickTime_{0U};
  int lastClickX_{0};
  int lastClickY_{0};
  unsigned int lastClickButton_{0U};
  int clickCount_{0};
};

}  // namespace

std::unique_ptr<INativeWindow> createNativeWindow() {
  return std::make_unique<X11NativeWindow>();
}

}  // namespace seam::native_ui

#endif
