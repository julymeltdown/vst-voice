#if defined(_WIN32)

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <cwchar>
#include <string>
#include <thread>
#include <vector>

int seam_editor_native_main(int argc, char** argv);

namespace {

std::string utf8FromWide(const wchar_t* text) {
  if (text == nullptr || *text == L'\0') return {};
  const int length = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
  if (length <= 1) return {};
  std::string result(static_cast<std::size_t>(length - 1), '\0');
  const int converted = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), length - 1,
      nullptr, nullptr);
  return converted == length - 1 ? result : std::string{};
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  HANDLE instanceMutex = CreateMutexW(nullptr, TRUE,
                                      L"Local\\ProjectSEAM.ExternalBeta");
  if (instanceMutex == nullptr) return 2;
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND window = nullptr;
    for (int attempt = 0; attempt < 20 && window == nullptr; ++attempt) {
      window = FindWindowW(L"ProjectSeamNativeEditorWindow", nullptr);
      if (window == nullptr) std::this_thread::sleep_for(
          std::chrono::milliseconds{50});
    }
    if (window != nullptr) {
      const auto commandLine = GetCommandLineW();
      const auto bytes = (wcslen(commandLine) + 1U) * sizeof(wchar_t);
      COPYDATASTRUCT data{1U, static_cast<DWORD>(bytes),
                          const_cast<wchar_t*>(commandLine)};
      SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
    }
    CloseHandle(instanceMutex);
    return 0;
  }

  int argumentCount = 0;
  LPWSTR* wideArguments = CommandLineToArgvW(GetCommandLineW(),
                                              &argumentCount);
  if (wideArguments == nullptr || argumentCount <= 0) {
    if (wideArguments != nullptr) LocalFree(wideArguments);
    CloseHandle(instanceMutex);
    return 2;
  }

  std::vector<std::string> arguments;
  arguments.reserve(static_cast<std::size_t>(argumentCount));
  for (int index = 0; index < argumentCount; ++index) {
    auto converted = utf8FromWide(wideArguments[index]);
    if (converted.empty() && wideArguments[index][0] != L'\0') {
      LocalFree(wideArguments);
      CloseHandle(instanceMutex);
      return 2;
    }
    arguments.push_back(std::move(converted));
  }
  LocalFree(wideArguments);

  std::vector<char*> pointers;
  pointers.reserve(arguments.size());
  for (auto& argument : arguments) pointers.push_back(argument.data());
  const auto result = seam_editor_native_main(static_cast<int>(pointers.size()),
                                              pointers.data());
  CloseHandle(instanceMutex);
  return result;
}

#endif
