#include "warnings/Warnings.h"

#include "core/model/Geometry.h"
#include "core/model/Labels.h"
#include "core/model/Mapping.h"
#include "style/Style.h"

#include <QSettings>
#include <algorithm>

namespace cd::warnings {

QString title(const QString& code) {
    static const QHash<QString, QString> t = {
        {"W101", "Unusual valence"},
        {"W102", "Undefined stereocentre"},
        {"W201", "Unbalanced atoms"},
        {"W202", "Arrow without product"},
        {"W203", "Incomplete atom mapping"},
        {"W301", "Overlapping atoms"},
        {"W302", "Inconsistent bond lengths"},
        {"W303", "Inconsistent abbreviation"},
        {"W401", "Unknown style setting"},
    };
    return t.value(code, code);
}

QStringList allCodes() { return {"W101", "W102", "W201", "W202", "W203", "W301", "W302", "W303", "W401"}; }

std::vector<Warning> check(const Document& d, const QHash<QString, chem::Analysis>& analyses,
                           const std::vector<StyleIssue>& styleIssues) {
    std::vector<Warning> out;

    // W1xx chemistry
    for (const auto& m : d.molecules) {
        auto it = analyses.find(m.id);
        if (it == analyses.end()) continue;
        for (const auto& id : it->badValence) {
            const Atom* a = m.atom(id);
            out.push_back({"W101", m.id, id, QString("%1 has an unusual valence").arg(a ? a->el : "Atom")});
        }
        for (const auto& id : it->undefinedStereo)
            out.push_back({"W102", m.id, id, "Stereocentre without a wedge or hash"});
    }

    // W2xx reactions
    auto heavy = [&](const QStringList& ids) {
        QMap<QString, int> c;
        for (const auto& id : ids) {
            auto it = analyses.find(id);
            if (it == analyses.end()) continue;
            for (auto e = it->elementCounts.begin(); e != it->elementCounts.end(); ++e)
                if (e.key() != "H") c[e.key()] += e.value();
        }
        return c;
    };
    QSet<QString> arrowsInSchemes;
    for (const auto& s : d.schemes)
        for (const auto& st : s.steps) {
            arrowsInSchemes.insert(st.arrow);
            QStringList unmapped = mapping::unmappedProductAtoms(d, st);
            if (!unmapped.isEmpty())
                out.push_back({"W203", st.arrow, {},
                               QString("%1 product atom%2 not mapped to a reactant atom")
                                   .arg(unmapped.size()).arg(unmapped.size() == 1 ? " is" : "s are")});
            if (st.products.isEmpty()) {
                out.push_back({"W202", st.arrow, {}, "Arrow has no product"});
                continue;
            }
            if (st.reactants.isEmpty()) continue;
            auto in = heavy(st.reactants + st.agents), prod = heavy(st.products);
            QStringList missing;
            for (auto e = prod.begin(); e != prod.end(); ++e)
                if (e.value() > in.value(e.key())) missing << QString("%1 (%2 > %3)").arg(e.key()).arg(e.value()).arg(in.value(e.key()));
            if (!missing.isEmpty())
                out.push_back({"W201", st.arrow, {}, "Products contain atoms not in reactants or reagents: " + missing.join(", ")});
        }
    for (const auto& a : d.arrows) {
        if (arrowsInSchemes.contains(a.id) || a.kind == ArrowKind::Resonance) continue;
        // No scheme: look for a molecule just past the arrow head.
        auto path = a.path();
        QPointF h = a.head(), dir = geom::unit(path[path.size() - 1] - path[path.size() - 2]);
        bool found = false;
        for (const auto& m : d.molecules) {
            QPointF c = m.center() - h;
            double along = geom::dot(c, dir), across = std::abs(geom::cross(dir, c));
            if (along > 0 && along < 8 && across < 2.5) { found = true; break; }
        }
        if (!found) out.push_back({"W202", a.id, {}, "Arrow has no product"});
    }

    // W3xx drawing
    std::vector<std::pair<const Molecule*, const Atom*>> atoms;
    for (const auto& m : d.molecules)
        for (const auto& a : m.atoms) atoms.push_back({&m, &a});
    QSet<QString> overlapped;
    for (size_t i = 0; i < atoms.size(); ++i)
        for (size_t j = i + 1; j < atoms.size(); ++j) {
            if (geom::len(atoms[i].second->pos() - atoms[j].second->pos()) < 0.25 &&
                !overlapped.contains(atoms[j].second->id)) {
                overlapped.insert(atoms[j].second->id);
                out.push_back({"W301", atoms[j].first->id, atoms[j].second->id, "Atoms overlap"});
            }
        }

    std::vector<double> lens;
    for (const auto& m : d.molecules)
        for (const auto& b : m.bonds) lens.push_back(geom::len(m.atom(b.a)->pos() - m.atom(b.b)->pos()));
    if (lens.size() >= 3) {
        std::vector<double> sorted = lens;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        double med = sorted[sorted.size() / 2];
        for (const auto& m : d.molecules)
            for (const auto& b : m.bonds) {
                double l = geom::len(m.atom(b.a)->pos() - m.atom(b.b)->pos());
                if (med > 0 && std::abs(l / med - 1) > 0.2)
                    out.push_back({"W302", m.id, b.id,
                                   QString("Bond is %1% of the usual length").arg(int(std::round(100 * l / med)))});
            }
    }

    // W303: the same group written two ways (OMe and OCH3, CO2H and COOH).
    QHash<QString, QHash<QString, int>> spellings;  // fragment -> label -> count
    for (const auto& m : d.molecules)
        for (const auto& a : m.atoms) {
            QString f = labels::fragmentSmiles(a.label);
            if (!f.isEmpty()) spellings[f][a.label]++;
        }
    for (auto it = spellings.begin(); it != spellings.end(); ++it) {
        if (it->size() < 2) continue;
        QString common;
        int best = -1;
        for (auto s = it->begin(); s != it->end(); ++s)
            if (s.value() > best) { best = s.value(); common = s.key(); }
        for (const auto& m : d.molecules)
            for (const auto& a : m.atoms)
                if (labels::fragmentSmiles(a.label) == it.key() && a.label != common)
                    out.push_back({"W303", m.id, a.id, QString("%1 is also written %2 in this document").arg(a.label, common)});
    }

    // W4xx style
    for (const auto& si : styleIssues)
        out.push_back({"W401", "style", QString::number(si.line), QString("Line %1: %2").arg(si.line).arg(si.message)});
    return out;
}

std::vector<Warning> active(const std::vector<Warning>& all, const Document& d, const QSet<QString>& globalCodes) {
    std::vector<Warning> out;
    for (const auto& w : all) {
        if (globalCodes.contains(w.code) || d.suppressedCodes.contains(w.code) ||
            d.suppressedInstances.contains(w.key()))
            continue;
        out.push_back(w);
    }
    return out;
}

QSet<QString> globalSilenced() {
    QSettings s;
    QStringList l = s.value("warnings/silenced").toStringList();
    return QSet<QString>(l.begin(), l.end());
}

void setGlobalSilenced(const QSet<QString>& codes) {
    QSettings s;
    QStringList l(codes.begin(), codes.end());
    l.sort();
    s.setValue("warnings/silenced", l);
}

}  // namespace cd::warnings
