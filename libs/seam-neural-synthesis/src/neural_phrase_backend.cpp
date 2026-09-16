#include "seam/neural_synthesis/neural_phrase_backend.hpp"
#include "seam/neural_synthesis/bundle_metadata.hpp"

#include "seam/platform/helper_process.hpp"
#include "seam/core/sha256.hpp"
#include "seam/core/file_io.hpp"
#include "seam/formats/json_value.hpp"
#include "seam/platform/application_paths.hpp"

#include <algorithm>
#include <span>
#include <map>
#include <cmath>
#include <set>

namespace seam::neural_synthesis {

core::Result<NeuralWorkerRunOptions> loadNeuralHelperForModule(
    const void* moduleAnchor,const std::filesystem::path& moduleRelativePath,
    const std::filesystem::path& manifestRelativePath,std::string_view expectedManifestHash,
    std::string_view expectedBuildId,std::stop_token stop) {
  using Output=NeuralWorkerRunOptions;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Neural package loading cancelled");
  const auto validPath=[](const std::filesystem::path& path) {
    const auto text=path.generic_string();
    if (text.empty() || text.size()>4096U || path.has_root_path() || text.find(':')!=std::string::npos ||
        text.find('\\')!=std::string::npos || std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32U || c==127U;})) return false;
    return std::none_of(path.begin(),path.end(),[](const auto& part){return part.empty() || part=="." || part=="..";});
  };
  if (!validPath(moduleRelativePath) || !validPath(manifestRelativePath))
    return core::failure<Output>(core::ErrorCode::InvalidArgument,"Neural deployment descriptor paths are invalid");
  const auto module=platform::loadedModulePath(moduleAnchor);
  if (!module) return core::Result<Output>{module.error()};
  auto root=module.value();
  for (auto it=moduleRelativePath.begin();it!=moduleRelativePath.end();++it) root=root.parent_path();
  if (root/moduleRelativePath!=module.value())
    return core::failure<Output>(core::ErrorCode::Conflict,"Neural deployment descriptor does not match the loaded module");
  const auto candidate=root/manifestRelativePath;
  std::error_code error;
  const auto manifest=std::filesystem::canonical(candidate,error);
  if (error || manifest!=candidate)
    return core::failure<Output>(core::ErrorCode::Conflict,"Neural package manifest is missing or redirected");
  const auto bytes=core::readTextFileLimited(manifest,256U*1024U);
  if (!bytes) return core::Result<Output>{bytes.error()};
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Neural package loading cancelled");
  const auto package=NeuralHelperPackage::decode(bytes.value(),expectedManifestHash);
  if (!package) return core::Result<Output>{package.error()};
  if (package.value().module.relativePath!=moduleRelativePath)
    return core::failure<Output>(core::ErrorCode::Conflict,"Neural manifest module differs from the deployment descriptor");
  return resolveNeuralHelperPackage(root,module.value(),package.value(),expectedBuildId,stop);
}

core::Result<NeuralWorkerRunOptions> resolveNeuralHelperForModule(
    const void* moduleAnchor,const NeuralHelperPackage& package,
    std::string_view expectedBuildId,std::stop_token stop) {
  if (stop.stop_requested()) return core::failure<NeuralWorkerRunOptions>(core::ErrorCode::Conflict,"Neural package resolution cancelled");
  const auto module=platform::loadedModulePath(moduleAnchor);
  if (!module) return core::Result<NeuralWorkerRunOptions>{module.error()};
  auto root=module.value();
  if (package.module.relativePath.empty() || package.module.relativePath.has_root_path())
    return core::failure<NeuralWorkerRunOptions>(core::ErrorCode::InvalidArgument,"Neural package module path is invalid");
  for (const auto& part:package.module.relativePath) {
    if (part.empty() || part=="." || part=="..")
      return core::failure<NeuralWorkerRunOptions>(core::ErrorCode::InvalidArgument,"Neural package module path is invalid");
    root=root.parent_path();
  }
  return resolveNeuralHelperPackage(root,module.value(),package,expectedBuildId,stop);
}

core::Result<NeuralHelperPackage> NeuralHelperPackage::decode(
    std::string_view json,std::string_view expectedContentHash) {
  const auto fail=[] {return core::failure<NeuralHelperPackage>(core::ErrorCode::ParseError,"Neural helper manifest is invalid");};
  if (json.size()>256U*1024U) return fail();
  if (core::sha256Hex(json)!=expectedContentHash)
    return core::failure<NeuralHelperPackage>(core::ErrorCode::Conflict,"Neural helper manifest differs from its trusted deployment digest");
  const auto parsed=formats::parseJson(json,{.maximumInputBytes=256U*1024U,.maximumDepth=4U,
      .maximumNodes=512U,.maximumStringBytes=4096U,.maximumCollectionEntries=64U});
  if (!parsed) return core::Result<NeuralHelperPackage>{parsed.error()};
  const auto& root=parsed.value();
  const auto* format=root.find("formatId"); const auto* schema=root.find("schemaVersion");
  const auto* build=root.find("buildId"); const auto* protocol=root.find("protocolVersion");
  const auto* module=root.find("module"); const auto* helper=root.find("helper"); const auto* deps=root.find("dependencies");
  const auto clean=[](const std::string& text) {
    return !text.empty() && domain::fromUtf8(text) &&
        std::none_of(text.begin(),text.end(),[](unsigned char c){return c<0x20U || c==0x7fU;});
  };
  if (!root.isObject() || root.asObject().size()!=7U || !format || !format->isString() ||
      format->asString()!="com.project-seam.neural-helper-package" || !schema || !schema->isInteger() ||
      (schema->asInt64()!=1 && schema->asInt64()!=2) ||
      !build || !build->isString() || build->asString().size()>256U || !clean(build->asString()) ||
      !protocol || !protocol->isInteger() || protocol->asInt64()!=schema->asInt64() || !module || !helper || !deps || !deps->isArray()) return fail();
  std::set<std::string> paths;
  const auto file=[&](const formats::JsonValue& value)->core::Result<NeuralPackageFile> {
    const auto* path=value.find("path"); const auto* hash=value.find("sha256"); const auto* size=value.find("maximumBytes");
    const auto invalid=[] {return core::failure<NeuralPackageFile>(core::ErrorCode::ParseError,"Neural helper manifest file entry is invalid");};
    if (!value.isObject() || value.asObject().size()!=3U || !path || !path->isString() || !clean(path->asString()) ||
        !hash || !hash->isString() || hash->asString().size()!=64U ||
        !std::all_of(hash->asString().begin(),hash->asString().end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}) ||
        !size || !size->isInteger() || size->asInt64()<=0 || size->asInt64()>256*1024*1024) return invalid();
    const auto& name=path->asString();
    // Portable manifest paths use slash separators and exclude Windows drive/ADS syntax.
    if (name.front()=='/' || name.back()=='/' || name.find('\\')!=std::string::npos || name.find(':')!=std::string::npos ||
        name.find("//")!=std::string::npos || !paths.insert(name).second) return invalid();
    const auto relative=std::filesystem::path{std::u8string{name.begin(),name.end()}};
    for (const auto& part:relative) if (part=="." || part=="..") return invalid();
    return NeuralPackageFile{relative,hash->asString(),static_cast<std::uint64_t>(size->asInt64())};
  };
  const auto moduleFile=file(*module); if (!moduleFile) return core::Result<NeuralHelperPackage>{moduleFile.error()};
  const auto helperFile=file(*helper); if (!helperFile) return core::Result<NeuralHelperPackage>{helperFile.error()};
  NeuralHelperPackage result{.buildId=build->asString(),.module=moduleFile.value(),.helper=helperFile.value(),.dependencies={}};
  result.protocolVersion=static_cast<std::uint32_t>(protocol->asInt64());
  for (const auto& dependency:deps->asArray()) {
    const auto decoded=file(dependency); if (!decoded) return core::Result<NeuralHelperPackage>{decoded.error()};
    result.dependencies.push_back(decoded.value());
  }
  return result;
}

core::Result<NeuralWorkerRunOptions> resolveNeuralHelperPackage(
    const std::filesystem::path& packageRoot,const std::filesystem::path& loadedModule,
    const NeuralHelperPackage& package,std::string_view expectedBuildId,std::stop_token stop) {
  using Output=NeuralWorkerRunOptions;
  if (!packageRoot.is_absolute() || !loadedModule.is_absolute() || expectedBuildId.empty() ||
      expectedBuildId.size()>256U || package.buildId!=expectedBuildId ||
      (package.protocolVersion!=1U && package.protocolVersion!=2U) ||
      package.dependencies.size()>64U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,"Neural package anchor, build or protocol is incompatible");
  std::error_code error;
  const auto root=std::filesystem::canonical(packageRoot,error);
  if (error || !std::filesystem::is_directory(root,error) || error)
    return core::failure<Output>(core::ErrorCode::IoError,"Neural package root is unavailable");
  std::set<std::filesystem::path> seen;
  const auto verify=[&](const NeuralPackageFile& file)->core::Result<std::filesystem::path> {
    if (stop.stop_requested())
      return core::failure<std::filesystem::path>(core::ErrorCode::Conflict,"Neural package resolution cancelled");
    if (file.relativePath.empty() || file.relativePath.has_root_path() ||
        file.relativePath.generic_string().size()>4096U || file.maximumBytes==0U ||
        file.maximumBytes>256U*1024U*1024U || file.contentHash.size()!=64U ||
        !std::all_of(file.contentHash.begin(),file.contentHash.end(),[](char c) {
          return (c>='0' && c<='9') || (c>='a' && c<='f');
        }))
      return core::failure<std::filesystem::path>(core::ErrorCode::InvalidArgument,"Neural package file declaration is invalid");
    for (const auto& part:file.relativePath)
      if (part==".." || part=="." || part.empty())
        return core::failure<std::filesystem::path>(core::ErrorCode::InvalidArgument,"Neural package paths must be contained canonical relative paths");
    const auto candidate=root/file.relativePath;
    const auto resolved=std::filesystem::canonical(candidate,error);
    if (error || resolved!=candidate || !seen.insert(resolved).second)
      return core::failure<std::filesystem::path>(core::ErrorCode::Conflict,"Neural package file is missing, redirected or duplicated");
    const auto digest=core::sha256File(resolved,file.maximumBytes);
    if (!digest) return core::Result<std::filesystem::path>{digest.error()};
    if (digest.value()!=file.contentHash)
      return core::failure<std::filesystem::path>(core::ErrorCode::Conflict,"Neural package file differs from its deployment digest");
    return resolved;
  };
  const auto module=verify(package.module); if (!module) return core::Result<Output>{module.error()};
  const auto actualModule=std::filesystem::canonical(loadedModule,error);
  if (error || actualModule!=module.value())
    return core::failure<Output>(core::ErrorCode::Conflict,"Neural package does not belong to the loaded SEAM module");
  const auto helper=verify(package.helper); if (!helper) return core::Result<Output>{helper.error()};
  for (const auto& dependency:package.dependencies) {
    const auto checked=verify(dependency); if (!checked) return core::Result<Output>{checked.error()};
  }
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Neural package resolution cancelled");
  return Output{.helper=helper.value(),.helperContentHash=package.helper.contentHash,
      .maximumHelperBytes=package.helper.maximumBytes,.protocolVersion=package.protocolVersion};
}

core::Result<NeuralRequest> prepareNeuralScoreRequest(
    std::uint64_t requestId,const ModelContract& model,const NeuralVocabulary& vocabulary,
    const synthesis::CompiledScorePerformance& performance,std::span<const domain::PhonemeToken> phones,
    std::string pronunciationHash,time::SampleFrame origin,time::SampleFrame end,
    std::string_view silencePhone,const WorkerProtocolLimits& limits,std::stop_token stop) {
  const auto cancelled=[] {return core::failure<NeuralRequest>(core::ErrorCode::Conflict,"Neural score request preparation cancelled");};
  if (stop.stop_requested()) return cancelled();
  const auto valid=model.validate(limits); if (!valid) return core::Result<NeuralRequest>{valid.error()};
  if (origin<0 || end<=origin || end>(time::SampleFrame{1}<<52) || performance.sampleRate()!=model.sampleRate ||
      static_cast<std::uint64_t>(end-origin)>model.maximumFrames || limits.maximumFrameBytes<=20U ||
      static_cast<std::uint64_t>(end-origin)>(limits.maximumFrameBytes-20U)/8U || vocabulary.contentHash()!=model.vocabularyHash)
    return core::failure<NeuralRequest>(core::ErrorCode::InvalidArgument,"Neural score request clock, vocabulary or frame budget differs");
  const auto conditioning=vocabulary.conditionScore(phones,performance.phonemeTiming(),origin,end,silencePhone,limits);
  if (!conditioning) return core::Result<NeuralRequest>{conditioning.error()};
  std::map<domain::PhonemeKey,bool> voiced;
  for (const auto& phone:phones) voiced.emplace(phone.key,phone.voiced);
  struct PhoneOwner { bool voiced; const synthesis::ScoreNoteSpan* note; };
  std::map<domain::NoteId,const synthesis::ScoreNoteSpan*> notes;
  for (const auto& note:performance.notes()) notes.emplace(note.id,&note);
  std::map<std::uint64_t,PhoneOwner> activeStarts;
  for (const auto& anchor:performance.phonemeTiming()) {
    const auto start=anchor.nucleusKey==std::optional{anchor.key}?anchor.nucleusFrame:
        anchor.explicitStartFrame.value_or(anchor.inferredStartFrame.value_or(-1));
    if (start<origin) return core::failure<NeuralRequest>(core::ErrorCode::Conflict,"Neural phone start lost its resolved timing");
    const auto owner=notes.find(anchor.key.noteId);
    if (owner==notes.end() || owner->second->endFrame<=owner->second->startFrame)
      return core::failure<NeuralRequest>(core::ErrorCode::Conflict,"Neural phoneme has no valid owning note");
    activeStarts.emplace(static_cast<std::uint64_t>(start-origin),PhoneOwner{voiced.at(anchor.key),owner->second});
  }
  NeuralRequest request{.requestId=requestId,.modelId=model.modelId,.modelVersion=model.modelVersion,
      .modelContentHash=model.modelContentHash,.pronunciationHash=std::move(pronunciationHash),
      .sampleRate=model.sampleRate,.channels=model.outputChannels,.frameCount=static_cast<std::uint64_t>(end-origin),
      .f0Hz={},.dynamics={},.conditioning=conditioning.value(),.vocabularySize=vocabulary.size()};
  request.f0Hz.resize(static_cast<std::size_t>(request.frameCount)); request.dynamics.resize(request.f0Hz.size());
  std::vector<float> breathiness(static_cast<std::size_t>(request.frameCount), 0.0F);
  bool anyBreathiness = false;
  for (const auto& span:request.conditioning->spans) {
    const auto active=activeStarts.find(span.startFrame);
    if (active==activeStarts.end()) continue;
    const auto& owner=*active->second.note;
    const auto extension=static_cast<time::SampleFrame>(performance.sampleRate())*2;
    if (origin+static_cast<time::SampleFrame>(span.startFrame)<owner.startFrame-extension ||
        origin+static_cast<time::SampleFrame>(span.endFrame)>owner.endFrame+extension)
      return core::failure<NeuralRequest>(core::ErrorCode::Unsupported,"Neural phonetic context exceeds two seconds beyond its owning note");
    for (auto frame=span.startFrame;frame<span.endFrame;++frame) {
      if ((frame&4095U)==0U && stop.stop_requested()) return cancelled();
      const auto absolute=origin+static_cast<time::SampleFrame>(frame);
      const auto sampled=std::clamp(absolute,owner.startFrame,owner.endFrame-1);
      const auto value=performance.at(sampled);
      if (value.noteId!=std::optional{owner.id})
        return core::failure<NeuralRequest>(core::ErrorCode::Conflict,"Neural phone context resolves to a different score voice");
      // scoreFrequencyHz already includes compiled pitch and vibrato.
      if (active->second.voiced && value.scoreFrequencyHz) request.f0Hz[frame]=static_cast<float>(*value.scoreFrequencyHz);
      // Explicit phonetic extensions must not inherit a closed note envelope.
      request.dynamics[frame]=value.dynamicsGain*(sampled==absolute?value.articulationGain:1.0F);
      breathiness[frame]=std::clamp(value.breathiness, 0.0F, 1.0F);
      if (value.breathiness != 0.0F) anyBreathiness = true;
    }
  }
  if (anyBreathiness) request.breathiness = std::move(breathiness);
  if (stop.stop_requested()) return cancelled();
  const auto checked=model.validateRequest(request,limits); if (!checked) return core::Result<NeuralRequest>{checked.error()};
  return request;
}

namespace {
core::Result<NeuralWorkerResult> runWorkerTransport(
    const NeuralRequest& request, const ModelContract& model,
    NeuralWorkerRunOptions options,std::vector<std::string> arguments, std::stop_token stop) {
  using Output = NeuralWorkerResult;
  if (stop.stop_requested())
    return core::failure<Output>(core::ErrorCode::Conflict,"Neural worker execution cancelled");
  if (options.helper.empty() || !options.helper.is_absolute())
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
                                 "Neural helper must be an absolute path");
  if (options.helperContentHash.size()!=64U ||
      !std::all_of(options.helperContentHash.begin(),options.helperContentHash.end(),[](char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f');
      }) || options.maximumHelperBytes==0U || options.maximumHelperBytes>256U*1024U*1024U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,"Neural helper requires a canonical expected digest and bounded executable size");
  const auto requestValid = model.validateRequest(request, options.limits);
  if (!requestValid) return core::Result<Output>{requestValid.error()};
  if (request.conditioning && (!options.vocabulary || options.vocabulary->contentHash()!=model.vocabularyHash ||
      options.vocabulary->size()!=request.vocabularySize))
    return core::failure<Output>(core::ErrorCode::Conflict,"Conditioned inference requires the verified model vocabulary and exact token count");
  const auto encoded = encodeRequest(request, options.limits);
  if (!encoded) return core::Result<Output>{encoded.error()};
  const auto helperDigest=core::sha256File(options.helper,options.maximumHelperBytes);
  if (!helperDigest) return core::Result<Output>{helperDigest.error()};
  if (helperDigest.value()!=options.helperContentHash)
    return core::failure<Output>(core::ErrorCode::Conflict,"Neural helper executable differs from its expected deployment identity");
  if (stop.stop_requested())
    return core::failure<Output>(core::ErrorCode::Conflict,"Neural worker execution cancelled");
  const auto input = std::string{reinterpret_cast<const char*>(encoded.value().data()), encoded.value().size()};
  platform::HelperProcessRequest helper{
      .executable = options.helper,
      .arguments = std::move(arguments),
      .timeout = options.timeout,
      .maximumStdoutBytes = options.limits.maximumFrameBytes,
      .maximumStderrBytes = options.limits.maximumMetadataBytes,
      .standardInput = input,
      .maximumStdinBytes = options.limits.maximumFrameBytes,
      .maximumResidentBytes = options.maximumResidentBytes,
      .maximumCpuTime = options.maximumCpuTime,
  };
  const auto run = platform::runBoundedHelperProcess(helper, stop);
  if (!run) return core::Result<Output>{run.error()};
  const auto responseBytes = std::span<const std::byte>{reinterpret_cast<const std::byte*>(run.value().standardOutput.data()), run.value().standardOutput.size()};
  const auto response = decodeResponse(responseBytes, options.limits);
  if (!response) return core::Result<Output>{response.error()};
  if ((request.conditioning && response.value().requestContentHash.empty()) ||
      (!response.value().requestContentHash.empty() && response.value().requestContentHash!=core::sha256Hex(std::span<const std::byte>{encoded.value()})))
    return core::failure<Output>(core::ErrorCode::Conflict,"Neural response is not bound to the complete request");
  if (response.value().requestId != request.requestId ||
      response.value().bundleContentHash != request.bundleContentHash ||
      response.value().modelContentHash != request.modelContentHash ||
      response.value().sampleRate != request.sampleRate ||
      response.value().channels != request.channels ||
      response.value().frameCount != request.frameCount)
    return core::failure<Output>(core::ErrorCode::Conflict,
                                 "Neural worker response does not match its request");
  return Output{std::move(response).value(), std::move(run.value().standardError)};
}
} // namespace

core::Result<NeuralWorkerResult> runNeuralWorker(
    const NeuralRequest& request,const ModelContract& model,
    NeuralWorkerRunOptions options,std::stop_token stop) {
  if (options.protocolVersion!=1U || !request.bundleContentHash.empty())
    return core::failure<NeuralWorkerResult>(core::ErrorCode::Unsupported,"Legacy worker launcher requires protocol 1 and a legacy request");
  return runWorkerTransport(request,model,std::move(options),{"--seam-neural-worker-v1"},stop);
}

core::Result<NeuralWorkerResult> runNeuralBundleWorker(
    const NeuralRequest& request,const std::filesystem::path& bundleDirectory,
    std::size_t maximumBundleBytes,NeuralWorkerRunOptions options,std::stop_token stop) {
  using Output=NeuralWorkerResult;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,"Neural bundle launch cancelled");
  if (options.protocolVersion!=2U || request.bundleContentHash.empty())
    return core::failure<Output>(core::ErrorCode::Unsupported,"Bundle launch requires protocol 2 and request metadata v3");
  const auto valid=request.validate(options.limits);
  if (!valid) return core::Result<Output>{valid.error()};
  if (!bundleDirectory.is_absolute() || maximumBundleBytes==0U || maximumBundleBytes>512U*1024U*1024U ||
      options.maximumResidentBytes==0U || options.maximumResidentBytes>4ULL*1024ULL*1024ULL*1024ULL ||
      options.maximumCpuTime.count()<=0 || options.maximumCpuTime.count()>60000 ||
      options.timeout.count()<=0 || options.timeout.count()>60000 ||
      options.limits.maximumFrameBytes==0U || options.limits.maximumFrameBytes>64U*1024U*1024U ||
      options.limits.maximumMetadataBytes==0U || options.limits.maximumMetadataBytes>1024U*1024U)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,"Bundle launch requires an absolute directory and explicit resource budgets");
  std::error_code error;
  const auto canonical=std::filesystem::canonical(bundleDirectory,error);
  if (error || canonical!=bundleDirectory)
    return core::failure<Output>(core::ErrorCode::Conflict,"Bundle launch directory must be canonical and available");
  // The child reloads its own bytes. Do not retain a second full graph payload
  // in the parent for the entire inference interval; metadata owns its values.
  const auto metadata=[&]() -> core::Result<NeuralBundleMetadata> {
    const auto bundle=loadNeuralBundleDirectory(canonical,
        {domain::SingerResourceKind::Neural,request.modelId,request.modelVersion,request.bundleContentHash},maximumBundleBytes,stop);
    if (!bundle) return core::Result<NeuralBundleMetadata>{bundle.error()};
    return inspectNeuralBundleMetadata(bundle.value(),stop);
  }();
  if (!metadata) return core::Result<Output>{metadata.error()};
  options.vocabulary=metadata.value().vocabulary;
  const auto name=canonical.u8string();
  return runWorkerTransport(request,metadata.value().model,std::move(options),
      {"--seam-neural-worker-v2",std::string{name.begin(),name.end()},request.modelId,
       request.modelVersion,request.bundleContentHash,std::to_string(maximumBundleBytes)},stop);
}

}  // namespace seam::neural_synthesis
