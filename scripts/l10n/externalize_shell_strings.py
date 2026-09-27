#!/usr/bin/env python3
"""Moves the user-visible English literals of the EMO/SCENE shell into its string table.

The shell, its workspaces and its re-homed overlays draw and publish English text. This script
finds those literals in the listed sources, gives each distinct text a stable key, writes the
table (libs/seam-native-ui/include/seam/native_ui/design/shell_strings.def, one
SEAM_SHELL_STRING(Key, "English") line per text) and replaces each literal with tr(Str::Key).
English output is unchanged: the table's English column is the literal exactly as it was written.

It is safe to run again after other changes land: keys already in the table are reused for the
same text, new text gets new keys, and code that already reads the table is left alone. A literal
in a constexpr or static const declaration becomes Str::Key (no lookup: a static would freeze the
first table it saw), so the compiler points at every use that now needs tr(); fix those by hand.

Usage: scripts/l10n/externalize_shell_strings.py [--check]
  --check  exit 1 (and change nothing) if any source still has an unexternalized literal.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
DESIGN = ROOT / "libs/seam-native-ui/src/design"
DESIGN_HEADERS = ROOT / "libs/seam-native-ui/include/seam/native_ui/design"
SOURCES = [
    "sing_shell.cpp",
    "shell_overlays.cpp",
    "tune_workspace.cpp",
    "mix_workspace.cpp",
    "voice_workspace.cpp",
    "character_surface.cpp",
    "tooltip.cpp",
]
# Headers carry display text too (a constant a painter draws); they are scanned the same way.
HEADERS = [
    "character_surface.hpp",
    "shell_overlays.hpp",
    "shell_workspace.hpp",
    "sing_layout.hpp",
    "sing_shell.hpp",
    "tooltip.hpp",
    "voice_workspace.hpp",
]
TABLE = ROOT / "libs/seam-native-ui/include/seam/native_ui/design/shell_strings.def"
INCLUDE = '#include "seam/native_ui/design/shell_strings.hpp"'

# Text preceding a literal that makes it a key, an identifier or a comparison, never display text.
NON_DISPLAY_CONTEXT = re.compile(
    r"(==|!=|starts_with\(|ends_with\(|find\(|getenv\(|findControl\([^)]*,|"
    r"parseDesignMode\(|rfind\(|contains\()\s*$"
)


def lex_literals(text: str):
    """Yields (start, end, body) for each ordinary string literal outside comments."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            i = text.find("\n", i)
            if i < 0:
                return
            continue
        if text.startswith("/*", i):
            i = text.find("*/", i) + 2
            continue
        if c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == "R" and text.startswith('R"', i) and (i == 0 or not text[i - 1].isalnum()):
            m = re.match(r'R"([^(]*)\(', text[i:])
            end = text.find(")" + m.group(1) + '"', i)
            i = end + len(m.group(1)) + 2
            continue
        if c == '"':
            prefix = text[max(0, i - 2):i]
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            if not (prefix.endswith("u8") or prefix[-1:] in ("L", "u", "U")):
                yield i, j + 1, text[i + 1:j]
            i = j + 1
            continue
        if c == "#" and (i == 0 or text[i - 1] == "\n"):
            i = text.find("\n", i)
            if i < 0:
                return
            continue
        i += 1


def grouped(text: str):
    """Adjacent literals (only whitespace between them) are one string."""
    group = []
    for start, end, body in lex_literals(text):
        if group and text[group[-1][1]:start].strip() == "":
            group.append((start, end, body))
            continue
        if group:
            yield group
        group = [(start, end, body)]
    if group:
        yield group


def displayable(body: str) -> bool:
    if not re.search(r"[A-Za-z]", re.sub(r"\\u[0-9A-Fa-f]{4}|\\.", "", body)):
        return False
    if body.startswith("shell.") or "%" in body:
        return False
    if re.fullmatch(r"[a-z0-9_.\-/:+]+", body):
        return False
    if re.fullmatch(r"[A-G]#?", body):  # note names are notation
        return False
    if re.fullmatch(r"[A-Z0-9_]+", body) and "_" in body:  # diagnostic codes
        return False
    return True


def key_for(english: str, table: dict[str, str], used: dict[str, str]) -> str:
    for key, text in table.items():
        if text == english:
            return key
    decoded = re.sub(r"\\u[0-9A-Fa-f]{4}|\\.", " ", english)
    words = re.findall(r"[A-Za-z0-9]+", decoded)[:6]
    base = "".join(w[0].upper() + w[1:] for w in words) or "Text"
    if base[0].isdigit():
        base = "N" + base
    key, suffix = base, 2
    while key in used and used[key] != english:
        key, suffix = f"{base}{suffix}", suffix + 1
    return key


def statement_start(text: str, at: int) -> int:
    return max(text.rfind(";", 0, at), text.rfind("}", 0, at)) + 1


def read_table() -> dict[str, str]:
    table: dict[str, str] = {}
    if TABLE.exists():
        entry = re.compile(r'^SEAM_SHELL_STRING\((\w+),\s*"((?:[^"\\]|\\.)*)"\)$', re.M)
        for m in entry.finditer(TABLE.read_text()):
            table[m.group(1)] = m.group(2)
    return table


def main() -> int:
    check = "--check" in sys.argv
    table = read_table()
    used = dict(table)
    pending = 0
    for path in [DESIGN / name for name in SOURCES] + [DESIGN_HEADERS / name for name in HEADERS]:
        text = path.read_text()
        edits = []
        for group in grouped(text):
            english = "".join(body for _, _, body in group)
            start, end = group[0][0], group[-1][1]
            if not displayable(english):
                continue
            if NON_DISPLAY_CONTEXT.search(text[max(0, start - 60):start]):
                continue
            pending += 1
            key = key_for(english, table, used)
            used[key] = english
            table.setdefault(key, english)
            statement = text[statement_start(text, start):start]
            constexpr = "constexpr" in statement or "static const " in statement
            edits.append((start, end, f"Str::{key}" if constexpr else f"tr(Str::{key})"))
        if check or not edits:
            continue
        for start, end, replacement in reversed(edits):
            text = text[:start] + replacement + text[end:]
        if INCLUDE not in text:
            first = text.find('#include "')
            line_end = text.find("\n", first)
            text = text[:line_end + 1] + INCLUDE + "\n" + text[line_end + 1:]
        path.write_text(text)
    if check:
        if pending:
            print(f"{pending} display literals are not in the shell string table")
        return 1 if pending else 0
    lines = [
        "// The EMO/SCENE shell's user-visible English, one entry per distinct text. Generated and",
        "// extended by scripts/l10n/externalize_shell_strings.py; keys are stable translation keys.",
        "// Included by shell_strings.hpp/.cpp with SEAM_SHELL_STRING(Key, \"English\") defined.",
    ]
    lines += [f'SEAM_SHELL_STRING({key}, "{text}")' for key, text in table.items()]
    TABLE.write_text("\n".join(lines) + "\n")
    print(f"{len(table)} strings in the table")
    return 0


if __name__ == "__main__":
    sys.exit(main())
