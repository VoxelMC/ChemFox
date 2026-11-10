// Reaction text, atom mapping, molecular diff and the reaction builder path.

#include "app.h"
#include "app/ReactionBuilder.h"
#include "app/StyleEditor.h"
#include "core/chem/Chem.h"
#include "core/model/Mapping.h"
#include "style/Style.h"
#include "render/Layout.h"
#include "core/model/Geometry.h"
#include "core/commands/Edits.h"
#include "editor/Canvas.h"
#include "io/Native.h"
#include <QDir>
#include "warnings/Warnings.h"

#include <QDoubleSpinBox>
#include <QLabel>
#include <QTabWidget>
#include <QToolButton>
#include <QLineEdit>
#include <catch2/catch_test_macros.hpp>

using namespace cd;

namespace {

int count(const std::vector<mapping::BondChange>& ch, mapping::Change c) {
    int n = 0;
    for (auto& v : ch) n += v.change == c;
    return n;
}

}  // namespace

TEST_CASE("reaction text splits and joins") {
    auto r = chem::splitReaction("CC(=O)O.OCC>[H+]>CCOC(C)=O.O");
    CHECK(r.reactants == QStringList{"CC(=O)O", "OCC"});
    CHECK(r.agents == QStringList{"[H+]"});
    CHECK(r.products == QStringList{"CCOC(C)=O", "O"});
    CHECK(chem::joinReaction(r) == "CC(=O)O.OCC>[H+]>CCOC(C)=O.O");
    CHECK(chem::splitReaction("A>>B").agents.isEmpty());
}

TEST_CASE("map numbers survive SMILES and SMARTS parsing") {
    auto m = chem::fromSmilesOrSmarts("[CH3:1][C:2](=[O:4])[OH:3]");
    REQUIRE(m);
    QSet<int> maps;
    for (auto& a : m->atoms) maps.insert(a.map);
    CHECK(maps == QSet<int>{1, 2, 3, 4});
    CHECK(chem::smilesWithMaps(*m).contains(":3]"));
    CHECK_FALSE(chem::smiles(*m).contains(':'));  // plain SMILES never carries maps

    // A SMARTS pattern (not valid SMILES) still becomes a drawable structure.
    auto q = chem::fromSmilesOrSmarts("[#6:1](=[#8:2])-[#7;H1:3]");
    REQUIRE(q);
    CHECK(q->atoms.size() == 3);
    CHECK(q->bonds.size() == 2);
}

TEST_CASE("typed maps give the diff: SN2") {
    auto r1 = *chem::fromSmilesOrSmarts("[CH3:1][Br:2]");
    auto r2 = *chem::fromSmilesOrSmarts("[OH-:3]");
    auto p1 = *chem::fromSmilesOrSmarts("[CH3:1][OH:3]");
    auto p2 = *chem::fromSmilesOrSmarts("[Br-:2]");
    auto ch = mapping::changeList({&r1, &r2}, {&p1, &p2});
    CHECK(count(ch, mapping::Change::Broken) == 1);  // C-Br
    CHECK(count(ch, mapping::Change::Formed) == 1);  // C-O
    CHECK(count(ch, mapping::Change::Changed) == 0);
}

TEST_CASE("auto-mapping: esterification and Diels-Alder") {
    auto acid = *chem::fromSmiles("CC(=O)O");
    auto alcohol = *chem::fromSmiles("OCC");
    auto ester = *chem::fromSmiles("CCOC(C)=O");
    int mapped = chem::autoMap({&acid, &alcohol}, {&ester});
    CHECK(mapped == 6);
    for (auto& a : ester.atoms) CHECK(a.map > 0);
    auto ch = mapping::changeList({&acid, &alcohol}, {&ester});
    CHECK(count(ch, mapping::Change::Broken) == 1);
    CHECK(count(ch, mapping::Change::Formed) == 1);

    // Butadiene + ethylene -> cyclohexene: every ring atom mapped, two ring bonds formed.
    // Fewest-changes mapping is not mechanism-aware: it may keep a diene double bond in place
    // rather than shifting it as the [4+2] mechanism does, so order changes are not pinned here.
    auto diene = *chem::fromSmiles("C=CC=C");
    auto dienophile = *chem::fromSmiles("C=C");
    auto ring = *chem::fromSmiles("C1=CCCCC1");
    CHECK(chem::autoMap({&diene, &dienophile}, {&ring}) == 6);
    auto da = mapping::changeList({&diene, &dienophile}, {&ring});
    CHECK(count(da, mapping::Change::Formed) == 2);
    CHECK(count(da, mapping::Change::Broken) == 0);

    // Typed maps give the textbook picture: all four pi bonds change order.
    auto d2 = *chem::fromSmilesOrSmarts("[CH2:1]=[CH:2][CH:3]=[CH2:4]");
    auto e2 = *chem::fromSmilesOrSmarts("[CH2:5]=[CH2:6]");
    auto r2 = *chem::fromSmilesOrSmarts("[CH2:1]1[CH:2]=[CH:3][CH2:4][CH2:5][CH2:6]1");
    auto textbook = mapping::changeList({&d2, &e2}, {&r2});
    CHECK(count(textbook, mapping::Change::Formed) == 2);
    CHECK(count(textbook, mapping::Change::Changed) == 8);  // 4 bonds, counted on both sides
}

TEST_CASE("builder path: apply, edit, undo, save with maps") {
    testApp();
    Canvas c;
    c.resize(900, 700);
    ReactionSpec spec;
    spec.reactants = {"CC(=O)O", "OCC"};
    spec.products = {"CCOC(C)=O"};
    spec.above = "H2SO4";
    ReactionResult r = c.applyReaction(spec, {});
    REQUIRE_FALSE(r.arrowId.isEmpty());
    CHECK(r.mapped == 6);
    REQUIRE(c.doc().schemes.size() == 1);
    const Step& st = c.doc().schemes[0].steps[0];
    CHECK(st.reactants.size() == 2);
    CHECK(st.products.size() == 1);
    CHECK(st.showDiff);
    CHECK(c.doc().molecules.size() == 3);

    // Editing replaces the step's structures instead of adding more; a bad row is reported.
    spec.products = {"CCOC(C)=O", "O", "not a molecule(("};
    r = c.applyReaction(spec, r.arrowId);
    CHECK(c.doc().molecules.size() == 4);
    CHECK(c.doc().arrows.size() == 1);
    REQUIRE(r.errors.size() == 5);
    CHECK(r.errors[4].size() > 0);
    CHECK(r.errors[2].isEmpty());

    // Load the step back as text.
    auto back = c.reactionFor(r.arrowId, false);
    REQUIRE(back);
    CHECK(back->products.size() == 2);
    CHECK(back->above == "H2SO4");

    // Map numbers are saved in the file.
    auto doc = io::fromJson(io::toJson(c.doc()));
    REQUIRE(doc);
    int maps = 0;
    for (auto& m : doc->molecules)
        for (auto& a : m.atoms) maps += a.map > 0;
    CHECK(maps >= 12);
    CHECK(doc->schemes[0].steps[0].showDiff);

    // Unmapped product atoms raise W203; undo removes the reaction.
    ReactionSpec manual;
    manual.reactants = {"[CH3:1][Br:2]"};
    manual.products = {"[CH3:1]O"};
    manual.autoMap = false;
    c.applyReaction(manual, {});
    bool w203 = false;
    for (auto& w : c.warnings()) w203 |= w.code == "W203";
    CHECK(w203);
    c.undoStack()->undo();
    CHECK(c.doc().arrows.size() == 1);
}

TEST_CASE("builder panel: reaction SMILES line drives the canvas") {
    testApp();
    Canvas c;
    c.resize(1000, 520);
    c.show();
    ReactionBuilder b(&c);
    b.resize(380, 900);
    b.show();

    // The first line edit is the reaction SMILES line.
    auto edits = b.findChildren<QLineEdit*>();
    REQUIRE(!edits.empty());
    QLineEdit* line = edits.front();
    line->setText("CC(=O)O.OCC>>CCOC(C)=O.O");
    emit line->returnPressed();
    QApplication::processEvents();

    CHECK(c.doc().arrows.size() == 1);
    CHECK(c.doc().molecules.size() == 4);
    REQUIRE(c.doc().schemes.size() == 1);
    CHECK(c.doc().schemes[0].steps[0].products.size() == 2);
    // Rows were created: line + 2 reactants + 0 reagents + 2 products + arrow above/below.
    CHECK(b.findChildren<QLineEdit*>().size() >= 7);

    // Optional snapshots for eyeballing the UI: set CHEMFOX_SNAPSHOT to a folder.
    QString dir = qEnvironmentVariable("CHEMFOX_SNAPSHOT");
    if (!dir.isEmpty()) {
        c.setShowMaps(true);
        c.zoomToFit();
        QApplication::processEvents();
        b.grab().save(dir + "/builder_panel.png");
        c.grab().save(dir + "/builder_canvas.png");
    }
}

TEST_CASE("mapping mode: drag to map, drag away to unmap, undo") {
    testApp();
    Canvas c;
    c.resize(1100, 600);
    c.show();
    ReactionSpec spec;
    spec.reactants = {"CC(=O)O", "OCC"};
    spec.products = {"CCOC(C)=O", "O"};
    c.applyReaction(spec, {});
    c.zoomToFit();

    auto mouse = [&](QEvent::Type t, QPointF p, Qt::MouseButtons b) {
        QMouseEvent e(t, p, c.mapToGlobal(p), t == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, b,
                      Qt::NoModifier);
        QApplication::sendEvent(&c, &e);
    };
    auto drag = [&](QPointF a, QPointF b) {
        mouse(QEvent::MouseMove, a, Qt::NoButton);
        mouse(QEvent::MouseButtonPress, a, Qt::LeftButton);
        for (int i = 1; i <= 6; ++i) mouse(QEvent::MouseMove, a + (b - a) * (i / 6.0), Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, b, Qt::NoButton);
    };
    auto step = [&]() -> const Step& { return c.doc().schemes[0].steps[0]; };
    auto atomWhere = [&](const QString& molId, auto pred) -> const Atom* {
        const Molecule* m = c.doc().molecule(molId);
        for (auto& a : m->atoms) if (pred(*m, a)) return &a;
        return nullptr;
    };
    auto acidOH = [&] {
        return atomWhere(step().reactants[0], [](const Molecule& m, const Atom& a) {
            return a.el == "O" && m.bondsOf(a.id).size() == 1 && m.bondsOf(a.id)[0]->order == 1;
        });
    };
    auto esterO = [&] {
        return atomWhere(step().products[0], [](const Molecule& m, const Atom& a) {
            return a.el == "O" && m.neighbors(a.id).size() == 2;
        });
    };
    REQUIRE(acidOH());
    REQUIRE(esterO());
    CHECK_FALSE(step().manualMap);
    int before = esterO()->map;
    CHECK(acidOH()->map != esterO()->map);  // auto-map keeps the alcohol O in the ester

    // Outside mapping mode, dragging from an atom draws a bond instead.
    c.setMapMode(true);
    drag(c.modelToScreen(acidOH()->pos()), c.modelToScreen(esterO()->pos()));
    CHECK(acidOH()->map == esterO()->map);
    CHECK(acidOH()->map > 0);
    CHECK(step().manualMap);
    // The ester O's old partner (the ethanol O) lost its number.
    const Atom* ethanolO = atomWhere(step().reactants[1], [&](const Molecule&, const Atom& a) { return a.el == "O"; });
    CHECK(ethanolO->map != before);
    CHECK(c.doc().molecules.size() == 4);  // no bond was drawn

    // A carbon can't be mapped onto an oxygen.
    const Atom* esterC = atomWhere(step().products[0], [](const Molecule&, const Atom& a) { return a.el == "C"; });
    int cMap = esterC->map;
    drag(c.modelToScreen(acidOH()->pos()), c.modelToScreen(esterC->pos()));
    CHECK(esterC->map == cMap);

    // Dropping on empty canvas unmaps the atom and its partner.
    drag(c.modelToScreen(esterO()->pos()), c.modelToScreen(esterO()->pos()) + QPointF(0, 180));
    CHECK(esterO()->map == 0);
    CHECK(acidOH()->map == 0);

    c.undoStack()->undo();
    CHECK(esterO()->map > 0);
    CHECK(esterO()->map == acidOH()->map);

    // Escape leaves mapping mode.
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&c, &esc);
    CHECK_FALSE(c.mapMode());

    QString dir = qEnvironmentVariable("CHEMFOX_SNAPSHOT");
    if (!dir.isEmpty()) {
        c.setMapMode(true);
        mouse(QEvent::MouseMove, c.modelToScreen(esterO()->pos()), Qt::NoButton);
        QApplication::processEvents();
        c.grab().save(dir + "/mapmode.png");
    }
}

TEST_CASE("style source: canonical keys, standard layout, and the form editor") {
    // Aliases collapse to one canonical key; messy indentation comes back in the standard layout.
    auto src = styles::parseSource("style mine based on acs\n\tline width: 0.8 pt\n      numbers\n\t  font: Helvetica\n  font size: 11\n");
    CHECK(src.name == "mine");
    CHECK(src.base == "acs");
    CHECK(src.value("", "bond width") == "0.8 pt");
    CHECK(src.value("", "text size") == "11");
    CHECK(src.value("numbers", "font") == "Helvetica");
    QString out = styles::writeSource(src);
    CHECK(out == "style \"mine\" based on acs\n  bond width: 0.8 pt\n  text size: 11\n\n  numbers\n    font: Helvetica\n");
    CHECK(styles::resolve(out).numberFont == "Helvetica");

    // The form: changing a field embeds a custom style based on the current one; reverting returns to it.
    testApp();
    Canvas c;
    StyleEditor ed(&c);
    ed.show();
    QDoubleSpinBox* bondLength = nullptr;
    for (auto* s : ed.findChildren<QDoubleSpinBox*>()) if (s->toolTip().startsWith("Length of one bond")) bondLength = s;
    REQUIRE(bondLength);
    bondLength->setValue(20);
    CHECK(c.style().bondLength == 20);
    CHECK(c.doc().styleText.contains("bond length: 20 pt"));
    CHECK(c.doc().styleText.contains("based on default"));
    auto reverts = ed.findChildren<QToolButton*>();
    QToolButton* revert = nullptr;
    for (auto* b : reverts) if (b->text() == "↺" && b->isEnabled()) revert = b;
    REQUIRE(revert);
    revert->click();
    CHECK(c.doc().styleText.isEmpty());  // nothing left to override: back to the built-in
    CHECK(c.doc().styleName == "default");
}

TEST_CASE("snapshot: style editor") {
    QString dir = qEnvironmentVariable("CHEMFOX_SNAPSHOT");
    if (dir.isEmpty()) return;
    testApp();
    Canvas c;
    c.setStyleName("acs");
    StyleEditor ed(&c);
    ed.resize(380, 560);
    ed.show();
    for (auto* s : ed.findChildren<QDoubleSpinBox*>()) if (s->toolTip().startsWith("Width of bond")) s->setValue(0.8);
    QApplication::processEvents();
    ed.grab().save(dir + "/style_editor_bonds.png");
    auto* tabs = ed.findChild<QTabWidget*>();
    tabs->setCurrentIndex(4);  // Names
    QApplication::processEvents();
    ed.grab().save(dir + "/style_editor_names.png");
    tabs->setCurrentIndex(tabs->count() - 1);  // Source
    QApplication::processEvents();
    ed.grab().save(dir + "/style_editor_source.png");
}

TEST_CASE("align to reactant: shared atoms copy the reactant's geometry, stereo kept") {
    // N-acetylation of L-alanine; the product starts in some arbitrary orientation.
    auto ala = *chem::fromSmiles("N[C@@H](C)C(=O)O");
    auto amide = *chem::fromSmiles("CC(=O)N[C@@H](C)C(=O)O");
    for (auto& a : amide.atoms) { QPointF p = a.pos(); a.setPos(QPointF(p.y(), -p.x()) + QPointF(20, 5)); }
    QString before = chem::smiles(amide);
    QPointF c0 = amide.center();
    auto ac2o = *chem::fromSmiles("CC(=O)OC(C)=O");
    chem::autoMap({&ala, &ac2o}, {&amide});
    REQUIRE(chem::alignTo(amide, ala));
    CHECK(chem::smiles(amide) == before);                  // same stereoisomer
    CHECK(geom::len(amide.center() - c0) < 1e-6);          // stays where it was on the page
    // Every pair of shared atoms is laid out exactly as in alanine (up to a shift).
    std::vector<std::pair<QPointF, QPointF>> pairs;
    for (auto& a : amide.atoms)
        for (auto& b : ala.atoms)
            if (a.map && a.map == b.map) pairs.push_back({a.pos(), b.pos()});
    REQUIRE(pairs.size() >= 5);
    QPointF shift = pairs[0].first - pairs[0].second;
    for (auto& [p, r] : pairs) CHECK(geom::len(p - r - shift) < 0.02);
}

TEST_CASE("clean on one structure cycles orientations as one undo step") {
    testApp();
    Canvas c;
    c.resize(900, 600);
    c.show();
    c.insertMolecules({*chem::fromSmiles("OC(=O)/C=C/c1ccc(O)cc1")}, QPointF(0, 0));
    c.selectAll();
    auto snapshot = [&] {
        QStringList s;
        for (auto& a : c.doc().molecules[0].atoms) s << QString("%1,%2").arg(a.x, 0, 'f', 2).arg(a.y, 0, 'f', 2);
        return s.join(';');
    };
    const QString original = snapshot();
    QStringList seen;
    for (int i = 0; i < 4; ++i) {
        c.cleanStructure();
        seen << snapshot();
    }
    CHECK(seen[1] != seen[0]);
    CHECK(seen[2] != seen[1]);
    CHECK(seen[3] != seen[2]);
    c.undoStack()->undo();
    CHECK(snapshot() == original);  // the whole cycle was one step
}

TEST_CASE("arrange selection: only the selected part moves") {
    testApp();
    Document d;
    // A -> B -> C along a row, but the user has dragged C far away and A off to one side.
    auto ids = edits::insertMolecules(d, {*chem::fromSmiles("CCO"), *chem::fromSmiles("CC=O"), *chem::fromSmiles("CC(=O)O")},
                                      {0, 0});
    d.molecule(ids[0])->translate(QPointF(-9, 1.5));
    d.molecule(ids[1])->translate(QPointF(0, 0));
    QString a1 = edits::addArrow(d, d.molecule(ids[1])->center() - QPointF(5, 0), 3);
    QString a2 = edits::addArrow(d, d.molecule(ids[1])->center() + QPointF(2, 0), 3);
    d.molecule(ids[2])->translate(QPointF(d.arrow(a2)->x2 + 2.5 - d.molecule(ids[2])->center().x(), 0));
    edits::buildScheme(d);                          // the scheme as first arranged
    REQUIRE(d.schemes.size() == 1);
    bool cIsProduct = false;
    for (auto& st : d.schemes[0].steps) cIsProduct |= st.arrow == a2 && st.products.contains(ids[2]);
    REQUIRE(cIsProduct);
    d.molecule(ids[2])->translate(QPointF(30, 0));  // then C dragged far away by hand
    edits::buildScheme(d);                          // Arrange re-reads: known steps are kept
    QPointF aBefore = d.molecule(ids[0])->center(), bBefore = d.molecule(ids[1])->center();
    QPointF arrow1Before = d.arrow(a1)->tail();

    Renderer r(styles::resolveNamed("default"));
    ChemOverlay ov;
    QSet<QString> only{a2, ids[2]};
    layout::layoutSchemes(d, r, ov, &only);
    CHECK(geom::len(d.molecule(ids[0])->center() - aBefore) < 1e-9);  // A untouched (custom spot kept)
    CHECK(geom::len(d.molecule(ids[1])->center() - bBefore) < 1e-9);  // B is context: it anchors the step
    CHECK(geom::len(d.arrow(a1)->tail() - arrow1Before) < 1e-9);       // the other arrow untouched
    // C came back to sit after the arrow, on B's side, not 30 bonds away.
    CHECK(d.molecule(ids[2])->center().x() > d.arrow(a2)->x2);
    CHECK(d.molecule(ids[2])->center().x() < bBefore.x() + 12);
}

TEST_CASE("snapshot: align products on the report figure") {
    QString dir = qEnvironmentVariable("CHEMFOX_SNAPSHOT");
    if (dir.isEmpty()) return;
    testApp();
    auto d = io::loadFile(QStringLiteral(CHEMFOX_SOURCE_DIR) + "/samples/resveratrol_report.cmfx");
    REQUIRE(d);
    Canvas c;
    c.resize(1500, 760);
    c.show();
    c.setDocument(*d);
    QApplication::processEvents();
    c.grab().save(dir + "/align_before.png");
    c.alignAllProducts();
    c.zoomToFit();
    QApplication::processEvents();
    c.grab().save(dir + "/align_after.png");
}

TEST_CASE("align to reactant keeps the product's own E/Z and R/S") {
    // Photoisomerisation: an (E) reactant, a (Z) product. Copying the reactant's drawing would make it (E).
    auto e = *chem::fromSmiles("OC(=O)/C=C/c1ccccc1");
    auto z = *chem::fromSmiles("OC(=O)/C=C\\c1ccccc1");
    const QString zBefore = chem::smiles(z);
    CHECK(zBefore.contains('\\'));  // really (Z)
    REQUIRE(chem::alignTo(z, e));
    CHECK(chem::smiles(z) == zBefore);

    // SN2 inversion: an (R) reactant, an (S) product, unmapped (shared atoms come from the MCS).
    auto r = *chem::fromSmiles("C[C@@H](Br)CC");
    auto s = *chem::fromSmiles("C[C@@H](O)CC");
    const QString sBefore = chem::smiles(s);
    const auto cipBefore = chem::analyze(s).cip;
    REQUIRE(chem::alignTo(s, r));
    CHECK(chem::smiles(s) == sBefore);
    CHECK(chem::analyze(s).cip == cipBefore);

    // Both mapped: E reactant to E product keeps E.
    auto e2 = *chem::fromSmiles("CC(=O)/C=C/c1ccccc1");
    const QString e2Before = chem::smiles(e2);
    REQUIRE(chem::alignTo(e2, e));
    CHECK(chem::smiles(e2) == e2Before);
}

TEST_CASE("aligning products in the samples never changes an E/Z or R/S label") {
    // Whatever the sample files contain (they are working files), alignment must keep every stereo label.
    testApp();
    QDir dir(CHEMFOX_SOURCE_DIR "/samples");
    for (const auto& f : dir.entryList({"*.cmfx"})) {
        auto doc = io::loadFile(dir.filePath(f));
        REQUIRE(doc);
        Canvas c;
        c.setDocument(*doc);
        QHash<QString, QHash<QString, QString>> before;
        for (const auto& m : c.doc().molecules) before[m.id] = chem::analyze(m).cip;
        c.alignAllProducts();
        for (const auto& m : c.doc().molecules) {
            auto after = chem::analyze(m).cip;
            for (auto it = before[m.id].begin(); it != before[m.id].end(); ++it)
                if (after.value(it.key()) != it.value())
                    FAIL_CHECK(f.toStdString() << " " << m.id.toStdString() << " " << it.key().toStdString() << ": " << it.value().toStdString() << " -> " << after.value(it.key()).toStdString());
        }
    }
}
