#include "seam/platform/file_dialog.hpp"

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace seam::platform {
namespace {

using Microsoft::WRL::ComPtr;

std::wstring wide(std::string_view text) {
  if (text.empty()) return {};
  const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
  if (length <= 0) return {};
  std::wstring result(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), result.data(), length);
  return result;
}

std::vector<COMDLG_FILTERSPEC> filters(
    const std::vector<std::string>& extensions,
    std::vector<std::wstring>& storage) {
  std::vector<COMDLG_FILTERSPEC> result;
  storage.clear();
  storage.reserve(extensions.size() * 2U);
  for (const auto& extension : extensions) {
    storage.push_back(L"*." + wide(extension));
    storage.push_back(wide(extension) + L" files");
    result.push_back(COMDLG_FILTERSPEC{storage[storage.size() - 1U].c_str(),
                                      storage[storage.size() - 2U].c_str()});
  }
  return result;
}

core::Result<std::optional<std::filesystem::path>> resultPath(
    ::IFileDialog* dialog) {
  ComPtr<IShellItem> item;
  if (FAILED(dialog->GetResult(&item))) {
    return core::failure<std::optional<std::filesystem::path>>(
        core::ErrorCode::IoError, "Unable to read the selected Windows path");
  }
  PWSTR value = nullptr;
  const auto status = item->GetDisplayName(SIGDN_FILESYSPATH, &value);
  if (FAILED(status) || value == nullptr) {
    if (value != nullptr) CoTaskMemFree(value);
    return core::failure<std::optional<std::filesystem::path>>(
        core::ErrorCode::IoError, "Unable to resolve the selected Windows path");
  }
  std::filesystem::path path{value};
  CoTaskMemFree(value);
  return std::optional<std::filesystem::path>{std::move(path)};
}

std::string utf8(std::wstring_view value) {
  if (value.empty()) return {};
  const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (count <= 0) return {};
  std::string result(static_cast<std::size_t>(count), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) != count) return {};
  return result;
}

struct DraftIdentityModal final {
  std::array<HWND, 5U> fields{};
  std::optional<SampleManifestDraftIdentityInput> result;
  std::string error;
};

INT_PTR CALLBACK draftIdentityProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* state = reinterpret_cast<DraftIdentityModal*>(GetWindowLongPtrW(window, DWLP_USER));
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<DraftIdentityModal*>(lParam);
    SetWindowLongPtrW(window, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
    SetWindowTextW(window, L"Create editable sample draft — unreviewed estimates");
    const auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height) {
      RECT bounds{x,y,x+width,y+height}; MapDialogRect(window, &bounds);
      auto child = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, bounds.left, bounds.top,
          bounds.right-bounds.left, bounds.bottom-bounds.top, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
      if (child) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
      return child;
    };
    control(L"STATIC", L"All fields are required. Source/approval state stays unchanged.", 0, 0, 10, 8, 284, 14);
    const std::array<const wchar_t*, 5U> labels{L"Bank ID", L"Version", L"Display name", L"Language", L"Style"};
    for (std::size_t i=0U; i<labels.size(); ++i) {
      const auto y = 28 + static_cast<int>(i)*24;
      control(L"STATIC", labels[i], 0, 0, 10,y+3,70,14);
      state->fields[i] = control(i == 3U ? L"COMBOBOX" : L"EDIT", L"",
          WS_TABSTOP | (i == 3U ? static_cast<DWORD>(CBS_DROPDOWNLIST | WS_VSCROLL) : static_cast<DWORD>(WS_BORDER | ES_AUTOHSCROLL)),
          101+static_cast<int>(i), 82,y,210,i == 3U ? 100 : 18);
      if (!state->fields[i]) { state->error = "Cannot create draft identity input controls"; EndDialog(window, -1); return TRUE; }
      if (i != 3U) SendMessageW(state->fields[i], EM_SETLIMITTEXT, 256U, 0);
    }
    for (const auto* label : {L"Choose language (required)", L"Japanese (ja)", L"English (en)", L"Korean (ko)"})
      SendMessageW(state->fields[3], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendMessageW(state->fields[3], CB_SETCURSEL, 0, 0);
    control(L"BUTTON", L"Cancel", WS_TABSTOP | BS_DEFPUSHBUTTON, IDCANCEL, 92,158,66,20);
    control(L"BUTTON", L"Choose New Destination", WS_TABSTOP | BS_PUSHBUTTON, IDOK, 166,158,126,20);
    SendMessageW(window, DM_SETDEFID, IDCANCEL, 0);
    SetFocus(state->fields[0]); return FALSE;
  }
  if (message == WM_CLOSE) { EndDialog(window, IDCANCEL); return TRUE; }
  if (message != WM_COMMAND || !state) return FALSE;
  if (LOWORD(wParam) == IDCANCEL) { EndDialog(window, IDCANCEL); return TRUE; }
  if (LOWORD(wParam) != IDOK) return FALSE;
  std::array<std::string, 5U> text;
  for (std::size_t i=0U; i<text.size(); ++i) {
    if (i == 3U) {
      const auto index = SendMessageW(state->fields[i], CB_GETCURSEL, 0, 0);
      text[i] = index == 1 ? "ja" : index == 2 ? "en" : index == 3 ? "ko" : "";
      continue;
    }
    const auto length = GetWindowTextLengthW(state->fields[i]);
    if (length < 0 || length > 256) { state->error = "Draft identity field is oversized"; EndDialog(window, -1); return TRUE; }
    std::wstring value(static_cast<std::size_t>(length)+1U, L'\0');
    const auto copied = GetWindowTextW(state->fields[i], value.data(), length+1);
    value.resize(static_cast<std::size_t>(std::max(0, copied))); text[i] = utf8(value);
  }
  state->result = SampleManifestDraftIdentityInput{text[0],text[1],text[2],text[3],text[4]};
  EndDialog(window, IDOK); return TRUE;
}

struct SourceQualityModal final {
  std::wstring summary;
  std::vector<std::string> reviewers;
  std::array<HWND,4U> fields{};
  std::optional<SourceQualityDecisionInput> result;
  std::string error;
};

INT_PTR CALLBACK sourceQualityProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* state = reinterpret_cast<SourceQualityModal*>(GetWindowLongPtrW(window,DWLP_USER));
  if (message == WM_INITDIALOG) {
    state = reinterpret_cast<SourceQualityModal*>(lParam);
    SetWindowLongPtrW(window,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
    SetWindowTextW(window,L"Record source quality — no source rights or unit approval granted");
    const auto control = [&](const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int y,int width,int height) {
      RECT bounds{x,y,x+width,y+height}; MapDialogRect(window,&bounds);
      auto child = CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
          window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
      if (child) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
      return child;
    };
    const auto summary = control(L"EDIT",state->summary.c_str(),WS_TABSTOP|WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,110,10,8,306,64);
    if (!summary) { state->error="Cannot create captured source context"; EndDialog(window,-1); return TRUE; }
    const std::array<const wchar_t*,4U> labels{L"Assessment ID",L"Actual reviewer",L"Coverage",L"Listening"};
    for (std::size_t i=0U;i<labels.size();++i) {
      const auto y = 80+static_cast<int>(i)*24;
      control(L"STATIC",labels[i],0,0,10,y+3,84,14);
      state->fields[i]=control(i==0U?L"EDIT":L"COMBOBOX",L"",WS_TABSTOP|
          (i==0U?static_cast<DWORD>(WS_BORDER|ES_AUTOHSCROLL):static_cast<DWORD>(CBS_DROPDOWNLIST|WS_VSCROLL)),
          101+static_cast<int>(i),96,y,220,i==0U?18:100);
      if (!state->fields[i]) { state->error="Cannot create source decision fields"; EndDialog(window,-1); return TRUE; }
    }
    SendMessageW(state->fields[0],EM_SETLIMITTEXT,128U,0);
    SendMessageW(state->fields[1],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Choose your reviewer identity"));
    for (const auto& reviewer : state->reviewers) { const auto name=wide(reviewer); SendMessageW(state->fields[1],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str())); }
    for (std::size_t i=2U;i<4U;++i) for (const auto* text : {L"Not assessed",L"Pass",L"Blocked"})
      SendMessageW(state->fields[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
    for (std::size_t i=1U;i<4U;++i) SendMessageW(state->fields[i],CB_SETCURSEL,0,0);
    control(L"BUTTON",L"Cancel",WS_TABSTOP|BS_DEFPUSHBUTTON,IDCANCEL,160,184,70,20);
    control(L"BUTTON",L"Record Decision",WS_TABSTOP|BS_PUSHBUTTON,IDOK,236,184,80,20);
    SendMessageW(window,DM_SETDEFID,IDCANCEL,0); SetFocus(state->fields[0]); return FALSE;
  }
  if (message==WM_CLOSE) { EndDialog(window,IDCANCEL); return TRUE; }
  if (message!=WM_COMMAND || !state) return FALSE;
  if (LOWORD(wParam)==IDCANCEL) { EndDialog(window,IDCANCEL); return TRUE; }
  if (LOWORD(wParam)!=IDOK) return FALSE;
  const auto length=GetWindowTextLengthW(state->fields[0]);
  const auto reviewer=SendMessageW(state->fields[1],CB_GETCURSEL,0,0);
  const auto coverage=SendMessageW(state->fields[2],CB_GETCURSEL,0,0), listening=SendMessageW(state->fields[3],CB_GETCURSEL,0,0);
  if (length<=0 || length>128 || reviewer<=0 || static_cast<std::size_t>(reviewer)>state->reviewers.size() ||
      coverage<0 || coverage>2 || listening<0 || listening>2) {
    state->error="Provide an assessment ID, your registered reviewer and explicit outcomes"; EndDialog(window,-1); return TRUE;
  }
  std::wstring id(static_cast<std::size_t>(length)+1U,L'\0');
  const auto copied=GetWindowTextW(state->fields[0],id.data(),length+1); id.resize(static_cast<std::size_t>(std::max(0,copied)));
  const auto outcome=[](LRESULT index) { return index==1?"pass":index==2?"blocked":"not-assessed"; };
  state->result=SourceQualityDecisionInput{utf8(id),state->reviewers[static_cast<std::size_t>(reviewer-1)],outcome(coverage),outcome(listening)};
  EndDialog(window,IDOK); return TRUE;
}

struct SourceRegistrationModal final {
  std::wstring summary;
  std::array<HWND,7U> fields{};
  std::optional<SourceRegistrationInput> result;
  std::string error;
};

INT_PTR CALLBACK sourceRegistrationProcedure(HWND window,UINT message,WPARAM wParam,LPARAM lParam) {
  auto* state = reinterpret_cast<SourceRegistrationModal*>(GetWindowLongPtrW(window,DWLP_USER));
  if (message==WM_INITDIALOG) {
    state=reinterpret_cast<SourceRegistrationModal*>(lParam); SetWindowLongPtrW(window,DWLP_USER,reinterpret_cast<LONG_PTR>(state));
    SetWindowTextW(window,L"Register NEW source — declaration, not legal or musical verification");
    const auto control = [&](const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int y,int width,int height) {
      RECT bounds{x,y,x+width,y+height}; MapDialogRect(window,&bounds);
      const auto child=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,
          window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
      if (child) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
      return child;
    };
    if (!control(L"EDIT",state->summary.c_str(),WS_TABSTOP|WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,110,10,8,350,64)) {
      state->error="Cannot create captured license context"; EndDialog(window,-1); return TRUE;
    }
    const std::array<const wchar_t*,7U> labels{L"New source ID",L"Source kind",L"Declared rights",L"Source use",L"Transformation",L"Bank redistribution",L"Commercial renders"};
    for (std::size_t i=0U;i<labels.size();++i) {
      const auto y=80+static_cast<int>(i)*24;
      control(L"STATIC",labels[i],0,0,10,y+3,110,14);
      state->fields[i]=control(i==0U?L"EDIT":L"COMBOBOX",L"",WS_TABSTOP|
          (i==0U?static_cast<DWORD>(WS_BORDER|ES_AUTOHSCROLL):static_cast<DWORD>(CBS_DROPDOWNLIST|WS_VSCROLL)),
          101+static_cast<int>(i),124,y,236,i==0U?18:100);
      if (!state->fields[i]) { state->error="Cannot create source declaration fields"; EndDialog(window,-1); return TRUE; }
      if (i==0U) { SendMessageW(state->fields[i],EM_SETLIMITTEXT,128U,0); continue; }
      const auto add = [&](const wchar_t* text) { SendMessageW(state->fields[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text)); };
      add(L"Choose explicitly — required");
      if (i==1U) { add(L"Human recording"); add(L"Procedural synthesis"); add(L"TTS-derived"); }
      else if (i==2U) { add(L"Not assessed"); add(L"Pass (producer declaration)"); add(L"Blocked"); }
      else { add(L"No"); add(L"Yes"); }
      SendMessageW(state->fields[i],CB_SETCURSEL,0,0);
    }
    control(L"BUTTON",L"Cancel",WS_TABSTOP|BS_DEFPUSHBUTTON,IDCANCEL,198,254,70,20);
    control(L"BUTTON",L"Register Source",WS_TABSTOP|BS_PUSHBUTTON,IDOK,274,254,86,20);
    SendMessageW(window,DM_SETDEFID,IDCANCEL,0); SetFocus(state->fields[0]); return FALSE;
  }
  if (message==WM_CLOSE) { EndDialog(window,IDCANCEL); return TRUE; }
  if (message!=WM_COMMAND || !state) return FALSE;
  if (LOWORD(wParam)==IDCANCEL) { EndDialog(window,IDCANCEL); return TRUE; }
  if (LOWORD(wParam)!=IDOK) return FALSE;
  const auto length=GetWindowTextLengthW(state->fields[0]);
  std::array<LRESULT,6U> chosen{};
  bool valid=length>0 && length<=128;
  for (std::size_t i=0U;i<chosen.size();++i) {
    chosen[i]=SendMessageW(state->fields[i+1U],CB_GETCURSEL,0,0);
    valid=valid && chosen[i]>0 && chosen[i]<=(i<2U?3:2);
  }
  if (!valid) { state->error="Explicitly enter source ID, kind, rights and all four permissions"; EndDialog(window,-1); return TRUE; }
  std::wstring id(static_cast<std::size_t>(length)+1U,L'\0');
  const auto copied=GetWindowTextW(state->fields[0],id.data(),length+1); id.resize(static_cast<std::size_t>(std::max(0,copied)));
  SourceRegistrationInput result{utf8(id),chosen[0]==1?"human":chosen[0]==2?"procedural":"tts",
      chosen[1]==1?"not-assessed":chosen[1]==2?"pass":"blocked",{}};
  for (std::size_t i=0U;i<4U;++i) result.permissions[i]=chosen[i+2U]==1?"no":"yes";
  state->result=std::move(result); EndDialog(window,IDOK); return TRUE;
}

class Win32FileDialog final : public IFileDialog {
public:
  core::Result<std::optional<ProductionWorkspaceInput>> chooseProductionWorkspace() override {
    using Output=std::optional<ProductionWorkspaceInput>;
    const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE);
    if (FAILED(initialized)) return core::failure<Output>(core::ErrorCode::Unsupported,"Workspace picker requires a Windows UI apartment");
    struct Apartment final { ~Apartment() { CoUninitialize(); } } apartment;
    ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&dialog))))
      return core::failure<Output>(core::ErrorCode::IoError,"Cannot create workspace folder picker");
    DWORD options=0;
    if (FAILED(dialog->GetOptions(&options)) || FAILED(dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST)) ||
        FAILED(dialog->SetTitle(L"Open Existing Producer Workspace")))
      return core::failure<Output>(core::ErrorCode::IoError,"Cannot configure workspace folder picker");
    ComPtr<IFileDialogCustomize> fields;
    if (FAILED(dialog.As(&fields)) ||
        FAILED(fields->AddText(100U,L"Expected inventory SHA-256 (64 lowercase hex characters)")) ||
        FAILED(fields->AddEditBox(101U,L"")) ||
        FAILED(fields->AddText(102U,L"Existing registered PRODUCER operator ID")) ||
        FAILED(fields->AddEditBox(103U,L"")) ||
        FAILED(fields->AddText(104U,L"Opening does not register an operator, approve sources, or publish changes.")))
      return core::failure<Output>(core::ErrorCode::IoError,"Cannot create workspace context fields");
    const auto shown=dialog->Show(GetActiveWindow());
    if (shown==HRESULT_FROM_WIN32(ERROR_CANCELLED)) return Output{};
    if (FAILED(shown)) return core::failure<Output>(core::ErrorCode::IoError,"Workspace selection failed");
    const auto root=resultPath(dialog.Get());
    if (!root) return core::Result<Output>{root.error()};
    if (!root.value()) return Output{};
    struct Text final { PWSTR value{nullptr}; ~Text() { if (value) CoTaskMemFree(value); } } digest,identity;
    if (FAILED(fields->GetEditBoxText(101U,&digest.value)) || FAILED(fields->GetEditBoxText(103U,&identity.value)) ||
        digest.value==nullptr || identity.value==nullptr)
      return core::failure<Output>(core::ErrorCode::IoError,"Cannot read workspace context fields");
    // Bound the native strings before conversion; shared validation also checks
    // the UTF-8 byte lengths and canonical digest spelling.
    std::size_t digestLength=0U,identityLength=0U;
    while (digestLength<=64U && digest.value[digestLength]!=L'\0') ++digestLength;
    while (identityLength<=128U && identity.value[identityLength]!=L'\0') ++identityLength;
    if (digestLength!=64U || identityLength==0U || identityLength>128U)
      return core::failure<Output>(core::ErrorCode::InvalidArgument,"Workspace digest or operator ID exceeds bounds");
    ProductionWorkspaceInput input{*root.value(),utf8({digest.value,digestLength}),utf8({identity.value,identityLength})};
    const auto valid=input.validate(); if (!valid) return core::Result<Output>{valid.error()};
    return Output{std::move(input)};
  }
  core::Result<std::optional<SourceRegistrationInput>> chooseSourceRegistration(std::string_view summary) override {
    using Output=std::optional<SourceRegistrationInput>;
    if (summary.empty() || summary.size()>16384U || wide(summary).empty())
      return core::failure<Output>(core::ErrorCode::InvalidArgument,"Source registration context is invalid or oversized");
    struct EmptyTemplate final { DLGTEMPLATE header; WORD menu,windowClass,title; };
    EmptyTemplate model{}; model.header.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER;
    model.header.cx=370; model.header.cy=284;
    SourceRegistrationModal state;
    for (const auto c:wide(summary)) { if (c==L'\n') state.summary+=L'\r'; state.summary+=c; }
    const auto shown=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&model.header,GetActiveWindow(),sourceRegistrationProcedure,reinterpret_cast<LPARAM>(&state));
    if (!state.error.empty()) return core::failure<Output>(core::ErrorCode::InvalidArgument,state.error);
    if (shown==-1) return core::failure<Output>(core::ErrorCode::IoError,"Cannot open source registration form");
    return state.result;
  }
  core::Result<std::optional<SourceQualityDecisionInput>> chooseSourceQualityDecision(
      std::string_view summary,const std::vector<std::string>& reviewers) override {
    using Output=std::optional<SourceQualityDecisionInput>;
    if (summary.size()>16384U || reviewers.empty() || reviewers.size()>256U ||
        std::any_of(reviewers.begin(),reviewers.end(),[](const auto& value) { return value.size()>256U || wide(value).empty(); }))
      return core::failure<Output>(core::ErrorCode::InvalidArgument,"Source decision context or reviewers are unavailable or oversized");
    struct EmptyTemplate final { DLGTEMPLATE header; WORD menu,windowClass,title; };
    alignas(DWORD) const EmptyTemplate model{{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER,0,0,0,0,326,216},0,0,0};
    SourceQualityModal state; state.summary=wide(summary); state.reviewers=reviewers;
    const auto shown=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),&model.header,GetActiveWindow(),sourceQualityProcedure,reinterpret_cast<LPARAM>(&state));
    if (shown==-1) return core::failure<Output>(core::ErrorCode::IoError,state.error.empty()?"Cannot display source quality decision":state.error);
    return shown==IDOK?std::move(state.result):Output{};
  }
  core::Result<std::optional<SampleManifestDraftIdentityInput>> chooseSampleManifestDraftIdentity() override {
    using Output = std::optional<SampleManifestDraftIdentityInput>;
    // Empty native dialog template; controls use dialog-unit coordinates and
    // the OS modal loop, including keyboard navigation and Cancel on close.
    struct EmptyTemplate final { DLGTEMPLATE header; WORD menu, windowClass, title; };
    alignas(DWORD) const EmptyTemplate model{{WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER, 0, 0, 0, 0, 306, 190}, 0,0,0};
    DraftIdentityModal state;
    const auto shown = DialogBoxIndirectParamW(GetModuleHandleW(nullptr), &model.header, GetActiveWindow(), draftIdentityProcedure, reinterpret_cast<LPARAM>(&state));
    if (shown == -1) return core::failure<Output>(core::ErrorCode::IoError, state.error.empty() ? "Cannot display draft identity entry" : state.error);
    return shown == IDOK ? std::move(state.result) : Output{};
  }
  core::Result<std::optional<std::string>> chooseSampleReviewer(const std::vector<std::string>& reviewers) override {
    using Output = std::optional<std::string>;
    if (reviewers.empty() || reviewers.size() > 256U) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Registered reviewer choices are unavailable or oversized");
    for (const auto& reviewer : reviewers) {
      const auto text = wide("Use your registered reviewer identity: " + reviewer +
          "?\nYes selects this reviewer; No shows the next; Cancel leaves it unchanged.\nThis is attribution, not authentication. Never select another person's identity.");
      const auto response = MessageBoxW(nullptr, text.c_str(), L"Explicit reviewer selection", MB_YESNOCANCEL | MB_DEFBUTTON3 | MB_ICONQUESTION);
      if (response == IDYES) return Output{reviewer};
      if (response == IDCANCEL) return Output{};
      if (response == 0) return core::failure<Output>(core::ErrorCode::Internal, "Cannot show reviewer selection");
    }
    return Output{};
  }
  core::Result<bool> confirmSampleReview(std::string_view summary, bool accept) override {
    const auto text = wide(summary);
    const auto response = MessageBoxW(nullptr, text.c_str(),
        accept ? L"Accept this exact selected-unit material?" : L"Reject this exact selected-unit material?",
        MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING);
    if (response == 0) return core::failure<bool>(core::ErrorCode::Internal, "Cannot show sample review confirmation");
    return response == IDYES;
  }
  core::Result<UnsavedSampleDecision> confirmUnsavedSampleChanges() override {
    const auto response = MessageBoxW(nullptr,
        L"Save sample marker and pitch edits before closing?\nYes: Save edits.\nNo: Discard unsaved edits without deleting source audio or saved files.\nCancel: Keep editing.",
        L"Unsaved sample edits", MB_YESNOCANCEL | MB_DEFBUTTON3 | MB_ICONWARNING);
    if (response == 0) return core::failure<UnsavedSampleDecision>(core::ErrorCode::Internal, "Cannot show sample close confirmation");
    return response == IDYES ? UnsavedSampleDecision::Save : response == IDNO ? UnsavedSampleDecision::Discard : UnsavedSampleDecision::Cancel;
  }
  core::Result<bool> confirmDiscardDesignerChanges() override {
    const auto response = MessageBoxW(nullptr,
        L"Discard unsaved Designer changes? Saved recipe files and producer takes are not deleted. Choose No to return and save.",
        L"Discard unsaved voice changes?", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING);
    if (response == 0) return core::failure<bool>(core::ErrorCode::Internal, "Unable to show Designer discard confirmation");
    return response == IDYES;
  }
  core::Result<std::optional<std::filesystem::path>> choose(
      const FileDialogRequest& request) override {
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                                        COINIT_DISABLE_OLE1DDE);
    const bool uninitialize = SUCCEEDED(initialized);
    const bool directory = request.purpose == FileDialogPurpose::RelinkVoicebank;
    const bool save = request.purpose == FileDialogPurpose::SaveProject ||
                      request.purpose == FileDialogPurpose::ExportAudio ||
                      request.purpose == FileDialogPurpose::ExportScore ||
                      request.purpose == FileDialogPurpose::ExportSet ||
                      request.purpose == FileDialogPurpose::BakeProceduralCandidates ||
                      request.purpose == FileDialogPurpose::ExportPitchInspection ||
                      request.purpose == FileDialogPurpose::PrepareGenerationJob ||
                      request.purpose == FileDialogPurpose::PrepareGenerationBatch ||
                      request.purpose == FileDialogPurpose::SaveDesignerRecipe ||
                      request.purpose == FileDialogPurpose::PublishSampleCandidate ||
                      request.purpose == FileDialogPurpose::CreateSampleManifestDraft;
    ComPtr<::IFileDialog> dialog;
    const auto created = save
        ? CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&dialog))
        : CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&dialog));
    if (FAILED(created)) {
      if (uninitialize) CoUninitialize();
      return core::failure<std::optional<std::filesystem::path>>(
          core::ErrorCode::Unsupported,
          "Unable to create the native Windows file dialog");
    }
    if (directory) {
      DWORD options = 0;
      if (SUCCEEDED(dialog->GetOptions(&options))) {
        static_cast<void>(dialog->SetOptions(options | FOS_PICKFOLDERS));
      }
    }
    const auto title = wide(request.title);
    if (!title.empty()) static_cast<void>(dialog->SetTitle(title.c_str()));
    const auto suggested = wide(request.suggestedName);
    if (!suggested.empty()) static_cast<void>(dialog->SetFileName(suggested.c_str()));
    if (!request.initialDirectory.empty()) {
      ComPtr<IShellItem> folder;
      if (SUCCEEDED(SHCreateItemFromParsingName(
              request.initialDirectory.c_str(), nullptr, IID_PPV_ARGS(&folder)))) {
        static_cast<void>(dialog->SetFolder(folder.Get()));
      }
    }
    std::vector<std::wstring> filterStorage;
    const auto specifications = filters(request.extensions, filterStorage);
    if (!specifications.empty()) {
      static_cast<void>(dialog->SetFileTypes(
          static_cast<UINT>(specifications.size()), specifications.data()));
    }
    const auto shown = dialog->Show(nullptr);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
      if (uninitialize) CoUninitialize();
      return std::optional<std::filesystem::path>{};
    }
    if (FAILED(shown)) {
      if (uninitialize) CoUninitialize();
      return core::failure<std::optional<std::filesystem::path>>(
          core::ErrorCode::IoError, "The native Windows file dialog failed");
    }
    auto selected = resultPath(dialog.Get());
    if (uninitialize) CoUninitialize();
    return selected;
  }
};

}  // namespace

std::unique_ptr<IFileDialog> createNativeFileDialog() {
  return std::make_unique<Win32FileDialog>();
}

}  // namespace seam::platform
#endif
