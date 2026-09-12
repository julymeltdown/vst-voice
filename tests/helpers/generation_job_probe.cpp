#include "seam/authoring/generation_job.hpp"
#include "signal_cancellation.hpp"
#include <charconv>
#include <cstdlib>
#include <string_view>

int main(int argc, char** argv) {
  if (argc != 4) return 1;
  const std::string_view value{argv[3]};
  if (value == "sigint" || value == "sigterm") {
    seam::voicebank_cli::SignalCancellation cancellation;
    if (!cancellation.install()) return 1;
    const auto deliver = [&] {
      if (std::raise(value == "sigint" ? SIGINT : SIGTERM) != 0) return false;
      for (unsigned i = 0U; i < 1000U && !cancellation.token().stop_requested(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
      return cancellation.token().stop_requested();
    };
    if (value == "sigint" && !deliver()) return 1;
    bool delivered = value == "sigint";
    const auto result = seam::authoring::runGenerationJob(argv[1], argv[2], cancellation.token(), [&](auto phase) {
      if (value == "sigterm" && phase == seam::authoring::ExportPublicationPhase::JournalPrepared) delivered = deliver();
      return false;
    });
    return delivered && !result && result.error().code == seam::core::ErrorCode::Conflict ? 0 : 1;
  }
  unsigned phase = 0U;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), phase);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || phase > 4U) return 1;
  const auto result = seam::authoring::runGenerationJob(argv[1], argv[2], {}, [phase](auto current) {
    if (static_cast<unsigned>(current) == phase) std::_Exit(86);
    return false;
  });
  return result ? 0 : 1;
}
