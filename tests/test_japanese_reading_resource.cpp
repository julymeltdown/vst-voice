#include "test_framework.hpp"
#include "test_support.hpp"
#include "seam/authoring/japanese_reading_resource.hpp"
#include "seam/authoring/japanese_reading_stage.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#if defined(__APPLE__) || defined(__linux__)
#include <sys/stat.h>
#endif

TEST_CASE("reading resource verification binds executable and exact dictionary set without accepting extra configuration") {
  using namespace seam; using namespace seam::authoring;
  const auto root = test::support::temporaryDirectory("reading-resource");
  std::filesystem::create_directory(root / "dictionary");
  JapaneseReadingResourceSpec spec{root / "reader", core::sha256Hex("helper fixture"), std::string(40U, 'a'), root / "dictionary", {}};
  CHECK(core::durableAtomicWriteText(spec.executable, "helper fixture"));
  for (std::size_t i = 0U; i < 4U; ++i) {
    const auto bytes = "dictionary fixture " + std::to_string(i);
    CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / kJapaneseDictionaryFiles[i], bytes));
    spec.dictionarySha256[i] = core::sha256Hex(bytes);
  }
  const auto verified = VerifiedJapaneseReadingResource::verify(spec); CHECK(verified); CHECK(verified.value().revalidate());
  CHECK(verified.value().identity().engineRevision == spec.engineRevision);
  CHECK(verified.value().identity().helperSha256 == spec.executableSha256);
  CHECK(verified.value().identity().dictionarySha256.size() == 64U);
  auto bad = spec; bad.executableSha256 = std::string(64U, 'f'); CHECK(!VerifiedJapaneseReadingResource::verify(bad));
  bad = spec; bad.dictionarySha256[0] = std::string(64U, 'f'); CHECK(!VerifiedJapaneseReadingResource::verify(bad));
  bad = spec; bad.engineRevision = "unversioned"; CHECK(!VerifiedJapaneseReadingResource::verify(bad));
  CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / "dicrc", "userdic=unapproved.dic"));
  CHECK(!verified.value().revalidate());
  std::filesystem::rename(spec.dictionaryDirectory / "dicrc", root / "removed-dicrc");
  CHECK(verified.value().revalidate());
  CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / "sys.dic", "changed dictionary"));
  CHECK(!verified.value().revalidate());
  CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / "sys.dic", "dictionary fixture 2"));
  CHECK(verified.value().revalidate());
  std::filesystem::resize_file(spec.dictionaryDirectory / "sys.dic", 128U * 1024U * 1024U + 1U);
  const auto oversized = verified.value().revalidate(); CHECK(!oversized);
  CHECK(oversized.error().message.find("size bounds") != std::string::npos);
  CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / "sys.dic", "dictionary fixture 2"));
  std::stop_source stop; stop.request_stop(); CHECK(!VerifiedJapaneseReadingResource::verify(spec, stop.get_token()));
#if defined(__APPLE__) || defined(__linux__)
  std::filesystem::create_symlink(spec.executable, root / "linked-reader");
  bad = spec; bad.executable = root / "linked-reader";
  const auto linked = VerifiedJapaneseReadingResource::verify(bad); CHECK(!linked); CHECK(linked.error().message.find("symlink") != std::string::npos);
  std::filesystem::rename(spec.dictionaryDirectory / "sys.dic", root / "saved-sys.dic");
  CHECK(::mkfifo((spec.dictionaryDirectory / "sys.dic").c_str(), 0600) == 0);
  const auto special = verified.value().revalidate(); CHECK(!special);
  CHECK(special.error().message.find("regular") != std::string::npos);
#endif
}

TEST_CASE("private reading staging owns verified independent copies until the last consumer retires") {
  using namespace seam; using namespace seam::authoring;
  const auto root = test::support::temporaryDirectory("reading-stage");
  std::filesystem::create_directory(root / "dictionary");
  JapaneseReadingResourceSpec spec{root / "reader", core::sha256Hex("helper"), std::string(40U, 'a'), root / "dictionary", {}};
  CHECK(core::durableAtomicWriteText(spec.executable, "helper"));
  for (std::size_t i = 0U; i < 4U; ++i) {
    CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / kJapaneseDictionaryFiles[i], "dictionary"));
    spec.dictionarySha256[i] = core::sha256Hex("dictionary");
  }
  const auto verified = VerifiedJapaneseReadingResource::verify(spec); CHECK(verified);
  std::optional<StagedJapaneseReadingResource> retained;
  std::filesystem::path stageRoot;
  {
    auto staged = StagedJapaneseReadingResource::prepare(verified.value(), root); CHECK(staged);
    stageRoot = staged.value().resource().spec().executable.parent_path();
    CHECK(stageRoot != root); CHECK(staged.value().resource().identity() == verified.value().identity());
    CHECK(staged.value().resource().revalidate());
    const auto mode = std::filesystem::status(stageRoot).permissions();
#if !defined(_WIN32)
    CHECK((mode & std::filesystem::perms::group_all) == std::filesystem::perms::none);
#endif
    CHECK((mode & std::filesystem::perms::owner_write) == std::filesystem::perms::none);
    std::filesystem::resize_file(spec.executable, 1U);
    std::filesystem::resize_file(spec.dictionaryDirectory / "sys.dic", 1U);
    CHECK(staged.value().resource().revalidate()); // Not hard links to the original inodes.
    CHECK(core::durableAtomicWriteText(spec.executable, "changed"));
    CHECK(core::durableAtomicWriteText(spec.dictionaryDirectory / "sys.dic", "changed"));
    CHECK(!verified.value().revalidate()); CHECK(staged.value().resource().revalidate());
    retained = staged.value();
  }
  CHECK(std::filesystem::exists(stageRoot)); CHECK(retained->resource().revalidate());
  retained.reset(); CHECK(!std::filesystem::exists(stageRoot));
  CHECK(core::readTextFileLimited(spec.executable, 1024U).value() == "changed");
  const auto count = [&] { return std::distance(std::filesystem::directory_iterator(root), std::filesystem::directory_iterator{}); };
  const auto before = count();
  CHECK(!StagedJapaneseReadingResource::prepare(verified.value(), root)); CHECK(count() == before);
  std::stop_source stop; stop.request_stop();
  CHECK(!StagedJapaneseReadingResource::prepare(verified.value(), root, stop.get_token())); CHECK(count() == before);
}
