// Linkage probe for the static Protobuf/Abseil closure.
//
// The shipped neural helper must resolve its runtime from beside itself, so it
// cannot link Homebrew's shared protobuf or abseil. This probe exists to prove
// that a statically linked Protobuf actually closes: it uses the generated
// descriptor API (which pulls in the runtime, abseil and utf8 validation),
// serializes and re-parses a message, and reports the result. The packaging side
// then reads this binary's own linkage with tools/phase13a/runtime_closure.py.
//
// It executes no model and grants no admission; it is a linkage fixture.
#include <google/protobuf/descriptor.pb.h>

#include <cstdio>
#include <string>

int main() {
  google::protobuf::FileDescriptorProto value;
  value.set_name("seam.static.probe");
  value.set_package("seam.static.probe");
  value.set_syntax("proto3");
  std::string encoded;
  if (!value.SerializeToString(&encoded) || encoded.empty()) {
    std::fprintf(stderr, "static protobuf probe failed to serialize\n");
    return 1;
  }
  google::protobuf::FileDescriptorProto decoded;
  if (!decoded.ParseFromString(encoded) || decoded.name() != value.name() ||
      decoded.package() != value.package()) {
    std::fprintf(stderr, "static protobuf probe failed to round-trip\n");
    return 1;
  }
  std::printf("static protobuf probe round-tripped %zu bytes\n", encoded.size());
  return 0;
}
