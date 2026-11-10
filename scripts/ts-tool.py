#!/usr/bin/env python3
"""Small helpers for translations/bld_<code>.ts (docs/TRANSLATING.md).

    ts-tool.py export <code> [--all] > todo.json
        The strings still to translate (or, with --all, every string), as
        {"source": "", ...}; a "%n" string's value is a list of its forms.
    ts-tool.py fill <code> done.json [--keep]
        Writes the translations in done.json (the same shape) into every
        message with that source, in every context, and marks them done.
        Empty values are skipped; with --keep, so are messages already done.
    ts-tool.py check <code>
        Clears (back to unfinished) translations that lost a placeholder
        (%1, %n) or an Alt letter (&File), or say "layer" for a sheet, so
        the app shows English there rather than something wrong.

The file is edited as text, so lupdate's layout is kept.
"""

from __future__ import annotations

import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MESSAGE = re.compile(r"(?P<head><message(?P<numerus> numerus=\"yes\")?>\s*<source>(?P<source>.*?)</source>\s*"
                     r"(?:<comment>.*?</comment>\s*)?)<translation(?P<attrs>[^>]*)>(?P<body>.*?)</translation>", re.S)
FORM = re.compile(r"<numerusform>(.*?)</numerusform>", re.S)
LAYER = {
    "de": r"Ebene", "es": r"\bcapas?\b", "fr": r"calque|\bcouches?\b", "it": r"\blivell[oi]\b",
    "ja": r"レイヤ", "nl": r"\blag(en)?\b", "no": r"\blag(et|ene)?\b", "pt": r"\bcamadas?\b",
    "sv": r"\blag(er|ret|ren)\b", "zh": r"图层",
}
SHEET = re.compile(r"\bsheets?\b|\barea\b", re.I)
# Forms per number: Qt's rules for these languages.
NUMERUS = {"ja": 1, "zh": 1}


def unescape(s: str) -> str:
    return html.unescape(s)


def escape(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;").replace("'", "&apos;")


def ts_path(code: str) -> Path:
    return ROOT / f"translations/bld_{code}.ts"


def placeholders(s: str) -> set[str]:
    return set(re.findall(r"%(?:\d|n)", s))


def has_mnemonic(s: str) -> bool:
    i = 0
    while i + 1 < len(s):
        if s[i] == "&":
            if s[i + 1] == "&":
                i += 2
                continue
            if s[i + 1].isalnum():
                return True
        i += 1
    return False


def problems(code: str, source: str, forms: list[str], numerus: bool) -> list[str]:
    out = []
    want = placeholders(source) | ({"%n"} if numerus else set())
    for f in forms:
        got = placeholders(f) | ({"%n"} if numerus else set())
        if got != want:
            out.append("placeholders")
        if has_mnemonic(source) and not has_mnemonic(f):
            out.append("Alt letter")
        if SHEET.search(source) and re.search(LAYER.get(code, r"$^"), f, re.I):
            out.append("layer")
    return out


def export(code: str, everything: bool) -> int:
    text = ts_path(code).read_text(encoding="utf-8")
    todo: dict[str, object] = {}
    for m in MESSAGE.finditer(text):
        done = "type=" not in m.group("attrs")
        if done and not everything:
            continue
        source = unescape(m.group("source"))
        todo.setdefault(source, [""] * NUMERUS.get(code, 2) if m.group("numerus") else "")
    json.dump(todo, sys.stdout, ensure_ascii=False, indent=1)
    print()
    return 0


def fill(code: str, done_path: str, keep: bool = False) -> int:
    done = json.loads(Path(done_path).read_text(encoding="utf-8"))
    path = ts_path(code)
    text = path.read_text(encoding="utf-8")
    count = 0
    skipped = []

    def sub(m: re.Match) -> str:
        nonlocal count
        source = unescape(m.group("source"))
        value = done.get(source)
        if not value or (keep and "type=" not in m.group("attrs")):
            return m.group(0)
        numerus = bool(m.group("numerus"))
        forms = value if isinstance(value, list) else [value]
        if numerus and len(forms) == 1 and NUMERUS.get(code, 2) == 2:
            forms = forms * 2
        if not all(f.strip() for f in forms):
            return m.group(0)
        bad = problems(code, source, forms, numerus)
        if bad:
            skipped.append(f"{source[:60]!r}: {', '.join(sorted(set(bad)))}")
            return m.group(0)
        count += 1
        if numerus:
            body = "".join(f"\n            <numerusform>{escape(f)}</numerusform>" for f in forms) + "\n        "
        else:
            body = escape(forms[0])
        return m.group("head") + "<translation>" + body + "</translation>"

    text = MESSAGE.sub(sub, text)
    path.write_text(text, encoding="utf-8")
    print(f"{path.name}: {count} messages filled")
    for s in skipped:
        print("  skipped", s)
    return 0


def check(code: str) -> int:
    path = ts_path(code)
    text = path.read_text(encoding="utf-8")
    cleared = []

    def sub(m: re.Match) -> str:
        if "type=" in m.group("attrs"):
            return m.group(0)
        source = unescape(m.group("source"))
        numerus = bool(m.group("numerus"))
        forms = [unescape(f) for f in FORM.findall(m.group("body"))] if numerus else [unescape(m.group("body"))]
        bad = problems(code, source, forms, numerus)
        if not bad:
            return m.group(0)
        cleared.append(f"{source[:60]!r}: {', '.join(sorted(set(bad)))}")
        empty = "".join("\n            <numerusform></numerusform>" for _ in forms) + "\n        " if numerus else ""
        return m.group("head") + '<translation type="unfinished">' + empty + "</translation>"

    text = MESSAGE.sub(sub, text)
    path.write_text(text, encoding="utf-8")
    print(f"{path.name}: {len(cleared)} cleared")
    for c in cleared:
        print("  ", c)
    return 0


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    cmd, code = sys.argv[1], sys.argv[2]
    if cmd == "export":
        return export(code, "--all" in sys.argv[3:])
    if cmd == "fill" and len(sys.argv) > 3:
        return fill(code, sys.argv[3], "--keep" in sys.argv[4:])
    if cmd == "check":
        return check(code)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
