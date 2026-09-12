#include "test_framework.hpp"
#include "seam/phonemizer/japanese_reading.hpp"
#include "seam/core/sha256.hpp"
#include <limits>

TEST_CASE("Japanese reading spans preserve UTF8 punctuation unknowns and whitespace") {
  using namespace seam::phonemizer;
  const JapaneseReadingIdentity identity{std::string(40U, 'a'), std::string(64U, 'b'), std::string(64U, 'c')};
  const std::string source = "私, XYZ　";
  JapaneseReadingResult value{identity, seam::core::sha256Hex(source), {
      {0U, 3U, "私", JapaneseReadingStatus::Known, "ワタシ", "ワタシ"},
      {3U, 1U, ",", JapaneseReadingStatus::Known, ",", ","},
      {5U, 3U, "XYZ", JapaneseReadingStatus::Unknown, {}, {}}}};
  CHECK(validateJapaneseReading(source, value, identity));
  for (int change = 0; change < 10; ++change) {
    auto bad = value;
    if (change == 0) bad.tokens[0].byteOffset = 1U;
    if (change == 1) bad.tokens[0].byteLength = 2U;
    if (change == 2) bad.tokens[1].byteOffset = 2U;
    if (change == 3) bad.tokens[0].surface = "他";
    if (change == 4) bad.tokens.erase(bad.tokens.begin() + 1);
    if (change == 5) bad.tokens[2].pronunciation = "エックス";
    if (change == 6) bad.tokens[0].lexicalReading = "*";
    if (change == 7) bad.identity.dictionarySha256[0] = 'c';
    if (change == 8) bad.sourceSha256[0] = bad.sourceSha256[0] == 'a' ? 'b' : 'a';
    if (change == 9) bad.identity.helperSha256[0] = 'd';
    CHECK(!validateJapaneseReading(source, bad, identity));
  }
  auto missing = value; missing.tokens[2].status = JapaneseReadingStatus::MissingReading;
  CHECK(validateJapaneseReading(source, missing, identity));
  std::stop_source stop; stop.request_stop(); CHECK(!validateJapaneseReading(source, value, identity, stop.get_token()));
  CHECK(!validateJapaneseReading(std::string(4097U, 'a'), value, identity));
  CHECK(!validateJapaneseReading(std::string("a\0b", 3U), value, identity));
  CHECK(!validateJapaneseReading(std::string("\xff", 1U), value, identity));
}

TEST_CASE("Japanese reading validation bounds aggregate expansion and permits only omitted whitespace") {
  using namespace seam::phonemizer;
  const JapaneseReadingIdentity identity{std::string(40U, 'a'), std::string(64U, 'b'), std::string(64U, 'c')};
  const std::string source = "aaaaaaaaa";
  JapaneseReadingResult value{identity, seam::core::sha256Hex(source), {}};
  for (std::size_t i = 0U; i < source.size(); ++i)
    value.tokens.push_back({i, 1U, "a", JapaneseReadingStatus::Known, std::string(4096U, 'a'), std::string(4096U, 'a')});
  CHECK(!validateJapaneseReading(source, value, identity));
  for (auto& token : value.tokens) { token.lexicalReading = "ア"; token.pronunciation = "ア"; }
  CHECK(validateJapaneseReading(source, value, identity));
  auto bad = value; bad.tokens.front().byteLength = std::numeric_limits<std::size_t>::max();
  CHECK(!validateJapaneseReading(source, bad, identity));
  bad = value; bad.tokens.front().status = static_cast<JapaneseReadingStatus>(99);
  CHECK(!validateJapaneseReading(source, bad, identity));
  bad = value; bad.tokens.front().pronunciation.reset(); CHECK(!validateJapaneseReading(source, bad, identity));
  bad = value; bad.tokens.resize(4097U); CHECK(!validateJapaneseReading(source, bad, identity));
  const std::string whitespace = " \t　\n";
  JapaneseReadingResult empty{identity, seam::core::sha256Hex(whitespace), {}};
  CHECK(validateJapaneseReading(whitespace, empty, identity));
  empty.sourceSha256 = seam::core::sha256Hex("漢"); CHECK(!validateJapaneseReading("漢", empty, identity));
}

TEST_CASE("validated Japanese dictionary readings project to SEAM phones without inventing unknowns") {
  using namespace seam::phonemizer;
  const JapaneseReadingIdentity identity{std::string(40U, 'a'), std::string(64U, 'b'), std::string(64U, 'c')};
  JapaneseReadingResult result{identity, seam::core::sha256Hex("学校XYZ"), {
      {0U, 6U, "学校", JapaneseReadingStatus::Known, "ガッコウ", "ガッコー"},
      {6U, 3U, "XYZ", JapaneseReadingStatus::Unknown, {}, {}}}};
  const auto projected = projectJapaneseReadingPhones(result); CHECK(projected); CHECK(projected.value().size() == 2U);
  CHECK(projected.value()[0].readingTokenIndex == 0U);
  CHECK(projected.value()[0].phones == (std::vector<std::string>{"g", "a", "cl", "k", "o", "o"}));
  CHECK(projected.value()[1].readingTokenIndex == 1U); CHECK(projected.value()[1].phones.empty());
  auto bad = result; bad.tokens[0].pronunciation = std::string("𠮷"); CHECK(!projectJapaneseReadingPhones(bad));
  bad = result; bad.tokens[0].pronunciation.reset(); CHECK(!projectJapaneseReadingPhones(bad));
  std::stop_source stop; stop.request_stop(); CHECK(!projectJapaneseReadingPhones(result, stop.get_token()));
}
