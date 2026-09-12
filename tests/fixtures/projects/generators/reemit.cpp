#include "seam/formats/project_json.hpp"

int main(int argc, char** argv) {
  if (argc != 3) return 1;
  seam::formats::ProjectJsonCodec codec;
  const auto source = codec.load(argv[1]);
  if (!source) return 2;
  if (!codec.save(source.value(), argv[2])) return 3;
  const auto output = codec.load(argv[2]);
  return output && output.value() == source.value() ? 0 : 4;
}
