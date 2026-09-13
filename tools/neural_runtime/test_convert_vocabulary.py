import json
import unittest

from convert_vocabulary import convert_vocabulary


class ConversionTests(unittest.TestCase):
    def test_exact_ids_and_aliases(self):
        original = {"SP": 1, "ja/a": 3, "ko/a": 3, "AP": 2, "en/aa": 3}
        result = json.loads(convert_vocabulary(json.dumps(original).encode()))
        self.assertEqual(result["tokens"], ["<PAD>", "SP", "AP", "en/aa"])
        recovered = {phone: index for index, phone in enumerate(result["tokens"]) if index}
        recovered.update(result["aliases"])
        self.assertEqual(recovered, original)
        self.assertEqual(convert_vocabulary(json.dumps(original).encode()),
                         convert_vocabulary(json.dumps(dict(reversed(list(original.items())))).encode()))

    def test_rejects_renumbering_and_bad_ids(self):
        for source in ({"a": 2}, {"a": 0}, {"a": -1}, {"a": True}, {"a": 1.5},
                       {"a": 65536}, {"<PAD>": 1}, {"bad\n": 1}, {}, []):
            with self.assertRaises(ValueError):
                convert_vocabulary(json.dumps(source).encode())

    def test_duplicate_json_and_token_bytes(self):
        for source in (b'{"a":1,"a":2}', json.dumps({"a" * 129: 1}).encode()):
            with self.assertRaises(ValueError):
                convert_vocabulary(source)


if __name__ == "__main__":
    unittest.main()
