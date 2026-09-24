#include "seam/neural_synthesis/deployment_descriptor.hpp"

#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>

namespace seam::neural_synthesis {
namespace {

bool clean(const std::string& value) {
  return !value.empty() && domain::fromUtf8(value) &&
      std::none_of(value.begin(),value.end(),[](unsigned char c){return c<32U || c==127U;});
}

bool portablePath(const std::string& value) {
  if (!clean(value) || value.size()>4096U || value.front()=='/' || value.back()=='/' ||
      value.find('\\')!=std::string::npos || value.find(':')!=std::string::npos ||
      value.find("//")!=std::string::npos) return false;
  const std::filesystem::path path{std::u8string{value.begin(),value.end()}};
  return !path.has_root_path() && std::none_of(path.begin(),path.end(),
      [](const auto& part){return part=="." || part==".." || part.empty();});
}

}  // namespace

core::Result<VerifiedNeuralDeployment> VerifiedNeuralDeployment::verify(
    std::string_view json,const distribution::Ed25519Signature& signature,
    const distribution::Ed25519PublicKey& trustedReleaseKey,
    const NeuralDeploymentTarget& expected) {
  using Output=VerifiedNeuralDeployment;
  const auto fail=[] {return core::failure<Output>(core::ErrorCode::InvalidArgument,
      "Neural deployment descriptor schema or target is invalid");};
  if (json.empty() || json.size()>16U*1024U) return fail();
  const auto signedBytes=std::as_bytes(std::span{json.data(),json.size()});
  const auto verified=distribution::verifyEd25519(signedBytes,signature,trustedReleaseKey);
  if (!verified) return core::Result<Output>{verified.error()};
  const auto parsed=formats::parseJson(json,{.maximumInputBytes=16U*1024U,.maximumDepth=2U,
      .maximumNodes=16U,.maximumStringBytes=4096U,.maximumCollectionEntries=9U});
  if (!parsed) return core::Result<Output>{parsed.error()};
  const auto& root=parsed.value();
  if (!root.isObject()) return fail();
  const auto get=[&](std::string_view key)->const std::string* {
    const auto* field=root.find(key);
    return field && field->isString()?&field->asString():nullptr;
  };
  const auto* format=get("formatId"); const auto* version=root.find("schemaVersion");
  const auto* build=get("buildId"); const auto* platform=get("platform");
  const auto* surface=get("surface"); const auto* module=get("modulePath");
  const auto* manifest=get("manifestPath"); const auto* hash=get("manifestSha256");
  if (!version || !version->isInteger() ||
      (version->asInt64()!=1 && version->asInt64()!=2 && version->asInt64()!=3)) return fail();
  const auto launchVersion=static_cast<std::uint32_t>(version->asInt64());
  const auto* protocol=root.find("protocolVersion");
  if (launchVersion!=expected.protocolVersion ||
      (launchVersion==1U && root.asObject().size()!=8U) ||
      (launchVersion>=2U && (root.asObject().size()!=9U || !protocol || !protocol->isInteger() ||
          protocol->asInt64()!=static_cast<std::int64_t>(launchVersion)))) return fail();
  if (!format || *format!="com.project-seam.neural-deployment" ||
      !build || !clean(*build) || build->size()>256U || *build!=expected.buildId ||
      !platform || (*platform!="macos-arm64" && *platform!="windows-x64") || *platform!=expected.platform ||
      !surface || (*surface!="standalone" && *surface!="clap" && *surface!="vst3" && *surface!="auv2") || *surface!=expected.surface ||
      (*surface=="auv2" && *platform!="macos-arm64") || !module || !portablePath(*module) ||
      !manifest || !portablePath(*manifest) || *manifest==*module || !hash || hash->size()!=64U ||
      !std::all_of(hash->begin(),hash->end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');})) return fail();
  Output result;
  result.modulePath_=std::filesystem::path{std::u8string{module->begin(),module->end()}};
  result.manifestPath_=std::filesystem::path{std::u8string{manifest->begin(),manifest->end()}};
  result.manifestHash_=*hash;
  result.buildId_=*build;
  result.contentHash_=core::sha256Hex(json);
  result.protocolVersion_=launchVersion;
  return result;
}

core::Result<NeuralWorkerRunOptions> VerifiedNeuralDeployment::load(
    const void* moduleAnchor,std::stop_token stop) const {
  auto loaded=loadNeuralHelperForModule(moduleAnchor,modulePath_,manifestPath_,manifestHash_,buildId_,stop);
  if (!loaded) return loaded;
  if (loaded.value().protocolVersion!=protocolVersion_)
    return core::failure<NeuralWorkerRunOptions>(core::ErrorCode::Conflict,"Neural deployment and helper launch versions differ");
  return loaded;
}

}  // namespace seam::neural_synthesis
