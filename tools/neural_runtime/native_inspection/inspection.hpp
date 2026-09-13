#pragma once
#include "onnx/onnx.pb.h"
#include <google/protobuf/struct.pb.h>
#include <span>

// Development structural checker, not production execution admission.
// Parses only supplied bytes; never reopens a graph or external tensor path.
// Outputs are replaced only on success. Input must remain alive for the call.
int inspectBytes(std::span<const char> bytes,onnx::ModelProto& model,
                 google::protobuf::Struct& report);
int runInspectionCommand(int argc,char** argv);
