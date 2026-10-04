// The per-mode views the replacement panel shows, one builder each.
//
// replacementReviewView was a single 433-line method that built seven overlays in one body: the
// Japanese reading review, the style coverage sheet, the dynamics draft inspector, the vibrato
// draft inspector, the find review, the clear-dynamics review, the note-cleanup review and the
// clear-vibrato review. Each was already a self-contained branch that ended in a return, and each
// is now its own method.
//
// The bodies below are the branch bodies unchanged. What changed is where the boundary between them
// sits: each builder receives the view the dispatcher has already started -- visible, with action 4
// (Cancel) enabled -- fills in what its own overlay needs, and returns it. That is why every builder
// takes a ReplacementReviewView rather than making one: the shared prologue has to be established
// before the first branch tests anything.
//
// The replacement review itself stays in the dispatcher as its fall-through, and so does the detail
// page it shows, because both read the asynchronous job handle the dispatcher owns. The dispatch
// order is unchanged from before the split; the dispatcher records why that is preserved even though
// no test can currently observe it, because the mode flags are members and the exclusivity between
// them comes from each open() rather than from here.

#include "seam/native_ui/editor_controller.hpp"

#include "seam/application/view_commands.hpp"

#include <cstddef>
#include <string>

namespace seam::native_ui {

ReplacementReviewView NativeEditorController::japaneseReadingReviewView(ReplacementReviewView view) const {
    view.dockedInspector = true;
    const auto* review = japaneseReadingIdentity_ ? japaneseReadingJob_.current(session_, regionId_, *japaneseReadingIdentity_) : nullptr;
    const bool current = review != nullptr;
    view.status = japaneseReadingJob_.state() == authoring::JapaneseReadingJob::State::Preparing ?
        "Preparing Japanese contextual reading…" : current ? "Japanese dictionary reading — review before Apply" :
        replacementError_.empty() ? "Japanese reading is unavailable" : replacementError_;
    view.labels[0] = "Previous"; view.labels[1] = "Next"; view.labels[2] = japaneseReadingDetail_ ? "Back to readings" : "Inspect first";
    view.labels[3] = "Apply reading"; view.labels[4] = "Cancel"; view.labels[5] = "Retry";
    if (japaneseReadingDetail_) {
      const auto count = japaneseReadingDetailLines_.size(); const auto offset = replacementPage_ * 6U;
      view.summary = "Reading detail " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (count + 5U) / 6U)) +
          " / read-only";
      for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) view.rows.push_back(japaneseReadingDetailLines_[i]);
      view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count; view.enabled[2] = true; view.enabled[3] = false; view.enabled[5] = false;
      return view;
    }
    if (current) {
      const auto& tokens = review->reading.tokens; const auto offset = replacementPage_ * 6U;
      view.summary = std::to_string(tokens.size()) + " dictionary tokens / " + std::to_string(review->reading.tokens.size()) + " source spans";
      for (std::size_t i = offset; i < std::min(offset + 6U, tokens.size()); ++i) {
        const auto& token = tokens[i]; const auto& binding = review->bindings[i];
        std::string row = token.surface + " → " + (token.pronunciation ? *token.pronunciation : "(unresolved)") +
            " / owners " + std::to_string(binding.notes.size());
        if (binding.crossesLyrics) row += " / cross-lyric";
        if (binding.touchesExplicitHint) row += " / explicit hint";
        view.rows.push_back(std::move(row));
      }
      view.rowsInspectable = true; view.enabled[2] = !tokens.empty();
      const bool canApply = japaneseReadingIdentity_ && japaneseReadingJob_.canApplyCurrent(
          session_, regionId_, *japaneseReadingIdentity_);
      view.enabled[3] = canApply;
      view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < tokens.size(); view.enabled[5] = false;
    } else {
      view.summary = replacementError_.empty() ? "Waiting for the reading worker" : replacementError_;
      view.rows.push_back(replacementError_.empty() ? "No reading result yet" : "Open Diagnostics for the complete error");
      view.enabled[2] = false; view.enabled[3] = false; view.enabled[5] = japaneseReadingJob_.state() != authoring::JapaneseReadingJob::State::Preparing;
    }
    return view;
}

ReplacementReviewView NativeEditorController::styleCoverageReviewView(ReplacementReviewView view) const {
    view.dockedInspector = true;
    const bool current = styleSourceCurrent();
    view.status = current ? "Style / structural coverage — draft only" : "Source changed; Refresh or Cancel";
    view.summary = styleDraft_->diagnostic();
    if (const auto& report = styleDraft_->coverage()) view.summary = std::to_string(report->summary.coveredPhonemes) + "/" +
        std::to_string(report->summary.totalPhonemes) + " phones covered; not audio QA";
    const auto& styles = styleDraft_->styles(); const auto offset = replacementPage_ * 6U;
    for (std::size_t i = offset; i < std::min(offset + 6U, styles.size()); ++i)
      view.rows.push_back(std::string(styles[i].id == styleDraft_->selection().styleId ? "Selected: " : "Choose: ") + styles[i].id +
          " / enabled " + std::to_string(styles[i].enabled) + " / disabled " + std::to_string(styles[i].disabled));
    if (offset <= styles.size() && styles.size() < offset + 6U) view.rows.push_back("Configure PCM style crossfade");
    view.rowsInspectable = current;
    view.labels[2] = "Draft only"; view.labels[3] = "Apply track style"; view.labels[4] = "Cancel draft"; view.labels[5] = "Refresh";
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < styles.size() + 1U;
    view.enabled[3] = current && styleDraft_->hasChanges() && !styleDraft_->selection().styleId.empty(); view.enabled[5] = true;
    view.labels[2] = "Coverage details"; view.enabled[2] = true;
    if (styleBlendMode_) {
      view.status = current ? "PCM style crossfade — draft only" : "Source changed; Refresh or Cancel";
      view.labels[2] = styleBlendChoosingSecondary_ ? "Back to pair" : "Back to styles";
      view.summary = "Render checks both styles' timing and local cancellation; not voice-morph or listening approval";
      if (styleBlendChoosingSecondary_) {
        view.rows.clear();
        for (std::size_t i = offset; i < std::min(offset + 6U, styles.size()); ++i)
          view.rows.push_back("Secondary: " + styles[i].id);
        view.enabled[1] = offset + 6U < styles.size();
      } else {
        const auto& selection = styleDraft_->selection();
        const auto percent = selection.blend ? std::to_string(static_cast<int>(std::lround(selection.blend->amount * 100.0F))) : "0";
        view.rows = {"Primary: " + selection.styleId + " (back to styles to change)",
            "Choose secondary: " + (selection.blend ? selection.blend->targetStyleId : std::string{"none"}),
            "Decrease by 5% (current " + percent + "%)", "Increase by 5% (current " + percent + "%)", "Disable crossfade pair"};
        view.enabled[0] = false; view.enabled[1] = false;
        if (const auto& secondary = styleDraft_->secondaryCoverage()) view.summary =
            "Secondary coverage " + std::to_string(secondary->summary.coveredPhonemes) + "/" +
            std::to_string(secondary->summary.totalPhonemes) + "; timing and phase checked when rendered, not audio approval";
      }
      return view;
    }
    if (styleIssues_) {
      const auto& report = styleDraft_->coverage();
      view.rows.clear(); view.rowsInspectable = current && !styleIssue_;
      view.labels[2] = styleIssue_ ? "Back to issues" : "Back to styles";
      const auto count = styleIssue_ ? styleDetailLines_.size() : report ? report->issues.size() : 1U;
      view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count;
      view.status = current ? (styleIssue_ ? "Coverage issue details — read only" : "Coverage issues — read only") : "Source changed; Refresh or Cancel";
      view.summary = "Page " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1}, (count + 5U) / 6U)) +
          (report ? " / structural coverage, not audio QA" : " / coverage unavailable");
      for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) {
        if (styleIssue_) view.rows.push_back(styleDetailLines_[i]);
        else if (!report) view.rows.push_back("Inspect why coverage is unavailable");
        else view.rows.push_back(std::string(voicebank::coverageIssueKindName(report->issues[i].kind)) + ": " + report->issues[i].symbol);
      }
      if (!styleIssue_ && report && report->issues.empty()) view.summary = "No structural issues; audio QA still required";
    }
    return view;
}

ReplacementReviewView NativeEditorController::dynamicsDraftReviewView(ReplacementReviewView view) const {
    view.dockedInspector = true;
    const bool current = dynamicsDraft_->matches(session_, regionId_);
    const auto influence = dynamicsDraft_->influence();
    view.status = !current ? "Source changed; Refresh or Cancel" : influence.generatedSelections == 0U
        ? "Region dynamics — native curve draft"
        : "Region: " + std::to_string(influence.generatedSelections) + " generated / " +
          std::to_string(influence.manualReplacementScopes) + " native Replace scopes";
    view.summary = replacementError_.empty() ? (influence.generatedSelections > 0U
        ? "Generated dynamics may override this curve"
        : "ENTIRE region / no generated dynamics selected") : replacementError_;
    view.labels[4] = "Cancel draft";
    ReplacementReviewView::DynamicsPlot plot;
    plot.bounds = layout_.dynamicsPlotBounds(logicalWidth_, logicalHeight_); plot.editable = current;
    plot.influenceDescription = std::to_string(influence.generatedSelections) + " accepted dynamics selections; " +
        std::to_string(influence.manualReplacementScopes) + " manual dynamics replacement scopes. " +
        "Non-null generated dynamics values override the native curve inside accepted scopes unless a manual Dynamics Replace scope applies. " +
        "A native curve edit does not claim ownership or remove generated selections. Scope counts are not rendered coverage. " +
        "Articulation, track gain and renderer processing may further change the audible level.";
    const auto* region = session_.project().findRegion(regionId_);
    plot.endTick = region ? std::max(std::int64_t{1}, region->durationTick.value()) : 1;
    for (const auto* curve : {&dynamicsDraft_->sourceCurve(), &dynamicsDraft_->curve()})
      if (!curve->points().empty()) plot.endTick = std::max(plot.endTick, curve->points().back().tick.value());
    const auto candidate = dynamicsPointValue();
    if (candidate) plot.endTick = std::max(plot.endTick, candidate.value().tick.value());
    plot.fullEndTick = plot.endTick;
    const auto visible = dynamicsViewport_.resolve(plot.fullEndTick); plot.startTick = visible.start; plot.endTick = visible.end;
    const auto navigation = layout_.reviewRowBounds(logicalWidth_, logicalHeight_, 2U, true);
    const auto navWidth = (navigation.width - 12.0) / 4.0;
    for (std::size_t i = 0U; i < 4U; ++i) plot.navigation[i] = {navigation.x + static_cast<double>(i) * (navWidth + 4.0), navigation.y, navWidth, navigation.height};
    const auto inView = [&](time::Tick tick) { return tick.value() >= plot.startTick && tick.value() <= plot.endTick; };
    const auto position = [&](domain::DynamicsAutomationPoint point) {
      return ui::Point{plot.bounds.x + plot.bounds.width * (static_cast<double>(point.tick.value() - plot.startTick) / static_cast<double>(plot.endTick - plot.startTick)),
          plot.bounds.y + plot.bounds.height * (1.0 - static_cast<double>(point.linearGain) / domain::kMaximumDynamicsGain)};
    };
    const auto line = [&](const domain::DynamicsAutomation& curve) {
      std::vector<ui::Point> points; points.reserve(curve.points().size() + 2U);
      points.push_back(position({time::Tick{plot.startTick}, curve.valueAt(time::Tick{plot.startTick})}));
      for (const auto& point : curve.points()) if (inView(point.tick)) points.push_back(position(point));
      points.push_back(position({time::Tick{plot.endTick}, curve.valueAt(time::Tick{plot.endTick})})); return points;
    };
    plot.score = line(dynamicsDraft_->sourceCurve()); plot.draft = line(dynamicsDraft_->curve());
    plot.targetStatus = dynamicsDraft_->targetReady() ? "Cyan dots: sampled staged score dynamics. Orange marks: selected generated dynamics before manual replacement. Per voice; not measured audio or final amplitude. Missing generated marks mean no selected non-null generated value."
        : "Target unavailable: " + dynamicsDraft_->targetError();
    if (current && dynamicsDraft_->targetReady()) for (const auto& sample : dynamicsDraft_->targetSamples()) {
      if (!inView(sample.tick)) continue;
      plot.target.push_back(position({sample.tick, sample.linearGain}));
      if (sample.selectedGeneratedGain) plot.selectedGenerated.push_back(position({sample.tick, *sample.selectedGeneratedGain}));
    }
    if (!dynamicsDraft_->targetReady() && replacementError_.empty()) view.summary = dynamicsDraft_->targetError().empty()
        ? "Target unavailable; native editing remains available" : dynamicsDraft_->targetError();
    const auto& draftPoints = dynamicsDraft_->curve().points();
    if (!dynamicsPointEdit_) for (std::size_t i = replacementPage_ * 2U; i < std::min(replacementPage_ * 2U + 2U, draftPoints.size()); ++i)
      if (inView(draftPoints[i].tick)) plot.handles.push_back({position(draftPoints[i]), i % 2U});
    if (candidate && inView(candidate.value().tick)) plot.candidate = position(candidate.value());
    plot.measurementAvailable = measurementCoordinator_ != nullptr; plot.measuredMode = measuredChannel_ != 0U;
    plot.measuredChannel = std::max(std::size_t{1U}, measuredChannel_);
    if (plot.measuredMode) {
      plot.handles.clear(); plot.candidate.reset(); plot.measurementLabel = measurementStatus_;
      if (current) view.status = "Measured rendered output (read-only)";
      view.summary = measurementStatus_;
      const auto measurementAudio = measurementCoordinator_ ? measurementCoordinator_->acquireCurrent()
          : authoring::RealtimeProjectAudioPublication::ReadHandle{};
      const auto* measured = measurementCoordinator_ && current ? measurementJob_.current(session_, *measurementCoordinator_) : nullptr;
      if (measured && measurementWindow_ && measurementAudio && measurementAudio->sourceIdentity == measurementWindow_->identity &&
          measurementAudio->requestId == measurementWindow_->requestId && measured->firstFrame == measurementWindow_->first &&
          measured->frameCount == measurementWindow_->count && measured->channelCount > 0U) {
        plot.measuredChannel = std::min(measuredChannel_, static_cast<std::size_t>(measured->channelCount));
        const auto channel = plot.measuredChannel - 1U;
        double squares = 0.0, peak = 0.0; std::size_t fullScale = 0U;
        for (const auto& bin : measured->bins) {
          const auto& level = bin.channels[channel];
          if (const auto db = level.rmsDbfs()) plot.measuredCeilingDb = std::max(plot.measuredCeilingDb, std::ceil(*db / 6.0) * 6.0);
          squares += level.rms * level.rms * static_cast<double>(bin.frameCount); peak = std::max(peak, level.peak);
          fullScale += level.atOrAboveFullScale;
        }
        const auto dbText = [](double amplitude) {
          if (amplitude == 0.0) return std::string{"silence"};
          std::array<char, 32> buffer{};
          const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), 20.0 * std::log10(amplitude), std::chars_format::fixed, 2);
          return std::string(buffer.data(), result.ptr);
        };
        view.summary = "RMS " + dbText(std::sqrt(squares / static_cast<double>(measured->frameCount))) +
            " / peak " + dbText(peak) + " dBFS / FS " + std::to_string(fullScale);
        const auto* audio = measurementAudio.get();
        if (audio) {
          plot.measurementLabel = "Rendered output ch" + std::to_string(plot.measuredChannel) + " RMS dBFS / " +
              (audio->quality == rendering::RenderQuality::Final ? "Final" : "Preview") + " / rev " + std::to_string(audio->projectRevision);
          for (const auto& bin : measured->bins) {
            const auto frame = bin.firstFrame + (bin.frameCount - 1U) / 2U;
            const auto tick = session_.project().tempoMap().tickAtSampleFrame(static_cast<time::SampleFrame>(frame), audio->result.sampleRate) - region->startTick;
            if (!inView(tick)) continue;
            const auto db = bin.channels[channel].rmsDbfs().value_or(-96.0);
            plot.measured.push_back({plot.bounds.x + plot.bounds.width * static_cast<double>(tick.value() - plot.startTick) / static_cast<double>(plot.endTick - plot.startTick),
                plot.bounds.y + plot.bounds.height * (1.0 - (std::clamp(db, -96.0, plot.measuredCeilingDb) + 96.0) / (plot.measuredCeilingDb + 96.0))});
          }
        }
      }
    }
    if (plot.bounds.width >= 16.0 && plot.bounds.height >= 16.0) view.dynamicsPlot = std::move(plot);
    if (dynamicsPointEdit_) {
      if (current) view.status = measuredChannel_ != 0U ? "Point fields / read-only measured plot" : "Point draft: drag gain / Shift-drag time";
      view.rows = {"Region tick: " + dynamicsPointEdit_->tickText, "Linear gain: " + dynamicsPointEdit_->gainText};
      view.rowsInspectable = current;
      view.labels[2] = "Delete point"; view.enabled[2] = current && dynamicsPointEdit_->source.has_value();
      view.labels[3] = "Save to draft"; view.enabled[3] = current && static_cast<bool>(dynamicsPointValue());
      view.labels[5] = "Back to curve"; view.enabled[5] = true;
      return view;
    }
    const auto& points = dynamicsDraft_->curve().points(); const auto offset = replacementPage_ * 2U;
    for (std::size_t i = offset; i < std::min(offset + 2U, points.size()); ++i) {
      std::array<char, 32> buffer{}; const auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), points[i].linearGain);
      view.rows.push_back("Tick " + std::to_string(points[i].tick.value()) + " / gain " + std::string(buffer.data(), formatted.ptr));
    }
    view.rowsInspectable = current && !points.empty();
    if (points.empty()) view.rows.push_back("No native points: unity gain (1)");
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 2U < points.size();
    view.labels[2] = "Add point"; view.enabled[2] = current && points.size() < domain::kMaximumDynamicsPoints;
    view.labels[3] = "Apply region curve"; view.enabled[3] = current && dynamicsDraft_->hasChanges();
    view.labels[5] = "Refresh / reset draft"; view.enabled[5] = true;
    return view;
}

ReplacementReviewView NativeEditorController::vibratoDraftReviewView(ReplacementReviewView view) const {
    view.dockedInspector = true;
    const bool current = vibratoDraft_->current(session_, regionId_);
    view.status = current ? "Vibrato inspector — draft only" : "Selection changed; Refresh or Cancel";
    const auto error = replacementError_.empty() ? vibratoDraft_->error() : replacementError_;
    view.summary = error.empty() ? std::to_string(vibratoDraft_->selectedCount()) + " selected / " + std::to_string(vibratoDraft_->changedCount()) + " changes / " +
        std::string{VibratoInspectorDraft::label(selectedVibratoField_)} + " / page " + std::to_string(replacementPage_ + 1U) + "/2" : error;
    const auto offset = replacementPage_ * 6U;
    for (std::size_t i = offset; i < std::min(offset + 6U, static_cast<std::size_t>(VibratoField::Count)); ++i) {
      const auto field = static_cast<VibratoField>(i);
      view.rows.push_back(std::string{VibratoInspectorDraft::label(field)} + ": " + vibratoDraft_->text(field));
    }
    view.rowsInspectable = current; view.labels[2] = "Reset selected field"; view.labels[3] = "Apply to Selection"; view.labels[4] = "Cancel draft";
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < static_cast<std::size_t>(VibratoField::Count);
    view.enabled[2] = current; view.enabled[3] = error.empty() && vibratoDraft_->canApply(session_, regionId_); view.enabled[5] = true;
    return view;
}

ReplacementReviewView NativeEditorController::findReviewView(ReplacementReviewView view) const {
    if (findPreparing()) {
      view.status = "Preparing Find results...";
      view.summary = "Search runs on a private snapshot; Close Find cancels";
      view.labels[2] = "Search field"; view.labels[3] = "Inspect result";
      view.labels[4] = "Close Find";
      return view;
    }
    if (diagnosticFindMode_) {
      const bool current = diagnosticFindReview_ && diagnosticFindReview_->matches(session_, diagnosticPanel_);
      const auto count = diagnosticFindReview_ ? diagnosticFindReview_->snapshot().hits().size() : 0U;
      view.status = replacementError_.empty() ? (current ? "Find: Active diagnostics" : "Diagnostic results changed. Refresh first.") : replacementError_;
      view.summary = std::to_string(count) + " matching diagnostics / no automatic recovery actions";
      view.labels[2] = "Search lyrics"; view.labels[3] = "Inspect first result"; view.labels[4] = "Close Find";
      view.enabled[2] = session_.project().findRegion(regionId_) != nullptr; view.enabled[5] = true;
      if (replacementDetail_ && findDetailIndex_) {
        const auto& detail = *replacementDetail_; const auto& lines = detail.lines[0]; const auto offset = detail.page * 6U;
        view.summary = "Diagnostic " + std::to_string(*findDetailIndex_ + 1U) + "/" + std::to_string(count) +
            " / page " + std::to_string(detail.page + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (lines.size() + 5U) / 6U));
        for (std::size_t i = offset; i < std::min(offset + 6U, lines.size()); ++i)
          view.rows.push_back(detail.text[0].substr(lines[i].offset, lines[i].length));
        view.enabled[0] = detail.page > 0U; view.enabled[1] = offset + 6U < lines.size();
        view.enabled[2] = false; view.labels[3] = "Read-only diagnostic"; view.labels[4] = "Back to results";
      } else {
        const auto offset = replacementPage_ * 6U;
        view.summary += " / " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (count + 5U) / 6U));
        for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) {
          const auto& snapshot = diagnosticFindReview_->snapshot(); const auto& hit = snapshot.hits()[i];
          view.rows.push_back(snapshot.source()[hit.sourceIndex].code + " / " + domain::toUtf8(hit.matchedText));
        }
        view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count;
        view.rowsInspectable = current && count > 0U; view.enabled[3] = current && offset < count;
        if (count == 0U && replacementError_.empty()) view.rows.push_back("No matching active diagnostics");
      }
      return view;
    }
    constexpr std::array<const char*, 5U> fields{"Lyrics", "Hints", "Note IDs", "Resolved phones (Japanese)", "Pronunciation warnings (Japanese)"};
    const auto fieldIndex = static_cast<std::size_t>(findField_);
    const std::string fieldName = fieldIndex < fields.size() ? fields[fieldIndex] : "Invalid field";
    const bool current = findNavigation_ && findNavigation_->matches(session_, regionId_);
    const auto count = findNavigation_ ? findNavigation_->result().hits.size() : 0U;
    view.status = replacementError_.empty() ? (current ? "Find: " + fieldName : "Find results changed. Refresh first.") : replacementError_;
    view.summary = std::to_string(count) + " matching notes / " + fieldName;
    view.labels[2] = "Next search field"; view.labels[3] = "Inspect first result";
    view.labels[4] = "Close Find";
    view.enabled[2] = regionId_ == replacementRegion_;
    view.enabled[5] = regionId_ == replacementRegion_;
    if (replacementDetail_ && findDetailIndex_) {
      const auto& detail = *replacementDetail_; const auto& lines = detail.lines[0];
      const auto offset = detail.page * 6U;
      view.summary = "Match " + std::to_string(*findDetailIndex_ + 1U) + "/" + std::to_string(count) +
          " / text page " + std::to_string(detail.page + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (lines.size() + 5U) / 6U));
      for (std::size_t i = offset; i < std::min(offset + 6U, lines.size()); ++i)
        view.rows.push_back(detail.text[0].substr(lines[i].offset, lines[i].length));
      view.labels[2] = "Read-only result"; view.labels[3] = "Select and reveal note"; view.labels[4] = "Back to results";
      view.enabled[0] = detail.page > 0U; view.enabled[1] = offset + 6U < lines.size();
      view.enabled[2] = false; view.enabled[3] = current; view.enabled[5] = false;
    } else {
      const auto offset = replacementPage_ * 6U;
      view.summary += " / page " + std::to_string(replacementPage_ + 1U) + "/" +
          std::to_string(std::max(std::size_t{1U}, (count + 5U) / 6U));
      for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) {
        const auto& hit = findNavigation_->result().hits[i];
        view.rows.push_back(hit.noteId.toString() + " / " + std::to_string(hit.start.value()) + " ticks / " + domain::toUtf8(hit.matchedText));
      }
      view.rowsInspectable = current && count > 0U;
      view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count;
      view.enabled[3] = current && offset < count;
      if (count == 0U && replacementError_.empty()) view.rows.push_back("No matching notes in this field");
    }
    return view;
}

ReplacementReviewView NativeEditorController::clearDynamicsReviewView(ReplacementReviewView view) const {
    const auto points = clearDynamics_->points(); const auto offset = replacementPage_ * 6U;
    const bool current = clearDynamics_->matches(session_, regionId_);
    view.status = current ? (clearDynamics_->retainedGeneratedSelections() > 0U
        ? "Clear ENTIRE region native curve; generated dynamics may still apply"
        : "Clear native dynamics curve for the ENTIRE region") : "Region changed. Refresh before clearing dynamics.";
    if (!replacementError_.empty()) view.status = replacementError_;
    view.summary = std::to_string(clearDynamics_->regionNoteCount()) + " region notes / " + std::to_string(points.size()) +
        " points / " + std::to_string(clearDynamics_->retainedGeneratedSelections()) + " generated selections retained";
    view.summary += " / " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (points.size() + 5U) / 6U));
    for (std::size_t i = offset; i < std::min(offset + 6U, points.size()); ++i)
      view.rows.push_back(std::to_string(points[i].tick.value()) + " ticks: gain " + std::to_string(points[i].linearGain) + " -> native default 1.0");
    if (points.empty()) view.rows.push_back("Native region dynamics curve is already empty");
    view.labels[2] = "Generated takes kept"; view.labels[3] = "Clear region curve";
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < points.size();
    view.enabled[3] = current && replacementError_.empty() && clearDynamics_->hasChanges();
    view.enabled[5] = regionId_ == replacementRegion_; return view;
}

ReplacementReviewView NativeEditorController::noteCleanupReviewView(ReplacementReviewView view) const {
    const auto rows = noteCleanup_->rows(); const auto dependencies = noteCleanup_->dependencies();
    const auto count = replacementDependencies_ ? dependencies.size() : rows.size(); const auto offset = replacementPage_ * 6U;
    const bool current = noteCleanup_->matches(session_, regionId_);
    const bool overlap = noteCleanup_->kind() == ui::NoteCleanupKind::RemoveOverlap;
    const bool legato = noteCleanup_->kind() == ui::NoteCleanupKind::AutoLegato;
    view.status = current ? (legato ? "Auto legato: adjacent selected notes; gap at most one grid" :
        (overlap ? "Review overlap removal; note starts stay fixed" : "Review gap closure; off-grid/staccato notes may be skipped")) : "Targets or grid changed. Refresh before applying.";
    if (!replacementError_.empty()) view.status = replacementError_;
    view.summary = std::to_string(rows.size()) + " selected / " + std::to_string(noteCleanup_->changedCount()) + " changes / grid " +
        std::to_string(noteCleanup_->grid().value()) + " / " + (replacementDependencies_ ? "dependencies " : "notes ") +
        std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (count + 5U) / 6U));
    for (std::size_t i = offset; i < std::min(offset + 6U, count); ++i) {
      if (!replacementDependencies_) {
        const auto& row = rows[i];
        view.rows.push_back(row.noteId.toString() + ": " + std::to_string(row.beforeDuration.value()) + " -> " +
            std::to_string(row.afterDuration.value()) + " ticks / " + ui::noteCleanupOutcomeName(row.outcome));
        if (row.beforeArticulation != row.afterArticulation) view.rows.back() += " / set legato";
      } else {
        const auto& row = dependencies[i];
        const auto status = [](std::optional<bool> state) { return !state ? "absent" : (*state ? "unresolved" : "resolved"); };
        const auto kind = row.kind == ui::DependentEditKind::Phoneme ? "Phone " : (row.kind == ui::DependentEditKind::Unit ? "Unit " : "Seam ");
        view.rows.push_back(std::string{kind} + row.key.toString() + " " + status(row.beforeUnresolved) + " -> " + status(row.afterUnresolved));
      }
    }
    if (count == 0U) view.rows.push_back("No retained dependency records");
    view.labels[2] = "Notes / dependencies"; view.labels[3] = legato ? "Auto legato" : (overlap ? "Remove overlaps" : "Close gaps");
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < count; view.enabled[2] = true;
    view.enabled[3] = current && replacementError_.empty() && noteCleanup_->changedCount() > 0U;
    view.enabled[5] = regionId_ == replacementRegion_; return view;
}

ReplacementReviewView NativeEditorController::clearVibratoReviewView(ReplacementReviewView view) const {
    const auto edits = clearVibrato_->edits();
    const bool current = clearVibrato_->matches(session_, regionId_);
    view.status = current ? "Clear vibrato; saved settings and other expressions stay intact" : "Targets changed. Refresh before clearing vibrato.";
    if (!replacementError_.empty()) view.status = replacementError_;
    view.summary = std::to_string(clearVibrato_->selectedCount()) + " selected / " + std::to_string(edits.size()) +
        " enabled vibratos / page " + std::to_string(replacementPage_ + 1U) + "/" + std::to_string(std::max(std::size_t{1U}, (edits.size() + 5U) / 6U));
    const auto offset = replacementPage_ * 6U;
    for (std::size_t i = offset; i < std::min(offset + 6U, edits.size()); ++i)
      view.rows.push_back(edits[i].noteId.toString() + ": enabled -> disabled");
    if (edits.empty()) view.rows.push_back("Selected notes have no enabled vibrato");
    view.labels[2] = "Settings preserved"; view.labels[3] = "Clear vibrato";
    view.enabled[0] = replacementPage_ > 0U; view.enabled[1] = offset + 6U < edits.size();
    view.enabled[3] = current && replacementError_.empty() && !edits.empty();
    view.enabled[5] = regionId_ == replacementRegion_;
    return view;
}

}  // namespace seam::native_ui
