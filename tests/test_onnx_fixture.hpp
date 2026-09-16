#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seam::test::onnx {

// --- ONNX graph fixtures -----------------------------------------------------------------------
// The product reads ONNX ModelProto bytes directly, so the tests build them directly too: the wire
// format is written here instead of taking a dependency on the ONNX SDK or a codegen tool. Every
// helper is a plain byte builder, so a test can also append a field the reader must refuse.

inline void protoVarint(std::string& out, std::uint64_t value) {
  while (value >= 0x80U) {
    out.push_back(static_cast<char>((value & 0x7FU) | 0x80U));
    value >>= 7U;
  }
  out.push_back(static_cast<char>(value));
}

inline void protoTag(std::string& out, std::uint32_t field, std::uint32_t wire) {
  protoVarint(out, (static_cast<std::uint64_t>(field) << 3U) | static_cast<std::uint64_t>(wire));
}

inline void protoVarintField(std::string& out, std::uint32_t field, std::uint64_t value) {
  protoTag(out, field, 0U);
  protoVarint(out, value);
}

inline void protoBytesField(std::string& out, std::uint32_t field, std::string_view value) {
  protoTag(out, field, 2U);
  protoVarint(out, value.size());
  out.append(value.data(), value.size());
}

// Each dimension is a decimal size or a symbolic name; "?" declares the dimension without a size.
inline std::string onnxValueInfo(std::string_view name, std::uint32_t elementType,
                                 const std::vector<std::string>& dimensions) {
  std::string shape;
  for (const auto& dimension : dimensions) {
    std::string entry;
    if (dimension == "?") {
      protoBytesField(entry, 2U, "unbounded");
    } else if (!dimension.empty() && std::isdigit(static_cast<unsigned char>(dimension.front())) != 0) {
      protoVarintField(entry, 1U, std::stoull(dimension));
    } else {
      protoBytesField(entry, 2U, dimension);
    }
    protoBytesField(shape, 1U, entry);
  }
  std::string tensor;
  protoVarintField(tensor, 1U, elementType);
  protoBytesField(tensor, 2U, shape);
  std::string type;
  protoBytesField(type, 1U, tensor);
  std::string value;
  protoBytesField(value, 1U, name);
  protoBytesField(value, 2U, type);
  return value;
}

inline std::string onnxNode(std::string_view opType, const std::vector<std::string>& inputs,
                            const std::vector<std::string>& outputs, std::string_view domain = {}) {
  std::string node;
  for (const auto& input : inputs) protoBytesField(node, 1U, input);
  for (const auto& output : outputs) protoBytesField(node, 2U, output);
  protoBytesField(node, 4U, opType);
  if (!domain.empty()) protoBytesField(node, 7U, domain);
  return node;
}

inline std::string onnxInitializer(std::string_view name, std::uint32_t elementType,
                                   const std::vector<std::string>& dimensions,
                                   std::size_t rawBytes = 0U) {
  std::string tensor;
  for (const auto& dimension : dimensions) protoVarintField(tensor, 1U, std::stoull(dimension));
  protoVarintField(tensor, 2U, elementType);
  protoBytesField(tensor, 8U, name);
  if (rawBytes != 0U) protoBytesField(tensor, 9U, std::string(rawBytes, '\0'));
  return tensor;
}

inline std::string onnxGraph(const std::vector<std::string>& inputs,
                             const std::vector<std::string>& outputs,
                             const std::vector<std::string>& nodes,
                             const std::vector<std::string>& initializers = {}) {
  std::string graph;
  for (const auto& node : nodes) protoBytesField(graph, 1U, node);
  for (const auto& initializer : initializers) protoBytesField(graph, 5U, initializer);
  for (const auto& input : inputs) protoBytesField(graph, 11U, input);
  for (const auto& output : outputs) protoBytesField(graph, 12U, output);
  return graph;
}

inline std::string onnxModel(std::string_view graph, std::uint32_t irVersion = 9U,
                             std::uint32_t opset = 17U, std::string_view producer = "seam-test",
                             const std::vector<std::pair<std::string, std::string>>& metadata = {}) {
  std::string opsetImport;
  protoVarintField(opsetImport, 2U, opset);
  std::string model;
  protoVarintField(model, 1U, irVersion);
  protoBytesField(model, 2U, producer);
  protoBytesField(model, 7U, graph);
  protoBytesField(model, 8U, opsetImport);
  for (const auto& [key, value] : metadata) {
    std::string entry;
    protoBytesField(entry, 1U, key);
    protoBytesField(entry, 2U, value);
    protoBytesField(model, 14U, entry);
  }
  return model;
}

// The acoustic graph emits the mel representation the vocoder consumes.
inline std::string onnxAcousticGraph(std::uint32_t bins = 80U, std::uint32_t elementType = 1U,
                                     const std::string& featureDimension = {},
                                     std::string_view producer = "seam-test",
                                     std::uint32_t irVersion = 9U, std::uint32_t opset = 17U,
                                     bool includeBreathiness = false,
                                     std::uint32_t breathinessElementType = 1U,
                                     const std::vector<std::string>& breathinessDimensions = {"1", "T"},
                                     std::string_view breathinessUnit = "normalized-periodic-aperiodic-balance",
                                     bool connectBreathiness = true) {
  const auto features = featureDimension.empty() ? std::to_string(bins) : featureDimension;
  std::vector<std::string> inputs{onnxValueInfo("phones", 7U, {"1", "T"})};
  if (includeBreathiness) {
    inputs.push_back(onnxValueInfo("breathiness", breathinessElementType, breathinessDimensions));
  }
  std::vector<std::string> nodes{onnxNode("MatMul", {"phones", "embedding"},
      {includeBreathiness && connectBreathiness ? "base_mel" : "mel"})};
  if (includeBreathiness && connectBreathiness)
    nodes.push_back(onnxNode("Add", {"base_mel", "breathiness"}, {"mel"}));
  std::vector<std::pair<std::string, std::string>> metadata;
  if (includeBreathiness) metadata = {
      {"seam.conditioning.revision", "2"},
      {"seam.conditioning.breathiness.type", "float32"},
      {"seam.conditioning.breathiness.unit", std::string{breathinessUnit}},
      {"seam.conditioning.breathiness.minimum", "0"},
      {"seam.conditioning.breathiness.maximum", "1"},
      {"seam.conditioning.breathiness.default", "0"},
      {"seam.conditioning.breathiness.supported", "true"},
  };
  return onnxModel(onnxGraph(
      inputs,
      {onnxValueInfo("mel", elementType, {"1", "T", features})},
      nodes,
      {onnxInitializer("embedding", 1U, {std::to_string(bins), "1"}, 4U * bins)}),
      irVersion, opset, producer, metadata);
}

inline std::string onnxVocoderGraph(std::uint32_t bins = 80U, std::uint32_t elementType = 1U,
                                    std::string_view outputName = "waveform",
                                    const std::string& outputChannels = "1",
                                    std::string_view producer = "seam-test",
                                    std::uint32_t irVersion = 9U, std::uint32_t opset = 17U) {
  return onnxModel(onnxGraph(
      {onnxValueInfo("mel", elementType, {"1", "T", std::to_string(bins)})},
      {onnxValueInfo(outputName, 1U, {outputChannels, "samples"})},
      {onnxNode("ConvTranspose", {"mel", "kernel"}, {std::string{outputName}})},
      {onnxInitializer("kernel", 1U, {"1", "1", "4"}, 4U)}),
      irVersion, opset, producer);
}

}  // namespace seam::test::onnx
