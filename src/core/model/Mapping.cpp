#include "core/model/Mapping.h"

#include <algorithm>

namespace cd::mapping {

namespace {

struct Side {
    QHash<int, QPair<const Molecule*, const Atom*>> byMap;
    std::vector<const Molecule*> mols;
};

Side side(const std::vector<const Molecule*>& mols) {
    Side s;
    for (const Molecule* m : mols) {
        s.mols.push_back(m);
        for (const auto& a : m->atoms)
            if (a.map > 0 && !s.byMap.contains(a.map)) s.byMap[a.map] = {m, &a};
    }
    return s;
}

std::vector<const Molecule*> molecules(const Document& d, const QStringList& ids) {
    std::vector<const Molecule*> out;
    for (const auto& id : ids)
        if (const Molecule* m = d.molecule(id)) out.push_back(m);
    return out;
}

int mapOf(const Molecule& m, const QString& atomId) {
    const Atom* a = m.atom(atomId);
    return a ? a->map : 0;
}

// Bond on the other side between the atoms carrying maps ma and mb, or null.
const Bond* counterpart(const Side& other, int ma, int mb) {
    auto ia = other.byMap.find(ma), ib = other.byMap.find(mb);
    if (ia == other.byMap.end() || ib == other.byMap.end()) return nullptr;
    if (ia->first != ib->first) return nullptr;  // different molecules: no bond
    return ia->first->bondBetween(ia->second->id, ib->second->id);
}

// Compare one side's bonds to the other's. `lost` is Broken for reactants, Formed for products.
void compare(const Side& from, const Side& to, Change lost, std::vector<BondChange>& out) {
    for (const Molecule* m : from.mols) {
        for (const auto& b : m->bonds) {
            int ma = mapOf(*m, b.a), mb = mapOf(*m, b.b);
            if (ma == 0 && mb == 0) continue;  // nothing known about this bond
            if (ma == 0 || mb == 0) {
                // A mapped atom bonded to an unmapped one: the partner left (or arrived).
                int mapped = ma ? ma : mb;
                if (to.byMap.contains(mapped)) out.push_back({m, &b, lost});
                continue;
            }
            if (!to.byMap.contains(ma) || !to.byMap.contains(mb)) continue;
            const Bond* c = counterpart(to, ma, mb);
            if (!c) out.push_back({m, &b, lost});
            else if (c->order != b.order) out.push_back({m, &b, Change::Changed});
        }
    }
}

}  // namespace

std::vector<BondChange> changeList(const std::vector<const Molecule*>& reactants,
                                   const std::vector<const Molecule*>& products) {
    std::vector<BondChange> out;
    Side r = side(reactants), p = side(products);
    if (r.byMap.isEmpty() || p.byMap.isEmpty()) return out;
    compare(r, p, Change::Broken, out);
    compare(p, r, Change::Formed, out);
    return out;
}

QHash<QString, Change> bondChanges(const Document& d, const Step& s) {
    QHash<QString, Change> out;
    for (const auto& c : changeList(molecules(d, s.reactants), molecules(d, s.products))) out[c.bond->id] = c.change;
    return out;
}

QHash<QString, Change> allBondChanges(const Document& d) {
    QHash<QString, Change> out;
    for (const auto& sc : d.schemes)
        for (const auto& st : sc.steps)
            if (st.showDiff) out.insert(bondChanges(d, st));
    return out;
}

namespace {

Atom* findAtom(Document& d, const QStringList& molIds, const QString& atomId) {
    for (const auto& id : molIds)
        if (Molecule* m = d.molecule(id))
            if (Atom* a = m->atom(atomId)) return a;
    return nullptr;
}

template <class F>
void forEachAtom(Document& d, const Step& s, F f) {
    for (const auto& list : {s.reactants, s.products})
        for (const auto& id : list)
            if (Molecule* m = d.molecule(id))
                for (auto& a : m->atoms) f(a);
}

}  // namespace

Step* stepLinking(Document& d, const QString& atomA, const QString& atomB, QString* reactantAtom,
                  QString* productAtom) {
    for (auto& sc : d.schemes)
        for (auto& st : sc.steps) {
            if (findAtom(d, st.reactants, atomA) && findAtom(d, st.products, atomB)) {
                *reactantAtom = atomA;
                *productAtom = atomB;
                return &st;
            }
            if (findAtom(d, st.reactants, atomB) && findAtom(d, st.products, atomA)) {
                *reactantAtom = atomB;
                *productAtom = atomA;
                return &st;
            }
        }
    return nullptr;
}

Step* stepOfAtom(Document& d, const QString& atomId) {
    for (auto& sc : d.schemes)
        for (auto& st : sc.steps)
            if (findAtom(d, st.reactants, atomId) || findAtom(d, st.products, atomId)) return &st;
    return nullptr;
}

void link(Document& d, Step& s, const QString& reactantAtom, const QString& productAtom) {
    Atom* r = findAtom(d, s.reactants, reactantAtom);
    Atom* p = findAtom(d, s.products, productAtom);
    if (!r || !p) return;
    int highest = 0;
    forEachAtom(d, s, [&](Atom& a) { highest = std::max(highest, a.map); });
    int n = r->map ? r->map : p->map ? p->map : highest + 1;
    int oldR = r->map, oldP = p->map;
    // The number now belongs to these two only; old partners become unmapped.
    forEachAtom(d, s, [&](Atom& a) {
        if (&a == r || &a == p) return;
        if (a.map == n || (oldR && a.map == oldR) || (oldP && a.map == oldP)) a.map = 0;
    });
    r->map = p->map = n;
    s.manualMap = true;
}

void unlink(Document& d, Step& s, const QString& atomId) {
    Atom* a = findAtom(d, s.reactants, atomId);
    if (!a) a = findAtom(d, s.products, atomId);
    if (!a || !a->map) return;
    int n = a->map;
    forEachAtom(d, s, [&](Atom& x) { if (x.map == n) x.map = 0; });
    s.manualMap = true;
}

QStringList partners(const Document& d, const Step& s, const QString& atomId) {
    int n = 0;
    for (const auto& list : {s.reactants, s.products})
        for (const auto& id : list)
            if (const Molecule* m = d.molecule(id))
                if (const Atom* a = m->atom(atomId)) n = a->map;
    QStringList out;
    if (!n) return out;
    for (const auto& list : {s.reactants, s.products})
        for (const auto& id : list)
            if (const Molecule* m = d.molecule(id))
                for (const auto& a : m->atoms)
                    if (a.map == n && a.id != atomId) out << a.id;
    return out;
}

bool stepIsMapped(const Document& d, const Step& s) {
    for (const auto& list : {s.reactants, s.products})
        for (const auto& id : list)
            if (const Molecule* m = d.molecule(id))
                for (const auto& a : m->atoms)
                    if (a.map > 0) return true;
    return false;
}

QStringList unmappedProductAtoms(const Document& d, const Step& s) {
    QStringList out;
    if (!stepIsMapped(d, s)) return out;
    for (const auto& id : s.products)
        if (const Molecule* m = d.molecule(id))
            for (const auto& a : m->atoms)
                if (a.map == 0 && a.el != "H") out << a.id;
    return out;
}

}  // namespace cd::mapping
