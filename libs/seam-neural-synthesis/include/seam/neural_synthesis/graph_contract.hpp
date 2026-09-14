#pragma once

#include "seam/core/result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace seam::neural_synthesis {

// Bounds for one graph inspection. A graph file is untrusted input: it is parsed with explicit
// byte, depth, node, tensor and initializer budgets, and no length the file declares is allocated
// before it has been checked against what is actually left. Nothing here is an operating-system
// sandbox: it is an input-admission policy over bytes this application already holds.
struct GraphInspectionLimits final {
  std::size_t maximumBytes{256U * 1024U};
  std::size_t maximumDepth{12U};
  std::size_t maximumNodes{200000U};
  std::size_t maximumTensors{65536U};
  std::size_t maximumNameBytes{256U};
  std::uint64_t maximumInitializerBytes{512ULL * 1024ULL * 1024ULL};
};

// One declared graph tensor. A dimension the model does not bound is recorded as -1, so the rank is
// always known even where a size is not; a caller that needs a bound refuses on that count rather
// than assuming one. Element types are ONNX TensorProto values.
struct GraphTensorContract final {
  std::string name;
  std::uint32_t elementType{0U};
  std::vector<std::int64_t> dimensions;
  [[nodiscard]] std::size_t rank() const noexcept { return dimensions.size(); }
  [[nodiscard]] std::size_t dynamicDimensions() const noexcept;
  friend bool operator==(const GraphTensorContract&, const GraphTensorContract&) = default;
};

// What a graph file actually declares, read from its own bytes rather than from a side declaration:
// the IR and operator-set revisions, the operator set used, and every graph input and output with
// its element type and shape.
//
// The shape is closed. Unknown fields in any message are refused instead of skipped, so a second
// representation cannot ride along unnoticed. Tensor data is refused as external data outright: an
// admitted graph is self-contained. Only the standard ONNX operator domains are admitted, and an
// operator outside the admitted set -- including the graph-bearing operators, whose subgraphs this
// reader does not inspect -- is refused by name. A contract says what a graph declares, not that
// the graph is correct, useful, trained or safe to execute.
struct GraphContract final {
  std::uint32_t irVersion{0U};
  std::uint32_t opset{0U};
  std::string producer;
  // Unique and sorted; a non-standard domain is reported as "domain:operator".
  std::vector<std::string> operators;
  std::vector<GraphTensorContract> inputs, outputs;
  std::size_t nodeCount{0U}, initializerCount{0U};
  std::uint64_t initializerBytes{0U};
  [[nodiscard]] const GraphTensorContract* findInput(std::string_view name) const noexcept;
  [[nodiscard]] const GraphTensorContract* findOutput(std::string_view name) const noexcept;
};

[[nodiscard]] core::Result<GraphContract> inspectNeuralGraph(std::span<const std::byte> graph,
    const GraphInspectionLimits& limits = {});
// The operators this build admits. Extending the set is a deliberate change with its own review,
// because a graph is executed by a first-party child that trusts this decision.
[[nodiscard]] bool isAdmittedOperator(std::string_view opType) noexcept;
[[nodiscard]] bool isFloatTensorElementType(std::uint32_t elementType) noexcept;
[[nodiscard]] std::string_view tensorElementTypeName(std::uint32_t elementType) noexcept;

}  // namespace seam::neural_synthesis
