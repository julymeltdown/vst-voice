#pragma once
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank/wav.hpp"

namespace seam::native_ui {
enum class PlosiveAuditionMode { Source, StopVowel, VowelStop };
enum class FricationAuditionMode { Source, FricationVowel, VowelFrication };
// One-second context with a 150 ms frication gesture, using the phrase renderer.
[[nodiscard]] core::Result<voicebank::AudioBuffer> renderDesignerFricationPhraseAudition(
    const synthesis::ProceduralSingerResource& resource, std::size_t index,
    std::size_t vowelPoseIndex, std::uint8_t midiKey = 69U, std::stop_token stopToken = {},
    FricationAuditionMode mode = FricationAuditionMode::FricationVowel);
// One-second stop/vowel context through the production articulation renderer.
[[nodiscard]] core::Result<voicebank::AudioBuffer> renderDesignerPlosivePhraseAudition(
    const synthesis::ProceduralSingerResource& resource, std::size_t index,
    std::size_t vowelPoseIndex, std::uint8_t midiKey = 69U, std::stop_token stopToken = {},
    PlosiveAuditionMode mode = PlosiveAuditionMode::StopVowel);
// Half-second source-only preview: 50 ms closure, authored burst, then silence.
// This is not a stop/vowel pronunciation audition or acoustic qualification.
[[nodiscard]] core::Result<voicebank::AudioBuffer> renderDesignerPlosiveAudition(
    const synthesis::ProceduralSingerResource& resource, std::size_t index, std::stop_token stopToken = {});
// One-second sustained oral-pose preview at 48 kHz. No file IO, source approval,
// song state, bank assignment or claim of phrase intelligibility.
[[nodiscard]] core::Result<voicebank::AudioBuffer> renderDesignerAudition(
    const synthesis::ProceduralSingerResource& resource, std::size_t poseIndex,
    std::uint8_t midiKey = 69U, std::stop_token stopToken = {});
[[nodiscard]] core::Result<voicebank::AudioBuffer> renderDesignerFricationAudition(
    const synthesis::ProceduralSingerResource& resource, std::size_t index, std::stop_token stopToken = {});
}
