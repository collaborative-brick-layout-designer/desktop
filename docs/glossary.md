# Glossary: the words people see

The same file is in both apps: `docs/glossary.md` in the web repo and in the
desktop repo. Keep the two identical. Each app has a test that flags the
banned words in text people see: the web's `src/test/uiWording.test.ts` and
the desktop's `UiWordingTest`.

| Say | Not | What it is |
|---|---|---|
| **Sheet** | layer | One of the see-through sheets a layout is made of (parts, text, areas, rulers, the grid). "Add sheet", "Remove sheet". A BlueBrick file's untouched "Layer 1" shows as "Sheet 1". |
| **Adding to** | active layer | The sheet new parts go on. |
| **Turn left / Turn right / Turn 90°** | rotate, CW / CCW | Turning the picked parts. The step is the **turn step**. An angle you type is the **angle**. |
| **Send to back / Bring to front** | to back / to front | Drawing order within a sheet. |
| **Club** | org, organization | A group of builders who share layouts, venues, modules and parts. |
| **Module** | group (for modules) | A saved piece of a layout, such as a table section, that moves as one. |
| **Venue** (the **room**) | hall, space | The room a layout is set up in: walls, doors, open areas and obstacles. |
| **Floor plan** | blueprint, sketch (for the picture) | The picture of the room you trace over in the venue designer, calibrated so its lengths are real. |
| **Parts library** / **part pack** | part library zip, package | The parts the apps can place. "Get the parts library" sets one up. |
| **Web address** | slug, URL slug | The short name in a club's or a page's address. A pack's **folder name** on the server. |
| **…** | ... | One ellipsis character on anything that opens a window. |
| **Color** | colour | US spelling everywhere. |

Code, file formats and addresses keep their own names: `Layer` in a `.bbm`,
`org` in routes such as `/orgs/…` and in the API, `slug` in the database,
`rotate` in function names. Those aren't text people see, so the tests leave
them alone. Don't rename the identifiers the 2026-10-26 rebrand kept.
