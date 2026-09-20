#include "seam/neural_synthesis/model_contract.hpp"

#include "seam/domain/note.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/core/sha256.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace seam::neural_synthesis {

core::Result<std::string> convertDiffSingerVocabulary(std::string_view json) {
  const auto invalid=[] {return core::failure<std::string>(core::ErrorCode::InvalidArgument,
      "DiffSinger vocabulary contains invalid names, IDs or gaps; IDs cannot be renumbered");};
  const auto limits=formats::JsonParseLimits{.maximumInputBytes=4U*1024U*1024U,.maximumDepth=3U,
      .maximumNodes=65544U,.maximumStringBytes=128U,.maximumCollectionEntries=65536U};
  const auto parsed=formats::parseJson(json,limits);
  if (!parsed) return core::Result<std::string>{parsed.error()};
  if (!parsed.value().isObject() || parsed.value().asObject().empty()) return invalid();
  std::map<std::uint32_t,std::vector<std::string>> groups;
  for (const auto& [phone,id]:parsed.value().asObject()) {
    if (phone.empty() || phone=="<PAD>" || !domain::fromUtf8(phone) ||
        std::any_of(phone.begin(),phone.end(),[](unsigned char c){return c<32U || c==127U;}) ||
        !id.isInteger() || id.asInt64()<1 || id.asInt64()>=65536) return invalid();
    groups[static_cast<std::uint32_t>(id.asInt64())].push_back(phone);
  }
  using J=formats::JsonValue;
  J::Array tokens{J{"<PAD>"}};
  J::Object aliases;
  std::uint32_t expected=1;
  for (auto& [id,names]:groups) {
    if (id!=expected++) return invalid();
    std::sort(names.begin(),names.end());
    tokens.emplace_back(names.front());
    for (std::size_t index=1;index<names.size();++index)
      aliases.emplace(names[index],J{static_cast<std::int64_t>(id)});
  }
  const auto encoded=formats::stringifyJson(J{J::Object{{"formatId","com.project-seam.neural-vocabulary"},
      {"schemaVersion",std::int64_t{2}},{"tokens",std::move(tokens)},{"aliases",std::move(aliases)}}});
  const auto checked=formats::parseJson(encoded,limits);
  if (!checked) return core::Result<std::string>{checked.error()};
  return encoded;
}

core::Result<NeuralVocabulary> NeuralVocabulary::decode(std::string_view json,const ModelContract& model) {
  const auto valid=model.validate(); if (!valid) return core::Result<NeuralVocabulary>{valid.error()};
  if (json.size()>4U*1024U*1024U) return core::failure<NeuralVocabulary>(core::ErrorCode::InvalidArgument,"Neural vocabulary exceeds its byte limit");
  const auto hash=core::sha256Hex(json);
  if (hash!=model.vocabularyHash) return core::failure<NeuralVocabulary>(core::ErrorCode::Conflict,"Neural vocabulary bytes differ from the model contract");
  const auto decoded=formats::parseJson(json,{.maximumInputBytes=4U*1024U*1024U,.maximumDepth=3U,
      .maximumNodes=65544U,.maximumStringBytes=128U,.maximumCollectionEntries=65536U});
  if (!decoded) return core::Result<NeuralVocabulary>{decoded.error()};
  const auto& root=decoded.value();
  const auto* format=root.find("formatId"); const auto* version=root.find("schemaVersion"); const auto* tokens=root.find("tokens");
  const bool aliased=version && version->isInteger() && version->asInt64()==2;
  const auto* aliases=root.find("aliases");
  if (!root.isObject() || root.asObject().size()!=(aliased?4U:3U) || !format || !format->isString() ||
      format->asString()!="com.project-seam.neural-vocabulary" || !version || !version->isInteger() || (!aliased && version->asInt64()!=1) ||
      (aliased && (!aliases || !aliases->isObject())) ||
      !tokens || !tokens->isArray() || tokens->asArray().empty())
    return core::failure<NeuralVocabulary>(core::ErrorCode::ParseError,"Neural vocabulary format is invalid");
  NeuralVocabulary result; result.contentHash_=hash;
  for (const auto& token:tokens->asArray()) {
    if (!token.isString() || token.asString().empty() || !domain::fromUtf8(token.asString()) ||
        std::any_of(token.asString().begin(),token.asString().end(),[](unsigned char c){return c<0x20U || c==0x7fU;}))
      return core::failure<NeuralVocabulary>(core::ErrorCode::InvalidArgument,"Neural vocabulary token is invalid");
    const auto index=static_cast<std::uint32_t>(result.tokens_.size());
    if (!result.tokens_.emplace(token.asString(),index).second)
      return core::failure<NeuralVocabulary>(core::ErrorCode::InvalidArgument,"Neural vocabulary contains duplicate tokens");
  }
  result.vocabularySize_=static_cast<std::uint32_t>(result.tokens_.size());
  if (aliased) {
    for (const auto& [alias,id]:aliases->asObject()) {
      if (alias.empty() || !domain::fromUtf8(alias) ||
          std::any_of(alias.begin(),alias.end(),[](unsigned char c){return c<0x20U || c==0x7fU;}) ||
          !id.isInteger() || id.asInt64()<1 || static_cast<std::uint64_t>(id.asInt64())>=result.vocabularySize_ ||
          !result.tokens_.emplace(alias,static_cast<std::uint32_t>(id.asInt64())).second)
        return core::failure<NeuralVocabulary>(core::ErrorCode::InvalidArgument,"Neural vocabulary alias is invalid or collides with a token");
    }
  }
  return result;
}

core::Result<std::uint32_t> NeuralVocabulary::tokenId(std::string_view phone) const {
  const auto found=tokens_.find(phone);
  if (found==tokens_.end()) return core::failure<std::uint32_t>(core::ErrorCode::Unsupported,"Phoneme is absent from the neural vocabulary");
  return found->second;
}

core::Result<PhoneticConditioning> NeuralVocabulary::conditionScore(
    std::span<const domain::PhonemeToken> phones,std::span<const synthesis::PhonemeTimingAnchor> timing,
    time::SampleFrame origin,time::SampleFrame end,std::string_view silencePhone,const WorkerProtocolLimits& limits) const {
  const auto fail=[](const char* message){return core::failure<PhoneticConditioning>(core::ErrorCode::InvalidArgument,message);};
  if (origin<0 || end<=origin || end>(time::SampleFrame{1}<<52) ||
      static_cast<std::uint64_t>(end-origin)>limits.maximumFrames || phones.empty() || phones.size()>4096U || phones.size()!=timing.size())
    return fail("Neural score conditioning dimensions exceed bounds");
  const auto silence=tokenId(silencePhone); if (!silence) return core::Result<PhoneticConditioning>{silence.error()};
  std::map<domain::PhonemeKey,const synthesis::PhonemeTimingAnchor*> anchors;
  for (const auto& anchor:timing) if (!anchors.emplace(anchor.key,&anchor).second) return fail("Neural timing keys are duplicated");
  std::vector<NeuralPhonemeSpan> resolved;
  std::set<domain::PhonemeKey> seen;
  for (const auto& phone:phones) {
    const auto valid=phone.validate(); if (!valid) return core::Result<PhoneticConditioning>{valid.error()};
    const auto found=anchors.find(phone.key); if (found==anchors.end() || !seen.insert(phone.key).second) return fail("Neural phone/timing coverage differs");
    const auto& anchor=*found->second;
    if (!anchor.voiced || *anchor.voiced!=phone.voiced) return fail("Neural phone/timing voicing differs");
    const auto token=tokenId(phone.symbol); if (!token) return core::Result<PhoneticConditioning>{token.error()};
    const bool nucleus=phone.role==domain::PhonemeRole::Nucleus;
    const auto start=nucleus?std::optional{anchor.nucleusFrame}:anchor.explicitStartFrame?anchor.explicitStartFrame:anchor.inferredStartFrame;
    if (!start) return core::failure<PhoneticConditioning>(core::ErrorCode::Unsupported,"Neural consonant timing is unresolved; provide resolved timing before inference");
    if (nucleus && anchor.nucleusKey!=std::optional{phone.key}) return fail("Neural nucleus does not own its timing anchor");
    if (!nucleus && anchor.nucleusKey) {
      const auto owner=anchors.find(*anchor.nucleusKey);
      if (owner==anchors.end() || owner->second->key.noteId!=phone.key.noteId ||
          owner->second->nucleusKey!=anchor.nucleusKey || owner->second->nucleusFrame!=anchor.nucleusFrame || !owner->second->voiced.value_or(false))
        return fail("Neural consonant nucleus binding is inconsistent");
    }
    if (phone.role==domain::PhonemeRole::Onset && !anchor.endExplicit && !anchor.nucleusKey)
      return fail("Neural onset has no resolved end or nucleus");
    const auto finish=phone.role==domain::PhonemeRole::Onset && !anchor.endExplicit?anchor.nucleusFrame:anchor.endFrame;
    if (anchor.nucleusKey && ((phone.role==domain::PhonemeRole::Onset && finish>anchor.nucleusFrame) ||
        (phone.role==domain::PhonemeRole::Coda && *start<anchor.nucleusFrame)))
      return fail("Neural consonant timing crosses its syllable nucleus");
    if (*start<origin || finish>end || finish<=*start) return fail("Neural phoneme span is empty or outside its output window");
    resolved.push_back({token.value(),static_cast<std::uint64_t>(*start-origin),static_cast<std::uint64_t>(finish-origin)});
  }
  std::sort(resolved.begin(),resolved.end(),[](const auto& a,const auto& b){return a.startFrame<b.startFrame;});
  PhoneticConditioning result{contentHash_,{}};
  std::uint64_t next=0U;
  for (const auto& span:resolved) {
    if (span.startFrame<next) return fail("Neural phoneme spans overlap");
    if (span.startFrame>next) result.spans.push_back({silence.value(),next,span.startFrame});
    result.spans.push_back(span); next=span.endFrame;
    if (result.spans.size()>4096U) return fail("Neural conditioning exceeds its span budget including silence");
  }
  const auto count=static_cast<std::uint64_t>(end-origin);
  if (next<count) result.spans.push_back({silence.value(),next,count});
  const auto valid=result.validate(count,size(),limits); if (!valid) return core::Result<PhoneticConditioning>{valid.error()};
  return result;
}

core::Result<void> ModelContract::validate(const WorkerProtocolLimits& limits) const {
  if (modelId.empty() || modelVersion.empty() || modelId.size() > limits.maximumModelIdBytes ||
      modelVersion.size() > limits.maximumModelIdBytes || !domain::fromUtf8(modelId) ||
      !domain::fromUtf8(modelVersion) || modelContentHash.size() != 64U ||
      vocabularyHash.size() != 64U || modelContentHash.size() > limits.maximumHashBytes ||
      vocabularyHash.size() > limits.maximumHashBytes || sampleRate < 8'000U ||
      sampleRate > limits.maximumSampleRate || hopSize == 0U || hopSize > 16'384U ||
      outputChannels == 0U || outputChannels > limits.maximumChannels || maximumFrames == 0U ||
      maximumFrames > limits.maximumFrames || maximumModelBytes == 0U ||
      maximumModelBytes > 4ULL * 1024ULL * 1024ULL * 1024ULL)
    return core::failure(core::ErrorCode::InvalidArgument, "Neural model contract is invalid");
  const auto validHash = [](const std::string& value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
      return (character >= '0' && character <= '9') ||
             (character >= 'a' && character <= 'f');
    });
  };
  if (!validHash(modelContentHash) || !validHash(vocabularyHash))
    return core::failure(core::ErrorCode::InvalidArgument, "Neural model contract hash is invalid");
  if (breathinessDefaults.size() > 4096U)
    return core::failure(core::ErrorCode::InvalidArgument, "Neural conditioning defaults exceed their bound");
  for (const auto& [symbol, value] : breathinessDefaults) {
    if (symbol.empty() || symbol.size() > 256U || !domain::fromUtf8(symbol) ||
        !std::isfinite(value) || value < 0.0F || value > 1.0F)
      return core::failure(core::ErrorCode::InvalidArgument, "Neural conditioning default symbol or value is invalid");
  }
  return core::success();
}

core::Result<void> ModelContract::validatePhoneticConditioning(
    const PhoneticConditioning& conditioning,std::uint64_t frameCount,
    std::uint32_t vocabularySize,const WorkerProtocolLimits& limits) const {
  const auto contract=validate(limits); if (!contract) return contract;
  const auto valid=conditioning.validate(frameCount,vocabularySize,limits); if (!valid) return valid;
  if (conditioning.vocabularyHash!=vocabularyHash || frameCount>maximumFrames)
    return core::failure(core::ErrorCode::Conflict,"Neural phonetic conditioning differs from the model contract");
  return core::success();
}

core::Result<void> ModelContract::validateRequest(
    const NeuralRequest& request, const WorkerProtocolLimits& limits) const {
  const auto valid = validate(limits); if (!valid) return valid;
  const auto requestValid = request.validate(limits); if (!requestValid) return requestValid;
  if (request.conditioning) {
    const auto phonetic=validatePhoneticConditioning(*request.conditioning,request.frameCount,request.vocabularySize,limits);
    if (!phonetic) return phonetic;
  }
  if (request.modelId != modelId || request.modelVersion != modelVersion ||
      request.modelContentHash != modelContentHash || request.sampleRate != sampleRate ||
      request.channels != outputChannels || request.frameCount > maximumFrames)
    return core::failure(core::ErrorCode::Conflict, "Neural request does not match the model contract");
  return core::success();
}

}  // namespace seam::neural_synthesis
