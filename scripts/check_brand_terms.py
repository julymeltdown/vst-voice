#!/usr/bin/env python3
"""Brand-term gate for everything Project SEAM ships (redesign plan section 14.6).

No real brand, band, label, retailer, social network, messenger or music-software product name may
appear in the design assets' metadata, the shell's string tables, bundle and installer manifests or
the documents copied into a release. This script scans exactly those files for a curated deny-list
and fails with one line per hit.

Matching is case-insensitive and on word boundaries: a term matches only where it is not part of a
longer word ("Vans" does not match "caravans"). Separators between the words of a multi-word term
may be spaces, hyphens, underscores or nothing,
so "Fall Out Boy", "fall-out-boy" and "falloutboy" are one term. A trailing plural "s" also
matches, because a brand used as a plural is still the brand.

Terms that are plain English words as well ("Supreme", "Journeys", "Signal", "Line", "Thursday",
"The Used", "Brand New") are deliberately not on the list: scanning for them would flag ordinary
prose, and the art review (docs/design/NATIVE_EDITOR_DESIGN_SYSTEM.md section 12) covers them.

Legitimate uses are listed in scripts/brand_terms_allowlist.json. Every entry names the term, the
files it may appear in (glob patterns relative to the root) and why. An allowlisted term is
allowed only in those files: a plug-in format name in the user manual does not make it acceptable
in the string table.

Usage: check_brand_terms.py [--root DIR] [--allowlist FILE] [--list-files]
Exit status 0 when clean, 1 when a term was found, 2 on a usage or configuration error.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

# The curated deny-list, by category. Keep entries specific enough that they cannot be ordinary
# words; see the module docstring for what is deliberately left out.
DENY_TERMS: dict[str, tuple[str, ...]] = {
    "footwear-apparel-retail": (
        "Converse", "Chuck Taylor", "Vans", "Dr. Martens", "Doc Martens", "Nike", "Adidas",
        "Reebok", "New Balance", "Skechers", "Dickies", "Hot Topic", "Urban Outfitters", "Zumiez",
        "Hollister", "Abercrombie", "Uniqlo", "Killstar", "Tripp NYC", "Iron Fist Clothing",
        "Sanrio", "Hello Kitty", "Kuromi", "Etnies", "DC Shoes", "Emily the Strange",
    ),
    "band": (
        "My Chemical Romance", "Fall Out Boy", "Paramore", "Panic! at the Disco",
        "Panic at the Disco", "Taking Back Sunday", "Dashboard Confessional", "Jimmy Eat World",
        "Sunny Day Real Estate", "American Football", "Hawthorne Heights", "Silverstein",
        "Senses Fail", "Underoath", "Saosin", "Pierce the Veil", "Sleeping with Sirens",
        "Bring Me the Horizon", "Black Veil Brides", "Escape the Fate", "Blood on the Dance Floor",
        "Brokencyde", "Asking Alexandria", "Attack Attack", "Blink-182", "Green Day", "Sum 41",
        "Good Charlotte", "Simple Plan", "New Found Glory", "Yellowcard", "All Time Low",
        "Mayday Parade", "Motion City Soundtrack", "Cute Is What We Aim For", "Hey Monday",
        "3OH!3", "Avenged Sevenfold", "Linkin Park", "Evanescence", "Mindless Self Indulgence",
        "Dance Gavin Dance", "Chiodos", "The Devil Wears Prada", "Neck Deep", "State Champs",
    ),
    "record-label-tour": (
        "Fueled by Ramen", "Drive-Thru Records", "Victory Records", "Epitaph Records",
        "Vagrant Records", "Hopeless Records", "Rise Records", "Equal Vision Records",
        "Warped Tour", "Sumerian Records", "Fearless Records",
    ),
    "daw-vocal-synth-audio-software": (
        "FL Studio", "Fruity Loops", "Image-Line", "Ableton", "Logic Pro", "GarageBand",
        "Pro Tools", "Avid", "Cubase", "Nuendo", "Steinberg", "Studio One", "PreSonus", "REAPER",
        "Cockos", "Bitwig", "Reason Studios", "Audacity", "Vocaloid", "UTAU", "OpenUtau",
        "Synthesizer V", "Dreamtonics", "CeVIO", "Piapro", "Crypton", "Hatsune Miku", "Kagamine",
        "Megurine Luka", "Kasane Teto", "Yamaha", "Melodyne", "Celemony", "Auto-Tune", "Antares",
        "iZotope", "Native Instruments", "Kontakt", "Xfer Serum", "Spitfire Audio", "Waves Audio",
        "Roland", "Korg",
    ),
    "plugin-format-trademark": ("VST", "VST2", "VST3", "Audio Units", "AAX"),
    "social-network-messenger-streaming": (
        "Myspace", "Facebook", "Instagram", "TikTok", "Twitter", "Tumblr", "Snapchat", "YouTube",
        "Discord", "WhatsApp", "Telegram", "KakaoTalk", "WeChat", "Skype", "Reddit", "Twitch",
        "SoundCloud", "Bandcamp", "Spotify", "Apple Music", "iTunes", "LiveJournal", "Xanga",
        "DeviantArt", "Pinterest", "PureVolume", "Bluesky", "Mastodon", "Weibo", "Line Messenger",
    ),
}

# What ships: the files a release copies into a bundle or an installer payload, and the metadata
# that describes them. Developer-only documents (the root README, plans, reports, build patches)
# are not scanned; they never reach a user.
SCAN_GLOBS: tuple[str, ...] = (
    # Design and character assets: every text file, and every file name.
    "assets/ui-design/**",
    "assets/fonts/**",
    "assets/character-01/*.json",
    "assets/character-01/*.md",
    "assets/character-01/**/*.json",
    "assets/character-01/runtime/**",
    "assets/demo-human-voicebank-public-domain/*.json",
    "assets/demo-human-voicebank-public-domain/*.md",
    # The shell's string table and any translation table beside it.
    "libs/seam-native-ui/include/seam/native_ui/design/shell_strings*.def",
    "libs/seam-native-ui/**/l10n/**",
    "resources/l10n/**",
    # Bundle manifests and installer metadata.
    "packaging/release-resource-inventory.json",
    "packaging/macos/*.plist",
    "packaging/macos/*.plist.in",
    "packaging/macos/Distribution.xml.in",
    "packaging/macos/installer-ownership.json",
    "packaging/macos/*.entitlements",
    "packaging/windows/*.nsi",
    "packaging/windows/*.rc.in",
    "packaging/windows/*.manifest",
    "packaging/windows/installer-ownership.json",
    "packaging/voicebanks/*.json",
    # Documents copied into the bundles and the installer payloads (CMakeLists.txt POST_BUILD).
    "docs/manual/**",
    "docs/support/**",
    "docs/product/EXTERNAL_BETA_ACCEPTANCE.md",
    "docs/product/external-beta-documentation.json",
    "THIRD_PARTY_NOTICES.md",
)

TEXT_SUFFIXES = {
    ".json", ".md", ".txt", ".def", ".in", ".plist", ".xml", ".nsi", ".manifest", ".entitlements",
    ".strings", ".po", ".toml", ".yaml", ".yml", ".csv",
}
MAX_TEXT_BYTES = 4 * 1024 * 1024


@dataclass(frozen=True)
class Term:
    text: str
    category: str
    pattern: re.Pattern[str]


@dataclass(frozen=True)
class Hit:
    path: str
    line: int
    term: str
    category: str
    excerpt: str


def compile_term(term: str) -> re.Pattern[str]:
    """Word-boundary, case-insensitive; words may be joined by space, hyphen, underscore or nothing."""
    words = [w for w in re.split(r"[\s\-_]+", term.strip()) if w]
    if not words:
        raise ValueError("empty term")
    body = r"[\s\-_]*".join(re.escape(w) for w in words)
    return re.compile(r"(?<![0-9A-Za-z])" + body + r"s?(?![0-9A-Za-z])", re.IGNORECASE)


def deny_terms() -> list[Term]:
    terms: list[Term] = []
    for category, entries in DENY_TERMS.items():
        for entry in entries:
            terms.append(Term(entry, category, compile_term(entry)))
    return terms


@dataclass(frozen=True)
class Allowance:
    term: str
    paths: tuple[str, ...]
    reason: str


def load_allowlist(path: Path, known: set[str]) -> list[Allowance]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schemaVersion") != 1:
        raise ValueError(f"{path}: expected schemaVersion 1")
    result: list[Allowance] = []
    for index, entry in enumerate(data.get("allow", [])):
        term = entry.get("term")
        paths = entry.get("paths")
        reason = entry.get("reason")
        if not isinstance(term, str) or term.lower() not in known:
            raise ValueError(f"{path}: allow[{index}] names a term that is not on the deny-list")
        if not isinstance(paths, list) or not paths or not all(isinstance(p, str) for p in paths):
            raise ValueError(f"{path}: allow[{index}] needs a non-empty list of path globs")
        if not isinstance(reason, str) or len(reason.strip()) < 12:
            raise ValueError(f"{path}: allow[{index}] needs a reason")
        result.append(Allowance(term, tuple(paths), reason))
    return result


def allowed(allowances: list[Allowance], term: str, relative: str) -> bool:
    for allowance in allowances:
        if allowance.term.lower() != term.lower():
            continue
        if any(fnmatch.fnmatchcase(relative, pattern) for pattern in allowance.paths):
            return True
    return False


def shipped_files(root: Path) -> list[Path]:
    found: set[Path] = set()
    for pattern in SCAN_GLOBS:
        for path in root.glob(pattern):
            if path.is_file() and ".git" not in path.parts:
                found.add(path)
    return sorted(found)


def scan(root: Path, allowances: list[Allowance], files: list[Path] | None = None) -> list[Hit]:
    terms = deny_terms()
    hits: list[Hit] = []
    for path in files if files is not None else shipped_files(root):
        relative = path.relative_to(root).as_posix()
        # The file's own name is shipped too (an asset called after a brand is a brand in the bundle).
        lines: list[tuple[int, str]] = [(0, relative)]
        if path.suffix.lower() in TEXT_SUFFIXES and path.stat().st_size <= MAX_TEXT_BYTES:
            text = path.read_text(encoding="utf-8", errors="replace")
            lines.extend(enumerate(text.splitlines(), start=1))
        for number, line in lines:
            for term in terms:
                match = term.pattern.search(line)
                if match is None or allowed(allowances, term.text, relative):
                    continue
                start = max(0, match.start() - 30)
                hits.append(Hit(relative, number, term.text, term.category,
                                line[start:match.end() + 30].strip()))
    return hits


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    here = Path(__file__).resolve().parent
    parser.add_argument("--root", type=Path, default=here.parent)
    parser.add_argument("--allowlist", type=Path, default=here / "brand_terms_allowlist.json")
    parser.add_argument("--list-files", action="store_true", help="print the scanned files")
    args = parser.parse_args(argv)
    root = args.root.resolve()
    try:
        known = {term.text.lower() for term in deny_terms()}
        allowances = load_allowlist(args.allowlist, known)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"check_brand_terms: {error}", file=sys.stderr)
        return 2
    files = shipped_files(root)
    if args.list_files:
        for path in files:
            print(path.relative_to(root).as_posix())
    if not files:
        print("check_brand_terms: no shipped files found under the root", file=sys.stderr)
        return 2
    hits = scan(root, allowances, files)
    for hit in hits:
        where = f"{hit.path}:{hit.line}" if hit.line else f"{hit.path} (file name)"
        print(f"{where}: '{hit.term}' ({hit.category}): {hit.excerpt}")
    print(f"check_brand_terms: {len(files)} shipped files, {len(hits)} brand-term hits")
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
