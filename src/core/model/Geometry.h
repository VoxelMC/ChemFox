#pragma once
// 2D geometry on molecules: ring perception for drawing, double-bond sides,
// directions for new bonds. Pure model code, shared by editing and rendering.

#include "core/model/Document.h"

#include <QLineF>
#include <cmath>
#include <optional>

namespace cd::geom {

inline double len(QPointF p) { return std::hypot(p.x(), p.y()); }
inline QPointF unit(QPointF p) { double l = len(p); return l > 1e-12 ? p / l : QPointF(1, 0); }
inline QPointF perp(QPointF p) { return {-p.y(), p.x()}; }
inline double cross(QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); }
inline double dot(QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); }
inline QPointF rotate(QPointF p, double rad) {
    double c = std::cos(rad), s = std::sin(rad);
    return {p.x() * c - p.y() * s, p.x() * s + p.y() * c};
}
double distToSegment(QPointF p, QPointF a, QPointF b);

// Smallest ring (as ordered atom ids) containing the bond, up to maxSize atoms; empty if none.
QStringList smallestRing(const Molecule& m, const Bond& b, int maxSize = 8);

// Side of the second line of a double bond: +1 = perp(b - a) side, -1 = other side, 0 = centred.
int doubleBondSide(const Molecule& m, const Bond& b);

// Unit direction for a new bond from an atom; `order` matters for linear (triple) geometry.
// With `toward` (a model point, e.g. the mouse), picks the well-formed direction nearest to it:
// any 30° step from a lone atom, the zig or the zag from a chain end, the open side elsewhere.
QPointF newBondDirection(const Molecule& m, const QString& atomId, int order = 1,
                         std::optional<QPointF> toward = std::nullopt);

// Direction pointing into the largest free angular gap around an atom.
QPointF freeDirection(const Molecule& m, const QString& atomId);

// The angle step for bonds and chains drawn with the mouse (Preferences → Drawing): 30° or 15°.
double defaultSnapStep();
void setDefaultSnapStep(double rad);
// Snap an angle (radians) to the nearest multiple of `step`.
double snapAngle(double rad, double step = defaultSnapStep());

}  // namespace cd::geom
