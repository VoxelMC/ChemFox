# Changelog

All notable changes to ChemFox are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org): MAJOR for changes that break files or habits, MINOR for new
features, PATCH for fixes. While the version starts with 0, anything may still change.

The release workflow copies the section for a version into its GitHub release notes, so keep the headings
in the form `## [x.y.z] - yyyy-mm-dd`.

## [Unreleased]

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
