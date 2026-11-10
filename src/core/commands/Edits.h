#pragma once
// Edit operations on a Document. Each is a plain function; the editor runs it
// on a copy and pushes a SnapshotCommand, so all of them are undoable.

#include "core/model/Document.h"

#include <QHash>
#include <QSet>
#include <optional>

namespace cd::edits {

// --- creation ---------------------------------------------------------------
// Ids are taken by value: these add atoms, which can move the atom list an id refers into.
QString addAtom(Document& d, QPointF pos, const QString& el = "C");
// Grow a bond from an atom in the best free direction (the one nearest `toward`, if given).
// Returns the new atom id.
QString growBond(Document& d, QString atomId, int order, std::optional<QPointF> toward = std::nullopt);
// Bond from an atom to a point (new atom) or to an existing atom (merging molecules).
QString bondToPoint(Document& d, QString atomId, QPointF pos, int order = 1);
QString bondAtoms(Document& d, QString a, QString b, int order = 1);
// Zig-zag chain from a start (atom or point) toward `end`. Returns the last atom id.
QString addChain(Document& d, std::optional<QString> startAtom, QPointF start, QPointF end, bool snap,
                 std::optional<QString> endAtom = std::nullopt);
// Chain geometry without editing, for previews. First point is the start.
std::vector<QPointF> chainPoints(const Document& d, std::optional<QString> startAtom, QPointF start, QPointF end,
                                 bool snap);
// A ring on an atom: through a chain end, or one bond away from an atom that already has two bonds
// (three for a saturated ring, which can be spiro). The new bond leans toward `toward` if given.
void attachRing(Document& d, QString atomId, int n, bool aromatic, std::optional<QPointF> toward = std::nullopt);
void fuseRing(Document& d, QString bondId, int n, bool aromatic);
// A group label such as Ac: a lone atom or chain end becomes the group; an atom with two or more
// bonds gets it one bond away (leaning toward `toward`). Returns the labelled atom.
QString attachGroup(Document& d, QString atomId, const QString& name, std::optional<QPointF> toward = std::nullopt);
void placeRing(Document& d, QPointF center, int n, bool aromatic);
// Insert molecules (e.g. from SMILES) centred at `at`, with fresh ids. Returns new molecule ids.
QStringList insertMolecules(Document& d, std::vector<Molecule> mols, QPointF at);
QString addArrow(Document& d, QPointF tail, double length = 3.0);
// Next step of a reaction: an arrow tail -> head and copies of the molecules moved by `delta`
// (fresh ids, no number or caption), ready to be changed into the product. Each copied atom is
// mapped to its original and the step shows bond changes. Returns the copies' ids.
QStringList continueReaction(Document& d, const QStringList& molIds, QPointF tail, QPointF head, QPointF delta);
QString addText(Document& d, QPointF pos, const QString& text);
// A mechanism (curved) arrow between two ends; a lone-pair start shows a lone pair on its atom if
// none is drawn yet. Returns the new id, or empty if an end is missing or both ends are the same thing.
QString addCurved(Document& d, const CurvedEnd& from, const CurvedEnd& to, bool half = false);

// --- atoms and bonds ---------------------------------------------------------
void setElement(Document& d, const QString& atomId, const QString& el);
void setLabel(Document& d, const QString& atomId, const QString& text);
void changeCharge(Document& d, const QString& atomId, int delta);
void toggleRadical(Document& d, const QString& atomId);
void setCharge(Document& d, const QString& atomId, int charge);
// Mass number (13 for 13C); 0 = natural abundance.
void setIsotope(Document& d, const QString& atomId, int massNumber);
void setMapNumber(Document& d, const QString& atomId, int map);
// Lone pairs drawn on an atom. `capacity` is how many the atom really has (chem::analyze works it
// out): cycling shows all of them, then one fewer each time, then none. Never more than the capacity.
void setLonePairs(Document& d, const QString& atomId, int n);
void cycleLonePairs(Document& d, const QString& atomId, int capacity);
// Turn a structure about a bond's midpoint so that bond lies horizontal or vertical.
void alignBond(Document& d, const QString& bondId, bool vertical);
// Draw one implicit hydrogen as an explicit H atom (one bond away). Returns the new atom, or empty.
QString addExplicitH(Document& d, QString atomId);
// Remove explicit H atoms bonded to an atom (they become implicit again). Returns how many.
int removeExplicitH(Document& d, const QString& atomId);
// Replace a set of atoms in one structure with a single label atom ("Boc", "R1"). The label sits on the
// atom that bonds to the rest of the structure; a set with more than one such bond is refused.
// Returns the label atom, or empty with *error set.
QString groupAsLabel(Document& d, const QSet<QString>& atomIds, const QString& label, QString* error = nullptr);
void setBondOrder(Document& d, const QString& bondId, int order);
// Set wedge/hash/wavy; applying the same stereo again flips its direction.
void setStereo(Document& d, const QString& bondId, BondStereo s);
void flipBond(Document& d, const QString& bondId);

// --- objects -----------------------------------------------------------------
// Delete any object by id: atom, bond, molecule, arrow, text.
void remove(Document& d, const QString& id);
void moveObjects(Document& d, const QSet<QString>& ids, QPointF delta);
void cycleArrowKind(Document& d, const QString& arrowId);
// Copies of molecules, arrows and texts in `ids`, moved by `offset`, with fresh ids (numbers and captions
// are not copied). Returns the new ids.
QSet<QString> duplicate(Document& d, const QSet<QString>& ids, QPointF offset);
// Copies of the mechanism arrows in `src` whose attached ends were all copied (ids translated through
// the maps); arrows with two free ends are copied only when listed in `ids`. Returns the new ids.
QSet<QString> copyCurved(Document& d, const std::vector<CurvedArrow>& src, const QHash<QString, QString>& atomIds,
                         const QHash<QString, QString>& bondIds, QPointF offset, const QSet<QString>& ids);
// Draw these objects on top of (toFront) or underneath the others of their kind.
void restack(Document& d, const QSet<QString>& ids, bool toFront);
// Apply a linear map (flip, rotation) to molecules, arrows and texts in `ids`, about their
// common centre. Mirror images swap wedges and hashes so the chemistry stays the same.
void transformObjects(Document& d, const QSet<QString>& ids, double m11, double m12, double m21, double m22);
// The same about a given point, and the centre transformObjects uses (false if `ids` holds nothing).
void transformAbout(Document& d, const QSet<QString>& ids, QPointF center, double m11, double m12, double m21, double m22);
bool transformCenter(const Document& d, const QSet<QString>& ids, QPointF* center);
// The tidy orientations of a structure: its long axis horizontal, flipped and turned (and the
// same upright), about its own centre, without visual duplicates. Mirror images swap wedges.
std::vector<Molecule> orientationVariants(const Molecule& m);
// Collapse an expanded abbreviation back to its label atom.
void contractGroup(Document& d, const QString& molId, const QString& anchorId);
// Split molecules into connected components (largest keeps the id and number).
void splitFragments(Document& d, const QString& molId);

// --- schemes -----------------------------------------------------------------
// Build one scheme from the spatial arrangement: rows of reactants -> arrow -> products,
// with molecules sitting above an arrow treated as agents.
void buildScheme(Document& d);
// Number compounds in scheme order (manual numbers are kept and skipped).
void renumber(Document& d);

}  // namespace cd::edits
