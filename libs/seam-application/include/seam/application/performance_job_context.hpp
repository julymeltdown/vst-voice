#pragma once

#include "seam/domain/project.hpp"

#include <memory>
#include <utility>

namespace seam::application {
namespace detail {
struct PerformanceJobGeneration final {};
struct PerformanceJobReceipt final { bool consumed{false}; };
}

class EditorSession;

class PerformanceJobContext final {
public:
  [[nodiscard]] const domain::Project& sourceProject() const noexcept { return *source_; }

private:
  friend class EditorSession;
  PerformanceJobContext(std::shared_ptr<const domain::Project> source,
                        std::shared_ptr<const detail::PerformanceJobGeneration> generation,
                        std::shared_ptr<detail::PerformanceJobReceipt> receipt)
      : source_(std::move(source)), generation_(std::move(generation)), receipt_(std::move(receipt)) {}

  std::shared_ptr<const domain::Project> source_;
  std::shared_ptr<const detail::PerformanceJobGeneration> generation_;
  std::shared_ptr<detail::PerformanceJobReceipt> receipt_;
};

}
