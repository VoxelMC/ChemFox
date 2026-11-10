#include "core/commands/Edits.h"

#include "core/model/Geometry.h"
#include "core/model/Labels.h"

#include <QHash>
#include <QQueue>
#include <algorithm>
#include <functional>
#include <map>
#include <numeric>

namespace cd::edits {

using namespace geom;

namespace {

constexpr double kMergeDist = 0.3;

Molecule& newMolecule(Document& d) {
    Molecule m;
    m.id = d.newId("m");
    d.molecules.push_back(m);
    return d.molecules.back();
}

Atom& newAtom(Document& d, Molecule& m, QPointF p, const QString& el = "C") {
    Atom a;
    a.id = d.newId("a");
    a.el = el;
    a.setPos(p);
    m.atoms.push_back(a);
    return m.atoms.back();
}

void newBond(Document& d, Molecule& m, const QString& a, const QString& b, int order) {
    if (a == b || m.bondBetween(a, b)) return;
    Bond bd;
    bd.id = d.newId("b");
    bd.a = a;
    bd.b = b;
    bd.order = order;
    m.bonds.push_back(bd);
}

// Existing atom within kMergeDist of p in molecule m (or anywhere when m is null).
QString atomNear(Document& d, QPointF p, const Molecule* m = nullptr) {
    double best = kMergeDist;
    QString id;
    for (auto& mol : d.molecules) {
        if (m && &mol != m) continue;
        for (auto& a : mol.atoms) {
            double l = len(a.pos() - p);
            if (l < best) { best = l; id = a.id; }
        }
    }
    return id;
}

// Move everything from molecule `from` into `into`; returns the merged molecule's id.
QString merge(Document& d, const QString& into, const QString& from) {
    if (into == from) return into;
    Molecule* a = d.molecule(into);
    Molecule* b = d.molecule(from);
    if (!a || !b) return into;
    a->atoms.insert(a->atoms.end(), b->atoms.begin(), b->atoms.end());
    a->bonds.insert(a->bonds.end(), b->bonds.begin(), b->bonds.end());
    a->groups.insert(a->groups.end(), b->groups.begin(), b->groups.end());
    if (a->number.isEmpty() && !b->number.isEmpty()) { a->number = b->number; a->numberManual = b->numberManual; }
    d.molecules.erase(std::remove_if(d.molecules.begin(), d.molecules.end(),
                                     [&](const Molecule& m) { return m.id == from; }),
                      d.molecules.end());
    d.pruneSchemes();
    return into;
}

bool isPlainCarbon(const Atom& a) { return a.el == "C" && a.label.isEmpty() && a.charge == 0 && a.radical == 0; }

// Kekulé pattern for a ring given as ordered atom ids: picks the parity that keeps
// existing double bonds and avoids two double bonds on one atom.
void kekulizeRing(Molecule& m, const QStringList& ring) {
    const int n = ring.size();
    auto bondAt = [&](int i) { return m.bond(m.bondBetween(ring[i], ring[(i + 1) % n])->id); };
    auto hasExternalDouble = [&](const QString& atomId) {
        for (auto* b : m.bondsOf(atomId)) {
            if (b->order < 2) continue;
            QString o = b->other(atomId);
            if (!ring.contains(o)) return true;
            int i = ring.indexOf(atomId), j = ring.indexOf(o);
            if (std::abs(i - j) != 1 && std::abs(i - j) != n - 1) return true;  // non-ring bond inside
        }
        return false;
    };
    auto valid = [&](int parity) {
        for (int i = 0; i < n; ++i) {
            bool dbl = (i % 2 == parity) && !(n % 2 == 1 && i == n - 1);
            Bond* b = bondAt(i);
            if (!dbl && b->order == 2) return false;
            if (dbl && (hasExternalDouble(ring[i]) || hasExternalDouble(ring[(i + 1) % n]))) return false;
        }
        return true;
    };
    int parity = valid(0) ? 0 : valid(1) ? 1 : (bondAt(0)->order == 2 ? 0 : 1);
    for (int i = 0; i < n; ++i) {
        bool dbl = (i % 2 == parity) && !(n % 2 == 1 && i == n - 1);
        if (dbl) bondAt(i)->order = 2;
    }
}

// Create ring atoms at `pts` (ordered), reusing atoms that already sit there; returns ring ids.
QStringList buildRing(Document& d, Molecule*& m, const std::vector<QPointF>& pts, const QStringList& fixed) {
    QStringList ids = fixed;
    for (size_t i = fixed.size(); i < pts.size(); ++i) {
        QString near = atomNear(d, pts[i]);
        if (!near.isEmpty()) {
            Molecule* other = d.moleculeOfAtom(near);
            if (other != m) {
                QString keep = m->id;
                merge(d, keep, other->id);
                m = d.molecule(keep);
            }
            ids << near;
        } else {
            ids << newAtom(d, *m, pts[i]).id;
        }
    }
    for (int i = 0; i < ids.size(); ++i) newBond(d, *m, ids[i], ids[(i + 1) % ids.size()], 1);
    return ids;
}

}  // namespace

QString addAtom(Document& d, QPointF pos, const QString& el) {
    Molecule& m = newMolecule(d);
    return newAtom(d, m, pos, el).id;
}

QString growBond(Document& d, QString atomId, int order, std::optional<QPointF> toward) {
    Molecule* m = d.moleculeOfAtom(atomId);
    if (!m) return {};
    QPointF dir = newBondDirection(*m, atomId, order, toward);
    QPointF p = m->atom(atomId)->pos() + dir;
    return bondToPoint(d, atomId, p, order);
}

QString bondToPoint(Document& d, QString atomId, QPointF pos, int order) {
    Molecule* m = d.moleculeOfAtom(atomId);
    if (!m) return {};
    QString target = atomNear(d, pos);
    if (!target.isEmpty() && target != atomId) {
        bondAtoms(d, atomId, target, order);
        return target;
    }
    QString id = newAtom(d, *m, pos).id;
    newBond(d, *m, atomId, id, order);
    return id;
}

QString bondAtoms(Document& d, QString a, QString b, int order) {
    Molecule* ma = d.moleculeOfAtom(a);
    Molecule* mb = d.moleculeOfAtom(b);
    if (!ma || !mb || a == b) return {};
    QString mid = merge(d, ma->id, mb->id);
    Molecule* m = d.molecule(mid);
    if (const Bond* existing = m->bondBetween(a, b)) {
        m->bond(existing->id)->order = order;
        return existing->id;
    }
    newBond(d, *m, a, b, order);
    return m->bonds.back().id;
}

std::vector<QPointF> chainPoints(const Document& d, std::optional<QString> startAtom, QPointF start, QPointF end,
                                 bool snap) {
    QPointF v = end - start;
    double l = len(v);
    std::vector<QPointF> pts{start};
    if (l < 0.3) return pts;
    double ang = std::atan2(v.y(), v.x());
    if (snap) ang = snapAngle(ang);
    QPointF u(std::cos(ang), std::sin(ang));
    QPointF n = perp(u);
    const double step = std::cos(M_PI / 6);
    int count = std::max(1, (int)std::round(l / step));
    // Zig-zag side: away from the start atom's existing neighbour, else upward.
    double side = n.y() < 0 ? 1 : -1;
    if (startAtom) {
        if (const Molecule* m = d.moleculeOfAtom(*startAtom)) {
            QStringList nbs = m->neighbors(*startAtom);
            if (!nbs.isEmpty()) {
                QPointF w = m->atom(nbs[0])->pos();
                QPointF p1 = start + u * step + n * 0.5, p2 = start + u * step - n * 0.5;
                side = len(p1 - w) >= len(p2 - w) ? 1 : -1;
            }
        }
    }
    for (int i = 1; i <= count; ++i) pts.push_back(start + u * (step * i) + n * ((i % 2) ? 0.5 * side : 0.0));
    return pts;
}

QString addChain(Document& d, std::optional<QString> startAtom, QPointF start, QPointF end, bool snap,
                 std::optional<QString> endAtom) {
    auto pts = chainPoints(d, startAtom, start, end, snap);
    if (pts.size() < 2) return {};
    QString prev = startAtom ? *startAtom : addAtom(d, start);
    for (size_t i = 1; i < pts.size(); ++i) {
        if (i + 1 == pts.size() && endAtom) {
            bondAtoms(d, prev, *endAtom, 1);
            prev = *endAtom;
        } else {
            prev = bondToPoint(d, prev, pts[i], 1);
        }
    }
    return prev;
}

void attachRing(Document& d, QString atomId, int n, bool aromatic, std::optional<QPointF> toward) {
    Molecule* m = d.moleculeOfAtom(atomId);
    if (!m || n < 3) return;
    // A chain end (or lone atom) becomes a ring atom. An atom already inside a chain gets the ring
    // one bond away, as a substituent; a plain ring on a two-bond atom stays spiro, which is valid.
    const int degree = int(m->neighbors(atomId).size());
    QPointF dir;
    if (degree >= 3 || (degree == 2 && aromatic)) {
        QString from = atomId;
        atomId = growBond(d, from, 1, toward);
        m = d.moleculeOfAtom(atomId);
        if (!m || atomId.isEmpty() || m->neighbors(atomId).size() != 1) return;  // ran into an existing atom
        dir = unit(m->atom(atomId)->pos() - m->atom(from)->pos());
    } else {
        dir = degree == 0 ? QPointF(0, 1) : freeDirection(*m, atomId);
    }
    QPointF a = m->atom(atomId)->pos();
    double R = 1.0 / (2 * std::sin(M_PI / n));
    QPointF c = a + dir * R;
    std::vector<QPointF> pts;
    QPointF r0 = a - c;
    for (int i = 0; i < n; ++i) pts.push_back(c + rotate(r0, 2 * M_PI * i / n));
    QString keep = m->id;
    QStringList ring = buildRing(d, m, pts, {atomId});
    if (aromatic) kekulizeRing(*d.molecule(keep), ring);
}

QString attachGroup(Document& d, QString atomId, const QString& name, std::optional<QPointF> toward) {
    Molecule* m = d.moleculeOfAtom(atomId);
    if (!m) return {};
    if (m->neighbors(atomId).size() >= 2) {
        QString from = atomId;
        atomId = growBond(d, from, 1, toward);
        m = d.moleculeOfAtom(atomId);
        if (!m || atomId.isEmpty() || m->neighbors(atomId).size() != 1) return {};  // ran into an existing atom
    }
    setLabel(d, atomId, name);
    return atomId;
}

void fuseRing(Document& d, QString bondId, int n, bool aromatic) {
    Molecule* m = d.moleculeOfBond(bondId);
    if (!m || n < 3) return;
    const Bond b = *m->bond(bondId);
    QPointF A = m->atom(b.a)->pos(), B = m->atom(b.b)->pos();
    QPointF mid = (A + B) / 2, dv = B - A, nrm = unit(perp(dv));
    // Fuse on the side with fewer atoms nearby.
    double score = 0;
    for (auto& at : m->atoms) {
        if (at.id == b.a || at.id == b.b) continue;
        double dd = len(at.pos() - mid);
        if (dd < 2.5) score += (dot(at.pos() - mid, nrm) > 0 ? 1 : -1) / std::max(dd, 0.3);
    }
    double side = score > 0 ? -1 : 1;
    double bl = len(dv);
    double apothem = bl / (2 * std::tan(M_PI / n));
    QPointF c = mid + nrm * side * apothem;
    double sgn = cross(A - c, B - c) > 0 ? 1 : -1;
    std::vector<QPointF> pts{A, B};
    for (int k = 2; k < n; ++k) pts.push_back(c + rotate(B - c, sgn * 2 * M_PI * (k - 1) / n));
    QString keep = m->id;
    QStringList ring = buildRing(d, m, pts, {b.a, b.b});
    if (aromatic) kekulizeRing(*d.molecule(keep), ring);
}

void placeRing(Document& d, QPointF center, int n, bool aromatic) {
    Molecule* m = &newMolecule(d);
    double R = 1.0 / (2 * std::sin(M_PI / n));
    std::vector<QPointF> pts;
    double start = (n % 2 == 0) ? -M_PI / 2 : -M_PI / 2;
    for (int i = 0; i < n; ++i) {
        double t = start + 2 * M_PI * i / n;
        pts.push_back(center + QPointF(std::cos(t), std::sin(t)) * R);
    }
    QString keep = m->id;
    QStringList ring = buildRing(d, m, pts, {});
    if (aromatic) kekulizeRing(*d.molecule(keep), ring);
}

QStringList insertMolecules(Document& d, std::vector<Molecule> input, QPointF at) {
    // Dot-separated SMILES arrive as one molecule with several fragments: split them.
    std::vector<Molecule> mols;
    for (auto& m : input) {
        Document tmp;
        tmp.molecules.push_back(m);
        tmp.fixNextId();
        splitFragments(tmp, m.id);
        for (auto& part : tmp.molecules) mols.push_back(part);
    }
    QStringList ids;
    // Lay several molecules out side by side, centred on `at`.
    double total = 0;
    for (auto& m : mols) total += m.bounds().width() + 1.5;
    double x = at.x() - total / 2;
    for (auto& m : mols) {
        QHash<QString, QString> rename;
        for (auto& a : m.atoms) rename[a.id] = d.newId("a");
        m.renameAtoms(rename);
        for (auto& b : m.bonds) b.id = d.newId("b");
        m.id = d.newId("m");
        QRectF r = m.bounds();
        double w = r.width();
        m.translate(QPointF(x + w / 2, at.y()) - r.center());
        x += w + 1.5;
        ids << m.id;
        d.molecules.push_back(std::move(m));
    }
    return ids;
}

QStringList continueReaction(Document& d, const QStringList& molIds, QPointF tail, QPointF head, QPointF delta) {
    QStringList ids;
    std::vector<Molecule> copies;
    // Map every atom to its copy: unmapped originals get fresh map numbers, which the copy inherits.
    // (Atoms already mapped keep theirs, so a chain of steps stays consistent.)
    int maxMap = 0;
    for (const auto& m : d.molecules) for (const auto& a : m.atoms) maxMap = std::max(maxMap, a.map);
    for (const auto& mid : molIds)
        if (Molecule* src = d.molecule(mid))
            for (auto& a : src->atoms) if (a.map == 0) a.map = ++maxMap;
    for (const auto& mid : molIds) {
        const Molecule* src = d.molecule(mid);
        if (!src) continue;
        Molecule m = *src;
        QHash<QString, QString> rename;
        for (auto& a : m.atoms) rename[a.id] = d.newId("a");
        m.renameAtoms(rename);
        for (auto& b : m.bonds) b.id = d.newId("b");
        m.id = d.newId("m");
        // The product is a new compound: it gets its own number later, not a copy of this one.
        m.number.clear();
        m.numberManual = false;
        m.caption.clear();
        m.translate(delta);
        ids << m.id;
        copies.push_back(std::move(m));
    }
    if (ids.isEmpty()) return ids;
    for (auto& m : copies) d.molecules.push_back(std::move(m));
    Arrow a;
    a.id = d.newId("r");
    a.x1 = tail.x(); a.y1 = tail.y();
    a.x2 = head.x(); a.y2 = head.y();
    d.arrows.push_back(a);
    // Register the step (read from the drawing, as Arrange does) and colour what changes as the copy
    // is edited. The mapping is exact, so mark it as set by hand: nothing should redo it.
    buildScheme(d);
    for (auto& s : d.schemes)
        for (auto& st : s.steps)
            if (st.arrow == a.id) { st.showDiff = true; st.manualMap = true; }
    return ids;
}

QString addArrow(Document& d, QPointF tail, double length) {
    Arrow a;
    a.id = d.newId("r");
    a.x1 = tail.x(); a.y1 = tail.y();
    a.x2 = tail.x() + length; a.y2 = tail.y();
    d.arrows.push_back(a);
    return a.id;
}

QString addText(Document& d, QPointF pos, const QString& text) {
    TextBox t;
    t.id = d.newId("t");
    t.x = pos.x(); t.y = pos.y();
    t.text = text;
    d.texts.push_back(t);
    return t.id;
}

void setElement(Document& d, const QString& atomId, const QString& el) {
    if (Molecule* m = d.moleculeOfAtom(atomId)) {
        Atom* a = m->atom(atomId);
        a->el = el;
        a->label.clear();
    }
}

void setLabel(Document& d, const QString& atomId, const QString& text) {
    Molecule* m = d.moleculeOfAtom(atomId);
    if (!m) return;
    Atom* a = m->atom(atomId);
    QString t = text.trimmed();
    if (t.isEmpty() || t == "C") { a->el = "C"; a->label.clear(); return; }
    if (labels::isElementSymbol(t)) { a->el = t; a->label.clear(); return; }
    a->label = labels::canonical(t);
    // The element of the anchor atom: first token's element where possible.
    QStringList tok = labels::tokens(a->label);
    QString first = tok.isEmpty() ? QString() : tok[0];
    while (!first.isEmpty() && first.back().isDigit()) first.chop(1);
    a->el = labels::isElementSymbol(first) ? first : "C";
}

void changeCharge(Document& d, const QString& atomId, int delta) {
    if (Molecule* m = d.moleculeOfAtom(atomId)) m->atom(atomId)->charge += delta;
}

void toggleRadical(Document& d, const QString& atomId) {
    if (Molecule* m = d.moleculeOfAtom(atomId)) {
        Atom* a = m->atom(atomId);
        a->radical = a->radical ? 0 : 1;
    }
}

void setBondOrder(Document& d, const QString& bondId, int order) {
    if (Molecule* m = d.moleculeOfBond(bondId)) {
        Bond* b = m->bond(bondId);
        b->order = order;
        b->stereo = BondStereo::None;  // 1 on a wedge gives a plain single bond
        b->dbpos = DbPos::Auto;
    }
}

void setStereo(Document& d, const QString& bondId, BondStereo s) {
    Molecule* m = d.moleculeOfBond(bondId);
    if (!m) return;
    Bond* b = m->bond(bondId);
    if (b->stereo == s && s != BondStereo::Wavy) std::swap(b->a, b->b);
    b->stereo = s;
    b->order = 1;
}

void flipBond(Document& d, const QString& bondId) {
    Molecule* m = d.moleculeOfBond(bondId);
    if (!m) return;
    Bond* b = m->bond(bondId);
    if (b->stereo == BondStereo::Wedge || b->stereo == BondStereo::Hash) {
        std::swap(b->a, b->b);
        return;
    }
    if (b->order == 2) {
        // Cycle: one side -> other side -> centred -> first side.
        int side = doubleBondSide(*m, *b);
        b->dbpos = side == 1 ? DbPos::Right : side == -1 ? DbPos::Center : DbPos::Left;
    }
}

void splitFragments(Document& d, const QString& molId) {
    Molecule* m = d.molecule(molId);
    if (!m) return;
    if (m->atoms.empty()) {
        d.molecules.erase(std::remove_if(d.molecules.begin(), d.molecules.end(),
                                         [&](const Molecule& x) { return x.id == molId; }),
                          d.molecules.end());
        d.pruneSchemes();
        return;
    }
    QHash<QString, int> comp;
    int nComp = 0;
    for (auto& a : m->atoms) {
        if (comp.contains(a.id)) continue;
        QQueue<QString> q;
        q.enqueue(a.id);
        comp[a.id] = nComp;
        while (!q.isEmpty()) {
            QString c = q.dequeue();
            for (const QString& nb : m->neighbors(c))
                if (!comp.contains(nb)) { comp[nb] = nComp; q.enqueue(nb); }
        }
        ++nComp;
    }
    if (nComp < 2) return;
    std::vector<int> sizes(nComp, 0);
    for (auto it = comp.begin(); it != comp.end(); ++it) sizes[it.value()]++;
    int keep = int(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
    std::vector<Molecule> extra(nComp);
    for (int c = 0; c < nComp; ++c) if (c != keep) extra[c].id = d.newId("m");
    Molecule kept;
    kept.id = m->id; kept.number = m->number; kept.numberManual = m->numberManual; kept.caption = m->caption;
    kept.showStereo = m->showStereo;
    kept.showCarbons = m->showCarbons;
    for (auto& x : extra) { x.showStereo = m->showStereo; x.showCarbons = m->showCarbons; }
    for (auto& a : m->atoms) (comp[a.id] == keep ? kept : extra[comp[a.id]]).atoms.push_back(a);
    for (auto& b : m->bonds) (comp[b.a] == keep ? kept : extra[comp[b.a]]).bonds.push_back(b);
    for (auto& g : m->groups)
        if (comp.contains(g.anchor)) (comp[g.anchor] == keep ? kept : extra[comp[g.anchor]]).groups.push_back(g);
    *m = kept;
    for (int c = 0; c < nComp; ++c) if (c != keep) d.molecules.push_back(extra[c]);
}

void remove(Document& d, const QString& id) {
    if (Molecule* m = d.moleculeOfAtom(id)) {
        QStringList nbs = m->neighbors(id);
        m->bonds.erase(std::remove_if(m->bonds.begin(), m->bonds.end(), [&](const Bond& b) { return b.has(id); }),
                       m->bonds.end());
        m->atoms.erase(std::remove_if(m->atoms.begin(), m->atoms.end(), [&](const Atom& a) { return a.id == id; }),
                       m->atoms.end());
        // Drop neighbours that are now lone implicit carbons.
        for (const auto& nb : nbs) {
            const Atom* a = m->atom(nb);
            if (a && isPlainCarbon(*a) && m->neighbors(nb).isEmpty())
                m->atoms.erase(std::remove_if(m->atoms.begin(), m->atoms.end(),
                                              [&](const Atom& x) { return x.id == nb; }),
                               m->atoms.end());
        }
        m->pruneGroups();
        splitFragments(d, m->id);
        return;
    }
    if (Molecule* m = d.moleculeOfBond(id)) {
        Bond b = *m->bond(id);
        m->bonds.erase(std::remove_if(m->bonds.begin(), m->bonds.end(), [&](const Bond& x) { return x.id == id; }),
                       m->bonds.end());
        for (const auto& end : {b.a, b.b}) {
            const Atom* a = m->atom(end);
            if (a && isPlainCarbon(*a) && m->neighbors(end).isEmpty())
                m->atoms.erase(std::remove_if(m->atoms.begin(), m->atoms.end(),
                                              [&](const Atom& x) { return x.id == end; }),
                               m->atoms.end());
        }
        m->pruneGroups();
        splitFragments(d, m->id);
        return;
    }
    auto before = d.molecules.size() + d.arrows.size() + d.texts.size();
    d.molecules.erase(std::remove_if(d.molecules.begin(), d.molecules.end(), [&](auto& x) { return x.id == id; }),
                      d.molecules.end());
    d.arrows.erase(std::remove_if(d.arrows.begin(), d.arrows.end(), [&](auto& x) { return x.id == id; }),
                   d.arrows.end());
    d.texts.erase(std::remove_if(d.texts.begin(), d.texts.end(), [&](auto& x) { return x.id == id; }),
                  d.texts.end());
    if (before != d.molecules.size() + d.arrows.size() + d.texts.size()) d.pruneSchemes();
}

void moveObjects(Document& d, const QSet<QString>& ids, QPointF delta) {
    for (auto& m : d.molecules) {
        if (ids.contains(m.id)) { m.translate(delta); continue; }
        QSet<QString> moved;
        for (auto& a : m.atoms) if (ids.contains(a.id)) moved.insert(a.id);
        for (auto& b : m.bonds) if (ids.contains(b.id)) { moved.insert(b.a); moved.insert(b.b); }
        for (auto& a : m.atoms) if (moved.contains(a.id)) { a.x += delta.x(); a.y += delta.y(); }
    }
    for (auto& a : d.arrows) if (ids.contains(a.id)) a.translate(delta);
    for (auto& t : d.texts) if (ids.contains(t.id)) { t.x += delta.x(); t.y += delta.y(); }
}

void transformObjects(Document& d, const QSet<QString>& ids, double m11, double m12, double m21, double m22) {
    QRectF box;
    auto grow = [&](QPointF p) { box |= QRectF(p, QSizeF(1e-6, 1e-6)); };
    for (const auto& m : d.molecules) if (ids.contains(m.id)) for (const auto& a : m.atoms) grow(a.pos());
    for (const auto& a : d.arrows) if (ids.contains(a.id)) for (const auto& p : a.path()) grow(p);
    for (const auto& t : d.texts) if (ids.contains(t.id)) grow({t.x, t.y});
    if (box.isNull()) return;
    const QPointF c = box.center();
    auto map = [&](QPointF p) {
        QPointF v = p - c;
        return c + QPointF(m11 * v.x() + m12 * v.y(), m21 * v.x() + m22 * v.y());
    };
    const bool mirror = m11 * m22 - m12 * m21 < 0;
    for (auto& m : d.molecules) {
        if (!ids.contains(m.id)) continue;
        for (auto& a : m.atoms) a.setPos(map(a.pos()));
        if (mirror)
            for (auto& b : m.bonds) {
                if (b.stereo == BondStereo::Wedge) b.stereo = BondStereo::Hash;
                else if (b.stereo == BondStereo::Hash) b.stereo = BondStereo::Wedge;
                if (b.dbpos == DbPos::Left) b.dbpos = DbPos::Right;
                else if (b.dbpos == DbPos::Right) b.dbpos = DbPos::Left;
            }
    }
    for (auto& a : d.arrows) {
        if (!ids.contains(a.id)) continue;
        QPointF t = map(a.tail()), h = map(a.head());
        a.x1 = t.x(); a.y1 = t.y(); a.x2 = h.x(); a.y2 = h.y();
        for (auto& v : a.via) v = map(v);
    }
    for (auto& t : d.texts) {
        if (!ids.contains(t.id)) continue;
        QPointF p = map({t.x, t.y});
        t.x = p.x(); t.y = p.y();
    }
}

std::vector<Molecule> orientationVariants(const Molecule& m) {
    std::vector<Molecule> out;
    if (m.atoms.empty()) return out;
    const QPointF c = m.center();
    // Principal axis of the atom positions -> rotate it onto the horizontal.
    double sxx = 0, syy = 0, sxy = 0;
    for (const auto& a : m.atoms) {
        QPointF v = a.pos() - c;
        sxx += v.x() * v.x();
        syy += v.y() * v.y();
        sxy += v.x() * v.y();
    }
    const double axis = 0.5 * std::atan2(2 * sxy, sxx - syy);
    auto variant = [&](double extraRot, bool mirror) {
        Molecule v = m;
        double ang = -axis + extraRot;
        double co = std::cos(ang), si = std::sin(ang);
        for (auto& a : v.atoms) {
            QPointF p = a.pos() - c;
            p = QPointF(p.x() * co - p.y() * si, p.x() * si + p.y() * co);
            if (mirror) p.setX(-p.x());
            a.setPos(c + p);
        }
        if (mirror)
            for (auto& b : v.bonds) {
                if (b.stereo == BondStereo::Wedge) b.stereo = BondStereo::Hash;
                else if (b.stereo == BondStereo::Hash) b.stereo = BondStereo::Wedge;
                if (b.dbpos == DbPos::Left) b.dbpos = DbPos::Right;
                else if (b.dbpos == DbPos::Right) b.dbpos = DbPos::Left;
            }
        return v;
    };
    // Same picture? Compare the drawings as sets of (element, position), ignoring atom identity.
    auto signature = [](const Molecule& v) {
        QPointF vc = v.center();
        QStringList s;
        for (const auto& a : v.atoms)
            s << QString("%1:%2:%3").arg(a.label.isEmpty() ? a.el : a.label)
                     .arg(std::round((a.x - vc.x()) * 10)).arg(std::round((a.y - vc.y()) * 10));
        s.sort();
        return s.join(';');
    };
    QSet<QString> seen;
    for (double rot : {0.0, M_PI, M_PI / 2, -M_PI / 2})
        for (bool mirror : {false, true}) {
            Molecule v = variant(rot, mirror);
            QString sig = signature(v);
            if (seen.contains(sig)) continue;
            seen.insert(sig);
            out.push_back(v);
        }
    return out;
}

void contractGroup(Document& d, const QString& molId, const QString& anchorId) {
    Molecule* m = d.molecule(molId);
    if (!m) return;
    auto it = std::find_if(m->groups.begin(), m->groups.end(), [&](const Group& g) { return g.anchor == anchorId; });
    if (it == m->groups.end()) return;
    Group g = *it;
    m->groups.erase(it);
    QSet<QString> drop(g.atoms.begin(), g.atoms.end());
    drop.remove(g.anchor);
    m->bonds.erase(std::remove_if(m->bonds.begin(), m->bonds.end(),
                                  [&](const Bond& b) { return drop.contains(b.a) || drop.contains(b.b); }),
                   m->bonds.end());
    m->atoms.erase(std::remove_if(m->atoms.begin(), m->atoms.end(), [&](const Atom& a) { return drop.contains(a.id); }),
                   m->atoms.end());
    setLabel(d, g.anchor, g.label);
    m = d.molecule(molId);
    m->pruneGroups();
    splitFragments(d, molId);
}

void cycleArrowKind(Document& d, const QString& arrowId) {
    if (Arrow* a = d.arrow(arrowId)) a->kind = ArrowKind((int(a->kind) + 1) % 6);
}

void buildScheme(Document& d) {
    // Steps already known (from an earlier arrange or the reaction builder) are kept for arrows that
    // still exist: the drawing may have been rearranged by hand since, so it can't be re-read reliably.
    std::map<QString, Step> known;
    for (const auto& s : d.schemes)
        for (const auto& st : s.steps) {
            bool live = d.arrow(st.arrow) && !st.reactants.isEmpty() && !st.products.isEmpty();
            for (const auto& list : {st.reactants, st.products})
                for (const auto& id : list) live &= d.molecule(id) != nullptr;
            if (live) known[st.arrow] = st;
        }
    d.schemes.clear();
    // Bent arrows are connectors drawn by hand, not steps: leave them out (and alone).
    std::vector<const Arrow*> arrows;
    for (auto& a : d.arrows)
        if (a.via.empty() && len(a.head() - a.tail()) > 1e-6) arrows.push_back(&a);
    if (arrows.empty()) return;

    // Each arrow is read along its own direction: reactants lie behind its tail,
    // products ahead of its head, reagents beside its shaft (above, or right of a vertical arrow).
    struct Line { QPointF tail, head, dir; double len; bool horizontal; };
    auto lineOf = [](const Arrow* a) {
        QPointF v = a->head() - a->tail();
        return Line{a->tail(), a->head(), unit(v), len(v), std::abs(v.x()) >= std::abs(v.y())};
    };

    QSet<QString> agents;
    for (const auto& [id, st] : known) for (const auto& a : st.agents) agents.insert(a);
    std::map<const Arrow*, QStringList> agentMap;
    for (auto* a : arrows) {
        Line l = lineOf(a);
        for (auto& m : d.molecules) {
            QPointF v = m.center() - l.tail;
            double t = dot(v, l.dir);
            if (t <= 0 || t >= l.len) continue;
            QPointF on = l.tail + l.dir * t;
            bool beside = l.horizontal ? (m.center().y() < on.y() - 0.8 && m.center().y() > on.y() - 5.5)
                                       : (m.center().x() > on.x() + 0.8 && m.center().x() < on.x() + 7);
            if (beside) { agentMap[a] << m.id; agents.insert(m.id); }
        }
    }

    // Molecules on an arrow's line, starting at `origin` and going along `dir`, up to `limit` away,
    // nearest first. A neighbouring arrow's end on the same line closes the window.
    auto onLine = [&](const Arrow* self, QPointF origin, QPointF dir, double limit) {
        for (auto* b : arrows) {
            if (b == self) continue;
            for (QPointF end : {b->tail(), b->head()}) {
                QPointF v = end - origin;
                double along = dot(v, dir);
                if (along > 0.3 && std::abs(cross(dir, v)) < 2.5) limit = std::min(limit, along);
            }
        }
        std::vector<std::pair<double, QString>> hits;
        for (auto& m : d.molecules) {
            if (agents.contains(m.id)) continue;
            QRectF b = m.bounds();
            QPointF v = m.center() - origin;
            double along = dot(v, dir), across = std::abs(cross(dir, v));
            double halfAcross = std::abs(dir.x()) * b.height() / 2 + std::abs(dir.y()) * b.width() / 2;
            if (along > 0 && along < limit && across < std::max(2.5, halfAcross + 1.0)) hits.push_back({along, m.id});
        }
        std::sort(hits.begin(), hits.end());
        QStringList ids;
        for (auto& h : hits) ids << h.second;
        return ids;
    };

    std::vector<Step> steps;
    for (auto* a : arrows) {
        if (auto k = known.find(a->id); k != known.end()) { steps.push_back(k->second); continue; }
        Line l = lineOf(a);
        Step st;
        st.arrow = a->id;
        st.agents = agentMap[a];
        st.reactants = onLine(a, l.tail, -l.dir, 12);
        std::reverse(st.reactants.begin(), st.reactants.end());  // reading order: farthest first
        st.products = onLine(a, l.head, l.dir, 12);
        if (st.products.isEmpty() && l.horizontal && l.dir.x() > 0) {
            // Wrapped row: the product starts the next row down, at the left.
            const Molecule* best = nullptr;
            for (auto& m : d.molecules) {
                if (agents.contains(m.id) || st.reactants.contains(m.id)) continue;
                double dy = m.center().y() - l.head.y();
                if (dy > 2 && dy < 14 && (!best || m.center().x() < best->center().x())) best = &m;
            }
            if (best) st.products << best->id;
        }
        steps.push_back(st);
    }

    // Order steps along the pathway: a step comes after the steps that make its reactants.
    // Starting steps go in reading order (top to bottom, left to right).
    auto pos = [&](const Step& s) { return d.arrow(s.arrow)->tail(); };
    std::vector<int> order, pending(steps.size());
    std::iota(pending.begin(), pending.end(), 0);
    std::sort(pending.begin(), pending.end(), [&](int a, int b) {
        QPointF pa = pos(steps[a]), pb = pos(steps[b]);
        if (std::abs(pa.y() - pb.y()) > 2.5) return pa.y() < pb.y();
        return pa.x() < pb.x();
    });
    std::vector<bool> done(steps.size(), false);
    auto madeBy = [&](const QString& mol, int except) {
        for (size_t i = 0; i < steps.size(); ++i)
            if (int(i) != except && steps[i].products.contains(mol)) return int(i);
        return -1;
    };
    std::function<void(int)> visit = [&](int i) {
        if (done[i]) return;
        done[i] = true;
        for (const auto& r : steps[i].reactants) {
            int p = madeBy(r, i);
            if (p >= 0) visit(p);  // producers first
        }
        order.push_back(i);
    };
    // Depth-first from each starting step, following products into the steps that use them.
    std::function<void(int)> follow = [&](int i) {
        visit(i);
        for (size_t j = 0; j < steps.size(); ++j)
            if (!done[j])
                for (const auto& p : steps[i].products)
                    if (steps[j].reactants.contains(p)) { follow(int(j)); break; }
    };
    for (int i : pending) {
        bool starts = true;
        for (const auto& r : steps[i].reactants) starts &= madeBy(r, i) < 0;
        if (starts) follow(i);
    }
    for (int i : pending) follow(i);  // anything left (cycles)

    Scheme s;
    s.id = d.newId("s");
    for (int i : order) s.steps.push_back(steps[i]);
    d.schemes.push_back(s);
    renumber(d);
}
void renumber(Document& d) {
    QStringList order;
    for (auto& s : d.schemes)
        for (auto& st : s.steps)
            for (const auto& list : {st.reactants, st.products})
                for (const auto& id : list) if (!order.contains(id)) order << id;
    if (d.schemes.empty()) {
        std::vector<const Molecule*> ms;
        for (auto& m : d.molecules) ms.push_back(&m);
        std::sort(ms.begin(), ms.end(), [](auto* a, auto* b) {
            QPointF ca = a->center(), cb = b->center();
            if (std::abs(ca.y() - cb.y()) > 2.5) return ca.y() < cb.y();
            return ca.x() < cb.x();
        });
        for (auto* m : ms) order << m->id;
    }
    QSet<QString> used;
    for (auto& m : d.molecules) if (m.numberManual) used.insert(m.number);
    int n = 1;
    for (const auto& id : order) {
        Molecule* m = d.molecule(id);
        if (!m || m->numberManual) continue;
        while (used.contains(QString::number(n))) ++n;
        m->number = QString::number(n++);
    }
    for (auto& m : d.molecules)
        if (!m.numberManual && !order.contains(m.id)) m.number.clear();
}

}  // namespace cd::edits
