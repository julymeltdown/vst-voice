"""Frozen English recording vocabulary and explicit draft stress contracts."""

from __future__ import annotations

import hashlib
from typing import Any

VOCABULARY_ID = "seam-en-arpabet-v1"
CONSONANTS = ["p", "b", "t", "d", "k", "g", "m", "n", "ng", "f", "v", "th", "dh", "s", "z",
              "sh", "zh", "hh", "ch", "j", "l", "r", "w", "y"]
VOWELS = ["aa", "ae", "ah", "ao", "aw", "ax", "axr", "ay", "eh", "er", "ey", "ih", "iy",
          "ow", "oy", "uh", "uw"]
VOCABULARY = CONSONANTS + [symbol for vowel in VOWELS for symbol in (vowel, vowel+"0", vowel+"1", vowel+"2")] + ["pau"]
VOCABULARY_SHA256 = hashlib.sha256((VOCABULARY_ID+"\n"+"\n".join(VOCABULARY)+"\n").encode()).hexdigest()
DEFAULT_CONTRACT = {"vocabularyId": VOCABULARY_ID, "vocabularySha256": VOCABULARY_SHA256,
                    "stressPolicy": "FOLD_LEXICAL_STRESS"}
DEFAULTS = {"language": "en", "vowels": VOWELS, "consonants": CONSONANTS,
            "specialPhones": CONSONANTS + ["pau"],
            "includeKinds": ["sustain", "release", "special", "cv", "vc", "vv"],
            "languageContract": DEFAULT_CONTRACT}


def validate_english_profile(profile: dict[str, Any]) -> None:
    contract = profile.get("languageContract")
    if not isinstance(contract, dict) or set(contract) != set(DEFAULT_CONTRACT) or \
            contract.get("vocabularyId") != VOCABULARY_ID or contract.get("vocabularySha256") != VOCABULARY_SHA256 or \
            contract.get("stressPolicy") not in ("FOLD_LEXICAL_STRESS", "DISTINGUISH_LEXICAL_STRESS"):
        raise ValueError("English languageContract must bind the frozen vocabulary and a supported stress policy")
    if set(profile["includeKinds"]) - set(DEFAULTS["includeKinds"]):
        raise ValueError("English inventory kinds cannot use Japanese breath or glottal tokens")
    vowels = {symbol for vowel in VOWELS for symbol in (vowel, vowel+"0", vowel+"1", vowel+"2")}
    if not set(profile["vowels"]) <= vowels or not set(profile["consonants"]) <= set(CONSONANTS) or \
            not set(profile["specialPhones"]) <= set(CONSONANTS+["pau"]):
        raise ValueError("English profile phone classes must use the frozen vocabulary")
    if contract["stressPolicy"] == "FOLD_LEXICAL_STRESS" and not set(profile["vowels"]) <= set(VOWELS):
        raise ValueError("Stress-folded English inventories require bare vowel labels")
