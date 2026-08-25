#include "seam/standalone/native_project_dialog.hpp"

#if defined(_WIN32)

#include "seam/native_ui/new_project_dialog.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seam::standalone {
namespace {

constexpr wchar_t kDialogClassName[] = L"ProjectSeamNewProjectDialog";

enum ControlId : int {
  kName = 1001,
  kTempo,
  kNumerator,
  kDenominator,
  kSampleRate,
  kChannels,
  kInitialTrack,
  kVoicebank,
  kChooseLocation,
  kCancel,
};

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
  return converted == size ? result : std::wstring{};
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
  return converted == size ? result : std::string{};
}

std::string suggestedProjectName(std::string_view suggested) {
  const auto fallback = suggested.empty() ? std::string{"Untitled"}
                                           : std::string{suggested};
  const auto path = std::filesystem::path{fallback};
  return path.extension() == ".seam" ? path.stem().string() : fallback;
}

std::wstring suggestedProjectFileName(std::string_view name) {
  const auto path = std::filesystem::path{name};
  const auto fileName = path.extension() == ".seam"
                            ? std::string{name}
                            : std::string{name} + ".seam";
  return wideFromUtf8(fileName);
}

HWND createControl(HWND parent, const wchar_t* className,
                   const wchar_t* text, DWORD style, int id, int x, int y,
                   int width, int height) {
  return CreateWindowExW(
      0, className, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
      parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
      GetModuleHandleW(nullptr), nullptr);
}

void setDefaultFont(HWND control) {
  if (control == nullptr) return;
  SendMessageW(control, WM_SETFONT,
               reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
               TRUE);
}

void addComboItem(HWND combo, std::wstring_view value, LPARAM data = 0) {
  const auto index = SendMessageW(combo, CB_ADDSTRING, 0,
                                  reinterpret_cast<LPARAM>(value.data()));
  if (index >= 0) {
    SendMessageW(combo, CB_SETITEMDATA, index, data);
  }
}

std::optional<std::wstring> controlText(HWND control) {
  if (control == nullptr) return std::nullopt;
  const auto length = GetWindowTextLengthW(control);
  if (length < 0 || length > 4096) return std::nullopt;
  std::wstring result(static_cast<std::size_t>(length) + 1U, L'\0');
  const auto copied = GetWindowTextW(control, result.data(), length + 1);
  if (copied < 0) return std::nullopt;
  result.resize(static_cast<std::size_t>(copied));
  return result;
}

std::optional<std::uint32_t> unsignedValue(HWND control) {
  const auto text = controlText(control);
  if (!text.has_value()) return std::nullopt;
  try {
    const auto parsed = std::stoul(*text);
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::uint32_t> selectedUnsigned(HWND combo) {
  const auto selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
  if (selected == CB_ERR) return std::nullopt;
  const auto length = SendMessageW(combo, CB_GETLBTEXTLEN, selected, 0);
  if (length <= 0 || length > 64) return std::nullopt;
  std::wstring value(static_cast<std::size_t>(length) + 1U, L'\0');
  SendMessageW(combo, CB_GETLBTEXT, selected,
               reinterpret_cast<LPARAM>(value.data()));
  value.resize(static_cast<std::size_t>(length));
  try {
    const auto parsed = std::stoul(value);
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
      return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
  } catch (...) {
    return std::nullopt;
  }
}

class Win32NativeNewProjectDialog final : public INativeNewProjectDialog {
public:
  core::Result<std::optional<authoring::NewProjectRequest>> choose(
      NativeNewProjectDialogConfig config) override {
    config_ = std::move(config);
    done_ = false;
    window_ = nullptr;
    result_ = core::success(std::optional<authoring::NewProjectRequest>{});
    model_ = native_ui::NewProjectDialogModel{config_.candidates};
    const auto initialName = suggestedProjectName(config_.suggestedName);
    model_.setName(initialName);
    model_.setTempoBpm(120.0);
    model_.setMeter(4U, 4U);
    model_.setSampleRate(config_.sampleRate);
    model_.setOutputChannels(config_.outputChannels);

    owner_ = GetActiveWindow();
    if (owner_ == nullptr) owner_ = GetForegroundWindow();
    if (owner_ != nullptr &&
        GetCurrentThreadId() != GetWindowThreadProcessId(owner_, nullptr)) {
      owner_ = nullptr;
    }
    if (owner_ == window_) owner_ = nullptr;
    if (owner_ != nullptr) EnableWindow(owner_, FALSE);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &Win32NativeNewProjectDialog::windowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kDialogClassName;
    const auto registered = RegisterClassExW(&windowClass);
    if (registered == 0U && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      restoreOwner();
      return core::failure<std::optional<authoring::NewProjectRequest>>(
          core::ErrorCode::IoError, "Unable to register the New Project form");
    }

    window_ = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kDialogClassName,
        L"Create New Project", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 510, owner_, nullptr,
        GetModuleHandleW(nullptr), this);
    if (window_ == nullptr) {
      restoreOwner();
      return core::failure<std::optional<authoring::NewProjectRequest>>(
          core::ErrorCode::IoError, "Unable to create the New Project form");
    }

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    SetForegroundWindow(window_);
    MSG message{};
    while (!done_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
      if (!IsDialogMessageW(window_, &message)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    if (window_ != nullptr) DestroyWindow(window_);
    window_ = nullptr;
    restoreOwner();
    return std::move(result_);
  }

private:
  static LRESULT CALLBACK windowProcedure(HWND window, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32NativeNewProjectDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
      self = static_cast<Win32NativeNewProjectDialog*>(create->lpCreateParams);
      self->window_ = window;
      SetWindowLongPtrW(window, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(self));
    }
    if (self != nullptr) return self->handleMessage(message, wParam, lParam);
    return DefWindowProcW(window, message, wParam, lParam);
  }

  LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
      case WM_CREATE:
        createControls();
        return 0;
      case WM_COMMAND:
        if (LOWORD(wParam) == kInitialTrack &&
            HIWORD(wParam) == BN_CLICKED) {
          const auto enabled = SendMessageW(
              track_, BM_GETCHECK, 0, 0) == BST_CHECKED;
          EnableWindow(bank_, enabled ? TRUE : FALSE);
          if (!enabled) SendMessageW(bank_, CB_SETCURSEL, 0, 0);
          return 0;
        }
        if (LOWORD(wParam) == kChooseLocation && HIWORD(wParam) == BN_CLICKED) {
          submitFromControls();
          return 0;
        }
        if (LOWORD(wParam) == kCancel && HIWORD(wParam) == BN_CLICKED) {
          finish(std::optional<authoring::NewProjectRequest>{});
          return 0;
        }
        break;
      case WM_CLOSE:
        finish(std::optional<authoring::NewProjectRequest>{});
        return 0;
      case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        window_ = nullptr;
        return 0;
      default:
        break;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
  }

  void createControls() {
    const auto font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const auto addLabel = [&](const wchar_t* text, int y) {
      const auto label = createControl(window_, L"STATIC", text, 0, 0, 24, y,
                                       190, 24);
      SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    };
    const auto addCombo = [&](int id, int x, int y, int width) {
      const auto combo = createControl(
          window_, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, id, x, y,
          width, 180);
      setDefaultFont(combo);
      return combo;
    };
    addLabel(L"Project name", 24);
    name_ = createControl(window_, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL |
                                                  WS_TABSTOP,
                          kName, 220, 20, 330, 28);
    SetWindowTextW(name_, wideFromUtf8(suggestedProjectName(config_.suggestedName)).c_str());
    setDefaultFont(name_);
    addLabel(L"Tempo (BPM)", 64);
    tempo_ = createControl(window_, L"EDIT", L"120", WS_BORDER | ES_AUTOHSCROLL |
                                                     WS_TABSTOP,
                           kTempo, 220, 60, 110, 28);
    setDefaultFont(tempo_);

    addLabel(L"Time signature", 104);
    numerator_ = addCombo(kNumerator, 220, 100, 95);
    for (const auto value : {L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L"12"}) {
      addComboItem(numerator_, value);
    }
    SendMessageW(numerator_, CB_SETCURSEL, 2, 0);
    denominator_ = addCombo(kDenominator, 325, 100, 95);
    for (const auto value : {L"1", L"2", L"4", L"8", L"16", L"32"}) {
      addComboItem(denominator_, value);
    }
    SendMessageW(denominator_, CB_SETCURSEL, 2, 0);

    addLabel(L"Sample rate", 144);
    sampleRate_ = addCombo(kSampleRate, 220, 140, 145);
    for (const auto value : {L"44100", L"48000", L"96000"}) {
      addComboItem(sampleRate_, value);
    }
    const auto sampleRateIndex = config_.sampleRate == 44100U
                                     ? 0
                                     : config_.sampleRate == 96000U ? 2 : 1;
    SendMessageW(sampleRate_, CB_SETCURSEL, sampleRateIndex, 0);

    addLabel(L"Output channels", 184);
    channels_ = addCombo(kChannels, 220, 180, 115);
    for (const auto value : {L"1", L"2", L"4", L"8"}) {
      addComboItem(channels_, value);
    }
    const auto channelIndex = config_.outputChannels == 1U
                                   ? 0
                                   : config_.outputChannels == 4U
                                         ? 2
                                         : config_.outputChannels == 8U ? 3 : 1;
    SendMessageW(channels_, CB_SETCURSEL, channelIndex, 0);

    track_ = createControl(window_, L"BUTTON", L"Create initial vocal track",
                           BS_AUTOCHECKBOX | WS_TABSTOP, kInitialTrack, 24, 230,
                           310, 28);
    SendMessageW(track_, BM_SETCHECK, BST_CHECKED, 0);
    setDefaultFont(track_);
    addLabel(L"Initial Voicebank", 274);
    bank_ = addCombo(kVoicebank, 220, 270, 330);
    addComboItem(bank_, L"No Voicebank");
    for (std::size_t index = 0; index < config_.candidates.size(); ++index) {
      const auto& candidate = config_.candidates[index];
      const auto display = candidate.manifest.displayName + " / " +
                           candidate.manifest.id + " " + candidate.manifest.version;
      const auto title = wideFromUtf8(display);
      if (title.empty()) {
        addComboItem(bank_, L"Voicebank", static_cast<LPARAM>(index));
      } else {
        addComboItem(bank_, title, static_cast<LPARAM>(index));
      }
    }
    SendMessageW(bank_, CB_SETCURSEL, 0, 0);
    SendMessageW(bank_, CB_SETDROPPEDWIDTH, 620, 0);

    choose_ = createControl(window_, L"BUTTON", L"Choose Location...",
                            BS_DEFPUSHBUTTON | WS_TABSTOP, kChooseLocation, 220,
                            330, 190, 32);
    cancel_ = createControl(window_, L"BUTTON", L"Cancel", BS_PUSHBUTTON |
                                                          WS_TABSTOP,
                            kCancel, 420, 330, 130, 32);
    setDefaultFont(choose_);
    setDefaultFont(cancel_);
    SetFocus(name_);
    SendMessageW(name_, EM_SETSEL, 0, -1);
  }

  void submitFromControls() {
    const auto name = controlText(name_);
    const auto tempo = controlText(tempo_);
    const auto numerator = selectedUnsigned(numerator_);
    const auto denominator = selectedUnsigned(denominator_);
    const auto sampleRate = selectedUnsigned(sampleRate_);
    const auto channels = selectedUnsigned(channels_);
    if (!name.has_value() || !tempo.has_value() || !numerator.has_value() ||
        !denominator.has_value() || !sampleRate.has_value() ||
        !channels.has_value()) {
      return;
    }
    double tempoBpm = 0.0;
    try {
      tempoBpm = std::stod(*tempo);
    } catch (...) {
      return;
    }
    if (!std::isfinite(tempoBpm)) return;
    model_.setName(utf8FromWide(*name));
    model_.setTempoBpm(tempoBpm);
    model_.setMeter(static_cast<std::uint8_t>(*numerator),
                    static_cast<std::uint8_t>(*denominator));
    model_.setSampleRate(*sampleRate);
    model_.setOutputChannels(static_cast<std::uint8_t>(*channels));
    const auto createTrack = SendMessageW(track_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    model_.setCreateInitialVocalTrack(createTrack);
    if (createTrack) {
      const auto selected = SendMessageW(bank_, CB_GETCURSEL, 0, 0);
      if (selected > 0) {
        const auto candidate = model_.selectVoicebank(
            static_cast<std::size_t>(selected - 1));
        if (!candidate) return;
      }
    }

    std::array<wchar_t, 4096> fileName{};
    const auto nameUtf8 = utf8FromWide(*name);
    const auto initial = suggestedProjectFileName(
        nameUtf8.empty() ? std::string_view{"Untitled"}
                         : std::string_view{nameUtf8});
    const auto copyLength = std::min(initial.size(), fileName.size() - 1U);
    std::copy_n(initial.begin(), copyLength, fileName.begin());
    fileName[copyLength] = L'\0';
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Project SEAM (*.seam)\0*.seam\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrDefExt = L"seam";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT |
                   OFN_NOCHANGEDIR;
    std::wstring initialDirectory;
    if (!config_.initialDirectory.empty()) {
      initialDirectory = config_.initialDirectory.wstring();
      dialog.lpstrInitialDir = initialDirectory.c_str();
    }
    if (!GetSaveFileNameW(&dialog)) {
      if (CommDlgExtendedError() != 0U) {
        finishError(core::ErrorCode::IoError,
                    "Unable to open the project save dialog");
      }
      return;
    }
    const auto path = utf8FromWide(fileName.data());
    if (path.empty()) {
      finishError(core::ErrorCode::IoError,
                  "Unable to resolve project location");
      return;
    }
    model_.setProjectPath(std::filesystem::path{path});
    auto submitted = model_.submit();
    if (!submitted) {
      finishError(submitted.error());
      return;
    }
    finish(std::optional<authoring::NewProjectRequest>{
        std::move(submitted).value()});
  }

  void finish(std::optional<authoring::NewProjectRequest> request) {
    if (done_) return;
    result_ = core::success(std::move(request));
    done_ = true;
    if (window_ != nullptr) DestroyWindow(window_);
  }

  void finishError(core::Error error) {
    if (done_) return;
    result_ = core::Result<std::optional<authoring::NewProjectRequest>>{
        std::move(error)};
    done_ = true;
    if (window_ != nullptr) DestroyWindow(window_);
  }

  void finishError(core::ErrorCode code, std::string message) {
    finishError(core::Error{code, std::move(message), {}});
  }

  void restoreOwner() noexcept {
    if (owner_ != nullptr) {
      EnableWindow(owner_, TRUE);
      SetForegroundWindow(owner_);
    }
    owner_ = nullptr;
  }

  NativeNewProjectDialogConfig config_;
  native_ui::NewProjectDialogModel model_{};
  HWND window_{nullptr};
  HWND owner_{nullptr};
  HWND name_{nullptr};
  HWND tempo_{nullptr};
  HWND numerator_{nullptr};
  HWND denominator_{nullptr};
  HWND sampleRate_{nullptr};
  HWND channels_{nullptr};
  HWND track_{nullptr};
  HWND bank_{nullptr};
  HWND choose_{nullptr};
  HWND cancel_{nullptr};
  bool done_{false};
  core::Result<std::optional<authoring::NewProjectRequest>> result_{
      std::optional<authoring::NewProjectRequest>{}};
};

}

std::unique_ptr<INativeNewProjectDialog> createNativeNewProjectDialog() {
  return std::make_unique<Win32NativeNewProjectDialog>();
}

}

#endif
