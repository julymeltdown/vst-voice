// Development-only dependency probe. Not linked into SEAM or a shipping helper.
// Build against the exact checkout recorded in JAPANESE_READING_INTAKE_2026-09-08.md.
#include "mecab.h"
#if defined(_WIN32)
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif
#include <cstring>
#include <iostream>
#include <string_view>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

std::string json(std::string_view text) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string output = "\"";
  for (unsigned char c : text) {
    if (c == '"' || c == '\\') { output += '\\'; output += static_cast<char>(c); }
    else if (c < 32U) { output += "\\u00"; output += digits[c >> 4U]; output += digits[c & 15U]; }
    else output += static_cast<char>(c);
  }
  return output + '"';
}

std::optional<std::vector<std::string>> fields(std::string_view feature) {
  std::vector<std::string> result;
  std::size_t i = 0U;
  for (;;) {
    if (result.size() >= 32U) return {};
    std::string value;
    if (i < feature.size() && feature[i] == '"') {
      ++i; bool closed = false;
      while (i < feature.size()) {
        const char c = feature[i++];
        if (c != '"') value += c;
        else if (i < feature.size() && feature[i] == '"') { value += '"'; ++i; }
        else { closed = true; break; }
      }
      if (!closed || (i < feature.size() && feature[i] != ',')) return {};
    } else {
      while (i < feature.size() && feature[i] != ',') {
        if (feature[i] == '"') return {};
        value += feature[i++];
      }
    }
    result.push_back(std::move(value));
    if (i == feature.size()) break;
    ++i;
  }
  return result;
}

int main(int argc, char** argv) {
#if defined(_WIN32)
  if (::_setmode(::_fileno(stdin), _O_BINARY) == -1 ||
      ::_setmode(::_fileno(stdout), _O_BINARY) == -1 ||
      ::_setmode(::_fileno(stderr), _O_BINARY) == -1) return 2;
#endif
  if (argc >= 2 && std::string_view{argv[1]} == "--compile-dictionary")
    return mecab_dict_index(argc - 1, argv + 1);
  const bool useStdin = argc == 3 && std::string_view{argv[1]} == "--read-stdin";
  if (!useStdin && (argc != 4 || std::string_view{argv[1]} != "--read")) {
    std::cerr << "Usage: probe --read-stdin DICTIONARY | --read DICTIONARY UTF8_TEXT | --compile-dictionary [index arguments]\n";
    return 2;
  }
  std::string input;
  if (useStdin) {
    for (int c; (c = std::cin.get()) != std::char_traits<char>::eof();) {
      if (input.size() == 4096U) { std::cerr << "Text exceeds 4096 bytes\n"; return 2; }
      input.push_back(static_cast<char>(c));
    }
    if (!std::cin.eof()) return 2;
  } else input = argv[3];
  const auto bytes = input.size();
  if (bytes == 0U || bytes > 4096U || input.find('\0') != std::string::npos) { std::cerr << "Text must contain 1..4096 non-NUL bytes\n"; return 2; }
  Mecab reader{}; Mecab_initialize(&reader);
  if (!Mecab_load(&reader, argv[2])) { Mecab_clear(&reader); return 3; }
  auto* lattice = static_cast<MeCab::Lattice*>(reader.lattice);
  lattice->set_sentence(input.data(), bytes);
  if (!static_cast<MeCab::Tagger*>(reader.tagger)->parse(lattice)) { Mecab_clear(&reader); return 4; }
  const auto start = reinterpret_cast<std::uintptr_t>(lattice->sentence());
  std::string output = "{\"schemaVersion\":1,\"source\":" + json(input) + ",\"tokens\":[";
  std::size_t visited = 0U, count = 0U, previousEnd = 0U;
  for (const auto* node = lattice->bos_node(); node; node = node->next) {
    if (++visited > 4098U) { Mecab_clear(&reader); return 5; }
    if (node->stat == MECAB_BOS_NODE || node->stat == MECAB_EOS_NODE) continue;
    const auto address = reinterpret_cast<std::uintptr_t>(node->surface);
    if (address < start || address - start > bytes || node->length == 0U || node->length > bytes - (address - start) ||
        address - start < previousEnd || !node->feature) { Mecab_clear(&reader); return 5; }
    const auto offset = address - start; previousEnd = offset + node->length;
    std::size_t size = 0U;
    while (size <= 65536U && node->feature[size] != '\0') ++size;
    if (size > 65536U) { Mecab_clear(&reader); return 5; }
    const auto values = fields({node->feature, size}); if (!values) { Mecab_clear(&reader); return 5; }
    const bool known = node->stat != MECAB_UNK_NODE && values->size() >= 9U &&
        !(*values)[7].empty() && (*values)[7] != "*" && !(*values)[8].empty() && (*values)[8] != "*";
    if (count++) output += ',';
    output += "{\"byteOffset\":" + std::to_string(offset) + ",\"byteLength\":" + std::to_string(node->length) +
        ",\"surface\":" + json({node->surface, node->length}) + ",\"status\":" +
        json(known ? "known" : node->stat == MECAB_UNK_NODE ? "unknown" : "missing-reading") +
        ",\"reading\":" + (known ? json((*values)[7]) : "null") + ",\"pronunciation\":" +
        (known ? json((*values)[8]) : "null") + "}";
    if (output.size() > 1048576U) { Mecab_clear(&reader); return 5; }
  }
  output += "]}\n";
  std::cout << output;
  Mecab_clear(&reader);
  return 0;
}
