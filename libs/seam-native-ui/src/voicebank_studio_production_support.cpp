#include "voicebank_studio_production_support.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace seam::native_ui::voicebank_studio_internal {

std::string coverageKey(const voicebank::Unit& unit) {
  auto kind = std::string{voicebank::unitKindName(unit.kind)};
  if (unit.kind == voicebank::UnitKind::Glottal) kind = "glottal-attack";
  std::string key = std::move(kind);
  for (const auto& phone : unit.phones) key += ":" + phone;
  return key;
}

std::string currentUtcTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto value = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &value);
#else
  gmtime_r(&value, &utc);
#endif
  std::ostringstream stream;
  stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}

std::map<std::string, std::string, std::less<>> metadataValues(
    const voicebank::Unit& unit) {
  std::map<std::string, std::string, std::less<>> values{
      {"audioOffset", std::to_string(unit.markers.audioOffset)},
      {"consonantEnd", std::to_string(unit.markers.consonantEnd)},
      {"vowelOnset", std::to_string(unit.markers.vowelOnset)},
      {"stableStart", std::to_string(unit.markers.stableStart)},
      {"loopStart", unit.markers.loopStart.has_value()
                        ? std::to_string(*unit.markers.loopStart) : ""},
      {"loopEnd", unit.markers.loopEnd.has_value()
                      ? std::to_string(*unit.markers.loopEnd) : ""},
      {"releaseStart", unit.markers.releaseStart.has_value()
                           ? std::to_string(*unit.markers.releaseStart) : ""},
      {"audioEnd", std::to_string(unit.markers.audioEnd)},
  };
  std::ostringstream pitchMarks;
  for (std::size_t index = 0U; index < unit.pitchMarks.size(); ++index) {
    if (index != 0U) pitchMarks << ';';
    pitchMarks << unit.pitchMarks[index].frame << ':'
               << unit.pitchMarks[index].confidence << ':'
               << (unit.pitchMarks[index].locked ? '1' : '0');
  }
  values.emplace("pitchMarks", pitchMarks.str());
  return values;
}

}
