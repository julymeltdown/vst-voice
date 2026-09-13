#include "seam/neural_synthesis/bundle_metadata.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace seam::neural_synthesis {
core::Result<synthesis::FrozenNeuralBundle> loadNeuralBundleDirectory(
    const std::filesystem::path& directory,domain::SingerResourceIdentity identity,
    std::size_t maximumTotalBytes,std::stop_token stop) {
  using Output=synthesis::FrozenNeuralBundle;
  const auto fail=[](const char* message) {return core::failure<Output>(core::ErrorCode::InvalidArgument,message);};
  const auto cancelled=[] {return core::failure<Output>(core::ErrorCode::Conflict,"Neural bundle loading cancelled");};
  if (stop.stop_requested()) return cancelled();
  if (!identity.validate() || identity.kind!=domain::SingerResourceKind::Neural ||
      maximumTotalBytes==0 || maximumTotalBytes>512U*1024U*1024U) return fail("Invalid neural bundle identity or payload limit");
  std::error_code error;
  const auto root=std::filesystem::canonical(directory,error);
  if (error || !std::filesystem::is_directory(root,error) || error) return fail("Neural bundle directory is unavailable");
  const auto regular=[&](const std::filesystem::path& path) {
    std::error_code ec;
    const auto status=std::filesystem::symlink_status(path,ec);
    return !ec && std::filesystem::is_regular_file(status);
  };
  if (!regular(root/"manifest.json")) return fail("Bundle manifest must be a regular non-symlink file");
  const auto manifest=core::readTextFileLimited(root/"manifest.json",32768U);
  if (!manifest) return core::Result<Output>{manifest.error()};
  if (core::sha256Hex(manifest.value())!=identity.contentHash) return fail("Bundle manifest digest mismatch");
  const auto parsed=formats::parseJson(manifest.value(),{.maximumInputBytes=32768U,.maximumDepth=3U,
      .maximumNodes=512U,.maximumStringBytes=128U,.maximumCollectionEntries=32U});
  if (!parsed) return core::Result<Output>{parsed.error()};
  const auto& value=parsed.value();
  const auto* format=value.find("formatId"),*version=value.find("schemaVersion"),*assets=value.find("assets");
  if (!value.isObject() || value.asObject().size()!=3 || !format || !format->isString() ||
      format->asString()!="com.project-seam.neural-data-bundle" || !version || !version->isInteger() || version->asInt64()!=1 ||
      !assets || !assets->isArray() || assets->asArray().size()<4) return fail("Invalid bundle manifest schema");
  struct Entry {synthesis::NeuralAssetRole role; std::string name,hash; std::size_t size;};
  std::vector<Entry> entries;
  constexpr std::array roles{"acoustic","vocoder","vocabulary","configuration","variance","tensor"};
  std::array<unsigned,6> counts{};
  std::set<std::string> names;
  std::size_t total=0;
  for (const auto& row:assets->asArray()) {
    const auto* role=row.find("role"),*name=row.find("name"),*hash=row.find("sha256"),*size=row.find("bytes");
    if (!row.isObject() || row.asObject().size()!=4 || !role || !role->isString() || !name || !name->isString() ||
        !hash || !hash->isString() || !size || !size->isInteger() || size->asInt64()<1) return fail("Invalid bundle asset entry");
    const auto found=std::find(roles.begin(),roles.end(),role->asString());
    if (found==roles.end()) return fail("Unknown bundle asset role");
    const auto index=static_cast<std::size_t>(found-roles.begin());
    const auto& n=name->asString(); const auto& h=hash->asString();
    if (n.empty() || n.size()>64 || n.front()=='.' || n=="manifest.json" ||
        !std::all_of(n.begin(),n.end(),[](char c){return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c=='.';}) ||
        !names.insert(n).second || h.size()!=64 || !std::all_of(h.begin(),h.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');})) return fail("Invalid bundle asset name or hash");
    const auto bytes=static_cast<std::uint64_t>(size->asInt64());
    if (bytes>256U*1024U*1024U || bytes>maximumTotalBytes-total || ((index==2 || index==3) && bytes>4U*1024U*1024U)) return fail("Bundle asset payload limit exceeded");
    total+=static_cast<std::size_t>(bytes); ++counts[index];
    entries.push_back({static_cast<synthesis::NeuralAssetRole>(index),n,h,static_cast<std::size_t>(bytes)});
  }
  for (std::size_t index=0;index<4;++index) if (counts[index]!=1) return fail("Bundle required role is missing or duplicated");
  std::vector<std::vector<std::byte>> owned; owned.reserve(entries.size());
  std::vector<synthesis::NeuralBundleAssetInput> inputs; inputs.reserve(entries.size());
  for (const auto& entry:entries) {
    if (stop.stop_requested()) return cancelled();
    if (!regular(root/entry.name)) return fail("Bundle asset must be a regular non-symlink file");
    auto bytes=core::readFileBytesLimited(root/entry.name,entry.size);
    if (!bytes) return core::Result<Output>{bytes.error()};
    if (bytes.value().size()!=entry.size) return fail("Bundle asset length mismatch");
    owned.push_back(std::move(bytes.value()));
    inputs.push_back({entry.role,entry.name,owned.back(),entry.hash});
  }
  // freeze also checks canonical manifest identity and hashes its owned copies.
  return Output::freeze(std::move(identity),inputs,maximumTotalBytes,stop);
}
}
