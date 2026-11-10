#include "render/Layout.h"

#include "core/model/Geometry.h"

#include <algorithm>
#include <cmath>

namespace cd::layout {

namespace {

using namespace geom;

// Everything here works in points.
struct Ctx {
    Document& d;
    const Renderer& r;
    const ChemOverlay& ov;
    const Style& s;
    double plusW;
};

// A structure's footprint: the drawing plus its name/number underneath.
QRectF box(const Ctx& c, const QString& id) {
    const Molecule& m = *c.d.molecule(id);
    QRectF b = c.r.moleculeRect(m, c.ov);
    QRectF n = c.r.numberRect(m, c.ov);
    return n.isNull() ? b : b | n;
}

// The point a structure is aligned by: horizontally its footprint's centre; vertically the centre of the
// drawing itself on horizontal arrows (so the arrow meets the structure mid-height), else the footprint's.
QPointF anchor(const Ctx& c, const QString& id, bool horizontal) {
    QRectF b = box(c, id);
    QRectF m = c.r.moleculeRect(*c.d.molecule(id), c.ov);
    return {b.center().x(), horizontal ? m.center().y() : b.center().y()};
}

// Distance from point p (inside rect) to the rect's edge along dir.
double extent(const QRectF& rect, QPointF p, QPointF dir) {
    double t = 1e9;
    if (dir.x() > 1e-9) t = std::min(t, (rect.right() - p.x()) / dir.x());
    if (dir.x() < -1e-9) t = std::min(t, (rect.left() - p.x()) / dir.x());
    if (dir.y() > 1e-9) t = std::min(t, (rect.bottom() - p.y()) / dir.y());
    if (dir.y() < -1e-9) t = std::min(t, (rect.top() - p.y()) / dir.y());
    return t > 1e8 ? 0 : t;
}

void moveTo(Ctx& c, const QString& id, bool horizontal, QPointF where) {
    QPointF delta = where - anchor(c, id, horizontal);
    c.d.molecule(id)->translate(delta / c.s.bondLength);
}

// Length a group of structures takes up along dir, including the gaps and plus signs between them.
double groupLength(const Ctx& c, const QStringList& ids, bool horizontal, QPointF dir) {
    double total = 0;
    for (int i = 0; i < ids.size(); ++i) {
        QRectF b = box(c, ids[i]);
        QPointF a = anchor(c, ids[i], horizontal);
        total += extent(b, a, dir) + extent(b, a, -dir);
        if (i > 0) total += 2 * c.s.schemeSpacing + c.plusW;
    }
    return total;
}

// Lay a group out along dir, its near edge at `start`.
void placeGroup(Ctx& c, const QStringList& ids, bool horizontal, QPointF dir, QPointF start) {
    QPointF at = start;
    for (int i = 0; i < ids.size(); ++i) {
        if (i > 0) at += dir * (2 * c.s.schemeSpacing + c.plusW);
        QRectF b = box(c, ids[i]);
        QPointF a = anchor(c, ids[i], horizontal);
        double back = extent(b, a, -dir), fwd = extent(b, a, dir);
        moveTo(c, ids[i], horizontal, at + dir * back);
        at += dir * (back + fwd);
    }
}

}  // namespace

void layoutSchemes(Document& d, const Renderer& r, const ChemOverlay& ov, const QSet<QString>* only) {
    const Style& s = r.style();
    const double bl = s.bondLength;
    Ctx c{d, r, ov, s, r.runsWidth({{"+"}}, FontSpec{s.font, s.fontSize * 1.3})};
    const double gap = s.schemeSpacing;

    for (const auto& scheme : d.schemes) {
        QSet<QString> placed;
        std::vector<QRectF> occupied;
        // Rows wrap at the page width measured from where the scheme starts.
        double x0 = 1e18;
        for (const auto& st : scheme.steps) {
            for (const auto& list : {st.reactants, st.products})
                for (const auto& id : list) if (d.molecule(id)) x0 = std::min(x0, box(c, id).left());
            if (const Arrow* a = d.arrow(st.arrow)) x0 = std::min(x0, std::min(a->x1, a->x2) * bl);
        }
        // Only a plain left-to-right chain wraps into rows; a scheme that already uses other
        // directions is a 2-D design, and wrapping would scramble it.
        bool linear = true;
        for (const auto& st : scheme.steps) {
            if (const Arrow* a = d.arrow(st.arrow)) {
                QPointF v = a->head() - a->tail();
                linear &= std::abs(snapAngle(std::atan2(v.y(), v.x()), M_PI / 4)) < 1e-6;
            }
        }
        auto rowBottom = [&] {
            double b = -1e18;
            for (const auto& o : occupied) b = std::max(b, o.bottom());
            return b;
        };
        auto remember = [&](const QStringList& ids) {
            for (const auto& id : ids) { occupied.push_back(box(c, id)); placed.insert(id); }
        };
        auto movable = [&](const QString& id) { return !only || only->contains(id); };
        auto active = [&](const Step& st) {
            if (!only || only->contains(st.arrow)) return true;
            bool r = false, p = false;
            for (const auto& id : st.reactants) r |= only->contains(id);
            for (const auto& id : st.products) p |= only->contains(id);
            return r && p;
        };
        if (only) {
            // Partial arrange: what isn't selected is already where the user wants it.
            linear = false;  // and rows never re-wrap
            for (const auto& m : d.molecules)
                if (!movable(m.id)) { placed.insert(m.id); occupied.push_back(box(c, m.id)); }
            for (const auto& a : d.arrows) {
                bool used = false;
                for (const auto& st : scheme.steps) used |= st.arrow == a.id && active(st);
                if (!used) occupied.push_back(r.arrowGeom(a, &d).bounds);
            }
        }
        auto collides = [&](const QStringList& ids) {
            for (const auto& id : ids) {
                QRectF b = box(c, id).adjusted(-gap / 2, -gap / 2, gap / 2, gap / 2);
                for (const auto& o : occupied) if (b.intersects(o)) return true;
            }
            return false;
        };

        for (const auto& st : scheme.steps) {
            Arrow* a = d.arrow(st.arrow);
            if (!a || !active(st)) continue;
            a->via.clear();
            QStringList R, P;
            for (const auto& id : st.reactants) if (d.molecule(id)) R << id;
            for (const auto& id : st.products) if (d.molecule(id)) P << id;

            // Keep the arrow's direction, tidied to the nearest 45°.
            QPointF v = a->head() - a->tail();
            double ang = snapAngle(std::atan2(v.y(), v.x()), M_PI / 4);
            QPointF dir(std::cos(ang), std::sin(ang));
            if (std::abs(dir.x()) < 1e-9) dir.setX(0);
            if (std::abs(dir.y()) < 1e-9) dir.setY(0);
            const bool horizontal = std::abs(dir.x()) > 0.7;

            // Arrow length: the style minimum, or enough for its text and reagents.
            Arrow probe = *a;
            probe.x2 = probe.x1 + dir.x() * 0.01;
            probe.y2 = probe.y1 + dir.y() * 0.01;
            ArrowGeom g = r.arrowGeom(probe, &d);
            double textNeed = g.textRect.isNull() ? 0
                              : horizontal        ? g.textRect.width() + s.annotationSize * 1.4
                                                  : g.textRect.height() + s.annotationSize;
            double agentsNeed = 0;
            for (const auto& id : st.agents) {
                if (!d.molecule(id)) continue;
                QRectF b = box(c, id);
                agentsNeed += (horizontal ? b.width() : b.height()) + gap;
            }
            const double L = std::max({s.arrowMinLength, textNeed, agentsNeed + gap});

            // Where does the arrow go? Anchor it on whichever side is already laid out.
            QString rAnchor, pAnchor;
            for (const auto& id : R) if (placed.contains(id)) rAnchor = id;
            for (const auto& id : P) if (placed.contains(id)) { pAnchor = id; break; }
            QPointF tail, head;
            if (!rAnchor.isEmpty() || pAnchor.isEmpty()) {
                // Start from the reactants: placed ones stay; the first step keeps its first structure in place.
                if (rAnchor.isEmpty() && !R.isEmpty()) {
                    QPointF keep = anchor(c, R.front(), horizontal);
                    QPointF start = keep - dir * extent(box(c, R.front()), keep, -dir);
                    placeGroup(c, R, horizontal, dir, start);
                    remember(R);
                    rAnchor = R.back();
                }
                if (!rAnchor.isEmpty()) {
                    QPointF ap = anchor(c, rAnchor, horizontal);
                    tail = ap + dir * (extent(box(c, rAnchor), ap, dir) + gap);
                } else {
                    tail = a->tail() * bl;  // no reactants: the arrow stays where it starts
                }
                head = tail + dir * L;
                if (!pAnchor.isEmpty()) {
                    // Both ends already laid out (a ring closing back): just join them.
                    QPointF pp = anchor(c, pAnchor, horizontal);
                    QPointF toward = unit(pp - tail);
                    head = pp - toward * (extent(box(c, pAnchor), pp, -toward) + gap);
                } else if (!P.isEmpty()) {
                    QPointF start = head + dir * gap;
                    // Long left-to-right rows wrap: the product starts the next row.
                    if (linear && start.x() + groupLength(c, P, horizontal, dir) > x0 + s.pageWidth &&
                        start.x() > x0 + s.pageWidth * 0.3) {
                        double half = 0;
                        for (const auto& id : P) {
                            QPointF ap = anchor(c, id, horizontal);
                            half = std::max(half, extent(box(c, id), ap, QPointF(0, -1)));
                        }
                        start = QPointF(x0, rowBottom() + s.rowSpacing + half);
                    }
                    placeGroup(c, P, horizontal, dir, start);
                    // Push past anything already there, stretching the arrow to follow.
                    for (int k = 0; k < 30 && collides(P); ++k) {
                        for (const auto& id : P) d.molecule(id)->translate(dir);
                        if (start.x() != x0 || !horizontal) head += dir * bl;
                    }
                    remember(P);
                }
            } else {
                // Converging branch (e.g. a second route into a structure already drawn): the product
                // stays put and the arrow and its reactants line up behind it.
                QPointF pp = anchor(c, pAnchor, horizontal);
                head = pp - dir * (extent(box(c, pAnchor), pp, -dir) + gap);
                tail = head - dir * L;
                if (!R.isEmpty()) {
                    double len = groupLength(c, R, horizontal, dir);
                    placeGroup(c, R, horizontal, dir, tail - dir * (gap + len));
                    for (int k = 0; k < 30 && collides(R); ++k) {
                        for (const auto& id : R) d.molecule(id)->translate(-dir);
                        tail -= dir * bl;
                    }
                    remember(R);
                }
                for (const auto& id : P) placed.insert(id);
            }

            a->x1 = tail.x() / bl;
            a->y1 = tail.y() / bl;
            a->x2 = head.x() / bl;
            a->y2 = head.y() / bl;

            // Reagent structures sit beside the shaft: above a horizontal arrow (over its text),
            // to the right of a vertical one (beyond its text).
            QStringList agentsToMove;
            for (const auto& id : st.agents) if (d.molecule(id) && movable(id)) agentsToMove << id;
            if (!agentsToMove.isEmpty()) {
                ArrowGeom ag = r.arrowGeom(*a, &d);
                QPointF mid = (tail + head) / 2;
                if (horizontal) {
                    double top = ag.above.empty() ? mid.y() - s.arrowHeadWidth - s.fontSize * 0.4
                                                  : ag.textRect.top() - s.fontSize * 0.4;
                    double total = -gap;
                    for (const auto& id : agentsToMove) if (d.molecule(id)) total += box(c, id).width() + gap;
                    double x = mid.x() - total / 2;
                    for (const auto& id : agentsToMove) {
                        if (!d.molecule(id)) continue;
                        QRectF b = box(c, id);
                        d.molecule(id)->translate(QPointF(x - b.left(), top - b.bottom()) / bl);
                        x += b.width() + gap;
                    }
                } else {
                    double left = (ag.above.empty() ? mid.x() + s.arrowHeadWidth : ag.textRect.right()) + gap;
                    double total = -gap;
                    for (const auto& id : agentsToMove) if (d.molecule(id)) total += box(c, id).height() + gap;
                    double y = mid.y() - total / 2;
                    for (const auto& id : agentsToMove) {
                        if (!d.molecule(id)) continue;
                        QRectF b = box(c, id);
                        d.molecule(id)->translate(QPointF(left - b.left(), y - b.top()) / bl);
                        y += b.height() + gap;
                    }
                }
                remember(agentsToMove);
            }
            occupied.push_back(r.arrowGeom(*a, &d).bounds);
        }
    }
}

}  // namespace cd::layout
