from __future__ import annotations

from dataclasses import dataclass
from typing import Final


@dataclass(frozen=True, slots=True)
class RequirementSpec:
    case_slugs: tuple[str, ...]
    work_packages: tuple[str, ...]
    result_type: str
    resources: tuple[str, ...]
    backends: tuple[str, ...]
    review_roles: tuple[str, ...]


REQUIREMENTS: Final = {
    "R1": RequirementSpec(
        tuple("cross-note-melody syllable-order melisma-articulation timing-tempo".split()),
        tuple("V02 V03".split()), "acoustic",
        tuple("sample-real sample-procedural neural-original".split()), tuple("classical neural".split()),
        tuple("musician".split()),
    ),
    "R2": RequirementSpec(
        tuple("vibrato pitch-portamento dynamics-gain consonant-attack legato-melisma style-blend breath-release breathiness tension-power formant-timbre growl-roughness automatic-performance takes-harmonies persistence-neutral".split()),
        tuple("V04 V05 V08 V13".split()), "expression",
        tuple("all-released".split()), tuple("classical procedural neural".split()),
        tuple("musician".split()),
    ),
    "R3": RequirementSpec(
        tuple("original-female-recipe phonetic-range voice-song-separation".split()),
        tuple("V15 V16".split()), "voice-design",
        tuple("recipe-original sample-procedural".split()), tuple("procedural".split()),
        tuple("musician voice-producer".split()),
    ),
    "R4": RequirementSpec(
        tuple("real-input generated-input".split()),
        tuple("V07 V16".split()), "producer-journey",
        tuple("sample-real".split()), tuple("classical".split()),
        tuple("voice-producer musician".split()),
    ),
    "R5": RequirementSpec(
        tuple("qa-lineage versioned-install interruption-recovery".split()),
        tuple("V07 V16".split()), "producer-journey",
        tuple("sample-real sample-procedural".split()), tuple("classical procedural".split()),
        tuple("voice-producer".split()),
    ),
    "R6": RequirementSpec(
        tuple("inventory-range two-styles paired-blend".split()),
        tuple("V06 V07 V08 V15".split()), "resource-quality",
        tuple("all-released".split()), tuple("classical procedural neural".split()),
        tuple("musician native-language-reviewer".split()),
    ),
    "R7": RequirementSpec(
        tuple("ja-pronunciation en-pronunciation ko-pronunciation edit-reconciliation".split()),
        tuple("V10 V12".split()), "language-review",
        tuple("all-released".split()), tuple("classical neural".split()),
        tuple("native-language-reviewer".split()),
    ),
    "R8": RequirementSpec(
        tuple("classical-range independent-transitions selection-capabilities".split()),
        tuple("V01 V05 V06 V08".split()), "acoustic",
        tuple("sample-real sample-procedural".split()), tuple("classical".split()),
        tuple("musician".split()),
    ),
    "R9": RequirementSpec(
        tuple("dataset-model installed-inference provider-closure".split()),
        tuple("V07 V12".split()), "neural-qualification",
        tuple("neural-original".split()), tuple("neural".split()),
        tuple("model-reviewer musician native-language-reviewer".split()),
    ),
    "R10": RequirementSpec(
        tuple("manual-ownership take-harmony-editing assisted-benefit".split()),
        tuple("V13".split()), "creator-session",
        tuple("neural-original".split()), tuple("neural".split()),
        tuple("independent-creator musician".split()),
    ),
    "R11": RequirementSpec(
        tuple("musical-editing dense-accessibility".split()),
        tuple("V09 V10".split()), "ui-accessibility",
        tuple("all-released".split()), tuple("native".split()),
        tuple("independent-creator accessibility-reviewer".split()),
    ),
    "R12": RequirementSpec(
        tuple("ustx-roundtrip smf-roundtrip hostile-input".split()),
        tuple("V11".split()), "interchange",
        tuple("all-released".split()), tuple("native".split()),
        tuple("independent-creator security-reviewer".split()),
    ),
    "R13": RequirementSpec(
        tuple("standalone-authoring macos-arm64-reaper-clap macos-arm64-reaper-vst3 windows-x86_64-reaper-clap windows-x86_64-reaper-vst3 macos-arm64-bitwig-clap macos-arm64-bitwig-vst3 windows-x86_64-bitwig-clap windows-x86_64-bitwig-vst3 macos-arm64-logic-pro-auv2".split()),
        tuple("V11 V14".split()), "host-session",
        tuple("all-released".split()), tuple("classical neural".split()),
        tuple("host-reviewer musician".split()),
    ),
    "R14": RequirementSpec(
        tuple("singer-identity synchronized-performance reduced-motion".split()),
        tuple("V09 V17".split()), "ui-accessibility",
        tuple("character-original all-released".split()), tuple("native".split()),
        tuple("independent-creator accessibility-reviewer".split()),
    ),
    "R15": RequirementSpec(
        tuple("bounded-work immutable-publication save-recovery classical-small-edit legacy-preview".split()),
        tuple("V05 V12 V14 V15 V16".split()), "safety",
        tuple("all-released".split()), tuple("classical procedural neural native".split()),
        tuple("runtime-reviewer".split()),
    ),
    "R16": RequirementSpec(
        tuple("ja-corpus en-corpus ko-corpus finished-songs independent-creators acoustic-listening".split()),
        tuple("V01 V06 V07 V12 V13 V14".split()), "quality-study",
        tuple("all-released".split()), tuple("classical procedural neural".split()),
        tuple("musician native-language-reviewer independent-creator voice-producer".split()),
    ),
    "R17": RequirementSpec(
        tuple("signed-install soak-hosts u60-support".split()),
        tuple("V14".split()), "installed-release",
        tuple("all-released".split()), tuple("native".split()),
        tuple("release-reviewer".split()),
    ),
    "R18": RequirementSpec(
        tuple("coverage-admission content-lineage promotion-closure".split()),
        tuple("V18".split()), "gate-regression",
        tuple("all-released".split()), tuple("native".split()),
        tuple("release-reviewer security-reviewer".split()),
    ),
    "R19": RequirementSpec(
        tuple("sources-transforms training-model dictionary-character".split()),
        tuple("V07 V12 V14 V16".split()), "provenance",
        tuple("sample-real sample-procedural recipe-original neural-original dictionary-original character-original".split()), tuple("native".split()),
        tuple("rights-reviewer".split()),
    ),
    "R20": RequirementSpec(
        tuple("shared-actions batch-lineage installed-song".split()),
        tuple("V07 V09 V12 V16".split()), "producer-journey",
        tuple("sample-real sample-procedural recipe-original neural-original".split()), tuple("native classical procedural neural".split()),
        tuple("voice-producer independent-creator".split()),
    ),
}

LANGUAGES: Final = ("ja", "en", "ko")
PLATFORMS: Final = ("macos-arm64", "windows-x86_64")
HOST_TUPLES: Final = (
    "macos-arm64/reaper/CLAP",
    "macos-arm64/reaper/VST3",
    "windows-x86_64/reaper/CLAP",
    "windows-x86_64/reaper/VST3",
    "macos-arm64/bitwig/CLAP",
    "macos-arm64/bitwig/VST3",
    "windows-x86_64/bitwig/CLAP",
    "windows-x86_64/bitwig/VST3",
    "macos-arm64/logic-pro/AUv2",
)
RESOURCE_KINDS: Final = ("sample-real", "sample-procedural", "recipe-original", "neural-original", "dictionary-original", "character-original")
BACKENDS: Final = ("classical", "procedural", "neural", "native")
WORK_PACKAGES: Final = tuple(f"V{number:02d}" for number in range(1, 19))
CASE_IDS: Final = tuple(f"{requirement}.{slug}" for requirement, spec in REQUIREMENTS.items() for slug in spec.case_slugs)

CRITERIA_BY_REQUIREMENT: Final = {
    "R1": tuple("pitch-median pitch-within-50 timing-displacement".split()),
    "R2": tuple("expression-tolerances".split()),
    "R3": tuple("identity-rubric generation-budgets".split()),
    "R4": tuple("producer-acceptance".split()),
    "R5": tuple("producer-acceptance".split()),
    "R6": tuple("identity-rubric".split()),
    "R7": tuple("pronunciation-scoring".split()),
    "R8": tuple("pitch-median pitch-within-50 acoustic-boundaries".split()),
    "R9": tuple("neural-budgets pronunciation-scoring identity-rubric".split()),
    "R10": tuple("creator-count assisted-comparison".split()),
    "R11": tuple("creator-count accessibility-acceptance".split()),
    "R12": tuple("interchange-acceptance".split()),
    "R13": tuple("host-acceptance".split()),
    "R14": tuple("accessibility-acceptance".split()),
    "R15": tuple("resource-limits cancellation-budgets reference-machines".split()),
    "R16": tuple("phrases-per-language song-count creator-count pitch-median pitch-within-50 timing-displacement pronunciation-scoring identity-rubric assisted-comparison".split()),
    "R17": tuple("release-acceptance".split()),
    "R18": tuple("gate-acceptance".split()),
    "R19": tuple("rights-acceptance".split()),
    "R20": tuple("producer-acceptance".split()),
}

ARTIFACT_KINDS: Final = {
    "acoustic": tuple("project audio measurement independent-review".split()),
    "expression": tuple("project audio measurement independent-review".split()),
    "voice-design": tuple("recipe source-binding audio measurement installed-resource independent-review".split()),
    "producer-journey": tuple("source-binding project operation-log installed-resource audio independent-review".split()),
    "resource-quality": tuple("manifest installed-resource audio measurement independent-review".split()),
    "language-review": tuple("project dictionary-manifest audio pronunciation-score independent-review".split()),
    "neural-qualification": tuple("model-manifest dataset-split inference-log audio measurement rights-record independent-review".split()),
    "creator-session": tuple("assignment-record project audio task-timing independent-review".split()),
    "ui-accessibility": tuple("project ui-capture accessibility-tree session-log independent-review".split()),
    "interchange": tuple("input-score output-score conversion-report allocation-log session-log independent-review".split()),
    "host-session": tuple("project audio session-log installed-manifest independent-review".split()),
    "safety": tuple("workload-log memory-trace cancellation-trace recovery-log independent-review".split()),
    "quality-study": tuple("project audio measurement assignment-record task-timing independent-review".split()),
    "installed-release": tuple("installed-manifest session-log soak-log support-redaction-log independent-review".split()),
    "gate-regression": tuple("candidate-manifest audit-log restored-archive independent-review".split()),
    "provenance": tuple("source-binding rights-record manifest independent-review".split()),
}
