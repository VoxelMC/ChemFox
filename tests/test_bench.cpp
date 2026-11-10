// Timing of the hot paths on a deliberately large structure. Hidden: run with
//   chemfox_tests "[.bench]"

#include "app.h"
#include "core/chem/Chem.h"
#include "core/commands/Edits.h"
#include "core/model/Mapping.h"
#include "editor/Canvas.h"
#include "io/Export.h"
#include "warnings/Warnings.h"

#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPainter>
#include <catch2/catch_test_macros.hpp>

using namespace cd;

namespace {

// Linear L-alanine peptide: ~5 heavy atoms and one stereocentre per residue.
QString peptide(int n) {
    QString s = "N";
    for (int i = 0; i < n; ++i) s += "[C@@H](C)C(=O)" + QString(i + 1 < n ? "N" : "O");
    return s;
}

double ms(QElapsedTimer& t) { return t.nsecsElapsed() / 1e6; }

}  // namespace

TEST_CASE("bench: hot paths on a 60-residue peptide", "[.bench]") {
    testApp();
    QElapsedTimer t;

    t.start();
    auto mol = chem::fromSmiles(peptide(60));
    REQUIRE(mol);
    WARN("import + 2D layout (" << mol->atoms.size() << " atoms): " << ms(t) << " ms");

    t.restart();
    auto an = chem::analyze(*mol);
    WARN("analyze (valence, stereo, CIP): " << ms(t) << " ms, " << an.cip.size() << " CIP labels");

    t.restart();
    auto info = chem::info(*mol);
    WARN("info (formula, mass, SMILES, InChI): " << ms(t) << " ms");

    Canvas c;
    c.resize(1200, 800);
    c.show();
    t.restart();
    c.insertMolecules({*mol}, QPointF(0, 0));
    WARN("insert as an edit (recompute + warnings): " << ms(t) << " ms");
    c.zoomToFit();

    // Paint: render the canvas several times.
    QImage img(c.size(), QImage::Format_ARGB32_Premultiplied);
    t.restart();
    for (int i = 0; i < 10; ++i) c.render(&img);
    WARN("paint: " << ms(t) / 10 << " ms per frame");

    // Hover: mouse moves run hit-testing.
    t.restart();
    for (int i = 0; i < 50; ++i) {
        QPointF p(100 + i * 20, 400);
        QMouseEvent e(QEvent::MouseMove, p, c.mapToGlobal(p), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&c, &e);
    }
    WARN("hover (hit test): " << ms(t) / 50 << " ms per mouse move");

    // A typical small edit on the big molecule.
    QString atom = c.doc().molecules[0].atoms[3].id;
    t.restart();
    c.edit("charge", [&](Document& d) { edits::changeCharge(d, atom, 1); });
    WARN("small edit on the big molecule: " << ms(t) << " ms");

    // Auto-mapping a peptide coupling: Ala(n) + Ala -> Ala(n+1).
    for (int n : {5, 15}) {
        auto r1 = *chem::fromSmiles(peptide(n));
        auto r2 = *chem::fromSmiles("N[C@@H](C)C(=O)O");
        auto p1 = *chem::fromSmiles(peptide(n + 1));
        t.restart();
        chem::autoMap({&r1, &r2}, {&p1});
        WARN("autoMap, " << n << "-mer coupling: " << ms(t) << " ms");
    }
}

TEST_CASE("bench: auto-mapping worst cases", "[.bench]") {
    QElapsedTimer t;
    struct Case { const char* name; const char* r1; const char* r2; const char* p; };
    const Case cases[] = {
        {"steroid acetylation", "C[C@]12CC[C@H]3[C@@H](CCC4=CC(=O)CC[C@]34C)[C@@H]1CC[C@@H]2O", "CC(=O)OC(C)=O",
         "CC(=O)O[C@H]1CC[C@H]2[C@@H]3CCC4=CC(=O)CC[C@]4(C)[C@H]3CC[C@]12C"},
        {"coronene hydrogenation (symmetric)", "c1cc2ccc3ccc4ccc5ccc6ccc1c1c2c3c4c5c61", "[H][H]",
         "C1Cc2ccc3ccc4ccc5ccc6ccc1c1c2c3c4c5c61"},
        {"Diels-Alder, anthracene + maleimide", "c1ccc2cc3ccccc3cc2c1", "O=C1C=CC(=O)N1",
         "O=C1NC(=O)C2C1C1c3ccccc3C2c2ccccc21"},
    };
    for (const auto& k : cases) {
        auto r1 = *chem::fromSmiles(k.r1), r2 = *chem::fromSmiles(k.r2), p = *chem::fromSmiles(k.p);
        t.start();
        int n = chem::autoMap({&r1, &r2}, {&p});
        double el = t.nsecsElapsed() / 1e6;
        int formed = 0, broken = 0, changed = 0;
        for (auto& c : mapping::changeList({&r1, &r2}, {&p})) {
            formed += c.change == mapping::Change::Formed;
            broken += c.change == mapping::Change::Broken;
            changed += c.change == mapping::Change::Changed;
        }
        WARN("autoMap, " << k.name << ": " << el << " ms, " << n << " mapped; formed " << formed << ", broken " << broken << ", order changes " << changed / 2);
    }
}
