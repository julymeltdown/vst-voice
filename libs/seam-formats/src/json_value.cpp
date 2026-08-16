#include "seam/formats/json_value.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>

namespace seam::formats {

double JsonValue::asNumber() const {
  if (isInteger()) return static_cast<double>(std::get<std::int64_t>(storage_));
  return std::get<double>(storage_);
}

std::int64_t JsonValue::asInt64() const {
  if (isInteger()) return std::get<std::int64_t>(storage_);
  const auto number = std::get<double>(storage_);
  // INT64_MAX is not exactly representable as double and rounds to 2^63.
  // Use an exclusive upper bound so that value can never reach an
  // out-of-range floating-to-integer conversion.
  constexpr double kMinimumInclusive = -9223372036854775808.0;
  constexpr double kMaximumExclusive = 9223372036854775808.0;
  if (!std::isfinite(number) || number < kMinimumInclusive ||
      number >= kMaximumExclusive || std::floor(number) != number) {
    throw std::bad_variant_access();
  }
  return static_cast<std::int64_t>(number);
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
  if (!isObject()) return nullptr;
  const auto& object = asObject();
  const auto iterator = object.find(key);
  return iterator == object.end() ? nullptr : &iterator->second;
}

JsonValue* JsonValue::find(std::string_view key) noexcept {
  if (!isObject()) return nullptr;
  auto& object = asObject();
  const auto iterator = object.find(key);
  return iterator == object.end() ? nullptr : &iterator->second;
}

namespace {

class Parser final {
public:
  Parser(std::string_view input, JsonParseLimits limits)
      : input_(input), limits_(limits) {}

  core::Result<JsonValue> parse() {
    if (input_.size() > limits_.maximumInputBytes) {
      return fail("JSON input exceeds configured byte limit");
    }
    if (limits_.maximumDepth == 0U || limits_.maximumNodes == 0U ||
        limits_.maximumStringBytes == 0U ||
        limits_.maximumCollectionEntries == 0U) {
      return fail("JSON parser limits are invalid");
    }
    skipWhitespace();
    auto value = parseValue(0U);
    if (!value) return value;
    skipWhitespace();
    if (position_ != input_.size()) return fail("Unexpected trailing characters");
    return value;
  }

private:
  core::Result<JsonValue> fail(std::string message) const {
    return core::failure<JsonValue>(core::ErrorCode::ParseError,
                                    std::move(message),
                                    "byte " + std::to_string(position_));
  }

  bool consumeNode() noexcept {
    if (nodes_ >= limits_.maximumNodes) return false;
    ++nodes_;
    return true;
  }

  void skipWhitespace() noexcept {
    while (position_ < input_.size()) {
      const auto value = input_[position_];
      if (value != ' ' && value != '\n' && value != '\r' && value != '\t') break;
      ++position_;
    }
  }

  core::Result<JsonValue> parseValue(std::size_t depth) {
    if (!consumeNode()) return fail("JSON node limit exceeded");
    if (depth > limits_.maximumDepth) return fail("JSON nesting depth limit exceeded");
    skipWhitespace();
    if (position_ >= input_.size()) return fail("Unexpected end of JSON input");
    const auto value = input_[position_];
    if (value == 'n') return parseLiteral("null", JsonValue{nullptr});
    if (value == 't') return parseLiteral("true", JsonValue{true});
    if (value == 'f') return parseLiteral("false", JsonValue{false});
    if (value == '"') {
      auto text = parseString();
      if (!text) return core::Result<JsonValue>{text.error()};
      return JsonValue{std::move(text).value()};
    }
    if (value == '[') return parseArray(depth + 1U);
    if (value == '{') return parseObject(depth + 1U);
    if (value == '-' || (value >= '0' && value <= '9')) return parseNumber();
    return fail("Unexpected JSON token");
  }

  core::Result<JsonValue> parseLiteral(std::string_view literal, JsonValue value) {
    if (input_.substr(position_, literal.size()) != literal) {
      return fail("Invalid JSON literal");
    }
    position_ += literal.size();
    return value;
  }

  core::Result<std::uint16_t> parseHex16() {
    if (position_ + 4U > input_.size()) {
      return core::failure<std::uint16_t>(core::ErrorCode::ParseError,
                                          "Truncated JSON Unicode escape");
    }
    std::uint16_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
      const auto character = input_[position_++];
      value = static_cast<std::uint16_t>(value << 4U);
      if (character >= '0' && character <= '9') {
        value = static_cast<std::uint16_t>(value + character - '0');
      } else if (character >= 'a' && character <= 'f') {
        value = static_cast<std::uint16_t>(value + 10 + character - 'a');
      } else if (character >= 'A' && character <= 'F') {
        value = static_cast<std::uint16_t>(value + 10 + character - 'A');
      } else {
        return core::failure<std::uint16_t>(core::ErrorCode::ParseError,
                                            "Invalid JSON Unicode escape");
      }
    }
    return value;
  }

  static void appendUtf8(std::string& output, std::uint32_t codePoint) {
    if (codePoint <= 0x7fU) {
      output.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7ffU) {
      output.push_back(static_cast<char>(0xc0U | (codePoint >> 6U)));
      output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else if (codePoint <= 0xffffU) {
      output.push_back(static_cast<char>(0xe0U | (codePoint >> 12U)));
      output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    } else {
      output.push_back(static_cast<char>(0xf0U | (codePoint >> 18U)));
      output.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | (codePoint & 0x3fU)));
    }
  }

  core::Result<std::string> parseString() {
    ++position_;
    std::string output;
    while (position_ < input_.size()) {
      const auto value = static_cast<unsigned char>(input_[position_++]);
      if (value == '"') return output;
      if (value < 0x20U) {
        return core::failure<std::string>(core::ErrorCode::ParseError,
                                          "Unescaped control character in JSON string");
      }
      if (value != '\\') {
        output.push_back(static_cast<char>(value));
      } else {
        if (position_ >= input_.size()) {
          return core::failure<std::string>(core::ErrorCode::ParseError,
                                            "Truncated JSON escape");
        }
        const auto escape = input_[position_++];
        switch (escape) {
          case '"': output.push_back('"'); break;
          case '\\': output.push_back('\\'); break;
          case '/': output.push_back('/'); break;
          case 'b': output.push_back('\b'); break;
          case 'f': output.push_back('\f'); break;
          case 'n': output.push_back('\n'); break;
          case 'r': output.push_back('\r'); break;
          case 't': output.push_back('\t'); break;
          case 'u': {
            auto first = parseHex16();
            if (!first) return core::Result<std::string>{first.error()};
            std::uint32_t codePoint = first.value();
            if (first.value() >= 0xd800U && first.value() <= 0xdbffU) {
              if (position_ + 2U > input_.size() || input_[position_] != '\\' ||
                  input_[position_ + 1U] != 'u') {
                return core::failure<std::string>(core::ErrorCode::ParseError,
                                                  "High surrogate lacks low surrogate");
              }
              position_ += 2U;
              auto second = parseHex16();
              if (!second) return core::Result<std::string>{second.error()};
              if (second.value() < 0xdc00U || second.value() > 0xdfffU) {
                return core::failure<std::string>(core::ErrorCode::ParseError,
                                                  "Invalid low surrogate");
              }
              codePoint = 0x10000U +
                  ((static_cast<std::uint32_t>(first.value()) - 0xd800U) << 10U) +
                  (static_cast<std::uint32_t>(second.value()) - 0xdc00U);
            } else if (first.value() >= 0xdc00U && first.value() <= 0xdfffU) {
              return core::failure<std::string>(core::ErrorCode::ParseError,
                                                "Unexpected low surrogate");
            }
            appendUtf8(output, codePoint);
            break;
          }
          default:
            return core::failure<std::string>(core::ErrorCode::ParseError,
                                              "Invalid JSON escape");
        }
      }
      if (output.size() > limits_.maximumStringBytes) {
        return core::failure<std::string>(core::ErrorCode::ParseError,
                                          "JSON string exceeds configured limit");
      }
    }
    return core::failure<std::string>(core::ErrorCode::ParseError,
                                      "Unterminated JSON string");
  }

  core::Result<JsonValue> parseNumber() {
    const auto start = position_;
    if (input_[position_] == '-') ++position_;
    if (position_ >= input_.size()) return fail("Truncated JSON number");
    if (input_[position_] == '0') {
      ++position_;
    } else if (input_[position_] >= '1' && input_[position_] <= '9') {
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
    } else {
      return fail("Invalid JSON number");
    }
    bool floating = false;
    if (position_ < input_.size() && input_[position_] == '.') {
      floating = true;
      ++position_;
      const auto fractionStart = position_;
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
      if (fractionStart == position_) return fail("Invalid JSON fraction");
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      floating = true;
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      const auto exponentStart = position_;
      while (position_ < input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9') {
        ++position_;
      }
      if (exponentStart == position_) return fail("Invalid JSON exponent");
    }

    const auto token = input_.substr(start, position_ - start);
    if (!floating) {
      std::int64_t integer = 0;
      const auto [end, error] = std::from_chars(
          token.data(), token.data() + token.size(), integer);
      if (error != std::errc{} || end != token.data() + token.size()) {
        return fail("JSON integer is not representable as int64");
      }
      return JsonValue{integer};
    }
    double number = 0.0;
    const auto [end, error] = std::from_chars(
        token.data(), token.data() + token.size(), number);
    if (error != std::errc{} || end != token.data() + token.size() ||
        !std::isfinite(number)) {
      return fail("JSON number is not representable");
    }
    return JsonValue{number};
  }

  core::Result<JsonValue> parseArray(std::size_t depth) {
    if (depth > limits_.maximumDepth) {
      return fail("JSON nesting depth limit exceeded");
    }
    ++position_;
    JsonValue::Array values;
    skipWhitespace();
    if (position_ < input_.size() && input_[position_] == ']') {
      ++position_;
      return JsonValue{std::move(values)};
    }
    while (true) {
      if (values.size() >= limits_.maximumCollectionEntries) {
        return fail("JSON array exceeds configured entry limit");
      }
      auto value = parseValue(depth);
      if (!value) return value;
      values.push_back(std::move(value).value());
      skipWhitespace();
      if (position_ >= input_.size()) return fail("Unterminated JSON array");
      const auto separator = input_[position_++];
      if (separator == ']') return JsonValue{std::move(values)};
      if (separator != ',') return fail("Expected ',' or ']' in JSON array");
      skipWhitespace();
    }
  }

  core::Result<JsonValue> parseObject(std::size_t depth) {
    if (depth > limits_.maximumDepth) {
      return fail("JSON nesting depth limit exceeded");
    }
    ++position_;
    JsonValue::Object object;
    skipWhitespace();
    if (position_ < input_.size() && input_[position_] == '}') {
      ++position_;
      return JsonValue{std::move(object)};
    }
    while (true) {
      if (object.size() >= limits_.maximumCollectionEntries) {
        return fail("JSON object exceeds configured entry limit");
      }
      skipWhitespace();
      if (position_ >= input_.size() || input_[position_] != '"') {
        return fail("Expected a string key in JSON object");
      }
      auto key = parseString();
      if (!key) return core::Result<JsonValue>{key.error()};
      skipWhitespace();
      if (position_ >= input_.size() || input_[position_++] != ':') {
        return fail("Expected ':' after JSON object key");
      }
      auto value = parseValue(depth);
      if (!value) return value;
      const auto [_, inserted] = object.emplace(
          std::move(key).value(), std::move(value).value());
      if (!inserted) return fail("Duplicate JSON object key");
      skipWhitespace();
      if (position_ >= input_.size()) return fail("Unterminated JSON object");
      const auto separator = input_[position_++];
      if (separator == '}') return JsonValue{std::move(object)};
      if (separator != ',') return fail("Expected ',' or '}' in JSON object");
    }
  }

  std::string_view input_;
  JsonParseLimits limits_;
  std::size_t position_{0U};
  std::size_t nodes_{0U};
};

void writeEscaped(std::ostringstream& stream, std::string_view text) {
  stream << '"';
  for (const char character : text) {
    const auto value = static_cast<unsigned char>(character);
    switch (value) {
      case '"': stream << "\\\""; break;
      case '\\': stream << "\\\\"; break;
      case '\b': stream << "\\b"; break;
      case '\f': stream << "\\f"; break;
      case '\n': stream << "\\n"; break;
      case '\r': stream << "\\r"; break;
      case '\t': stream << "\\t"; break;
      default:
        if (value < 0x20U) {
          stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                 << static_cast<int>(value) << std::dec << std::setfill(' ');
        } else {
          stream << static_cast<char>(value);
        }
    }
  }
  stream << '"';
}

void writeValue(std::ostringstream& stream, const JsonValue& value,
                bool pretty, int depth) {
  const auto indent = [&](int level) {
    if (pretty) stream << std::string(static_cast<std::size_t>(level * 2), ' ');
  };
  if (value.isNull()) {
    stream << "null";
  } else if (value.isBool()) {
    stream << (value.asBool() ? "true" : "false");
  } else if (value.isInteger()) {
    stream << value.asInt64();
  } else if (value.isNumber()) {
    stream << std::setprecision(17) << value.asNumber();
  } else if (value.isString()) {
    writeEscaped(stream, value.asString());
  } else if (value.isArray()) {
    const auto& array = value.asArray();
    stream << '[';
    if (!array.empty() && pretty) stream << '\n';
    for (std::size_t index = 0; index < array.size(); ++index) {
      indent(depth + 1);
      writeValue(stream, array[index], pretty, depth + 1);
      if (index + 1U < array.size()) stream << ',';
      if (pretty) stream << '\n';
    }
    if (!array.empty()) indent(depth);
    stream << ']';
  } else {
    const auto& object = value.asObject();
    stream << '{';
    if (!object.empty() && pretty) stream << '\n';
    std::size_t index = 0U;
    for (const auto& [key, child] : object) {
      indent(depth + 1);
      writeEscaped(stream, key);
      stream << (pretty ? ": " : ":");
      writeValue(stream, child, pretty, depth + 1);
      if (++index < object.size()) stream << ',';
      if (pretty) stream << '\n';
    }
    if (!object.empty()) indent(depth);
    stream << '}';
  }
}

}  // namespace

core::Result<JsonValue> parseJson(std::string_view input,
                                  const JsonParseLimits& limits) {
  return Parser{input, limits}.parse();
}

std::string stringifyJson(const JsonValue& value, bool pretty) {
  std::ostringstream stream;
  writeValue(stream, value, pretty, 0);
  if (pretty) stream << '\n';
  return stream.str();
}

}  // namespace seam::formats
