#include "seam/neural_synthesis/worker_protocol.hpp"

#include "seam/domain/note.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <string_view>

namespace seam::neural_synthesis {
namespace {

using formats::JsonValue;
using Object = JsonValue::Object;

constexpr std::array<std::byte, 4U> kMagic{
    std::byte{'S'}, std::byte{'N'}, std::byte{'W'}, std::byte{'1'}};
constexpr std::uint8_t kRequestType = 1U;
constexpr std::uint8_t kResponseType = 2U;
constexpr std::uint16_t kVersion = 1U;
constexpr std::size_t kHeaderBytes = 4U + 2U + 1U + 1U + 4U + 8U;

bool validHash(std::string_view value, std::size_t expected) noexcept {
  return value.size() == 64U && value.size() <= expected && std::all_of(value.begin(), value.end(),
      [](unsigned char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      });
}

bool validText(std::string_view value, std::size_t limit) {
  return !value.empty() && value.size() <= limit &&
      std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return character >= 0x20U && character != 0x7fU;
      }) && domain::fromUtf8(std::string{value});
}

void appendU16(std::vector<std::byte>& output, std::uint16_t value) {
  output.push_back(static_cast<std::byte>(value & 0xffU));
  output.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void appendU32(std::vector<std::byte>& output, std::uint32_t value) {
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
    output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

void appendU64(std::vector<std::byte>& output, std::uint64_t value) {
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
    output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

void appendFloatLE(std::vector<std::byte>& output, float value) {
  std::uint32_t bits = 0U;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  appendU32(output, bits);
}

std::uint16_t readU16(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset])) |
      static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset + 1U]) << 8U);
}

std::uint32_t readU32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  std::uint32_t value = 0U;
  for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
    value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + shift / 8U])) << shift;
  return value;
}

std::uint64_t readU64(std::span<const std::byte> bytes, std::size_t offset) noexcept {
  std::uint64_t value = 0U;
  for (std::uint32_t shift = 0U; shift < 64U; shift += 8U)
    value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(bytes[offset + shift / 8U])) << shift;
  return value;
}

core::Result<std::string> requiredString(const JsonValue& root,
                                         std::string_view key,
                                         std::size_t limit) {
  const auto* value = root.find(key);
  if (value == nullptr || !value->isString() || !validText(value->asString(), limit))
    return core::failure<std::string>(core::ErrorCode::ParseError,
        "Neural worker metadata string is invalid", std::string(key));
  return value->asString();
}

bool hasOnlyKeys(const JsonValue& root,
                 std::initializer_list<std::string_view> expected) {
  if (!root.isObject() || root.asObject().size() != expected.size()) return false;
  for (const auto& [key, unused] : root.asObject()) {
    if (std::find(expected.begin(), expected.end(), key) == expected.end()) return false;
  }
  return true;
}

core::Result<std::uint64_t> requiredUnsigned(const JsonValue& root,
                                             std::string_view key,
                                             std::uint64_t maximum) {
  const auto* value = root.find(key);
  if (value == nullptr || !value->isNumber() || value->asNumber() < 0.0 ||
      value->asNumber() > static_cast<double>(maximum) ||
      std::floor(value->asNumber()) != value->asNumber())
    return core::failure<std::uint64_t>(core::ErrorCode::ParseError,
        "Neural worker metadata integer is invalid", std::string(key));
  return value->isInteger() ? static_cast<std::uint64_t>(value->asInt64())
                            : static_cast<std::uint64_t>(value->asNumber());
}

core::Result<std::uint32_t> requiredRate(const JsonValue& root,
                                         std::string_view key,
                                         std::uint32_t maximum) {
  auto value = requiredUnsigned(root, key, maximum);
  if (!value) return core::Result<std::uint32_t>{value.error()};
  if (value.value() > std::numeric_limits<std::uint32_t>::max())
    return core::failure<std::uint32_t>(core::ErrorCode::ParseError,
                                        "Neural sample rate is invalid");
  return static_cast<std::uint32_t>(value.value());
}

core::Result<std::uint8_t> requiredChannels(const JsonValue& root,
                                            const WorkerProtocolLimits& limits) {
  auto value = requiredUnsigned(root, "channels", limits.maximumChannels);
  if (!value) return core::Result<std::uint8_t>{value.error()};
  return static_cast<std::uint8_t>(value.value());
}

core::Result<std::vector<std::byte>> encodeFrame(
    std::uint8_t type, const Object& metadata, std::span<const float> payload,
    WorkerProtocolLimits limits) {
  const auto metadataText = formats::stringifyJson(JsonValue{metadata}, false);
  if (payload.size() > std::numeric_limits<std::size_t>::max() / sizeof(float))
    return core::failure<std::vector<std::byte>>(core::ErrorCode::InvalidArgument,
        "Neural worker payload size overflows");
  const auto payloadBytes = payload.size() * sizeof(float);
  if (metadataText.size() > limits.maximumMetadataBytes ||
      metadataText.size() > limits.maximumFrameBytes ||
      kHeaderBytes > limits.maximumFrameBytes - metadataText.size() ||
      payloadBytes > limits.maximumFrameBytes - kHeaderBytes - metadataText.size() ||
      metadataText.size() > std::numeric_limits<std::uint32_t>::max())
    return core::failure<std::vector<std::byte>>(core::ErrorCode::InvalidArgument,
        "Neural worker frame exceeds bounds");
  std::vector<std::byte> output;
  output.reserve(kHeaderBytes + metadataText.size() + payloadBytes);
  output.insert(output.end(), kMagic.begin(), kMagic.end());
  appendU16(output, kVersion); output.push_back(static_cast<std::byte>(type)); output.push_back(std::byte{0});
  appendU32(output, static_cast<std::uint32_t>(metadataText.size()));
  appendU64(output, static_cast<std::uint64_t>(payloadBytes));
  const auto metadataBytes = std::as_bytes(std::span{metadataText.data(), metadataText.size()});
  output.insert(output.end(), metadataBytes.begin(), metadataBytes.end());
  for (const auto value : payload) appendFloatLE(output, value);
  return output;
}

struct DecodedFrame final {
  std::uint8_t type{0U};
  JsonValue metadata;
  std::span<const std::byte> payload;
};

core::Result<DecodedFrame> decodeFrame(std::span<const std::byte> frame,
                                       std::uint8_t expectedType,
                                       WorkerProtocolLimits limits) {
  if (limits.maximumFrameBytes == 0U || limits.maximumMetadataBytes == 0U ||
      limits.maximumFrames == 0U || frame.size() < kHeaderBytes ||
      frame.size() > limits.maximumFrameBytes ||
      !std::equal(kMagic.begin(), kMagic.end(), frame.begin()) ||
      readU16(frame, 4U) != kVersion || std::to_integer<unsigned char>(frame[6U]) != expectedType ||
      std::to_integer<unsigned char>(frame[7U]) != 0U)
    return core::failure<DecodedFrame>(core::ErrorCode::ParseError,
                                        "Neural worker frame header is invalid");
  const auto metadataBytes = static_cast<std::size_t>(readU32(frame, 8U));
  const auto payloadBytes = readU64(frame, 12U);
  if (metadataBytes > limits.maximumMetadataBytes || payloadBytes > limits.maximumFrameBytes ||
      metadataBytes > frame.size() - kHeaderBytes ||
      payloadBytes != frame.size() - kHeaderBytes - metadataBytes ||
      payloadBytes % sizeof(float) != 0U)
    return core::failure<DecodedFrame>(core::ErrorCode::ParseError,
                                       "Neural worker frame lengths are invalid");
  const auto metadata = std::string{reinterpret_cast<const char*>(frame.data() + kHeaderBytes), metadataBytes};
  const auto parsed = formats::parseJson(metadata, {.maximumInputBytes = limits.maximumMetadataBytes,
      .maximumDepth = 4U, .maximumNodes = 32768U, .maximumStringBytes = limits.maximumMetadataBytes,
      .maximumCollectionEntries = 4096U});
  if (!parsed || !parsed.value().isObject()) return core::failure<DecodedFrame>(core::ErrorCode::ParseError, "Neural worker metadata JSON is invalid");
  return DecodedFrame{expectedType, parsed.value(), frame.subspan(kHeaderBytes + metadataBytes, static_cast<std::size_t>(payloadBytes))};
}

core::Result<std::vector<float>> decodeFloats(std::span<const std::byte> payload,
                                              std::uint64_t count) {
  if (count > std::numeric_limits<std::size_t>::max() / sizeof(float) || payload.size() != count * sizeof(float))
    return core::failure<std::vector<float>>(core::ErrorCode::ParseError, "Neural Float32 payload size is invalid");
  std::vector<float> result(static_cast<std::size_t>(count));
  for (std::size_t index = 0U; index < result.size(); ++index) {
    const auto offset = index * sizeof(float);
    const auto bits = readU32(payload, offset);
    std::memcpy(&result[index], &bits, sizeof(bits));
  }
  for (const auto value : result) if (!std::isfinite(value)) return core::failure<std::vector<float>>(core::ErrorCode::ParseError, "Neural Float32 payload contains a non-finite value");
  return result;
}

}  // namespace

core::Result<void> PhoneticConditioning::validate(std::uint64_t frameCount,
    std::uint32_t vocabularySize,const WorkerProtocolLimits& limits) const {
  if (!validHash(vocabularyHash,limits.maximumHashBytes) || vocabularySize==0U || vocabularySize>65536U ||
      frameCount==0U || frameCount>limits.maximumFrames || spans.empty() || spans.size()>4096U)
    return core::failure(core::ErrorCode::InvalidArgument,"Neural phonetic identity or dimensions exceed bounds");
  std::uint64_t next=0U;
  for (const auto& span:spans) {
    if (span.tokenId>=vocabularySize || span.startFrame!=next || span.endFrame<=span.startFrame || span.endFrame>frameCount)
      return core::failure(core::ErrorCode::InvalidArgument,"Neural phoneme spans must be ordered, contiguous, nonempty and vocabulary-bound");
    next=span.endFrame;
  }
  if (next!=frameCount) return core::failure(core::ErrorCode::InvalidArgument,"Neural phoneme spans do not cover the complete output");
  return core::success();
}

core::Result<void> NeuralRequest::validate(const WorkerProtocolLimits& limits) const {
  if (requestId == 0U || !validText(modelId, limits.maximumModelIdBytes) ||
      !validText(modelVersion, limits.maximumModelIdBytes) ||
      !validHash(modelContentHash, limits.maximumHashBytes) ||
      !validHash(pronunciationHash, limits.maximumHashBytes) || sampleRate < 8'000U ||
      sampleRate > limits.maximumSampleRate || channels == 0U || channels > limits.maximumChannels ||
      frameCount == 0U || frameCount > limits.maximumFrames || f0Hz.size() != frameCount ||
      dynamics.size() != frameCount ||
      requestId > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return core::failure(core::ErrorCode::InvalidArgument, "Neural request identity or shape is invalid");
  for (const auto value : f0Hz) if (!std::isfinite(value) || value < 0.0F || value > 20'000.0F) return core::failure(core::ErrorCode::InvalidArgument, "Neural request F0 is invalid");
  for (const auto value : dynamics) if (!std::isfinite(value) || value < 0.0F || value > 4.0F) return core::failure(core::ErrorCode::InvalidArgument, "Neural request dynamics are invalid");
  if (conditioning) return conditioning->validate(frameCount,vocabularySize,limits);
  if (vocabularySize!=0U) return core::failure(core::ErrorCode::InvalidArgument,"Unconditioned neural request cannot declare a vocabulary size");
  return core::success();
}

core::Result<void> NeuralResponse::validate(const WorkerProtocolLimits& limits) const {
  if (!requestContentHash.empty() && !validHash(requestContentHash,limits.maximumHashBytes))
    return core::failure(core::ErrorCode::InvalidArgument,"Neural response request hash is invalid");
  if (requestId == 0U || !validText(backendId, limits.maximumModelIdBytes) ||
      !validHash(modelContentHash, limits.maximumHashBytes) || sampleRate < 8'000U ||
      sampleRate > limits.maximumSampleRate || channels == 0U || channels > limits.maximumChannels ||
      frameCount == 0U || frameCount > limits.maximumFrames ||
      frameCount > std::numeric_limits<std::size_t>::max() / channels ||
      pcm.size() != static_cast<std::size_t>(frameCount * channels) ||
      requestId > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return core::failure(core::ErrorCode::InvalidArgument, "Neural response identity or shape is invalid");
  for (const auto value : pcm) if (!std::isfinite(value) || value < -1.0F || value > 1.0F) return core::failure(core::ErrorCode::InvalidArgument, "Neural response PCM is invalid");
  return core::success();
}

core::Result<std::vector<std::byte>> encodeRequest(const NeuralRequest& request,
                                                   WorkerProtocolLimits limits) {
  const auto valid = request.validate(limits); if (!valid) return core::Result<std::vector<std::byte>>{valid.error()};
  Object metadata{{"kind", request.conditioning?"seam-neural-request-v2":"seam-neural-request-v1"}, {"requestId", JsonValue{static_cast<std::int64_t>(request.requestId)}}, {"modelId", request.modelId}, {"modelVersion", request.modelVersion}, {"modelContentHash", request.modelContentHash}, {"pronunciationHash", request.pronunciationHash}, {"sampleRate", JsonValue{static_cast<std::int64_t>(request.sampleRate)}}, {"channels", JsonValue{static_cast<std::int64_t>(request.channels)}}, {"frameCount", JsonValue{static_cast<std::int64_t>(request.frameCount)}}, {"featureKind", request.conditioning?"f0-dynamics-phonemes":"f0-dynamics"}};
  if (request.conditioning) {
    JsonValue::Array spans;
    for (const auto& span:request.conditioning->spans) spans.emplace_back(Object{
        {"tokenId",JsonValue{static_cast<std::int64_t>(span.tokenId)}},
        {"startFrame",JsonValue{static_cast<std::int64_t>(span.startFrame)}},
        {"endFrame",JsonValue{static_cast<std::int64_t>(span.endFrame)}}});
    metadata.emplace("phonemes",std::move(spans));
    metadata.emplace("vocabularyHash",request.conditioning->vocabularyHash);
    metadata.emplace("vocabularySize",JsonValue{static_cast<std::int64_t>(request.vocabularySize)});
  }
  std::vector<float> payload; payload.reserve(request.f0Hz.size() + request.dynamics.size()); payload.insert(payload.end(), request.f0Hz.begin(), request.f0Hz.end()); payload.insert(payload.end(), request.dynamics.begin(), request.dynamics.end());
  return encodeFrame(kRequestType, metadata, payload, limits);
}

core::Result<NeuralRequest> decodeRequest(std::span<const std::byte> frame,
                                          WorkerProtocolLimits limits) {
  const auto decoded = decodeFrame(frame, kRequestType, limits); if (!decoded) return core::Result<NeuralRequest>{decoded.error()};
  const auto& metadata = decoded.value().metadata;
  const auto kind = requiredString(metadata, "kind", 64U);
  if (!kind || (kind.value()!="seam-neural-request-v1" && kind.value()!="seam-neural-request-v2")) return core::failure<NeuralRequest>(core::ErrorCode::ParseError,"Neural request kind is invalid");
  const bool conditioned=kind.value()=="seam-neural-request-v2";
  if (!(conditioned?hasOnlyKeys(metadata,{"kind","requestId","modelId","modelVersion","modelContentHash","pronunciationHash","sampleRate","channels","frameCount","featureKind","vocabularyHash","vocabularySize","phonemes"}):hasOnlyKeys(metadata, {"kind", "requestId", "modelId", "modelVersion", "modelContentHash", "pronunciationHash", "sampleRate", "channels", "frameCount", "featureKind"})))
    return core::failure<NeuralRequest>(core::ErrorCode::ParseError, "Neural request metadata fields are unsupported");
  auto id = requiredUnsigned(metadata, "requestId", std::numeric_limits<std::uint64_t>::max()); if (!id) return core::Result<NeuralRequest>{id.error()};
  auto model = requiredString(metadata, "modelId", limits.maximumModelIdBytes); if (!model) return core::Result<NeuralRequest>{model.error()};
  auto version = requiredString(metadata, "modelVersion", limits.maximumModelIdBytes); if (!version) return core::Result<NeuralRequest>{version.error()};
  auto modelHash = requiredString(metadata, "modelContentHash", limits.maximumHashBytes); if (!modelHash) return core::Result<NeuralRequest>{modelHash.error()};
  auto pronunciation = requiredString(metadata, "pronunciationHash", limits.maximumHashBytes); if (!pronunciation) return core::Result<NeuralRequest>{pronunciation.error()};
  auto rate = requiredRate(metadata, "sampleRate", limits.maximumSampleRate); if (!rate) return core::Result<NeuralRequest>{rate.error()};
  auto channels = requiredChannels(metadata, limits); if (!channels) return core::Result<NeuralRequest>{channels.error()};
  auto frames = requiredUnsigned(metadata, "frameCount", limits.maximumFrames); if (!frames) return core::Result<NeuralRequest>{frames.error()};
  const auto features = requiredString(metadata, "featureKind", 64U); if (!features || features.value() != (conditioned?"f0-dynamics-phonemes":"f0-dynamics")) return core::failure<NeuralRequest>(core::ErrorCode::ParseError, "Neural request feature kind is invalid");
  std::optional<PhoneticConditioning> conditioning;
  std::uint32_t vocabularySize=0U;
  if (conditioned) {
    const auto hash=requiredString(metadata,"vocabularyHash",limits.maximumHashBytes);
    const auto size=requiredUnsigned(metadata,"vocabularySize",65536U);
    const auto* spans=metadata.find("phonemes");
    if (!hash || !size || !spans || !spans->isArray() || spans->asArray().size()>4096U)
      return core::failure<NeuralRequest>(core::ErrorCode::ParseError,"Neural phonetic metadata is invalid");
    vocabularySize=static_cast<std::uint32_t>(size.value());
    conditioning=PhoneticConditioning{hash.value(),{}};
    for (const auto& entry:spans->asArray()) {
      if (!hasOnlyKeys(entry,{"tokenId","startFrame","endFrame"})) return core::failure<NeuralRequest>(core::ErrorCode::ParseError,"Neural phoneme fields are invalid");
      const auto token=requiredUnsigned(entry,"tokenId",65535U),start=requiredUnsigned(entry,"startFrame",frames.value()),end=requiredUnsigned(entry,"endFrame",frames.value());
      if (!token || !start || !end) return core::failure<NeuralRequest>(core::ErrorCode::ParseError,"Neural phoneme span is invalid");
      conditioning->spans.push_back({static_cast<std::uint32_t>(token.value()),start.value(),end.value()});
    }
    const auto valid=conditioning->validate(frames.value(),vocabularySize,limits);
    if (!valid) return core::Result<NeuralRequest>{valid.error()};
  }
  const auto values = decodeFloats(decoded.value().payload, frames.value() * 2U); if (!values) return core::Result<NeuralRequest>{values.error()};
  NeuralRequest result{.requestId = id.value(), .modelId = std::move(model).value(), .modelVersion = std::move(version).value(), .modelContentHash = std::move(modelHash).value(), .pronunciationHash = std::move(pronunciation).value(), .sampleRate = rate.value(), .channels = channels.value(), .frameCount = frames.value(), .f0Hz = {}, .dynamics = {}};
  result.conditioning=std::move(conditioning); result.vocabularySize=vocabularySize;
  result.f0Hz.assign(values.value().begin(), values.value().begin() + static_cast<std::ptrdiff_t>(frames.value()));
  result.dynamics.assign(values.value().begin() + static_cast<std::ptrdiff_t>(frames.value()), values.value().end());
  const auto valid = result.validate(limits); if (!valid) return core::Result<NeuralRequest>{valid.error()};
  return result;
}

core::Result<std::vector<std::byte>> encodeResponse(const NeuralResponse& response,
                                                    WorkerProtocolLimits limits) {
  const auto valid = response.validate(limits); if (!valid) return core::Result<std::vector<std::byte>>{valid.error()};
  Object metadata{{"kind", response.requestContentHash.empty()?"seam-neural-response-v1":"seam-neural-response-v2"}, {"requestId", JsonValue{static_cast<std::int64_t>(response.requestId)}}, {"backendId", response.backendId}, {"modelContentHash", response.modelContentHash}, {"sampleRate", JsonValue{static_cast<std::int64_t>(response.sampleRate)}}, {"channels", JsonValue{static_cast<std::int64_t>(response.channels)}}, {"frameCount", JsonValue{static_cast<std::int64_t>(response.frameCount)}}};
  if (!response.requestContentHash.empty()) metadata.emplace("requestContentHash",response.requestContentHash);
  return encodeFrame(kResponseType, metadata, response.pcm, limits);
}

core::Result<NeuralResponse> decodeResponse(std::span<const std::byte> frame,
                                            WorkerProtocolLimits limits) {
  const auto decoded = decodeFrame(frame, kResponseType, limits); if (!decoded) return core::Result<NeuralResponse>{decoded.error()};
  const auto& metadata = decoded.value().metadata;
  const auto kind = requiredString(metadata, "kind", 64U);
  if (!kind || (kind.value()!="seam-neural-response-v1" && kind.value()!="seam-neural-response-v2")) return core::failure<NeuralResponse>(core::ErrorCode::ParseError,"Neural response kind is invalid");
  const bool bound=kind.value()=="seam-neural-response-v2";
  if (!(bound?hasOnlyKeys(metadata,{"kind","requestId","backendId","modelContentHash","sampleRate","channels","frameCount","requestContentHash"}):hasOnlyKeys(metadata, {"kind", "requestId", "backendId", "modelContentHash", "sampleRate", "channels", "frameCount"})))
    return core::failure<NeuralResponse>(core::ErrorCode::ParseError, "Neural response metadata fields are unsupported");
  std::string requestHash;
  if (bound) {
    const auto value=requiredString(metadata,"requestContentHash",limits.maximumHashBytes);
    if (!value || !validHash(value.value(),limits.maximumHashBytes)) return core::failure<NeuralResponse>(core::ErrorCode::ParseError,"Neural response request binding is invalid");
    requestHash=value.value();
  }
  auto id = requiredUnsigned(metadata, "requestId", std::numeric_limits<std::uint64_t>::max()); if (!id) return core::Result<NeuralResponse>{id.error()};
  auto backend = requiredString(metadata, "backendId", limits.maximumModelIdBytes); if (!backend) return core::Result<NeuralResponse>{backend.error()};
  auto hash = requiredString(metadata, "modelContentHash", limits.maximumHashBytes); if (!hash) return core::Result<NeuralResponse>{hash.error()};
  auto rate = requiredRate(metadata, "sampleRate", limits.maximumSampleRate); if (!rate) return core::Result<NeuralResponse>{rate.error()};
  auto channels = requiredChannels(metadata, limits); if (!channels) return core::Result<NeuralResponse>{channels.error()};
  auto frames = requiredUnsigned(metadata, "frameCount", limits.maximumFrames); if (!frames || frames.value() > std::numeric_limits<std::uint64_t>::max() / channels.value()) return core::failure<NeuralResponse>(core::ErrorCode::ParseError, "Neural response frame count is invalid");
  const auto values = decodeFloats(decoded.value().payload, frames.value() * channels.value()); if (!values) return core::Result<NeuralResponse>{values.error()};
  NeuralResponse result{.requestId = id.value(), .backendId = std::move(backend).value(), .modelContentHash = std::move(hash).value(), .sampleRate = rate.value(), .channels = channels.value(), .frameCount = frames.value(), .pcm = std::move(values).value()};
  result.requestContentHash=std::move(requestHash);
  const auto valid = result.validate(limits); if (!valid) return core::Result<NeuralResponse>{valid.error()};
  return result;
}

}  // namespace seam::neural_synthesis
