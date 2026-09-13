#include "inspection.hpp"
#include <iostream>
int main(int argc,char** argv) {
  const auto result=runInspectionCommand(argc,argv);
  if (result) std::cerr<<"Native ONNX inspection failed (code "<<result<<")\n";
  return result;
}
