#include "test_framework.hpp"
#include "async_test_support.hpp"

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

namespace {
struct PollResult {
  bool ok;
  int error{0};
  explicit operator bool() const { return ok; }
};
struct FakeTime {
  std::chrono::steady_clock::time_point value{};
  auto now() const { return value; }
  void advance(int milliseconds=1) { value += std::chrono::milliseconds{milliseconds}; }
};
}

TEST_CASE("Async functional wait preserves terminal success and real refusal results") {
  for (const bool success : {true,false}) {
    FakeTime time;int polls=0;bool busy=true;
    const auto result=seam::test::drainAsyncOperation([&] {
      busy=++polls<3;
      return PollResult{busy || success, busy || success ? 0 : 42};
    },[&] { return busy; },std::chrono::milliseconds{5},"fixture",
      [&] { return time.now(); },[&] { time.advance(); });
    CHECK(polls==3);CHECK(!busy);CHECK(result.ok==success);CHECK(result.error==(success?0:42));
  }
}

TEST_CASE("Async functional wait cannot confuse a deadline with an expected refusal") {
  FakeTime time;int polls=0;bool caught=false;bool refusalAccepted=false;
  try {
    const auto result=seam::test::drainAsyncOperation([&] { ++polls;return PollResult{true}; },
        [] { return true; },std::chrono::milliseconds{3},"never-finishes",
        [&] { return time.now(); },[&] { time.advance(); });
    refusalAccepted=!result;
  } catch (const seam::test::Failure& error) {
    caught=std::string_view{error.what()}.find("functional test wait budget")!=std::string_view::npos;
  }
  CHECK(caught);CHECK(!refusalAccepted);CHECK(polls==3);
  // The old Result-error convention would accept the same harness failure.
  const PollResult oldTimeout{false,99};CHECK(!oldTimeout);
}

TEST_CASE("Async functional wait rejects late completion nonterminal errors and invalid budgets") {
  for (const bool success : {true,false}) {
    FakeTime time;bool caught=false;
    try {
      static_cast<void>(seam::test::drainAsyncOperation([&] {
        time.advance(3);return PollResult{success,42};
      },[] { return false; },std::chrono::milliseconds{3},"late-result",
        [&] { return time.now(); },[&] { time.advance(); }));
    } catch (const seam::test::Failure&) { caught=true; }
    CHECK(caught);
  }
  {
    FakeTime time;bool caught=false;
    try {
      static_cast<void>(seam::test::drainAsyncOperation([] { return PollResult{true}; },
          [&] { time.advance(3);return false; },std::chrono::milliseconds{3},"late-busy-check",
          [&] { return time.now(); },[&] { time.advance(); }));
    } catch (const seam::test::Failure&) { caught=true; }
    CHECK(caught);
  }
  for (const int budget : {0,-1,3}) {
    FakeTime time;bool caught=false;int polls=0;
    try {
      static_cast<void>(seam::test::drainAsyncOperation([&] { ++polls;return PollResult{false,42}; },
          [] { return true; },std::chrono::milliseconds{budget},"busy-error",
          [&] { return time.now(); },[&] { time.advance(); }));
    } catch (const seam::test::Failure&) { caught=true; }
    CHECK(caught);CHECK(polls==(budget>0?1:0));
  }
}
