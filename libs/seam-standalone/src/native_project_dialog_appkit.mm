#include "seam/standalone/native_project_dialog.hpp"

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "seam/native_ui/new_project_dialog.hpp"

#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

@interface SEAMVoicebankToggleTarget : NSObject
@property(nonatomic, assign) NSPopUpButton* voicebank;
@end

@implementation SEAMVoicebankToggleTarget
- (void)toggleVoicebank:(NSButton*)sender {
  const auto enabled = sender.state == NSControlStateValueOn;
  self.voicebank.enabled = enabled;
  if (!enabled) [self.voicebank selectItemAtIndex:0];
}
@end

namespace seam::standalone {
namespace {

NSString* nsString(std::string_view value) {
  return [[NSString alloc] initWithBytes:value.data()
                                  length:value.size()
                                encoding:NSUTF8StringEncoding];
}

std::string utf8(NSString* value) {
  const auto* bytes = value == nil ? nullptr : value.UTF8String;
  return bytes == nullptr ? std::string{} : std::string{bytes};
}

NSTextField* label(NSString* title, NSRect frame) {
  auto* result = [NSTextField labelWithString:title];
  result.frame = frame;
  return result;
}

NSPopUpButton* popup(NSRect frame, std::initializer_list<NSString*> values) {
  auto* result = [[NSPopUpButton alloc] initWithFrame:frame pullsDown:NO];
  for (NSString* value : values) [result addItemWithTitle:value];
  return result;
}

std::optional<std::uint32_t> unsignedValue(NSString* value) {
  try {
    const auto parsed = std::stoul(utf8(value));
    if (parsed > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(parsed);
  } catch (...) {
    return std::nullopt;
  }
}

std::string suggestedProjectName(std::string_view suggested) {
  const auto fallback = suggested.empty() ? std::string{"Untitled"}
                                           : std::string{suggested};
  const auto path = std::filesystem::path{fallback};
  return path.extension() == ".seam" ? path.stem().string() : fallback;
}

std::string suggestedProjectFileName(std::string_view name) {
  const auto path = std::filesystem::path{name};
  return path.extension() == ".seam" ? std::string{name}
                                     : std::string{name} + ".seam";
}

class AppKitNativeNewProjectDialog final : public INativeNewProjectDialog {
public:
  core::Result<std::optional<authoring::NewProjectRequest>> choose(
      NativeNewProjectDialogConfig config) override {
    if (![NSThread isMainThread]) {
      return core::failure<std::optional<authoring::NewProjectRequest>>(
          core::ErrorCode::InvalidState,
          "AppKit New Project form must run on the main thread");
    }

    @autoreleasepool {
      native_ui::NewProjectDialogModel model{config.candidates};
      const auto initialName = suggestedProjectName(config.suggestedName);
      model.setName(initialName);
      model.setTempoBpm(120.0);
      model.setMeter(4U, 4U);
      model.setSampleRate(config.sampleRate);
      model.setOutputChannels(config.outputChannels);

      auto* view = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 460.0, 276.0)];
      auto* name = [[NSTextField alloc] initWithFrame:NSMakeRect(150.0, 238.0, 294.0, 24.0)];
      name.stringValue = nsString(initialName);
      [view addSubview:label(@"Project name", NSMakeRect(12.0, 242.0, 126.0, 18.0))];
      [view addSubview:name];

      auto* tempo = [[NSTextField alloc] initWithFrame:NSMakeRect(150.0, 204.0, 90.0, 24.0)];
      tempo.stringValue = @"120";
      [view addSubview:label(@"Tempo (BPM)", NSMakeRect(12.0, 208.0, 126.0, 18.0))];
      [view addSubview:tempo];

      auto* numerator = popup(NSMakeRect(150.0, 170.0, 70.0, 26.0),
                              {@"2", @"3", @"4", @"5", @"6", @"7", @"8", @"9", @"12"});
      [numerator selectItemWithTitle:@"4"];
      auto* denominator = popup(NSMakeRect(230.0, 170.0, 70.0, 26.0),
                                {@"1", @"2", @"4", @"8", @"16", @"32"});
      [denominator selectItemWithTitle:@"4"];
      [view addSubview:label(@"Time signature", NSMakeRect(12.0, 174.0, 126.0, 18.0))];
      [view addSubview:numerator];
      [view addSubview:denominator];

      auto* sampleRate = popup(NSMakeRect(150.0, 136.0, 120.0, 26.0),
                               {@"44100", @"48000", @"96000"});
      [sampleRate selectItemWithTitle:nsString(std::to_string(config.sampleRate))];
      [view addSubview:label(@"Sample rate", NSMakeRect(12.0, 140.0, 126.0, 18.0))];
      [view addSubview:sampleRate];

      auto* channels = popup(NSMakeRect(150.0, 102.0, 90.0, 26.0),
                             {@"1", @"2", @"4", @"8"});
      [channels selectItemWithTitle:nsString(std::to_string(config.outputChannels))];
      [view addSubview:label(@"Output channels", NSMakeRect(12.0, 106.0, 126.0, 18.0))];
      [view addSubview:channels];

      auto* track = [[NSButton alloc] initWithFrame:NSMakeRect(12.0, 66.0, 300.0, 24.0)];
      track.buttonType = NSButtonTypeSwitch;
      track.title = @"Create initial vocal track";
      track.state = NSControlStateValueOn;
      [view addSubview:track];

      auto* bank = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(150.0, 26.0, 294.0, 26.0)
                                             pullsDown:NO];
      [bank addItemWithTitle:@"No Voicebank"];
      for (const auto& candidate : config.candidates) {
        const auto title = candidate.manifest.displayName + " / " +
                           candidate.manifest.id + " " + candidate.manifest.version;
        [bank addItemWithTitle:nsString(title)];
      }
      [view addSubview:label(@"Initial Voicebank", NSMakeRect(12.0, 30.0, 126.0, 18.0))];
      [view addSubview:bank];
      auto* toggleTarget = [[SEAMVoicebankToggleTarget alloc] init];
      toggleTarget.voicebank = bank;
      track.target = toggleTarget;
      track.action = @selector(toggleVoicebank:);

      auto* alert = [[NSAlert alloc] init];
      alert.messageText = @"Create New Project";
      alert.informativeText = @"Choose the project identity, timing, output, and exact initial Voicebank.";
      alert.accessoryView = view;
      [alert addButtonWithTitle:@"Choose Location…"];
      [alert addButtonWithTitle:@"Cancel"];
      if ([alert runModal] != NSAlertFirstButtonReturn) {
        return std::optional<authoring::NewProjectRequest>{};
      }

      const auto nameValue = utf8(name.stringValue);
      double tempoValue = 0.0;
      try {
        tempoValue = std::stod(utf8(tempo.stringValue));
      } catch (...) {
        return core::failure<std::optional<authoring::NewProjectRequest>>(
            core::ErrorCode::InvalidArgument, "Tempo must be a number");
      }
      const auto numeratorValue = unsignedValue(numerator.titleOfSelectedItem);
      const auto denominatorValue = unsignedValue(denominator.titleOfSelectedItem);
      const auto sampleRateValue = unsignedValue(sampleRate.titleOfSelectedItem);
      const auto channelValue = unsignedValue(channels.titleOfSelectedItem);
      if (!numeratorValue.has_value() || !denominatorValue.has_value() ||
          !sampleRateValue.has_value() || !channelValue.has_value()) {
        return core::failure<std::optional<authoring::NewProjectRequest>>(
            core::ErrorCode::InvalidArgument,
            "New Project numeric selections are invalid");
      }
      model.setName(nameValue);
      model.setTempoBpm(tempoValue);
      model.setMeter(static_cast<std::uint8_t>(*numeratorValue),
                     static_cast<std::uint8_t>(*denominatorValue));
      model.setSampleRate(*sampleRateValue);
      model.setOutputChannels(static_cast<std::uint8_t>(*channelValue));
      const auto createTrack = track.state == NSControlStateValueOn;
      model.setCreateInitialVocalTrack(createTrack);
      if (createTrack && bank.indexOfSelectedItem > 0) {
        const auto selected = model.selectVoicebank(
            static_cast<std::size_t>(bank.indexOfSelectedItem - 1));
        if (!selected) return core::Result<std::optional<authoring::NewProjectRequest>>{
            selected.error()};
      }

      auto* save = [NSSavePanel savePanel];
      save.title = @"Choose Project Location";
      const auto fileName = suggestedProjectFileName(
          nameValue.empty() ? std::string_view{"Untitled"}
                            : std::string_view{nameValue});
      save.nameFieldStringValue = nsString(fileName);
      save.canCreateDirectories = YES;
      save.allowedContentTypes = @[[UTType typeWithFilenameExtension:@"seam"]];
      if (!config.initialDirectory.empty()) {
        save.directoryURL = [NSURL fileURLWithPath:
            [NSString stringWithUTF8String:config.initialDirectory.string().c_str()]
                                      isDirectory:YES];
      }
      if ([save runModal] != NSModalResponseOK || save.URL == nil) {
        return std::optional<authoring::NewProjectRequest>{};
      }
      const char* path = save.URL.fileSystemRepresentation;
      if (path == nullptr) {
        return core::failure<std::optional<authoring::NewProjectRequest>>(
            core::ErrorCode::IoError, "Unable to resolve project location");
      }
      model.setProjectPath(std::filesystem::path{path});
      auto submitted = model.submit();
      if (!submitted) {
        return core::Result<std::optional<authoring::NewProjectRequest>>{
            submitted.error()};
      }
      return std::optional<authoring::NewProjectRequest>{std::move(submitted).value()};
    }
  }
};

}

std::unique_ptr<INativeNewProjectDialog> createNativeNewProjectDialog() {
  return std::make_unique<AppKitNativeNewProjectDialog>();
}

}

#endif
