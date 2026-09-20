"""Expand reviewed label geometry onto its analysis clock; no admission side effect."""
from .labels import label_report, score_report

# The single approved architectural addition to an acoustic model. Enabling the
# breathiness control adds exactly this embedding and nothing else, which is what
# lets a warm start stay closed to every other change. Warm-start initialization
# and lineage auditing both import this set so the two cannot drift apart.
ADDED_CONDITIONING_PARAMETERS = frozenset({
    "fs2.variance_embeds.breathiness.weight",
    "fs2.variance_embeds.breathiness.bias"})

ADDED_CONDITIONING_CONTROLS = ("breathiness",)


def added_parameters(prior_controls, current_controls):
    """Return the parameters gained by enabling the control, else the empty set.

    Raises for any transition that is not exactly enabling breathiness, so an
    unsupported architectural change cannot be mistaken for the approved one.
    """
    prior = list(prior_controls or [])
    current = list(current_controls or [])
    if prior == current:
        return frozenset()
    if prior != [] or current != list(ADDED_CONDITIONING_CONTROLS):
        raise ValueError("Warm start may only add an enabled breathiness control")
    return ADDED_CONDITIONING_PARAMETERS


def declares_added_parameters(value):
    """Validate a captured warm-start declaration of the added parameters."""
    added = value.get("addedParameters", []) if isinstance(value, dict) else None
    return (isinstance(added, list) and len(set(added)) == len(added)
            and all(name in ADDED_CONDITIONING_PARAMETERS for name in added))


# The dataset-identity fields a conditioning addition is allowed to change. The
# remaining bindings - reviewed sources, rights review, permission configuration
# and the train/validation/test split - must stay byte-identical, so the repair
# cannot quietly train on different material than the checkpoint it warms from.
CONDITIONING_BINDING_FIELDS = (
    "conditioningSha256", "labelConfigurationSha256", "labelReviewSha256")


def validate_conditioning_bindings(source, current):
    """Require a fresh dataset to differ from its captured one only in conditioning.

    Adding the aperiodicity channel necessarily changes the dataset digest and the
    three conditioning bindings. Requiring those to change, while requiring every
    other binding to match, is what keeps a repaired run comparable to the model it
    continues: the same reviewed songs, split, rights and permissions.
    """
    if (not isinstance(source, dict) or not isinstance(current, dict)
            or set(source) != set(current) or not source
            or not set(CONDITIONING_BINDING_FIELDS) <= set(source)):
        raise ValueError("Conditioning addition requires matching dataset binding fields")
    for name in sorted(set(source) - set(CONDITIONING_BINDING_FIELDS)):
        if source[name] != current[name]:
            raise ValueError(f"Conditioning addition may not change the {name} dataset binding")
    for name in CONDITIONING_BINDING_FIELDS:
        if source[name] == current[name]:
            raise ValueError("Conditioning addition requires changed conditioning supervision")
    return dict(current)


def build_conditioning(label: dict, score: dict, *, vocabulary: list[str],
                       minimum_confidence: float, maximum_frames: int = 65536,
                       breathiness: list[float] | None = None) -> dict:
    """Use left-edge sample positions and half-open phone/note intervals.

    Token zero is reserved for padding; vocabulary order defines positive IDs.
    Rest is a separate mask, so MIDI zero remains a legitimate pitched note.
    Phone and note clocks remain independent (consonants may anticipate notes).
    This pure transform neither authenticates a review nor loads source audio.
    """
    if (not isinstance(vocabulary, list) or not 1 <= len(vocabulary) <= 4096
            or any(not isinstance(s, str) or not 1 <= len(s.encode()) <= 256 for s in vocabulary)
            or len(set(vocabulary)) != len(vocabulary)):
        raise ValueError("Conditioning requires an ordered unique vocabulary")
    if type(maximum_frames) is not int or not 1 <= maximum_frames <= 1000000:
        raise ValueError("Invalid conditioning frame budget")
    report = label_report(label, vocabulary=set(vocabulary), minimum_confidence=minimum_confidence)
    if any(issue["code"] != "review-revision-missing" for issue in report["correctionQueue"]):
        raise ValueError("Resolve label corrections before constructing conditioning")
    score_report(score, frame_count=label["frameCount"], phoneme_count=len(label["phonemes"]), explicit_silence=True)
    count = len(label["f0Hz"])
    if count > maximum_frames:
        raise ValueError("Conditioning frame budget exceeded")
    if breathiness is not None:
        if not isinstance(breathiness, list) or len(breathiness) != count:
            raise ValueError("Breathiness conditioning length must match frame count")
        if any(type(v) not in (int, float) or not 0.0 <= v <= 1.0 for v in breathiness):
            raise ValueError("Breathiness values must be normalized floats in [0, 1]")
    tokens = {symbol: index + 1 for index, symbol in enumerate(vocabulary)}
    phones, notes = label["phonemes"], score["notes"]
    phone_index = note_index = 0
    rows = []
    for index in range(count):
        sample = index * label["hopSize"]
        while sample >= phones[phone_index]["endFrame"]:
            phone_index += 1
        while sample >= notes[note_index]["endFrame"]:
            note_index += 1
        phone, note = phones[phone_index], notes[note_index]
        rows.append(dict(sourceFrame=sample, validSamples=min(label["hopSize"], label["frameCount"] - sample),
                         phoneIndex=phone_index, phoneId=tokens[phone["symbol"]], noteIndex=note_index,
                         midi=note["midi"], rest=note["midi"] is None, slur=note["slur"],
                         syllable=note["syllable"], f0Hz=label["f0Hz"][index], voiced=label["voiced"][index],
                         breathiness=float(breathiness[index]) if breathiness is not None else 0.0))
    return dict(formatId="com.project-seam.training-frame-conditioning", schemaVersion=2,
                sourceId=label["sourceId"], hopSize=label["hopSize"], sampleCount=label["frameCount"],
                frameAnchor="left-edge", vocabulary=list(vocabulary), paddingId=0,
                language=score["language"], frames=rows, hasBreathiness=bool(breathiness is not None),
                conditioningRevision=2,
                trainingAdmitted=False, releaseEligible=False)
