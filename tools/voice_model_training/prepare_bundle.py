"""Compose an admitted neural bundle from real acoustic and vocoder exports.

The bundle declaration is read from the exports, never typed by hand: profile,
vocabulary and graph bytes all come from the export receipts and are re-verified
against the bytes on disk. The tool writes the four required assets and then
publishes manifest.json last, using the same canonical manifest bytes as the
native `FrozenNeuralBundle::manifest`. It does not qualify a singer, grant source
rights or admit a model bundle for release.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "neural_runtime"))

from convert_vocabulary import convert_vocabulary  # noqa: E402


ACOUSTIC_FORMAT = "com.project-seam.acoustic-export"
VOCODER_FORMAT = "com.project-seam.vocoder-export"
CONFIGURATION_FORMAT = "com.project-seam.neural-bundle-configuration"
BUNDLE_FORMAT = "com.project-seam.neural-data-bundle"
MAXIMUM_FRAMES_LIMIT = 4 * 1024 * 1024
# The single verified export family. Anything else is refused rather than
# converted, because the runtime, the worker and the mel convention are bound
# to this geometry.
SUPPORTED_PROFILE = dict(profileId="seam-full-hop-slaney-v1", sampleRate=48000,
                         fftSize=1024, windowSize=1024, hopSize=256, bins=80,
                         minimumHz=20, maximumHz=24000, amplitudeScale="ln-amplitude",
                         melFrequencyScale="slaney", floor=1e-5, layout="TF")
PROFILE_FIELDS = tuple(SUPPORTED_PROFILE)
BREATHINESS_CONTROL = dict(name="breathiness", type="float32", shape=[1, "T"],
                           unit="normalized-periodic-aperiodic-balance", minimum=0, maximum=1,
                           default=0, supported=True)


def canonical_json(value, *, compact=False):
    # Native JsonValue objects are std::map, so every key is emitted in sorted
    # order; matching that is what makes both preparation paths interchangeable.
    if compact:
        return json.dumps(value, ensure_ascii=False, sort_keys=True,
                          separators=(",", ":")).encode("utf-8")
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


def sha256(payload):
    return hashlib.sha256(payload).hexdigest()


def read_report(directory, expected_format, graph_field, sha_field, bytes_field):
    path = directory / "export.json"
    report = json.loads(path.read_bytes())
    if type(report) is not dict or report.get("formatId") != expected_format:
        raise ValueError(f"{path} is not a {expected_format} report")
    graph_path = directory / report[graph_field]
    graph = graph_path.read_bytes()
    if sha256(graph) != report[sha_field] or len(graph) != report[bytes_field]:
        raise ValueError(f"{graph_path} differs from its recorded identity")
    if report.get("releaseEligible") is not False or report.get("singerQualified") is not False:
        raise ValueError(f"{path} does not describe an unqualified engineering export")
    profile = report.get("profile")
    # Real targets carry additional descriptive fields (padding, window, dtype).
    # The supported family must be present and exact; extras are hashed as-is.
    if type(profile) is not dict or not set(PROFILE_FIELDS) <= set(profile):
        raise ValueError(f"{path} has an unexpected acoustic profile shape")
    if any(profile[field] != value for field, value in SUPPORTED_PROFILE.items()):
        raise ValueError(f"{path} uses an unsupported acoustic profile")
    if sha256(canonical_json(profile, compact=True)) != report.get("profileSha256"):
        raise ValueError(f"{path} profile digest does not match its declaration")
    return report, graph


def vocabulary_asset(tokens, silence_phone=None):
    if type(tokens) is not list or len(tokens) < 2 or tokens[0] != "<PAD>":
        raise ValueError("Export vocabulary must be an ordered list beginning with <PAD>")
    mapping = {}
    for index, token in enumerate(tokens[1:], start=1):
        if type(token) is not str:
            raise ValueError("Export vocabulary entries must be strings")
        mapping.setdefault(token, index)
    # Reuse the native-compatible converter so token order, padding and aliases
    # stay identical to the CLI conversion path.
    converted = convert_vocabulary(json.dumps(mapping).encode("utf-8"))
    if silence_phone is None:
        return converted
    if silence_phone not in ("SP", "pau", "sil") or silence_phone not in mapping:
        raise ValueError("Selected silence phone must already exist in the trained vocabulary")
    index = mapping[silence_phone]
    if "SP" in mapping and mapping["SP"] != index:
        raise ValueError("Default SP silence conflicts with the selected trained silence ID")
    if "SP" in mapping:
        return converted
    # Add only a lookup alias, never a new embedding or a renamed trained token.
    vocabulary = json.loads(converted)
    vocabulary["aliases"]["SP"] = index
    result = canonical_json(vocabulary)
    if len(result) > 4 * 1024 * 1024:
        raise ValueError("Aliased vocabulary exceeds the native parser envelope")
    return result


def configuration_asset(profile, maximum_frames, steps_layout, vocoder_output,
                        breathiness_defaults=None):
    def features():
        return dict(sampleRate=profile["sampleRate"], hopSize=profile["hopSize"],
                    bins=profile["bins"],
                    # The stored target matrix is [T,F]; the admitted graph consumes
                    # mel as [1,T,F], so the declared runtime layout is BTF.
                    layout="BTF", amplitudeScale=profile["amplitudeScale"],
                    # SEAM targets already store ln amplitude with a 1e-5 floor, so
                    # the declaration is an identity normalization.
                    multiplier=1.0, offset=0.0, minimumHz=profile["minimumHz"],
                    maximumHz=profile["maximumHz"], fftSize=profile["fftSize"],
                    windowSize=profile["windowSize"],
                    melFrequencyScale=profile["melFrequencyScale"])
    document = dict(formatId=CONFIGURATION_FORMAT, maximumFrames=maximum_frames,
                    stepsLayout=steps_layout, vocoderOutput=vocoder_output,
                    acousticFeatures=features(), vocoderFeatures=features())
    if breathiness_defaults is not None:
        # Schema 4 binds the measured per-phone defaults into the model itself.
        document["schemaVersion"] = 4
        document["conditioningDefaults"] = dict(breathiness=breathiness_defaults)
    else:
        document["schemaVersion"] = 3
    return canonical_json(document)


def load_breathiness_prior(path, expected_sha256, tokens):
    """Read a captured breathiness prior into a vocabulary-bound default map.

    The prior is a measurement receipt, not a hand edit: the file must match
    its declared digest, carry the published schema, and name only symbols the
    trained vocabulary actually contains.
    """
    if path.is_symlink() or not path.is_file():
        raise ValueError("Breathiness prior must be a regular non-symlink file")
    payload = path.read_bytes()
    if len(payload) > 4 * 1024 * 1024 or sha256(payload) != expected_sha256:
        raise ValueError("Breathiness prior differs from its declared SHA-256")
    prior = json.loads(payload)
    if (type(prior) is not dict
            or prior.get("formatId") != "com.project-seam.breathiness-prior"
            or prior.get("schemaVersion") != 1
            or prior.get("releaseEligible") is not False
            or prior.get("singerQualified") is not False
            or prior.get("supervisionAdmitted") is not False):
        raise ValueError("Breathiness prior is not a captured unqualified receipt")
    rows = prior.get("symbols")
    if type(rows) is not list or len(rows) > 4096:
        raise ValueError("Breathiness prior symbol table is invalid")
    known = set(tokens)
    defaults = {}
    for row in rows:
        if (type(row) is not dict
                or not {"symbol", "breathiness", "windows", "sourceMean", "measured", "periodic"}
                <= set(row)):
            raise ValueError("Breathiness prior row shape is invalid")
        symbol, value = row["symbol"], row["breathiness"]
        if (type(symbol) is not str or not symbol or len(symbol.encode()) > 256
                or any(ord(c) < 32 or ord(c) == 127 for c in symbol)
                or type(value) not in (int, float) or type(value) is bool
                or not 0 <= value <= 1):
            raise ValueError("Breathiness prior symbol or value is invalid")
        if value > 0:
            if symbol not in known:
                raise ValueError("Breathiness prior names a symbol outside the vocabulary")
            defaults[symbol] = float(value)
    return defaults


def manifest_asset(assets):
    rows = [dict(role=role, name=name, sha256=sha256(payload), bytes=len(payload))
            for role, name, payload in sorted(assets, key=lambda entry: entry[1])]
    # Native frozen-bundle manifests are published pretty-printed with sorted
    # keys and a trailing newline; byte equality keeps both paths interchangeable.
    return canonical_json(dict(formatId=BUNDLE_FORMAT, schemaVersion=1, assets=rows))


def prepare(acoustic_directory, vocoder_directory, output, maximum_frames,
            steps_layout, vocoder_output, resource_id=None, resource_version=None,
            silence_phone=None, breathiness_prior=None, breathiness_prior_sha256=None):
    if output.exists() or output.is_symlink() or not output.parent.is_dir():
        raise ValueError("Output must be a new directory with an existing parent")
    if type(maximum_frames) is not int or not 1 <= maximum_frames <= MAXIMUM_FRAMES_LIMIT:
        raise ValueError("Maximum frames must be a bounded positive integer")
    if steps_layout not in ("scalar", "vector1") or vocoder_output not in ("audio", "waveform"):
        raise ValueError("Unsupported steps layout or vocoder output name")
    # A resource record is what lets an installed directory be resolved back to
    # the identity a project saved. It is written only as a pair.
    if (resource_id is None) != (resource_version is None):
        raise ValueError("Resource id and version must be supplied together")
    if resource_id is not None:
        for value, limit in ((resource_id, 256), (resource_version, 256)):
            if type(value) is not str or not 1 <= len(value) <= limit or any(ord(c) < 32 or ord(c) == 127 for c in value):
                raise ValueError("Resource id and version must be bounded printable text")
    if (breathiness_prior is None) != (breathiness_prior_sha256 is None):
        raise ValueError("Breathiness prior and its SHA-256 must be supplied together")
    acoustic, acoustic_graph = read_report(acoustic_directory, ACOUSTIC_FORMAT,
        "acousticPath", "acousticSha256", "acousticBytes")
    vocoder, vocoder_graph = read_report(vocoder_directory, VOCODER_FORMAT,
        "vocoderPath", "vocoderSha256", "vocoderBytes")
    if acoustic.get("runtimeSmokePassed") is not True:
        raise ValueError("Acoustic export did not record a passing runtime smoke test")
    controls = acoustic.get("conditioningControls", [])
    if controls not in ([], [BREATHINESS_CONTROL]):
        raise ValueError("Acoustic export declares unsupported conditioning controls")
    if controls:
        if (acoustic.get("schemaVersion") != 2 or acoustic.get("conditioningRevision") != 2
                or acoustic.get("encoderRuntimeCheck", {}).get("breathinessConditionEffectPassed") is not True
                or acoustic.get("encoderRuntimeCheck", {}).get("maximumBreathinessConditionEffect", 0) <= 1e-7
                or acoustic.get("deploymentBridgeCheck", {}).get("breathinessConditionEffectPassed") is not True
                or acoustic.get("deploymentBridgeCheck", {}).get("maximumBreathinessMelEffect", 0) <= 1e-7):
            raise ValueError("Breathiness export lacks a passing acoustic-effect receipt")
        graph_inputs = acoustic.get("inspection", {}).get("inputs", [])
        declared = next((entry for entry in graph_inputs if entry.get("name") == "breathiness"), None)
        if declared is None or declared.get("dtype") != 1 or declared.get("shape") != [1, "n_frames"]:
            raise ValueError("Breathiness export graph interface differs from [1, T] float32")
    if breathiness_prior is not None and not controls:
        raise ValueError("A breathiness prior requires a conditioned acoustic export")
    if acoustic["profileSha256"] != vocoder["profileSha256"] or acoustic["profile"] != vocoder["profile"]:
        raise ValueError("Acoustic and vocoder exports disagree on the acoustic profile")
    if len(acoustic_graph) == 0 or len(vocoder_graph) == 0:
        raise ValueError("Exported graphs must be nonempty")
    vocabulary = vocabulary_asset(acoustic.get("vocabulary"), silence_phone)
    breathiness_defaults = None
    if breathiness_prior is not None:
        published = json.loads(vocabulary)
        known_symbols = list(published.get("tokens", [])) + list(published.get("aliases", {}))
        breathiness_defaults = load_breathiness_prior(
            breathiness_prior, breathiness_prior_sha256, known_symbols)
    configuration = configuration_asset(acoustic["profile"], maximum_frames,
                                        steps_layout, vocoder_output, breathiness_defaults)
    assets = [("acoustic", "acoustic", acoustic_graph), ("vocoder", "vocoder", vocoder_graph),
              ("vocabulary", "vocabulary", vocabulary), ("configuration", "configuration", configuration)]
    manifest = manifest_asset(assets)
    output.mkdir(mode=0o700)
    for _role, name, payload in assets:
        path = output / name
        with path.open("xb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    # The manifest is published last: a directory without it is not a bundle.
    with (output / "manifest.json").open("xb") as stream:
        stream.write(manifest)
        stream.flush()
        os.fsync(stream.fileno())
    if resource_id is not None:
        record = canonical_json(dict(formatId="com.project-seam.neural-resource", schemaVersion=1,
                                     id=resource_id, version=resource_version,
                                     contentHash=sha256(manifest)))
        with (output / "resource.json").open("xb") as stream:
            stream.write(record)
            stream.flush()
            os.fsync(stream.fileno())
    return dict(manifestSha256=sha256(manifest), resourceId=resource_id,
                resourceVersion=resource_version, silencePhone=silence_phone,
                assets=[dict(role=role, name=name, sha256=sha256(payload), bytes=len(payload))
                        for role, name, payload in assets],
                acousticCheckpointReceiptSha256=acoustic["checkpointReceiptSha256"],
                vocoderCheckpointReceiptSha256=vocoder["checkpointReceiptSha256"],
                maximumFrames=maximum_frames, stepsLayout=steps_layout,
                vocoderOutput=vocoder_output, sourceRightsRevalidated=False,
                conditioningRevision=2, conditioningControls=controls,
                breathinessDefaults=breathiness_defaults,
                modelBundleAdmitted=False, singerQualified=False, releaseEligible=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--acoustic-export", type=Path, required=True)
    parser.add_argument("--vocoder-export", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--maximum-frames", type=int, required=True)
    parser.add_argument("--steps-layout", choices=("scalar", "vector1"), default="scalar")
    parser.add_argument("--vocoder-output", choices=("audio", "waveform"), default="audio")
    parser.add_argument("--resource-id")
    parser.add_argument("--resource-version")
    parser.add_argument("--silence-phone", choices=("SP", "pau", "sil"),
                        help="Alias default SP lookup to an existing trained silence token")
    parser.add_argument("--breathiness-prior", type=Path,
                        help="Captured breathiness prior bound into schema-4 conditioning defaults")
    parser.add_argument("--breathiness-prior-sha256",
                        help="Expected SHA-256 of the breathiness prior file")
    args = parser.parse_args()
    try:
        report = prepare(args.acoustic_export, args.vocoder_export, args.output,
                         args.maximum_frames, args.steps_layout, args.vocoder_output,
                         args.resource_id, args.resource_version, args.silence_phone,
                         args.breathiness_prior, args.breathiness_prior_sha256)
        print(json.dumps(report))
        return 0
    except (OSError, ValueError, KeyError) as error:
        print(str(error)[:256], file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
