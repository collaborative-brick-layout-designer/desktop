#!/usr/bin/env python3
"""Fills translations/bld_en.ts: the English singular and plural of every
"%n" string, so "%n part(s)" reads "1 part" and "3 parts".

The sources say "(s)" (or "(es)") for the plural ending; English needs a
catalog of its own for Qt to pick the right form. Refresh it after adding
"%n" strings:

    lupdate -recursive src -pluralonly -source-language en -target-language en \
        -locations none -no-obsolete -ts translations/bld_en.ts
    python3 scripts/fill-english-plurals.py

Every form is written again each run, so editing a source just works.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path
from xml.sax.saxutils import escape, unescape

TS = Path(__file__).resolve().parent.parent / "translations" / "bld_en.ts"

# Sources whose one form isn't the plural with "(s)" dropped.
SPECIAL = {
    "While you were offline you made %n changes. None of them clash with what changed on the server "
    "meanwhile. Apply them to the live layout, or discard them.": (
        "While you were offline you made %n change. It doesn't clash with what changed on the server "
        "meanwhile. Apply it to the live layout, or discard it."
    ),
    "%1 (%n times)": "%1 (once)",
    "%n of these parts are on %1 too. Update them there so everyone sees the new versions?": (
        "%n of these parts is on %1 too. Update it there so everyone sees the new version?"
    ),
    "%n parts in this layout come from %1. People on %2 will see them as missing.": (
        "%n part in this layout comes from %1. People on %2 will see it as missing."
    ),
    "%n parts in this layout come from other servers (%1). People on %2 will see them as missing.": (
        "%n part in this layout comes from other servers (%1). People on %2 will see it as missing."
    ),
    "This layout has its own version of %n parts you already have. Choose which to use for each.": (
        "This layout has its own version of %n part you already have. Choose which to use."
    ),
}


def forms(source: str) -> tuple[str, str]:
    if source in SPECIAL:
        return SPECIAL[source], source
    one = source.replace("(es)", "").replace("(s)", "")
    many = source.replace("(es)", "es").replace("(s)", "s")
    return one, many


def main() -> int:
    text = TS.read_text(encoding="utf-8")
    message = re.compile(r'(<message numerus="yes">\s*<source>)(.*?)(</source>.*?<translation)[^>]*>.*?(</translation>)', re.S)

    def fill(m: re.Match) -> str:
        source = unescape(m.group(2), {"&quot;": '"', "&apos;": "'"})
        one, many = forms(source)
        body = (
            "\n            <numerusform>%s</numerusform>\n            <numerusform>%s</numerusform>\n        "
            % (escape(one), escape(many))
        )
        return m.group(1) + m.group(2) + m.group(3) + ">" + body + m.group(4)

    text, n = message.subn(fill, text)
    TS.write_text(text, encoding="utf-8")
    print(f"{n} English plurals filled in {TS.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
