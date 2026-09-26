#pragma once

// Objective-C++ only: one SemanticNode -> NSAccessibility mapping shared by the standalone window
// and the embedded CLAP view, so the two presenters cannot disagree about roles or values.
#if !defined(__OBJC__)
#error "accessibility_appkit_mapping.hpp is Objective-C++ only"
#endif

#import <AppKit/AppKit.h>

#include "seam/native_ui/editor_semantics.hpp"

#include <algorithm>

namespace seam::native_ui::appkit_ax {

[[nodiscard]] inline bool hasAction(const SemanticNode& node, SemanticAction action) noexcept {
  return std::find(node.actions.begin(), node.actions.end(), action) != node.actions.end();
}

[[nodiscard]] inline NSAccessibilityRole role(const SemanticNode& node) {
  switch (node.role) {
    case SemanticRole::Window: return NSAccessibilityWindowRole;
    case SemanticRole::Toolbar: return NSAccessibilityToolbarRole;
    case SemanticRole::Button: return NSAccessibilityButtonRole;
    case SemanticRole::TextField: return NSAccessibilityTextFieldRole;
    case SemanticRole::Note:
      return hasAction(node, SemanticAction::EditText) ? NSAccessibilityTextFieldRole
                                                       : NSAccessibilityGroupRole;
    case SemanticRole::Timeline:
    case SemanticRole::Lane:
    case SemanticRole::Panel:
    case SemanticRole::Status: return NSAccessibilityGroupRole;
    case SemanticRole::Slider: return NSAccessibilitySliderRole;
    case SemanticRole::RadioButton:
    case SemanticRole::Tab: return NSAccessibilityRadioButtonRole;
    case SemanticRole::ProgressIndicator: return NSAccessibilityProgressIndicatorRole;
    case SemanticRole::CheckBox: return NSAccessibilityCheckBoxRole;
  }
  return NSAccessibilityGroupRole;
}

[[nodiscard]] inline NSAccessibilitySubrole subrole(const SemanticNode& node) {
  return node.role == SemanticRole::Tab ? NSAccessibilityTabButtonSubrole : nil;
}

// Sliders and progress report numbers; radio buttons, tabs and check boxes report 1/0.
[[nodiscard]] inline NSNumber* numericValue(const SemanticNode& node) {
  if (node.numericValue.has_value()) return @(*node.numericValue);
  if (node.role == SemanticRole::RadioButton || node.role == SemanticRole::Tab ||
      node.role == SemanticRole::CheckBox)
    return @(node.selected ? 1 : 0);
  return nil;
}

[[nodiscard]] inline NSNumber* numericMinimum(const SemanticNode& node) {
  return node.numericMinimum.has_value() ? @(*node.numericMinimum) : nil;
}

[[nodiscard]] inline NSNumber* numericMaximum(const SemanticNode& node) {
  return node.numericMaximum.has_value() ? @(*node.numericMaximum) : nil;
}

}  // namespace seam::native_ui::appkit_ax
