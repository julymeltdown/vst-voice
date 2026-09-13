#include "seam/platform/file_dialog.hpp"

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace seam::platform {
namespace {

template <typename Dialog>
NSModalResponse runModalRestoringFocus(Dialog* dialog) {
  NSWindow* owner = NSApp.keyWindow;
  NSResponder* responder = owner.firstResponder;
  const NSModalResponse response = [dialog runModal];
  // A cancelled panel can leave keyboard input detached from the canvas.
  // Do not revive an owner that was closed while the modal loop was running.
  if (owner != nil && owner.visible) {
    [owner makeKeyWindow];
    if (responder != nil) [owner makeFirstResponder:responder];
  }
  return response;
}

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
  core::Result<std::optional<ProductionWorkspaceInput>> chooseProductionWorkspace() override {
    using Output=std::optional<ProductionWorkspaceInput>;
    if (![NSThread isMainThread]) return core::failure<Output>(core::ErrorCode::InvalidState,"Workspace entry must run on the main thread");
    @autoreleasepool {
      NSOpenPanel* panel=[NSOpenPanel openPanel];
      panel.title=@"Open Existing Producer Workspace";
      panel.canChooseDirectories=YES; panel.canChooseFiles=NO;
      panel.canCreateDirectories=NO; panel.allowsMultipleSelection=NO;
      if (runModalRestoringFocus(panel)!=NSModalResponseOK || panel.URL==nil) return Output{};
      const auto root=pathFromUrl(panel.URL);
      NSAlert* alert=[[NSAlert alloc] init];
      alert.messageText=@"Bind producer workspace context";
      alert.informativeText=@"Enter the expected inventory SHA-256 and your existing operator ID. Opening does not register an operator, approve sources, or publish changes.";
      [alert addButtonWithTitle:@"Open Workspace"]; [alert addButtonWithTitle:@"Cancel"];
      NSView* fields=[[NSView alloc] initWithFrame:NSMakeRect(0,0,480,66)];
      NSTextField* digest=[[NSTextField alloc] initWithFrame:NSMakeRect(0,36,480,26)];
      digest.placeholderString=@"Expected inventory SHA-256 (64 lowercase hex characters)";
      [digest setAccessibilityLabel:@"Expected inventory SHA-256"];
      NSTextField* identity=[[NSTextField alloc] initWithFrame:NSMakeRect(0,0,480,26)];
      identity.placeholderString=@"Existing operator ID"; [identity setAccessibilityLabel:@"Producer operator ID"];
      [fields addSubview:digest]; [fields addSubview:identity]; alert.accessoryView=fields;
      if (runModalRestoringFocus(alert)!=NSAlertFirstButtonReturn) return Output{};
      if (digest.stringValue.length!=64U || identity.stringValue.length==0U || identity.stringValue.length>128U ||
          digest.stringValue.UTF8String==nullptr || identity.stringValue.UTF8String==nullptr)
        return core::failure<Output>(core::ErrorCode::InvalidArgument,"Workspace digest or operator ID is invalid");
      ProductionWorkspaceInput input{root,digest.stringValue.UTF8String,identity.stringValue.UTF8String};
      const auto valid=input.validate(); if (!valid) return core::Result<Output>{valid.error()};
      return Output{std::move(input)};
    }
  }
  core::Result<std::optional<SourceRegistrationInput>> chooseSourceRegistration(std::string_view summary) override {
    using Output = std::optional<SourceRegistrationInput>;
    if (![NSThread isMainThread] || summary.size()>16384U || nsString(summary)==nil)
      return core::failure<Output>(core::ErrorCode::InvalidArgument,"Source registration context is invalid or oversized");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Register and select a NEW source";
      alert.informativeText = @"Declare only rights you actually hold. These entries are not legal verification. Existing take ownership stays unchanged; coverage and listening remain Not assessed.";
      [alert addButtonWithTitle:@"Cancel"]; [alert addButtonWithTitle:@"Register Source"];
      NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0,0,560,364)];
      NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0,256,560,108)];
      scroll.hasVerticalScroller = YES; scroll.borderType = NSBezelBorder;
      NSTextView* context = [[NSTextView alloc] initWithFrame:NSMakeRect(0,0,540,108)];
      context.editable = NO; context.selectable = YES; context.string = nsString(summary);
      context.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
      context.textContainer.widthTracksTextView = YES; context.autoresizingMask = NSViewWidthSizable;
      context.verticallyResizable = YES; [context setAccessibilityLabel:@"Captured license and producer identities"];
      scroll.documentView = context; [view addSubview:scroll];
      NSArray<NSString*>* labels = @[@"New source ID",@"Source kind",@"Declared rights",@"Source use",@"Transformation",@"Bank redistribution",@"Commercial renders"];
      NSTextField* identity = nil;
      NSMutableArray<NSPopUpButton*>* choices = [NSMutableArray array];
      for (NSUInteger index=0U;index<labels.count;++index) {
        const CGFloat y = 216.0-static_cast<CGFloat>(index)*36.0;
        NSTextField* label = [NSTextField labelWithString:labels[index]];
        label.frame=NSMakeRect(0,y+4,176,22); [view addSubview:label];
        if (index==0U) {
          identity = [[NSTextField alloc] initWithFrame:NSMakeRect(180,y,380,26)];
          identity.placeholderString=@"Required unique ID"; [identity setAccessibilityLabel:labels[index]]; [view addSubview:identity];
        } else {
          NSPopUpButton* field = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(180,y,380,28) pullsDown:NO];
          [field addItemWithTitle:@"Choose explicitly — required"];
          if (index==1U) [field addItemsWithTitles:@[@"Human recording",@"Procedural synthesis",@"TTS-derived"]];
          else if (index==2U) [field addItemsWithTitles:@[@"Not assessed",@"Pass (producer declaration)",@"Blocked"]];
          else [field addItemsWithTitles:@[@"No",@"Yes"]];
          [field setAccessibilityLabel:labels[index]]; [view addSubview:field]; [choices addObject:field];
        }
      }
      alert.accessoryView=view;
      if (runModalRestoringFocus(alert)!=NSAlertSecondButtonReturn) return Output{};
      if (identity.stringValue.length==0U || identity.stringValue.length>128U || identity.stringValue.UTF8String==nullptr)
        return core::failure<Output>(core::ErrorCode::InvalidArgument,"Enter a unique source ID of at most 128 characters");
      for (NSPopUpButton* choice in choices) if (choice.indexOfSelectedItem<=0)
        return core::failure<Output>(core::ErrorCode::InvalidArgument,"Explicitly choose source kind, rights and all four permissions");
      const auto kind = choices[0].indexOfSelectedItem, rights = choices[1].indexOfSelectedItem;
      SourceRegistrationInput result{identity.stringValue.UTF8String,kind==1?"human":kind==2?"procedural":"tts",
          rights==1?"not-assessed":rights==2?"pass":"blocked",{}};
      for (NSUInteger i=0U;i<4U;++i) result.permissions[i]=choices[i+2U].indexOfSelectedItem==1?"no":"yes";
      return Output{std::move(result)};
    }
  }
  core::Result<std::optional<SourceQualityDecisionInput>> chooseSourceQualityDecision(
      std::string_view summary, const std::vector<std::string>& reviewers) override {
    using Output = std::optional<SourceQualityDecisionInput>;
    if (![NSThread isMainThread] || summary.size()>16384U || reviewers.empty() || reviewers.size()>256U)
      return core::failure<Output>(core::ErrorCode::InvalidArgument,"Source decision context or reviewer choices are unavailable or oversized");
    @autoreleasepool {
      for (const auto& value : reviewers) if (value.empty() || value.size()>256U || nsString(value)==nil)
        return core::failure<Output>(core::ErrorCode::InvalidArgument,"Source reviewer identity is invalid or oversized");
      if (nsString(summary)==nil) return core::failure<Output>(core::ErrorCode::InvalidArgument,"Source decision context is not valid UTF-8");
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Record source-quality assessment";
      alert.informativeText = @"Use your actual registered reviewer identity. Record only the decision supported by the captured evidence. Source rights stay unchanged; unit review remains separate.";
      [alert addButtonWithTitle:@"Cancel"]; [alert addButtonWithTitle:@"Record Decision"];
      NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0,0,560,282)];
      NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0,164,560,118)];
      scroll.hasVerticalScroller = YES; scroll.borderType = NSBezelBorder;
      NSTextView* details = [[NSTextView alloc] initWithFrame:NSMakeRect(0,0,540,118)];
      details.editable = NO; details.selectable = YES; details.string = nsString(summary);
      details.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
      details.textContainer.widthTracksTextView = YES; details.autoresizingMask = NSViewWidthSizable;
      details.verticallyResizable = YES; [details setAccessibilityLabel:@"Captured source and evidence identities"];
      scroll.documentView = details; [view addSubview:scroll];
      const auto label = [&](NSString* text, CGFloat y) { NSTextField* field = [NSTextField labelWithString:text]; field.frame=NSMakeRect(0,y+4,128,22); [view addSubview:field]; };
      label(@"Assessment ID",124); label(@"Source reviewer",88); label(@"Coverage",52); label(@"Listening",16);
      NSTextField* identity = [[NSTextField alloc] initWithFrame:NSMakeRect(132,124,428,26)];
      identity.placeholderString = @"Required unique ID"; [identity setAccessibilityLabel:@"Unique source assessment ID"]; [view addSubview:identity];
      NSPopUpButton* reviewer = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(132,88,428,28) pullsDown:NO];
      [reviewer addItemWithTitle:@"Choose reviewer — no default"];
      for (const auto& value : reviewers) [reviewer addItemWithTitle:nsString(value)];
      [reviewer setAccessibilityLabel:@"Actual independent source reviewer"]; [view addSubview:reviewer];
      NSPopUpButton* coverage = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(132,52,428,28) pullsDown:NO];
      NSPopUpButton* listening = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(132,16,428,28) pullsDown:NO];
      for (NSPopUpButton* choice in @[coverage,listening]) { [choice addItemsWithTitles:@[@"Not assessed",@"Pass",@"Blocked"]]; [view addSubview:choice]; }
      [coverage setAccessibilityLabel:@"Source coverage decision"]; [listening setAccessibilityLabel:@"Source listening decision"];
      alert.accessoryView = view;
      if (runModalRestoringFocus(alert) != NSAlertSecondButtonReturn) return Output{};
      const auto selected = reviewer.indexOfSelectedItem;
      const char* id = identity.stringValue.UTF8String;
      if (!id || identity.stringValue.length==0U || identity.stringValue.length>128U || selected<=0 || static_cast<std::size_t>(selected)>reviewers.size())
        return core::failure<Output>(core::ErrorCode::InvalidArgument,"Enter an assessment ID and explicitly choose your reviewer identity");
      const auto outcome = [](NSInteger index) { return index==1 ? "pass" : index==2 ? "blocked" : "not-assessed"; };
      return Output{SourceQualityDecisionInput{id,reviewers[static_cast<std::size_t>(selected-1)],outcome(coverage.indexOfSelectedItem),outcome(listening.indexOfSelectedItem)}};
    }
  }
  core::Result<std::optional<SampleManifestDraftIdentityInput>> chooseSampleManifestDraftIdentity() override {
    using Output = std::optional<SampleManifestDraftIdentityInput>;
    if (![NSThread isMainThread]) return core::failure<Output>(core::ErrorCode::InvalidState, "Draft identity entry must run on the main thread");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Create an editable sample draft";
      alert.informativeText = @"Enter all five fields explicitly. Current producer audio is copied to a NEW directory. Marker and pitch estimates remain unreviewed; this does not approve sources, publish a release, or install a bank.";
      [alert addButtonWithTitle:@"Cancel"];
      [alert addButtonWithTitle:@"Choose New Destination"];
      NSView* fields = [[NSView alloc] initWithFrame:NSMakeRect(0,0,480,176)];
      NSArray<NSString*>* names = @[@"Bank ID", @"Version", @"Display name", @"Language", @"Style"];
      NSMutableArray<NSTextField*>* inputs = [NSMutableArray array];
      NSPopUpButton* language = nil;
      for (NSUInteger i = 0U; i < names.count; ++i) {
        const CGFloat y = 144.0 - static_cast<CGFloat>(i) * 34.0;
        NSTextField* label = [NSTextField labelWithString:names[i]];
        label.frame = NSMakeRect(0,y+4.0,108,22); [fields addSubview:label];
        if (i == 3U) {
          language = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(112,y,368,28) pullsDown:NO];
          [language addItemsWithTitles:@[@"Choose language — required", @"Japanese (ja)", @"English (en)", @"Korean (ko)"]];
          [language setAccessibilityLabel:@"Draft language, explicitly choose ja, en or ko"];
          [fields addSubview:language];
        } else {
          NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(112,y,368,26)];
          field.placeholderString = i == 0U ? @"Required, e.g. my.singer" : i == 1U ? @"Required, e.g. 0.1.0" : i == 2U ? @"Required display name" : @"Required single style";
          [field setAccessibilityLabel:names[i]]; [inputs addObject:field]; [fields addSubview:field];
        }
      }
      alert.accessoryView = fields;
      if ([alert runModal] != NSAlertSecondButtonReturn) return Output{};
      const auto index = language.indexOfSelectedItem;
      if (index < 1 || index > 3) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Explicitly select Japanese, English or Korean; there is no default language");
      std::vector<std::string> text;
      for (NSTextField* field in inputs) {
        const char* value = field.stringValue.UTF8String;
        if (value == nullptr || field.stringValue.length > 256U) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Draft identity field is invalid or oversized");
        text.emplace_back(value);
      }
      return Output{SampleManifestDraftIdentityInput{text[0],text[1],text[2],index == 1 ? "ja" : index == 2 ? "en" : "ko",text[3]}};
    }
  }
  core::Result<std::optional<std::string>> chooseSampleReviewer(const std::vector<std::string>& reviewers) override {
    using Output = std::optional<std::string>;
    if (![NSThread isMainThread] || reviewers.empty() || reviewers.size() > 256U)
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Registered reviewer choices are unavailable or oversized");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Choose the actual independent reviewer";
      alert.informativeText = @"Select your registered reviewer identity. This is attribution, not authentication. Do not select another person's identity. The producer or an editor of this material cannot independently approve it.";
      [alert addButtonWithTitle:@"Cancel"];
      [alert addButtonWithTitle:@"Use Selected Reviewer"];
      NSPopUpButton* choices = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0,0,420,28) pullsDown:NO];
      [choices addItemWithTitle:@"Choose reviewer — no default"];
      [choices setAccessibilityLabel:@"Explicit registered reviewer identity"];
      for (const auto& reviewer : reviewers) [choices addItemWithTitle:nsString(reviewer)];
      alert.accessoryView = choices;
      if ([alert runModal] != NSAlertSecondButtonReturn) return Output{};
      const auto index = choices.indexOfSelectedItem;
      if (index <= 0 || static_cast<std::size_t>(index) > reviewers.size())
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "No reviewer was explicitly selected");
      return Output{reviewers[static_cast<std::size_t>(index - 1)]};
    }
  }
  core::Result<bool> confirmSampleReview(std::string_view summary, bool accept) override {
    if (![NSThread isMainThread]) return core::failure<bool>(core::ErrorCode::InvalidState, "Review confirmation must run on the main thread");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = accept ? @"Accept this exact selected-unit material?" : @"Reject this exact selected-unit material?";
      alert.informativeText = nsString(summary);
      [alert addButtonWithTitle:@"Cancel"];
      [alert addButtonWithTitle:accept ? @"Record Acceptance" : @"Record Rejection"];
      return [alert runModal] == NSAlertSecondButtonReturn;
    }
  }
  core::Result<std::optional<std::string>> chooseDesignerSeed(std::string_view current, bool frication) override {
    using Output = std::optional<std::string>;
    if (![NSThread isMainThread]) return core::failure<Output>(core::ErrorCode::InvalidState, "Seed dialog must run on the main thread");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = frication ? @"Set noise source seed" : @"Set reproducible voice seed";
      alert.informativeText = frication ? @"Enter a whole decimal number from 0 to 18446744073709551615. Only the selected noise source changes; the global voice seed and other sources are preserved."
          : @"Enter a whole decimal number from 0 to 18446744073709551615. This changes draft source noise and modulation phase, not source approval.";
      [alert addButtonWithTitle:@"Apply"];
      [alert addButtonWithTitle:@"Cancel"];
      NSTextField* field = [[NSTextField alloc] initWithFrame:NSMakeRect(0,0,360,26)];
      field.stringValue = nsString(current); [field setAccessibilityLabel:@"Voice seed, unsigned 64-bit decimal"];
      alert.accessoryView = field;
      if ([alert runModal] != NSAlertFirstButtonReturn) return Output{};
      const char* text = field.stringValue.UTF8String;
      if (text == nullptr || field.stringValue.length > 20U) return core::failure<Output>(core::ErrorCode::InvalidArgument, "Voice seed is too long");
      return Output{std::string{text}};
    }
  }
  core::Result<std::optional<DesignerPoseIdentity>> chooseDesignerPoseIdentity(DesignerPoseKind kind) override {
    const bool frication=kind!=DesignerPoseKind::Voiced, plosive=kind==DesignerPoseKind::Plosive;
    using Output = std::optional<DesignerPoseIdentity>;
    if (![NSThread isMainThread]) return core::failure<Output>(core::ErrorCode::InvalidState, "Pose naming must run on the main thread");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = plosive ? @"Add plosive source" : frication ? @"Add frication source" : @"Duplicate selected voice pose";
      alert.informativeText = plosive ? @"Enter p, t, or k and an existing vowel style. The initial 10 ms burst is an unqualified starting point; tune it and review rendered phrases before production use." : frication ? @"Enter a distinct consonant phone and an existing vowel style. The default noise source must be tuned and reviewed before production use."
          : @"Enter a distinct phone/style pair. Resonances are copied as a starting point; retune and audition the new pose before using it.";
      [alert addButtonWithTitle:frication ? @"Add Source" : @"Duplicate"];
      [alert addButtonWithTitle:@"Cancel"];
      NSView* fields = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 360, 66)];
      NSTextField* phone = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 36, 360, 26)];
      phone.placeholderString = plosive ? @"Phone (p, t, or k)" : frication ? @"Phone (for example s)" : @"Phone (for example i)";
      [phone setAccessibilityLabel:@"New pose phone"];
      NSTextField* style = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 360, 26)];
      style.stringValue = @"neutral";
      [style setAccessibilityLabel:@"New pose style"];
      [fields addSubview:phone]; [fields addSubview:style]; alert.accessoryView = fields;
      if ([alert runModal] != NSAlertFirstButtonReturn) return Output{};
      const char* phoneText = phone.stringValue.UTF8String;
      const char* styleText = style.stringValue.UTF8String;
      if (phoneText == nullptr || styleText == nullptr || phone.stringValue.length > 128U || style.stringValue.length > 128U)
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "Pose phone/style is invalid or too long");
      return Output{DesignerPoseIdentity{phoneText, styleText}};
    }
  }
  core::Result<UnsavedSampleDecision> confirmUnsavedSampleChanges() override {
    if (![NSThread isMainThread]) return core::failure<UnsavedSampleDecision>(core::ErrorCode::InvalidState, "Sample close confirmation must run on the main thread");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Save sample edits before closing?";
      alert.informativeText = @"Marker and pitch edits have not been saved. Discard closes without saving these edits; existing source audio and saved files are not deleted.";
      [alert addButtonWithTitle:@"Cancel"];
      [alert addButtonWithTitle:@"Save"];
      [alert addButtonWithTitle:@"Discard Edits"];
      const auto response = [alert runModal];
      return response == NSAlertSecondButtonReturn ? UnsavedSampleDecision::Save
          : response == NSAlertThirdButtonReturn ? UnsavedSampleDecision::Discard : UnsavedSampleDecision::Cancel;
    }
  }
  core::Result<bool> confirmDiscardDesignerChanges() override {
    if (![NSThread isMainThread]) return core::failure<bool>(core::ErrorCode::InvalidState, "Discard confirmation must run on the main thread");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Discard unsaved voice changes?";
      alert.informativeText = @"Only the unsaved Designer draft changes will be discarded. Saved recipe files and producer takes are not deleted. Cancel to return and save your draft.";
      [alert addButtonWithTitle:@"Cancel"];
      [alert addButtonWithTitle:@"Discard Changes"];
      return [alert runModal] == NSAlertSecondButtonReturn;
    }
  }
  core::Result<std::vector<std::filesystem::path>> chooseGenerationJobs() override {
    using Output = std::vector<std::filesystem::path>;
    if (![NSThread isMainThread]) return core::failure<Output>(core::ErrorCode::InvalidState, "Job selection must run on the main thread");
    @autoreleasepool {
      NSOpenPanel* panel = [NSOpenPanel openPanel];
      panel.title = @"Select Prepared Job Folders or References (1–64)";
      panel.canChooseFiles = YES;
      panel.canChooseDirectories = YES;
      panel.allowsMultipleSelection = YES;
      panel.allowedContentTypes = allowedTypes({"seamjob"});
      if ([panel runModal] != NSModalResponseOK) return Output{};
      if (panel.URLs.count == 0U || panel.URLs.count > 64U)
        return core::failure<Output>(core::ErrorCode::InvalidArgument, "Select 1 to 64 prepared job references");
      Output result;
      for (NSURL* url in panel.URLs) {
        auto path = pathFromUrl(url);
        if (url.hasDirectoryPath) path /= "job.seamjob";
        result.push_back(std::move(path));
      }
      return result;
    }
  }
  core::Result<std::optional<std::size_t>> chooseGenerationRegion(const std::vector<std::string>& regions) override {
    using Output = std::optional<std::size_t>;
    if (![NSThread isMainThread] || regions.empty() || regions.size() > 64U)
      return core::failure<Output>(core::ErrorCode::InvalidArgument, "Generation region choices are unavailable or oversized");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Choose Generation Region";
      alert.informativeText = @"Choose the saved procedural region for the selected producer pitch layer. Preparation does not generate or approve audio.";
      [alert addButtonWithTitle:@"Choose Destination"];
      [alert addButtonWithTitle:@"Cancel"];
      NSPopUpButton* choices = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 420, 28) pullsDown:NO];
      [choices setAccessibilityLabel:@"Generation score region"];
      for (const auto& region : regions) {
        NSString* label = nsString(region);
        if (region.empty() || region.size() > 512U || label == nil)
          return core::failure<Output>(core::ErrorCode::InvalidArgument, "Generation region label is invalid");
        [choices addItemWithTitle:label];
      }
      alert.accessoryView = choices;
      if ([alert runModal] != NSAlertFirstButtonReturn) return Output{};
      const auto selected = choices.indexOfSelectedItem;
      if (selected < 0 || static_cast<std::size_t>(selected) >= regions.size())
        return core::failure<Output>(core::ErrorCode::InvalidState, "No generation region selected");
      return Output{static_cast<std::size_t>(selected)};
    }
  }
  core::Result<std::optional<bool>> chooseRecipePackaging() override {
    if (![NSThread isMainThread]) return core::failure<std::optional<bool>>(core::ErrorCode::InvalidState,
        "Export packaging dialogs must run on the main thread");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Include editable project and recipes?";
      alert.informativeText = @"Audio Only exports the master and stems. Include Project adds the editable song and procedural recipe JSON files. Sample banks and backing audio files are not bundled.";
      [alert addButtonWithTitle:@"Audio Only"];
      [alert addButtonWithTitle:@"Include Project"];
      [alert addButtonWithTitle:@"Cancel"];
      const auto response = [alert runModal];
      if (response == NSAlertFirstButtonReturn) return std::optional<bool>{false};
      if (response == NSAlertSecondButtonReturn) return std::optional<bool>{true};
      return std::optional<bool>{};
    }
  }

  core::Result<std::optional<std::string>> chooseRecipeStyle(
      const std::vector<std::string>& styles) override {
    using Output = std::optional<std::string>;
    if (![NSThread isMainThread]) return core::failure<Output>(core::ErrorCode::InvalidState,
        "Recipe style dialogs must run on the main thread");
    if (styles.empty() || styles.size() > 64U) return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Recipe style choices are empty or oversized");
    @autoreleasepool {
      NSAlert* alert = [[NSAlert alloc] init];
      alert.messageText = @"Choose Recipe Style";
      alert.informativeText = @"Select the style to save for this track.";
      [alert addButtonWithTitle:@"Select"];
      [alert addButtonWithTitle:@"Cancel"];
      NSPopUpButton* choices = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 320, 28) pullsDown:NO];
      [choices setAccessibilityLabel:@"Recipe style"];
      for (const auto& style : styles) {
        NSString* label = nsString(style);
        if (style.empty() || style.size() > 128U || label == nil) return core::failure<Output>(
            core::ErrorCode::InvalidArgument, "Recipe style label is invalid");
        [choices addItemWithTitle:label];
      }
      alert.accessoryView = choices;
      if ([alert runModal] != NSAlertFirstButtonReturn) return Output{};
      const auto index = choices.indexOfSelectedItem;
      if (index < 0 || static_cast<std::size_t>(index) >= styles.size()) return core::failure<Output>(
          core::ErrorCode::InvalidState, "Recipe style selection is missing");
      return Output{styles[static_cast<std::size_t>(index)]};
    }
  }

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
                        request.purpose == FileDialogPurpose::ExportScore ||
                        request.purpose == FileDialogPurpose::ExportSet ||
                        request.purpose == FileDialogPurpose::BakeProceduralCandidates ||
                        request.purpose == FileDialogPurpose::ExportPitchInspection ||
                        request.purpose == FileDialogPurpose::PrepareGenerationJob ||
                        request.purpose == FileDialogPurpose::PrepareGenerationBatch ||
                        request.purpose == FileDialogPurpose::PlanGenerationCampaign ||
                        request.purpose == FileDialogPurpose::SaveDesignerRecipe ||
                        request.purpose == FileDialogPurpose::PublishSampleCandidate ||
                        request.purpose == FileDialogPurpose::CreateSampleManifestDraft;
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
            request.purpose == FileDialogPurpose::ExportSet ||
            request.purpose == FileDialogPurpose::BakeProceduralCandidates ||
            request.purpose == FileDialogPurpose::PlanGenerationCampaign;
      }
      if (openPanel != nil) {
        openPanel.canChooseFiles = !directory;
        openPanel.canChooseDirectories = directory;
        openPanel.canCreateDirectories = directory;
        openPanel.allowsMultipleSelection = NO;
      }
      if (runModalRestoringFocus(panel) != NSModalResponseOK || panel.URL == nil) {
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
