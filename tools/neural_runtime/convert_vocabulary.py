"""Convert DiffSinger phone-to-ID JSON without changing trained IDs.

Prints SEAM vocabulary JSON to stdout; never modifies the source file.
"""
import json
from pathlib import Path
import sys

from inspect_bundle import parse


def convert_vocabulary(payload: bytes) -> bytes:
    mapping = parse(payload, 4 * 1024 * 1024)
    if type(mapping) is not dict or not mapping:
        raise ValueError("Expected a nonempty exported phone-to-ID mapping")
    groups = {}
    for phone, index in mapping.items():
        if not phone or phone == "<PAD>" or any(ord(c) < 32 or ord(c) == 127 for c in phone):
            raise ValueError("Invalid phone name or padding collision")
        if type(index) is not int or not 1 <= index < 65536:
            raise ValueError("Exported phone ID must be a bounded positive integer")
        groups.setdefault(index, []).append(phone)
    if set(groups) != set(range(1, len(groups) + 1)):
        raise ValueError("Exported IDs contain gaps; renumbering is forbidden")
    tokens, aliases = ["<PAD>"], {}
    for index in range(1, len(groups) + 1):
        names = sorted(groups[index])
        tokens.append(names[0])
        aliases.update((name, index) for name in names[1:])
    result = dict(formatId="com.project-seam.neural-vocabulary", schemaVersion=2,
                  tokens=tokens, aliases=aliases)
    # Match formats::stringifyJson: two-space indentation plus final newline.
    encoded = (json.dumps(result, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")
    # The output adds canonical token and alias structure. Check the resulting
    # native parser envelope too, not just the source mapping's byte count.
    parse(encoded, 4 * 1024 * 1024)
    return encoded


def main():
    if len(sys.argv) != 2:
        raise SystemExit("Usage: convert_vocabulary.py EXPORTED.phonemes.json")
    with Path(sys.argv[1]).open("rb") as stream:
        payload = stream.read(4 * 1024 * 1024 + 1)
    sys.stdout.buffer.write(convert_vocabulary(payload))


if __name__ == "__main__":
    main()
