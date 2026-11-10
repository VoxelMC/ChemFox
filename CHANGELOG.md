# Changelog

All notable changes to ChemFox are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org): MAJOR for changes that break files or habits, MINOR for new
features, PATCH for fixes. While the version starts with 0, anything may still change.

The release workflow copies the section for a version into its GitHub release notes, so keep the headings
in the form `## [x.y.z] - yyyy-mm-dd`.

## [Unreleased]

## [0.2.1] - 2026-11-10

### Added
- Check for Updates (Help menu): asks GitHub whether a newer ChemFox is out. It also checks quietly at most
  once a day at startup, which you can turn off with Help → Check for Updates Automatically. This is the only
  network request ChemFox makes.
- A user guide with every keyboard shortcut, at cmfx.trevfox.dev/guide.html (Help → User Guide).

### Changed
- Documents now save as `.cmfx`. `.chemfox` (and the older `.chemdoc`) files still open and can be saved back
  under their own name; Save As offers `.cmfx`. The installers associate `.cmfx` as well as both older
  extensions. The samples are now `.cmfx`.
- The website moved to cmfx.trevfox.dev.
- macOS: the app's identifier is now `dev.trevfox.chemfox` (it was `app.chemfox.ChemFox`). Settings are kept;
  you may need to choose ChemFox again as the app for opening documents.

### Fixed
- Third-party notices now cover the code compiled into RDKit (InChI, coordgenlibs, maeparser,
  RingDecomposerLib, Eigen), and credit FreeType and the Independent JPEG Group.
- The Windows build no longer includes Microsoft's DirectX shader compiler DLLs, which ChemFox doesn't use.

## [0.2.0] - 2026-11-10

### Added
- Focused atom (light orange): after a key grows a bond, keys act on the newest atom wherever the mouse goes,
  until Escape or a click. Each new bond leans toward the mouse, so you steer by pointing. Arrow keys move
  the focus to a neighbouring atom, and clicking an atom of an already-selected structure focuses it.
- Continue Reaction (Ctrl+arrow key; Cmd on macOS): an arrow from the selected structure in that direction
  and a copy of it past the head, selected, ready to edit into the product. The copy is mapped atom for atom
  and the step shows bond changes, so edits to it are coloured as you make them.
- Show Bond Changes for any arrow: right-click it, or Reaction → Arrow (all steps if no arrow is hovered or
  selected). Steps without a mapping are mapped automatically.
- Custom groups: Edit → Keyboard Shortcuts → Groups. Give a name and a SMILES with * at the attachment point,
  and optionally a hover key. Files that use them carry the definitions.
- Shift+C adds an acetyl (Ac). Group keys turn a chain end into the group, or add it one bond away from an
  atom with two or more bonds.
- E on a group label expands it to its structure; E on an atom of an expanded group collapses it.

### Changed
- New logo: a solid fox head made from the benzene hexagon, with an orange nose, on a flat orange tile.
  A press kit (SVG and PNG, icon and mark, in colour, black and white) is in `docs/press` and linked from
  the download page.
- The toolbar's Undo and Redo buttons keep a fixed label (what they'd undo is in the tooltip), so the
  buttons next to them no longer shift.
- Files open with the drawing fitted and centred, also when opened at launch.
- A ring added to an atom that already has two bonds (three for a saturated ring, which may be spiro) hangs
  off a new bond instead of being drawn through the atom; it leans toward the mouse like a new bond.

## [0.1.0] - 2026-11-10

First test release.

### Drawing
- Hover-and-type editing: elements, labels, charges, radicals, bond orders, wedges/hashes/wavy bonds, rings
  and benzene fused onto atoms or bonds; Tab cycles overlapping targets.
- Tools toolbar: Select (box selection), Chain, Arrow, Bent arrow, Text and Mapping, with keys V, G, Shift+A,
  Shift+B, Shift+T.
- Rendering rules for double bonds, label flipping and anchoring (CO₂Et ↔ EtO₂C), automatic subscripts and
  superscript charges, wedges and hashes, optional aromatic circles, all-carbon labels, R/S and E/Z labels.
- Abbreviations (OMe, CO₂Et, NHBoc, CoA…) count toward the formula and can be expanded to structures and
  collapsed back.
- Clean structure with RDKit that keeps E/Z and R/S; pressing Clean again cycles orientations.
- Right-click menus: flip, rotate, align, copy as SMILES/InChI/InChIKey/MOL/image, label and bond options,
  arrow kind, colour, head and line styles, corners, templates.
- Templates: rings, the 20 L-amino acids, sugars, cofactors (ATP, PLP, TPP, SAM, CoA), common compounds.

### Reactions
- Six arrow kinds plus bent arrows with draggable corners and segments; conditions above and below with
  automatic formula subscripts and **bold**/*italic* markup.
- Arrange Scheme keeps each arrow's direction, handles converging branches, and can tidy just a selection.
- Compound names and numbers (bold, auto-numbered in scheme order or set by hand) and captions.
- Reaction Builder: build a reaction from SMILES/SMARTS rows or one reaction SMILES; live updates.
- Atom mapping (automatic or typed), drag-to-fix mapping mode, broken/formed/changed bonds coloured.
- Align products to their reactants (by mapping or common substructure).

### Chemistry and files
- Formula, molecular weight, exact mass, SMILES, InChI and InChIKey.
- Import SMILES, InChI, CXSMILES labels, MOL/SDF (V2000/V3000); paste structures at the cursor.
- Export SVG (reopenable in ChemFox), PDF, PNG at any DPI, EMF (Windows) and MOL/SDF; clipboard image + SMILES.
- Warnings W101–W401 with per-item, per-document and global silencing.

### Styles and settings
- Built-in styles: default, ACS 1996, slides, poster, dark; a tabbed style editor with a source view.
- Every command and hover key can be rebound in Edit → Keyboard Shortcuts.
