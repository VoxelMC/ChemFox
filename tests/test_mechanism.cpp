// Mechanism (curved) arrows and lone pairs; snap guides, Alt+drag duplicate, align, distribute and rotate.

#include "app.h"
#include "core/chem/Chem.h"
#include "core/commands/Edits.h"
#include "core/model/Geometry.h"
#include "editor/Canvas.h"
#include "editor/Snap.h"
#include "io/Export.h"
#include "io/Native.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cd;
using geom::len;

namespace {

struct Driver {
    Canvas c;
    Driver() {
        c.resize(900, 700);
        c.show();
    }
    void mouse(QEvent::Type t, QPointF p, Qt::MouseButton b, Qt::MouseButtons bs, Qt::KeyboardModifiers m) {
        QMouseEvent e(t, p, c.mapToGlobal(p), b, bs, m);
        QApplication::sendEvent(&c, &e);
    }
    void move(QPointF p, Qt::KeyboardModifiers m = Qt::NoModifier) { mouse(QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton, m); }
    void drag(QPointF a, QPointF b, Qt::KeyboardModifiers m = Qt::NoModifier) {
        move(a, m);
        mouse(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton, m);
        for (int i = 1; i <= 8; ++i) mouse(QEvent::MouseMove, a + (b - a) * (i / 8.0), Qt::NoButton, Qt::LeftButton, m);
        mouse(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton, m);
    }
    void key(int k, const QString& text, Qt::KeyboardModifiers m = Qt::NoModifier) {
        QKeyEvent e(QEvent::KeyPress, k, m, text);
        QApplication::sendEvent(&c, &e);
    }
    const Molecule& mol(int i = 0) { return c.doc().molecules.at(i); }
    QPointF atomPos(const Atom& a) { return c.modelToScreen(a.pos()); }
    QPointF bondPos(const Molecule& m, const Bond& b) { return c.modelToScreen((m.atom(b.a)->pos() + m.atom(b.b)->pos()) / 2); }
    QRectF rect(const Molecule& m) { return c.renderer().moleculeRect(m, c.overlay()); }
};

QPointF r_toPt(Driver& d, QPointF model) { return d.c.renderer().toPt(model); }

const Atom* firstOf(const Molecule& m, const QString& el) {
    for (const auto& a : m.atoms) if (a.el == el) return &a;
    return nullptr;
}

}  // namespace

TEST_CASE("mechanism arrows attach to atoms, bonds and lone pairs and follow their structures") {
    testApp();
    Document d;
    auto mols = std::vector<Molecule>{};
    d.molecules.push_back(Molecule{});
    // Acetaldehyde-like fragment drawn by hand: C1-C2=O3
    Molecule& m = d.molecules.back();
    m.id = "m1";
    m.atoms = {{"a1", "C", 0, 0}, {"a2", "C", 1, 0}, {"a3", "O", 1.5, -0.87}};
    m.bonds = {{"b1", "a1", "a2", 1}, {"b2", "a2", "a3", 2}};
    d.fixNextId();

    // Lone pairs: as many as the atom really has (a carbonyl O: two), then one fewer, then none.
    const int cap = chem::analyze(*d.molecule("m1")).lonePairs.value("a3");
    CHECK(cap == 2);
    edits::cycleLonePairs(d, "a3", cap);
    CHECK(d.molecule("m1")->atom("a3")->lonePairs == 2);
    edits::cycleLonePairs(d, "a3", cap);
    CHECK(d.molecule("m1")->atom("a3")->lonePairs == 1);
    edits::cycleLonePairs(d, "a3", cap);
    CHECK(d.molecule("m1")->atom("a3")->lonePairs == 0);
    edits::cycleLonePairs(d, "a3", cap);
    CHECK(d.molecule("m1")->atom("a3")->lonePairs == 2);
    edits::setLonePairs(d, "a3", 0);

    // From a lone pair (shown on demand), from a bond, to an atom.
    CurvedEnd lp{"a3", {}, true};
    CurvedEnd bond{{}, "b1"};
    CurvedEnd atom{"a1"};
    const QString e1 = edits::addCurved(d, lp, bond);
    const QString e2 = edits::addCurved(d, CurvedEnd{{}, "b2"}, atom, true);
    REQUIRE(!e1.isEmpty());
    REQUIRE(!e2.isEmpty());
    CHECK(d.molecule("m1")->atom("a3")->lonePairs == 1);
    CHECK(edits::addCurved(d, atom, atom).isEmpty());  // nowhere to go

    // Drawn geometry: the start sits at a lone pair near O, the end at the bond's midpoint.
    Style st;
    Renderer r(st);
    ChemOverlay ov;
    CurvedGeom g = r.curvedGeom(*d.curvedArrow(e1), d, ov);
    REQUIRE(g.ok);
    CHECK(len(g.a - r.toPt({1.5, -0.87})) < st.bondLength * 0.7);
    CHECK(len(g.b - r.toPt({0.5, 0})) < 1e-6);
    CHECK(len(g.tip - g.b) > 0.5);  // stops short of the bond

    // Moving, flipping and cleaning the structure: attached ends go with it.
    edits::moveObjects(d, {"m1"}, QPointF(5, 3));
    g = r.curvedGeom(*d.curvedArrow(e1), d, ov);
    CHECK(len(g.b - r.toPt({5.5, 3})) < 1e-6);
    edits::transformObjects(d, {"m1"}, -1, 0, 0, 1);
    const Molecule* mm = d.molecule("m1");
    g = r.curvedGeom(*d.curvedArrow(e2), d, ov);
    const Bond* b2 = mm->bond("b2");
    CHECK(len(g.a - r.toPt((mm->atom(b2->a)->pos() + mm->atom(b2->b)->pos()) / 2)) < 1e-6);
    CHECK(len(g.b - r.toPt(mm->atom("a1")->pos())) < 1e-6);

    // Saved and reopened: same ends, head and hand-set curve. Files with arrows are format 2.
    d.curvedArrow(e2)->bulge = -0.4;
    d.curvedArrow(e2)->manual = true;
    QByteArray json = io::toJson(d, false);
    CHECK(QJsonDocument::fromJson(json).object().value("version").toInt() == 2);
    auto back = io::fromJson(json);
    REQUIRE(back);
    REQUIRE(back->curved.size() == 2);
    const CurvedArrow* c1 = back->curvedArrow(e1);
    const CurvedArrow* c2 = back->curvedArrow(e2);
    REQUIRE(c1);
    REQUIRE(c2);
    CHECK(c1->from.atom == "a3");
    CHECK(c1->from.lonePair);
    CHECK(c1->to.bond == "b1");
    CHECK(!c1->half);
    CHECK(c2->half);
    CHECK(c2->manual);
    CHECK(c2->bulge == Catch::Approx(-0.4));
    CHECK(back->molecule("m1")->atom("a3")->lonePairs == 1);

    // Without arrows or lone pairs a file stays at format 1, which older versions open.
    Document plain = d;
    plain.curved.clear();
    for (auto& a : plain.molecules[0].atoms) a.lonePairs = 0;
    CHECK(QJsonDocument::fromJson(io::toJson(plain, false)).object().value("version").toInt() == 1);

    // Copying the structure copies its arrows; deleting the atom an arrow uses deletes the arrow.
    QSet<QString> copies = edits::duplicate(d, {"m1"}, QPointF(0, 4));
    CHECK(copies.size() == 3);  // the molecule and both arrows
    CHECK(d.curved.size() == 4);
    // (a1 is a chain end, so its bond b1 goes too, and with it the arrow that pointed at b1.)
    edits::remove(d, "a1");
    CHECK(d.curved.size() == 2);
    CHECK(!d.curvedArrow(e1));
    CHECK(!d.curvedArrow(e2));

    // The subset used for copy and export keeps arrows whose structures are in it.
    copies.intersect({d.molecules.back().id});
    CHECK(io::subset(d, copies).curved.size() == 2);
}

TEST_CASE("mechanism arrows from the keyboard and the Curved tool; undo, redo and moving") {
    testApp();
    Driver d;
    d.c.insertSmiles("CC(=O)OC", QPointF(0, 0));
    REQUIRE(d.c.doc().molecules.size() == 1);
    const QString molId = d.mol().id;
    const Atom* o = nullptr;
    for (const auto& a : d.mol().atoms)
        if (a.el == "O" && d.mol().neighbors(a.id).size() == 1) o = &a;  // the carbonyl O
    REQUIRE(o);
    const QString oId = o->id;

    // Shift+L shows lone pairs; M on the O then M on a C–O single bond draws the arrow from a lone pair.
    d.move(d.atomPos(*o));
    d.key(Qt::Key_L, "L", Qt::ShiftModifier);
    CHECK(d.mol().atom(oId)->lonePairs == 2);
    d.key(Qt::Key_M, "m");
    CHECK(d.c.curvedPending());
    const Bond* single = nullptr;
    for (const auto& b : d.mol().bonds)
        if (b.order == 1 && (d.mol().atom(b.a)->el == "O" || d.mol().atom(b.b)->el == "O")) single = &b;
    REQUIRE(single);
    const QString bondId = single->id;
    d.move(d.bondPos(d.mol(), *single));
    d.key(Qt::Key_M, "m");
    CHECK(!d.c.curvedPending());
    REQUIRE(d.c.doc().curved.size() == 1);
    const CurvedArrow arrow = d.c.doc().curved[0];
    CHECK(arrow.from.atom == oId);
    CHECK(arrow.from.lonePair);
    CHECK(arrow.to.bond == bondId);

    // Undo removes it, redo brings it back.
    d.c.undoStack()->undo();
    CHECK(d.c.doc().curved.empty());
    d.c.undoStack()->redo();
    REQUIRE(d.c.doc().curved.size() == 1);

    // K over the arrow switches to a fishhook.
    const CurvedGeom g0 = d.c.renderer().curvedGeom(d.c.doc().curved[0], d.c.doc(), d.c.overlay());
    d.move(d.c.modelToScreen(d.c.renderer().toModel(g0.apex)));
    d.key(Qt::Key_K, "k");
    CHECK(d.c.doc().curved[0].half);

    // Dragging the structure takes the arrow along.
    const Molecule& m = d.mol();
    const Bond& any = m.bonds[0];
    d.drag(d.bondPos(m, any), d.bondPos(m, any) + QPointF(150, 60));
    const Molecule* moved = d.c.doc().molecule(molId);
    const Bond* sb = moved->bond(bondId);
    const QPointF mid = d.c.renderer().toPt((moved->atom(sb->a)->pos() + moved->atom(sb->b)->pos()) / 2);
    const CurvedGeom g1 = d.c.renderer().curvedGeom(d.c.doc().curved[0], d.c.doc(), d.c.overlay());
    CHECK(len(g1.b - mid) < 1e-6);
    CHECK(len(g1.a - d.c.renderer().toPt(moved->atom(oId)->pos())) < d.c.style().bondLength * 0.6);

    // Dragging the curve itself reshapes it (one undo step).
    const int before = d.c.undoStack()->index();
    d.drag(d.c.modelToScreen(d.c.renderer().toModel(g1.apex)), d.c.modelToScreen(d.c.renderer().toModel(g1.apex)) + QPointF(0, -40));
    CHECK(d.c.doc().curved[0].shaped);
    CHECK(d.c.undoStack()->index() == before + 1);

    // The Curved tool: drag from an atom to another atom.
    d.c.setTool(Canvas::Tool::Mechanism);
    const Molecule& m2 = d.mol();
    const Atom* c0 = firstOf(m2, "C");
    REQUIRE(c0);
    d.drag(d.atomPos(*m2.atom(oId)), d.atomPos(*c0));
    REQUIRE(d.c.doc().curved.size() == 2);
    CHECK(d.c.doc().curved[1].to.atom == c0->id);
    d.c.setTool(Canvas::Tool::Select);
}

TEST_CASE("Shift+drag snaps to other objects with guides; without Shift dragging is free") {
    testApp();
    Driver d;
    d.c.insertSmiles("CCCC", QPointF(0, 0));
    d.c.insertSmiles("CCCC", QPointF(7, 2));
    REQUIRE(d.c.doc().molecules.size() == 2);
    const QString aId = d.mol(0).id, bId = d.mol(1).id;
    auto centreGap = [&] {
        return d.rect(*d.c.doc().molecule(bId)).center().y() - d.rect(*d.c.doc().molecule(aId)).center().y();
    };
    // Drag B up to just short of level with A (0.1 bond lengths away).
    const double gap0 = d.c.renderer().toModel(QPointF(0, centreGap())).y();
    auto dragB = [&](Qt::KeyboardModifiers mods) {
        const Molecule& b = *d.c.doc().molecule(bId);
        const QPointF from = d.bondPos(b, b.bonds[1]);
        const QPointF to = d.c.modelToScreen((b.atom(b.bonds[1].a)->pos() + b.atom(b.bonds[1].b)->pos()) / 2 -
                                             QPointF(0, gap0 - 0.1));
        d.drag(from, to, mods);
    };

    dragB(Qt::NoModifier);
    CHECK(std::abs(centreGap()) > 1.0);  // free: still about 0.1 bond (1.5 pt) off
    d.c.undoStack()->undo();

    dragB(Qt::ShiftModifier);
    CHECK(std::abs(centreGap()) < 1e-6);  // lined up
    CHECK(d.c.guides().empty());          // guides are only shown during the drag
    d.c.undoStack()->undo();

    // Guide maths on its own: equal spacing after a pair, and baselines.
    snap::Box a{QRectF(0, 0, 10, 10), {}}, b{QRectF(30, 0, 10, 10), {}};
    snap::Box moving{QRectF(62, 1, 10, 10), {}};
    snap::Result r = snap::snapMove(moving, {a, b}, 3);
    CHECK(r.delta.x() == Catch::Approx(-2));  // 20 pt gap repeated: left edge at 60
    CHECK(r.delta.y() == Catch::Approx(-1));
    CHECK(!r.guides.empty());
    snap::Box text{QRectF(100, 70, 20, 8), {77}}, other{QRectF(0, 40, 20, 20), {78.5}};
    CHECK(snap::snapMove(text, {other}, 2).delta.y() == Catch::Approx(1.5));
}

TEST_CASE("Alt+drag duplicates: the original stays, the copy moves, one undo step") {
    testApp();
    Driver d;
    d.c.insertSmiles("CCO", QPointF(0, 0));
    const QString id = d.mol().id;
    const QPointF c0 = d.mol().center();
    const int before = d.c.undoStack()->index();
    const Molecule& m = d.mol();
    d.drag(d.bondPos(m, m.bonds[0]), d.bondPos(m, m.bonds[0]) + QPointF(160, 0), Qt::AltModifier);
    REQUIRE(d.c.doc().molecules.size() == 2);
    CHECK(len(d.c.doc().molecule(id)->center() - c0) < 1e-9);
    const Molecule& copy = d.c.doc().molecules[1];
    CHECK(copy.center().x() > c0.x() + 2);
    CHECK(d.c.selection() == QSet<QString>{copy.id});
    CHECK(d.c.undoStack()->index() == before + 1);
    d.c.undoStack()->undo();
    CHECK(d.c.doc().molecules.size() == 1);
}

TEST_CASE("align, distribute, align a bond, and rotate with the handle") {
    testApp();
    Driver d;
    d.c.insertSmiles("CCCC", QPointF(0, 0));
    d.c.insertSmiles("CC", QPointF(5, 1.3));
    d.c.insertSmiles("CCC", QPointF(12, -0.6));
    d.move({5, 5});
    d.c.selectAll();

    d.c.alignSelection(snap::Align::Top);
    const double top = d.rect(d.mol(0)).top();
    for (int i = 1; i < 3; ++i) CHECK(d.rect(d.mol(i)).top() == Catch::Approx(top));

    d.c.alignSelection(snap::Align::DistributeH);
    const QRectF r0 = d.rect(d.mol(0)), r1 = d.rect(d.mol(1)), r2 = d.rect(d.mol(2));
    CHECK(r1.left() - r0.right() == Catch::Approx(r2.left() - r1.right()));

    // Pure maths: centres line up on the centre of the group's bounding box (0..24 here).
    auto deltas = snap::alignDeltas({QRectF(0, 0, 10, 10), QRectF(20, 5, 4, 4)}, snap::Align::HCenter);
    CHECK(deltas[0].x() == Catch::Approx(7));
    CHECK(deltas[1].x() == Catch::Approx(-10));

    // Align a bond horizontally / vertically; the structure turns with it.
    Document doc = d.c.doc();
    const Molecule& m = doc.molecules[0];
    const QString bid = m.bonds[0].id;
    edits::alignBond(doc, bid, true);
    const Molecule* mv = doc.moleculeOfBond(bid);
    const Bond* b = mv->bond(bid);
    CHECK(mv->atom(b->a)->x == Catch::Approx(mv->atom(b->b)->x).margin(1e-9));
    edits::alignBond(doc, bid, false);
    CHECK(mv->atom(b->a)->y == Catch::Approx(mv->atom(b->b)->y).margin(1e-9));

    // Rotate: Shift+R over the first structure shows a handle; dragging it a quarter turn turns 90°.
    const Molecule& first = d.mol(0);
    const QString fid = first.id;
    const QPointF v0 = first.atoms[1].pos() - first.atoms[0].pos();
    d.c.selectAll();
    d.move({5, 5});
    d.c.startRotate();
    REQUIRE(d.c.rotating());
    // Select only the first structure, then rotate it.
    d.key(Qt::Key_Escape, "");
    CHECK(!d.c.rotating());
    d.key(Qt::Key_Escape, "");  // and deselect
    CHECK(d.c.selection().isEmpty());
    const QPointF other0 = d.mol(1).atoms[0].pos();
    d.move(d.bondPos(first, first.bonds[0]));
    d.key(Qt::Key_R, "R", Qt::ShiftModifier);
    REQUIRE(d.c.rotating());
    const QPointF centre = d.c.modelToScreen(d.c.renderer().toModel(d.rect(*d.c.doc().molecule(fid)).center()));
    const QPointF h = d.c.rotateHandleScreen();
    const double reach = len(h - centre);
    d.drag(h, centre + QPointF(reach * 3, 2));  // from straight up to (almost) straight right: snapped to 90°
    const Molecule* rot = d.c.doc().molecule(fid);
    const QPointF v1 = rot->atoms[1].pos() - rot->atoms[0].pos();
    const double turned = std::remainder(std::atan2(v1.y(), v1.x()) - std::atan2(v0.y(), v0.x()), 2 * M_PI);
    CHECK(turned == Catch::Approx(M_PI / 2).margin(1e-6));
    CHECK(len(d.mol(1).atoms[0].pos() - other0) < 1e-9);  // the others are untouched
}

TEST_CASE("an arrow from a bond to its own atom hooks onto the atom; homolysis fishhooks don't cross") {
    testApp();
    Style st;
    Renderer r(st);
    ChemOverlay ov;
    // C–Br heterolysis, with the bond pointing right, up, and on a slant.
    for (int k = 0; k < 8; ++k) {
        const QPointF x(std::cos(k * M_PI / 4), std::sin(k * M_PI / 4));  // the bond in eight directions
        Document d;
        Molecule m;
        m.id = "m1";
        const QPointF back = geom::rotate(-x, M_PI / 3);  // the other carbon, 120° away from the Br
        m.atoms = {{"a1", "C", 0, 0}, {"a2", "Br", x.x(), x.y()}, {"a3", "C", back.x(), back.y()}};
        m.bonds = {{"b1", "a1", "a2", 1}, {"b2", "a1", "a3", 1}};
        d.molecules.push_back(m);
        const QString id = edits::addCurved(d, CurvedEnd{{}, "b1"}, CurvedEnd{"a2"});
        const CurvedGeom g = r.curvedGeom(*d.curvedArrow(id), d, ov);
        REQUIRE(g.ok);
        const QPointF X = r.toPt(x);
        CAPTURE(x.x(), x.y(), g.tip.x(), g.tip.y(), g.radius, g.sweep * 180 / M_PI);
        CHECK(geom::dot(g.tipDir, X - g.tip) > 0);                          // heading toward the atom
        CHECK(std::abs(geom::cross(g.tipDir, X - g.tip)) < 0.4 * st.bondLength);   // and into its label
        CHECK(len(X - g.tip) < 0.6 * st.bondLength);                         // ending at it, not beside it
        CHECK(g.headLength > 0.6 * st.mechanismHeadLength());                // with a real head
        // The tail starts right beside the bond, on the part of it that is drawn (not inside the label).
        const QPointF c0 = r.toPt({0, 0});
        const double along = geom::dot(g.start - c0, geom::unit(X - c0)) / len(X - c0);
        CHECK(geom::distToSegment(g.start, c0, X) < 0.12 * st.bondLength);
        CHECK(along > 0.1);
        CHECK(along < 0.6);
    }
    // Br–Br homolysis: one fishhook to each atom, curving away from each other.
    Document d;
    Molecule m;
    m.id = "m1";
    m.atoms = {{"a1", "Br", 0, 0}, {"a2", "Br", 1, 0}};
    m.bonds = {{"b1", "a1", "a2", 1}};
    d.molecules.push_back(m);
    const QString left = edits::addCurved(d, CurvedEnd{{}, "b1"}, CurvedEnd{"a1"}, true);
    const QString right = edits::addCurved(d, CurvedEnd{{}, "b1"}, CurvedEnd{"a2"}, true);
    const CurvedGeom gl = r.curvedGeom(*d.curvedArrow(left), d, ov), gr = r.curvedGeom(*d.curvedArrow(right), d, ov);
    const double mid = r.toPt({0.5, 0}).x();
    for (const auto& q : gl.points) CHECK(q.x() <= mid + 0.5);
    for (const auto& q : gr.points) CHECK(q.x() >= mid - 0.5);
}

TEST_CASE("the arrowhead carries straight on from the end of the line") {
    testApp();
    Document d;
    Molecule m;
    m.id = "m1";
    m.atoms = {{"a1", "O", 0, 0, -1}, {"a2", "C", 2.5, 0}, {"a3", "C", 3.5, 0}};
    m.bonds = {{"b1", "a2", "a3", 1}};
    d.molecules.push_back(m);
    Style st;
    Renderer r(st);
    ChemOverlay ov;
    for (bool half : {false, true}) {
        const QString id = edits::addCurved(d, CurvedEnd{"a1"}, CurvedEnd{"a2"}, half);
        const CurvedGeom g = r.curvedGeom(*d.curvedArrow(id), d, ov);
        REQUIRE(g.ok);
        // The line meets the head at its notch, which lies on the curve; the head's axis runs along the
        // curve there, and its point sits at the arrow's end.
        double nearest = 1e9;
        size_t seg = 0;
        for (size_t i = 0; i + 1 < g.points.size(); ++i) {
            const double dd = geom::distToSegment(g.notch, g.points[i], g.points[i + 1]);
            if (dd < nearest) { nearest = dd; seg = i; }
        }
        CHECK(nearest < 0.05);
        CHECK(geom::dot(geom::unit(g.points[seg + 1] - g.points[seg]), g.tipDir) > 0.99);
        CHECK(len(g.headTip - (g.notch + g.tipDir * (g.headLength * 0.8))) < 1e-6);
        CHECK(len(g.headTip - g.tip) < 0.8);
    }
}

TEST_CASE("single atoms move: drag a selected atom, or Ctrl+Alt+drag any atom") {
    testApp();
    Driver d;
    d.c.insertSmiles("CCCO", QPointF(0, 0));
    d.move({5, 5});
    d.key(Qt::Key_Escape, "");  // nothing selected
    const Molecule before = d.mol();
    const Atom a0 = before.atoms[0], a1 = before.atoms[1];

    // Alt+click selects one atom; dragging it then moves just that atom.
    const QPointF p0 = d.atomPos(a0);
    d.move(p0, Qt::AltModifier);
    d.mouse(QEvent::MouseButtonPress, p0, Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
    d.mouse(QEvent::MouseButtonRelease, p0, Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
    REQUIRE(d.c.selection() == QSet<QString>{a0.id});
    d.drag(p0, p0 + QPointF(0, 60));
    CHECK(d.mol().atom(a0.id)->y > a0.y + 0.5);
    CHECK(len(d.mol().atom(a1.id)->pos() - a1.pos()) < 1e-9);
    CHECK(d.mol().atoms.size() == before.atoms.size());  // no bond was drawn
    d.c.undoStack()->undo();
    CHECK(len(d.mol().atom(a0.id)->pos() - a0.pos()) < 1e-9);

    // Ctrl+Alt+drag moves an atom that isn't selected.
    d.move({5, 5});
    d.key(Qt::Key_Escape, "");
    const QPointF p1 = d.atomPos(a1);
    d.drag(p1, p1 + QPointF(40, 0), Qt::ControlModifier | Qt::AltModifier);
    CHECK(d.mol().atom(a1.id)->x > a1.x + 0.5);
    CHECK(len(d.mol().atom(a0.id)->pos() - a0.pos()) < 1e-9);

    // A plain drag from an unselected atom still draws a bond.
    const size_t n = d.mol().atoms.size();
    const QPointF pe = d.atomPos(d.mol().atoms.back());
    d.drag(pe, pe + QPointF(0, -40));
    CHECK(d.mol().atoms.size() == n + 1);
}

TEST_CASE("dragging an arrow end highlights what it will attach to") {
    testApp();
    Driver d;
    d.c.insertSmiles("CCO", QPointF(0, 0));
    d.c.setTool(Canvas::Tool::Mechanism);
    const Molecule& m = d.mol();
    const Atom* o = firstOf(m, "O");
    const Atom& c0 = m.atoms[0];
    REQUIRE(o);
    const QPointF from = d.atomPos(*o), to = d.atomPos(c0);
    d.move(from);
    d.mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    for (int i = 1; i <= 8; ++i) d.mouse(QEvent::MouseMove, from + (to - from) * (i / 8.0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    CHECK(d.c.dragTarget() == c0.id);
    // Away from everything: a free end, nothing highlighted.
    d.mouse(QEvent::MouseMove, QPointF(850, 650), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    CHECK(d.c.dragTarget().isEmpty());
    d.mouse(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    d.mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    CHECK(d.c.dragTarget().isEmpty());  // cleared once dropped
    REQUIRE(d.c.doc().curved.size() == 1);
    CHECK(d.c.doc().curved[0].to.atom == c0.id);
    d.c.setTool(Canvas::Tool::Select);
}

TEST_CASE("a hook onto its own atom can be dragged out bigger and back in") {
    testApp();
    Driver d;
    d.c.insertSmiles("CCBr", QPointF(0, 0));
    const Molecule& m = d.mol();
    const Atom* br = firstOf(m, "Br");
    REQUIRE(br);
    const Bond* cbr = nullptr;
    for (const auto& b : m.bonds) if (b.has(br->id)) cbr = &b;
    REQUIRE(cbr);
    const QString brId = br->id, bondId = cbr->id;
    d.c.edit("arrow", [&](Document& doc) { edits::addCurved(doc, CurvedEnd{{}, bondId}, CurvedEnd{brId}); });
    auto geomNow = [&] { return d.c.renderer().curvedGeom(d.c.doc().curved[0], d.c.doc(), d.c.overlay()); };
    auto standOut = [&](const CurvedGeom& g) { return len(g.apex - (g.a + g.b) / 2); };
    const CurvedGeom g0 = geomNow();
    REQUIRE(g0.ok);
    // Drag the middle handle out, away from the line between the ends.
    const QPointF apex = d.c.modelToScreen(d.c.renderer().toModel(g0.apex));
    const QPointF outward = geom::unit(g0.apex - (g0.a + g0.b) / 2);
    d.move({5, 5});
    d.move(apex);
    d.drag(apex, apex + outward * 30);
    const CurvedGeom g1 = geomNow();
    CHECK(d.c.doc().curved[0].shaped);
    CHECK(standOut(g1) > standOut(g0) + 3);
    CHECK(len(r_toPt(d, br->pos()) - g1.tip) < 0.75 * d.c.style().bondLength);  // still lands on the Br
}

TEST_CASE("lone pairs sit on the element, clear of the hydrogens its label writes") {
    testApp();
    Style st;
    Renderer r(st);
    for (const char* smi : {"CCO", "OCC", "CCN"}) {
        auto m = chem::fromSmiles(smi);
        REQUIRE(m);
        ChemOverlay ov;
        auto an = chem::analyze(*m);
        for (auto it = an.implicitH.begin(); it != an.implicitH.end(); ++it) ov.implicitH[it.key()] = it.value();
        for (auto& a : m->atoms) {
            if (a.el == "C") continue;
            a.lonePairs = a.el == "O" ? 2 : 1;
            const AtomLabel L = r.atomLabel(*m, a, ov);
            REQUIRE(L.visible);
            const QPointF c = r.toPt(a.pos());
            for (const auto& q : r.lonePairPoints(*m, a, ov)) {
                CAPTURE(smi, q.x(), q.y(), L.clip.left(), L.clip.right(), L.bounds.left(), L.bounds.right());
                CHECK(!L.bounds.contains(q));                         // never on top of the letters
                CHECK(q.x() > L.clip.left() - 0.5 * st.fontSize);     // and over the O or N itself,
                CHECK(q.x() < L.clip.right() + 0.5 * st.fontSize);    // not out past its H
                CHECK(len(q - c) < 1.2 * st.fontSize);
            }
        }
    }
}

TEST_CASE("lone pairs come from the chemistry: never more than an atom has") {
    testApp();
    // How many each atom really has (RDKit: outer electrons not in bonds, charge or radicals).
    struct Case { const char* smiles; const char* el; int charge; int pairs; };
    const Case cases[] = {{"CCO", "O", 0, 2},          {"[OH-]", "O", -1, 3},   {"C[CH2-]", "C", -1, 1},
                          {"C[NH3+]", "N", 1, 0},      {"CS(C)(=O)=O", "S", 0, 0}, {"C[N+](=O)[O-]", "N", 1, 0},
                          {"C[N+](=O)[O-]", "O", -1, 3}, {"CBr", "Br", 0, 3},     {"C[CH2+]", "C", 1, 0},
                          {"CCN", "N", 0, 1}};
    for (const auto& k : cases) {
        auto m = chem::fromSmiles(k.smiles);
        REQUIRE(m);
        const auto an = chem::analyze(*m);
        bool found = false;
        for (const auto& a : m->atoms)
            if (a.el == k.el && a.charge == k.charge) {
                CAPTURE(k.smiles, k.el);
                CHECK(an.lonePairs.value(a.id) == k.pairs);
                found = true;
            }
        CHECK(found);
    }

    Driver d;
    d.c.insertSmiles("CCO", QPointF(0, 0));
    const Atom* o = firstOf(d.mol(), "O");
    REQUIRE(o);
    const QString oId = o->id;
    // Shift+L: both pairs, then one, then none, then both again; never a third.
    d.move(d.atomPos(*o));
    for (int want : {2, 1, 0, 2}) {
        d.key(Qt::Key_L, "L", Qt::ShiftModifier);
        CHECK(d.mol().atom(oId)->lonePairs == want);
    }
    // Turn the O into a C: it has no lone pairs, so they go (in the same undo step).
    d.key(Qt::Key_C, "c");
    CHECK(d.mol().atom(oId)->lonePairs == 0);
    d.c.undoStack()->undo();
    CHECK(d.mol().atom(oId)->lonePairs == 2);
    // A carbon can't be given any.
    const QString cId = d.mol().atoms[0].id;
    d.move(d.atomPos(d.mol().atoms[0]));
    d.key(Qt::Key_L, "L", Qt::ShiftModifier);
    CHECK(d.mol().atom(cId)->lonePairs == 0);

    // A file asking for more than an atom has opens with the true number.
    Document doc;
    Molecule mm;
    mm.id = "m1";
    mm.atoms = {{"a1", "C", 0, 0}, {"a2", "O", 1, 0}};
    mm.bonds = {{"b1", "a1", "a2", 1}};
    mm.atoms[0].lonePairs = 4;
    mm.atoms[1].lonePairs = 4;
    doc.molecules.push_back(mm);
    auto back = io::fromJson(io::toJson(doc, false));
    REQUIRE(back);
    CHECK(back->molecules[0].atom("a1")->lonePairs == 0);
    CHECK(back->molecules[0].atom("a2")->lonePairs == 2);
}

TEST_CASE("double-clicking an atom focuses it for typing") {
    testApp();
    Driver d;
    d.c.insertSmiles("CC", QPointF(0, 0));
    d.move({5, 5});
    d.key(Qt::Key_Escape, "");
    const Atom end = d.mol().atoms.back();
    const QPointF p = d.atomPos(end);
    d.move(p);
    d.mouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    d.mouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    d.mouse(QEvent::MouseButtonDblClick, p, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    d.mouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    CHECK(d.c.focusedAtom() == end.id);
    // Keys now add to it wherever the mouse is.
    d.move({850, 650});
    const size_t n = d.mol().atoms.size();
    d.key(Qt::Key_1, "1");
    CHECK(d.mol().atoms.size() == n + 1);
}

TEST_CASE("a hook's tail clears the lines of a double bond on either side") {
    testApp();
    Style st;
    Renderer r(st);
    ChemOverlay ov;
    for (int pos : {0, 1, 2, 3}) {  // the second line placed automatically, left, right, centred
        for (double sideSign : {1.0, -1.0}) {
            Document d;
            Molecule m;
            m.id = "m1";
            m.atoms = {{"a1", "C", 0, 0}, {"a2", "O", 0, -1}, {"a3", "C", -0.87, 0.5}, {"a4", "C", 0.87, 0.5}};
            m.bonds = {{"b1", "a1", "a2", 2}, {"b2", "a1", "a3", 1}, {"b3", "a1", "a4", 1}};
            m.bonds[0].dbpos = DbPos(pos);
            d.molecules.push_back(m);
            const QString id = edits::addCurved(d, CurvedEnd{{}, "b1"}, CurvedEnd{"a2"});
            CurvedArrow* c = d.curvedArrow(id);
            c->manual = true;
            c->bulge = 0.5 * sideSign;
            const CurvedGeom g = r.curvedGeom(*c, d, ov);
            REQUIRE(g.ok);
            // Where the two lines of the double bond are drawn (points).
            const QPointF pa = r.toPt({0, 0}), pb = r.toPt({0, -1});
            const QPointF nb = geom::perp(geom::unit(pb - pa));
            const int side = geom::doubleBondSide(d.molecules[0], d.molecules[0].bonds[0]);
            const double sp = st.spacingPt();
            std::vector<double> lines = side == 0 ? std::vector<double>{-sp / 2, sp / 2} : std::vector<double>{0, side * sp};
            CAPTURE(pos, sideSign, g.start.x(), g.start.y());
            for (double off : lines)
                CHECK(geom::distToSegment(g.start, pa + nb * off, pb + nb * off) > 0.6);
            CHECK(geom::distToSegment(g.start, pa, pb) < 0.45 * st.bondLength);  // still right beside the bond
        }
    }
}

TEST_CASE("an arrow ending at a bare carbon stops short of its bonds, both lines of a double bond included") {
    testApp();
    auto doc = io::loadFile(CHEMFOX_SOURCE_DIR "/samples/mechanism.cmfx");
    REQUIRE(doc);
    Style st;
    Renderer r(st);
    ChemOverlay ov;
    const CurvedArrow* toKetone = doc->curvedArrow("e5");  // cyanide's lone pair onto the carbonyl carbon
    REQUIRE(toKetone);
    const CurvedGeom g = r.curvedGeom(*toKetone, *doc, ov);
    REQUIRE(g.ok);
    const Molecule* m = doc->moleculeOfAtom("c3");
    const double sp = st.spacingPt();
    for (const auto* bd : m->bondsOf("c3")) {
        const QPointF pa = r.toPt(m->atom(bd->a)->pos()), pb = r.toPt(m->atom(bd->b)->pos());
        const QPointF nb = geom::perp(geom::unit(pb - pa));
        std::vector<double> offs{0};
        if (bd->order == 2) {
            const int side = geom::doubleBondSide(*m, *bd);
            offs = side == 0 ? std::vector<double>{-sp / 2, sp / 2} : std::vector<double>{0, side * sp};
        }
        for (double o : offs) CHECK(geom::distToSegment(g.tip, pa + nb * o, pb + nb * o) > st.mechanismHeadWidth() * 0.8);
    }
}

TEST_CASE("freeform arrows: control handles shape them, ends lock onto atoms and follow them") {
    testApp();
    Driver d;
    d.c.insertSmiles("CCO", QPointF(0, 0));
    d.c.insertSmiles("CC", QPointF(0, 4));
    const Molecule& m = d.mol(0);
    const Atom* o = firstOf(m, "O");
    REQUIRE(o);
    const QString oId = o->id, c0 = m.atoms[0].id, otherC = d.mol(1).atoms[0].id, mol2 = d.mol(1).id;
    d.c.edit("arrow", [&](Document& doc) { edits::addCurved(doc, CurvedEnd{oId}, CurvedEnd{c0}); });
    auto geomNow = [&] { return d.c.renderer().curvedGeom(d.c.doc().curved[0], d.c.doc(), d.c.overlay()); };
    auto screen = [&](QPointF pt) { return d.c.modelToScreen(d.c.renderer().toModel(pt)); };

    // Select the arrow, then drag its first control handle: the shape becomes hand-set and the curve moves.
    CurvedGeom g = geomNow();
    d.move({5, 5});
    d.c.selectAll();
    const QPointF h1 = screen(g.c1);
    d.move(h1);
    d.drag(h1, h1 + QPointF(0, -40));
    REQUIRE(d.c.doc().curved[0].shaped);
    const CurvedGeom g1 = geomNow();
    CHECK(len(g1.c1 - g.c1) > 10);
    CHECK(len(g1.start - g.start) < 0.5);  // the ends stay put
    CHECK(len(g1.tip - g.tip) < 0.5);

    // Drag the head onto a carbon of the other structure, a little off its centre: it attaches there.
    const QPointF target = d.c.renderer().toPt(d.c.doc().molecule(mol2)->atom(otherC)->pos()) + QPointF(2, -2);
    const QPointF tipScreen = screen(g1.tip);
    d.move(tipScreen);
    d.drag(tipScreen, screen(target));
    const CurvedArrow a = d.c.doc().curved[0];
    CAPTURE(a.to.atom, a.to.bond, a.to.x, a.to.y, a.toOff.x(), a.toOff.y(), tipScreen.x(), tipScreen.y(), d.c.undoStack()->count());
    CHECK(a.to.atom == otherC);
    CHECK(len(geomNow().tip - target) < 1.0);  // exactly where it was dropped

    // Move that structure: the head follows, keeping its offset.
    const QPointF tip0 = geomNow().tip;
    d.c.edit("move", [&](Document& doc) { edits::moveObjects(doc, {mol2}, QPointF(2, 1)); });
    CHECK(len(geomNow().tip - (tip0 + d.c.renderer().toPt({2, 1}))) < 1e-6);

    // Rotate it half a turn: the offset turns with it.
    const QPointF cAt = d.c.renderer().toPt(d.c.doc().molecule(mol2)->atom(otherC)->pos());
    const QPointF offBefore = geomNow().tip - cAt;
    d.c.edit("rotate", [&](Document& doc) { edits::transformObjects(doc, {mol2}, -1, 0, 0, -1); });
    const QPointF cAfter = d.c.renderer().toPt(d.c.doc().molecule(mol2)->atom(otherC)->pos());
    CHECK(len((geomNow().tip - cAfter) + offBefore) < 1e-6);

    // Saved and reopened: the same shape.
    auto back = io::fromJson(io::toJson(d.c.doc(), false));
    REQUIRE(back);
    const CurvedArrow& b = back->curved[0];
    CHECK(b.shaped);
    CHECK(len(b.c1 - d.c.doc().curved[0].c1) < 1e-3);
    CHECK(len(b.toOff - d.c.doc().curved[0].toOff) < 1e-3);

    // Reset Shape goes back to the automatic one.
    d.c.edit("reset", [&](Document& doc) { doc.curved[0].shaped = false; });
    CHECK(!d.c.doc().curved[0].shaped);
}

TEST_CASE("plus signs can be removed and restored; one arrow can hide its step letter") {
    testApp();
    Driver d;
    d.c.insertSmiles("[OH-]", QPointF(0, 0));
    d.c.insertSmiles("CC(C)Br", QPointF(3, 0));
    d.move({5, 5});
    d.c.selectAll();
    d.c.continueReaction(QPointF(1, 0));  // both structures, so a "+" on each side
    auto plus = [&] { return d.c.renderer().plusSigns(d.c.doc(), d.c.overlay()); };
    REQUIRE(plus().size() == 2);

    // Hover one and press Delete: it goes, the other stays; undo brings it back.
    const PlusSign first = plus()[0];
    d.move(d.c.modelToScreen(d.c.renderer().toModel(first.rect.center())));
    d.key(Qt::Key_Delete, "");
    REQUIRE(plus().size() == 1);
    CHECK(plus()[0].id() != first.id());
    d.c.undoStack()->undo();
    CHECK(plus().size() == 2);
    d.c.undoStack()->redo();

    // It stays removed when the scheme is arranged again, and in the saved file.
    d.c.arrangeScheme();
    CHECK(plus().size() == 1);
    auto back = io::fromJson(io::toJson(d.c.doc(), false));
    REQUIRE(back);
    Renderer r(d.c.style());
    CHECK(r.plusSigns(*back, d.c.overlay()).size() == 1);

    // Step letters: hiding one arrow's letter leaves the next one lettered "a".
    Document doc = d.c.doc();
    doc.stepLetters = true;
    doc.arrows.push_back(doc.arrows[0]);
    doc.arrows.back().id = "r999";
    doc.arrows.back().translate(QPointF(0, 6));
    const QString first2 = doc.arrows[0].id;
    auto letterRuns = [&](const Arrow& a) { return r.arrowGeom(a, &doc).above.size(); };
    const size_t withLetter = letterRuns(doc.arrows[0]);
    doc.arrow(first2)->hideLetter = true;
    CHECK(letterRuns(*doc.arrow(first2)) == withLetter - 1);
    CHECK(letterRuns(*doc.arrow("r999")) == withLetter);  // still lettered
    CHECK(io::fromJson(io::toJson(doc, false))->arrow(first2)->hideLetter);
}
