#include "core/chem/Chem.h"
#include "core/commands/Edits.h"
#include "core/model/Geometry.h"
#include "core/model/Labels.h"
#include "editor/Templates.h"
#include "io/Emf.h"
#include "io/Export.h"

#include <QDir>
#include <QFile>
#include "io/Native.h"
#include "render/Layout.h"
#include "style/Style.h"
#include "warnings/Warnings.h"

#include "app.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cd;
using Catch::Approx;

namespace {

Molecule fromSmiles(const char* s) {
    auto m = chem::fromSmiles(s);
    REQUIRE(m.has_value());
    return *m;
}

}  // namespace

TEST_CASE("labels tokenise, flip and subscript") {
    CHECK(labels::tokens("CO2Et") == QStringList{"C", "O2", "Et"});
    CHECK(labels::flipped("CO2Et") == "EtO2C");
    CHECK(labels::flipped("NHBoc") == "BocHN");
    CHECK(labels::flipped("OMe") == "MeO");
    CHECK(labels::canonical("MeO") == "OMe");
    CHECK(labels::canonical("EtO2C") == "CO2Et");
    auto counts = labels::parseCondensed("N(CH3)2");
    CHECK(counts["C"] == 2);
    CHECK(counts["H"] == 6);
    CHECK(counts["N"] == 1);

    auto runs = labels::formulaRuns("NaOH, H2O");
    QString subs;
    for (auto& r : runs) if (r.script == Script::Sub) subs += r.text;
    CHECK(subs == "2");
    runs = labels::formulaRuns("rt, 2 h");
    for (auto& r : runs) CHECK(r.script == Script::Normal);
    runs = labels::formulaRuns("Pd(PPh3)4");
    subs.clear();
    for (auto& r : runs) if (r.script == Script::Sub) subs += r.text;
    CHECK(subs == "34");
}

TEST_CASE("style language: inheritance, units, unknown settings") {
    Style acs = styles::resolveNamed("acs");
    CHECK(acs.bondLength == Approx(14.4));
    CHECK(acs.lineWidth == Approx(0.6));
    CHECK(acs.boldWidth == Approx(2.0));
    CHECK(acs.bondSpacing == Approx(0.18));
    CHECK(acs.margin == Approx(1.6));
    CHECK(acs.hashSpacing == Approx(2.5));
    CHECK(acs.font == "Arial");
    CHECK(acs.fontSize == Approx(10));

    std::vector<StyleIssue> issues;
    Style s = styles::resolve("style \"mine\" based on acs\n  bond length: 5 mm\n  sparkle: yes\n  arrows\n    head: open\n  text size: large\n",
                              &issues);
    CHECK(s.name == "mine");
    CHECK(s.bondLength == Approx(5 * 72 / 25.4));
    CHECK(s.lineWidth == Approx(0.6));  // inherited
    CHECK_FALSE(s.arrowHeadFilled);
    CHECK(s.fontSize == Approx(14));
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].line == 3);

    // Built-in styles load cleanly (hex colours are not comments).
    for (const auto& name : styles::builtinNames()) {
        std::vector<StyleIssue> none;
        styles::resolveNamed(name, &none);
        CHECK(none.empty());
    }
    CHECK(styles::resolveNamed("default").formedBonds == QColor("#1f9d55"));

    Style slides = styles::resolveNamed("slides");
    CHECK(slides.fontSize == Approx(16));
    CHECK(slides.lineWidth == Approx(1.6));
}

TEST_CASE("chemistry: formula, mass, SMILES") {
    Molecule aspirin = fromSmiles("CC(=O)Oc1ccccc1C(=O)O");
    auto info = chem::info(aspirin);
    REQUIRE(info.ok);
    CHECK(info.formula == "C9H8O4");
    CHECK(info.mw == Approx(180.159).epsilon(0.001));
    CHECK(info.exactMass == Approx(180.0423).epsilon(0.0001));
    CHECK(info.inchiKey == "BSYNRYMUTXBXSQ-UHFFFAOYSA-N");
    // Imported structures are Kekulé with 2D coordinates around bond length 1.
    int doubles = 0;
    for (auto& b : aspirin.bonds) doubles += b.order == 2;
    CHECK(doubles == 5);
}

TEST_CASE("chemistry: labels count toward the formula") {
    Document d;
    edits::placeRing(d, {0, 0}, 6, true);
    QString atom = d.molecules[0].atoms[0].id;
    QString sub = edits::growBond(d, atom, 1);
    edits::setLabel(d, sub, "OMe");
    auto info = chem::info(d.molecules[0]);
    CHECK(info.formula == "C7H8O");  // anisole
    CHECK(info.smiles == "COc1ccccc1");
}

TEST_CASE("warnings: valence, overlap, unbalanced, silencing") {
    Document d;
    QString c = edits::addAtom(d, {0, 0});
    for (int i = 0; i < 5; ++i) edits::bondToPoint(d, c, QPointF(std::cos(i * 1.25), std::sin(i * 1.25)), 1);
    QHash<QString, chem::Analysis> an;
    for (auto& m : d.molecules) an[m.id] = chem::analyze(m);
    auto ws = warnings::check(d, an, {});
    bool w101 = false;
    for (auto& w : ws) w101 |= w.code == "W101" && w.subId == c;
    CHECK(w101);

    d.suppressedCodes.insert("W101");
    auto active = warnings::active(ws, d, {});
    for (auto& w : active) CHECK(w.code != "W101");
}

TEST_CASE("edits: rings, fusion, chains, delete") {
    Document d;
    edits::placeRing(d, {0, 0}, 6, true);
    REQUIRE(d.molecules.size() == 1);
    Molecule& m = d.molecules[0];
    int doubles = 0;
    for (auto& b : m.bonds) doubles += b.order == 2;
    CHECK(doubles == 3);
    // Fuse a second benzene onto a single bond -> naphthalene.
    QString single;
    for (auto& b : m.bonds) if (b.order == 1) { single = b.id; break; }
    edits::fuseRing(d, single, 6, true);
    REQUIRE(d.molecules.size() == 1);
    CHECK(d.molecules[0].atoms.size() == 10);
    CHECK(d.molecules[0].bonds.size() == 11);
    CHECK(chem::info(d.molecules[0]).formula == "C10H8");

    Document c;
    edits::addChain(c, std::nullopt, {0, 0}, {4.4, 0}, true);
    REQUIRE(c.molecules.size() == 1);
    CHECK(c.molecules[0].atoms.size() == 6);
    for (auto& b : c.molecules[0].bonds) {
        double l = geom::len(c.molecules[0].atom(b.a)->pos() - c.molecules[0].atom(b.b)->pos());
        CHECK(l == Approx(1.0).epsilon(0.001));
    }
    // Deleting a middle bond splits the chain into two molecules.
    edits::remove(c, c.molecules[0].bonds[2].id);
    CHECK(c.molecules.size() == 2);
}

TEST_CASE("native JSON round trip keeps everything") {
    Document d;
    edits::placeRing(d, {0, 0}, 6, true);
    edits::addArrow(d, {3, 0});
    d.arrows[0].above = "NaOH, H2O";
    d.arrows[0].below = "rt, 2 h";
    d.molecules[0].number = "3a";
    d.molecules[0].numberManual = true;
    d.suppressedCodes.insert("W201");
    d.styleName = "acs";
    QByteArray json = io::toJson(d);
    auto back = io::fromJson(json);
    REQUIRE(back.has_value());
    CHECK(back->molecules.size() == 1);
    CHECK(back->molecules[0].bonds.size() == 6);
    CHECK(back->molecules[0].number == "3a");
    CHECK(back->arrows[0].above == "NaOH, H2O");
    CHECK(back->suppressedCodes.contains("W201"));
    CHECK(back->styleName == "acs");
    CHECK(json.contains("\"smiles\": \"c1ccccc1\""));

    // Files from the future are refused, not misread.
    QByteArray future = json;
    future.replace("\"version\": 1", "\"version\": 99");
    CHECK_FALSE(io::fromJson(future).has_value());
}

TEST_CASE("SVG export embeds the document; scheme layout and numbering") {
    testApp();
    Document d;
    auto ins = edits::insertMolecules(d, {fromSmiles("CC(=O)O"), fromSmiles("OCC")}, {0, 0});
    edits::insertMolecules(d, {fromSmiles("CCOC(C)=O")}, {12, 0});
    edits::addArrow(d, {6, 0}, 3);
    d.arrows[0].above = "H2SO4";
    edits::buildScheme(d);
    REQUIRE(d.schemes.size() == 1);
    REQUIRE(d.schemes[0].steps.size() == 1);
    CHECK(d.schemes[0].steps[0].reactants.size() == 2);
    CHECK(d.schemes[0].steps[0].products.size() == 1);
    CHECK(d.molecule(ins[0])->number == "1");

    Style style = styles::resolveNamed("default");
    Renderer r(style);
    ChemOverlay ov;
    layout::layoutSchemes(d, r, ov);
    // Arrow sits between reactants and product after layout.
    double arrowX = d.arrows[0].x1;
    for (auto& id : d.schemes[0].steps[0].reactants) CHECK(d.molecule(id)->center().x() < arrowX);
    CHECK(d.molecule(d.schemes[0].steps[0].products[0])->center().x() > d.arrows[0].x2);

    QByteArray svg = io::svgBytes(d, r, ov);
    CHECK(svg.contains("<metadata>"));
    auto back = io::documentFromSvg(svg);
    REQUIRE(back.has_value());
    CHECK(back->molecules.size() == 3);
    CHECK(back->schemes.size() == 1);

    QImage img = io::renderImage(d, r, ov, 150);
    CHECK(img.width() > 100);

    if (io::emfSupported()) {
        QString path = QDir::temp().filePath("chemfox_test.emf");
        QFile::remove(path);
        REQUIRE(io::exportEmf(path, d, r, ov));
        QFile f(path);
        REQUIRE(f.open(QIODevice::ReadOnly));
        QByteArray head = f.read(44);
        CHECK(f.size() > 2000);
        CHECK(head.mid(40, 4) == QByteArray(" EMF"));  // EMR_HEADER signature
    }
}

TEST_CASE("stereo survives clean; E/Z reaches SMILES and InChI") {
    // trans- and cis-resveratrol
    const char* trans = "Oc1ccc(/C=C/c2cc(O)cc(O)c2)cc1";
    const char* cis = "Oc1ccc(/C=C\\c2cc(O)cc(O)c2)cc1";
    Molecule t = fromSmiles(trans), z = fromSmiles(cis);
    QString canonTrans = chem::smiles(t), canonCis = chem::smiles(z);
    CHECK(canonTrans.contains('/'));
    CHECK(canonTrans != canonCis);
    CHECK(chem::info(t).inchiKey == "LUKBXSAWLPMMSZ-OWOJBTEDSA-N");  // (E)-resveratrol

    for (int i = 0; i < 3; ++i) {
        REQUIRE(chem::cleanup(t));
        REQUIRE(chem::cleanup(z));
    }
    CHECK(chem::smiles(t) == canonTrans);
    CHECK(chem::smiles(z) == canonCis);

    // R/S survives too: wedges are re-derived for the new coordinates.
    Molecule ala = fromSmiles("C[C@@H](C(=O)O)N");
    QString before = chem::smiles(ala);
    for (auto& a : ala.atoms) { a.x = -a.x; }  // mirror the drawing and its wedges...
    for (auto& b : ala.bonds)
        if (b.stereo == BondStereo::Wedge) b.stereo = BondStereo::Hash;
        else if (b.stereo == BondStereo::Hash) b.stereo = BondStereo::Wedge;
    CHECK(chem::smiles(ala) == before);  // ...which is the same molecule
    REQUIRE(chem::cleanup(ala));
    CHECK(chem::smiles(ala) == before);
}

TEST_CASE("chain double bonds sit beside the bond, terminal ones are centred") {
    Molecule stilbene = fromSmiles("c1ccccc1/C=C/c1ccccc1");
    for (auto& b : stilbene.bonds) {
        if (b.order != 2 || !geom::smallestRing(stilbene, b).isEmpty()) continue;
        CHECK(geom::doubleBondSide(stilbene, b) != 0);  // trans alkene: offset, not centred
    }
    Molecule acetone = fromSmiles("CC(C)=O");
    for (auto& b : acetone.bonds)
        if (b.order == 2) CHECK(geom::doubleBondSide(acetone, b) == 0);
}

TEST_CASE("markup: bold names, italic prefixes, formulas left alone where they aren't") {
    auto runs = labels::markupRuns("*trans*-cinnamic acid");
    REQUIRE(runs.size() >= 2);
    CHECK(runs[0].text == "trans");
    CHECK(runs[0].italic);
    CHECK_FALSE(runs[1].italic);
    // Bold text (enzyme names) is never subscripted; long numbers aren't formula counts.
    for (auto& r : labels::markupRuns("**C4H**")) CHECK(r.script == Script::Normal);
    for (auto& r : labels::markupRuns("cytochrome P450")) CHECK(r.script == Script::Normal);
    bool sub = false;
    for (auto& r : labels::markupRuns("O2, NADPH")) sub |= r.script == Script::Sub;
    CHECK(sub);
    // A lone asterisk stays literal.
    QString all;
    for (auto& r : labels::markupRuns("3 * 2")) all += r.text;
    CHECK(all == "3 * 2");
}

TEST_CASE("figure features round-trip: bent arrows, colours, text size, CXSMILES labels") {
    Document d;
    QString id = edits::addArrow(d, {0, 0}, 3);
    d.arrow(id)->via = {QPointF(0, 2), QPointF(4, 2)};
    d.arrow(id)->color = "#2e7d4f";
    QString t = edits::addText(d, {1, 1}, "**HEADER**");
    d.text(t)->size = 8;
    d.text(t)->color = "#555555";
    auto back = io::fromJson(io::toJson(d));
    REQUIRE(back);
    CHECK(back->arrows[0].via.size() == 2);
    CHECK(back->arrows[0].via[1] == QPointF(4, 2));
    CHECK(back->arrows[0].color == "#2e7d4f");
    CHECK(back->texts[0].size == 8);
    CHECK(back->texts[0].color == "#555555");

    // "*" with a CXSMILES label becomes an abbreviation, and CoA counts toward the formula.
    auto m = chem::fromSmiles("*SC(=O)C |$CoA;;;;$|");
    REQUIRE(m);
    bool coa = false;
    for (auto& a : m->atoms) coa |= a.label == "CoA";
    CHECK(coa);
    CHECK(chem::info(*m).formula == "C23H38N7O17P3S");  // acetyl-CoA
}

TEST_CASE("the resveratrol sample loads as placed (it is a working file, so contents may change)") {
    QString path = QStringLiteral(CHEMFOX_SOURCE_DIR) + "/samples/resveratrol_biosynthesis.chemfox";
    auto d = io::loadFile(path);
    REQUIRE(d);
    CHECK_FALSE(d->needsLayout);
    CHECK(d->molecules.size() >= 7);
    bool bent = false;
    for (auto& a : d->arrows) bent |= !a.via.empty();
    CHECK(bent);
}

TEST_CASE("flip, rotate, expand and collapse labels") {
    Document d;
    auto ids = edits::insertMolecules(d, {fromSmiles("C[C@@H](C(=O)O)N")}, {0, 0});
    QString before = chem::smiles(*d.molecule(ids[0]));
    QSet<QString> set{ids[0]};
    edits::transformObjects(d, set, -1, 0, 0, 1);  // flip horizontal
    CHECK(chem::smiles(*d.molecule(ids[0])) == before);  // same enantiomer: wedges swapped
    edits::transformObjects(d, set, 0, -1, 1, 0);  // rotate 90°
    CHECK(chem::smiles(*d.molecule(ids[0])) == before);

    // OMe on benzene: expand to O-CH3 and collapse back.
    Document e;
    edits::placeRing(e, {0, 0}, 6, true);
    QString tip = edits::growBond(e, e.molecules[0].atoms[0].id, 1);
    edits::setLabel(e, tip, "OMe");
    QString formula = chem::info(e.molecules[0]).formula;
    REQUIRE(chem::expandLabel(e, tip));
    const Molecule& m = e.molecules[0];
    CHECK(m.atoms.size() == 8);
    CHECK(m.groups.size() == 1);
    CHECK(m.atom(tip)->label.isEmpty());
    CHECK(m.atom(tip)->el == "O");
    CHECK(chem::info(m).formula == formula);
    // The new methyl carbon sits one bond away from the oxygen.
    for (const auto& id : m.groups[0].atoms)
        if (id != tip) CHECK(std::abs(geom::len(m.atom(id)->pos() - m.atom(tip)->pos()) - 1.0) < 0.05);
    // Survives a save.
    auto back = io::fromJson(io::toJson(e));
    REQUIRE(back);
    CHECK(back->molecules[0].groups.size() == 1);
    edits::contractGroup(e, m.id, tip);
    CHECK(e.molecules[0].atoms.size() == 7);
    CHECK(e.molecules[0].atom(tip)->label == "OMe");
    CHECK(e.molecules[0].groups.empty());
    CHECK(chem::info(e.molecules[0]).formula == formula);
}

TEST_CASE("1 on a wedge gives a plain single bond; bent arrows stay out of Arrange") {
    Document d;
    QString a = edits::addAtom(d, {0, 0});
    edits::growBond(d, a, 1);
    QString b = d.molecules[0].bonds[0].id;
    edits::setStereo(d, b, BondStereo::Wedge);
    edits::setBondOrder(d, b, 1);
    CHECK(d.molecules[0].bond(b)->stereo == BondStereo::None);

    QString bent = edits::addArrow(d, {5, 0}, 3);
    d.arrow(bent)->via = {QPointF(5, 3)};
    edits::buildScheme(d);
    CHECK(d.schemes.empty());
    CHECK(d.arrow(bent)->via.size() == 1);
}

TEST_CASE("CIP labels match known configurations") {
    auto cipOf = [](const char* smi) {
        Molecule m = fromSmiles(smi);
        QStringList codes;
        auto an = chem::analyze(m);
        for (auto& a : m.atoms) if (an.cip.contains(a.id)) codes << an.cip[a.id];
        for (auto& b : m.bonds) if (an.cip.contains(b.id)) codes << an.cip[b.id];
        codes.sort();
        return codes;
    };
    CHECK(cipOf("N[C@@H](C)C(=O)O") == QStringList{"S"});           // L-alanine
    CHECK(cipOf("N[C@@H](CS)C(=O)O") == QStringList{"R"});          // L-cysteine is R
    CHECK(cipOf("C[C@@H](O)[C@H](N)C(=O)O") == QStringList{"R", "S"});  // L-threonine (2S,3R)
    CHECK(cipOf("Oc1ccc(/C=C/c2cc(O)cc(O)c2)cc1") == QStringList{"E"});
    CHECK(cipOf("Oc1ccc(/C=C\\c2cc(O)cc(O)c2)cc1") == QStringList{"Z"});
}

TEST_CASE("every template parses, and amino acids are L") {
    for (const auto& group : templates::all())
        for (const auto& t : group.items) {
            INFO(t.name.toStdString());
            auto m = chem::fromSmiles(t.smiles);
            REQUIRE(m);
            CHECK(!chem::info(*m).formula.isEmpty());
            if (group.name.startsWith("Amino") && !t.name.startsWith("Glycine")) {
                // L-amino acids are S at the alpha carbon, except cysteine (R).
                Molecule mm = *m;
                auto an = chem::analyze(mm);
                QStringList codes;
                for (auto& a : mm.atoms) if (an.cip.contains(a.id)) codes << an.cip[a.id];
                CHECK(codes.contains(t.name.startsWith("Cysteine") ? "R" : "S"));
            }
        }
}

TEST_CASE("arrow head and line styles round-trip") {
    Document d;
    QString id = edits::addArrow(d, {0, 0}, 3);
    Arrow* a = d.arrow(id);
    a->headStyle = "half";
    a->bothHeads = true;
    a->line = "dotted";
    a->widthScale = 1.8;
    d.molecules.push_back(fromSmiles("CC"));
    d.molecules.back().id = "mx";
    d.molecules.back().showStereo = true;
    d.molecules.back().showCarbons = true;
    auto back = io::fromJson(io::toJson(d));
    REQUIRE(back);
    CHECK(back->arrows[0].headStyle == "half");
    CHECK(back->arrows[0].bothHeads);
    CHECK(back->arrows[0].line == "dotted");
    CHECK(back->arrows[0].widthScale == 1.8);
    CHECK(back->molecule("mx")->showStereo);
    CHECK(back->molecule("mx")->showCarbons);
}

TEST_CASE("arrange scheme keeps arrow directions and converging branches") {
    testApp();
    // Its own copy of the report figure (the sample file is a working file that gets edited):
    // horizontal PAL/C4H/4CL, TAL up into p-coumaric acid, STS down to resveratrol.
    // (Written with the pre-rename format id, so this also checks old files still open.)
    const char* figure = R"({"format":"chemdoc","version":1,"style":"acs","objects":[
      {"id":"m1","type":"molecule","number":"L-phenylalanine","numberManual":true,"smiles":"N[C@@H](Cc1ccccc1)C(=O)O","rotate":-30,"x":-1,"y":0.25},
      {"id":"m2","type":"molecule","number":"cinnamic acid","numberManual":true,"smiles":"OC(=O)/C=C/c1ccccc1","flip":true,"x":13.5,"y":0.3},
      {"id":"m3","type":"molecule","number":"p-coumaric acid","numberManual":true,"smiles":"OC(=O)/C=C/c1ccc(O)cc1","flip":true,"x":28,"y":0},
      {"id":"m4","type":"molecule","number":"p-coumaroyl-CoA","numberManual":true,"smiles":"*SC(=O)/C=C/c1ccc(O)cc1 |$CoA;;;;;;;;;;;;$|","flip":true,"x":43.5,"y":0},
      {"id":"m5","type":"molecule","number":"L-tyrosine","numberManual":true,"smiles":"N[C@@H](Cc1ccc(O)cc1)C(=O)O","rotate":-30,"x":28,"y":9},
      {"id":"m6","type":"molecule","number":"resveratrol","numberManual":true,"smiles":"Oc1ccc(/C=C/c2cc(O)cc(O)c2)cc1","flip":true,"x":43.5,"y":12.5},
      {"id":"r1","type":"arrow","x1":4.5,"y1":0,"x2":8.5,"y2":0,"above":"**PAL**","below":"− NH3"},
      {"id":"r2","type":"arrow","x1":18.5,"y1":0,"x2":22.5,"y2":0,"above":"**C4H**","below":"O2, NADPH"},
      {"id":"r3","type":"arrow","x1":33,"y1":0,"x2":37,"y2":0,"above":"**4CL**","below":"CoA, ATP"},
      {"id":"r4","type":"arrow","x1":28,"y1":7.2,"x2":28,"y2":3.4,"above":"**TAL**","below":"− NH3"},
      {"id":"r5","type":"arrow","x1":43.5,"y1":3,"x2":43.5,"y2":8,"above":"**STS**","below":"+ 3 malonyl-CoA"}]})";
    auto loaded = io::fromJson(QByteArray(figure));
    REQUIRE(loaded);
    Document d = *loaded;
    auto dirOf = [&](const QString& id) {
        const Arrow* a = d.arrow(id);
        return geom::unit(a->head() - a->tail());
    };
    std::map<QString, QPointF> before;
    for (auto& a : d.arrows) before[a.id] = dirOf(a.id);

    edits::buildScheme(d);
    REQUIRE(d.schemes.size() == 1);
    CHECK(d.schemes[0].steps.size() == 5);
    for (auto& st : d.schemes[0].steps) {
        INFO(st.arrow.toStdString());
        CHECK(st.reactants.size() == 1);
        CHECK(st.products.size() == 1);
    }
    // TAL (r4): L-tyrosine (m5) -> p-coumaric acid (m3), drawn vertically.
    for (auto& st : d.schemes[0].steps)
        if (st.arrow == "r4") { CHECK(st.reactants == QStringList{"m5"}); CHECK(st.products == QStringList{"m3"}); }

    Style style = styles::resolveNamed(d.styleName);
    Renderer r(style);
    ChemOverlay ov;
    for (auto& m : d.molecules) {
        auto an = chem::analyze(m);
        for (auto it = an.implicitH.begin(); it != an.implicitH.end(); ++it) ov.implicitH[it.key()] = it.value();
    }
    layout::layoutSchemes(d, r, ov);

    for (auto& a : d.arrows) {
        INFO(a.id.toStdString());
        QPointF now = dirOf(a.id);
        CHECK(geom::dot(now, before[a.id]) > 0.99);  // same direction as drawn
    }
    // Products ahead of their arrows, reactants behind, along the arrow's own direction.
    for (auto& st : d.schemes[0].steps) {
        const Arrow* a = d.arrow(st.arrow);
        QPointF dir = dirOf(st.arrow);
        INFO(st.arrow.toStdString());
        CHECK(geom::dot(d.molecule(st.products[0])->center() - a->head(), dir) > 0);
        CHECK(geom::dot(d.molecule(st.reactants[0])->center() - a->tail(), dir) < 0);
    }
    // Nothing overlaps.
    for (size_t i = 0; i < d.molecules.size(); ++i)
        for (size_t j = i + 1; j < d.molecules.size(); ++j) {
            QRectF a = r.moleculeRect(d.molecules[i], ov) | r.numberRect(d.molecules[i], ov);
            QRectF b = r.moleculeRect(d.molecules[j], ov) | r.numberRect(d.molecules[j], ov);
            INFO(d.molecules[i].id.toStdString() + " / " + d.molecules[j].id.toStdString());
            CHECK_FALSE(a.intersects(b));
        }
    QString dir = qEnvironmentVariable("CHEMFOX_SNAPSHOT");
    if (!dir.isEmpty()) io::renderImage(d, r, ov, 110).save(dir + "/arranged_report.png");
}

TEST_CASE("text distances from arrows and structures are style settings") {
    testApp();
    Style s = styles::resolve("style \"t\" based on default\n  arrows\n    text gap: 10 pt\n  numbers\n    gap: 20 pt\n");
    CHECK(s.arrowTextGap == 10);
    CHECK(s.numberGap == 20);
    // Built-ins keep the spacing they always had.
    CHECK(styles::resolveNamed("default").arrowTextGap == Catch::Approx(3.6));
    CHECK(styles::resolveNamed("acs").arrowTextGap == Catch::Approx(3.2));
    CHECK(styles::resolveNamed("slides").numberGap == Catch::Approx(11));

    Document d;
    QString id = edits::addArrow(d, {0, 0}, 4);
    d.arrow(id)->above = "H2O";
    d.arrow(id)->below = "rt";
    d.molecules.push_back(fromSmiles("CCO"));
    d.molecules.back().number = "1";
    ChemOverlay ov;
    Renderer near(styles::resolveNamed("default")), far(s);
    // Text sits further from the arrow, the name further from the structure.
    CHECK(far.arrowGeom(*d.arrow(id), &d).above[0].origin.y() < near.arrowGeom(*d.arrow(id), &d).above[0].origin.y() - 5);
    CHECK(far.arrowGeom(*d.arrow(id), &d).below[0].origin.y() > near.arrowGeom(*d.arrow(id), &d).below[0].origin.y() + 5);
    CHECK(far.numberRect(d.molecules[0], ov).top() > near.numberRect(d.molecules[0], ov).top() + 10);
}

TEST_CASE("rings on atoms: through a chain end, one bond away from a chain middle") {
    // Propane's middle carbon: benzene hangs off a new bond (isopropylbenzene), not through the atom.
    Document d;
    QString a = edits::addAtom(d, {0, 0});
    QString mid = edits::bondToPoint(d, a, {0.866, -0.5});
    edits::bondToPoint(d, mid, {1.732, 0});
    edits::attachRing(d, mid, 6, true);
    CHECK(chem::info(d.molecules[0]).smiles == "CC(C)c1ccccc1");
    // No atom of the new ring sits on top of an old one.
    for (const auto& p : d.molecules[0].atoms)
        for (const auto& q : d.molecules[0].atoms)
            if (p.id != q.id) CHECK(geom::len(p.pos() - q.pos()) > 0.5);
    // A chain end becomes a ring atom: ethane's end gives toluene.
    Document e;
    QString x = edits::addAtom(e, {0, 0});
    QString y = edits::bondToPoint(e, x, {1, 0});
    edits::attachRing(e, y, 6, true);
    CHECK(chem::info(e.molecules[0]).smiles == "Cc1ccccc1");
    // A saturated ring on a two-bond carbon is spiro.
    Document s;
    QString s0 = edits::addAtom(s, {0, 0});
    QString s1 = edits::bondToPoint(s, s0, {0.866, -0.5});
    edits::bondToPoint(s, s1, {1.732, 0});
    edits::attachRing(s, s1, 6, false);
    CHECK(s.molecules[0].atoms.size() == 8);
}
TEST_CASE("groups: acetyl on chain ends and middles, custom groups, carried in files") {
    // Ac on ethane's end turns that carbon into the acetyl: acetone's skeleton CC(C)=O.
    Document d;
    QString a = edits::addAtom(d, {0, 0});
    QString b = edits::bondToPoint(d, a, {1, 0});
    edits::attachGroup(d, b, "Ac");
    CHECK(chem::info(d.molecules[0]).smiles == "CC(C)=O");
    // On propane's middle carbon it hangs off a new bond: 3-methylbutan-2-one.
    Document e;
    QString e0 = edits::addAtom(e, {0, 0});
    QString e1 = edits::bondToPoint(e, e0, {0.866, -0.5});
    edits::bondToPoint(e, e1, {1.732, 0});
    QString lab = edits::attachGroup(e, e1, "Ac");
    CHECK(lab != e1);
    CHECK(e.molecules[0].atom(lab)->label == "Ac");
    CHECK(chem::info(e.molecules[0]).smiles == "CC(=O)C(C)C");

    // A custom group (trityl) works in formulas and expands like a built-in.
    CHECK(labels::checkGroup("Tr", "C(c1ccccc1)c1ccccc1").contains("*"));
    CHECK(labels::checkGroup("C", "*C").contains("element"));
    CHECK(labels::checkGroup("Tr", "*C(c1ccccc1)(c1ccccc1)c1ccccc1").isEmpty());
    labels::setCustomGroups({{"Tr", "*C(c1ccccc1)(c1ccccc1)c1ccccc1"}});
    Document t;
    QString o2 = edits::addAtom(t, {5, 0}, "O");
    QString tr = edits::bondToPoint(t, o2, {6, 0});
    edits::attachGroup(t, tr, "Tr");
    const Molecule* trMol = t.moleculeOfAtom(tr);
    CHECK(chem::info(*trMol).formula == "C19H16O");  // triphenylmethanol
    CHECK(labels::tokens("OTr") == QStringList{"O", "Tr"});
    // Saved files carry the definition; on a computer without it, opening the file brings it back.
    QByteArray json = io::toJson(t, false);
    CHECK(json.contains("\"groups\""));
    labels::setCustomGroups({});
    CHECK(labels::fragmentSmiles("Tr").isEmpty());
    auto back = io::fromJson(json);
    REQUIRE(back);
    CHECK(labels::fragmentSmiles("Tr") == "*C(c1ccccc1)(c1ccccc1)c1ccccc1");
    CHECK(chem::info(*back->moleculeOfAtom(tr)).formula == "C19H16O");
}