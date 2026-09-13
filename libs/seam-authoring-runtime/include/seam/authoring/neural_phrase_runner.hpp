#pragma once

#include "seam/core/result.hpp"
#include "seam/neural_synthesis/neural_phrase_backend.hpp"
#include "seam/rendering/render_pipeline.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>

namespace seam::authoring {

// Application-owned execution selection for one admitted bundle directory.
// The helper path, its expected digest, the process budgets and the canonical
// bundle location are all chosen above the bank by the trusted deployment owner.
struct NeuralPhraseRunnerOptions final {
  std::filesystem::path bundleDirectory;
  std::size_t maximumBundleBytes{0U};
  neural_synthesis::NeuralWorkerRunOptions worker{};
  // Explicit vocabulary symbol used for silence. A missing symbol is refused
  // rather than guessed, so pronunciation never invents a token.
  std::string silencePhone{"SP"};
  [[nodiscard]] core::Result<void> validate() const;
};

// Executes one prepared neural snapshot through the selected first-party helper.
// It owns no musical decision: the snapshot owns score, pronunciation window and
// the admitted bundle identity, and this runner only turns that into an admitted
// request and a validated phrase result. Never callable from the audio callback.
class AuthoringNeuralPhraseRunner final : public rendering::NeuralPhraseRunner {
public:
  // Reads and binds the canonical manifest digest of the selected directory so a
  // later snapshot cannot be rendered through a different bundle. Cancellation
  // before construction leaves no runner at all.
  [[nodiscard]] static core::Result<AuthoringNeuralPhraseRunner> create(
      NeuralPhraseRunnerOptions options,std::stop_token stop = {});
  [[nodiscard]] core::Result<synthesis::PhraseRenderResult> render(
      const rendering::RenderSnapshot& snapshot,std::stop_token stopToken) const override;
  // Digest of the bundle directory this runner will hand to the child.
  [[nodiscard]] const std::string& bundleContentHash() const noexcept {return bundleContentHash_;}
private:
  AuthoringNeuralPhraseRunner(NeuralPhraseRunnerOptions options,std::string bundleContentHash)
      : options_(std::move(options)),bundleContentHash_(std::move(bundleContentHash)) {}
  NeuralPhraseRunnerOptions options_;
  std::string bundleContentHash_;
  // Shared so the runner stays movable into a Result; the counter itself is
  // never copied between runners.
  mutable std::shared_ptr<std::atomic<std::uint64_t>> nextRequestId_{
      std::make_shared<std::atomic<std::uint64_t>>(1U)};
};

}  // namespace seam::authoring
