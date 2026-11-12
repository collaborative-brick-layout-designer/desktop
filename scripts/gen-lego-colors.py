#!/usr/bin/env python3
"""Build the LEGO color list the theme color picker offers.

    gen-lego-colors.py LDConfig.ldr StudioColorDefinition.txt \\
        colors.csv color_mappings.csv --ts OUT.ts --cpp OUT.cpp [--report OUT.md]

The web repo keeps this script in packages/model/scripts and the desktop
repo in scripts/, word for word; one run writes both apps' lists, so they
stay the same.

Sources (read once by hand, the output is committed):
  * LDConfig.ldr from the LDraw parts library (the one the desktop app
    downloads; CC BY 2.0, LDraw.org): the LEGO color number and LEGO name
    (its "// LEGOID nn - Name" comments, which LDraw took from LEGO Digital
    Designer), the RGB value, and which kind of color it is.
  * Rebrickable's public catalog downloads, colors.csv and
    color_mappings.csv (https://rebrickable.com/downloads/, served from
    https://cdn.rebrickable.com/media/downloads/; no account or key).
    Their terms: "Free for personal and commercial use, provided you
    acknowledge Rebrickable as your data source", so both apps say so
    where the list is used (the "Any LEGO color" help) and in the
    READMEs. color_mappings.csv links each Rebrickable color to its LEGO
    (LE) and BrickLink (BL) numbers and names.
  * BrickLink Studio's data/StudioColorDefinition.txt: the BrickLink color
    number and name for a LEGO number (rows marked "o", Studio's one-to-one
    matches).

The BrickLink number for a LEGO number, by this rule:
  1. Rebrickable's, when a Rebrickable color has the LEGO number as its own
     (an LE row marked is_export_id; the import-only rows are nearest-color
     guesses) and that color has a BrickLink number, with Rebrickable's
     BrickLink name. Rebrickable keeps these against BrickLink, so it wins
     where it and Studio disagree.
  2. Otherwise Studio's one-to-one row, with Studio's name.
  3. Otherwise none (0 and an empty name).
--report writes every LEGO color where the two sources differ, and the
ones left without a match.

Kept: LDraw's solid, pearlescent and metallic colors that have a LEGO
number. Left out: transparent, chrome, glitter, speckle, milky, opalescent,
fluorescent, rubber, fabric and Modulex colors (a see-through or sparkly
color has no single flat color to theme an app with), and the conductive
and metallic-ink colors. Pearl and metallic colors are used as their flat
base color.
"""
import argparse, csv, json, re

KEEP = {'LDraw Solid Colours': 'solid', 'LDraw Pearlescent Plastic Colours': 'pearl', 'LDraw Metallic Paint Colours': 'pearl'}
SKIP_NAME = re.compile(r'Ink|Conductive', re.I)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ldconfig'); ap.add_argument('studio')
    ap.add_argument('rb_colors'); ap.add_argument('rb_mappings')
    ap.add_argument('--ts'); ap.add_argument('--cpp'); ap.add_argument('--report')
    a = ap.parse_args()
    cur = None; lego = None; colors = []; seen = set()
    for line in open(a.ldconfig, encoding='utf-8'):
        m = re.match(r'0 // (LDraw .* Colours)\s*$', line)
        if m:
            cur = KEEP.get(m.group(1)); continue
        m = re.match(r'0\s+// LEGOID\s+(\d+)(?:\s*/\s*\d+)* - (.+?)\s*$', line)
        if m:
            lego = (int(m.group(1)), m.group(2)); continue
        m = re.match(r'0 !COLOUR (\S+)\s+CODE\s+(\d+)\s+VALUE #([0-9A-Fa-f]{6})', line)
        if m:
            if cur and lego and lego[0] not in seen and not SKIP_NAME.search(lego[1]):
                seen.add(lego[0])
                colors.append({'id': lego[0], 'lego': lego[1], 'hex': '#' + m.group(3).upper(), 'kind': cur})
            lego = None
    studio = {}
    with open(a.studio, encoding='utf-8') as f:
        r = csv.reader(f, delimiter='\t'); next(r)
        for row in r:
            if len(row) > 12 and row[3] and row[1] and row[12].strip() == 'o':
                studio.setdefault(int(row[3]), (int(row[1]), row[5].strip()))
    rb = rebrickable(a.rb_colors, a.rb_mappings)
    report = []
    for c in colors:
        st, r = studio.get(c['id']), rb.get(c['id'])
        rbl = r['bl'] if r else None
        c['blId'], c['bl'] = rbl or st or (0, '')
        if st == rbl:
            continue
        if not st:
            why = 'Filled from Rebrickable (Studio has no match)'
        elif not rbl:
            why = 'Studio kept (Rebrickable has no BrickLink match)'
        elif st[0] == rbl[0]:
            why = "Same BrickLink color, Rebrickable's name"
        else:
            why = "Different BrickLink colors: Rebrickable's"
        report.append((c, st, r, why))
    colors.sort(key=lambda c: (c['kind'] != 'solid', c['lego'].lower()))
    if a.ts:
        with open(a.ts, 'w', encoding='utf-8') as o:
            o.write(TS_HEAD)
            for c in colors:
                o.write(f"  [{c['id']}, {js(c['lego'])}, {c['blId']}, {js(c['bl'])}, '{c['hex']}', '{c['kind']}'],\n")
            o.write(TS_TAIL)
    if a.cpp:
        with open(a.cpp, 'w', encoding='utf-8') as o:
            o.write(CPP_HEAD)
            for c in colors:
                o.write(f"    {{ {c['id']}, \"{c['lego']}\", {c['blId']}, \"{c['bl']}\", \"{c['hex']}\", {'true' if c['kind'] == 'pearl' else 'false'} }},\n")
            o.write(CPP_TAIL)
    if a.report:
        with open(a.report, 'w', encoding='utf-8') as o:
            o.write('| LEGO color | Studio says | Rebrickable says | Used |\n|---|---|---|---|\n')
            for c, st, r, why in sorted(report, key=lambda t: t[0]['id']):
                o.write(f"| {c['lego']} ({c['id']}) | {named(st)} | {rb_text(r)} | {why} |\n")
            for c in sorted(colors, key=lambda c: c['id']):
                if not c['bl']:
                    o.write(f"| {c['lego']} ({c['id']}) | none | {rb_text(rb.get(c['id']))} | no BrickLink match |\n")
    print(len(colors), 'colors;', sum(1 for c in colors if not c['bl']), 'without a BrickLink match;',
          len(report), 'where Studio and Rebrickable differ')


def rebrickable(colors_csv, mappings_csv):
    """LEGO number -> {'rb': (Rebrickable number, name), 'bl': (BrickLink number, name) or None}."""
    names = {}
    with open(colors_csv, encoding='utf-8') as f:
        for row in csv.DictReader(f):
            names[row['id']] = row['name']
    le, bl = {}, {}
    with open(mappings_csv, encoding='utf-8') as f:
        for row in csv.DictReader(f):
            if row['is_export_id'] != 'True':
                continue
            if row['ext_system'] == 'LE':
                le.setdefault(int(row['ext_id']), []).append(row['id'])
            elif row['ext_system'] == 'BL':
                descr = json.loads(row['ext_descr'] or '[]')
                bl.setdefault(row['id'], (int(row['ext_id']), descr[0] if descr else ''))
    out = {}
    for lego_id, rb_ids in le.items():
        found = {bl[i] for i in rb_ids if i in bl}
        # Two Rebrickable colors with one LEGO number count only when they agree.
        out[lego_id] = {'rb': (rb_ids[0], names.get(rb_ids[0], '?')), 'bl': found.pop() if len(found) == 1 else None}
    return out


def named(b):
    return f'{b[1]} ({b[0]})' if b else 'none'


def rb_text(r):
    if not r:
        return 'no color with this LEGO number'
    return f"{named(r['bl']) if r['bl'] else 'no BrickLink match'}, for its {r['rb'][1]}"


def js(s):
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


TS_HEAD = '''// LEGO colors for the theme color picker (Settings > Color). Generated by
// gen-lego-colors.py (here in packages/model/scripts, and in the desktop
// repo's scripts/); don't edit by hand. The desktop app has the same list
// (src/ui/theme/LegoColors.cpp).
//
// From LDraw's LDConfig.ldr (LEGO numbers, LEGO names and RGB; CC BY 2.0,
// LDraw.org). The BrickLink numbers and names: data from Rebrickable
// (rebrickable.com, its catalog downloads), and BrickLink Studio's color
// table where Rebrickable has none. Only solid, pearl and metallic colors: transparent, chrome,
// glitter and other see-through or sparkly colors are left out, and pearl
// and metallic colors are used as their flat color.

export interface LegoColor {
  /** LEGO's own color number (what the setting stores). */
  id: number;
  /** LEGO's name, e.g. "Bright Yellow". */
  lego: string;
  /** BrickLink's color number, or 0 when BrickLink has no match. */
  blId: number;
  /** BrickLink's name, e.g. "Yellow"; empty when BrickLink has no match. */
  bl: string;
  /** The color, #RRGGBB. */
  hex: string;
  /** Pearl and metallic colors show as their flat color. */
  kind: 'solid' | 'pearl';
}

type Row = [number, string, number, string, string, 'solid' | 'pearl'];

const ROWS: Row[] = [
'''
TS_TAIL = '''];

/** Every color, solid ones first, each group by LEGO name. */
export const LEGO_COLORS: readonly LegoColor[] = ROWS.map(([id, lego, blId, bl, hex, kind]) => ({ id, lego, blId, bl, hex, kind }));

const BY_ID = new Map(LEGO_COLORS.map((c) => [c.id, c]));

export function legoColor(id: number | null | undefined): LegoColor | undefined {
  return id === null || id === undefined ? undefined : BY_ID.get(id);
}

export function isLegoColorId(v: unknown): v is number {
  return typeof v === 'number' && BY_ID.has(v);
}
'''
CPP_HEAD = '''// LEGO colors for the theme color picker (Settings > Color). Generated by
// scripts/gen-lego-colors.py (the web repo keeps the same script in
// packages/model/scripts); don't edit by hand. The web app has the same
// list (packages/model/src/legoColors.ts).
//
// From LDraw's LDConfig.ldr (LEGO numbers, LEGO names and RGB; CC BY 2.0,
// LDraw.org). The BrickLink numbers and names: data from Rebrickable
// (rebrickable.com, its catalog downloads), and BrickLink Studio's color
// table where Rebrickable has none. Only solid, pearl and metallic colors: transparent, chrome,
// glitter and other see-through or sparkly colors are left out, and pearl
// and metallic colors are used as their flat color.

#include "LegoColors.h"

#include <iterator>

namespace bld::ui::theme {

namespace {
struct Row { int id; const char* lego; int blId; const char* bl; const char* hex; bool pearl; };
constexpr Row kRows[] = {
'''
CPP_TAIL = '''};
}  // namespace

const QList<LegoColor>& legoColors() {
    static const QList<LegoColor> all = [] {
        QList<LegoColor> out;
        out.reserve(static_cast<qsizetype>(std::size(kRows)));
        for (const Row& r : kRows)
            out.append({ r.id, QString::fromUtf8(r.lego), r.blId, QString::fromUtf8(r.bl), QColor(QLatin1String(r.hex)), r.pearl });
        return out;
    }();
    return all;
}

const LegoColor* legoColor(int id) {
    for (const LegoColor& c : legoColors())
        if (c.id == id) return &c;
    return nullptr;
}

}  // namespace bld::ui::theme
'''

if __name__ == '__main__':
    main()
