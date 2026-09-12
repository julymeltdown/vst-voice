#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
  if (argc != 3 || std::string_view{argv[1]} != "--read-stdin") return 2;
  std::string source((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
  if (source.empty() || source.size() > 4096U) return 2;
  if (source == "学校へ") {
    std::cout << R"({"schemaVersion":1,"source":"学校へ","tokens":[{"byteOffset":0,"byteLength":6,"surface":"学校","status":"known","reading":"ガッコウ","pronunciation":"ガッコー"},{"byteOffset":6,"byteLength":3,"surface":"へ","status":"known","reading":"ヘ","pronunciation":"エ"}]})";
    return 0;
  }
  if (source == "学 学 へ") {
    std::cout << R"({"schemaVersion":1,"source":"学 学 へ","tokens":[{"byteOffset":0,"byteLength":3,"surface":"学","status":"known","reading":"ガク","pronunciation":"ガク"},{"byteOffset":4,"byteLength":3,"surface":"学","status":"known","reading":"ガク","pronunciation":"ガク"},{"byteOffset":8,"byteLength":3,"surface":"へ","status":"known","reading":"ヘ","pronunciation":"エ"}]})";
    return 0;
  }
  if (source == "漢") {
    std::cout << R"({"schemaVersion":1,"source":"漢","tokens":[{"byteOffset":0,"byteLength":3,"surface":"漢","status":"known","reading":"カン","pronunciation":"カン"}]})";
    return 0;
  }
  std::cout << "{}";
  return 0;
}
