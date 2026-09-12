#include "seam/interchange/ustx_codec.hpp"

#include "seam/domain/note.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace seam::interchange {
namespace {

using Node = formats::JsonValue;
using Object = Node::Object;
using Array = Node::Array;

struct Line final {
  std::size_t indent{0U};
  std::size_t number{0U};
  std::string text;
};

core::Result<Node> parseFailure(std::string message, std::size_t line = 0U) {
  return core::failure<Node>(core::ErrorCode::ParseError, std::move(message),
                             line == 0U ? std::string{} : "line " + std::to_string(line));
}

std::string_view trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1U);
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1U);
  return value;
}

std::size_t mappingColon(std::string_view value) {
  bool single = false;
  bool doubleQuote = false;
  bool escaped = false;
  std::size_t flowDepth = 0U;
  for (std::size_t index = 0U; index < value.size(); ++index) {
    const auto character = value[index];
    if (doubleQuote) {
      if (escaped) escaped = false;
      else if (character == '\\') escaped = true;
      else if (character == '"') doubleQuote = false;
      continue;
    }
    if (single) {
      if (character == '\'' && (index + 1U >= value.size() || value[index + 1U] != '\'')) single = false;
      else if (character == '\'' && index + 1U < value.size()) ++index;
      continue;
    }
    if (character == '"') { doubleQuote = true; continue; }
    if (character == '\'') { single = true; continue; }
    if (character == '{' || character == '[') { ++flowDepth; continue; }
    if (character == '}' || character == ']') { if (flowDepth > 0U) --flowDepth; continue; }
    if (character == ':') {
      if (flowDepth == 0U && (index + 1U == value.size() || value[index + 1U] == ' ' || value[index + 1U] == '\t')) return index;
    }
  }
  return std::string_view::npos;
}

std::string stripComment(std::string_view value) {
  bool single = false;
  bool doubleQuote = false;
  bool escaped = false;
  for (std::size_t index = 0U; index < value.size(); ++index) {
    const auto character = value[index];
    if (doubleQuote) {
      if (escaped) escaped = false;
      else if (character == '\\') escaped = true;
      else if (character == '"') doubleQuote = false;
      continue;
    }
    if (single) {
      if (character == '\'' && (index + 1U >= value.size() || value[index + 1U] != '\'')) single = false;
      else if (character == '\'' && index + 1U < value.size()) ++index;
      continue;
    }
    if (character == '"') { doubleQuote = true; continue; }
    if (character == '\'') { single = true; continue; }
    if (character == '#' && (index == 0U || value[index - 1U] == ' ' || value[index - 1U] == '\t'))
      return std::string{value.substr(0U, index)};
  }
  return std::string{value};
}

class FlowParser final {
public:
  FlowParser(std::string_view value, const UstxLimits& limits, std::size_t& nodes,
             std::size_t line) : value_(value), limits_(limits), nodes_(nodes), line_(line) {}

  core::Result<Node> parse() {
    auto result = parseValue(0U);
    if (!result) return result;
    skipSpace();
    if (position_ != value_.size()) return parseFailure("USTX inline value has trailing characters", line_);
    return result;
  }

private:
  core::Result<Node> fail(std::string message) const { return parseFailure(std::move(message), line_); }

  bool consumeNode() {
    if (nodes_ >= limits_.maximumNodes) return false;
    ++nodes_;
    return true;
  }

  void skipSpace() noexcept {
    while (position_ < value_.size() && (value_[position_] == ' ' || value_[position_] == '\t')) ++position_;
  }

  core::Result<Node> parseValue(std::size_t depth) {
    if (!consumeNode()) return fail("USTX node limit exceeded");
    if (depth > limits_.maximumDepth) return fail("USTX nesting depth limit exceeded");
    skipSpace();
    if (position_ >= value_.size()) return fail("USTX inline value is empty");
    switch (value_[position_]) {
      case '[': return parseArray(depth + 1U);
      case '{': return parseObject(depth + 1U);
      case '\'':
      case '"': return parseQuoted();
      default: return parseBare();
    }
  }

  core::Result<Node> parseQuoted() {
    const auto quote = value_[position_++];
    std::string output;
    while (position_ < value_.size()) {
      const auto character = value_[position_++];
      if (character == quote) {
        if (quote == '\'' && position_ < value_.size() && value_[position_] == '\'') {
          ++position_; output.push_back('\''); continue;
        }
        if (output.size() > limits_.maximumScalarBytes) return fail("USTX scalar exceeds limit");
        if (!domain::fromUtf8(output)) return fail("USTX string is not valid UTF-8");
        return Node{std::move(output)};
      }
      if (quote == '"' && character == '\\') {
        if (position_ >= value_.size()) return fail("USTX double-quoted escape is truncated");
        const auto escaped = value_[position_++];
        switch (escaped) {
          case '"': output.push_back('"'); break;
          case '\\': output.push_back('\\'); break;
          case 'n': output.push_back('\n'); break;
          case 'r': output.push_back('\r'); break;
          case 't': output.push_back('\t'); break;
          case '0': output.push_back('\0'); break;
          default: return fail("USTX double-quoted escape is unsupported");
        }
      } else {
        if (static_cast<unsigned char>(character) < 0x20U && character != '\t')
          return fail("USTX quoted scalar contains a control character");
        output.push_back(character);
      }
      if (output.size() > limits_.maximumScalarBytes) return fail("USTX scalar exceeds limit");
    }
    return fail("USTX quoted scalar is unterminated");
  }

  core::Result<Node> parseBare() {
    const auto start = position_;
    // Plain YAML scalars may contain spaces (for example a track name).  Flow
    // delimiters, rather than whitespace, terminate the value; surrounding
    // whitespace is trimmed below.
    while (position_ < value_.size() && value_[position_] != ',' && value_[position_] != ']' &&
           value_[position_] != '}') ++position_;
    const auto token = trim(value_.substr(start, position_ - start));
    if (token.empty()) return fail("USTX bare scalar is empty");
    if (token.size() > limits_.maximumScalarBytes) return fail("USTX scalar exceeds limit");
    if (token == "null" || token == "Null" || token == "NULL") return Node{nullptr};
    if (token == "true" || token == "True" || token == "TRUE") return Node{true};
    if (token == "false" || token == "False" || token == "FALSE") return Node{false};
    std::int64_t integer = 0;
    const auto integerResult = std::from_chars(token.data(), token.data() + token.size(), integer);
    if (integerResult.ec == std::errc{} && integerResult.ptr == token.data() + token.size()) return Node{integer};
    double number = 0.0;
    const auto numberResult = std::from_chars(token.data(), token.data() + token.size(), number);
    if (numberResult.ec == std::errc{} && numberResult.ptr == token.data() + token.size()) {
      if (!std::isfinite(number)) return fail("USTX number is not finite");
      return Node{number};
    }
    const auto numericCandidate = token.front() == '-' || token.front() == '+' ||
        token.front() == '.' || (token.front() >= '0' && token.front() <= '9');
    if (numericCandidate) return fail("USTX numeric scalar is malformed or out of range");
    if (token == "nan" || token == "NaN" || token == "inf" || token == ".inf" || token == "-.inf")
      return fail("USTX non-finite number is not supported");
    if (!domain::fromUtf8(std::string{token})) return fail("USTX string is not valid UTF-8");
    return Node{std::string{token}};
  }

  core::Result<Node> parseArray(std::size_t depth) {
    ++position_;
    Array output;
    skipSpace();
    if (position_ < value_.size() && value_[position_] == ']') { ++position_; return Node{std::move(output)}; }
    while (position_ < value_.size()) {
      if (output.size() >= limits_.maximumCollectionEntries) return fail("USTX collection entry limit exceeded");
      auto item = parseValue(depth);
      if (!item) return item;
      output.push_back(std::move(item).value());
      skipSpace();
      if (position_ >= value_.size()) return fail("USTX flow sequence is unterminated");
      if (value_[position_] == ']') { ++position_; return Node{std::move(output)}; }
      if (value_[position_] != ',') return fail("USTX flow sequence requires commas");
      ++position_; skipSpace();
      if (position_ < value_.size() && value_[position_] == ']') return fail("USTX flow sequence has a trailing comma");
    }
    return fail("USTX flow sequence is unterminated");
  }

  core::Result<Node> parseObject(std::size_t depth) {
    ++position_;
    Object output;
    skipSpace();
    if (position_ < value_.size() && value_[position_] == '}') { ++position_; return Node{std::move(output)}; }
    while (position_ < value_.size()) {
      if (output.size() >= limits_.maximumCollectionEntries) return fail("USTX collection entry limit exceeded");
      if (!consumeNode()) return fail("USTX node limit exceeded");
      auto key = parseObjectKey();
      if (!key) return core::Result<Node>{key.error()};
      skipSpace();
      if (position_ >= value_.size() || value_[position_] != ':') return fail("USTX flow mapping requires a colon");
      ++position_;
      auto item = parseValue(depth);
      if (!item) return item;
      if (!output.emplace(std::move(key).value(), std::move(item).value()).second)
        return fail("USTX flow mapping contains a duplicate key");
      skipSpace();
      if (position_ >= value_.size()) return fail("USTX flow mapping is unterminated");
      if (value_[position_] == '}') { ++position_; return Node{std::move(output)}; }
      if (value_[position_] != ',') return fail("USTX flow mapping requires commas");
      ++position_; skipSpace();
      if (position_ < value_.size() && value_[position_] == '}') return fail("USTX flow mapping has a trailing comma");
    }
    return fail("USTX flow mapping is unterminated");
  }

  core::Result<std::string> parseObjectKey() {
    if (position_ >= value_.size()) return core::failure<std::string>(core::ErrorCode::ParseError, "USTX object key is missing");
    if (value_[position_] == '\'' || value_[position_] == '"') {
      auto parsed = parseQuoted();
      if (!parsed || !parsed.value().isString()) return core::failure<std::string>(core::ErrorCode::ParseError, "USTX object key is invalid");
      return parsed.value().asString();
    }
    const auto start = position_;
    while (position_ < value_.size() && value_[position_] != ':') ++position_;
    const auto key = trim(value_.substr(start, position_ - start));
    if (key.empty() || key.size() > limits_.maximumScalarBytes || !domain::fromUtf8(std::string{key}))
      return core::failure<std::string>(core::ErrorCode::ParseError, "USTX object key is invalid");
    return std::string{key};
  }

  std::string_view value_;
  const UstxLimits& limits_;
  std::size_t& nodes_;
  std::size_t line_{0U};
  std::size_t position_{0U};
};

class BlockParser final {
public:
  BlockParser(std::string input, const UstxLimits& limits) : input_(std::move(input)), limits_(limits) {}

  core::Result<Node> parse() {
    if (limits_.maximumDepth == 0U || limits_.maximumNodes == 0U || limits_.maximumCollectionEntries == 0U || limits_.maximumScalarBytes == 0U)
      return parseFailure("USTX parser limits are invalid");
    auto prepared = prepareLines();
    if (!prepared) return core::Result<Node>{prepared.error()};
    if (lines_.empty() || lines_.front().indent != 0U) return parseFailure("USTX root must start at indentation zero");
    auto root = parseBlock(0U, 0U);
    if (!root) return root;
    if (lineIndex_ != lines_.size()) return parseFailure("USTX has trailing content", lines_[lineIndex_].number);
    if (!root.value().isObject()) return parseFailure("USTX root must be a mapping");
    return root;
  }

private:
  core::Result<void> prepareLines() {
    std::size_t start = 0U;
    std::size_t lineNumber = 1U;
    if (input_.size() >= 3U && static_cast<unsigned char>(input_[0]) == 0xefU &&
        static_cast<unsigned char>(input_[1]) == 0xbbU && static_cast<unsigned char>(input_[2]) == 0xbfU) {
      input_.erase(0U, 3U);
    }
    while (start <= input_.size()) {
      const auto end = input_.find('\n', start);
      const auto stop = end == std::string::npos ? input_.size() : end;
      std::string raw = input_.substr(start, stop - start);
      if (!raw.empty() && raw.back() == '\r') raw.pop_back();
      std::size_t indent = 0U;
      while (indent < raw.size() && raw[indent] == ' ') ++indent;
      if (indent < raw.size() && raw[indent] == '\t') return core::failure(core::ErrorCode::ParseError, "USTX indentation may not contain tabs", "line " + std::to_string(lineNumber));
      auto text = stripComment(std::string_view{raw}.substr(indent));
      const auto content = trim(text);
      if (!content.empty()) {
        if (content == "---" || content == "..." || content.starts_with("--- ") || content.starts_with("... "))
          return core::failure(core::ErrorCode::ParseError, "USTX multiple YAML documents are not supported", "line " + std::to_string(lineNumber));
        bool single = false, doubleQuote = false, escaped = false;
        for (const auto character : content) {
          if (doubleQuote) { if (escaped) escaped = false; else if (character == '\\') escaped = true; else if (character == '"') doubleQuote = false; continue; }
          if (single) { if (character == '\'') single = false; continue; }
          if (character == '"') doubleQuote = true;
          else if (character == '\'') single = true;
          else if (character == '&' || character == '*' || character == '!')
            return core::failure(core::ErrorCode::ParseError, "USTX aliases and tags are not supported", "line " + std::to_string(lineNumber));
        }
        if (lines_.size() >= limits_.maximumNodes) return core::failure(core::ErrorCode::ParseError, "USTX line/node limit exceeded");
        lines_.push_back(Line{indent, lineNumber, std::string{content}});
      }
      if (end == std::string::npos) break;
      start = end + 1U; ++lineNumber;
    }
    return core::success();
  }

  bool isSequenceLine(const Line& line, std::size_t indent) const noexcept {
    return line.indent == indent && line.text.size() >= 1U && line.text.front() == '-' &&
           (line.text.size() == 1U || line.text[1] == ' ' || line.text[1] == '\t');
  }

  core::Result<Node> parseInline(std::string_view value, std::size_t line) {
    FlowParser parser{trim(value), limits_, nodes_, line};
    return parser.parse();
  }

  core::Result<Node> parseBlock(std::size_t depth, std::size_t indent) {
    if (depth > limits_.maximumDepth) return parseFailure("USTX nesting depth limit exceeded", lines_[lineIndex_].number);
    if (lineIndex_ >= lines_.size() || lines_[lineIndex_].indent != indent)
      return parseFailure("USTX indentation is inconsistent", lineIndex_ < lines_.size() ? lines_[lineIndex_].number : 0U);
    if (isSequenceLine(lines_[lineIndex_], indent)) return parseSequence(depth, indent);
    return parseMapping(depth, indent);
  }

  core::Result<Node> parseMapping(std::size_t depth, std::size_t indent) {
    if (++nodes_ > limits_.maximumNodes) return parseFailure("USTX node limit exceeded");
    Object output;
    while (lineIndex_ < lines_.size() && lines_[lineIndex_].indent == indent && !isSequenceLine(lines_[lineIndex_], indent)) {
      const auto line = lines_[lineIndex_];
      const auto colon = mappingColon(line.text);
      if (colon == std::string_view::npos || colon == 0U) return parseFailure("USTX mapping entry requires a key and colon", line.number);
      auto keyNode = parseInline(line.text.substr(0U, colon), line.number);
      if (!keyNode || !keyNode.value().isString()) return parseFailure("USTX mapping key must be a scalar string", line.number);
      const auto key = keyNode.value().asString();
      auto value = parseEntryValue(depth, indent, line.text.substr(colon + 1U), line.number);
      if (!value) return value;
      if (!output.emplace(key, std::move(value).value()).second) return parseFailure("USTX mapping contains a duplicate key", line.number);
    }
    if (output.empty()) return parseFailure("USTX mapping is empty");
    return Node{std::move(output)};
  }

  core::Result<Node> parseEntryValue(std::size_t depth, std::size_t indent, std::string_view raw, std::size_t line) {
    const auto value = trim(raw);
    ++lineIndex_;
    if (!value.empty()) return parseInline(value, line);
    if (lineIndex_ < lines_.size() && lines_[lineIndex_].indent > indent)
      return parseBlock(depth + 1U, lines_[lineIndex_].indent);
    return Node{nullptr};
  }

  core::Result<Node> parseSequence(std::size_t depth, std::size_t indent) {
    if (++nodes_ > limits_.maximumNodes) return parseFailure("USTX node limit exceeded");
    Array output;
    while (lineIndex_ < lines_.size() && isSequenceLine(lines_[lineIndex_], indent)) {
      if (output.size() >= limits_.maximumCollectionEntries) return parseFailure("USTX collection entry limit exceeded", lines_[lineIndex_].number);
      const auto line = lines_[lineIndex_];
      const auto body = trim(std::string_view{line.text}.substr(1U));
      ++lineIndex_;
      if (body.empty()) {
        if (lineIndex_ < lines_.size() && lines_[lineIndex_].indent > indent) {
          auto nested = parseBlock(depth + 1U, lines_[lineIndex_].indent);
          if (!nested) return nested;
          output.push_back(std::move(nested).value());
        } else output.emplace_back(nullptr);
        continue;
      }
      const auto colon = mappingColon(body);
      if (colon == std::string_view::npos) {
        auto item = parseInline(body, line.number);
        if (!item) return item;
        output.push_back(std::move(item).value());
        continue;
      }
      if (++nodes_ > limits_.maximumNodes) return parseFailure("USTX node limit exceeded", line.number);
      Object item;
      auto key = parseInline(body.substr(0U, colon), line.number);
      if (!key || !key.value().isString()) return parseFailure("USTX sequence mapping key is invalid", line.number);
      auto firstValue = parseSequenceEntryValue(depth, indent, body.substr(colon + 1U), line.number);
      if (!firstValue) return firstValue;
      item.emplace(key.value().asString(), std::move(firstValue).value());
      if (lineIndex_ < lines_.size() && lines_[lineIndex_].indent > indent) {
        const auto continuationIndent = lines_[lineIndex_].indent;
        if (isSequenceLine(lines_[lineIndex_], continuationIndent)) return parseFailure("USTX sequence mapping continuation is invalid", lines_[lineIndex_].number);
        auto continuation = parseMappingEntries(depth + 1U, continuationIndent, item);
        if (!continuation) return core::Result<Node>{continuation.error()};
      }
      output.push_back(Node{std::move(item)});
    }
    return Node{std::move(output)};
  }

  core::Result<Node> parseSequenceEntryValue(std::size_t depth, std::size_t parentIndent,
                                             std::string_view raw, std::size_t line) {
    const auto value = trim(raw);
    if (!value.empty()) return parseInline(value, line);
    if (lineIndex_ < lines_.size() && lines_[lineIndex_].indent > parentIndent)
      return parseBlock(depth + 1U, lines_[lineIndex_].indent);
    return Node{nullptr};
  }

  core::Result<void> parseMappingEntries(std::size_t depth, std::size_t indent, Object& output) {
    while (lineIndex_ < lines_.size() && lines_[lineIndex_].indent == indent && !isSequenceLine(lines_[lineIndex_], indent)) {
      const auto line = lines_[lineIndex_];
      const auto colon = mappingColon(line.text);
      if (colon == std::string_view::npos || colon == 0U) return core::failure(core::ErrorCode::ParseError, "USTX mapping continuation requires a key and colon", "line " + std::to_string(line.number));
      auto key = parseInline(line.text.substr(0U, colon), line.number);
      if (!key || !key.value().isString()) return core::failure(core::ErrorCode::ParseError, "USTX mapping continuation key is invalid", "line " + std::to_string(line.number));
      auto value = parseEntryValue(depth, indent, line.text.substr(colon + 1U), line.number);
      if (!value) return core::Result<void>{value.error()};
      if (!output.emplace(key.value().asString(), std::move(value).value()).second) return core::failure(core::ErrorCode::ParseError, "USTX mapping contains a duplicate key", "line " + std::to_string(line.number));
    }
    return core::success();
  }

  std::string input_;
  const UstxLimits& limits_;
  std::vector<Line> lines_;
  std::size_t lineIndex_{0U};
  std::size_t nodes_{0U};
};

const Node* find(const Node& node, std::string_view key) noexcept { return node.find(key); }

core::Result<const Node*> required(const Node& object, std::string_view key, bool (*predicate)(const Node&),
                                   std::string_view type, std::string_view path) {
  const auto* value = find(object, key);
  if (!value) return core::failure<const Node*>(core::ErrorCode::ParseError, "Missing USTX field " + std::string(path) + "." + std::string(key));
  if (!predicate(*value)) return core::failure<const Node*>(core::ErrorCode::ParseError, "USTX field " + std::string(path) + "." + std::string(key) + " must be " + std::string(type));
  return value;
}

bool isArray(const Node& node) { return node.isArray(); }
bool isString(const Node& node) { return node.isString(); }
bool isNumber(const Node& node) { return node.isNumber(); }

core::Result<std::string> stringValue(const Node& value, std::string_view path, const UstxLimits& limits) {
  if (!value.isString() || value.asString().size() > limits.maximumScalarBytes || !domain::fromUtf8(value.asString()))
    return core::failure<std::string>(core::ErrorCode::ParseError, "USTX field " + std::string(path) + " must be a bounded UTF-8 string");
  return value.asString();
}

core::Result<std::int64_t> integerValue(const Node& value, std::string_view path, std::int64_t minimum,
                                        std::int64_t maximum) {
  if (!value.isNumber()) return core::failure<std::int64_t>(core::ErrorCode::ParseError, "USTX field " + std::string(path) + " must be an integer");
  double number = value.asNumber();
  if (!std::isfinite(number) || std::floor(number) != number || number < static_cast<double>(minimum) || number > static_cast<double>(maximum))
    return core::failure<std::int64_t>(core::ErrorCode::ParseError, "USTX field " + std::string(path) + " is outside its integer bounds");
  return value.isInteger() ? value.asInt64() : static_cast<std::int64_t>(number);
}

core::Result<double> numberValue(const Node& value, std::string_view path, double minimum, double maximum) {
  if (!value.isNumber() || !std::isfinite(value.asNumber()) || value.asNumber() < minimum || value.asNumber() > maximum)
    return core::failure<double>(core::ErrorCode::ParseError, "USTX field " + std::string(path) + " is outside its numeric bounds");
  return value.asNumber();
}

core::Result<bool> boolValue(const Node& value, std::string_view path) {
  if (!value.isBool()) return core::failure<bool>(core::ErrorCode::ParseError, "USTX field " + std::string(path) + " must be boolean");
  return value.asBool();
}

const Node* optional(const Node& object, std::string_view key) noexcept { return find(object, key); }

void issue(std::vector<UstxIssue>& issues, UstxIssueSeverity severity, std::string path, std::string message,
           const UstxLimits& limits) {
  if (issues.size() < std::min<std::size_t>(limits.maximumNodes, 4'096U)) issues.push_back({severity, std::move(path), std::move(message)});
}

void reportUnknown(const Node& object, std::initializer_list<std::string_view> known,
                   std::string_view path, std::vector<UstxIssue>& issues, const UstxLimits& limits) {
  if (!object.isObject()) return;
  for (const auto& [key, unused] : object.asObject()) {
    if (std::find(known.begin(), known.end(), key) == known.end())
      issue(issues, UstxIssueSeverity::Loss, std::string(path) + "." + key, "USTX field is outside the supported typed subset", limits);
  }
}

core::Result<UstxPitchPoint> decodePitch(const Node& value, std::string_view path,
                                         const UstxLimits& limits, std::vector<UstxIssue>& issues) {
  if (!value.isObject()) return core::failure<UstxPitchPoint>(core::ErrorCode::ParseError, "USTX pitch point must be an object");
  const auto x = required(value, "x", isNumber, "number", path); if (!x) return core::Result<UstxPitchPoint>{x.error()};
  const auto y = required(value, "y", isNumber, "number", path); if (!y) return core::Result<UstxPitchPoint>{y.error()};
  auto xValue = numberValue(*x.value(), std::string(path) + ".x", 0.0, 86'400'000.0); if (!xValue) return core::Result<UstxPitchPoint>{xValue.error()};
  auto yValue = numberValue(*y.value(), std::string(path) + ".y", -480.0, 480.0); if (!yValue) return core::Result<UstxPitchPoint>{yValue.error()};
  UstxPitchPoint result{.offsetMilliseconds = xValue.value(), .y = yValue.value(), .shape = "l"};
  if (const auto* shape = optional(value, "shape")) {
    auto parsed = stringValue(*shape, std::string(path) + ".shape", limits); if (!parsed) return core::Result<UstxPitchPoint>{parsed.error()};
    result.shape = std::move(parsed).value();
    if (result.shape.empty() || result.shape.size() > 16U) return core::failure<UstxPitchPoint>(core::ErrorCode::ParseError, "USTX pitch shape is invalid");
  }
  reportUnknown(value, {"x", "y", "shape"}, path, issues, limits);
  return result;
}

core::Result<UstxNote> decodeNote(const Node& value, std::string_view path,
                                  const UstxLimits& limits, std::vector<UstxIssue>& issues) {
  if (!value.isObject()) return core::failure<UstxNote>(core::ErrorCode::ParseError, "USTX note must be an object");
  const auto position = required(value, "position", isNumber, "integer", path); if (!position) return core::Result<UstxNote>{position.error()};
  const auto duration = required(value, "duration", isNumber, "integer", path); if (!duration) return core::Result<UstxNote>{duration.error()};
  const auto tone = required(value, "tone", isNumber, "integer", path); if (!tone) return core::Result<UstxNote>{tone.error()};
  const auto lyric = required(value, "lyric", isString, "string", path); if (!lyric) return core::Result<UstxNote>{lyric.error()};
  auto positionValue = integerValue(*position.value(), std::string(path) + ".position", 0, limits.maximumTick); if (!positionValue) return core::Result<UstxNote>{positionValue.error()};
  auto durationValue = integerValue(*duration.value(), std::string(path) + ".duration", 1, limits.maximumTick); if (!durationValue) return core::Result<UstxNote>{durationValue.error()};
  auto toneValue = integerValue(*tone.value(), std::string(path) + ".tone", 0, 127); if (!toneValue) return core::Result<UstxNote>{toneValue.error()};
  auto lyricValue = stringValue(*lyric.value(), std::string(path) + ".lyric", limits); if (!lyricValue) return core::Result<UstxNote>{lyricValue.error()};
  UstxNote result{.position = time::Tick{positionValue.value()}, .duration = time::Tick{durationValue.value()}, .tone = static_cast<std::uint8_t>(toneValue.value()), .lyric = std::move(lyricValue).value()};
  if (const auto* tuning = optional(value, "tuning")) { auto parsed = numberValue(*tuning, std::string(path) + ".tuning", -4800.0, 4800.0); if (!parsed) return core::Result<UstxNote>{parsed.error()}; result.tuning = parsed.value(); }
  if (const auto* pitch = optional(value, "pitch")) {
    if (!pitch->isObject()) return core::failure<UstxNote>(core::ErrorCode::ParseError, "USTX note pitch must be an object");
    const auto* data = optional(*pitch, "data");
    if (!data) return core::failure<UstxNote>(core::ErrorCode::ParseError, "USTX note pitch.data is required");
    if (!data->isArray() || data->asArray().size() > limits.maximumCurvePoints) return core::failure<UstxNote>(core::ErrorCode::InvalidArgument, "USTX pitch point count exceeds bounds");
    for (std::size_t index = 0U; index < data->asArray().size(); ++index) {
      auto point = decodePitch(data->asArray()[index], std::string(path) + ".pitch.data[" + std::to_string(index) + "]", limits, issues);
      if (!point) return core::Result<UstxNote>{point.error()};
      result.pitch.push_back(std::move(point).value());
    }
    if (const auto* snap = optional(*pitch, "snap_first")) { auto parsed = boolValue(*snap, std::string(path) + ".pitch.snap_first"); if (!parsed) return core::Result<UstxNote>{parsed.error()}; result.snapFirst = parsed.value(); }
    reportUnknown(*pitch, {"data", "snap_first"}, std::string(path) + ".pitch", issues, limits);
  }
  if (const auto* vibrato = optional(value, "vibrato")) {
    if (!vibrato->isObject()) return core::failure<UstxNote>(core::ErrorCode::ParseError, "USTX note vibrato must be an object");
    const auto read = [&](std::string_view key, double minimum, double maximum, double fallback) -> core::Result<double> {
      const auto* field = optional(*vibrato, key); if (!field) return fallback;
      return numberValue(*field, std::string(path) + ".vibrato." + std::string(key), minimum, maximum);
    };
    auto length = read("length", 0.0, 100.0, 0.0); if (!length) return core::Result<UstxNote>{length.error()};
    auto period = read("period", 1.0, 2'000.0, 175.0); if (!period) return core::Result<UstxNote>{period.error()};
    auto depth = read("depth", 0.0, 2'000.0, 25.0); if (!depth) return core::Result<UstxNote>{depth.error()};
    auto fadeIn = read("in", 0.0, 100.0, 10.0); if (!fadeIn) return core::Result<UstxNote>{fadeIn.error()};
    auto fadeOut = read("out", 0.0, 100.0, 10.0); if (!fadeOut) return core::Result<UstxNote>{fadeOut.error()};
    auto shift = read("shift", -100.0, 100.0, 0.0); if (!shift) return core::Result<UstxNote>{shift.error()};
    auto drift = read("drift", -100.0, 100.0, 0.0); if (!drift) return core::Result<UstxNote>{drift.error()};
    auto volume = read("vol_link", -100.0, 100.0, 0.0); if (!volume) return core::Result<UstxNote>{volume.error()};
    result.vibrato = {length.value(), period.value(), depth.value(), fadeIn.value(), fadeOut.value(), shift.value(), drift.value(), volume.value()};
    result.hasVibrato = length.value() > 0.0;
    reportUnknown(*vibrato, {"length", "period", "depth", "in", "out", "shift", "drift", "vol_link"}, std::string(path) + ".vibrato", issues, limits);
  }
  reportUnknown(value, {"position", "duration", "tone", "lyric", "pitch", "vibrato", "tuning", "phoneme_expressions", "phoneme_overrides", "phonemizer"}, path, issues, limits);
  for (const auto field : {"phoneme_expressions", "phoneme_overrides", "phonemizer"}) if (optional(value, field)) issue(issues, UstxIssueSeverity::Loss, std::string(path) + "." + field, "phoneme-level detail is not represented in the SEAM interchange subset", limits);
  return result;
}

core::Result<UstxDocument> decodeNode(const Node& root, const UstxLimits& limits) {
  using Output = UstxDocument;
  if (!root.isObject()) return core::failure<Output>(core::ErrorCode::ParseError, "USTX root must be a mapping");
  Output document;
  const auto version = required(root, "ustx_version", isString, "string", "ustx"); if (!version) return core::Result<Output>{version.error()};
  auto versionValue = stringValue(*version.value(), "ustx.ustx_version", limits); if (!versionValue) return core::Result<Output>{versionValue.error()};
  if (versionValue.value() != "0.9") return core::failure<Output>(core::ErrorCode::Unsupported, "Only USTX version 0.9 is supported");
  document.version = std::move(versionValue).value();
  if (const auto* name = optional(root, "name")) { auto parsed = stringValue(*name, "ustx.name", limits); if (!parsed) return core::Result<Output>{parsed.error()}; document.name = std::move(parsed).value(); }
  const auto tempos = required(root, "tempos", isArray, "array", "ustx"); if (!tempos) return core::Result<Output>{tempos.error()};
  const auto meters = required(root, "time_signatures", isArray, "array", "ustx"); if (!meters) return core::Result<Output>{meters.error()};
  const auto tracks = required(root, "tracks", isArray, "array", "ustx"); if (!tracks) return core::Result<Output>{tracks.error()};
  const auto parts = required(root, "voice_parts", isArray, "array", "ustx"); if (!parts) return core::Result<Output>{parts.error()};
  if (tempos.value()->asArray().size() > limits.maximumTempoEvents || meters.value()->asArray().size() > limits.maximumMeterEvents || tracks.value()->asArray().size() > limits.maximumTracks || parts.value()->asArray().size() > limits.maximumParts)
    return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX collection count exceeds bounds");
  for (std::size_t index = 0U; index < tempos.value()->asArray().size(); ++index) {
    const auto path = "ustx.tempos[" + std::to_string(index) + "]"; const auto& item = tempos.value()->asArray()[index];
    if (!item.isObject()) return core::failure<Output>(core::ErrorCode::ParseError, path + " must be an object");
    const auto position = required(item, "position", isNumber, "integer", path); if (!position) return core::Result<Output>{position.error()};
    const auto bpm = required(item, "bpm", isNumber, "number", path); if (!bpm) return core::Result<Output>{bpm.error()};
    auto p = integerValue(*position.value(), path + ".position", 0, limits.maximumTick); if (!p) return core::Result<Output>{p.error()};
    auto b = numberValue(*bpm.value(), path + ".bpm", 0.000001, 1'000.0); if (!b) return core::Result<Output>{b.error()};
    document.tempos.push_back({time::Tick{p.value()}, b.value()}); reportUnknown(item, {"position", "bpm"}, path, document.issues, limits);
  }
  for (std::size_t index = 0U; index < meters.value()->asArray().size(); ++index) {
    const auto path = "ustx.time_signatures[" + std::to_string(index) + "]"; const auto& item = meters.value()->asArray()[index];
    if (!item.isObject()) return core::failure<Output>(core::ErrorCode::ParseError, path + " must be an object");
    const auto bar = required(item, "bar_position", isNumber, "integer", path); if (!bar) return core::Result<Output>{bar.error()};
    const auto numerator = required(item, "beat_per_bar", isNumber, "integer", path); if (!numerator) return core::Result<Output>{numerator.error()};
    const auto denominator = required(item, "beat_unit", isNumber, "integer", path); if (!denominator) return core::Result<Output>{denominator.error()};
    auto barValue = integerValue(*bar.value(), path + ".bar_position", 0, limits.maximumTick); if (!barValue) return core::Result<Output>{barValue.error()};
    auto numeratorValue = integerValue(*numerator.value(), path + ".beat_per_bar", 1, 32); if (!numeratorValue) return core::Result<Output>{numeratorValue.error()};
    auto denominatorValue = integerValue(*denominator.value(), path + ".beat_unit", 1, 32); if (!denominatorValue || (denominatorValue.value() != 1 && denominatorValue.value() != 2 && denominatorValue.value() != 4 && denominatorValue.value() != 8 && denominatorValue.value() != 16 && denominatorValue.value() != 32)) return core::failure<Output>(core::ErrorCode::Unsupported, path + ".beat_unit is unsupported");
    document.meters.push_back({barValue.value(), static_cast<std::uint8_t>(numeratorValue.value()), static_cast<std::uint8_t>(denominatorValue.value())}); reportUnknown(item, {"bar_position", "beat_per_bar", "beat_unit"}, path, document.issues, limits);
  }
  for (std::size_t index = 0U; index < tracks.value()->asArray().size(); ++index) {
    const auto path = "ustx.tracks[" + std::to_string(index) + "]"; const auto& item = tracks.value()->asArray()[index];
    if (!item.isObject()) return core::failure<Output>(core::ErrorCode::ParseError, path + " must be an object");
    UstxTrack track;
    if (const auto* name = optional(item, "track_name")) { auto parsed = stringValue(*name, path + ".track_name", limits); if (!parsed) return core::Result<Output>{parsed.error()}; track.name = std::move(parsed).value(); }
    if (const auto* singer = optional(item, "singer")) { auto parsed = stringValue(*singer, path + ".singer", limits); if (!parsed) return core::Result<Output>{parsed.error()}; track.singer = std::move(parsed).value(); }
    const auto readNumber = [&](std::string_view key, double minimum, double maximum, double fallback) -> core::Result<double> { const auto* field = optional(item, key); return field ? numberValue(*field, path + "." + std::string(key), minimum, maximum) : core::success(fallback); };
    auto volume = readNumber("volume", -120.0, 24.0, 0.0); if (!volume) return core::Result<Output>{volume.error()}; track.volume = volume.value();
    auto pan = readNumber("pan", -1.0, 1.0, 0.0); if (!pan) return core::Result<Output>{pan.error()}; track.pan = pan.value();
    if (const auto* mute = optional(item, "mute")) { auto parsed = boolValue(*mute, path + ".mute"); if (!parsed) return core::Result<Output>{parsed.error()}; track.mute = parsed.value(); }
    if (const auto* solo = optional(item, "solo")) { auto parsed = boolValue(*solo, path + ".solo"); if (!parsed) return core::Result<Output>{parsed.error()}; track.solo = parsed.value(); }
    if (const auto* colors = optional(item, "voice_color_names")) {
      if (!colors->isArray() || colors->asArray().size() > limits.maximumCollectionEntries) return core::failure<Output>(core::ErrorCode::InvalidArgument, path + ".voice_color_names exceeds bounds");
      for (std::size_t color = 0U; color < colors->asArray().size(); ++color) { auto parsed = stringValue(colors->asArray()[color], path + ".voice_color_names[" + std::to_string(color) + "]", limits); if (!parsed) return core::Result<Output>{parsed.error()}; track.voiceColors.push_back(std::move(parsed).value()); }
    }
    reportUnknown(item, {"singer", "track_name", "track_color", "mute", "solo", "volume", "pan", "track_expressions", "voice_color_names", "phonemizer", "renderer_settings", "mix_fx"}, path, document.issues, limits);
    for (const auto field : {"track_expressions", "phonemizer", "renderer_settings", "mix_fx"}) if (optional(item, field)) issue(document.issues, UstxIssueSeverity::Loss, path + "." + field, "track metadata is not represented in the SEAM interchange subset", limits);
    document.tracks.push_back(std::move(track));
  }
  std::size_t noteCount = 0U;
  for (std::size_t index = 0U; index < parts.value()->asArray().size(); ++index) {
    const auto path = "ustx.voice_parts[" + std::to_string(index) + "]"; const auto& item = parts.value()->asArray()[index];
    if (!item.isObject()) return core::failure<Output>(core::ErrorCode::ParseError, path + " must be an object");
    UstxPart part;
    if (const auto* name = optional(item, "name")) { auto parsed = stringValue(*name, path + ".name", limits); if (!parsed) return core::Result<Output>{parsed.error()}; part.name = std::move(parsed).value(); }
    const auto trackNo = required(item, "track_no", isNumber, "integer", path); if (!trackNo) return core::Result<Output>{trackNo.error()};
    const auto position = required(item, "position", isNumber, "integer", path); if (!position) return core::Result<Output>{position.error()};
    const auto duration = required(item, "duration", isNumber, "integer", path); if (!duration) return core::Result<Output>{duration.error()};
    auto trackValue = integerValue(*trackNo.value(), path + ".track_no", 0, static_cast<std::int64_t>(limits.maximumTracks - 1U)); if (!trackValue) return core::Result<Output>{trackValue.error()};
    auto positionValue = integerValue(*position.value(), path + ".position", 0, limits.maximumTick); if (!positionValue) return core::Result<Output>{positionValue.error()};
    auto durationValue = integerValue(*duration.value(), path + ".duration", 1, limits.maximumTick); if (!durationValue) return core::Result<Output>{durationValue.error()};
    part.trackNo = static_cast<std::uint32_t>(trackValue.value()); part.position = time::Tick{positionValue.value()}; part.duration = time::Tick{durationValue.value()};
    const auto notes = required(item, "notes", isArray, "array", path); if (!notes) return core::Result<Output>{notes.error()};
    if (notes.value()->asArray().size() > limits.maximumNotes - std::min(noteCount, limits.maximumNotes)) return core::failure<Output>(core::ErrorCode::InvalidArgument, "USTX note count exceeds bounds");
    for (std::size_t noteIndex = 0U; noteIndex < notes.value()->asArray().size(); ++noteIndex) {
      auto note = decodeNote(notes.value()->asArray()[noteIndex], path + ".notes[" + std::to_string(noteIndex) + "]", limits, document.issues); if (!note) return core::Result<Output>{note.error()}; part.notes.push_back(std::move(note).value()); ++noteCount;
    }
    if (const auto* curves = optional(item, "curves")) if (curves->isArray() && !curves->asArray().empty()) issue(document.issues, UstxIssueSeverity::Loss, path + ".curves", "part curves are not represented in the SEAM interchange subset", limits);
    reportUnknown(item, {"name", "comment", "track_no", "position", "duration", "curves", "notes"}, path, document.issues, limits);
    document.parts.push_back(std::move(part));
  }
  reportUnknown(root, {"ustx_version", "name", "comment", "output_dir", "cache_dir", "expressions", "exp_selectors", "exp_primary", "exp_secondary", "key", "time_signatures", "tempos", "tracks", "voice_parts", "wave_parts"}, "ustx", document.issues, limits);
  for (const auto field : {"expressions", "wave_parts"}) if (const auto* value = optional(root, field); value && ((!value->isObject() && !value->isArray()) || (value->isObject() && !value->asObject().empty()) || (value->isArray() && !value->asArray().empty()))) issue(document.issues, UstxIssueSeverity::Loss, std::string("ustx.") + field, "USTX field is not represented in the SEAM interchange subset", limits);
  const auto valid = document.validate(limits); if (!valid) return core::Result<Output>{valid.error()};
  return document;
}

std::string quote(std::string_view value) {
  std::string output{"\""};
  for (const auto character : value) {
    switch (character) {
      case '\\': output += "\\\\"; break;
      case '"': output += "\\\""; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      case '\0': output += "\\0"; break;
      default: output.push_back(character); break;
    }
  }
  output.push_back('"'); return output;
}

std::string number(double value) {
  if (value == 0.0) return "0";
  std::ostringstream stream; stream.setf(std::ios::fmtflags(0), std::ios::floatfield); stream.precision(12); stream << value;
  return stream.str();
}

std::string flowPitch(const UstxPitchPoint& point) {
  return "{x: " + number(point.offsetMilliseconds) + ", y: " + number(point.y) + ", shape: " + point.shape + "}";
}

std::string flowVibrato(const UstxVibrato& vibrato) {
  return "{length: " + number(vibrato.length) + ", period: " + number(vibrato.period) + ", depth: " + number(vibrato.depth) + ", in: " + number(vibrato.fadeIn) + ", out: " + number(vibrato.fadeOut) + ", shift: " + number(vibrato.shift) + ", drift: " + number(vibrato.drift) + ", vol_link: " + number(vibrato.volumeLink) + "}";
}

}  // namespace

core::Result<void> UstxDocument::validate(const UstxLimits& limits) const {
  if (limits.maximumInputBytes == 0U || limits.maximumTracks == 0U || limits.maximumParts == 0U || limits.maximumNotes == 0U ||
      version != "0.9" || name.size() > limits.maximumScalarBytes || !domain::fromUtf8(name) ||
      tempos.empty() || meters.empty() || tempos.size() > limits.maximumTempoEvents || meters.size() > limits.maximumMeterEvents ||
      tracks.size() > limits.maximumTracks || parts.size() > limits.maximumParts ||
      issues.size() > std::min<std::size_t>(limits.maximumNodes, 4'096U))
    return core::failure(core::ErrorCode::InvalidArgument, "USTX document exceeds declared bounds");
  if (tempos.front().position != time::Tick{0} || meters.front().barPosition != 0) return core::failure(core::ErrorCode::InvalidArgument, "USTX tempo and meter maps must begin at zero");
  for (std::size_t index = 0U; index < tempos.size(); ++index) {
    const auto& tempo = tempos[index]; if (tempo.position.value() < 0 || tempo.position.value() > limits.maximumTick || !std::isfinite(tempo.bpm) || tempo.bpm <= 0.0 || tempo.bpm > 1'000.0 || (index > 0U && tempos[index - 1U].position >= tempo.position)) return core::failure(core::ErrorCode::InvalidArgument, "USTX tempo map is invalid");
  }
  for (std::size_t index = 0U; index < meters.size(); ++index) {
    const auto& meter = meters[index]; if (meter.barPosition < 0 || meter.barPosition > limits.maximumTick || meter.numerator == 0U || meter.numerator > 32U || (meter.denominator != 1U && meter.denominator != 2U && meter.denominator != 4U && meter.denominator != 8U && meter.denominator != 16U && meter.denominator != 32U) || (index > 0U && meters[index - 1U].barPosition >= meter.barPosition)) return core::failure(core::ErrorCode::InvalidArgument, "USTX meter map is invalid");
  }
  std::size_t noteCount = 0U, curveCount = 0U;
  for (const auto& track : tracks) {
    if (track.name.size() > limits.maximumScalarBytes || track.singer.size() > limits.maximumScalarBytes || !domain::fromUtf8(track.name) || !domain::fromUtf8(track.singer) || !std::isfinite(track.volume) || track.volume < -120.0 || track.volume > 24.0 || !std::isfinite(track.pan) || track.pan < -1.0 || track.pan > 1.0) return core::failure(core::ErrorCode::InvalidArgument, "USTX track is invalid");
    for (const auto& color : track.voiceColors) if (color.size() > limits.maximumScalarBytes || !domain::fromUtf8(color)) return core::failure(core::ErrorCode::InvalidArgument, "USTX voice color is invalid");
  }
  for (const auto& part : parts) {
    if (part.trackNo >= tracks.size() || part.name.size() > limits.maximumScalarBytes || !domain::fromUtf8(part.name) || part.position.value() < 0 || part.duration.value() <= 0 || part.position.value() > limits.maximumTick || part.duration.value() > limits.maximumTick - part.position.value() || part.notes.size() > limits.maximumNotes - std::min(noteCount, limits.maximumNotes)) return core::failure(core::ErrorCode::InvalidArgument, "USTX part is invalid");
    for (const auto& note : part.notes) {
      ++noteCount; if (note.position.value() < 0 || note.duration.value() <= 0 || note.position.value() > limits.maximumTick || note.duration.value() > limits.maximumTick - note.position.value() || note.position.value() + note.duration.value() > part.duration.value() || note.tone > 127U || note.lyric.size() > limits.maximumScalarBytes || !domain::fromUtf8(note.lyric) || !std::isfinite(note.tuning) || note.tuning < -4800.0 || note.tuning > 4800.0) return core::failure(core::ErrorCode::InvalidArgument, "USTX note is invalid");
      for (const auto& point : note.pitch) { ++curveCount; if (curveCount > limits.maximumCurvePoints || !std::isfinite(point.offsetMilliseconds) || point.offsetMilliseconds < 0.0 || point.offsetMilliseconds > 86'400'000.0 || !std::isfinite(point.y) || point.y < -480.0 || point.y > 480.0 || point.shape.empty() || point.shape.size() > 16U || !domain::fromUtf8(point.shape)) return core::failure(core::ErrorCode::InvalidArgument, "USTX pitch point is invalid"); }
      if (note.hasVibrato && (!std::isfinite(note.vibrato.length) || note.vibrato.length <= 0.0 || note.vibrato.length > 100.0 || !std::isfinite(note.vibrato.period) || note.vibrato.period < 1.0 || note.vibrato.period > 2'000.0 || !std::isfinite(note.vibrato.depth) || note.vibrato.depth < 0.0 || note.vibrato.depth > 2'000.0 || !std::isfinite(note.vibrato.fadeIn) || note.vibrato.fadeIn < 0.0 || note.vibrato.fadeIn > 100.0 || !std::isfinite(note.vibrato.fadeOut) || note.vibrato.fadeOut < 0.0 || note.vibrato.fadeOut > 100.0)) return core::failure(core::ErrorCode::InvalidArgument, "USTX vibrato is invalid");
    }
  }
  return core::success();
}

core::Result<UstxDocument> decodeUstx(std::span<const std::uint8_t> bytes, UstxLimits limits) {
  if (bytes.empty() || bytes.size() > limits.maximumInputBytes || limits.maximumDepth == 0U ||
      limits.maximumNodes == 0U || limits.maximumCollectionEntries == 0U ||
      limits.maximumScalarBytes == 0U || limits.maximumTracks == 0U ||
      limits.maximumParts == 0U || limits.maximumNotes == 0U ||
      limits.maximumTempoEvents == 0U || limits.maximumMeterEvents == 0U ||
      limits.maximumCurvePoints == 0U || limits.maximumTick <= 0)
    return core::failure<UstxDocument>(core::ErrorCode::InvalidArgument, "USTX input or parser limits are invalid");
  std::string input{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
  if (!domain::fromUtf8(input)) return core::failure<UstxDocument>(core::ErrorCode::ParseError, "USTX input must be valid UTF-8");
  BlockParser parser{std::move(input), limits};
  const auto root = parser.parse(); if (!root) return core::Result<UstxDocument>{root.error()};
  return decodeNode(root.value(), limits);
}

core::Result<std::vector<std::uint8_t>> encodeUstx(const UstxDocument& document, UstxLimits limits) {
  const auto valid = document.validate(limits); if (!valid) return core::Result<std::vector<std::uint8_t>>{valid.error()};
  std::string output;
  output.reserve(1024U);
  output += "ustx_version: \"0.9\"\n";
  output += "name: " + quote(document.name.empty() ? "SEAM USTX Export" : document.name) + "\n";
  output += "comment: \"Generated by Project SEAM native interchange\"\n";
  output += "output_dir: \"Vocal\"\ncache_dir: \"UCache\"\nexpressions: {}\n";
  output += "exp_selectors: [dyn, pitd, clr, eng, vel, vol, atk, dec, gen, bre]\nexp_primary: 0\nexp_secondary: 1\nkey: 0\n";
  output += "time_signatures:\n";
  for (const auto& meter : document.meters) output += "  - {bar_position: " + std::to_string(meter.barPosition) + ", beat_per_bar: " + std::to_string(meter.numerator) + ", beat_unit: " + std::to_string(meter.denominator) + "}\n";
  output += "tempos:\n";
  for (const auto& tempo : document.tempos) output += "  - {position: " + std::to_string(tempo.position.value()) + ", bpm: " + number(tempo.bpm) + "}\n";
  output += "tracks:\n";
  for (const auto& track : document.tracks) {
    output += "  - singer: " + quote(track.singer) + "\n    track_name: " + quote(track.name) + "\n    track_color: Blue\n    mute: " + std::string(track.mute ? "true" : "false") + "\n    solo: " + std::string(track.solo ? "true" : "false") + "\n    volume: " + number(track.volume) + "\n    pan: " + number(track.pan) + "\n    track_expressions: []\n    voice_color_names: [";
    if (track.voiceColors.empty()) output += "\"\"";
    else for (std::size_t index = 0U; index < track.voiceColors.size(); ++index) { if (index > 0U) output += ", "; output += quote(track.voiceColors[index]); }
    output += "]\n";
  }
  output += "voice_parts:\n";
  for (const auto& part : document.parts) {
    output += "  - name: " + quote(part.name) + "\n    comment: \"\"\n    track_no: " + std::to_string(part.trackNo) + "\n    position: " + std::to_string(part.position.value()) + "\n    duration: " + std::to_string(part.duration.value()) + "\n    curves: []\n    notes:\n";
    for (const auto& note : part.notes) {
      output += "      - position: " + std::to_string(note.position.value()) + "\n        duration: " + std::to_string(note.duration.value()) + "\n        tone: " + std::to_string(note.tone) + "\n        lyric: " + quote(note.lyric) + "\n        pitch:\n          data: [";
      for (std::size_t index = 0U; index < note.pitch.size(); ++index) { if (index > 0U) output += ", "; output += flowPitch(note.pitch[index]); }
      output += "]\n          snap_first: " + std::string(note.snapFirst ? "true" : "false") + "\n        vibrato: " + flowVibrato(note.hasVibrato ? note.vibrato : UstxVibrato{}) + "\n        tuning: " + number(note.tuning) + "\n        phoneme_expressions: []\n        phoneme_overrides: []\n";
    }
  }
  if (output.size() > limits.maximumInputBytes) return core::failure<std::vector<std::uint8_t>>(core::ErrorCode::InvalidArgument, "Encoded USTX exceeds byte bounds");
  return std::vector<std::uint8_t>{output.begin(), output.end()};
}

}  // namespace seam::interchange
