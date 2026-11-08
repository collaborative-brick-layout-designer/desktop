# The app icon

`logo.svg` is the one source for the logo and every app icon: a top-down view
of a blue 2×4 and a red 2×4 meeting at a corner, with a yellow 2×2 joining
them, on a navy rounded tile. It's a 32 × 32 viewBox on a 4-unit stud grid,
with round studs and no shadows, so brick edges land on whole pixels at 16,
24, 32, 48 px and every larger multiple of 16. Edit it by hand. The web app
uses the same file (`assets/logo/logo.svg` in the web repository).

## Rebuilding the icons

```sh
packaging/icons/source/build-icons.sh
```

It needs `rsvg-convert` (librsvg) and `magick` (ImageMagick 7) and renders
each size at exactly that size:

| File | Used for |
| --- | --- |
| `png/brick-layout-designer-{16,24,32,48,64,128,512}.png`, `brick-layout-designer.png` (256) | the window icon (Qt resource `src/ui/theme/appicon.qrc`) and the Linux hicolor theme |
| `brick-layout-designer.ico` (16 to 256, PNG entries) | the Windows .exe (`packaging/windows/brick-layout-designer.rc.in`) |
| `brick-layout-designer.icns` (16 to 1024) | the macOS app bundle |

`logo.svg` itself is installed as the hicolor "scalable" icon.
`tests/ui/AppIconTest.cpp` checks the sizes.
