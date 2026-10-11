# Endo icons for packaging

The logo is a hand-painted `λ` followed by a `_` cursor in a frame, drawn as a companion to
[Contour Terminal](https://github.com/contour-terminal/contour)'s icon: the `λ` in the
blue→teal of Endo's `endo-signature` prompt, the cursor and frame in Contour's colours.

The SVGs in [`docs/assets/images/`](../../docs/assets/images/) are the source. Everything
in this directory is rendered from them:

| File | Rendered from | Used by |
|------|---------------|---------|
| `endo.ico` (16–256 px) | `endo-icon-small.svg` up to 32 px, `endo-icon.svg` above | `endo.exe`'s icon resource ([`src/shell/EndoVersionInfo.rc.in`](../../src/shell/EndoVersionInfo.rc.in)), the MSI's Add/Remove Programs icon |
| `endo.icns` (16–512 px) | the same split | the macOS DMG volume icon |
| `hicolor/<N>x<N>/apps/endo.png` | the same split | the freedesktop icon theme on Linux, next to `endo.desktop` |

The VS Code extension takes the 256 px PNG as its Marketplace icon. The Marketplace wants
a PNG of at least 128 px and rejects SVG, and `vsce` packages only what sits below the
extension folder, so [`editors/vscode/package.json`](../../editors/vscode/package.json)'s
`package` script copies it to `images/endo.png` on its way to building the `.vsix` — the
same way it copies `LICENSE.txt` in. The copy is gitignored, so re-rendering the icons
needs nothing extra.

The WiX installer's banner and Welcome/Finish image are not committed:
[`../windows/compose-installer-bitmaps.ps1`](../windows/compose-installer-bitmaps.ps1)
composes them from the 48 and 128 px PNGs at build time.

The painted icon's brush texture blurs below about 40 px, so the small sizes use
`endo-icon-small.svg`: the same layout in flat colour, with strokes thick enough to stay a
pixel or two wide.

The icon paths are declared in
[`cmake/ProjectMetadata.cmake`](../../cmake/ProjectMetadata.cmake); the installer bitmaps
are composed and handed to WiX in [`cmake/Packaging.cmake`](../../cmake/Packaging.cmake).
