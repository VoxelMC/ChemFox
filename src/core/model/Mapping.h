#pragma once
// Reaction atom mapping as stored in the model (Atom::map), and the molecular
// diff it implies: bonds broken, formed or changed in order across a step.

#include "core/model/Document.h"

#include <QHash>

namespace cd::mapping {

enum class Change { None, Broken, Formed, Changed };

// Bond id -> change, for bonds of the step's reactants (broken/changed) and
// products (formed/changed). Agents are ignored.
QHash<QString, Change> bondChanges(const Document& d, const Step& s);

// Same, as a list, for molecules that are not (yet) in a document and may share ids.
// A bond whose order changes appears once on each side.
struct BondChange {
    const Molecule* molecule;
    const Bond* bond;
    Change change;
};
std::vector<BondChange> changeList(const std::vector<const Molecule*>& reactants,
                                   const std::vector<const Molecule*>& products);

// Every step with showDiff on, merged.
QHash<QString, Change> allBondChanges(const Document& d);

// Product heavy atoms that carry no map number, when the step uses mapping at all.
QStringList unmappedProductAtoms(const Document& d, const Step& s);

// The step in which one atom is on the reactant side and the other on the product side.
// Sets reactantAtom/productAtom in that order. Null if the atoms don't face each other in any step.
Step* stepLinking(Document& d, const QString& atomA, const QString& atomB, QString* reactantAtom,
                  QString* productAtom);
// Step that contains the atom's molecule as a reactant or product, if any.
Step* stepOfAtom(Document& d, const QString& atomId);

// Give a reactant atom and a product atom the same map number. Their previous partners
// lose their numbers; the step is marked as hand-mapped.
void link(Document& d, Step& s, const QString& reactantAtom, const QString& productAtom);
// Remove an atom's map number and its partner's on the other side.
void unlink(Document& d, Step& s, const QString& atomId);
// Atoms in the step that share the atom's map number (its partners), excluding itself.
QStringList partners(const Document& d, const Step& s, const QString& atomId);

// True if any atom of the step's reactants or products has a map number.
bool stepIsMapped(const Document& d, const Step& s);

}  // namespace cd::mapping
