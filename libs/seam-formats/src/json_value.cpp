#include "seam/formats/json_value.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>

namespace seam::formats {

std::int64_t JsonValue::asInt64() const {
  const auto number = asNumber();
  if (!std::isfinite(number) || number < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
      number > static_cast<double>(std::numeric_limits<std::int64_t>::max()) ||
      std::floor(number) != number) {
    throw std::bad_variant_access();
  }
  return static_cast<std::int64_t>(number);
}

const JsonValue* JsonValue::find(std::string_view key) const noexcept {
  if (!isObject()) {
    return nullptr;
  }
  const auto& object = asObject();
  const auto iterator = object.find(key);
  return iterator == object.end() ? nullptr : &iterator->second;
}

JsonValue* JsonValue::find(std::string_view key) noexcept {
  if (!isObject()) {
    return nullptr;
  }
  auto& object = asObject();
  const auto iterator = object.find(key);
  return iterator == object.end() ? nullptr : &iterator->second;
}

namespace {

class Parser final {
public:
  explicit Parser(std::string_view input) : input_(input) {}

  core::Result<JsonValue> parse() {
    skipWhitespace();
    auto value = parseValue();
    if (!value) {
      return value;
    }
    skipWhitespace();
    if (position_ != input_.size()) {
      return fail("Unexpected trailing characters");
    }
    return value;
  }

private:
  core::Result<JsonValue> fail(std::string message) const {
    return core::failure<JsonValue>(core::ErrorCode::ParseError,
                                    std::move(message),
                                    "byte " + std::to_string(position_));
  }

  void skipWhitespace() noexcept {
    while (position_ < input_.size()) {
      const char value = input_[position_];
      if (value != ' ' && value != '\n' && value != '\r' && value != '\t') {
        break;
      }
      ++position_;
    }
  }

  core::Result<JsonValue> parseValue() {
    skipWhitespace();
    if (position_ >= input_.size()) {
      return fail("Unexpected end of JSON input");
    }
    switch (input_[position_]) {
      case 'n': return parseLiteral("null", JsonValue{nullptr});
      case 't': return parseLiteral("true", JsonValue{true});
      case 'f': return parseLiteral("false", JsonValue{false});
      case '"': {
        auto value = parseString();
        if (!value) {
          return core::Result<JsonValue>{value.error()};
        }
        return JsonValue{std::move(value).value()};
      }
      case '[': return parseArray();
      case '{': return parseObject();
      default: return parseNumber();
    }
  }

  core::Result<JsonValue> parseLiteral(std::string_view literal, JsonValue value) {
    if (input_.substr(position_, literal.size()) != literal) {
      return fail("Invalid JSON literal");
    }
    position_ += literal.size();
    return value;
  }

  core::Result<std::string> parseString() {
    if (input_[position_] != '"') {
      return core::failure<std::string>(core::ErrorCode::ParseError,
                                        "Expected a JSON string");
    }
    ++position_;
    std::string output;
    while (position_ < input_.size()) {
      const char value = input_[position_++];
      if (value == '"') {
        return output;
      }
      if (static_cast<unsigned char>(value) < 0x20u) {
        return core::failure<std::string>(core::ErrorCode::ParseError,
                                          "Control character in JSON string",
                                          "byte " + std::to_string(position_ - 1));
      }
      if (value != '\\') {
        output.push_back(value);
        continue;
      }
      if (position_ >= input_.size()) {
        return core::failure<std::string>(core::ErrorCode::ParseError,
                                          "Truncated JSON escape");
      }
      const char escape = input_[position_++];
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
          if (position_ + 4 > input_.size()) {
            return core::failure<std::string>(core::ErrorCode::ParseError,
                                              "Truncated Unicode escape");
          }
          std::uint32_t codePoint = 0;
          for (int index = 0; index < 4; ++index) {
            const char hex = input_[position_++];
            codePoint <<= 4u;
            if (hex >= '0' && hex <= '9') codePoint |= static_cast<std::uint32_t>(hex - '0');
            else if (hex >= 'a' && hex <= 'f') codePoint |= static_cast<std::uint32_t>(10 + hex - 'a');
            else if (hex >= 'A' && hex <= 'F') codePoint |= static_cast<std::uint32_t>(10 + hex - 'A');
            else return core::failure<std::string>(core::ErrorCode::ParseError,
                                                   "Invalid Unicode escape");
          }
          if (codePoint <= 0x7Fu) {
            output.push_back(static_cast<char>(codePoint));
          } else if (codePoint <= 0x7FFu) {
            output.push_back(static_cast<char>(0xC0u | (codePoint >> 6u)));
            output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
          } else {
            output.push_back(static_cast<char>(0xE0u | (codePoint >> 12u)));
            output.push_back(static_cast<char>(0x80u | ((codePoint >> 6u) & 0x3Fu)));
            output.push_back(static_cast<char>(0x80u | (codePoint & 0x3Fu)));
          }
          break;
        }
        default:
          return core::failure<std::string>(core::ErrorCode::ParseError,
                                            "Invalid JSON escape");
      }
    }
    return core::failure<std::string>(core::ErrorCode::ParseError,
                                      "Unterminated JSON string");
  }

  core::Result<JsonValue> parseNumber() {
    const auto start = position_;
    if (input_[position_] == '-') {
      ++position_;
    }
    if (position_ >= input_.size()) {
      return fail("Truncated JSON number");
    }
    if (input_[position_] == '0') {
      ++position_;
    } else if (input_[position_] >= '1' && input_[position_] <= '9') {
      while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
        ++position_;
      }
    } else {
      return fail("Invalid JSON number");
    }
    if (position_ < input_.size() && input_[position_] == '.') {
      ++position_;
      const auto fractionStart = position_;
      while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
        ++position_;
      }
      if (fractionStart == position_) {
        return fail("Invalid JSON fraction");
      }
    }
    if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) {
        ++position_;
      }
      const auto exponentStart = position_;
      while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') {
        ++position_;
      }
      if (exponentStart == position_) {
        return fail("Invalid JSON exponent");
      }
    }

    const auto token = input_.substr(start, position_ - start);
    double number = 0.0;
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), number);
    if (error != std::errc{} || end != token.data() + token.size() || !std::isfinite(number)) {
      return fail("JSON number is not representable");
    }
    return JsonValue{number};
  }

  core::Result<JsonValue> parseArray() {
    ++position_;
    JsonValue::Array values;
    skipWhitespace();
    if (position_ < input_.size() && input_[position_] == ']') {
      ++position_;
      return JsonValue{std::move(values)};
    }
    while (true) {
      auto value = parseValue();
      if (!value) {
        return value;
      }
      values.push_back(std::move(value).value());
      skipWhitespace();
      if (position_ >= input_.size()) {
        return fail("Unterminated JSON array");
      }
      const char separator = input_[position_++];
      if (separator == ']') {
        return JsonValue{std::move(values)};
      }
      if (separator != ',') {
        return fail("Expected ',' or ']' in JSON array");
      }
      skipWhitespace();
    }
  }

  core::Result<JsonValue> parseObject() {
    ++position_;
    JsonValue::Object object;
    skipWhitespace();
    if (position_ < input_.size() && input_[position_] == '}') {
      ++position_;
      return JsonValue{std::move(object)};
    }
    while (true) {
      skipWhitespace();
      if (position_ >= input_.size() || input_[position_] != '"') {
        return fail("Expected a string key in JSON object");
      }
      auto key = parseString();
      if (!key) {
        return core::Result<JsonValue>{key.error()};
      }
      skipWhitespace();
      if (position_ >= input_.size() || input_[position_++] != ':') {
        return fail("Expected ':' after JSON object key");
      }
      auto value = parseValue();
      if (!value) {
        return value;
      }
      const auto [_, inserted] = object.emplace(std::move(key).value(), std::move(value).value());
      if (!inserted) {
        return fail("Duplicate JSON object key");
      }
      skipWhitespace();
      if (position_ >= input_.size()) {
        return fail("Unterminated JSON object");
      }
      const char separator = input_[position_++];
      if (separator == '}') {
        return JsonValue{std::move(object)};
      }
      if (separator != ',') {
        return fail("Expected ',' or '}' in JSON object");
      }
    }
  }

  std::string_view input_;
  std::size_t position_{0};
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
        if (value < 0x20u) {
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
      if (index + 1 < array.size()) stream << ',';
      if (pretty) stream << '\n';
    }
    if (!array.empty()) indent(depth);
    stream << ']';
  } else {
    const auto& object = value.asObject();
    stream << '{';
    if (!object.empty() && pretty) stream << '\n';
    std::size_t index = 0;
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

core::Result<JsonValue> parseJson(std::string_view input) {
  return Parser{input}.parse();
}

std::string stringifyJson(const JsonValue& value, bool pretty) {
  std::ostringstream stream;
  writeValue(stream, value, pretty, 0);
  if (pretty) {
    stream << '\n';
  }
  return stream.str();
}

}  // namespace seam::formats
