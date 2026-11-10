#!/usr/bin/env python3
"""Fails when translations/*.ts don't match the source: a string marked for
translation that no .ts file has (it would always show in English), or a
.ts entry for a string the source no longer has.

Runs lupdate on a copy of each file and compares the strings (by context
and source), not the files, so a different lupdate version doesn't matter.

    scripts/ci/check-translations.py [path to lupdate]

Fix what it reports by running lupdate (docs/TRANSLATING.md).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LANGUAGES = ["de", "es", "fr", "it", "ja", "nl", "no", "pt", "sv", "zh"]


def find_lupdate(arg: str | None) -> str:
    candidates = [arg] if arg else []
    if os.environ.get("QT_ROOT_DIR"):
        candidates.append(str(Path(os.environ["QT_ROOT_DIR"]) / "bin" / "lupdate"))
    candidates += ["/usr/lib/qt6/bin/lupdate", shutil.which("lupdate6") or "", shutil.which("lupdate") or ""]
    for c in candidates:
        if c and Path(c).exists():
            return c
    sys.exit("check-translations: no lupdate found (pass its path)")


def keys(ts: Path) -> set[tuple[str, str, bool]]:
    out = set()
    for context in ET.parse(ts).getroot().iter("context"):
        name = context.findtext("name") or ""
        for m in context.iter("message"):
            t = m.find("translation")
            if t is not None and t.get("type") in ("vanished", "obsolete"):
                continue
            out.add((name, m.findtext("source") or "", m.get("numerus") == "yes"))
    return out


def run(lupdate: str, ts: Path, extra: list[str]) -> None:
    subprocess.run([lupdate, "-silent", "-recursive", str(ROOT / "src"), "-ts", str(ts),
                    "-locations", "none", "-no-obsolete", *extra],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main() -> int:
    lupdate = find_lupdate(sys.argv[1] if len(sys.argv) > 1 else None)
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        jobs = [(code, []) for code in LANGUAGES]
        jobs.append(("en", ["-pluralonly", "-source-language", "en", "-target-language", "en"]))
        for code, extra in jobs:
            mine = ROOT / f"translations/bld_{code}.ts"
            fresh = Path(tmp) / mine.name
            shutil.copy(mine, fresh)
            run(lupdate, fresh, extra)
            have, want = keys(mine), keys(fresh)
            for ctx, src, _ in sorted(want - have):
                problems.append(f"{mine.name}: missing [{ctx}] {src[:90]!r}")
            for ctx, src, _ in sorted(have - want):
                problems.append(f"{mine.name}: no longer in the source [{ctx}] {src[:90]!r}")
    if problems:
        print("\n".join(problems[:60]))
        if len(problems) > 60:
            print(f"... and {len(problems) - 60} more")
        print("\nThe .ts files are out of step with the source: run lupdate (docs/TRANSLATING.md).")
        return 1
    print(f"check-translations: {len(LANGUAGES) + 1} .ts files match the source")
    return 0


if __name__ == "__main__":
    sys.exit(main())
