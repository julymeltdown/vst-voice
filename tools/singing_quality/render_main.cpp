#include "render_driver.hpp"

#include <exception>
#include <iostream>
#include <string_view>

namespace {

void help() {
  std::cout << "seam_singing_quality_render PROJECT MANIFEST AUDIO_LOCK OUTPUT_DIRECTORY bank|raw\n"
               "Renders a diagnostic dry vocal with frozen audio, timing and fallback records.\n"
               "Use the Python singing_quality runner to verify and stage the full corpus first.\n"
               "OUTPUT_DIRECTORY must not exist. This command makes no musical approval claim.\n";
}

int execute(int argc, char* argv[]) {
  if (argc == 2 && std::string_view{argv[1]} == "--help") {
    help();
    return 0;
  }
  if (argc != 6) {
    help();
    std::cerr << "singing-quality: expected PROJECT MANIFEST AUDIO_LOCK OUTPUT_DIRECTORY bank|raw\n";
    return 2;
  }
  const std::string_view policy{argv[5]};
  if (policy != "bank" && policy != "raw") {
    std::cerr << "singing-quality: renderer policy must be bank or raw\n";
    return 2;
  }
  const seam::singing_quality::Invocation invocation{
      argv[1], argv[2], argv[3], argv[4],
      policy == "bank" ? seam::synthesis::RenderPolicy::RespectVoicebank
                       : seam::synthesis::RenderPolicy::ForceRaw};
  const auto prepared = seam::singing_quality::prepare(invocation);
  if (!prepared) {
    std::cerr << "singing-quality: " << prepared.error().message
              << " (" << prepared.error().context << ")\n";
    return 3;
  }
  const auto rendered = seam::singing_quality::renderPacket(prepared.value(), invocation);
  if (!rendered) {
    std::cerr << "singing-quality: " << rendered.error().message
              << " (" << rendered.error().context << ")\n";
    return 4;
  }
  std::cout << invocation.output << '\n';
  return 0;
}

}

int main(int argc, char* argv[]) {
  try {
    return execute(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "singing-quality: " << error.what() << '\n';
    return 5;
  }
}
