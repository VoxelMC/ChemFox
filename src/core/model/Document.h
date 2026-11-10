#pragma once
// Document model: molecules, arrows, text and schemes. Qt Core only, no GUI.
// Coordinates are in bond-length units (1.0 = one bond); y points down.

#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <optional>
#include <vector>

namespace cd {

struct Atom {
    QString id;
    QString el = "C";
    double x = 0, y = 0;
    int charge = 0;
    int radical = 0;   // number of unpaired electrons
    QString label;     // abbreviation such as OMe or CO2Et; empty = plain element
    int map = 0;       // reaction atom-map number; 0 = unmapped
    int isotope = 0;   // mass number shown before the element (13 for 13C, 2 for D); 0 = natural
    int lonePairs = 0; // lone pairs drawn as dot pairs around the atom (0-4); display only
    QPointF pos() const { return {x, y}; }
    void setPos(QPointF p) { x = p.x(); y = p.y(); }
};

enum class BondStereo { None, Wedge, Hash, Wavy };
enum class DbPos { Auto, Left, Right, Center };  // side of the second line, relative to a->b

struct Bond {
    QString id, a, b;
    int order = 1;
    BondStereo stereo = BondStereo::None;
    DbPos dbpos = DbPos::Auto;
    bool has(const QString& atom) const { return a == atom || b == atom; }
    QString other(const QString& atom) const { return a == atom ? b : a; }
};

// Atoms that were expanded from an abbreviation ("OMe" shown as O–CH3), so the
// group can be collapsed back to its label. `anchor` is the atom that carries the label.
struct Group {
    QString label;
    QString anchor;
    QStringList atoms;  // includes the anchor
};

struct Molecule {
    QString id;
    std::vector<Group> groups;
    QString number;           // compound number shown under the structure
    bool numberManual = false;
    QString caption;          // e.g. yield "85%", shown under the number
    bool showStereo = false;  // draw CIP labels: (R)/(S) at stereocentres, (E)/(Z) at double bonds
    bool showCarbons = false; // label every carbon with its hydrogens (CH3, CH2, ...)
    QString aromatic;         // aromatic rings: "" = as the style says, "kekule", "circles"
    std::vector<Atom> atoms;
    std::vector<Bond> bonds;

    Atom* atom(const QString& id);
    const Atom* atom(const QString& id) const;
    Bond* bond(const QString& id);
    const Bond* bond(const QString& id) const;
    const Bond* bondBetween(const QString& a, const QString& b) const;
    std::vector<const Bond*> bondsOf(const QString& atomId) const;
    QStringList neighbors(const QString& atomId) const;
    const Group* groupOf(const QString& atomId) const;
    // Keep groups consistent after atoms are removed or renamed.
    void pruneGroups();
    void renameAtoms(const QHash<QString, QString>& rename);
    QRectF bounds() const;   // in bond units
    QPointF center() const;
    void translate(QPointF d);
};

enum class ArrowKind { Forward, Equilibrium, Retro, Dashed, Crossed, Resonance };
QString arrowKindName(ArrowKind k);
ArrowKind arrowKindFromName(const QString& s);

struct Arrow {
    QString id;
    ArrowKind kind = ArrowKind::Forward;
    double x1 = 0, y1 = 0, x2 = 3, y2 = 0;  // tail -> head
    std::vector<QPointF> via;                // corners of a bent arrow, tail to head
    QString above, below;                    // multi-line annotations (markup allowed)
    QString color;                           // arrow and its "above" text; empty = style colour
    QString headStyle;                       // "", "filled", "open", "half", "none" ("" = the style's)
    bool bothHeads = false;                  // a head at the tail too
    QString line;                            // "", "dashed", "dotted"
    double widthScale = 1.0;                 // line weight relative to the style (thin 0.6, thick 1.8)
    bool hideLetter = false;                 // no step letter on this arrow even when the document shows them
    QPointF tail() const { return {x1, y1}; }
    QPointF head() const { return {x2, y2}; }
    std::vector<QPointF> path() const {
        std::vector<QPointF> p{tail()};
        p.insert(p.end(), via.begin(), via.end());
        p.push_back(head());
        return p;
    }
    void translate(QPointF d) {
        x1 += d.x(); x2 += d.x(); y1 += d.y(); y2 += d.y();
        for (auto& v : via) v += d;
    }
};

// One end of a mechanism arrow: attached to an atom, to a lone pair drawn on an atom, or to a bond
// (its midpoint); otherwise a free point. Attached ends are resolved from the structure every time
// the arrow is drawn, so they follow it when it is moved, rotated, flipped or cleaned.
struct CurvedEnd {
    QString atom;           // atom id (with lonePair: the lone pair on that atom nearest the other end)
    QString bond;           // bond id
    bool lonePair = false;
    double x = 0, y = 0;    // position of a free end
    bool attached() const { return !atom.isEmpty() || !bond.isEmpty(); }
    QPointF pos() const { return {x, y}; }
};

// A curved electron-pushing arrow: a full head moves an electron pair, a half head (fishhook) one electron.
struct CurvedArrow {
    QString id;
    CurvedEnd from, to;
    bool half = false;
    // Height of the curve above the straight line between its ends, as a fraction of that line's
    // length; the sign picks the side (+ = perp(to - from)). Used only when `manual` is set:
    // otherwise the arrow bulges into the emptier side by a standard amount.
    double bulge = 0;
    bool manual = false;
    QString color;          // empty = the style's
    // A shape set by hand (dragging an end, a control handle or the curve): the ends sit at these offsets
    // from what they're attached to (the atom, the bond's middle, or the free point), and the curve's two
    // control points at these offsets from the tail and the head. Model units, so the whole shape follows
    // its structure. Unset, the shape is worked out automatically.
    bool shaped = false;
    QPointF fromOff, toOff, c1, c2;
};

struct TextBox {
    QString id;
    double x = 0, y = 0;
    QString text;   // markup allowed: **bold**, *italic*
    double size = 0;  // points; 0 = the style's text size
    QString color;    // empty = the style's text colour
    QString align;    // lines aligned "" (left), "center" or "right"
};

struct Step {
    QStringList reactants;
    QStringList agents;    // molecules placed above the arrow as reagents
    QString arrow;
    QStringList products;
    bool showDiff = false;  // colour broken/formed/changed bonds using the atom mapping
    bool manualMap = false; // mapping was edited by hand; don't overwrite it automatically
    QStringList noPlus;     // reactants/products whose following "+" was removed (a mechanism drawn as one scene)
};

struct Scheme {
    QString id;
    std::vector<Step> steps;
};

struct Document {
    std::vector<Molecule> molecules;
    std::vector<Arrow> arrows;
    std::vector<TextBox> texts;
    std::vector<CurvedArrow> curved;  // mechanism arrows
    std::vector<Scheme> schemes;

    QString styleName = "default";
    QString styleText;              // embedded copy of a custom style, optional
    bool stepLetters = false;       // label arrows a), b), ...
    bool showMaps = false;          // draw atom-map numbers
    QSet<QString> suppressedCodes;  // "W201"
    QSet<QString> suppressedInstances;  // "W301@m1/a4"
    int nextId = 1;
    bool needsLayout = false;  // transient: set when a loader generated coordinates

    QString newId(const QString& prefix) { return prefix + QString::number(nextId++); }

    Molecule* molecule(const QString& id);
    const Molecule* molecule(const QString& id) const;
    Molecule* moleculeOfAtom(const QString& atomId);
    const Molecule* moleculeOfAtom(const QString& atomId) const;
    Molecule* moleculeOfBond(const QString& bondId);
    Arrow* arrow(const QString& id);
    const Arrow* arrow(const QString& id) const;
    TextBox* text(const QString& id);
    CurvedArrow* curvedArrow(const QString& id);
    const CurvedArrow* curvedArrow(const QString& id) const;
    const Molecule* moleculeOfBond(const QString& bondId) const;
    bool empty() const { return molecules.empty() && arrows.empty() && texts.empty() && curved.empty(); }
    // Model position of a mechanism-arrow end: the atom, the bond's midpoint, or the free point.
    // (The renderer refines atom and lone-pair ends so the arrow stops short of what it points at.)
    // Returns false when the atom or bond is gone.
    bool resolve(const CurvedEnd& e, QPointF* pos) const;
    // Drop mechanism arrows whose atom or bond no longer exists.
    void pruneCurved();

    // Remove any reference to deleted objects from schemes and drop empty steps.
    void pruneSchemes();
    // Re-derive the ids counter so newly created ids never collide.
    void fixNextId();
};

}  // namespace cd
