#include "seam/authoring/neural_resource_registry.hpp"

#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace seam::authoring {
namespace {
constexpr std::size_t kMaximumRecordBytes=4096U;
constexpr std::size_t kMaximumManifestBytes=32768U;
constexpr std::size_t kMaximumBundleBytes=512U*1024U*1024U;
constexpr const char* kRecordFormat="com.project-seam.neural-resource";
constexpr const char* kManifestFormat="com.project-seam.neural-data-bundle";

std::string text(const formats::JsonValue& value) {
  return value.isString() ? value.asString() : std::string{};
}

bool printable(std::string_view value,std::size_t limit) {
  return !value.empty() && value.size()<=limit &&
      std::all_of(value.begin(),value.end(),[](char c) {
        return static_cast<unsigned char>(c)>=32U && static_cast<unsigned char>(c)<127U;
      });
}

core::Result<InstalledNeuralResource> inspectBundle(const std::filesystem::path& directory,
    std::size_t maximumAssetBytes,std::size_t maximumTotalBytes) {
  using Output=InstalledNeuralResource;
  const auto fail=[&](const char* message) {
    return core::failure<Output>(core::ErrorCode::ParseError,message);
  };
  const auto regular=[&](const std::filesystem::path& path) {
    std::error_code error;
    const auto status=std::filesystem::symlink_status(path,error);
    return !error && std::filesystem::is_regular_file(status);
  };
  if (!regular(directory/"manifest.json") || !regular(directory/"resource.json"))
    return fail("Neural bundle record or manifest is missing or not a regular file");
  const auto record=core::readFileBytesLimited(directory/"resource.json",kMaximumRecordBytes);
  if (!record) return core::Result<Output>{record.error()};
  const auto parsedRecord=formats::parseJson(
      {reinterpret_cast<const char*>(record.value().data()),record.value().size()},
      {.maximumInputBytes=kMaximumRecordBytes,.maximumDepth=2U,.maximumNodes=16U,
       .maximumStringBytes=256U,.maximumCollectionEntries=8U});
  if (!parsedRecord) return core::Result<Output>{parsedRecord.error()};
  const auto& recordValue=parsedRecord.value();
  if (!recordValue.isObject() || recordValue.asObject().size()!=5U ||
      text(*recordValue.find("formatId"))!=kRecordFormat ||
      !recordValue.find("schemaVersion")->isInteger() || recordValue.find("schemaVersion")->asInt64()!=1)
    return fail("Neural bundle resource record has an unsupported schema");
  const auto id=text(*recordValue.find("id")),version=text(*recordValue.find("version"));
  const auto digest=text(*recordValue.find("contentHash"));
  if (!printable(id,256U) || !printable(version,256U) || digest.size()!=64U ||
      !std::all_of(digest.begin(),digest.end(),[](char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f');
      }))
    return fail("Neural bundle resource record identity is invalid");
  const auto manifest=core::readFileBytesLimited(directory/"manifest.json",kMaximumManifestBytes);
  if (!manifest) return core::Result<Output>{manifest.error()};
  if (core::sha256Hex(manifest.value())!=digest)
    return fail("Neural bundle manifest digest differs from its resource record");
  const auto parsedManifest=formats::parseJson(
      {reinterpret_cast<const char*>(manifest.value().data()),manifest.value().size()},
      {.maximumInputBytes=kMaximumManifestBytes,.maximumDepth=3U,.maximumNodes=256U,
       .maximumStringBytes=128U,.maximumCollectionEntries=32U});
  if (!parsedManifest) return core::Result<Output>{parsedManifest.error()};
  const auto& root=parsedManifest.value();
  const auto* assets=root.find("assets");
  if (!root.isObject() || text(*root.find("formatId"))!=kManifestFormat ||
      !root.find("schemaVersion")->isInteger() || root.find("schemaVersion")->asInt64()!=1 ||
      !assets || !assets->isArray() || assets->asArray().size()<4U || assets->asArray().size()>32U)
    return fail("Neural bundle manifest has an unsupported schema");
  std::set<std::string> names;
  std::map<std::string,unsigned> roles;
  std::size_t total=0U;
  for (const auto& entry:assets->asArray()) {
    if (!entry.isObject() || entry.asObject().size()!=4U) return fail("Neural bundle asset entry is invalid");
    const auto name=text(*entry.find("name")),hash=text(*entry.find("sha256")),role=text(*entry.find("role"));
    const auto* size=entry.find("bytes");
    if (!printable(role,64U)) return fail("Neural bundle asset role is invalid");
    ++roles[role];
    if (!printable(name,64U) || name=="manifest.json" || name=="resource.json" ||
        !names.insert(name).second || hash.size()!=64U || !size || !size->isInteger() || size->asInt64()<1)
      return fail("Neural bundle asset identity is invalid");
    const auto bytes=static_cast<std::uint64_t>(size->asInt64());
    if (bytes>maximumAssetBytes || bytes>maximumTotalBytes-total)
      return fail("Neural bundle asset exceeds the configured budget");
    total+=static_cast<std::size_t>(bytes);
    if (!regular(directory/name)) return fail("Neural bundle asset is missing or not a regular file");
    const auto payload=core::readFileBytesLimited(directory/name,static_cast<std::size_t>(bytes));
    if (!payload || payload.value().size()!=bytes ||
        core::sha256Hex(std::span<const std::byte>{payload.value()})!=hash)
      return fail("Neural bundle asset bytes differ from the manifest");
  }
  // A resolvable bundle still has to be an admissible one: the four required
  // roles must each appear exactly once. Admission re-checks this in the worker.
  for (const auto* role:{"acoustic","vocoder","vocabulary","configuration"})
    if (roles[role]!=1U) return fail("Neural bundle is missing or duplicating a required role");
  return core::success(Output{id,version,digest,directory});
}
}

core::Result<NeuralResourceRegistry> NeuralResourceRegistry::scan(const std::filesystem::path& root,
    std::size_t maximumResources,std::size_t maximumAssetBytes,std::size_t maximumTotalBytes,
    std::stop_token stop) {
  using Output=NeuralResourceRegistry;
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,
      "Neural resource scan cancelled");
  if (!root.is_absolute() || maximumResources==0U || maximumResources>4096U ||
      maximumAssetBytes==0U || maximumAssetBytes>256U*1024U*1024U ||
      maximumTotalBytes==0U || maximumTotalBytes>kMaximumBundleBytes)
    return core::failure<Output>(core::ErrorCode::InvalidArgument,
        "Neural resource scan requires an absolute root and explicit budgets");
  std::error_code error;
  const auto canonical=std::filesystem::canonical(root,error);
  if (error || !std::filesystem::is_directory(canonical,error) || error)
    return core::failure<Output>(core::ErrorCode::NotFound,"Neural resource root is unavailable");
  std::vector<std::filesystem::path> candidates;
  for (const auto& entry:std::filesystem::directory_iterator(canonical,error)) {
    if (error) return core::failure<Output>(core::ErrorCode::IoError,"Neural resource scan failed");
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural resource scan cancelled");
    if (entry.is_directory(error) && !entry.is_symlink(error))
      candidates.push_back(entry.path().lexically_normal());
  }
  std::sort(candidates.begin(),candidates.end());
  Output registry;
  std::set<std::string> identities;
  for (const auto& directory:candidates) {
    if (registry.resources_.size()>=maximumResources) break;
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural resource scan cancelled");
    const auto inspected=inspectBundle(directory,maximumAssetBytes,maximumTotalBytes);
    if (!inspected) return core::Result<Output>{inspected.error()};
    const auto key=inspected.value().id+"\n"+inspected.value().version+"\n"+inspected.value().contentHash;
    if (!identities.insert(key).second) return core::failure<Output>(core::ErrorCode::Conflict,
        "Neural resource root contains a duplicate identity");
    registry.resources_.push_back(inspected.value());
  }
  return core::success(std::move(registry));
}

core::Result<std::filesystem::path> NeuralResourceRegistry::resolve(
    const domain::NeuralResourceReference& selection) const {
  const auto valid=selection.validate();
  if (!valid) return core::Result<std::filesystem::path>{valid.error()};
  for (const auto& resource:resources_)
    if (resource.id==selection.resource.id && resource.version==selection.resource.version &&
        resource.contentHash==selection.resource.contentHash) return core::success(resource.directory);
  return core::failure<std::filesystem::path>(core::ErrorCode::NotFound,
      "No installed neural resource matches the saved selection");
}

}  // namespace seam::authoring
