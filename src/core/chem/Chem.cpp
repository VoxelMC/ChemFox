#include "core/chem/Chem.h"

#include "core/model/Geometry.h"
#include "core/model/Labels.h"
#include "core/model/Mapping.h"

#include <GraphMol/CIPLabeler/CIPLabeler.h>
#include <GraphMol/Chirality.h>
#include <GraphMol/Conformer.h>
#include <GraphMol/Depictor/RDDepictor.h>
#include <GraphMol/FMCS/FMCS.h>
#include <GraphMol/SmilesParse/SmartsWrite.h>
#include <GraphMol/Substruct/SubstructMatch.h>
#include <GraphMol/FileParsers/FileParsers.h>
#include <GraphMol/FileParsers/MolFileStereochem.h>
#include <GraphMol/MolOps.h>
#include <GraphMol/PeriodicTable.h>
#include <GraphMol/RWMol.h>
#include <GraphMol/SmilesParse/SmilesParse.h>
#include <GraphMol/SmilesParse/SmilesWrite.h>
#include <GraphMol/inchi.h>
#include <RDGeneral/RDLog.h>

#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <memory>

namespace cd::chem {

namespace {

using RDKit::RWMol;

struct Built {
    std::unique_ptr<RWMol> mol;
    QHash<QString, unsigned> atomIdx;   // model atom -> RDKit index (attachment atom for labels)
    std::vector<QString> owner;         // RDKit index -> model atom id
    QMap<QString, int> extraCounts;     // counts from labels RDKit cannot represent
    bool sanitized = false;
};

void quietRdkit() {
    static bool done = false;
    if (!done) {
        RDLog::InitLogs();
        boost::logging::disable_logs("rdApp.*");
        done = true;
    }
}

RDKit::PeriodicTable* ptable() { return RDKit::PeriodicTable::getTable(); }

int atomicNum(const QString& el) {
    try {
        return ptable()->getAtomicNumber(el.toStdString());
    } catch (...) {
        return 0;
    }
}

// Build an RDKit molecule from the model. Labels with known fragments are expanded.
Built build(const Molecule& m, bool withConformer, bool withMaps = false) {
    quietRdkit();
    Built b;
    b.mol = std::make_unique<RWMol>();
    auto& mol = *b.mol;
    std::vector<std::pair<unsigned, RDGeom::Point3D>> coords;

    for (const auto& a : m.atoms) {
        if (!a.label.isEmpty()) {
            QString frag = labels::fragmentSmiles(a.label);
            std::unique_ptr<RWMol> f;
            if (!frag.isEmpty()) {
                try { f.reset(RDKit::SmilesToMol(frag.toStdString())); } catch (...) {}
            }
            if (f) {
                // Copy fragment atoms except the '*' attachment marker.
                int star = -1;
                for (auto at : f->atoms()) if (at->getAtomicNum() == 0) star = at->getIdx();
                RDKit::MolOps::Kekulize(*f, true);
                std::vector<int> map(f->getNumAtoms(), -1);
                for (auto at : f->atoms()) {
                    if ((int)at->getIdx() == star) continue;
                    RDKit::Atom na(*at);
                    na.setIsAromatic(false);
                    map[at->getIdx()] = mol.addAtom(&na, true, false);
                    b.owner.push_back(a.id);
                }
                for (auto bd : f->bonds()) {
                    int s = map[bd->getBeginAtomIdx()], e = map[bd->getEndAtomIdx()];
                    if (s >= 0 && e >= 0) mol.addBond(s, e, bd->getBondType());
                }
                int attach = 0;
                for (auto bd : f->bonds()) {
                    if ((int)bd->getBeginAtomIdx() == star) attach = map[bd->getEndAtomIdx()];
                    if ((int)bd->getEndAtomIdx() == star) attach = map[bd->getBeginAtomIdx()];
                }
                if (a.charge) mol.getAtomWithIdx(attach)->setFormalCharge(
                    mol.getAtomWithIdx(attach)->getFormalCharge() + a.charge);
                b.atomIdx[a.id] = attach;
                if (withMaps && a.map) mol.getAtomWithIdx(attach)->setAtomMapNum(a.map);
                coords.push_back({(unsigned)attach, RDGeom::Point3D(a.x * 1.5, -a.y * 1.5, 0)});
                continue;
            }
            // Unknown label: dummy atom; count elements from the condensed formula if possible.
            auto counts = labels::parseCondensed(a.label);
            for (auto it = counts.begin(); it != counts.end(); ++it) b.extraCounts[it.key()] += it.value();
            RDKit::Atom dummy(0);
            dummy.setNoImplicit(true);
            unsigned idx = mol.addAtom(&dummy, true, false);
            b.owner.push_back(a.id);
            b.atomIdx[a.id] = idx;
            coords.push_back({idx, RDGeom::Point3D(a.x * 1.5, -a.y * 1.5, 0)});
            continue;
        }
        RDKit::Atom at(atomicNum(a.el));
        at.setFormalCharge(a.charge);
        if (a.isotope > 0) at.setIsotope(a.isotope);
        if (withMaps && a.map) at.setAtomMapNum(a.map);
        unsigned idx = mol.addAtom(&at, true, false);
        b.owner.push_back(a.id);
        b.atomIdx[a.id] = idx;
        coords.push_back({idx, RDGeom::Point3D(a.x * 1.5, -a.y * 1.5, 0)});
    }

    for (const auto& bd : m.bonds) {
        if (!b.atomIdx.contains(bd.a) || !b.atomIdx.contains(bd.b)) continue;
        unsigned s = b.atomIdx[bd.a], e = b.atomIdx[bd.b];
        if (s == e || mol.getBondBetweenAtoms(s, e)) continue;
        auto type = bd.order == 2 ? RDKit::Bond::DOUBLE : bd.order == 3 ? RDKit::Bond::TRIPLE : RDKit::Bond::SINGLE;
        unsigned n = mol.addBond(s, e, type);
        auto rb = mol.getBondWithIdx(n - 1);
        if (bd.stereo == BondStereo::Wedge) rb->setBondDir(RDKit::Bond::BEGINWEDGE);
        else if (bd.stereo == BondStereo::Hash) rb->setBondDir(RDKit::Bond::BEGINDASH);
        else if (bd.stereo == BondStereo::Wavy) rb->setBondDir(RDKit::Bond::UNKNOWN);
    }

    // Implicit hydrogens: let RDKit compute them, then subtract radicals.
    try {
        unsigned failed = 0;
        RDKit::MolOps::sanitizeMol(mol, failed, RDKit::MolOps::SANITIZE_ALL ^ RDKit::MolOps::SANITIZE_PROPERTIES);
        mol.updatePropertyCache(false);
        for (const auto& a : m.atoms) {
            if (!a.label.isEmpty() || a.radical == 0) continue;
            auto at = mol.getAtomWithIdx(b.atomIdx[a.id]);
            int h = std::max(0, (int)at->getTotalNumHs() - a.radical);
            at->setNoImplicit(true);
            at->setNumExplicitHs(h);
            at->setNumRadicalElectrons(a.radical);
        }
        mol.updatePropertyCache(false);
        RDKit::MolOps::setAromaticity(mol);
        RDKit::MolOps::symmetrizeSSSR(mol);
        b.sanitized = (failed == 0);
    } catch (...) {
        mol.updatePropertyCache(false);
    }

    if (withConformer) {
        auto* conf = new RDKit::Conformer(mol.getNumAtoms());
        // Fragment atoms without model coordinates sit on their attachment atom.
        for (unsigned i = 0; i < mol.getNumAtoms(); ++i) {
            const Atom* owner = m.atom(b.owner[i]);
            if (owner) conf->setAtomPos(i, RDGeom::Point3D(owner->x * 1.5, -owner->y * 1.5, 0));
        }
        for (auto& [i, p] : coords) conf->setAtomPos(i, p);
        conf->set3D(false);
        mol.addConformer(conf, true);
    }
    return b;
}

// Stereo as drawn: R/S from wedges and hashes, E/Z from the 2D geometry of double bonds.
void perceiveStereo(RWMol& mol) {
    RDKit::MolOps::assignChiralTypesFromBondDirs(mol);
    RDKit::MolOps::detectBondStereochemistry(mol);
    RDKit::MolOps::assignStereochemistry(mol, true, true);
}

QString hill(const QMap<QString, int>& counts, int charge) {
    QString out;
    auto add = [&](const QString& el) {
        int n = counts.value(el);
        if (n > 0) out += el + (n > 1 ? QString::number(n) : QString());
    };
    if (counts.value("C") > 0) {
        add("C");
        add("H");
        for (auto it = counts.begin(); it != counts.end(); ++it)
            if (it.key() != "C" && it.key() != "H") add(it.key());
    } else {
        for (auto it = counts.begin(); it != counts.end(); ++it) add(it.key());
    }
    if (charge) out += (std::abs(charge) > 1 ? QString::number(std::abs(charge)) : "") + (charge > 0 ? "+" : "-");
    return out;
}

QMap<QString, int> countElements(const Built& b, int* charge) {
    QMap<QString, int> counts = b.extraCounts;
    int q = 0;
    for (auto at : b.mol->atoms()) {
        if (at->getAtomicNum() == 0) continue;
        counts[QString::fromStdString(at->getSymbol())] += 1;
        int h = at->getTotalNumHs();
        if (h) counts["H"] += h;
        q += at->getFormalCharge();
    }
    if (charge) *charge = q;
    return counts;
}

Molecule fromRd(RWMol& mol, bool computeCoords) {
    Molecule m;
    if (!mol.getRingInfo()->isInitialized()) RDKit::MolOps::fastFindRings(mol);
    if (computeCoords || mol.getNumConformers() == 0) {
        RDDepict::preferCoordGen = true;
        RDDepict::compute2DCoords(mol);
    }
    try { RDKit::MolOps::Kekulize(mol, true); } catch (...) {}
    const auto& conf = mol.getConformer();
    RDKit::WedgeMolBonds(mol, &conf);

    // Normalise so the median bond is 1.0.
    std::vector<double> lens;
    for (auto bd : mol.bonds())
        lens.push_back((conf.getAtomPos(bd->getBeginAtomIdx()) - conf.getAtomPos(bd->getEndAtomIdx())).length());
    double scale = 1.5;
    if (!lens.empty()) {
        std::nth_element(lens.begin(), lens.begin() + lens.size() / 2, lens.end());
        if (lens[lens.size() / 2] > 1e-3) scale = lens[lens.size() / 2];
    }

    int n = 0;
    for (auto at : mol.atoms()) {
        Atom a;
        a.id = QString("t%1").arg(n++);  // temporary ids, renamed when inserted
        a.el = QString::fromStdString(at->getSymbol());
        if (at->getAtomicNum() == 0) {
            // CXSMILES atom labels ("*CoA |$;;CoA$|") become abbreviations; otherwise R.
            a.el = "C";
            std::string lab;
            a.label = at->getPropIfPresent(RDKit::common_properties::atomLabel, lab) && !lab.empty()
                          ? QString::fromStdString(lab).remove("_AP1")
                          : QString("R");
        } else {
            std::string lab;
            if (at->getPropIfPresent(RDKit::common_properties::atomLabel, lab) && !lab.empty())
                a.label = labels::canonical(QString::fromStdString(lab));
        }
        auto p = conf.getAtomPos(at->getIdx());
        a.x = p.x / scale;
        a.y = -p.y / scale;
        a.charge = at->getFormalCharge();
        a.radical = at->getNumRadicalElectrons();
        a.map = at->getAtomMapNum();
        a.isotope = (int)at->getIsotope();
        m.atoms.push_back(a);
    }
    n = 0;
    for (auto bd : mol.bonds()) {
        Bond b;
        b.id = QString("u%1").arg(n++);
        b.a = m.atoms[bd->getBeginAtomIdx()].id;
        b.b = m.atoms[bd->getEndAtomIdx()].id;
        switch (bd->getBondType()) {
        case RDKit::Bond::DOUBLE: b.order = 2; break;
        case RDKit::Bond::TRIPLE: b.order = 3; break;
        default: b.order = 1;
        }
        switch (bd->getBondDir()) {
        case RDKit::Bond::BEGINWEDGE: b.stereo = BondStereo::Wedge; break;
        case RDKit::Bond::BEGINDASH: b.stereo = BondStereo::Hash; break;
        case RDKit::Bond::UNKNOWN: b.stereo = BondStereo::Wavy; break;
        default: break;
        }
        m.bonds.push_back(b);
    }
    m.translate(-m.center());
    return m;
}

}  // namespace

double atomicWeight(const QString& element) {
    int z = atomicNum(element);
    return z ? ptable()->getAtomicWeight(z) : 0.0;
}

QString smiles(const Molecule& m) {
    if (m.atoms.empty()) return {};
    try {
        auto b = build(m, true);
        perceiveStereo(*b.mol);
        return QString::fromStdString(RDKit::MolToSmiles(*b.mol));
    } catch (...) {
        return {};
    }
}

MolInfo info(const Molecule& m, bool withInchi) {
    MolInfo out;
    if (m.atoms.empty()) return out;
    try {
        auto b = build(m, true);
        int charge = 0;
        auto counts = countElements(b, &charge);
        out.formula = hill(counts, charge);
        for (auto it = counts.begin(); it != counts.end(); ++it) {
            int z = atomicNum(it.key());
            if (!z) continue;
            out.mw += ptable()->getAtomicWeight(z) * it.value();
            out.exactMass += ptable()->getMostCommonIsotopeMass(z) * it.value();
        }
        out.exactMass -= charge * 0.000548579909;  // electron mass
        perceiveStereo(*b.mol);
        out.smiles = QString::fromStdString(RDKit::MolToSmiles(*b.mol));
        if (withInchi && b.sanitized && b.extraCounts.isEmpty()) {
            RDKit::ExtraInchiReturnValues rv;
            std::string inchi = RDKit::MolToInchi(*b.mol, rv);
            out.inchi = QString::fromStdString(inchi);
            if (!inchi.empty()) out.inchiKey = QString::fromStdString(RDKit::InchiToInchiKey(inchi));
        }
        out.ok = true;
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    return out;
}

void capLonePairs(Document& d) {
    for (auto& m : d.molecules) {
        bool any = false;
        for (const auto& a : m.atoms) any |= a.lonePairs > 0;
        if (!any) continue;
        const Analysis an = analyze(m);
        for (auto& a : m.atoms) {
            if (a.lonePairs <= 0) continue;
            a.lonePairs = std::min(a.lonePairs, an.lonePairs.value(a.id, 0));
            if (a.lonePairs == 0)
                for (auto& c : d.curved) if (c.from.atom == a.id) c.from.lonePair = false;
        }
    }
}

Analysis analyze(const Molecule& m) {
    Analysis out;
    if (m.atoms.empty()) return out;
    Built b;
    try {
        b = build(m, true);
    } catch (...) {
        return out;
    }
    auto& mol = *b.mol;
    out.elementCounts = countElements(b, nullptr);

    for (const auto& a : m.atoms) {
        // Lone pairs, labels included (the O of OMe): (outer electrons - valence - charge - radicals) / 2.
        if (b.atomIdx.contains(a.id)) {
            try {
                auto* at = mol.getAtomWithIdx(b.atomIdx[a.id]);
                if (!b.sanitized) at->updatePropertyCache(false);
                const int outer = ptable()->getNouterElecs(at->getAtomicNum());
                const int free = outer - int(at->getTotalValence()) - at->getFormalCharge() - int(at->getNumRadicalElectrons());
                out.lonePairs[a.id] = std::clamp(free / 2, 0, 4);
            } catch (...) {
                out.lonePairs[a.id] = 0;
            }
        }
        if (!a.label.isEmpty()) continue;
        auto at = mol.getAtomWithIdx(b.atomIdx[a.id]);
        out.implicitH[a.id] = at->getTotalNumHs();

        // Valence check against RDKit's allowed valences of the isoelectronic element.
        int z = at->getAtomicNum() - a.charge;
        if (z < 1 || z > 118) continue;
        const auto& allowed = ptable()->getValenceList(z);
        if (std::find(allowed.begin(), allowed.end(), -1) != allowed.end()) continue;
        int valence = a.radical;
        for (auto* bd : m.bondsOf(a.id)) valence += bd->order;
        int mx = *std::max_element(allowed.begin(), allowed.end());
        if (valence > mx) out.badValence << a.id;
    }

    if (auto* ri = mol.getRingInfo(); ri && ri->isInitialized()) {
        for (const auto& ring : ri->atomRings()) {
            QStringList ids;
            bool ok = true;
            for (int idx : ring) {
                auto at = mol.getAtomWithIdx(idx);
                const QString& owner = b.owner[idx];
                if (!at->getIsAromatic() || b.atomIdx.value(owner, UINT_MAX) != (unsigned)idx) { ok = false; break; }
                ids << owner;
            }
            if (ok) out.aromaticRings.push_back(ids);
        }
    }

    try {
        RDKit::MolOps::assignChiralTypesFromBondDirs(mol);
        auto stereo = RDKit::Chirality::findPotentialStereo(mol, true, true);
        for (const auto& si : stereo) {
            if (si.type != RDKit::Chirality::StereoType::Atom_Tetrahedral) continue;
            if (si.specified != RDKit::Chirality::StereoSpecified::Unspecified) continue;
            const QString& owner = b.owner[si.centeredOn];
            if (b.atomIdx.value(owner) != si.centeredOn) continue;  // inside an abbreviation
            bool wavy = false;
            for (auto* bd : m.bondsOf(owner)) wavy |= bd->stereo == BondStereo::Wavy;
            if (!wavy) out.undefinedStereo << owner;
        }
    } catch (...) {
    }

    // CIP descriptors with RDKit's new labeller, on a fresh copy so nothing above interferes.
    try {
        auto c = build(m, true);
        perceiveStereo(*c.mol);
        RDKit::CIPLabeler::assignCIPLabels(*c.mol, 100000);
        for (auto at : c.mol->atoms()) {
            std::string code;
            if (!at->getPropIfPresent(RDKit::common_properties::_CIPCode, code)) continue;
            const QString& owner = c.owner[at->getIdx()];
            if (c.atomIdx.value(owner) == at->getIdx()) out.cip[owner] = QString::fromStdString(code);
        }
        for (const auto& bd : m.bonds) {
            auto* rb = c.mol->getBondBetweenAtoms(c.atomIdx[bd.a], c.atomIdx[bd.b]);
            std::string code;
            if (rb && rb->getPropIfPresent(RDKit::common_properties::_CIPCode, code))
                out.cip[bd.id] = QString::fromStdString(code);
        }
    } catch (...) {
    }
    return out;
}

std::optional<Molecule> fromSmiles(const QString& smi, QString* error) {
    quietRdkit();
    try {
        std::unique_ptr<RWMol> mol(RDKit::SmilesToMol(smi.trimmed().toStdString()));
        if (!mol) {
            if (error) *error = "Could not parse SMILES";
            return std::nullopt;
        }
        return fromRd(*mol, true);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::optional<Molecule> fromInchi(const QString& inchi, QString* error) {
    quietRdkit();
    try {
        RDKit::ExtraInchiReturnValues rv;
        std::unique_ptr<RWMol> mol(RDKit::InchiToMol(inchi.trimmed().toStdString(), rv));
        if (!mol) {
            if (error) *error = rv.messagePtr.empty() ? "Could not parse InChI" : QString::fromStdString(rv.messagePtr);
            return std::nullopt;
        }
        return fromRd(*mol, true);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

std::vector<Molecule> fromMolBlocks(const QString& text, QString* error) {
    quietRdkit();
    std::vector<Molecule> out;
    for (const QString& block : text.split("$$$$")) {
        if (!block.contains("M  END") && !block.contains("M  V30")) continue;
        try {
            QString b = block;
            while (b.startsWith('\n') || b.startsWith('\r')) b.remove(0, 1);
            std::unique_ptr<RWMol> mol(RDKit::MolBlockToMol(b.toStdString(), true, true, false));
            if (!mol) continue;
            bool flat = true;
            if (mol->getNumConformers()) {
                for (auto& p : mol->getConformer().getPositions()) flat &= p.x == 0 && p.y == 0;
            }
            out.push_back(fromRd(*mol, flat || mol->getNumAtoms() < 2 ? true : false));
        } catch (const std::exception& e) {
            if (error) *error = e.what();
        }
    }
    if (out.empty() && error && error->isEmpty()) *error = "No structures found";
    return out;
}

std::vector<Molecule> fromText(const QString& text, QString* error) {
    QString t = text.trimmed();
    if (t.isEmpty()) return {};
    if (t.startsWith("InChI=")) {
        auto m = fromInchi(t, error);
        return m ? std::vector<Molecule>{*m} : std::vector<Molecule>{};
    }
    if (t.contains("M  END") || t.contains("V3000")) return fromMolBlocks(text, error);
    // One SMILES per line (a .smi file may add a name after whitespace).
    std::vector<Molecule> out;
    for (const QString& line : t.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts)) {
        QString smi = line.trimmed().section(QRegularExpression("\\s+"), 0, 0);
        if (smi.isEmpty()) continue;
        if (auto m = fromSmiles(smi, error)) out.push_back(*m);
        else return {};
    }
    return out;
}

QString toMolBlock(const Molecule& m) {
    try {
        auto b = build(m, true);
        perceiveStereo(*b.mol);
        return QString::fromStdString(RDKit::MolToMolBlock(*b.mol, true, -1, true));
    } catch (...) {
        return {};
    }
}

bool canExpand(const Atom& a) { return !a.label.isEmpty() && !labels::fragmentSmiles(a.label).isEmpty(); }

static bool expandLabelImpl(Document& d, const QString& atomId);

bool expandLabel(Document& d, QString atomId) {
    // RDKit can throw; an exception reaching Qt's event loop would abort the app.
    try {
        return expandLabelImpl(d, atomId);
    } catch (...) {
        return false;
    }
}

static bool expandLabelImpl(Document& d, const QString& atomId) {
    quietRdkit();
    Molecule* m = d.moleculeOfAtom(atomId);
    if (!m || !canExpand(*m->atom(atomId))) return false;
    std::unique_ptr<RWMol> f;
    try {
        f.reset(RDKit::SmilesToMol(labels::fragmentSmiles(m->atom(atomId)->label).toStdString()));
    } catch (...) {
    }
    if (!f) return false;
    RDDepict::preferCoordGen = true;
    RDDepict::compute2DCoords(*f);
    try { RDKit::MolOps::Kekulize(*f, true); } catch (...) {}
    const auto& conf = f->getConformer();
    int star = -1, attach = -1;
    for (auto at : f->atoms()) if (at->getAtomicNum() == 0) star = at->getIdx();
    if (star < 0) return false;
    for (auto nb : f->atomNeighbors(f->getAtomWithIdx(star))) attach = nb->getIdx();
    if (attach < 0) return false;

    // Fragment coordinates, y down, scaled to bond length 1 along the star-attach bond.
    auto fp = [&](int i) { auto p = conf.getAtomPos(i); return QPointF(p.x, -p.y); };
    const Atom anchor = *m->atom(atomId);
    QStringList nbs = m->neighbors(atomId);
    QPointF nbPos = nbs.isEmpty() ? anchor.pos() - QPointF(1, 0) : m->atom(nbs[0])->pos();
    QPointF src = fp(attach) - fp(star), dst = anchor.pos() - nbPos;
    double scale = geom::len(dst) / std::max(geom::len(src), 1e-6);
    double rot = std::atan2(dst.y(), dst.x()) - std::atan2(src.y(), src.x());

    // Two placements (mirror images across the bond); keep the one that crowds the rest least.
    auto place = [&](int i, bool mirror) {
        QPointF v = (fp(i) - fp(star)) * scale;
        v = geom::rotate(v, rot);
        if (mirror) {
            QPointF u = geom::unit(dst);
            v = 2 * geom::dot(v, u) * u - v;  // reflect across the bond axis
        }
        return nbPos + v;
    };
    auto crowding = [&](bool mirror) {
        double worst = 1e9;
        for (auto at : f->atoms()) {
            int i = at->getIdx();
            if (i == star || i == attach) continue;
            QPointF p = place(i, mirror);
            for (const auto& a : m->atoms)
                if (a.id != atomId) worst = std::min(worst, geom::len(a.pos() - p));
        }
        return worst;
    };
    const bool mirror = crowding(true) > crowding(false);

    Group g{anchor.label, atomId, {atomId}};
    std::vector<QString> ids(f->getNumAtoms());
    for (auto at : f->atoms()) {
        int i = at->getIdx();
        if (i == star) continue;
        if (i == attach) {
            Atom* a = m->atom(atomId);
            a->el = QString::fromStdString(at->getSymbol());
            a->label.clear();
            a->charge = anchor.charge + at->getFormalCharge();
            ids[i] = atomId;
            continue;
        }
        Atom na;
        na.id = d.newId("a");
        na.el = QString::fromStdString(at->getSymbol());
        na.charge = at->getFormalCharge();
        na.setPos(place(i, mirror));
        m->atoms.push_back(na);
        ids[i] = na.id;
        g.atoms << na.id;
    }
    for (auto bd : f->bonds()) {
        int s = bd->getBeginAtomIdx(), e = bd->getEndAtomIdx();
        if (s == star || e == star) continue;
        Bond nb;
        nb.id = d.newId("b");
        nb.a = ids[s];
        nb.b = ids[e];
        nb.order = bd->getBondType() == RDKit::Bond::DOUBLE ? 2 : bd->getBondType() == RDKit::Bond::TRIPLE ? 3 : 1;
        m->bonds.push_back(nb);
    }
    m->groups.push_back(g);
    return true;
}

namespace {

// Wedge/hash directions encode stereo for the current coordinates only; drop them before a re-layout.
// (Call after perceiveStereo, so the chirality itself is already recorded on the atoms.)
bool stripWedges(RWMol& mol) {
    bool had = false;
    for (auto bd : mol.bonds()) {
        auto dir = bd->getBondDir();
        if (dir == RDKit::Bond::BEGINWEDGE || dir == RDKit::Bond::BEGINDASH) {
            had = true;
            bd->setBondDir(RDKit::Bond::NONE);
        }
    }
    return had;
}

// After a re-layout the old wedges may mean the other enantiomer: pick new ones for the new coordinates.
void rederiveWedges(Molecule& m, Built& b) {
    RDKit::WedgeMolBonds(*b.mol, &b.mol->getConformer());
    for (auto& bd : m.bonds) {
        if (bd.stereo == BondStereo::Wedge || bd.stereo == BondStereo::Hash) bd.stereo = BondStereo::None;
        auto* rb = b.mol->getBondBetweenAtoms(b.atomIdx[bd.a], b.atomIdx[bd.b]);
        if (!rb) continue;
        auto dir = rb->getBondDir();
        if (dir != RDKit::Bond::BEGINWEDGE && dir != RDKit::Bond::BEGINDASH) continue;
        bd.stereo = dir == RDKit::Bond::BEGINWEDGE ? BondStereo::Wedge : BondStereo::Hash;
        if (b.atomIdx[bd.a] != rb->getBeginAtomIdx()) std::swap(bd.a, bd.b);  // narrow end first
    }
}

}  // namespace

bool cleanup(Molecule& m) {    if (m.atoms.size() < 2) return false;
    try {
        // Read the stereo from the drawing first (E/Z from geometry, R/S from wedges)...
        auto b = build(m, true);
        perceiveStereo(*b.mol);
        bool hadWedges = stripWedges(*b.mol);
        // ...then lay out fresh; RDKit's depiction honours the E/Z flags.
        b.mol->clearConformers();
        RDDepict::preferCoordGen = true;
        RDDepict::compute2DCoords(*b.mol);
        const auto& conf = b.mol->getConformer();
        if (hadWedges) rederiveWedges(m, b);
        for (auto& bd : m.bonds) bd.dbpos = DbPos::Auto;  // sides chosen for the old layout no longer apply
        QPointF c0 = m.center();
        std::vector<QPointF> pts;
        for (auto& a : m.atoms) {
            auto p = conf.getAtomPos(b.atomIdx[a.id]);
            pts.emplace_back(p.x, -p.y);
        }
        std::vector<double> lens;
        for (auto& bd : m.bonds) {
            int i = 0, j = 0;
            for (int k = 0; k < (int)m.atoms.size(); ++k) {
                if (m.atoms[k].id == bd.a) i = k;
                if (m.atoms[k].id == bd.b) j = k;
            }
            QPointF d = pts[i] - pts[j];
            lens.push_back(std::hypot(d.x(), d.y()));
        }
        double scale = 1.5;
        if (!lens.empty()) {
            std::nth_element(lens.begin(), lens.begin() + lens.size() / 2, lens.end());
            if (lens[lens.size() / 2] > 1e-3) scale = lens[lens.size() / 2];
        }
        for (size_t k = 0; k < m.atoms.size(); ++k) m.atoms[k].setPos(pts[k] / scale);
        m.translate(c0 - m.center());
        return true;
    } catch (...) {
        return false;
    }
}

bool alignTo(Molecule& m, const Molecule& ref) {
    if (m.atoms.size() < 2 || ref.atoms.size() < 2) return false;
    try {
        quietRdkit();
        auto mb = build(m, true);
        auto rb = build(ref, true);  // its conformer holds the reference drawing
        perceiveStereo(*mb.mol);

        // Which atoms correspond: shared atom-map numbers if the reaction is mapped, else the MCS.
        RDKit::MatchVectType match;  // (reference index, molecule index)
        QHash<int, unsigned> refByMap;
        for (const auto& a : ref.atoms) if (a.map) refByMap[a.map] = rb.atomIdx[a.id];
        for (const auto& a : m.atoms)
            if (a.map && refByMap.contains(a.map)) match.push_back({int(refByMap[a.map]), int(mb.atomIdx[a.id])});
        if (match.size() < 3) {
            match.clear();
            RDKit::MCSParameters params;
            params.Timeout = 2;
            params.setMCSAtomTyperFromEnum(RDKit::AtomCompareElements);
            params.setMCSBondTyperFromEnum(RDKit::BondCompareAny);
            params.BondCompareParameters.RingMatchesRingOnly = true;
            params.AtomCompareParameters.RingMatchesRingOnly = true;
            std::vector<RDKit::ROMOL_SPTR> pair{RDKit::ROMOL_SPTR(new RWMol(*rb.mol)), RDKit::ROMOL_SPTR(new RWMol(*mb.mol))};
            RDKit::MCSResult res = RDKit::findMCS(pair, &params);
            RDKit::ROMOL_SPTR query = res.QueryMol;
            if (!query && !res.SmartsString.empty()) query.reset(RDKit::SmartsToMol(res.SmartsString));
            if (!query || res.NumAtoms < 2) return false;
            RDKit::MatchVectType qr, qm;
            if (!RDKit::SubstructMatch(*rb.mol, *query, qr) || !RDKit::SubstructMatch(*mb.mol, *query, qm)) return false;
            for (auto& [q1, r1] : qr)
                for (auto& [q2, m2] : qm)
                    if (q1 == q2) match.push_back({r1, m2});
        }
        if (match.size() < 2) return false;

        // Redraw: the shared atoms take the reference's coordinates, the rest is laid out around them.
        auto attempt = [&](const RDKit::MatchVectType& pins) {
            Molecule out = m;
            auto ob = build(out, true);
            perceiveStereo(*ob.mol);
            bool hadWedges = stripWedges(*ob.mol);
            ob.mol->clearConformers();
            // RDKit's own engine pins the matched atoms exactly (CoordGen only treats them as a guide).
            RDDepict::generateDepictionMatching2DStructure(*ob.mol, *rb.mol, pins, -1, true);
            if (hadWedges) rederiveWedges(out, ob);
            for (auto& bd : out.bonds) bd.dbpos = DbPos::Auto;
            // Same units as the reference (model * 1.5, y up); keep the molecule where it was on the page.
            const auto& conf = ob.mol->getConformer();
            QPointF c0 = out.center();
            for (auto& a : out.atoms) {
                auto p = conf.getAtomPos(ob.atomIdx[a.id]);
                a.setPos(QPointF(p.x, -p.y) / 1.5);
            }
            out.translate(c0 - out.center());
            return out;
        };

        // Pinned atoms can force the reference's geometry onto a double bond or stereocentre that differs
        // in the product (or matches it the other way round). The product's own E/Z and R/S must survive:
        // unpin the atoms around anything that changed and lay out again; give up rather than change them.
        const auto stereo = analyze(m).cip;
        for (int tries = 0; tries < 6 && match.size() >= 2; ++tries) {
            Molecule out = attempt(match);
            const auto now = analyze(out).cip;
            QSet<QString> pinned;
            for (const auto& [r, mi] : match)
                for (const auto& a : m.atoms) if (int(mb.atomIdx[a.id]) == mi) pinned.insert(a.id);
            // Atoms reachable from `start` without crossing the bond start–`not`.
            auto side = [&](const QString& start, const QString& notVia) {
                QSet<QString> seen{start};
                QStringList todo{start};
                while (!todo.isEmpty()) {
                    QString x = todo.takeLast();
                    for (const auto& nb : m.neighbors(x)) {
                        if ((x == start && nb == notVia) || seen.contains(nb)) continue;
                        seen.insert(nb);
                        todo << nb;
                    }
                }
                return seen;
            };
            QSet<QString> unpin;
            for (auto it = stereo.begin(); it != stereo.end(); ++it) {
                if (now.value(it.key()) == it.value()) continue;
                QStringList centre;
                if (const Bond* b = m.bond(it.key())) {
                    centre = {b->a, b->b};
                    // A double bond outside a ring: free its whole smaller side (by pinned atoms), so that
                    // side is laid out in the product's own geometry instead of the reference's.
                    QSet<QString> sa = side(b->a, b->b), sb = side(b->b, b->a);
                    if (!sa.contains(b->b)) {
                        const QSet<QString>& small = QSet<QString>(sa).intersect(pinned).size() <=
                                                     QSet<QString>(sb).intersect(pinned).size() ? sa : sb;
                        unpin |= small;
                    }
                } else {
                    centre = {it.key()};
                }
                for (const auto& c : centre) {
                    unpin.insert(c);
                    for (const auto& nb : m.neighbors(c)) unpin.insert(nb);
                }
            }
            if (unpin.isEmpty()) { m = out; return true; }
            QSet<int> drop;
            for (const auto& id : unpin) if (mb.atomIdx.contains(id)) drop.insert(int(mb.atomIdx[id]));
            const size_t before = match.size();
            match.erase(std::remove_if(match.begin(), match.end(), [&](const auto& p) { return drop.contains(p.second); }),
                        match.end());
            if (match.size() == before) break;  // nothing left to unpin that would help
        }
        return false;
    } catch (...) {
        return false;
    }
}
// ---------------------------------------------------------------------------
// Reactions

ReactionText splitReaction(const QString& text) {
    ReactionText r;
    QStringList parts = text.trimmed().split('>');
    auto comps = [](const QString& s) {
        QStringList out;
        for (const QString& c : s.split('.', Qt::SkipEmptyParts)) out << c.trimmed();
        return out;
    };
    if (parts.size() == 3) {
        r.reactants = comps(parts[0]);
        r.agents = comps(parts[1]);
        r.products = comps(parts[2]);
    } else if (parts.size() == 1) {
        r.reactants = comps(parts[0]);
    }
    return r;
}

QString joinReaction(const ReactionText& r) {
    return r.reactants.join('.') + ">" + r.agents.join('.') + ">" + r.products.join('.');
}

std::optional<Molecule> fromSmilesOrSmarts(const QString& text, QString* error) {
    quietRdkit();
    const std::string s = text.trimmed().toStdString();
    if (s.empty()) {
        if (error) *error = "Empty";
        return std::nullopt;
    }
    try {
        std::unique_ptr<RWMol> mol(RDKit::SmilesToMol(s));
        if (mol) return fromRd(*mol, true);
    } catch (...) {
    }
    try {
        std::unique_ptr<RWMol> q(RDKit::SmartsToMol(s));
        if (!q) {
            if (error) *error = "Not valid SMILES or SMARTS";
            return std::nullopt;
        }
        // Turn query atoms and bonds into plain ones so the pattern can be drawn.
        RWMol plain;
        for (auto at : q->atoms()) {
            RDKit::Atom na(at->getAtomicNum());
            na.setFormalCharge(at->getFormalCharge());
            na.setAtomMapNum(at->getAtomMapNum());
            na.setIsotope(at->getIsotope());
            na.setIsAromatic(at->getIsAromatic());
            plain.addAtom(&na, true, false);
        }
        for (auto bd : q->bonds()) {
            auto type = bd->getBondType();
            if (type != RDKit::Bond::SINGLE && type != RDKit::Bond::DOUBLE && type != RDKit::Bond::TRIPLE &&
                type != RDKit::Bond::AROMATIC)
                type = RDKit::Bond::SINGLE;
            unsigned n = plain.addBond(bd->getBeginAtomIdx(), bd->getEndAtomIdx(), type);
            if (type == RDKit::Bond::AROMATIC) plain.getBondWithIdx(n - 1)->setIsAromatic(true);
        }
        try {
            RDKit::MolOps::sanitizeMol(plain);
        } catch (...) {
            plain.updatePropertyCache(false);
        }
        return fromRd(plain, true);
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

QString smilesWithMaps(const Molecule& m) {
    if (m.atoms.empty()) return {};
    try {
        auto b = build(m, true, true);
        perceiveStereo(*b.mol);
        return QString::fromStdString(RDKit::MolToSmiles(*b.mol));
    } catch (...) {
        return {};
    }
}

namespace {

// A molecule taking part in mapping, with which of its atoms are still free.
struct MapWork {
    Molecule* model;
    Built built;
    std::vector<bool> used;   // by RDKit index
    std::vector<bool> mappable;  // RDKit index is a model atom (not inside an abbreviation)
};

// Copy of `w` without its used atoms; `orig` receives the original index of each kept atom.
RDKit::ROMOL_SPTR freePart(const MapWork& w, std::vector<int>& orig) {
    auto* sub = new RWMol(*w.built.mol);
    orig.clear();
    sub->beginBatchEdit();
    for (unsigned i = 0; i < w.used.size(); ++i) {
        if (w.used[i] || !w.mappable[i]) sub->removeAtom(i);
        else orig.push_back(int(i));
    }
    sub->commitBatchEdit();
    sub->updatePropertyCache(false);
    RDKit::MolOps::fastFindRings(*sub);
    return RDKit::ROMOL_SPTR(sub);
}

}  // namespace

// One greedy mapping pass. If forceP/forceR are set, the first piece must come from that pair.
static int greedyMap(const std::vector<Molecule*>& reactants, const std::vector<Molecule*>& products, int forceP,
                     int forceR) {
    auto prepare = [](Molecule* m) {
        for (auto& a : m->atoms) a.map = 0;
        MapWork w{m, build(*m, false), {}, {}};
        unsigned n = w.built.mol->getNumAtoms();
        w.used.assign(n, false);
        w.mappable.assign(n, false);
        for (const auto& a : m->atoms) w.mappable[w.built.atomIdx[a.id]] = true;
        return w;
    };
    std::vector<MapWork> rs, ps;
    for (auto* m : reactants) rs.push_back(prepare(m));
    for (auto* m : products) ps.push_back(prepare(m));

    int next = 1;
    auto assign = [&](MapWork& p, int pi, MapWork& r, int ri) {
        p.used[pi] = r.used[ri] = true;
        int n = next++;
        p.model->atom(p.built.owner[pi])->map = n;
        r.model->atom(r.built.owner[ri])->map = n;
    };

    RDKit::MCSParameters params;
    params.Timeout = 2;
    params.setMCSAtomTyperFromEnum(RDKit::AtomCompareElements);
    params.setMCSBondTyperFromEnum(RDKit::BondCompareAny);
    params.BondCompareParameters.RingMatchesRingOnly = false;
    params.BondCompareParameters.CompleteRingsOnly = false;
    params.AtomCompareParameters.RingMatchesRingOnly = false;

    // Greedy: repeatedly take the largest common piece between any product and reactant.
    for (int round = 0; round < 64; ++round) {
        int bestSize = 1;
        MapWork *bp = nullptr, *br = nullptr;
        std::vector<std::pair<int, int>> bestPairs;
        for (int pi = 0; pi < (int)ps.size(); ++pi)
            for (int ri = 0; ri < (int)rs.size(); ++ri) {
                if (round == 0 && forceP >= 0 && (pi != forceP || ri != forceR)) continue;
                MapWork& p = ps[pi];
                MapWork& r = rs[ri];
                std::vector<int> po, ro;
                auto sp = freePart(p, po), sr = freePart(r, ro);
                if (sp->getNumAtoms() < 2 || sr->getNumAtoms() < 2) continue;
                RDKit::MCSResult res;
                try {
                    std::vector<RDKit::ROMOL_SPTR> pair{sp, sr};
                    res = RDKit::findMCS(pair, &params);
                } catch (...) {
                    continue;
                }
                if ((int)res.NumAtoms <= bestSize) continue;
                RDKit::ROMOL_SPTR query = res.QueryMol;
                if (!query) {
                    try { query.reset(RDKit::SmartsToMol(res.SmartsString)); } catch (...) {}
                }
                if (!query) continue;
                RDKit::MatchVectType mp, mr;
                if (!RDKit::SubstructMatch(*sp, *query, mp) || !RDKit::SubstructMatch(*sr, *query, mr)) continue;
                std::vector<std::pair<int, int>> pairs;
                for (auto& [qi, pi] : mp)
                    for (auto& [qj, rj] : mr)
                        if (qi == qj) pairs.push_back({po[pi], ro[rj]});
                bestSize = int(res.NumAtoms);
                bestPairs = pairs;
                bp = &p;
                br = &r;
            }
        if (!bp) break;
        for (auto& [pi, ri] : bestPairs) assign(*bp, pi, *br, ri);
    }

    // Single atoms left over: same element, preferring one next to the matching mapped neighbour.
    for (auto& p : ps) {
        for (unsigned pi = 0; pi < p.used.size(); ++pi) {
            if (p.used[pi] || !p.mappable[pi]) continue;
            auto pat = p.built.mol->getAtomWithIdx(pi);
            QSet<int> nbMaps;
            for (auto nb : p.built.mol->atomNeighbors(pat))
                if (const Atom* a = p.model->atom(p.built.owner[nb->getIdx()])) if (a->map) nbMaps.insert(a->map);
            MapWork* bestR = nullptr;
            int bestRi = -1, bestScore = -1;
            for (auto& r : rs)
                for (unsigned ri = 0; ri < r.used.size(); ++ri) {
                    if (r.used[ri] || !r.mappable[ri]) continue;
                    auto rat = r.built.mol->getAtomWithIdx(ri);
                    if (rat->getAtomicNum() != pat->getAtomicNum()) continue;
                    int score = 0;
                    for (auto nb : r.built.mol->atomNeighbors(rat))
                        if (const Atom* a = r.model->atom(r.built.owner[nb->getIdx()]))
                            if (a->map && nbMaps.contains(a->map)) ++score;
                    if (score > bestScore) { bestScore = score; bestR = &r; bestRi = int(ri); }
                }
            if (bestR) assign(p, int(pi), *bestR, bestRi);
        }
    }
    return next - 1;
}

static int autoMapImpl(const std::vector<Molecule*>& reactants, const std::vector<Molecule*>& products);

int autoMap(const std::vector<Molecule*>& reactants, const std::vector<Molecule*>& products) {
    try {
        return autoMapImpl(reactants, products);
    } catch (...) {
        for (const auto& list : {reactants, products})
            for (auto* m : list) for (auto& a : m->atoms) a.map = 0;  // no half-finished mapping
        return 0;
    }
}

static int autoMapImpl(const std::vector<Molecule*>& reactants, const std::vector<Molecule*>& products) {
    quietRdkit();
    // Minimum chemical distance: try each product/reactant pair as the first match and keep
    // the mapping that breaks and forms the fewest bonds (then the one mapping the most atoms).
    std::vector<std::pair<int, int>> starts{{-1, -1}};
    for (int p = 0; p < (int)products.size(); ++p)
        for (int r = 0; r < (int)reactants.size(); ++r) starts.push_back({p, r});
    if (starts.size() > 13) starts.resize(13);

    auto snapshot = [&] {
        std::vector<std::vector<int>> s;
        for (const auto& list : {reactants, products})
            for (auto* m : list) {
                s.emplace_back();
                for (auto& a : m->atoms) s.back().push_back(a.map);
            }
        return s;
    };
    auto restore = [&](const std::vector<std::vector<int>>& s) {
        size_t k = 0;
        for (const auto& list : {reactants, products})
            for (auto* m : list) {
                for (size_t i = 0; i < m->atoms.size(); ++i) m->atoms[i].map = s[k][i];
                ++k;
            }
    };
    std::vector<const Molecule*> cr(reactants.begin(), reactants.end()), cp(products.begin(), products.end());

    int bestEdits = INT_MAX, bestMapped = -1;
    std::vector<std::vector<int>> best;
    for (auto [p, r] : starts) {
        int mapped = greedyMap(reactants, products, p, r);
        int edits = int(mapping::changeList(cr, cp).size());
        if (edits < bestEdits || (edits == bestEdits && mapped > bestMapped)) {
            bestEdits = edits;
            bestMapped = mapped;
            best = snapshot();
        }
    }
    if (!best.empty()) restore(best);
    return std::max(bestMapped, 0);
}

}  // namespace cd::chem
