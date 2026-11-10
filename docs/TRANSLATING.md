# Translating Collaborative Brick Layout Designer

## Languages we ship

Upstream BlueBrick's languages plus Chinese and Japanese. How much of each
is done is in `src/ui/TranslationCoverage.h`, which Settings › Language
shows next to each language until it reaches 100 % (the rest shows in
English):

| Code | Language              | Sheet is | Part is |
|------|-----------------------|----------|---------|
| en   | English               | sheet    | part    |
| de   | German                | Blatt    | Teil    |
| fr   | French                | feuille  | pièce   |
| it   | Italian               | foglio   | pezzo   |
| nl   | Dutch                 | blad     | onderdeel |
| pt   | Portuguese            | folha    | peça    |
| es   | Spanish               | hoja     | pieza   |
| sv   | Swedish               | blad     | del     |
| no   | Norwegian (Bokmål)    | ark      | del     |
| zh   | Chinese (Simplified)  | 图纸     | 零件    |
| ja   | Japanese              | シート   | パーツ  |

"Sheet" is never the word for a layer (`Ebene`, `calque`, `capa`, `レイヤー`
…): docs/glossary.md, and the Translations test checks it. Norwegian uses
the code `no`, as BlueBrick did; systems set to `nb` or `nn` pick it
automatically.

`bld_en.ts` holds only English's plurals: the source says "%n part(s)",
and the catalog makes it "1 part" and "3 parts". It is filled by
[`scripts/fill-english-plurals.py`](../scripts/fill-english-plurals.py).

## Workflow — running `lupdate`

From the repo root, with Qt 6's `lupdate` (Qt 5's writes other contexts):

```sh
for lang in de es fr it ja nl no pt sv zh; do
    lupdate -recursive src -ts translations/bld_${lang}.ts -locations none -no-obsolete
done
lupdate -recursive src -pluralonly -source-language en -target-language en \
    -locations none -no-obsolete -ts translations/bld_en.ts
python3 scripts/fill-english-plurals.py
python3 scripts/translation-coverage.py
```

`lupdate` keeps the translations that are there, adds the new strings as
unfinished and (with `-no-obsolete`) drops the ones the source no longer
has. `-locations none` keeps the files free of line numbers, so they only
change when the words do.

CI's *Translations match the source* step
([`scripts/ci/check-translations.py`](../scripts/ci/check-translations.py))
fails when a `.ts` file is missing a string the source marks for
translation, or keeps one it dropped. Run the loop above to fix it.

A `tr()` lupdate can't place (a lambda or a free function named `tr`)
lands in the namespace's name, where the app never looks: use a struct
with `Q_DECLARE_TR_FUNCTIONS(context)` and call `Struct::tr()` (see
`src/ui/ImportPipeline.cpp`). Strings kept in tables (`const char*`) are
marked with `QT_TRANSLATE_NOOP("context", "…")` and translated where they
are shown.

### Help texts

The help popovers come from `src/ui/help/help-texts.json`, the web app's
file word for word. `src/ui/help/HelpTextStrings.cpp` marks the same
strings with `QT_TRANSLATE_NOOP("HelpTexts", …)`; after changing the JSON,
run `scripts/gen-help-strings.py`.

### Tour and welcome text

The guided tours and the first-launch welcome come from
`src/ui/tours/tours.json`, which is the web app's file word for word, so
it can't carry `tr()` calls. `src/ui/tours/TourStrings.cpp` marks the same
strings with `QT_TRANSLATE_NOOP("Tours", …)` so `lupdate` finds them (the
`Tours` context), and the app translates them when it reads the file.
After changing `tours.json`, run `scripts/gen-tour-strings.py`; the
`Tours.EveryStringInToursJsonIsMarkedForTranslation` test fails until you do.

## Workflow — translating with `scripts/ts-tool.py`

```sh
python3 scripts/ts-tool.py export de > de-todo.json   # what is left, as {"source": ""}
# fill in the values (a "%n" string's value is its list of forms), then:
python3 scripts/ts-tool.py fill de de-todo.json
python3 scripts/translation-coverage.py
```

`fill` writes each translation into every message with that source and
leaves out any that loses a placeholder (`%1`, `%n`) or an Alt letter
(`&File`), or says "layer" for a sheet; `check` clears such translations
already in a file.

## Workflow — bulk-applying translations via the script

`scripts/apply-translations.py` ships a `TRANS` dictionary keyed by
language, where each entry maps a `<source>` string (XML-escaped as
Qt writes it — `&amp;File` etc) to the target translation. Running:

```sh
python3 scripts/apply-translations.py
```

walks every `translations/bld_<lang>.ts`, substitutes the
`<translation>` body for any source appearing in `TRANS[<lang>]`, and
strips `type="unfinished"`. Missing sources stay unfinished so Qt
falls back to English at runtime.

Native speakers: the easiest way to contribute a pass is to grow the
relevant language block inside `TRANS` in that script and rerun it.

## Workflow — adding translations

Option A — **Qt Linguist (GUI):**

```sh
linguist translations/bld_fr.ts
```

Opens a three-pane editor: source string → translation → context.
Write the translation, mark as Done, save. Qt Linguist also flags
translations that include placeholders (`%1`, `%2`) that don't match
the source arity.

Option B — **plain text editor:**

Open the `.ts` file, find `<message>` entries with empty
`<translation type="unfinished"></translation>` bodies, fill in the
translated string, and remove the `type="unfinished"` attribute:

```xml
<message>
    <source>Open...</source>
    <translation>Ouvrir...</translation>
</message>
```

Save and commit.

## Workflow — building

CMake auto-detects the `.ts` files; a successful build produces
`.qm` files in the build directory (under `<build>/translations/`).
The `main.cpp` loader picks them up based on the user's language
preference (Settings › Language).

When you ship a release, the package step copies `.qm` files from the
build directory to `<app>/translations/` so the installed binary
finds them at runtime.

## Translator guidelines

- **Preserve placeholders** (`%1`, `%2`, `&File`, `Ctrl+S`). Moving
  them is fine — translating them is a bug.
- **Keep mnemonic ampersands** (`&File`, `Sa&ve`). Qt turns these into
  Alt-key shortcuts; removing one breaks keyboard nav in that menu.
- **LEGO terms stay in English** where they're brand names — "brick",
  "stud", "BlueBrick", "LUG" — unless the native-language LEGO
  community has a widely-used equivalent.
- **Matched punctuation**: if the source ends with `:` or `...`,
  preserve it in the translation. Dialog labels and menu items are
  wired to those.
- **When in doubt, ask on the PR.** Context helps — BlueBrick
  Windows-only for 20 years means many existing community
  conventions. If there's an established vanilla-BlueBrick
  translation, we match.

## Adding a new language

1. Add the ISO 639-1 code to `BLD_LANGUAGES` in the root
   `CMakeLists.txt`.
2. Create `translations/bld_<code>.ts` as an empty TS (copy an
   existing stub, change the `language="..."` attribute).
3. Run `lupdate` to populate it (see above).
4. Add the language to `settings::languages()` (`src/ui/SettingsSections.cpp`) and to the lists in `scripts/translation-coverage.py`, `scripts/ts-tool.py` and `scripts/ci/check-translations.py`.
5. Translate.
6. Open a PR.

## Rebuilding from scratch

If a `.ts` file gets corrupted or you want to nuke all translations
and start over:

```sh
# Clear one
echo '<?xml version="1.0" encoding="utf-8"?><!DOCTYPE TS>
<TS version="2.1" language="fr"><context><name>BrickLayoutDesigner</name></context></TS>' > translations/bld_fr.ts

# Repopulate
lupdate -recursive src -ts translations/bld_fr.ts
```
