#pragma once

#include <string>
#include <utility>

namespace seam::core {

enum class ErrorCode {
  InvalidArgument,
  InvalidState,
  InvariantViolation,
  NotFound,
  Conflict,
  ParseError,
  IoError,
  Unsupported,
  Internal,
};

struct Error final {
  ErrorCode code{ErrorCode::Internal};
  std::string message;
  std::string context;

  Error() = default;
  Error(ErrorCode codeValue, std::string messageValue, std::string contextValue = {})
      : code(codeValue),
        message(std::move(messageValue)),
        context(std::move(contextValue)) {}
};

}  // namespace seam::core
