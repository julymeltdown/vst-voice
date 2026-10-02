#include "test_framework.hpp"

#include <limits>

namespace {

void requireNearRejected(double lhs, double rhs, double epsilon) {
  bool rejected = false;
  try {
    seam::test::checkNear(lhs, rhs, epsilon, "lhs ~= rhs", __FILE__, __LINE__);
  } catch (const seam::test::Failure&) {
    rejected = true;
  }
  CHECK(rejected);
}

}  // namespace

TEST_CASE("CHECK_NEAR rejects non-finite operands and invalid tolerances") {
  seam::test::checkNear(1.0, 1.0, 0.0, "equal finite values", __FILE__, __LINE__);
  seam::test::checkNear(1.25, 1.0, 0.25, "finite tolerance boundary", __FILE__, __LINE__);

  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  requireNearRejected(nan, 1.0, 1.0);
  requireNearRejected(1.0, nan, 1.0);
  requireNearRejected(infinity, infinity, 1.0);
  requireNearRejected(-infinity, -infinity, 1.0);
  requireNearRejected(1.0, 1.0, nan);
  requireNearRejected(1.0, 1.0, infinity);
  requireNearRejected(1.0, 1.0, -0.1);
}
