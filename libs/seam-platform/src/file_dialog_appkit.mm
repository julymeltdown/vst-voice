#include "seam/platform/file_dialog.hpp"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace seam::platform {
namespace {

NSString* nsString(std::string_view value) {
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding];
}

NSArray<UTType*>* allowedTypes(const std::vector<std::string>& extensions) {
  NSMutableArray<UTType*>* result = [NSMutableArray array];
  for (const auto& extension : extensions) {
    NSString* text = nsString(extension);
    UTType* type = [UTType typeWithFilenameExtension:text];
    if (type != nil) [result addObject:type];
  }
  return result;
}

std::filesystem::path pathFromUrl(NSURL* url) {
  const char* value = url.fileSystemRepresentation;
  return value == nullptr ? std::filesystem::path{}
                          : std::filesystem::path{value};
}

class AppKitFileDialog final : public IFileDialog {
public:
  core::Result<std::optional<std::filesystem::path>> choose(
      const FileDialogRequest& request) override {
    if (![NSThread isMainThread]) {
      return core::failure<std::optional<std::filesystem::path>>(
          core::ErrorCode::InvalidState,
          "AppKit file dialogs must run on the main thread");
    }
    @autoreleasepool {
      const bool directory = request.purpose == FileDialogPurpose::RelinkVoicebank;
      const bool save = request.purpose == FileDialogPurpose::SaveProject ||
                        request.purpose == FileDialogPurpose::ExportAudio ||
                        request.purpose == FileDialogPurpose::ExportSet;
      NSOpenPanel* openPanel = save ? nil : [NSOpenPanel openPanel];
      NSSavePanel* panel = save ? [NSSavePanel savePanel] : openPanel;
      panel.title = nsString(request.title);
      if (!request.initialDirectory.empty()) {
        panel.directoryURL = [NSURL fileURLWithPath:
            [NSString stringWithUTF8String:request.initialDirectory.string().c_str()]
                                      isDirectory:YES];
      }
      if (!request.suggestedName.empty()) {
        panel.nameFieldStringValue = nsString(request.suggestedName);
      }
      const auto types = allowedTypes(request.extensions);
      if (types.count > 0U) panel.allowedContentTypes = types;
      if (save) {
        panel.canCreateDirectories =
            request.purpose == FileDialogPurpose::SaveProject ||
            request.purpose == FileDialogPurpose::ExportSet;
      }
      if (openPanel != nil) {
        openPanel.canChooseFiles = !directory;
        openPanel.canChooseDirectories = directory;
        openPanel.canCreateDirectories = directory;
        openPanel.allowsMultipleSelection = NO;
      }
      if ([panel runModal] != NSModalResponseOK || panel.URL == nil) {
        return std::optional<std::filesystem::path>{};
      }
      return std::optional<std::filesystem::path>{pathFromUrl(panel.URL)};
    }
  }
};

}  // namespace

std::unique_ptr<IFileDialog> createNativeFileDialog() {
  return std::make_unique<AppKitFileDialog>();
}

}  // namespace seam::platform
#endif
