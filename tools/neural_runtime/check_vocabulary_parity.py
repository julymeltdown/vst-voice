"""Compare native and offline conversion bytes; uses the test helper, not ORT."""
import json
from pathlib import Path
import random
import subprocess
import sys

from convert_vocabulary import convert_vocabulary


def main():
    binary = str(Path(sys.argv[1]).resolve())
    rng = random.Random(827)
    valid = [{"SP": 1, "AP": 2, "ja/a": 3, "ko/a": 3, "en/aa": 3},
             {"가": 1, "あ": 1, "é": 2, "😀": 2}, {"a" * 128: 1}]
    for count in (1, 2, 7, 64, 512):
        rows = [(f"phone/{index}", index) for index in range(1, count + 1)]
        rows += [(f"alias/{index}", index) for index in range(1, count + 1)]
        rng.shuffle(rows)
        valid.append(dict(rows))
    for mapping in valid:
        for ascii_only in (False, True):
            source = json.dumps(mapping, ensure_ascii=ascii_only).encode()
            result = subprocess.run([binary, "--seam-convert-vocabulary-probe"], input=source,
                                    capture_output=True, timeout=20)
            assert result.returncode == 0, result.stderr
            assert result.stdout == convert_vocabulary(source), "Canonical converter bytes differ"
    invalid = [b'{"a":1,"a":2}', b'{"a":true}', b'{"a":1.0}', b'{"a":2}',
               b'{"a":0}', b'{"a":65536}', b'{"<PAD>":1}', b'{"bad\\n":1}',
               b'{}', b'[]', b'{"a":NaN}', json.dumps({"a" * 129: 1}).encode()]
    for source in invalid:
        try:
            convert_vocabulary(source)
        except ValueError:
            pass
        else:
            raise AssertionError("Offline converter admitted invalid source")
        result = subprocess.run([binary, "--seam-convert-vocabulary-probe"], input=source,
                                capture_output=True, timeout=20)
        assert result.returncode == 8, (source, result.returncode, result.stderr)
    print(f"Native/offline vocabulary parity passed: {len(valid) * 2} byte comparisons, {len(invalid)} rejection cases.")


if __name__ == "__main__":
    main()
