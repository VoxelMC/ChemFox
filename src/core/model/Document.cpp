#include "core/model/Document.h"

#include <QRegularExpression>
#include <algorithm>

namespace cd {

Atom* Molecule::atom(const QString& id) {
    for (auto& a : atoms) if (a.id == id) return &a;
    return nullptr;
}
const Atom* Molecule::atom(const QString& id) const {
    for (auto& a : atoms) if (a.id == id) return &a;
    return nullptr;
}
Bond* Molecule::bond(const QString& id) {
    for (auto& b : bonds) if (b.id == id) return &b;
    return nullptr;
}
const Bond* Molecule::bond(const QString& id) const {
    for (auto& b : bonds) if (b.id == id) return &b;
    return nullptr;
}
const Bond* Molecule::bondBetween(const QString& a, const QString& b) const {
    for (auto& bd : bonds)
        if ((bd.a == a && bd.b == b) || (bd.a == b && bd.b == a)) return &bd;
    return nullptr;
}
std::vector<const Bond*> Molecule::bondsOf(const QString& atomId) const {
    std::vector<const Bond*> out;
    for (auto& b : bonds) if (b.has(atomId)) out.push_back(&b);
    return out;
}
QStringList Molecule::neighbors(const QString& atomId) const {
    QStringList out;
    for (auto& b : bonds) if (b.has(atomId)) out << b.other(atomId);
    return out;
}
const Group* Molecule::groupOf(const QString& atomId) const {
    for (const auto& g : groups)
        if (g.atoms.contains(atomId)) return &g;
    return nullptr;
}

void Molecule::pruneGroups() {
    for (auto& g : groups) {
        QStringList keep;
        for (const auto& id : g.atoms) if (atom(id)) keep << id;
        g.atoms = keep;
    }
    groups.erase(std::remove_if(groups.begin(), groups.end(), [&](const Group& g) { return !atom(g.anchor); }),
                 groups.end());
}

void Molecule::renameAtoms(const QHash<QString, QString>& rename) {
    for (auto& a : atoms) a.id = rename.value(a.id, a.id);
    for (auto& b : bonds) { b.a = rename.value(b.a, b.a); b.b = rename.value(b.b, b.b); }
    for (auto& g : groups) {
        g.anchor = rename.value(g.anchor, g.anchor);
        for (auto& id : g.atoms) id = rename.value(id, id);
    }
}

QRectF Molecule::bounds() const {
    if (atoms.empty()) return {};
    double x0 = atoms[0].x, x1 = x0, y0 = atoms[0].y, y1 = y0;
    for (auto& a : atoms) {
        x0 = std::min(x0, a.x); x1 = std::max(x1, a.x);
        y0 = std::min(y0, a.y); y1 = std::max(y1, a.y);
    }
    return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}
QPointF Molecule::center() const { return bounds().center(); }
void Molecule::translate(QPointF d) {
    for (auto& a : atoms) { a.x += d.x(); a.y += d.y(); }
}

QString arrowKindName(ArrowKind k) {
    switch (k) {
    case ArrowKind::Forward: return "forward";
    case ArrowKind::Equilibrium: return "equilibrium";
    case ArrowKind::Retro: return "retrosynthesis";
    case ArrowKind::Dashed: return "dashed";
    case ArrowKind::Crossed: return "crossed";
    case ArrowKind::Resonance: return "resonance";
    }
    return "forward";
}
ArrowKind arrowKindFromName(const QString& s) {
    for (auto k : {ArrowKind::Forward, ArrowKind::Equilibrium, ArrowKind::Retro, ArrowKind::Dashed,
                   ArrowKind::Crossed, ArrowKind::Resonance})
        if (arrowKindName(k) == s) return k;
    return ArrowKind::Forward;
}

Molecule* Document::molecule(const QString& id) {
    for (auto& m : molecules) if (m.id == id) return &m;
    return nullptr;
}
const Molecule* Document::molecule(const QString& id) const {
    for (auto& m : molecules) if (m.id == id) return &m;
    return nullptr;
}
Molecule* Document::moleculeOfAtom(const QString& atomId) {
    for (auto& m : molecules) if (m.atom(atomId)) return &m;
    return nullptr;
}
const Molecule* Document::moleculeOfAtom(const QString& atomId) const {
    for (auto& m : molecules) if (m.atom(atomId)) return &m;
    return nullptr;
}
Molecule* Document::moleculeOfBond(const QString& bondId) {
    for (auto& m : molecules) if (m.bond(bondId)) return &m;
    return nullptr;
}
const Molecule* Document::moleculeOfBond(const QString& bondId) const {
    for (auto& m : molecules) if (m.bond(bondId)) return &m;
    return nullptr;
}
CurvedArrow* Document::curvedArrow(const QString& id) {
    for (auto& c : curved) if (c.id == id) return &c;
    return nullptr;
}
const CurvedArrow* Document::curvedArrow(const QString& id) const {
    for (auto& c : curved) if (c.id == id) return &c;
    return nullptr;
}

bool Document::resolve(const CurvedEnd& e, QPointF* pos) const {
    if (!e.atom.isEmpty()) {
        const Molecule* m = moleculeOfAtom(e.atom);
        if (!m) return false;
        if (pos) *pos = m->atom(e.atom)->pos();
        return true;
    }
    if (!e.bond.isEmpty()) {
        const Molecule* m = moleculeOfBond(e.bond);
        if (!m) return false;
        const Bond* b = m->bond(e.bond);
        if (pos) *pos = (m->atom(b->a)->pos() + m->atom(b->b)->pos()) / 2;
        return true;
    }
    if (pos) *pos = e.pos();
    return true;
}

void Document::pruneCurved() {
    if (curved.empty()) return;
    curved.erase(std::remove_if(curved.begin(), curved.end(),
                                [&](const CurvedArrow& c) { return !resolve(c.from, nullptr) || !resolve(c.to, nullptr); }),
                 curved.end());
}

Arrow* Document::arrow(const QString& id) {
    for (auto& a : arrows) if (a.id == id) return &a;
    return nullptr;
}
const Arrow* Document::arrow(const QString& id) const {
    for (auto& a : arrows) if (a.id == id) return &a;
    return nullptr;
}
TextBox* Document::text(const QString& id) {
    for (auto& t : texts) if (t.id == id) return &t;
    return nullptr;
}

void Document::pruneSchemes() {
    auto keep = [&](QStringList& ids) {
        ids.erase(std::remove_if(ids.begin(), ids.end(), [&](const QString& id) { return !molecule(id); }),
                  ids.end());
    };
    for (auto& s : schemes) {
        for (auto& st : s.steps) {
            keep(st.reactants); keep(st.agents); keep(st.products);
            if (!arrow(st.arrow)) st.arrow.clear();
        }
        s.steps.erase(std::remove_if(s.steps.begin(), s.steps.end(),
                                     [](const Step& st) { return st.arrow.isEmpty(); }),
                      s.steps.end());
    }
    schemes.erase(std::remove_if(schemes.begin(), schemes.end(), [](const Scheme& s) { return s.steps.empty(); }),
                  schemes.end());
}

void Document::fixNextId() {
    static const QRegularExpression re("^[a-z]+(\\d+)$");
    int mx = 0;
    auto see = [&](const QString& id) {
        auto m = re.match(id);
        if (m.hasMatch()) mx = std::max(mx, m.captured(1).toInt());
    };
    for (auto& m : molecules) {
        see(m.id);
        for (auto& a : m.atoms) see(a.id);
        for (auto& b : m.bonds) see(b.id);
    }
    for (auto& a : arrows) see(a.id);
    for (auto& t : texts) see(t.id);
    for (auto& c : curved) see(c.id);
    for (auto& s : schemes) see(s.id);
    nextId = std::max(nextId, mx + 1);
}

}  // namespace cd
