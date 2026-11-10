# Contributing to ChemFox

## Code layout

| Folder | What it does |
| --- | --- |
| `src/core/model` | Document, molecules, arrows, text, schemes; labels; geometry; atom mapping. Qt Core only. |
| `src/core/chem` | RDKit adapter: formula, mass, SMILES/InChI, stereo, clean-up, auto-mapping, alignment, import. |
| `src/core/commands` | Edit operations; every edit runs inside an undoable snapshot command. |
| `src/style` | The styling language: parser, inheritance, built-in presets, source read/write. |
| `src/render` | One renderer for the screen and every export; scheme layout. |
| `src/editor` | Canvas: hit-testing, hover keys, tools, context menus, keymap, templates. |
| `src/io` | Native JSON (versioned, with migrations), SVG/PDF/PNG/EMF/MOL export, clipboard, import. |
| `src/warnings` | Checks and silencing. |
| `src/app` | Main window, panels (properties, warnings, style, reaction builder), shortcuts, CLI export. |
| `tests` | Catch2 tests, including ones that drive the canvas with real mouse and key events. |
| `packaging` | Icon, Windows installer (Inno Setup), macOS bundle and `.dmg`, licence collection. |
| `docs` | The download website (GitHub Pages). |

Rule of thumb from the original design: the model owns the drawing; RDKit is asked chemistry questions and
never stores the document.

## Building and testing

See the README for setup. `.\build.ps1 -Test` (Windows) or `packaging/macos/build-mac.sh` (macOS) builds and
runs the tests. A few extras:

- `chemfox_tests "[.bench]"` times the hot paths on a large molecule.
- Set `CHEMFOX_SNAPSHOT=<folder>` before running the tests to save PNG snapshots of panels and canvases.

## Versions and releases

ChemFox uses [semantic versioning](https://semver.org). The version is set once, in `CMakeLists.txt`
(`project(ChemFox VERSION x.y.z)`), and flows into the app, installers and file names.

To release:

1. Move the `[Unreleased]` notes in `CHANGELOG.md` under a new `## [x.y.z] - yyyy-mm-dd` heading.
2. Set the same version in `CMakeLists.txt`. Commit.
3. Tag and push: `git tag vx.y.z && git push origin vx.y.z`.
4. The **Build** workflow builds and tests on Windows and macOS, makes the installer, zip and `.dmg`, and creates
   a **draft** GitHub release with those files and the changelog section as notes. Versions starting with `0.`
   (or containing `-`, like `1.0.0-beta.1`) are marked as pre-releases.
5. Check the draft, then publish it.

The workflow refuses to release if the tag and the CMake version disagree.

## File format

Documents are JSON (`.chemfox`, format id `chemfox`; files from before the rename use `chemdoc` and still
load). Bump `kFormatVersion` in `src/io/Native.h` and add a migration in `src/io/Native.cpp` whenever a change
would make old versions misread new files; that's also a MAJOR (or, before 1.0, MINOR) version change.
