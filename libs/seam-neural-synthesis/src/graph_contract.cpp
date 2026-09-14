#include "seam/neural_synthesis/graph_contract.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>

namespace seam::neural_synthesis {
namespace {

// The operator set this build admits: the standard ONNX domain as used by the pinned first-party
// export family plus the primitives those graphs are built from. Membership is a deliberate
// decision, because the child that executes an admitted graph trusts it. The graph-bearing
// operators (If, Loop, Scan) are not in the set: their subgraphs are not inspected here, so
// admitting them would admit bytes nothing in this reader validated.
constexpr std::string_view kAdmittedOperators[]{
    "Abs", "Add", "And", "ArgMax", "ArgMin", "AveragePool", "BatchNormalization", "Cast", "Ceil",
    "Clip", "Concat", "Constant", "ConstantOfShape", "Conv", "ConvTranspose", "Cos", "CumSum",
    "DequantizeLinear", "Div", "Dropout", "Einsum", "Elu", "Equal", "Erf", "Exp", "Expand",
    "Flatten", "Floor", "Gather", "GatherElements", "GatherND", "Gemm", "GlobalAveragePool",
    "GlobalMaxPool", "Greater", "GreaterOrEqual", "GRU", "HardSigmoid", "HardSwish", "Identity",
    "InstanceNormalization", "LayerNormalization", "LeakyRelu", "Less", "LessOrEqual", "Log",
    "LogSoftmax", "LSTM", "MatMul", "Max", "MaxPool", "Mean", "Min", "Mod", "Mul", "Neg",
    "NonZero", "Not", "Or", "Pad", "Pow", "PRelu", "QuantizeLinear", "RNN", "Range", "Reciprocal",
    "ReduceL1", "ReduceL2", "ReduceLogSum", "ReduceLogSumExp", "ReduceMax", "ReduceMean",
    "ReduceMin", "ReduceProd", "ReduceSum", "ReduceSumSquare", "Relu", "Reshape", "Resize",
    "Round", "ScatterElements", "ScatterND", "Shape", "Sigmoid", "Sign", "Sin", "Size", "Slice",
    "Softmax", "Softplus", "Split", "Sqrt", "Squeeze", "Sub", "Sum", "Tanh", "Tile", "TopK",
    "Transpose", "Trilu", "Unsqueeze", "Where", "Xor"};

// The two AttributeProto fields that carry a subgraph. They are named separately so the refusal
// says what was found instead of reporting a generic unknown field.
constexpr std::uint32_t kAttributeGraphFields[]{6U, 11U};

core::Error truncated(std::string_view what) {
  return {core::ErrorCode::ParseError, std::string{what} + " is truncated or malformed"};
}

core::Error unknownField(std::string_view what, std::uint32_t field) {
  return {core::ErrorCode::ParseError,
      std::string{what} + " field " + std::to_string(field) + " is not admitted"};
}

core::Error refused(std::string_view what, std::string_view detail) {
  return {core::ErrorCode::Unsupported, std::string{what} + " " + std::string{detail}};
}

bool admits(std::string_view opType) noexcept {
  return std::find(std::begin(kAdmittedOperators), std::end(kAdmittedOperators), opType) !=
      std::end(kAdmittedOperators);
}

std::uint64_t elementBytes(std::uint32_t elementType) noexcept {
  switch (elementType) {
    case 1U: return 4U;   // float
    case 2U: return 1U;   // uint8
    case 3U: return 1U;   // int8
    case 4U: return 2U;   // uint16
    case 5U: return 2U;   // int16
    case 6U: return 4U;   // int32
    case 7U: return 8U;   // int64
    case 9U: return 1U;   // bool
    case 10U: return 2U;  // float16
    case 11U: return 8U;  // double
    case 12U: return 4U;  // uint32
    case 13U: return 8U;  // uint64
    case 16U: return 2U;  // bfloat16
    default: return 0U;
  }
}

struct Reader final {
  std::span<const std::byte> bytes;
  std::size_t position{0U};

  [[nodiscard]] bool done() const noexcept { return position >= bytes.size(); }
  [[nodiscard]] std::size_t remaining() const noexcept { return bytes.size() - position; }

  bool varint(std::uint64_t& value) noexcept {
    value = 0U;
    std::size_t shift = 0U;
    for (std::size_t index = 0U; index < 10U; ++index) {
      if (position >= bytes.size()) return false;
      const auto byte = std::to_integer<std::uint8_t>(bytes[position++]);
      if (shift < 64U) value |= static_cast<std::uint64_t>(byte & 0x7FU) << shift;
      else if ((byte & 0x7FU) != 0U) return false;  // A value wider than 64 bits is malformed.
      if ((byte & 0x80U) == 0U) return true;
      shift += 7U;
    }
    return false;
  }

  bool field(std::uint32_t& number, std::uint32_t& wire) noexcept {
    std::uint64_t key = 0U;
    if (!varint(key)) return false;
    number = static_cast<std::uint32_t>(key >> 3U);
    wire = static_cast<std::uint32_t>(key & 0x7U);
    return number != 0U && number <= 0x1FFFFFFFU;
  }

  bool submessage(std::span<const std::byte>& value) noexcept {
    std::uint64_t length = 0U;
    if (!varint(length)) return false;
    if (length > static_cast<std::uint64_t>(remaining())) return false;
    const auto size = static_cast<std::size_t>(length);
    value = bytes.subspan(position, size);
    position += size;
    return true;
  }

  bool text(std::string_view& value, std::size_t maximumBytes) noexcept {
    std::span<const std::byte> raw;
    if (!submessage(raw)) return false;
    if (raw.size() > maximumBytes) return false;
    value = std::string_view{reinterpret_cast<const char*>(raw.data()), raw.size()};
    return true;
  }

  bool skip(std::uint32_t wire) noexcept {
    switch (wire) {
      case 0U: { std::uint64_t ignored = 0U; return varint(ignored); }
      case 1U: if (remaining() < 8U) return false; position += 8U; return true;
      case 2U: { std::span<const std::byte> ignored; return submessage(ignored); }
      case 5U: if (remaining() < 4U) return false; position += 4U; return true;
      default: return false;  // Groups and reserved wire types are refused rather than skipped.
    }
  }
};

struct Budget final {
  std::size_t nodes{0U};
  std::size_t tensors{0U};
  std::uint64_t initializerBytes{0U};
};

core::Result<void> parseDimension(Reader& reader, GraphTensorContract& tensor,
    const GraphInspectionLimits& limits) {
  std::optional<std::int64_t> value;
  bool symbolic = false;
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX tensor shape dimension");
    if (field == 1U && wire == 0U) {
      std::uint64_t declared = 0U;
      if (!reader.varint(declared)) return truncated("ONNX tensor shape dimension");
      if (declared > 0x40000000ULL) return refused("ONNX tensor shape dimension", "exceeds its bound");
      value = static_cast<std::int64_t>(declared);
      continue;
    }
    if (field == 2U && wire == 2U) {
      std::string_view name;
      if (!reader.text(name, limits.maximumNameBytes)) return truncated("ONNX tensor shape dimension");
      symbolic = true;
      continue;
    }
    return unknownField("ONNX tensor shape dimension", field);
  }
  // A symbolic name, an absent size and a declared zero all mean the model did not bound this
  // dimension. The rank is still known, which is what a caller binds against.
  tensor.dimensions.push_back(symbolic || !value || *value <= 0 ? -1 : *value);
  return core::success();
}

core::Result<void> parseShape(Reader& reader, GraphTensorContract& tensor,
    const GraphInspectionLimits& limits) {
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX tensor shape");
    if (field != 1U || wire != 2U) return unknownField("ONNX tensor shape", field);
    if (tensor.dimensions.size() >= 8U) return refused("ONNX tensor shape", "declares more than eight dimensions");
    std::span<const std::byte> dimension;
    if (!reader.submessage(dimension)) return truncated("ONNX tensor shape");
    Reader nested{dimension};
    const auto parsed = parseDimension(nested, tensor, limits);
    if (!parsed) return parsed;
  }
  return core::success();
}

core::Result<void> parseTensorType(Reader& reader, GraphTensorContract& tensor,
    const GraphInspectionLimits& limits, std::size_t depth) {
  if (depth > limits.maximumDepth) return refused("ONNX tensor type", "exceeds the admitted nesting depth");
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX tensor type");
    if (field == 1U && wire == 0U) {
      std::uint64_t elementType = 0U;
      if (!reader.varint(elementType)) return truncated("ONNX tensor type");
      if (elementType == 0U || elementType > 16U) return refused("ONNX tensor type", "declares an unknown element type");
      tensor.elementType = static_cast<std::uint32_t>(elementType);
      continue;
    }
    if (field == 2U && wire == 2U) {
      std::span<const std::byte> shape;
      if (!reader.submessage(shape)) return truncated("ONNX tensor type");
      Reader nested{shape};
      const auto parsed = parseShape(nested, tensor, limits);
      if (!parsed) return parsed;
      continue;
    }
    if (field == 3U && wire == 2U) {
      std::string_view denotation;
      if (!reader.text(denotation, limits.maximumNameBytes)) return truncated("ONNX tensor type");
      continue;
    }
    return unknownField("ONNX tensor type", field);
  }
  return core::success();
}

core::Result<void> parseType(Reader& reader, GraphTensorContract& tensor,
    const GraphInspectionLimits& limits, std::size_t depth) {
  if (depth > limits.maximumDepth) return refused("ONNX type", "exceeds the admitted nesting depth");
  bool tensorType = false;
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX type");
    if (field == 1U && wire == 2U) {
      std::span<const std::byte> message;
      if (!reader.submessage(message)) return truncated("ONNX type");
      Reader nested{message};
      const auto parsed = parseTensorType(nested, tensor, limits, depth + 1U);
      if (!parsed) return parsed;
      tensorType = true;
      continue;
    }
    if (field == 6U && wire == 2U) {
      std::string_view denotation;
      if (!reader.text(denotation, limits.maximumNameBytes)) return truncated("ONNX type");
      continue;
    }
    // Sequence, map, optional and sparse types are not a tensor declaration this reader binds.
    if (wire == 2U && (field == 4U || field == 5U || field == 8U || field == 9U))
      return refused("ONNX type", "is not a plain tensor");
    return unknownField("ONNX type", field);
  }
  if (!tensorType) return refused("ONNX type", "does not declare a tensor");
  return core::success();
}

core::Result<void> parseValueInfo(Reader& reader, GraphTensorContract& tensor,
    const GraphInspectionLimits& limits, std::size_t depth) {
  if (depth > limits.maximumDepth) return refused("ONNX value info", "exceeds the admitted nesting depth");
  bool typed = false;
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX value info");
    if (field == 1U && wire == 2U) {
      std::string_view name;
      if (!reader.text(name, limits.maximumNameBytes)) return truncated("ONNX value info");
      tensor.name = std::string{name};
      continue;
    }
    if (field == 2U && wire == 2U) {
      std::span<const std::byte> type;
      if (!reader.submessage(type)) return truncated("ONNX value info");
      Reader nested{type};
      const auto parsed = parseType(nested, tensor, limits, depth + 1U);
      if (!parsed) return parsed;
      typed = true;
      continue;
    }
    if (field == 3U && wire == 2U) {
      std::string_view docString;
      if (!reader.text(docString, limits.maximumBytes)) return truncated("ONNX value info");
      continue;
    }
    if (field == 4U && wire == 2U) {
      if (!reader.skip(wire)) return truncated("ONNX value info");
      continue;
    }
    return unknownField("ONNX value info", field);
  }
  if (tensor.name.empty()) return refused("ONNX value info", "has no name");
  if (!typed) return refused("ONNX value info", "declares no tensor type");
  return core::success();
}

core::Result<void> parseTensor(Reader& reader, Budget& budget, const GraphInspectionLimits& limits) {
  std::uint32_t dataType = 0U;
  std::uint64_t elements = 1U;
  bool bounded = false;
  const auto dimension = [&](std::uint64_t declared) -> core::Result<void> {
    if (declared > 0x40000000ULL) return refused("ONNX initializer", "declares an oversized tensor");
    elements *= std::max<std::uint64_t>(declared, 1U);
    if (elements > 0x40000000ULL) return refused("ONNX initializer", "declares an oversized tensor");
    bounded = true;
    return core::success();
  };
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX initializer");
    if (field == 1U && wire == 0U) {
      std::uint64_t declared = 0U;
      if (!reader.varint(declared)) return truncated("ONNX initializer");
      const auto parsed = dimension(declared);
      if (!parsed) return parsed;
      continue;
    }
    if (field == 1U && wire == 2U) {
      std::span<const std::byte> packed;
      if (!reader.submessage(packed)) return truncated("ONNX initializer");
      Reader dimensions{packed};
      while (!dimensions.done()) {
        std::uint64_t declared = 0U;
        if (!dimensions.varint(declared)) return truncated("ONNX initializer");
        const auto parsed = dimension(declared);
        if (!parsed) return parsed;
      }
      continue;
    }
    if (field == 2U && wire == 0U) {
      std::uint64_t declared = 0U;
      if (!reader.varint(declared)) return truncated("ONNX initializer");
      if (declared == 0U || declared > 16U) return refused("ONNX initializer", "declares an unknown element type");
      dataType = static_cast<std::uint32_t>(declared);
      continue;
    }
    // A tensor this reader will not execute may not reach outside the bundle for its bytes.
    if (field == 13U) return refused("ONNX initializer", "declares external tensor data");
    if (field == 14U && wire == 0U) {
      std::uint64_t location = 0U;
      if (!reader.varint(location)) return truncated("ONNX initializer");
      if (location != 0U) return refused("ONNX initializer", "declares external tensor data");
      continue;
    }
    if (field == 3U) return refused("ONNX initializer", "declares a tensor segment");
    if (field == 6U) return refused("ONNX initializer", "declares string tensor data");
    if ((field == 8U || field == 12U) && wire == 2U) {
      std::string_view ignored;
      if (!reader.text(ignored, limits.maximumNameBytes)) return truncated("ONNX initializer");
      continue;
    }
    if (field == 9U || field == 4U || field == 5U || field == 7U || field == 10U || field == 11U ||
        field == 16U) {
      if (!reader.skip(wire)) return truncated("ONNX initializer");
      continue;
    }
    return unknownField("ONNX initializer", field);
  }
  if (dataType == 0U) return refused("ONNX initializer", "declares no element type");
  if (!bounded) elements = 1U;
  const auto width = elementBytes(dataType);
  if (width == 0U) return refused("ONNX initializer", "declares an unbounded element width");
  if (elements > (limits.maximumInitializerBytes / width))
    return refused("ONNX initializer", "exceeds the admitted tensor budget");
  budget.initializerBytes += elements * width;
  if (budget.initializerBytes > limits.maximumInitializerBytes)
    return refused("ONNX initializer", "exceeds the admitted tensor budget");
  ++budget.tensors;
  return core::success();
}

core::Result<void> parseAttribute(Reader& reader, const GraphInspectionLimits& limits) {
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX operator attribute");
    for (const auto graphField : kAttributeGraphFields)
      if (field == graphField)
        return refused("ONNX operator attribute", "carries a subgraph this reader does not admit");
    if ((field == 1U || field == 4U || field == 13U || field == 21U) && wire == 2U) {
      std::string_view ignored;
      if (!reader.text(ignored, limits.maximumBytes)) return truncated("ONNX operator attribute");
      continue;
    }
    if (field == 2U || field == 3U || field == 5U || field == 6U || field == 7U || field == 8U ||
        field == 9U || field == 10U || field == 14U || field == 15U || field == 20U ||
        field == 22U || field == 23U) {
      if (!reader.skip(wire)) return truncated("ONNX operator attribute");
      continue;
    }
    return unknownField("ONNX operator attribute", field);
  }
  return core::success();
}

core::Result<void> parseNode(Reader& reader, GraphContract& contract, Budget& budget,
    const GraphInspectionLimits& limits, std::size_t depth) {
  if (depth > limits.maximumDepth) return refused("ONNX operator", "exceeds the admitted nesting depth");
  if (budget.nodes >= limits.maximumNodes) return refused("ONNX graph", "declares more operators than admitted");
  ++budget.nodes;
  std::string opType, domain;
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX operator");
    if ((field == 1U || field == 2U) && wire == 2U) {
      std::string_view ignored;
      if (!reader.text(ignored, limits.maximumNameBytes)) return truncated("ONNX operator");
      continue;
    }
    if (field == 4U && wire == 2U) {
      std::string_view value;
      if (!reader.text(value, limits.maximumNameBytes)) return truncated("ONNX operator");
      opType = std::string{value};
      continue;
    }
    if (field == 7U && wire == 2U) {
      std::string_view value;
      if (!reader.text(value, limits.maximumNameBytes)) return truncated("ONNX operator");
      domain = std::string{value};
      continue;
    }
    if (field == 5U && wire == 2U) {
      std::span<const std::byte> attribute;
      if (!reader.submessage(attribute)) return truncated("ONNX operator");
      Reader nested{attribute};
      const auto parsed = parseAttribute(nested, limits);
      if (!parsed) return parsed;
      continue;
    }
    if ((field == 3U || field == 6U) && wire == 2U) {
      std::string_view text;
      if (!reader.text(text, limits.maximumBytes)) return truncated("ONNX operator");
      continue;
    }
    return unknownField("ONNX operator", field);
  }
  if (opType.empty()) return refused("ONNX operator", "declares no operator type");
  if (!domain.empty() && domain != "ai.onnx")
    return refused("ONNX operator", "declares a custom operator domain");
  if (!admits(opType)) return refused("ONNX operator", "is not in the admitted operator set");
  if (std::find(contract.operators.begin(), contract.operators.end(), opType) == contract.operators.end())
    contract.operators.push_back(opType);
  return core::success();
}

core::Result<void> parseGraph(Reader& reader, GraphContract& contract, Budget& budget,
    const GraphInspectionLimits& limits, std::size_t depth) {
  if (depth > limits.maximumDepth) return refused("ONNX graph", "exceeds the admitted nesting depth");
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX graph");
    if (field == 1U && wire == 2U) {
      std::span<const std::byte> node;
      if (!reader.submessage(node)) return truncated("ONNX graph");
      Reader nested{node};
      const auto parsed = parseNode(nested, contract, budget, limits, depth + 1U);
      if (!parsed) return parsed;
      continue;
    }
    if (field == 5U && wire == 2U) {
      std::span<const std::byte> initializer;
      if (!reader.submessage(initializer)) return truncated("ONNX graph");
      if (budget.tensors >= limits.maximumTensors) return refused("ONNX graph", "declares more tensors than admitted");
      Reader nested{initializer};
      const auto parsed = parseTensor(nested, budget, limits);
      if (!parsed) return parsed;
      ++contract.initializerCount;
      continue;
    }
    if ((field == 11U || field == 12U || field == 13U) && wire == 2U) {
      std::span<const std::byte> value;
      if (!reader.submessage(value)) return truncated("ONNX graph");
      if (budget.tensors >= limits.maximumTensors) return refused("ONNX graph", "declares more tensors than admitted");
      ++budget.tensors;
      GraphTensorContract tensor;
      Reader nested{value};
      const auto parsed = parseValueInfo(nested, tensor, limits, depth + 1U);
      if (!parsed) return parsed;
      if (field == 13U) continue;  // A shape declaration adds no input or output of its own.
      auto& target = field == 11U ? contract.inputs : contract.outputs;
      const auto duplicate = std::find_if(target.begin(), target.end(),
          [&](const GraphTensorContract& existing) { return existing.name == tensor.name; });
      if (duplicate != target.end()) return refused("ONNX graph", "declares a duplicate tensor name");
      target.push_back(std::move(tensor));
      continue;
    }
    if ((field == 2U || field == 10U) && wire == 2U) {
      std::string_view text;
      if (!reader.text(text, limits.maximumBytes)) return truncated("ONNX graph");
      continue;
    }
    if (field == 14U || field == 15U)
      return refused("ONNX graph", "declares quantization or sparse data this reader does not admit");
    return unknownField("ONNX graph", field);
  }
  if (contract.inputs.empty()) return refused("ONNX graph", "declares no input");
  if (contract.outputs.empty()) return refused("ONNX graph", "declares no output");
  return core::success();
}

core::Result<void> parseOperatorSet(Reader& reader, GraphContract& contract,
    const GraphInspectionLimits& limits) {
  bool seen = false;
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX operator set");
    if (field == 1U && wire == 2U) {
      std::string_view domain;
      if (!reader.text(domain, limits.maximumNameBytes)) return truncated("ONNX operator set");
      if (!domain.empty() && domain != "ai.onnx") return refused("ONNX operator set", "declares a custom domain");
      continue;
    }
    if (field == 2U && wire == 0U) {
      std::uint64_t version = 0U;
      if (!reader.varint(version)) return truncated("ONNX operator set");
      if (version < 13U || version > 21U) return refused("ONNX operator set", "declares an unsupported version");
      contract.opset = static_cast<std::uint32_t>(version);
      seen = true;
      continue;
    }
    return unknownField("ONNX operator set", field);
  }
  if (!seen) return refused("ONNX operator set", "declares no version");
  return core::success();
}

core::Result<void> parseMetadata(Reader& reader, const GraphInspectionLimits& limits) {
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX model metadata");
    if ((field == 1U || field == 2U) && wire == 2U) {
      std::string_view text;
      if (!reader.text(text, limits.maximumBytes)) return truncated("ONNX model metadata");
      continue;
    }
    return unknownField("ONNX model metadata", field);
  }
  return core::success();
}

core::Result<void> parseModel(Reader& reader, GraphContract& contract, Budget& budget,
    const GraphInspectionLimits& limits) {
  bool graph = false, opset = false;
  while (!reader.done()) {
    std::uint32_t field = 0U, wire = 0U;
    if (!reader.field(field, wire)) return truncated("ONNX model");
    if (field == 1U && wire == 0U) {
      std::uint64_t version = 0U;
      if (!reader.varint(version)) return truncated("ONNX model");
      if (version < 1U || version > 10U) return refused("ONNX model", "declares an unsupported IR version");
      contract.irVersion = static_cast<std::uint32_t>(version);
      continue;
    }
    if ((field == 2U || field == 3U || field == 4U) && wire == 2U) {
      std::string_view value;
      if (!reader.text(value, limits.maximumNameBytes)) return truncated("ONNX model");
      if (field == 2U) contract.producer = std::string{value};
      continue;
    }
    if (field == 5U && wire == 0U) {
      std::uint64_t ignored = 0U;
      if (!reader.varint(ignored)) return truncated("ONNX model");
      continue;
    }
    if (field == 6U && wire == 2U) {
      std::string_view text;
      if (!reader.text(text, limits.maximumBytes)) return truncated("ONNX model");
      continue;
    }
    if (field == 7U && wire == 2U) {
      if (graph) return refused("ONNX model", "declares more than one graph");
      std::span<const std::byte> message;
      if (!reader.submessage(message)) return truncated("ONNX model");
      Reader nested{message};
      const auto parsed = parseGraph(nested, contract, budget, limits, 1U);
      if (!parsed) return parsed;
      graph = true;
      continue;
    }
    if (field == 8U && wire == 2U) {
      if (opset) return refused("ONNX model", "declares more than one operator set");
      std::span<const std::byte> message;
      if (!reader.submessage(message)) return truncated("ONNX model");
      Reader nested{message};
      const auto parsed = parseOperatorSet(nested, contract, limits);
      if (!parsed) return parsed;
      opset = true;
      continue;
    }
    if (field == 14U && wire == 2U) {
      std::span<const std::byte> message;
      if (!reader.submessage(message)) return truncated("ONNX model");
      Reader nested{message};
      const auto parsed = parseMetadata(nested, limits);
      if (!parsed) return parsed;
      continue;
    }
    // Training info, functions and anything else are refused rather than carried along.
    return unknownField("ONNX model", field);
  }
  if (!graph) return refused("ONNX model", "declares no graph");
  if (!opset) return refused("ONNX model", "declares no operator set");
  return core::success();
}

}  // namespace

std::size_t GraphTensorContract::dynamicDimensions() const noexcept {
  return static_cast<std::size_t>(
      std::count(dimensions.begin(), dimensions.end(), std::int64_t{-1}));
}

const GraphTensorContract* GraphContract::findInput(std::string_view name) const noexcept {
  const auto found = std::find_if(inputs.begin(), inputs.end(),
      [&](const GraphTensorContract& tensor) { return tensor.name == name; });
  return found == inputs.end() ? nullptr : &*found;
}

const GraphTensorContract* GraphContract::findOutput(std::string_view name) const noexcept {
  const auto found = std::find_if(outputs.begin(), outputs.end(),
      [&](const GraphTensorContract& tensor) { return tensor.name == name; });
  return found == outputs.end() ? nullptr : &*found;
}

bool isAdmittedOperator(std::string_view opType) noexcept { return admits(opType); }

bool isFloatTensorElementType(std::uint32_t elementType) noexcept {
  return elementType == 1U || elementType == 10U || elementType == 11U || elementType == 16U;
}

std::string_view tensorElementTypeName(std::uint32_t elementType) noexcept {
  switch (elementType) {
    case 1U: return "float32";
    case 2U: return "uint8";
    case 3U: return "int8";
    case 4U: return "uint16";
    case 5U: return "int16";
    case 6U: return "int32";
    case 7U: return "int64";
    case 9U: return "bool";
    case 10U: return "float16";
    case 11U: return "float64";
    case 12U: return "uint32";
    case 13U: return "uint64";
    case 16U: return "bfloat16";
    default: return "unknown";
  }
}

core::Result<GraphContract> inspectNeuralGraph(std::span<const std::byte> graph,
    const GraphInspectionLimits& limits) {
  using Output = GraphContract;
  if (graph.empty())
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "Neural graph payload is empty");
  if (graph.size() > limits.maximumBytes)
    return core::failure<Output>(core::ErrorCode::Unsupported, "Neural graph exceeds the admitted byte bound");
  GraphContract contract;
  Budget budget;
  Reader reader{graph};
  const auto parsed = parseModel(reader, contract, budget, limits);
  if (!parsed) return core::Result<Output>{parsed.error()};
  std::sort(contract.operators.begin(), contract.operators.end());
  contract.nodeCount = budget.nodes;
  contract.initializerBytes = budget.initializerBytes;
  return core::success(std::move(contract));
}

}  // namespace seam::neural_synthesis
