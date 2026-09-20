"""Bind offline graph inspection to immutable, manifest-verified asset bytes.

No filesystem paths, executable selection or production admission is provided.
"""
import hashlib
import json
import math
import re

from inspect_pair import inspect_pair


def parse(payload, maximum, *, maximum_nodes=65544, maximum_entries=65536):
    if type(payload) is not bytes or not 0 < len(payload) <= maximum:
        raise ValueError("Metadata byte bound or immutable byte type invalid")
    # Check container nesting before invoking the recursive JSON decoder. Quotes
    # and escapes must be tracked so braces inside strings do not count.
    depth = 0
    quoted = escaped = False
    for byte in payload:
        if quoted:
            if escaped:
                escaped = False
            elif byte == 92:
                escaped = True
            elif byte == 34:
                quoted = False
        elif byte == 34:
            quoted = True
        elif byte in (123, 91):
            depth += 1
            if depth > 3:
                raise ValueError("Metadata nesting depth exceeded")
        elif byte in (125, 93):
            depth -= 1
            if depth < 0:
                raise ValueError("Invalid metadata nesting")
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("Duplicate JSON field")
            result[key] = value
        return result
    result = json.loads(payload.decode("utf-8"), object_pairs_hook=pairs,
                        parse_constant=lambda _: (_ for _ in ()).throw(ValueError("Nonfinite JSON")))
    stack, nodes = [result], 0
    while stack:
        value = stack.pop()
        nodes += 1
        if nodes > maximum_nodes:
            raise ValueError("Metadata node bound exceeded")
        strings = []
        if type(value) in (dict, list):
            if len(value) > maximum_entries:
                raise ValueError("Metadata collection bound exceeded")
            stack.extend(value.values() if type(value) is dict else value)
            if type(value) is dict:
                strings.extend(value)
        elif type(value) is str:
            strings.append(value)
        elif type(value) is float and not math.isfinite(value):
            raise ValueError("Nonfinite JSON number")
        if any(len(string.encode("utf-8")) > 128 for string in strings):
            raise ValueError("Metadata string byte bound exceeded")
    return result


def fields(value, names):
    if type(value) is not dict or set(value) != set(names):
        raise ValueError("Unexpected metadata fields")


def inspect_bundle(manifest_bytes, asset_bytes, expected_hash):
    manifest = parse(manifest_bytes, 32768)
    if hashlib.sha256(manifest_bytes).hexdigest() != expected_hash:
        raise ValueError("Manifest digest mismatch")
    fields(manifest, ("formatId", "schemaVersion", "assets"))
    if manifest["formatId"] != "com.project-seam.neural-data-bundle" or type(manifest["schemaVersion"]) is not int or manifest["schemaVersion"] != 1:
        raise ValueError("Unsupported manifest schema")
    rows = manifest["assets"]
    # This first paired profile cannot execute variance or external tensor assets.
    if type(rows) is not list or len(rows) != 4:
        raise ValueError("Paired profile requires exactly four roles")
    by_role, names, total = {}, [], 0
    for row in rows:
        fields(row, ("role", "name", "sha256", "bytes"))
        role, name = row["role"], row["name"]
        if role not in ("acoustic", "vocoder", "configuration", "vocabulary") or role in by_role:
            raise ValueError("Unsupported or duplicate role")
        if type(name) is not str or not re.fullmatch(r"[a-z0-9_-][a-z0-9_.-]{0,63}", name) or name in names:
            raise ValueError("Invalid asset name")
        maximum = 4 * 1024 * 1024 if role in ("configuration", "vocabulary") else 256 * 1024 * 1024
        if type(row["bytes"]) is not int or not 0 < row["bytes"] <= maximum:
            raise ValueError("Asset byte bound invalid")
        payload = asset_bytes.get(name)
        if type(payload) is not bytes or len(payload) != row["bytes"] or hashlib.sha256(payload).hexdigest() != row["sha256"]:
            raise ValueError("Asset bytes differ from manifest")
        total += len(payload)
        if total > 512 * 1024 * 1024:
            raise ValueError("Bundle byte bound exceeded")
        by_role[role] = payload
        names.append(name)
    if names != sorted(names) or set(names) != set(asset_bytes):
        raise ValueError("Asset ordering or closure differs")
    configuration = parse(by_role["configuration"], 4 * 1024 * 1024,
                          maximum_nodes=16384, maximum_entries=4096)
    conditioned = type(configuration) is dict and configuration.get("schemaVersion") == 4
    output_bound = conditioned or (type(configuration) is dict and configuration.get("schemaVersion") == 3)
    extended = output_bound or (type(configuration) is dict and configuration.get("schemaVersion") == 2)
    fields(configuration, ("formatId", "schemaVersion", "maximumFrames", "acousticFeatures", "vocoderFeatures") + (("stepsLayout",) if extended else ()) + (("vocoderOutput",) if output_bound else ()) + (("conditioningDefaults",) if conditioned else ()))
    if configuration["formatId"] != "com.project-seam.neural-bundle-configuration" or type(configuration["schemaVersion"]) is not int or configuration["schemaVersion"] not in (1, 2, 3, 4):
        raise ValueError("Unsupported configuration schema")
    feature = configuration["acousticFeatures"]
    fields(feature, ("sampleRate", "hopSize", "bins", "layout", "amplitudeScale", "multiplier", "offset", "minimumHz", "maximumHz") + (("fftSize", "windowSize", "melFrequencyScale") if extended else ()))
    if feature != configuration["vocoderFeatures"]:
        raise ValueError("Acoustic and vocoder features disagree")
    for name in ("sampleRate", "hopSize", "bins"):
        if type(configuration["vocoderFeatures"][name]) is not int:
            raise ValueError("Invalid vocoder integer feature")
    if any(type(value) is bool for value in configuration["vocoderFeatures"].values()):
        raise ValueError("Boolean vocoder feature rejected")
    for name, low, high in (("sampleRate", 8000, 384000), ("hopSize", 1, 8192), ("bins", 1, 512)):
        if type(feature[name]) is not int or not low <= feature[name] <= high:
            raise ValueError("Invalid integer feature")
    for name in ("multiplier", "offset", "minimumHz", "maximumHz"):
        if type(feature[name]) not in (int, float) or not math.isfinite(feature[name]):
            raise ValueError("Invalid numeric feature")
    if feature["amplitudeScale"] not in ("linear-amplitude", "ln-amplitude", "log10-amplitude") or not 0 < feature["multiplier"] <= 1000 or abs(feature["offset"]) > 1000 or not 0 <= feature["minimumHz"] < feature["maximumHz"] <= feature["sampleRate"] / 2:
        raise ValueError("Invalid mel representation")
    if extended:
        for role in ("acousticFeatures", "vocoderFeatures"):
            spec = configuration[role]
            if type(spec["fftSize"]) is not int or type(spec["windowSize"]) is not int or not 2 <= spec["fftSize"] <= 32768 or not 1 <= spec["windowSize"] <= spec["fftSize"] or spec["hopSize"] > spec["windowSize"] or spec["melFrequencyScale"] not in ("slaney", "htk"):
                raise ValueError("Invalid spectral analysis convention")
    vocabulary = parse(by_role["vocabulary"], 4 * 1024 * 1024)
    aliased = type(vocabulary) is dict and vocabulary.get("schemaVersion") == 2
    fields(vocabulary, ("formatId", "schemaVersion", "tokens") + (("aliases",) if aliased else ()))
    tokens = vocabulary["tokens"]
    if vocabulary["formatId"] != "com.project-seam.neural-vocabulary" or type(vocabulary["schemaVersion"]) is not int or vocabulary["schemaVersion"] not in (1, 2) or type(tokens) is not list or not 1 <= len(tokens) <= 65536:
        raise ValueError("Invalid vocabulary schema")
    if any(type(token) is not str or not token or len(token.encode("utf-8")) > 128 or
           any(ord(character) < 32 or ord(character) == 127 for character in token)
           for token in tokens) or len(set(tokens)) != len(tokens) or tokens[0] != "<PAD>":
        raise ValueError("Invalid paired-profile vocabulary")
    if aliased:
        aliases = vocabulary["aliases"]
        canonical_tokens = set(tokens)
        if type(aliases) is not dict:
            raise ValueError("Invalid vocabulary aliases")
        for alias, index in aliases.items():
            if not alias or alias in canonical_tokens or any(ord(c) < 32 or ord(c) == 127 for c in alias) or type(index) is not int or not 1 <= index < len(tokens):
                raise ValueError("Invalid vocabulary alias target or name")
    if conditioned:
        # Schema 4 carries measured per-phone defaults for the aperiodicity
        # channel. The shape is closed and every symbol must resolve against
        # the bundle's own vocabulary, so a typo cannot silently dead-code.
        defaults = configuration["conditioningDefaults"]
        fields(defaults, ("breathiness",))
        breathiness = defaults["breathiness"]
        if type(breathiness) is not dict or len(breathiness) > 4096:
            raise ValueError("Invalid breathiness defaults")
        known = set(tokens) | set(vocabulary.get("aliases", {}))
        for symbol, value in breathiness.items():
            if not symbol or len(symbol.encode("utf-8")) > 256 or any(ord(c) < 32 or ord(c) == 127 for c in symbol):
                raise ValueError("Invalid breathiness default symbol")
            if symbol not in known:
                raise ValueError("Breathiness default names a symbol outside the vocabulary")
            if type(value) not in (int, float) or type(value) is bool or not math.isfinite(value) or not 0 <= value <= 1:
                raise ValueError("Invalid breathiness default value")
    pair = inspect_pair(by_role["acoustic"], by_role["vocoder"], bins=feature["bins"],
                        layout=feature["layout"], hop_size=feature["hopSize"],
                        maximum_sample_frames=configuration["maximumFrames"],
                        steps_layout=configuration["stepsLayout"] if extended else "scalar",
                        vocoder_output=configuration["vocoderOutput"] if output_bound else "audio")
    # The same invariant the native admission enforces: measured defaults may
    # only ship with a graph that consumes the channel they feed.
    if (conditioned and configuration["conditioningDefaults"]["breathiness"]
            and "breathiness" not in pair["contract"].get("conditioningControls", [])):
        raise ValueError("Breathiness defaults require a conditioned acoustic graph")
    return {"status": "OFFLINE_BUNDLE_INSPECTED", "bundleHash": expected_hash,
            "vocabularyHash": hashlib.sha256(by_role["vocabulary"]).hexdigest(),
            "pair": pair, "executionAdmitted": False, "releaseEligible": False}
