#include "test_framework.hpp"

int main(int argc, char** argv) {
  if (argc > 2) {
    std::cerr << "Usage: seam_tests [test-name-substring]\n";
    return 2;
  }
  return seam::test::runAll(argc == 2 ? argv[1] : "");
}
