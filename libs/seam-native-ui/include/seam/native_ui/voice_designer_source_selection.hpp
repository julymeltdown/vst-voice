#pragma once
#include "seam/voice_design/voice_recipe.hpp"
#include "seam/phonemizer/phonemizer.hpp"
#include <optional>

namespace seam::native_ui {
struct DesignerNoisePreviewAvailability final { bool source{false}, context{false}; };
[[nodiscard]] inline DesignerNoisePreviewAvailability designerNoisePreviewAvailability(
    const voice_design::VoiceRecipe& recipe,std::size_t pose,std::size_t source,bool plosive) {
  if (pose>=recipe.poses.size() || source>=(plosive?recipe.plosives.size():recipe.frications.size())) return {};
  const auto& style=plosive?recipe.plosives[source].style:recipe.frications[source].style;
  if (style!=recipe.poses[pose].style) return {};
  return {plosive || !recipe.frications[source].voicingGain.has_value(),phonemizer::isVowelSymbol(recipe.poses[pose].phone)};
}
// UI zero means explicit removal of the voiced component, not invalid schema-5 gain zero.
inline void setDesignerFricationVoicing(voice_design::VoiceRecipe::FricationPose& pose,double gain) {
  if (gain==0.0) pose.voicingGain.reset(); else pose.voicingGain=gain;
}
struct DesignerSourceSelection final { std::size_t pose, control; };
// Resolve a recipe-global source index into the style-filtered control list.
// Retain the current pose when its style matches; otherwise use the first match.
[[nodiscard]] inline std::optional<DesignerSourceSelection> designerSourceSelection(
    const voice_design::VoiceRecipe& recipe, std::size_t currentPose,
    std::size_t sourceIndex, bool plosive) {
  if (sourceIndex >= (plosive ? recipe.plosives.size() : recipe.frications.size())) return {};
  const auto& style=plosive?recipe.plosives[sourceIndex].style:recipe.frications[sourceIndex].style;
  auto pose=currentPose;
  if (pose>=recipe.poses.size() || recipe.poses[pose].style!=style) {
    pose=0U;
    while (pose<recipe.poses.size() && recipe.poses[pose].style!=style) ++pose;
    if (pose==recipe.poses.size()) return {};
  }
  std::size_t control=8U+recipe.poses[pose].formants.size()*3U;
  for (std::size_t i=0U;i<(plosive?recipe.frications.size():sourceIndex);++i)
    if (recipe.frications[i].style==style) control+=4U;
  if (plosive) for (std::size_t i=0U;i<sourceIndex;++i)
    if (recipe.plosives[i].style==style) control+=4U;
  return DesignerSourceSelection{pose,control};
}
}
