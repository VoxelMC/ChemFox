# Changelog

All notable changes to ChemFox are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org): MAJOR for changes that break files or habits, MINOR for new
features, PATCH for fixes. While the version starts with 0, anything may still change.

The release workflow copies the section for a version into its GitHub release notes, so keep the headings
in the form `## [x.y.z] - yyyy-mm-dd`.

## [Unreleased]

## [0.3.0] - 2026-11-10

### Added
- Mechanism (curved) arrows for electron pushing (#4):
  - Hover the atom, lone pair or bond the electrons come from and press M, then hover where they go and press M again;
    Shift+M draws a fishhook (one electron). Or use the new Curved tool (Shift+E): drag from start to end, or click
    one, then the other.
  - Arrows stay attached to their atoms and bonds when structures are moved, flipped, rotated or cleaned, and are
    removed with the atom or bond they use. Copy, paste, duplicate and undo all carry them.
  - Each arrow starts with an automatic shape, which you can then shape freely: drag the curve to bend it, or drag
    either of its two control handles (shown when it's selected). Drag an end and it locks onto the nearest atom or
    bond (highlighted as you go) at exactly the spot you drop it, and keeps that place as the structure moves,
    rotates or flips. Right-click → Reset Shape goes back to automatic. F curves it the other way and K switches
    between electron pair and single electron.
  - The head is the style's own arrowhead at half size; line width, head size and colour can be set in the style's
    new `mechanism` section. Saved in `.cmfx` and exported to SVG, PDF, PNG and EMF. A new sample,
    `mechanism.cmfx`, shows SN2, homolysis, addition to a ketone and E2.
- Lone pairs: Shift+L on an atom shows the lone pairs it really has (from its bonds, hydrogens, charge and radicals,
  as RDKit sees them), then one fewer each press, then none; or right-click → Atom → Lone Pairs. An atom never shows
  more than it has. They sit square to the letter, clear of the hydrogens a label writes. An arrow from an atom
  with lone pairs starts at the pair facing its target.
- Moving single atoms: drag a selected atom (Alt+click selects one), or Ctrl+Alt+drag any atom.
- Plus signs between structures in a step can be removed: hover one and press Delete, or right-click → Remove Plus
  Sign (useful for a mechanism drawn as one scene). Right-click the arrow → Restore Plus Signs brings them back.
- With Reaction → Step Letters on, right-click an arrow → Show Step Letter leaves just that arrow unlettered; the
  others keep running a, b, c…
- Smarter positioning (#9):
  - Hold Shift while dragging structures, arrows or text to snap to the edges, centres and text baselines of other
    objects and to equal spacing, with guide lines. Without Shift, dragging stays free.
  - Alt+drag (Option on macOS) moves a copy and leaves the original, in one undo step.
  - Align left, centre, right, top, middle or bottom, and distribute horizontally or vertically: Edit → Align and
    Distribute, or the right-click menu of a selection.
  - Right-click a bond → Align Bond Horizontally / Vertically turns its structure so the bond lies level or upright.
  - Rotate freely: Shift+R over a structure (or with a selection), or Rotate Freely… on the right-click menu, shows a
    handle that turns it in 15° steps (Alt: freely).
- Preferences (Edit → Preferences, Ctrl+,; ChemFox → Settings on macOS):
  - General: the style for new documents, the tool selected at startup, a folder for opening and saving
    documents, reopen the last document at startup, automatic update checks.
  - Styles: your saved styles with a preview, and import, export, rename, delete, use for new documents and
    open folder.
  - Drawing: whether new bonds lean toward the mouse; 30° or 15° steps for bonds drawn with the mouse; scroll
    wheel pans (Ctrl+scroll zooms) for trackpads; the page width guide; a grid.
  - Export: the format Ctrl+E saves as, PNG resolution, whether to ask each time, transparent background.
  - Keys: opens Keyboard Shortcuts and custom groups.
- Crash recovery: unsaved work is copied every few minutes (Preferences → General), and if ChemFox closes
  without saving, it offers the copy next time. Your own file is never overwritten by it.
- More on the right-click menus:
  - Atoms: charge, radical, isotope (¹³C, ²H), atom-map number, add or remove explicit H.
  - Structures: group selected atoms as a label (Boc, R¹…); aromatic rings as circles or alternating double
    bonds, per structure.
  - Text: bold, italic, size, alignment and colour.
  - Arrows: Edit in Reaction Builder.
  - Everything: Duplicate, Bring to Front, Send to Back (also in the Edit menu: Ctrl+D, Ctrl+Shift+], Ctrl+Shift+[).
  - Empty canvas: Arrange Scheme, Number Compounds, Style, Show Grid, Snap to Grid, Zoom to Fit, Actual Size.
- A dot grid you can snap moved objects to (View menu, the right-click menu or Preferences). It's never exported.

### Changed
- X on an atom types a label (it was Enter), and double-clicking an atom focuses it (keys then add to it) instead of
  opening the label editor.
  Keys you've rebound yourself are kept.
- Charges move to a free corner of the atom instead of sitting on a bond.
- Documents with mechanism arrows or lone pairs are saved as format version 2, which ChemFox 0.2 can't open. Documents
  without them are still saved as version 1.
- Group labels such as OMe colour only their bonding element when atom colours are on.

### Fixed
- Align to Reactant / Align Products to Reactants could flip a product's E/Z or R/S. Each product now keeps
  its own; if that isn't possible, the structure is left as it was.

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
