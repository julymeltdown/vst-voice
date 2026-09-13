#include "seam/authoring/inventory_preflight.hpp"
#include "seam/authoring/generation_job.hpp"
#include "seam/voice_design/recipe_resource.hpp"
#include "seam/voicebank_production/project_codec.hpp"
#include "seam/voicebank/wav.hpp"
#include "seam/core/file_io.hpp"
#include "seam/core/sha256.hpp"
#include "seam/formats/json_value.hpp"
#include <algorithm>
#include <set>

namespace seam::authoring {
namespace {
struct PreflightJob final {
  std::string takeId, coverageKey, style, kind;
  std::int64_t pitchLayer{0};
  std::uint64_t frameCount{0U};
  std::vector<std::string> phones;
  [[nodiscard]] std::string order() const {
    return coverageKey + "\n" + style + "\n" + std::to_string(pitchLayer) + "\n" + takeId;
  }
};
// Events that are supposed to be silent; a phrase made only of them is still produced.
bool isSilenceEvent(std::string_view phone) {
  return phone == "pau" || phone == "cl" || phone == "sp" || phone == "sil";
}
core::Result<std::vector<PreflightJob>> parsePreflightJobs(const formats::JsonValue& plan) {
  const auto invalid = [] { return core::failure<std::vector<PreflightJob>>(
      core::ErrorCode::InvalidArgument, "Campaign jobs cannot be read for preflight"); };
  if (!plan.find("jobs") || !plan.find("jobs")->isArray()) return invalid();
  std::vector<PreflightJob> jobs;
  for (const auto& entry : plan.find("jobs")->asArray()) {
    if (!entry.isObject() || !entry.find("takeId") || !entry.find("takeId")->isString() ||
        !entry.find("coverageKey") || !entry.find("coverageKey")->isString() ||
        !entry.find("style") || !entry.find("style")->isString() ||
        !entry.find("pitchLayer") || !entry.find("pitchLayer")->isInteger() ||
        !entry.find("frameCount") || !entry.find("frameCount")->isInteger()) return invalid();
    PreflightJob job;
    job.takeId = entry.find("takeId")->asString();
    job.coverageKey = entry.find("coverageKey")->asString();
    job.style = entry.find("style")->asString();
    job.pitchLayer = entry.find("pitchLayer")->asInt64();
    job.frameCount = static_cast<std::uint64_t>(entry.find("frameCount")->asInt64());
    if (job.takeId.empty() || job.takeId.size() > 128U || job.coverageKey.size() > 256U || job.style.empty()) return invalid();
    const auto separator = job.coverageKey.find(':');
    if (separator == std::string::npos || separator == 0U) return invalid();
    job.kind = job.coverageKey.substr(0U, separator);
    auto rest = job.coverageKey.substr(separator + 1U);
    while (!rest.empty()) {
      const auto next = rest.find(':');
      const auto phone = rest.substr(0U, next);
      if (phone.empty() || phone.size() > 32U) return invalid();
      job.phones.push_back(phone);
      if (next == std::string::npos) break;
      rest = rest.substr(next + 1U);
    }
    if (job.phones.empty() || job.phones.size() > 4U) return invalid();
    jobs.push_back(std::move(job));
  }
  std::sort(jobs.begin(), jobs.end(), [](const auto& left, const auto& right) { return left.order() < right.order(); });
  return core::success(std::move(jobs));
}
bool isSilencePhrase(const PreflightJob& job) {
  return std::all_of(job.phones.begin(), job.phones.end(), [](const auto& phone) { return isSilenceEvent(phone); });
}
formats::JsonValue::Array stringArray(const std::vector<std::string>& values) {
  formats::JsonValue::Array array;
  array.reserve(values.size());
  for (const auto& value : values) array.emplace_back(value);
  return array;
}
}
core::Result<std::vector<std::string>> selectInventoryPreflightTakeIds(
    std::string_view campaignDefinition, std::string_view campaignSha256,
    InventoryPreflightLimits limits, std::stop_token stop) {
  using Output = std::vector<std::string>;
  const auto fail = [](std::string message) { return core::failure<Output>(core::ErrorCode::InvalidArgument, std::move(message)); };
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Preflight selection cancelled");
  if (limits.maximumPhrases == 0U || limits.maximumPhrases > 256U || limits.maximumFrames == 0U ||
      limits.maximumFrames > (1ULL << 32U)) return fail("Preflight exceeds its supported bounds");
  const auto verified = VerifiedGenerationCampaign::admit(campaignDefinition, campaignSha256, stop);
  if (!verified) return core::Result<Output>{verified.error()};
  const auto jobs = parsePreflightJobs(verified.value().plan());
  if (!jobs) return core::Result<Output>{jobs.error()};
  if (jobs.value().empty()) return fail("Campaign has no jobs to preflight");
  std::set<std::string> requiredPhones, requiredKinds, coveredPhones, coveredKinds;
  for (const auto& job : jobs.value()) {
    requiredKinds.insert(job.kind);
    for (const auto& phone : job.phones) requiredPhones.insert(phone);
  }
  std::vector<bool> used(jobs.value().size(), false);
  std::vector<std::size_t> order;
  std::uint64_t frames = 0U;
  while (coveredPhones.size() < requiredPhones.size() || coveredKinds.size() < requiredKinds.size()) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Preflight selection cancelled");
    std::size_t best = jobs.value().size();
    std::size_t bestGain = 0U;
    for (std::size_t index = 0U; index < jobs.value().size(); ++index) {
      if (used[index]) continue;
      const auto& job = jobs.value()[index];
      std::size_t gain = coveredKinds.count(job.kind) == 0U ? 1U : 0U;
      for (const auto& phone : job.phones) if (coveredPhones.count(phone) == 0U) ++gain;
      // Jobs are ordered canonically, so the first strict improvement wins the tie.
      if (gain > bestGain) { bestGain = gain; best = index; }
    }
    if (best == jobs.value().size() || bestGain == 0U) return fail("Campaign preflight cannot cover its declared phones and kinds");
    used[best] = true;
    const auto& job = jobs.value()[best];
    coveredKinds.insert(job.kind);
    for (const auto& phone : job.phones) coveredPhones.insert(phone);
    frames += job.frameCount;
    order.push_back(best);
    if (order.size() > limits.maximumPhrases)
      return fail("Campaign preflight exceeds the bound of " + std::to_string(limits.maximumPhrases) +
          " phrases while covering " + std::to_string(requiredPhones.size()) + " phones and " +
          std::to_string(requiredKinds.size()) + " kinds; raise the bound explicitly or drop the classes");
    if (frames > limits.maximumFrames)
      return fail("Campaign preflight exceeds the admitted frame budget before the bound is reached");
  }
  Output ids;
  ids.reserve(order.size());
  for (const auto index : order) ids.push_back(jobs.value()[index].takeId);
  return core::success(std::move(ids));
}
core::Result<InventoryPreflightReport> runInventoryPreflight(
    std::string_view campaignDefinition, std::string_view campaignSha256,
    const std::filesystem::path& directory, InventoryPreflightLimits limits,
    std::stop_token stop, std::function<std::optional<std::string>(std::size_t)> refusePhrase) {
  using Output = InventoryPreflightReport;
  const auto fail = [](std::string message) { return core::failure<Output>(core::ErrorCode::InvalidArgument, std::move(message)); };
  const auto verified = VerifiedGenerationCampaign::admit(campaignDefinition, campaignSha256, stop);
  if (!verified) return core::Result<Output>{verified.error()};
  const auto& plan = verified.value().plan();
  const auto guaranteed = parsePreflightJobs(plan);
  if (!guaranteed) return core::Result<Output>{guaranteed.error()};
  const auto selected = selectInventoryPreflightTakeIds(campaignDefinition, campaignSha256, limits, stop);
  if (!selected) return core::Result<Output>{selected.error()};
  const auto producer = voicebank_production::decodeProductionProject(plan.find("initialProducerJson")->asString());
  if (!producer) return core::Result<Output>{producer.error()};
  const auto recipe = voice_design::decodeVoiceRecipe(plan.find("recipeJson")->asString());
  if (!recipe) return core::Result<Output>{recipe.error()};
  const auto resource = voice_design::freezeVoiceRecipeResource(recipe.value());
  if (!resource) return core::Result<Output>{resource.error()};
  std::error_code error;
  if (!std::filesystem::create_directory(directory, error) || error)
    return fail("Preflight directory must be new with an existing parent");
  InventoryPreflightReport report;
  std::set<std::string> requiredPhones, requiredKinds;
  for (const auto& job : guaranteed.value()) {
    requiredKinds.insert(job.kind);
    for (const auto& phone : job.phones) requiredPhones.insert(phone);
  }
  report.requiredPhones = requiredPhones.size();
  report.requiredKinds = requiredKinds.size();
  for (std::size_t index = 0U; index < selected.value().size(); ++index) {
    if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Preflight rendering cancelled");
    const auto found = std::find_if(guaranteed.value().begin(), guaranteed.value().end(),
        [&](const auto& job) { return job.takeId == selected.value()[index]; });
    if (found == guaranteed.value().end()) return fail("Preflight selection left the campaign's own jobs");
    const auto& job = *found;
    InventoryPreflightPhrase phrase;
    phrase.takeId = job.takeId;
    phrase.coverageKey = job.coverageKey;
    phrase.style = job.style;
    phrase.pitchLayer = job.pitchLayer;
    phrase.requiredPhones = job.phones;
    const auto number = std::to_string(index);
    const auto jobDirectory = directory / ("phrase-" + number);
    auto refuse = refusePhrase ? refusePhrase(index) : std::optional<std::string>{};
    std::optional<std::string> manifestSha256;
    if (!refuse) {
      auto prepared = prepareInventoryGenerationJob(directory / ("phrase-" + number + "-score.json"), jobDirectory,
          producer.value(), job.takeId, {resource.value(), job.style}, stop);
      if (!prepared) refuse = prepared.error().message;
      else manifestSha256 = prepared.value().manifestSha256;
    }
    if (!refuse) {
      auto rendered = runGenerationJob(jobDirectory, *manifestSha256, stop);
      if (!rendered) {
        refuse = rendered.error().message;
      } else {
        const auto metadata = core::readTextFileLimited(rendered.value().metadataPath, 4U * 1024U * 1024U);
        const auto parsed = metadata ? formats::parseJson(metadata.value()) : core::Result<formats::JsonValue>{metadata.error()};
        if (!parsed || !parsed.value().find("markers") || !parsed.value().find("markers")->isArray()) {
          refuse = "Rendered candidate metadata has no markers";
        } else {
          for (const auto& marker : parsed.value().find("markers")->asArray()) {
            if (!marker.isObject() || !marker.find("phone") || !marker.find("phone")->isString()) {
              refuse = "Rendered candidate marker is malformed";
              break;
            }
            const auto phone = marker.find("phone")->asString();
            if (std::find(phrase.producedPhones.begin(), phrase.producedPhones.end(), phone) == phrase.producedPhones.end())
              phrase.producedPhones.push_back(phone);
          }
          const auto wav = voicebank::readWav(rendered.value().audioPath, {}, stop);
          if (!wav) {
            refuse = "Rendered candidate audio cannot be read: " + wav.error().message;
          } else {
            const auto mono = wav.value().monoMix();
            phrase.frameCount = static_cast<std::uint64_t>(mono.size());
            phrase.peak = voicebank::analyzeAudio(mono).peak;
            phrase.nonzeroFrames = static_cast<std::uint64_t>(std::count_if(mono.begin(), mono.end(),
                [](float sample) { return sample != 0.0F; }));
            if (phrase.frameCount > limits.maximumFrames) {
              refuse = "Rendered phrase exceeded the admitted frame budget";
            } else {
              std::vector<std::string> missing;
              for (const auto& phone : job.phones)
                if (std::find(phrase.producedPhones.begin(), phrase.producedPhones.end(), phone) == phrase.producedPhones.end())
                  missing.push_back(phone);
              if (!missing.empty()) {
                std::string detail = "The candidate produced no gesture for:";
                for (const auto& phone : missing) detail += " " + phone;
                refuse = std::move(detail);
              } else if (phrase.nonzeroFrames == 0U && !isSilencePhrase(job)) {
                refuse = "The candidate rendered silence for a phrase that declares audible phones";
              }
            }
          }
        }
      }
    }
    if (refuse) {
      phrase.verdict = "REFUSED";
      phrase.detail = *refuse;
      ++report.defective;
      report.defectiveClasses.push_back(job.coverageKey);
    } else {
      phrase.verdict = "PRODUCED";
      phrase.detail = isSilencePhrase(job) ? "declared silence event" : "gestures and audio produced";
      ++report.produced;
    }
    report.phrases.push_back(std::move(phrase));
  }
  if (stop.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Preflight rendering cancelled");
  report.passed = report.defective == 0U && report.produced == report.phrases.size() && !report.phrases.empty();
  formats::JsonValue::Array phrases;
  for (const auto& phrase : report.phrases) {
    phrases.emplace_back(formats::JsonValue::Object{
        {"takeId", phrase.takeId}, {"coverageKey", phrase.coverageKey}, {"style", phrase.style},
        {"pitchLayer", phrase.pitchLayer}, {"verdict", phrase.verdict}, {"detail", phrase.detail},
        {"requiredPhones", stringArray(phrase.requiredPhones)}, {"producedPhones", stringArray(phrase.producedPhones)},
        {"peak", phrase.peak}, {"frameCount", static_cast<std::int64_t>(phrase.frameCount)},
        {"nonzeroFrames", static_cast<std::int64_t>(phrase.nonzeroFrames)}});
  }
  report.json = formats::stringifyJson(formats::JsonValue::Object{
      {"formatId", "com.project-seam.inventory-preflight"}, {"schemaVersion", std::int64_t{1}},
      {"selectionRevision", std::int64_t{1}},
      {"status", report.passed ? "PASS" : "FAIL"}, {"releaseEligible", false},
      {"campaignSha256", std::string{campaignSha256}},
      {"producerSha256", plan.find("initialProducerSha256")->asString()},
      {"recipeSha256", plan.find("recipeSha256")->asString()},
      {"requiredPhones", static_cast<std::int64_t>(report.requiredPhones)},
      {"requiredKinds", static_cast<std::int64_t>(report.requiredKinds)},
      {"phraseCount", static_cast<std::int64_t>(report.phrases.size())},
      {"produced", static_cast<std::int64_t>(report.produced)},
      {"defective", static_cast<std::int64_t>(report.defective)},
      {"defectiveClasses", stringArray(report.defectiveClasses)},
      {"phrases", std::move(phrases)}}, true);
  const auto saved = core::durableAtomicWriteTextNew(directory / "report.json", report.json);
  if (!saved) return core::Result<Output>{saved.error()};
  return core::success(std::move(report));
}
core::Result<void> verifyInventoryPreflight(
    std::string_view report, std::string_view campaignDefinition, std::string_view campaignSha256,
    std::stop_token stop) {
  const auto fail = [](std::string message) { return core::failure<void>(core::ErrorCode::InvalidArgument, std::move(message)); };
  if (stop.stop_requested()) return core::failure<void>(core::ErrorCode::Conflict, "Preflight verification cancelled");
  if (report.empty() || report.size() > 32U * 1024U * 1024U || campaignSha256.size() != 64U) return fail("Preflight report is invalid");
  const auto parsed = formats::parseJson(report);
  if (!parsed || !parsed.value().isObject()) return fail("Preflight report is not an object");
  const auto& root = parsed.value();
  const auto stringField = [&](const char* name) -> std::optional<std::string> {
    if (!root.find(name) || !root.find(name)->isString()) return std::nullopt;
    return root.find(name)->asString();
  };
  const auto integerField = [&](const char* name) -> std::optional<std::int64_t> {
    if (!root.find(name) || !root.find(name)->isInteger()) return std::nullopt;
    return root.find(name)->asInt64();
  };
  if (stringField("formatId") != std::optional<std::string>{"com.project-seam.inventory-preflight"} ||
      integerField("schemaVersion") != std::optional<std::int64_t>{1} ||
      integerField("selectionRevision") != std::optional<std::int64_t>{1}) return fail("Preflight report format is not admitted");
  if (!root.find("releaseEligible") || root.find("releaseEligible")->isBool() == false ||
      root.find("releaseEligible")->asBool()) return fail("Preflight report cannot be release eligible");
  if (stringField("campaignSha256") != std::optional<std::string>{std::string{campaignSha256}})
    return fail("Preflight report belongs to a different campaign");
  const auto verified = VerifiedGenerationCampaign::admit(campaignDefinition, campaignSha256, stop);
  if (!verified) return core::Result<void>{verified.error()};
  const auto& plan = verified.value().plan();
  if (stringField("producerSha256") != std::optional<std::string>{plan.find("initialProducerSha256")->asString()})
    return fail("Preflight report measured a different frozen producer state");
  if (stringField("recipeSha256") != std::optional<std::string>{plan.find("recipeSha256")->asString()})
    return fail("Preflight report measured a different recipe");
  if (!root.find("phrases") || !root.find("phrases")->isArray() || root.find("phrases")->asArray().empty() ||
      root.find("phrases")->asArray().size() > 256U) return fail("Preflight report has no bounded phrase list");
  // The phrase set has to be the campaign's own deterministic selection, in order, so a
  // report cannot skip a class, relabel one, or invent a phrase the campaign never declared.
  const auto canonical = selectInventoryPreflightTakeIds(campaignDefinition, campaignSha256, {}, stop);
  if (!canonical) return core::Result<void>{canonical.error()};
  if (canonical.value().size() != root.find("phrases")->asArray().size())
    return fail("Preflight report does not render the campaign's canonical phrase set");
  const auto jobs = parsePreflightJobs(plan);
  if (!jobs) return core::Result<void>{jobs.error()};
  // The status and counts are derived from the phrases, so a rewritten verdict or a
  // hand-edited status cannot admit a campaign the renderer did not clear.
  std::size_t produced = 0U, defective = 0U, total = 0U;
  std::vector<std::string> defectiveClasses;
  for (const auto& entry : root.find("phrases")->asArray()) {
    if (!entry.isObject() || !entry.find("verdict") || !entry.find("verdict")->isString() ||
        !entry.find("coverageKey") || !entry.find("coverageKey")->isString()) return fail("Preflight phrase entry is malformed");
    if (entry.find("takeId") == nullptr || !entry.find("takeId")->isString() ||
        entry.find("takeId")->asString() != canonical.value()[total])
      return fail("Preflight report does not render the campaign's canonical phrase set");
    const auto job = std::find_if(jobs.value().begin(), jobs.value().end(),
        [&](const auto& value) { return value.takeId == canonical.value()[total]; });
    if (job == jobs.value().end() || job->coverageKey != entry.find("coverageKey")->asString())
      return fail("Preflight phrase does not match the campaign's own job");
    ++total;
    const auto verdict = entry.find("verdict")->asString();
    if (verdict == "PRODUCED") ++produced;
    else if (verdict == "REFUSED") { ++defective; defectiveClasses.push_back(entry.find("coverageKey")->asString()); }
    else return fail("Preflight phrase has an unadmitted verdict");
  }
  if (total != static_cast<std::size_t>(integerField("phraseCount").value_or(-1)) ||
      produced != static_cast<std::size_t>(integerField("produced").value_or(-1)) ||
      defective != static_cast<std::size_t>(integerField("defective").value_or(-1)))
    return fail("Preflight counts do not follow from its own phrases");
  if (!root.find("defectiveClasses") || !root.find("defectiveClasses")->isArray() ||
      root.find("defectiveClasses")->asArray().size() != defectiveClasses.size())
    return fail("Preflight defective classes do not follow from its own phrases");
  for (std::size_t index = 0U; index < defectiveClasses.size(); ++index)
    if (!root.find("defectiveClasses")->asArray()[index].isString() ||
        root.find("defectiveClasses")->asArray()[index].asString() != defectiveClasses[index])
      return fail("Preflight defective classes do not follow from its own phrases");
  const std::string expected = defective == 0U && produced == total && total != 0U ? "PASS" : "FAIL";
  if (stringField("status") != std::optional<std::string>{expected})
    return fail("Preflight status does not follow from its own phrases");
  if (expected != "PASS") return fail("Preflight did not clear every phrase it rendered");
  return core::success();
}
core::Result<void> verifyCampaignPreflight(
    const std::filesystem::path& campaignRoot, std::string_view campaignDefinition,
    std::string_view campaignSha256, std::stop_token stop) {
  const auto bytes = core::readTextFileLimited(campaignRoot / "preflight" / "report.json", 32U * 1024U * 1024U);
  if (!bytes) return core::failure<void>(core::ErrorCode::NotFound,
      "Campaign has no preflight/report.json; render its held-out phrases before multiplying the bank");
  return verifyInventoryPreflight(bytes.value(), campaignDefinition, campaignSha256, stop);
}
}
