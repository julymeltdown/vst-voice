#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace seam::test {

struct Case final {
  std::string name;
  std::function<void()> function;
};

inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}

class Registrar final {
public:
  Registrar(std::string name, std::function<void()> function) {
    registry().push_back(Case{std::move(name), std::move(function)});
  }
};

class Failure final : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

inline void check(bool condition, std::string_view expression,
                  std::string_view file, int line) {
  if (!condition) {
    std::ostringstream stream;
    stream << file << ':' << line << ": CHECK failed: " << expression;
    throw Failure(stream.str());
  }
}

inline void checkNear(double lhs, double rhs, double epsilon,
                      std::string_view expression, std::string_view file, int line) {
  if (std::abs(lhs - rhs) > epsilon) {
    std::ostringstream stream;
    stream << file << ':' << line << ": CHECK_NEAR failed: " << expression
           << " (" << lhs << " vs " << rhs << ", epsilon " << epsilon << ')';
    throw Failure(stream.str());
  }
}

inline int runAll() {
  std::size_t passed = 0;
  std::size_t failed = 0;
  for (const auto& test : registry()) {
    try {
      test.function();
      ++passed;
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& exception) {
      ++failed;
      std::cerr << "[FAIL] " << test.name << "\n       " << exception.what() << '\n';
    } catch (...) {
      ++failed;
      std::cerr << "[FAIL] " << test.name << "\n       unknown exception\n";
    }
  }
  std::cout << "\n" << passed << " passed, " << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace seam::test

#define SEAM_TEST_JOIN_IMPL(a, b) a##b
#define SEAM_TEST_JOIN(a, b) SEAM_TEST_JOIN_IMPL(a, b)
#define TEST_CASE(name) \
  static void SEAM_TEST_JOIN(seam_test_function_, __LINE__)(); \
  static ::seam::test::Registrar SEAM_TEST_JOIN(seam_test_registrar_, __LINE__){ \
      name, SEAM_TEST_JOIN(seam_test_function_, __LINE__)}; \
  static void SEAM_TEST_JOIN(seam_test_function_, __LINE__)()

#define CHECK(expression) \
  ::seam::test::check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define CHECK_NEAR(lhs, rhs, epsilon) \
  ::seam::test::checkNear(static_cast<double>(lhs), static_cast<double>(rhs), \
                          static_cast<double>(epsilon), #lhs " ~= " #rhs, __FILE__, __LINE__)
