#pragma once
// Adapter between the model and RDKit. RDKit is asked chemistry questions here
// and never stores the document; RDKit types do not leak through this header.

#include "core/model/Document.h"

#include <QHash>
#include <QMap>
#include <optional>

namespace cd::chem {

struct MolInfo {
    bool ok = false;
    QString formula;       // Hill order, e.g. "C9H8O4"
    double mw = 0;         // average molecular weight, g/mol
    double exactMass = 0;  // monoisotopic
    QString smiles;
    QString inchi;
    QString inchiKey;
    QString error;
};

// Per-molecule facts the renderer and warnings need.
struct Analysis {
    QHash<QString, int> implicitH;          // atom id -> H count to show
    std::vector<QStringList> aromaticRings; // atom ids of each aromatic ring
    QStringList badValence;                 // atom ids with unusual valence (W101)
    QStringList undefinedStereo;            // atom ids of undefined stereocentres (W102)
    QMap<QString, int> elementCounts;       // including hydrogens
    QHash<QString, QString> cip;            // atom id -> "R"/"S", bond id -> "E"/"Z" (CIP rules)
};

MolInfo info(const Molecule& m, bool withInchi = true);
QString smiles(const Molecule& m);
Analysis analyze(const Molecule& m);

// Import. Coordinates come back in bond units, y down, centred on the origin.
std::optional<Molecule> fromSmiles(const QString& smiles, QString* error = nullptr);
std::optional<Molecule> fromInchi(const QString& inchi, QString* error = nullptr);
std::vector<Molecule> fromMolBlocks(const QString& text, QString* error = nullptr);  // MOL or SDF
// Tries SMILES, InChI or MOL depending on what the text looks like.
std::vector<Molecule> fromText(const QString& text, QString* error = nullptr);

QString toMolBlock(const Molecule& m);

// --- reactions ---------------------------------------------------------------
struct ReactionText {
    QStringList reactants, agents, products;
};
// "A.B>C>D" or "A.B>>D" (reaction SMILES or SMARTS) -> components.
ReactionText splitReaction(const QString& text);
QString joinReaction(const ReactionText& r);

// One structure from SMILES, falling back to SMARTS (query atoms become plain atoms).
// Atom-map numbers ([C:1]) are kept in Atom::map.
std::optional<Molecule> fromSmilesOrSmarts(const QString& text, QString* error = nullptr);
// SMILES including atom-map numbers, for editing a reaction as text.
QString smilesWithMaps(const Molecule& m);

// Map reactant atoms onto product atoms by repeated maximum common substructure,
// writing map numbers 1..n into Atom::map (previous maps are cleared). Returns how many were mapped.
int autoMap(const std::vector<Molecule*>& reactants, const std::vector<Molecule*>& products);

// Draw an abbreviation as real atoms ("OMe" -> O–CH3), recorded as a Group so it can be
// collapsed again with edits::contractGroup. Returns false if the label isn't a known abbreviation.
bool expandLabel(Document& d, QString atomId);
bool canExpand(const Atom& a);

// RDKit 2D layout for the molecule, keeping its centre. Returns false on failure.
bool cleanup(Molecule& m);

// Redraw `m` so the part it shares with `ref` (by atom-map numbers if both are mapped, else the
// largest common substructure) has exactly the reference's geometry and orientation. The molecule
// keeps its place on the page and its stereochemistry. Returns false if they share too little.
bool alignTo(Molecule& m, const Molecule& ref);

// Atomic weight for formula display; 0 if unknown.
double atomicWeight(const QString& element);

}  // namespace cd::chem
