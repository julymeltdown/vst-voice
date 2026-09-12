#include "seam/rendering/render_scheduler.hpp"
#include "seam/rendering/render_pipeline.hpp"

#include <algorithm>
#include <exception>
#include <limits>

namespace seam::rendering {

core::Result<synthesis::PhraseAudio> BackgroundRenderScheduler::assembleSnapshotCompletions(
    std::span<const RenderSnapshot> expected, std::span<const RenderCompletion> completed,
    synthesis::PhraseFrameRange output, std::stop_token stopToken) {
  using Output = synthesis::PhraseAudio;
  const auto valid = output.validate();
  if (!valid) return core::Result<Output>{valid.error()};
  if (expected.empty() || expected.size() > 4096U || completed.size() != expected.size()) return core::failure<Output>(
      core::ErrorCode::Conflict, "Chunk completion manifest is empty, incomplete or oversized");
  std::unordered_map<std::string, const RenderSnapshot*> pending;
  const auto group = snapshotGroupId(expected.front());
  for (const auto& snapshot : expected) {
    if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Chunk assembly cancelled");
    if (!snapshot.project || !snapshot.sourceProjectId.valid() || !snapshot.ownedFrames || snapshot.contentHash.empty() ||
        snapshot.revision != expected.front().revision || snapshot.sampleRate != expected.front().sampleRate ||
        snapshotGroupId(snapshot) != group || snapshot.resource.index() != expected.front().resource.index()) {
      return core::failure<Output>(core::ErrorCode::Conflict, "Chunk manifest mixes incompatible source snapshots");
    }
    if (std::holds_alternative<synthesis::ProceduralSingerResource>(snapshot.resource)) {
      const auto supported = validateProceduralSnapshot(snapshot);
      if (!supported) return core::Result<Output>{supported.error()};
    } else if (!std::holds_alternative<synthesis::SampleSingerResource>(snapshot.resource)) {
      return core::failure<Output>(core::ErrorCode::Unsupported, "Chunk manifest resource has no supported backend");
    }
    const auto window = synthesis::PhraseOutputContract{snapshot.sampleRate, output, *snapshot.ownedFrames}.validate();
    if (!window) return core::Result<Output>{window.error()};
    if (!pending.emplace(snapshotJobId(snapshot), &snapshot).second) return core::failure<Output>(
        core::ErrorCode::Conflict, "Chunk manifest contains duplicate job identities");
  }
  std::vector<synthesis::PhraseAudioView> chunks;
  chunks.reserve(completed.size());
  for (const auto& completion : completed) {
    if (stopToken.stop_requested()) return core::failure<Output>(core::ErrorCode::Conflict, "Chunk assembly cancelled");
    const auto found = pending.find(completion.phraseId);
    if (found == pending.end()) return core::failure<Output>(core::ErrorCode::Conflict,
        "Unexpected or duplicate chunk completion");
    const auto& snapshot = *found->second;
    const auto& window = *snapshot.ownedFrames;
    if (std::holds_alternative<synthesis::ProceduralSingerResource>(snapshot.resource)) {
      const auto markers = projectProceduralMarkers(snapshot);
      if (!markers) return core::Result<Output>{markers.error()};
      if (completion.proceduralMarkers != markers.value()) return core::failure<Output>(
          core::ErrorCode::Conflict, "Chunk markers differ from the expected procedural snapshot");
    } else if (!completion.proceduralMarkers.empty()) return core::failure<Output>(
        core::ErrorCode::Conflict, "Sample completion contains procedural markers");
    if ((completion.status != RenderCompletionStatus::Completed && completion.status != RenderCompletionStatus::CacheHit) ||
        completion.revision != snapshot.revision || completion.cacheKey != snapshot.contentHash || !completion.pcm ||
        completion.pcm->sampleRate != snapshot.sampleRate || completion.pcm->startFrame != window.start ||
        completion.pcm->samples.size() != static_cast<std::size_t>(window.end - window.start)) {
      return core::failure<Output>(core::ErrorCode::Conflict, "Chunk completion differs from its expected source or output window");
    }
    chunks.push_back({completion.pcm->startFrame, completion.pcm->samples});
    pending.erase(found);
  }
  return synthesis::assemblePhraseOutput(output, chunks, stopToken);
}

std::string BackgroundRenderScheduler::snapshotJobId(const RenderSnapshot& snapshot) {
  const auto base = "snapshot:" + snapshot.sourceProjectId.toString() + ":" +
      snapshot.trackId.toString() + ":" + snapshot.segment.regionId.toString() + ":" +
      std::to_string(snapshot.segment.id.size()) + ":" + snapshot.segment.id + ":" + std::to_string(snapshot.sampleRate);
  if (!snapshot.ownedFrames) return base + ":full";
  return base + ":owned:" + std::to_string(snapshot.ownedFrames->start) + ":" +
      std::to_string(snapshot.ownedFrames->end);
}

std::string BackgroundRenderScheduler::snapshotGroupId(const RenderSnapshot& snapshot) {
  return snapshot.sourceProjectId.toString() + ":" +
      snapshot.trackId.toString() + ":" + snapshot.segment.regionId.toString();
}

core::Result<void> BackgroundRenderScheduler::invalidateGroup(std::string groupId, std::uint64_t minimumRevision) {
  std::scoped_lock lock{mutex_};
  if (stopping_) return core::failure(core::ErrorCode::Conflict, "Render scheduler is stopping");
  if (groupId.empty() || groupId.size() > 2048U ||
      (!groupRevisions_.contains(groupId) && groupRevisions_.size() >= 65536U)) return core::failure(
      core::ErrorCode::InvalidArgument, "Render revision-group invalidation exceeds bounds");
  advanceGroupLocked(groupId, minimumRevision);
  condition_.notify_all();
  return core::success();
}

void BackgroundRenderScheduler::advanceGroupLocked(const std::string& groupId, std::uint64_t requestedRevision) {
  auto& revision = groupRevisions_[groupId];
  if (requestedRevision <= revision) return;
  revision = requestedRevision;
  std::erase_if(proceduralCheckpoints_, [&](const auto& checkpoint) {
    return checkpoint.group == groupId && checkpoint.revision < revision;
  });
  for (const auto& [job, group] : jobGroups_) {
    if (group != groupId) continue;
    latestRevision_[job] = std::max(latestRevision_[job], revision);
    const auto control = controls_.find(job);
    if (control != controls_.end()) control->second->stop.request_stop();
  }
}

core::Result<void> BackgroundRenderScheduler::submitSnapshot(RenderSnapshot snapshot, RenderPriority priority) {
  if (!snapshot.project || !snapshot.sourceProjectId.valid() || !snapshot.phonemes || snapshot.segment.id.empty()) {
    return core::failure(core::ErrorCode::InvalidArgument, "Scheduled snapshot is incomplete");
  }
  if (std::holds_alternative<synthesis::ProceduralSingerResource>(snapshot.resource)) {
    const auto valid = validateProceduralSnapshot(snapshot);
    if (!valid) return core::Result<void>{valid.error()};
  } else if (const auto* sample = std::get_if<synthesis::SampleSingerResource>(&snapshot.resource)) {
    if (!sample->voicebank || !sample->unitPlan || sample->frozenAudio.size() != sample->unitPlan->entries.size()) {
      return core::failure(core::ErrorCode::InvalidArgument, "Scheduled sample snapshot is incomplete");
    }
  } else return core::failure(core::ErrorCode::Unsupported, "Snapshot scheduler has no backend for this singer resource");
  auto frozen = std::make_shared<const RenderSnapshot>(std::move(snapshot));
  std::uint64_t checkpointEpoch;
  { std::scoped_lock lock{mutex_}; checkpointEpoch = epoch_; }
  auto outputFrames = frozen->ownedFrames;
  if (!outputFrames && std::holds_alternative<synthesis::ProceduralSingerResource>(frozen->resource)) {
    outputFrames = synthesis::PhraseFrameRange{frozen->compiledPerformance->notes().front().startFrame,
        frozen->compiledPerformance->notes().back().endFrame};
  }
  std::vector<voice_design::ProceduralPhoneMarker> markers;
  if (std::holds_alternative<synthesis::ProceduralSingerResource>(frozen->resource)) {
    auto projected = projectProceduralMarkers(*frozen);
    if (!projected) return core::Result<void>{projected.error()};
    markers = std::move(projected).value();
  }
  return submit({.phraseId = snapshotJobId(*frozen), .cacheKey = frozen->contentHash,
      .revision = frozen->revision, .sampleRate = frozen->sampleRate, .priority = priority,
      .task = [this, frozen, checkpointEpoch](std::stop_token token) -> core::Result<synthesis::PhraseAudio> {
        if (std::holds_alternative<synthesis::ProceduralSingerResource>(frozen->resource)) {
          return renderProcedural(*frozen, checkpointEpoch, token);
        }
        auto result = PhraseRenderPipeline{}.render(*frozen, token);
        if (!result) return core::Result<synthesis::PhraseAudio>{result.error()};
        return std::move(result.value().rendered.audio);
      }, .outputFrames = outputFrames,
      .groupId = snapshotGroupId(*frozen), .proceduralMarkers = std::move(markers)});
}

core::Result<synthesis::PhraseAudio> BackgroundRenderScheduler::renderProcedural(
    const RenderSnapshot& snapshot, std::uint64_t epoch, std::stop_token token) {
  const auto start = snapshot.ownedFrames ? snapshot.ownedFrames->start :
      snapshot.compiledPerformance->notes().front().startFrame;
  const auto group = snapshotGroupId(snapshot);
  std::shared_ptr<const ProceduralSnapshotStream> checkpoint;
  {
    std::scoped_lock lock{mutex_};
    if (epoch != epoch_ || token.stop_requested()) return core::failure<synthesis::PhraseAudio>(
        core::ErrorCode::Conflict, "Procedural scheduler session was cancelled");
    for (const auto& entry : proceduralCheckpoints_) {
      if (entry.stream->position() <= start && entry.stream->matchesContext(snapshot) &&
          (!checkpoint || entry.stream->position() > checkpoint->position())) checkpoint = entry.stream;
    }
  }
  // Copies isolate workers; never hold the scheduler mutex during DSP. A
  // missing/late checkpoint simply replays from the frozen context origin.
  auto source = snapshot;
  source.ownedFrames.reset();
  auto stream = checkpoint ? core::Result<ProceduralSnapshotStream>{*checkpoint} :
      ProceduralSnapshotStream::create(std::move(source), token);
  if (!stream) return core::Result<synthesis::PhraseAudio>{stream.error()};
  auto rendered = stream.value().render(snapshot, token);
  if (!rendered) return core::Result<synthesis::PhraseAudio>{rendered.error()};
  auto saved = std::make_shared<const ProceduralSnapshotStream>(std::move(stream).value());
  {
    std::scoped_lock lock{mutex_};
    const auto floor = groupRevisions_.find(group);
    if (epoch == epoch_ && !token.stop_requested() &&
        (floor == groupRevisions_.end() || snapshot.revision >= floor->second)) {
      if (proceduralCheckpoints_.size() >= 16U) proceduralCheckpoints_.erase(proceduralCheckpoints_.begin());
      proceduralCheckpoints_.push_back({group, snapshot.revision, std::move(saved)});
      stats_.proceduralFrames += rendered.value().processedFrames;
    }
  }
  return std::move(rendered.value().audio);
}

bool BackgroundRenderScheduler::staleLocked(const ScheduledRenderRequest& request) const {
  const auto job = latestRevision_.find(request.phraseId);
  const auto group = groupRevisions_.find(request.groupId);
  return (job != latestRevision_.end() && request.revision < job->second) ||
      (!request.groupId.empty() && group != groupRevisions_.end() && request.revision < group->second);
}

core::Result<void> BackgroundRenderScheduler::registerGroupLocked(const ScheduledRenderRequest& request) {
  const auto existing = jobGroups_.find(request.phraseId);
  if (existing != jobGroups_.end() && existing->second != request.groupId) return core::failure(
      core::ErrorCode::Conflict, "A render job cannot move between revision groups");
  if (request.groupId.empty()) return core::success();
  if (request.groupId.size() > 2048U || request.phraseId.size() > 4096U ||
      (!groupRevisions_.contains(request.groupId) && groupRevisions_.size() >= 65536U) ||
      (existing == jobGroups_.end() && jobGroups_.size() >= 65536U)) return core::failure(
      core::ErrorCode::Unsupported, "Render revision-group tracking exceeds bounds");
  advanceGroupLocked(request.groupId, request.revision);
  jobGroups_[request.phraseId] = request.groupId;
  return core::success();
}

BackgroundRenderScheduler::BackgroundRenderScheduler(PcmCache& cache,
                                                       std::size_t workerCount,
                                                       RenderSchedulerHooks hooks)
    : cache_(cache), hooks_(std::move(hooks)) {
  workerCount = std::clamp<std::size_t>(workerCount, 1U, 16U);
  workers_.reserve(workerCount);
  for (std::size_t index = 0; index < workerCount; ++index) {
    workers_.emplace_back([this](std::stop_token token) { workerLoop(token); });
  }
}

BackgroundRenderScheduler::~BackgroundRenderScheduler() {
  {
    std::scoped_lock lock{mutex_};
    stopping_ = true;
    for (auto& [phrase, control] : controls_) {
      static_cast<void>(phrase);
      control->stop.request_stop();
    }
  }
  for (auto& worker : workers_) worker.request_stop();
  condition_.notify_all();
  workers_.clear();  // Join while all scheduler/checkpoint state is still alive.
}

core::Result<void> BackgroundRenderScheduler::reset() {
  std::scoped_lock lock{mutex_};
  if (stopping_ || epoch_ == std::numeric_limits<std::uint64_t>::max()) return core::failure(
      core::ErrorCode::Conflict, "Render scheduler cannot reset its session");
  ++epoch_;
  for (const auto& [job, control] : controls_) {
    static_cast<void>(job);
    control->stop.request_stop();
  }
  stats_.cancelled += jobs_.size();
  while (!jobs_.empty()) jobs_.pop();
  for (const auto& completion : completions_) {
    if (completion.status == RenderCompletionStatus::Completed) { --stats_.completed; ++stats_.cancelled; }
    else if (completion.status == RenderCompletionStatus::CacheHit) { --stats_.cacheHits; ++stats_.cancelled; }
  }
  completions_.clear();
  controls_.clear();
  latestRevision_.clear();
  groupRevisions_.clear();
  jobGroups_.clear();
  proceduralCheckpoints_.clear();
  condition_.notify_all();
  if (activeWorkers_ == 0U) idleCondition_.notify_all();
  return core::success();
}

core::Result<void> BackgroundRenderScheduler::submit(
    ScheduledRenderRequest request) {
  if (request.proceduralMarkers.size() > 16384U) return core::failure(core::ErrorCode::InvalidArgument, "Procedural marker count exceeds bounds");
  std::optional<time::SampleFrame> previousEnd;
  for (const auto& marker : request.proceduralMarkers) {
    if (!request.outputFrames || !marker.ownedSpan.validate() || !marker.key.noteId.valid() ||
        marker.ownedSpan.start < request.outputFrames->start || marker.ownedSpan.end > request.outputFrames->end ||
        (previousEnd && marker.ownedSpan.start < *previousEnd) ||
        (marker.kind != voice_design::ProceduralGestureKind::OralVowel && marker.kind != voice_design::ProceduralGestureKind::Frication && marker.kind != voice_design::ProceduralGestureKind::Nasal && marker.kind != voice_design::ProceduralGestureKind::Plosive && marker.kind != voice_design::ProceduralGestureKind::VoicedFrication) ||
        marker.key.ordinal >= 16384U || marker.phone.empty() || marker.phone.size() > 128U ||
        std::any_of(marker.phone.begin(), marker.phone.end(), [](unsigned char value) { return value < 0x20U || value == 0x7fU; })) {
      return core::failure(core::ErrorCode::InvalidArgument, "Procedural markers do not fit the requested output");
    }
    previousEnd = marker.ownedSpan.end;
  }
  if (request.outputFrames) {
    const auto valid = synthesis::PhraseOutputContract{request.sampleRate, *request.outputFrames, *request.outputFrames}.validate();
    if (!valid) return valid;
  }
  if (request.phraseId.empty() || request.cacheKey.empty() || request.task == nullptr ||
      request.sampleRate < 8000 || request.sampleRate > 384000) {
    return core::failure(core::ErrorCode::InvalidArgument,
                         "Render scheduler request is invalid");
  }

  std::uint64_t submissionEpoch = 0U;
  {
    std::scoped_lock lock{mutex_};
    if (stopping_) {
      return core::failure(core::ErrorCode::Conflict,
                           "Render scheduler is stopping");
    }
    submissionEpoch = epoch_;
    if (staleLocked(request)) {
      ++stats_.submitted;
      ++stats_.stale;
      completions_.push_back(RenderCompletion{
          .phraseId = request.phraseId,
          .cacheKey = request.cacheKey,
          .revision = request.revision,
          .status = RenderCompletionStatus::Stale,
          .pcm = nullptr,
          .error = {},
      });
      idleCondition_.notify_all();
      return core::success();
    }
  }

  auto cached = cache_.load(request.cacheKey);
  if (cached) {
    if (request.outputFrames && (cached.value()->startFrame != request.outputFrames->start ||
        cached.value()->samples.size() != static_cast<std::size_t>(request.outputFrames->end - request.outputFrames->start))) {
      return core::failure(core::ErrorCode::Conflict, "Cached PCM does not match the requested owned window");
    }
    if (cached.value()->sampleRate != request.sampleRate) {
      return core::failure(
          core::ErrorCode::Conflict,
          "PCM cache entry sample rate does not match the render request",
          request.cacheKey);
    }
    std::scoped_lock lock{mutex_};
    if (submissionEpoch != epoch_) return core::failure(core::ErrorCode::Conflict,
        "Render request belongs to a reset scheduler session");
    if (stopping_) {
      return core::failure(core::ErrorCode::Conflict,
                           "Render scheduler is stopping");
    }
    const bool stale = staleLocked(request);
    if (!stale) {
      const auto registered = registerGroupLocked(request);
      if (!registered) return registered;
    }
    ++stats_.submitted;
    if (!stale) {
      if (const auto existing = controls_.find(request.phraseId);
          existing != controls_.end()) {
        existing->second->stop.request_stop();
      }
      latestRevision_[request.phraseId] = request.revision;
      ++stats_.cacheHits;
    } else {
      ++stats_.stale;
    }
    completions_.push_back(RenderCompletion{
        .phraseId = std::move(request.phraseId),
        .cacheKey = std::move(request.cacheKey),
        .revision = request.revision,
        .status = stale ? RenderCompletionStatus::Stale
                        : RenderCompletionStatus::CacheHit,
        .pcm = stale ? nullptr : cached.value(),
        .error = {},
        .proceduralMarkers = stale ? std::vector<voice_design::ProceduralPhoneMarker>{} : std::move(request.proceduralMarkers),
    });
    idleCondition_.notify_all();
    return core::success();
  }
  if (cached.error().code != core::ErrorCode::NotFound) {
    return core::Result<void>{cached.error()};
  }

  auto control = std::make_shared<JobControl>();
  control->epoch = submissionEpoch;
  {
    std::scoped_lock lock{mutex_};
    if (submissionEpoch != epoch_) return core::failure(core::ErrorCode::Conflict,
        "Render request belongs to a reset scheduler session");
    if (stopping_) {
      return core::failure(core::ErrorCode::Conflict,
                           "Render scheduler is stopping");
    }
    if (staleLocked(request)) {
      ++stats_.submitted;
      ++stats_.stale;
      completions_.push_back(RenderCompletion{
          .phraseId = request.phraseId,
          .cacheKey = request.cacheKey,
          .revision = request.revision,
          .status = RenderCompletionStatus::Stale,
          .pcm = nullptr,
          .error = {},
      });
      idleCondition_.notify_all();
      return core::success();
    }
    const auto registered = registerGroupLocked(request);
    if (!registered) return registered;
    const auto existing = controls_.find(request.phraseId);
    if (existing != controls_.end()) existing->second->stop.request_stop();
    latestRevision_[request.phraseId] = std::max(
        latestRevision_[request.phraseId], request.revision);
    controls_[request.phraseId] = control;
    jobs_.push(Job{
        .request = std::move(request),
        .sequence = nextSequence_++,
        .control = std::move(control),
    });
    ++stats_.submitted;
  }
  condition_.notify_one();
  return core::success();
}

void BackgroundRenderScheduler::cancelPhrase(std::string_view phraseId) {
  std::scoped_lock lock{mutex_};
  const auto iterator = controls_.find(std::string{phraseId});
  if (iterator != controls_.end()) iterator->second->stop.request_stop();
}

void BackgroundRenderScheduler::workerLoop(std::stop_token token) {
  while (!token.stop_requested()) {
    Job job;
    {
      std::unique_lock lock{mutex_};
      condition_.wait(lock, [this, &token] {
        return stopping_ || token.stop_requested() || !jobs_.empty();
      });
      if (stopping_ || token.stop_requested()) return;
      job = jobs_.top();
      jobs_.pop();
      ++activeWorkers_;
    }

    RenderCompletion completion{
        .phraseId = job.request.phraseId,
        .cacheKey = job.request.cacheKey,
        .revision = job.request.revision,
        .status = RenderCompletionStatus::Failed,
        .pcm = nullptr,
        .error = {},
    };
    if (job.control->stop.stop_requested()) {
      completion.status = RenderCompletionStatus::Cancelled;
    } else {
      try {
        auto rendered = job.request.task(job.control->stop.get_token());
        if (!rendered) {
          completion.error = rendered.error();
          completion.status = job.control->stop.stop_requested()
              ? RenderCompletionStatus::Cancelled
              : RenderCompletionStatus::Failed;
        } else if (job.control->stop.stop_requested()) {
          completion.status = RenderCompletionStatus::Cancelled;
        } else if (job.request.outputFrames &&
            (rendered.value().startFrame != job.request.outputFrames->start ||
             rendered.value().samples.size() != static_cast<std::size_t>(
                 job.request.outputFrames->end - job.request.outputFrames->start))) {
          completion.status = RenderCompletionStatus::Failed;
          completion.error = core::Error{core::ErrorCode::Conflict,
              "Backend PCM does not match the requested owned window", job.request.phraseId};
        } else {
          bool stale = false;
          {
            std::scoped_lock lock{mutex_};
            const auto latest = latestRevision_.find(job.request.phraseId);
            stale = latest != latestRevision_.end() &&
                    job.request.revision < latest->second;
          }
          if (stale) {
            completion.status = RenderCompletionStatus::Stale;
          } else {
            CachedPcm pcm{
                .sampleRate = job.request.sampleRate,
                .startFrame = rendered.value().startFrame,
                .samples = std::move(rendered).value().samples,
            };
            auto stored = cache_.store(job.request.cacheKey, pcm);
            if (!stored) {
              completion.status = RenderCompletionStatus::Failed;
              completion.error = stored.error();
            } else {
              auto loaded = cache_.load(job.request.cacheKey);
              if (!loaded) {
                completion.status = RenderCompletionStatus::Failed;
                completion.error = loaded.error();
              } else {
                completion.status = RenderCompletionStatus::Completed;
                completion.pcm = loaded.value();
                completion.proceduralMarkers = std::move(job.request.proceduralMarkers);
              }
            }
          }
        }
      } catch (const std::exception& exception) {
        completion.status = RenderCompletionStatus::Failed;
        completion.error = core::Error{core::ErrorCode::Internal,
                                       "Render task threw an exception",
                                       exception.what()};
      } catch (...) {
        completion.status = RenderCompletionStatus::Failed;
        completion.error = core::Error{core::ErrorCode::Internal,
                                       "Render task threw an unknown exception", {}};
      }
    }
    if ((completion.status == RenderCompletionStatus::Completed ||
         completion.status == RenderCompletionStatus::CacheHit) &&
        hooks_.beforeFinalPublish) {
      hooks_.beforeFinalPublish();
    }
    if (completion.status == RenderCompletionStatus::Completed ||
        completion.status == RenderCompletionStatus::CacheHit) {
      std::scoped_lock lock{mutex_};
      const auto latest = latestRevision_.find(job.request.phraseId);
      if (latest != latestRevision_.end() &&
          job.request.revision < latest->second) {
        completion.status = RenderCompletionStatus::Stale;
        completion.pcm.reset();
        completion.proceduralMarkers.clear();
      } else if (job.control->stop.stop_requested()) {
        completion.status = RenderCompletionStatus::Cancelled;
        completion.pcm.reset();
        completion.proceduralMarkers.clear();
      }
    }
    pushCompletion(std::move(completion), job.control->epoch);
    {
      std::scoped_lock lock{mutex_};
      if (activeWorkers_ > 0) --activeWorkers_;
      const auto control = controls_.find(job.request.phraseId);
      if (control != controls_.end() && control->second == job.control) {
        controls_.erase(control);
      }
      if (jobs_.empty() && activeWorkers_ == 0) idleCondition_.notify_all();
    }
  }
}

void BackgroundRenderScheduler::pushCompletion(RenderCompletion completion, std::uint64_t epoch) {
  std::scoped_lock lock{mutex_};
  if (epoch != epoch_) { ++stats_.cancelled; return; }
  if (completion.status == RenderCompletionStatus::Completed ||
      completion.status == RenderCompletionStatus::CacheHit) {
    const auto latest = latestRevision_.find(completion.phraseId);
    if (latest != latestRevision_.end() && completion.revision < latest->second) {
      completion.status = RenderCompletionStatus::Stale;
      completion.pcm.reset();
      completion.error = {};
      completion.proceduralMarkers.clear();
    }
  }
  switch (completion.status) {
    case RenderCompletionStatus::Completed: ++stats_.completed; break;
    case RenderCompletionStatus::CacheHit: ++stats_.cacheHits; break;
    case RenderCompletionStatus::Cancelled: ++stats_.cancelled; break;
    case RenderCompletionStatus::Stale: ++stats_.stale; break;
    case RenderCompletionStatus::Failed: ++stats_.failed; break;
  }
  completions_.push_back(std::move(completion));
}

std::vector<RenderCompletion> BackgroundRenderScheduler::drainCompleted() {
  std::scoped_lock lock{mutex_};
  for (auto& completion : completions_) {
    const auto latest = latestRevision_.find(completion.phraseId);
    if (latest != latestRevision_.end() && completion.revision < latest->second &&
        (completion.status == RenderCompletionStatus::Completed || completion.status == RenderCompletionStatus::CacheHit)) {
      if (completion.status == RenderCompletionStatus::Completed) --stats_.completed;
      else --stats_.cacheHits;
      ++stats_.stale;
      completion.status = RenderCompletionStatus::Stale;
      completion.pcm.reset();
      completion.proceduralMarkers.clear();
    }
  }
  std::vector<RenderCompletion> result;
  result.swap(completions_);
  return result;
}

bool BackgroundRenderScheduler::waitIdle(std::chrono::milliseconds timeout) {
  std::unique_lock lock{mutex_};
  return idleCondition_.wait_for(lock, timeout, [this] {
    return jobs_.empty() && activeWorkers_ == 0;
  });
}

RenderSchedulerStats BackgroundRenderScheduler::stats() const {
  std::scoped_lock lock{mutex_};
  return stats_;
}

}  // namespace seam::rendering
