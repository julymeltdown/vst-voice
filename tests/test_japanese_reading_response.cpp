#include "test_framework.hpp"
#include "seam/authoring/japanese_reading_response.hpp"
#include "seam/formats/json_value.hpp"

namespace {
const std::string response = R"({"schemaVersion":1,"source":"私, XYZ","tokens":[
{"byteOffset":0,"byteLength":3,"surface":"私","status":"known","reading":"ワタシ","pronunciation":"ワタシ"},
{"byteOffset":3,"byteLength":1,"surface":",","status":"unknown","reading":null,"pronunciation":null},
{"byteOffset":5,"byteLength":3,"surface":"XYZ","status":"unknown","reading":null,"pronunciation":null}]})";
const seam::phonemizer::JapaneseReadingIdentity identity{std::string(40U, 'a'), std::string(64U, 'b'), std::string(64U, 'c')};
}
TEST_CASE("Japanese reading JSON decoder binds exact typed source spans and rejects schema drift") {
  using namespace seam;
  const auto decoded = authoring::decodeJapaneseReadingResponse("私, XYZ", response, identity); CHECK(decoded);
  CHECK(decoded.value().tokens.size() == 3U); CHECK(decoded.value().tokens[0].pronunciation == "ワタシ");
  CHECK(decoded.value().tokens[1].status == phonemizer::JapaneseReadingStatus::Unknown);
  CHECK(!authoring::decodeJapaneseReadingResponse("他, XYZ", response, identity));
  for (int change = 0; change < 12; ++change) {
    auto tree = formats::parseJson(response); CHECK(tree);
    auto& token = tree.value().find("tokens")->asArray()[0];
    if (change == 0) *tree.value().find("schemaVersion") = formats::JsonValue{2.0};
    if (change == 1) tree.value().asObject()["extra"] = formats::JsonValue{true};
    if (change == 2) *token.find("byteOffset") = formats::JsonValue{-1.0};
    if (change == 3) *token.find("byteOffset") = formats::JsonValue{0.5};
    if (change == 4) *token.find("byteLength") = formats::JsonValue{std::int64_t{2}};
    if (change == 5) *token.find("status") = formats::JsonValue{"guessed"};
    if (change == 6) *token.find("reading") = formats::JsonValue{nullptr};
    if (change == 7) token.asObject().erase("surface");
    if (change == 8) *token.find("surface") = formats::JsonValue{"他"};
    if (change == 9) *token.find("reading") = formats::JsonValue{true};
    if (change == 10) *token.find("byteLength") = formats::JsonValue{std::int64_t{5000}};
    if (change == 11) *tree.value().find("tokens") = formats::JsonValue{formats::JsonValue::Array{}};
    CHECK(!authoring::decodeJapaneseReadingResponse("私, XYZ", formats::stringifyJson(tree.value()), identity));
  }
}
TEST_CASE("Japanese reading JSON decoder rejects oversized deep duplicate and cancelled responses") {
  using seam::authoring::decodeJapaneseReadingResponse;
  CHECK(!decodeJapaneseReadingResponse("私, XYZ", std::string(1048577U, ' '), identity));
  CHECK(!decodeJapaneseReadingResponse("私, XYZ", std::string(4096U, '[') + "0" + std::string(4096U, ']'), identity));
  CHECK(!decodeJapaneseReadingResponse("私, XYZ", R"({"schemaVersion":1,"schemaVersion":1,"source":"私, XYZ","tokens":[]})", identity));
  CHECK(!decodeJapaneseReadingResponse("私, XYZ", response + "{}", identity));
  auto oversized = seam::formats::parseJson(response); CHECK(oversized);
  *oversized.value().find("tokens") = seam::formats::JsonValue{seam::formats::JsonValue::Array(4097U)};
  CHECK(!decodeJapaneseReadingResponse("私, XYZ", seam::formats::stringifyJson(oversized.value()), identity));
  auto tree = seam::formats::parseJson(response); CHECK(tree);
  *tree.value().find("source") = seam::formats::JsonValue{std::string(4097U, 'a')};
  CHECK(!decodeJapaneseReadingResponse("私, XYZ", seam::formats::stringifyJson(tree.value()), identity));
  std::stop_source stop; stop.request_stop();
  CHECK(!decodeJapaneseReadingResponse("私, XYZ", response, identity, stop.get_token()));
}
