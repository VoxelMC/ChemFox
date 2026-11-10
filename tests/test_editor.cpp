// Drives the canvas with real mouse and key events: hover-and-type, drawing, undo.

#include "app.h"
#include "editor/Keymap.h"
#include "core/model/Geometry.h"
#include "app/Shortcuts.h"
#include "app/Preferences.h"
#include "style/Style.h"
#include "core/chem/Chem.h"
#include "core/commands/Edits.h"
#include "editor/Canvas.h"

#include <QAction>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QKeyEvent>
#include <QMenu>
#include <QTabWidget>
#include <QLineEdit>
#include <QMouseEvent>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace cd;

namespace {

struct Driver {
    Canvas c;
    Driver() {
        c.resize(900, 700);
        c.show();
    }
    void move(QPointF p, Qt::MouseButtons b = Qt::NoButton) {
        QMouseEvent e(QEvent::MouseMove, p, c.mapToGlobal(p), Qt::NoButton, b, Qt::NoModifier);
        QApplication::sendEvent(&c, &e);
    }
    void press(QPointF p) {
        QMouseEvent e(QEvent::MouseButtonPress, p, c.mapToGlobal(p), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&c, &e);
    }
    void release(QPointF p) {
        QMouseEvent e(QEvent::MouseButtonRelease, p, c.mapToGlobal(p), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&c, &e);
    }
    void drag(QPointF a, QPointF b) {
        move(a);
        press(a);
        for (int i = 1; i <= 8; ++i) move(a + (b - a) * (i / 8.0), Qt::LeftButton);
        release(b);
    }
    void key(int k, const QString& text, Qt::KeyboardModifiers m = Qt::NoModifier) {
        QKeyEvent e(QEvent::KeyPress, k, m, text);
        QApplication::sendEvent(&c, &e);
    }
    const Molecule& mol(int i = 0) { return c.doc().molecules.at(i); }
    QPointF atomPos(const Atom& a) { return c.modelToScreen(a.pos()); }
    QPointF bondPos(const Molecule& m, const Bond& b) {
        return c.modelToScreen((m.atom(b.a)->pos() + m.atom(b.b)->pos()) / 2);
    }
};

int countOrder(const Molecule& m, int order) {
    int n = 0;
    for (auto& b : m.bonds) n += b.order == order;
    return n;
}

}  // namespace

TEST_CASE("hover and type builds and edits structures") {
    testApp();
    Driver d;

    // Empty canvas: r drops a benzene at the cursor.
    d.move({450, 350});
    d.key(Qt::Key_R, "r");
    REQUIRE(d.c.doc().molecules.size() == 1);
    CHECK(d.mol().atoms.size() == 6);
    CHECK(countOrder(d.mol(), 2) == 3);

    // Hover an atom and type N.
    Atom a0 = d.mol().atoms[0];
    d.move(d.atomPos(a0));
    d.key(Qt::Key_N, "n");
    CHECK(d.mol().atom(a0.id)->el == "N");

    // 1 grows a bond from the hovered atom.
    d.key(Qt::Key_1, "1");
    CHECK(d.mol().atoms.size() == 7);

    // Undo and redo cover it.
    d.c.undoStack()->undo();
    CHECK(d.mol().atoms.size() == 6);
    d.c.undoStack()->redo();
    CHECK(d.mol().atoms.size() == 7);

    // + raises the charge.
    d.key(Qt::Key_Plus, "+");
    CHECK(d.mol().atom(a0.id)->charge == 1);

    // On the substituent bond: w makes a wedge, 2 makes it double.
    const Molecule& m = d.mol();
    const Bond* sub = nullptr;
    for (auto& b : m.bonds) if (b.has(a0.id) && m.neighbors(b.other(a0.id)).size() == 1) sub = &b;
    REQUIRE(sub);
    QString subId = sub->id;
    d.move(d.bondPos(m, *sub));
    d.key(Qt::Key_W, "w");
    CHECK(d.mol().bond(subId)->stereo == BondStereo::Wedge);
    d.key(Qt::Key_2, "2");
    CHECK(d.mol().bond(subId)->order == 2);
    CHECK(d.mol().bond(subId)->stereo == BondStereo::None);

    // Shift+6 on a ring bond fuses a second ring.
    const Bond* ringBond = nullptr;
    for (auto& b : d.mol().bonds) if (!b.has(a0.id) && b.order == 1) { ringBond = &b; break; }
    REQUIRE(ringBond);
    d.move(d.bondPos(d.mol(), *ringBond));
    d.key(Qt::Key_AsciiCircum, "^", Qt::ShiftModifier);
    CHECK(d.mol().atoms.size() == 11);

    // Double-clicking an atom doesn't open the label editor; X does.
    Atom tip;
    for (auto& a : d.mol().atoms) if (d.mol().neighbors(a.id).size() == 1) tip = a;
    d.move(d.atomPos(tip));
    {
        QMouseEvent dbl(QEvent::MouseButtonDblClick, d.atomPos(tip), d.c.mapToGlobal(d.atomPos(tip)), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&d.c, &dbl);
        auto* open = d.c.findChild<QLineEdit*>();
        CHECK((open == nullptr || !open->isVisible()));
    }
    d.key(Qt::Key_X, "x");
    auto* le = d.c.findChild<QLineEdit*>();
    REQUIRE(le);
    le->setText("MeO2C");
    emit le->returnPressed();
    CHECK(d.mol().atom(tip.id)->label == "CO2Me");  // stored canonically, flipped when drawn

    // Delete removes the hovered atom.
    size_t before = d.mol().atoms.size();
    d.move(d.atomPos(*d.mol().atom(tip.id)));
    d.key(Qt::Key_Delete, "");
    CHECK(d.mol().atoms.size() == before - 1);
}

TEST_CASE("mouse drawing: chains, bonds from atoms, moving") {
    testApp();
    Driver d;

    // With the default Select tool, dragging on empty canvas only selects.
    d.drag({100, 500}, {300, 500});
    CHECK(d.c.doc().molecules.empty());

    // The Chain tool draws a zig-zag chain.
    d.c.setTool(Canvas::Tool::Chain);
    d.drag({100, 500}, {300, 500});
    REQUIRE(d.c.doc().molecules.size() == 1);
    size_t n = d.mol().atoms.size();
    CHECK(n >= 4);

    // Short drag from an atom adds one bond, snapped to unit length.
    Atom end = d.mol().atoms.back();
    QPointF p = d.atomPos(end);
    d.drag(p, p + QPointF(0, -40));
    CHECK(d.mol().atoms.size() == n + 1);

    // Dragging a bond moves the whole molecule.
    const Molecule& m = d.mol();
    QPointF c0 = m.center();
    d.drag(d.bondPos(m, m.bonds[0]), d.bondPos(m, m.bonds[0]) + QPointF(120, 0));
    CHECK(d.mol().center().x() > c0.x() + 0.5);
    d.c.undoStack()->undo();
    CHECK(std::abs(d.mol().center().x() - c0.x()) < 1e-9);

    // Marquee selection picks up the whole molecule.
    d.c.setTool(Canvas::Tool::Select);
    QRectF box = QRectF(d.atomPos(d.mol().atoms.front()), d.atomPos(d.mol().atoms.back())).normalized();
    d.drag(box.topLeft() - QPointF(60, 60), box.bottomRight() + QPointF(60, 60));
    CHECK(d.c.selection().contains(d.mol().id));

    // Bent arrow tool: click corners, double-click to finish.
    d.c.setTool(Canvas::Tool::BentArrow);
    for (QPointF p : {QPointF(500, 100), QPointF(500, 200), QPointF(400, 205)}) {
        d.move(p);
        d.press(p);
        d.release(p);
    }
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(400, 205), d.c.mapToGlobal(QPointF(400, 205)),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&d.c, &dbl);
    REQUIRE(d.c.doc().arrows.size() == 1);
    const Arrow& bent = d.c.doc().arrows[0];
    CHECK(bent.via.size() == 1);
    CHECK(std::abs(bent.y2 - bent.via[0].y()) < 1e-9);  // last leg snapped horizontal
    d.c.undoStack()->undo();
    d.c.setTool(Canvas::Tool::Select);

    // a on empty canvas makes an arrow; Delete while hovering removes it.
    d.move({600, 200});
    d.key(Qt::Key_A, "a");
    REQUIRE(d.c.doc().arrows.size() == 1);
    d.move(d.c.modelToScreen((d.c.doc().arrows[0].tail() + d.c.doc().arrows[0].head()) / 2));
    d.key(Qt::Key_Delete, "");
    CHECK(d.c.doc().arrows.empty());
}

TEST_CASE("bent arrows reshape: drag a leg sideways, drag a corner; Ctrl+drag moves") {
    testApp();
    Driver d;
    d.c.edit("arrow", [](Document& doc) {
        QString id = edits::addArrow(doc, {2, 2}, 3);
        Arrow* a = doc.arrow(id);
        a->via = {QPointF(2, 6), QPointF(10, 6)};
        a->x2 = 10;
        a->y2 = 9;
    });
    auto arrow = [&]() -> const Arrow& { return d.c.doc().arrows[0]; };

    // Drag the middle (horizontal) leg down by about one bond: both its corners move down only.
    QPointF mid = d.c.modelToScreen({6, 6});
    QPointF down = d.c.modelToScreen({6, 7}) - d.c.modelToScreen({6, 6});
    d.drag(mid, mid + down);
    CHECK(std::abs(arrow().via[0].y() - 7) < 0.1);
    CHECK(std::abs(arrow().via[1].y() - 7) < 0.1);
    CHECK(std::abs(arrow().via[0].x() - 2) < 1e-9);
    CHECK(std::abs(arrow().x1 - 2) < 1e-9);  // the tail stays put

    // Drag a corner.
    QPointF corner = d.c.modelToScreen(arrow().via[1]);
    d.drag(corner, corner + (d.c.modelToScreen({12, 7}) - d.c.modelToScreen({10, 7})));
    CHECK(std::abs(arrow().via[1].x() - 12) < 0.1);

    // Undo restores the previous shape in one step.
    d.c.undoStack()->undo();
    CHECK(std::abs(arrow().via[1].x() - 10) < 1e-9);
}

TEST_CASE("snapshot: selected bent arrow and an expanded label") {
    QString dir = qEnvironmentVariable("CHEMFOX_SNAPSHOT");
    if (dir.isEmpty()) return;
    testApp();
    Driver d;
    d.c.edit("setup", [](Document& doc) {
        QString id = edits::addArrow(doc, {2, 2}, 3);
        Arrow* a = doc.arrow(id);
        a->via = {QPointF(2, 6), QPointF(14, 6)};
        a->x2 = 14;
        a->y2 = 9;
        a->above = "*p*-coumaroyl-CoA";
        edits::placeRing(doc, {20, 3}, 6, true);
        QString tip = edits::growBond(doc, doc.molecules[0].atoms[1].id, 1);
        edits::setLabel(doc, tip, "CO2Et");
        QString tip2 = edits::growBond(doc, doc.molecules[0].atoms[4].id, 1);
        edits::setLabel(doc, tip2, "CO2Et");
        chem::expandLabel(doc, tip2);
    });
    d.c.zoomToFit();
    d.move(d.c.modelToScreen({8, 6}));
    QApplication::processEvents();
    d.c.grab().save(dir + "/bent_select.png");
}

TEST_CASE("snapshot: stereo labels, all carbons, arrow styles") {
    QString dir = qEnvironmentVariable("CHEMFOX_SNAPSHOT");
    if (dir.isEmpty()) return;
    testApp();
    Driver d;
    d.c.edit("setup", [](Document& doc) {
        auto ids = edits::insertMolecules(doc, {*chem::fromSmiles("C[C@@H](O)[C@H](N)C(=O)O")}, {0, 0});
        doc.molecule(ids[0])->showStereo = true;
        auto ids2 = edits::insertMolecules(doc, {*chem::fromSmiles("CC/C=C/CO")}, {9, 0});
        doc.molecule(ids2[0])->showStereo = true;
        doc.molecule(ids2[0])->showCarbons = true;
        const char* heads[] = {"", "open", "half", "none"};
        for (int i = 0; i < 4; ++i) {
            QString id = edits::addArrow(doc, {-4, 5.0 + i * 1.6}, 4);
            Arrow* a = doc.arrow(id);
            a->headStyle = heads[i];
            a->bothHeads = i == 1;
            a->line = i == 2 ? "dashed" : i == 3 ? "dotted" : "";
            a->widthScale = i == 0 ? 1.8 : 1.0;
        }
    });
    d.c.zoomToFit();
    QApplication::processEvents();
    d.c.grab().save(dir + "/menus_features.png");
}

TEST_CASE("the document changing mid-drag cancels the drag instead of crashing") {
    testApp();
    // Bond drag from an atom; undo removes the molecule; the mouse keeps moving.
    {
        Driver d;
        d.move({450, 350});
        d.key(Qt::Key_R, "r");
        QPointF a = d.atomPos(d.mol().atoms[0]);
        d.move(a);
        d.press(a);
        d.move(a + QPointF(30, 0), Qt::LeftButton);
        d.c.undoStack()->undo();
        CHECK(d.c.doc().molecules.empty());
        d.move(a + QPointF(60, 10), Qt::LeftButton);
        d.release(a + QPointF(60, 10));
        CHECK(d.c.doc().molecules.empty());  // the stale drag didn't recreate anything
    }
    // Reshaping a bent arrow while it is undone away.
    {
        Driver d;
        d.c.edit("arrow", [](Document& doc) {
            QString id = edits::addArrow(doc, {2, 2}, 3);
            doc.arrow(id)->via = {QPointF(2, 6), QPointF(10, 6)};
            doc.arrow(id)->x2 = 10;
            doc.arrow(id)->y2 = 9;
        });
        QPointF corner = d.c.modelToScreen({10, 6});
        d.move(corner);
        d.press(corner);
        d.move(corner + QPointF(20, 0), Qt::LeftButton);
        d.c.undoStack()->undo();
        d.move(corner + QPointF(40, 0), Qt::LeftButton);
        d.release(corner + QPointF(40, 0));
        CHECK(d.c.doc().arrows.empty());
    }
}

TEST_CASE("arrow ends are grabbable even right next to a structure") {
    testApp();
    Driver d;
    // An arrow whose head ends right on a benzene atom.
    d.move({450, 350});
    d.key(Qt::Key_R, "r");
    QPointF atom = d.mol().atoms[0].pos();
    d.c.edit("arrow", [&](Document& doc) {
        QString id = edits::addArrow(doc, atom - QPointF(4, 0), 3);
        doc.arrow(id)->x2 = atom.x() - 0.15;
        doc.arrow(id)->y2 = atom.y();
    });
    size_t atoms = d.mol().atoms.size();
    QPointF head = d.c.modelToScreen(d.c.doc().arrows[0].head());
    d.drag(head, head + QPointF(0, 60));
    // The head moved; no bond was drawn from the atom.
    CHECK(d.c.doc().arrows[0].y2 > atom.y() + 0.5);
    CHECK(d.mol().atoms.size() == atoms);
    // Tail too.
    QPointF tail = d.c.modelToScreen(d.c.doc().arrows[0].tail());
    d.drag(tail, tail + QPointF(-40, 0));
    CHECK(d.c.doc().arrows[0].x1 < atom.x() - 4.5);
}

TEST_CASE("tool keys, commands and rebinding") {
    testApp();
    Driver d;
    // Default tool keys: V select, G chain, Shift+B bent arrow.
    d.move({450, 350});
    d.key(Qt::Key_G, "g");
    CHECK(d.c.tool() == Canvas::Tool::Chain);
    d.key(Qt::Key_B, "B", Qt::ShiftModifier);
    CHECK(d.c.tool() == Canvas::Tool::BentArrow);
    d.key(Qt::Key_V, "v");
    CHECK(d.c.tool() == Canvas::Tool::Select);

    // Commands act on the hovered structure: flip keeps the molecule (same SMILES), moves atoms.
    d.c.insertMolecules({*chem::fromSmiles("C[C@@H](O)CC")}, d.c.doc().molecules.empty() ? QPointF(10, 10) : QPointF(10, 10));
    const Molecule& m = d.c.doc().molecules.back();
    QString smi = chem::smiles(m);
    QPointF p0 = m.atoms[0].pos();
    d.move(d.atomPos(m.atoms[1]));
    d.c.command("structure.flipH");
    CHECK(chem::smiles(d.c.doc().molecules.back()) == smi);
    CHECK(geom::len(d.c.doc().molecules.back().atoms[0].pos() - p0) > 0.1);
    d.c.command("structure.toggleStereo");
    CHECK(d.c.doc().molecules.back().showStereo);

    // Rebinding a hover key: Shift+6 on an atom now makes a cyclopentane ring; X unbound -> nothing.
    Keymap::Map km = d.c.keymap().map();
    for (const auto& k : km["atom"].keys()) if (km["atom"][k] == "ring:5") km["atom"].remove(k);
    km["atom"]["Shift+6"] = "ring:5";
    d.c.keymap().setMap(km);
    size_t before = d.c.doc().molecules.back().atoms.size();
    d.move(d.atomPos(d.c.doc().molecules.back().atoms[4]));
    d.key(Qt::Key_AsciiCircum, "^", Qt::ShiftModifier);
    CHECK(d.c.doc().molecules.back().atoms.size() == before + 4);  // a 5-ring adds 4 atoms
}

TEST_CASE("keymap saves unbinds and new bindings; shortcut registry round-trips") {
    testApp();
    Keymap k;
    k.load();
    QFile::copy(k.path(), k.path() + ".bak");
    Keymap::Map m = Keymap::defaults();
    m["any"].remove("V");             // unbind a default
    m["empty"]["X"] = "element:Si";   // bind an action that has no default key
    k.setMap(m);
    REQUIRE(k.save());
    Keymap k2;
    k2.load();
    CHECK(k2.action("empty", "V").isEmpty());
    CHECK(k2.action("empty", "X") == "element:Si");
    CHECK(k2.action("atom", "C") == "element:C");  // everything else intact
    QFile::remove(k.path());
    QFile::rename(k.path() + ".bak", k.path());

    // Command shortcuts: defaults recorded, overrides saved and reapplied, conflicts found.
    QAction a("Flip"), b("Rotate");
    a.setShortcut(QKeySequence("Ctrl+Alt+F"));
    ShortcutRegistry reg;
    reg.add(&a, "test.flip", "Test");
    reg.add(&b, "test.rotate", "Test");
    QFile::copy(reg.path(), reg.path() + ".bak");
    reg.set("test.rotate", {QKeySequence("Ctrl+Alt+R")});
    CHECK(reg.holder(QKeySequence("Ctrl+Alt+F"), "test.rotate")->id == "test.flip");
    REQUIRE(reg.save());
    QAction a2("Flip"), b2("Rotate");
    a2.setShortcut(QKeySequence("Ctrl+Alt+F"));
    ShortcutRegistry reg2;
    reg2.add(&a2, "test.flip", "Test");
    reg2.add(&b2, "test.rotate", "Test");
    reg2.load();
    CHECK(b2.shortcut() == QKeySequence("Ctrl+Alt+R"));
    CHECK(a2.shortcut() == QKeySequence("Ctrl+Alt+F"));
    reg2.resetAll();
    CHECK(b2.shortcut().isEmpty());
    QFile::remove(reg.path());
    QFile::rename(reg.path() + ".bak", reg.path());
}

TEST_CASE("snapshot: keyboard shortcuts dialog") {
    QString dir = qEnvironmentVariable("CHEMFOX_SNAPSHOT");
    if (dir.isEmpty()) return;
    testApp();
    Canvas c;
    QMenu structure("&Structure"), edit("&Edit");
    edit.addAction("&Undo")->setShortcut(QKeySequence::Undo);
    edit.addAction("&Copy")->setShortcut(QKeySequence::Copy);
    structure.addAction("&Clean Structure")->setShortcut(QKeySequence("Ctrl+L"));
    auto* t = structure.addMenu("&Transform");
    t->addAction("Flip Horizontal");
    t->addAction("Flip Vertical");
    t->addAction("Rotate 90° Clockwise");
    structure.addAction("Align to Reactant / Product");
    ShortcutRegistry reg;
    reg.addMenu(&edit);
    reg.addMenu(&structure);
    ShortcutsDialog dlg(reg, &c);
    dlg.resize(680, 560);
    dlg.show();
    QApplication::processEvents();
    dlg.grab().save(dir + "/shortcuts_commands.png");
    dlg.findChild<QTabWidget*>()->setCurrentIndex(1);
    QApplication::processEvents();
    dlg.grab().save(dir + "/shortcuts_hover.png");
}

TEST_CASE("keyboard chains follow the newest atom and lean toward the mouse") {
    testApp();
    Driver d;
    Document doc;
    QString first = edits::addAtom(doc, {5, 5});
    d.c.setDocument(doc);
    // Three presses of 1 without moving the mouse: a chain of three bonds, not a star.
    d.move(d.atomPos(*d.mol().atom(first)));
    for (int i = 0; i < 3; ++i) d.key(Qt::Key_1, "1");
    REQUIRE(d.mol().atoms.size() == 4);
    CHECK(d.mol().neighbors(first).size() == 1);
    int ends = 0;
    for (const auto& a : d.mol().atoms) ends += d.mol().neighbors(a.id).size() == 1;
    CHECK(ends == 2);
    // It keeps building from the newest atom wherever the mouse goes, even over another atom...
    d.move(d.atomPos(*d.mol().atom(first)));
    d.key(Qt::Key_1, "1");
    CHECK(d.mol().neighbors(first).size() == 1);
    // ...until Escape: then keys act on what's under the mouse again.
    d.key(Qt::Key_Escape, "");
    d.move(d.atomPos(*d.mol().atom(first)) + QPointF(1, 0));
    d.key(Qt::Key_1, "1");
    CHECK(d.mol().neighbors(first).size() == 2);

    // A chain end: the mouse just above the atom picks the upper zig, just below the lower one.
    for (double side : {-1.0, 1.0}) {
        Document e;
        QString a = edits::addAtom(e, {5, 5});
        QString b = edits::bondToPoint(e, a, {6, 5});
        d.c.setDocument(e);
        d.move(d.c.modelToScreen(QPointF(6, 5 + 0.22 * side)));
        d.key(Qt::Key_1, "1");
        REQUIRE(d.mol().atoms.size() == 3);
        const Atom& tip = d.mol().atoms.back();
        CHECK(tip.y * side > 5 * side + 0.5);
        // Now moving the mouse off into empty space keeps the new atom hovered and steers the next
        // bond toward the mouse, even against the usual zig-zag.
        QPointF tipPos = tip.pos();
        QString tipId = tip.id;
        d.move(d.c.modelToScreen(tipPos + QPointF(-1.0, 1.6 * side)));
        d.key(Qt::Key_1, "1");
        REQUIRE(d.mol().atoms.size() == 4);
        const Atom& next = d.mol().atoms.back();
        CHECK(d.mol().bondBetween(tipId, next.id));
        CHECK(next.x < tipPos.x() - 0.3);
        CHECK(next.y * side > tipPos.y() * side + 0.5);
    }
}

TEST_CASE("continue reaction: arrow and an editable copy in the chosen direction") {
    testApp();
    Driver d;
    d.c.insertMolecules({*chem::fromSmiles("CCO")}, QPointF(5, 5));
    QString smi = chem::smiles(d.mol());
    QPointF c0 = d.mol().center();
    d.c.selectAll();
    d.c.command("reaction.continueRight");
    REQUIRE(d.c.doc().molecules.size() == 2);
    REQUIRE(d.c.doc().arrows.size() == 1);
    const Arrow& r = d.c.doc().arrows[0];
    const Molecule& copy = d.c.doc().molecules[1];
    CHECK(chem::smiles(copy) == smi);
    CHECK(r.x2 > r.x1);
    CHECK(std::abs(r.y1 - r.y2) < 1e-9);
    CHECK(std::abs(r.y1 - c0.y()) < 0.5);
    CHECK(d.mol(0).bounds().right() < r.x1);
    CHECK(copy.bounds().left() > r.x2);
    CHECK(std::abs(copy.center().y() - c0.y()) < 1e-6);
    // The copy is selected, so the next one carries on from it; down goes below.
    CHECK(d.c.selection() == QSet<QString>{copy.id});
    QString copyId = copy.id;
    d.c.command("reaction.continueDown");
    REQUIRE(d.c.doc().molecules.size() == 3);
    REQUIRE(d.c.doc().arrows.size() == 2);
    const Arrow& down = d.c.doc().arrows[1];
    CHECK(down.y2 > down.y1);
    CHECK(d.c.doc().molecule(copyId)->bounds().bottom() < down.y1);
    CHECK(d.c.doc().molecules[2].bounds().top() > down.y2);
    // One undo removes the step.
    d.c.undoStack()->undo();
    CHECK(d.c.doc().molecules.size() == 2);
    CHECK(d.c.doc().arrows.size() == 1);
}
TEST_CASE("Shift+C puts an acetyl on the hovered atom") {
    testApp();
    Driver d;
    Document doc;
    QString a = edits::addAtom(doc, {5, 5});
    QString b = edits::bondToPoint(doc, a, {6, 5});
    d.c.setDocument(doc);
    d.move(d.atomPos(*d.mol().atom(b)));
    d.key(Qt::Key_C, "C", Qt::ShiftModifier);
    CHECK(d.mol().atom(b)->label == "Ac");
}
TEST_CASE("continue reaction maps the copy and shows bond changes; the toggle turns it off") {
    testApp();
    Driver d;
    d.c.insertMolecules({*chem::fromSmiles("CC(=O)O")}, QPointF(5, 5));
    d.c.selectAll();
    d.c.command("reaction.continueRight");
    REQUIRE(d.c.doc().molecules.size() == 2);
    const Molecule& r = d.c.doc().molecules[0];
    const Molecule& p = d.c.doc().molecules[1];
    REQUIRE(r.atoms.size() == p.atoms.size());
    for (size_t i = 0; i < r.atoms.size(); ++i) {
        CHECK(r.atoms[i].map != 0);
        CHECK(r.atoms[i].map == p.atoms[i].map);
    }
    QString arrow = d.c.doc().arrows[0].id;
    CHECK(d.c.showsBondChanges(arrow));
    d.c.toggleBondChanges({arrow});
    CHECK_FALSE(d.c.showsBondChanges(arrow));
    d.c.toggleBondChanges();
    CHECK(d.c.showsBondChanges(arrow));
}
TEST_CASE("E expands and collapses labels; focus moves with arrow keys and starts by clicking a selected atom") {
    testApp();
    Driver d;
    Document doc;
    QString a = edits::addAtom(doc, {5, 5});
    QString b = edits::bondToPoint(doc, a, {6, 5});
    edits::setLabel(doc, b, "Ac");
    d.c.setDocument(doc);
    // E on the label expands it (acetyl: C, C, O), E on one of its atoms collapses it again.
    d.move(d.atomPos(*d.mol().atom(b)));
    d.key(Qt::Key_E, "e");
    REQUIRE(d.mol().atoms.size() == 4);
    CHECK(d.mol().atom(b)->label.isEmpty());
    d.key(Qt::Key_E, "e");
    CHECK(d.mol().atoms.size() == 2);
    CHECK(d.mol().atom(b)->label == "Ac");

    // Click the structure to select it, then click atom a: a becomes the focused atom.
    Document chain;
    QString c0 = edits::addAtom(chain, {5, 5});
    QString c1 = edits::bondToPoint(chain, c0, {5.866, 4.5});
    QString c2 = edits::bondToPoint(chain, c1, {6.732, 5});
    d.c.setDocument(chain);
    QPointF p0 = d.atomPos(*d.mol().atom(c0));
    d.move(p0);
    d.press(p0);
    d.release(p0);
    CHECK(d.c.focusedAtom().isEmpty());
    d.press(p0);
    d.release(p0);
    CHECK(d.c.focusedAtom() == c0);
    // Right arrow walks along the zig-zag; left comes back; nothing to the left of c0 keeps it there.
    d.key(Qt::Key_Right, "");
    CHECK(d.c.focusedAtom() == c1);
    d.key(Qt::Key_Right, "");
    CHECK(d.c.focusedAtom() == c2);
    d.key(Qt::Key_Left, "");
    d.key(Qt::Key_Left, "");
    d.key(Qt::Key_Left, "");
    CHECK(d.c.focusedAtom() == c0);
    // Keys act on the focused atom wherever the mouse is: 1 grows from c0, even with the mouse far away.
    d.move({20, 20});
    d.key(Qt::Key_1, "1");
    CHECK(d.mol().neighbors(c0).size() == 2);
}
TEST_CASE("preferences: importing styles and the default style") {
    testApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString src = tmp.filePath("labnotebook.style");
    {
        QFile f(src);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("style \"Lab notebook\"\n  bond length: 20 pt\n");
    }
    QString err;
    QString name = prefs::importStyle(src, false, &err);
    CHECK(name == "Lab notebook");
    const QString dest = QDir(styles::userStyleDir()).filePath("Lab notebook.style");  // saved under its declared name
    CHECK(QFile::exists(dest));
    CHECK(styles::availableNames().contains("Lab notebook"));
    // Importing it again asks first (reported as "exists"); replacing works when allowed.
    CHECK(prefs::importStyle(src, false, &err).isEmpty());
    CHECK(err == "exists");
    CHECK(prefs::importStyle(src, true, &err) == "Lab notebook");
    // Not a style at all.
    const QString junk = tmp.filePath("junk.style");
    { QFile f(junk); REQUIRE(f.open(QIODevice::WriteOnly)); f.write("hello"); }
    CHECK(prefs::importStyle(junk, false, &err).isEmpty());
    CHECK_FALSE(err.isEmpty());
    // The default style falls back to "default" if the chosen one has gone.
    QSettings().setValue(prefs::kDefaultStyle, "Lab notebook");
    CHECK(prefs::defaultStyle() == "Lab notebook");
    QFile::remove(dest);
    CHECK(prefs::defaultStyle() == "default");
    QSettings().remove(prefs::kDefaultStyle);

    // The dialog builds; with CHEMFOX_SNAPSHOT set, save a picture of each tab.
    PreferencesDialog dlg({});
    dlg.show();
    auto* tabs = dlg.findChild<QTabWidget*>();
    REQUIRE(tabs);
    CHECK(tabs->count() == 5);
    const QString snap = qEnvironmentVariable("CHEMFOX_SNAPSHOT");
    for (int i = 0; !snap.isEmpty() && i < tabs->count(); ++i) {
        tabs->setCurrentIndex(i);
        dlg.grab().save(QDir(snap).filePath(QString("prefs-%1.png").arg(i)));
    }
}
TEST_CASE("keymap: keys for an action, shortest first, follow rebinding") {
    Keymap k;
    CHECK(k.keysFor("any", "tool:select") == QStringList{"V"});
    CHECK(k.keysFor("any", "tool:arrow") == QStringList{"Shift+A"});
    Keymap::Map m = k.map();
    m["any"]["Shift+S"] = "tool:select";
    k.setMap(m);
    CHECK(k.keysFor("any", "tool:select") == QStringList{"V", "Shift+S"});
    CHECK(k.keysFor("any", "nothing").isEmpty());
}
TEST_CASE("grid snapping, duplicate, bond angle steps and preference helpers") {
    testApp();
    Driver d;
    d.c.insertSmiles("CCCC");
    REQUIRE(d.c.doc().molecules.size() == 1);

    // With snapping on, the dragged bond's first atom lands on a grid point.
    d.c.setGrid(true, true, 0.5);
    const Molecule& m = d.mol();
    const Bond b = m.bonds[1];
    d.drag(d.bondPos(m, b), d.bondPos(m, b) + QPointF(53, 31));
    const Atom* a = d.mol().atom(b.a);
    auto onGrid = [](double v) { return std::abs(v / 0.5 - std::round(v / 0.5)) < 1e-6; };
    CHECK(onGrid(a->x));
    CHECK(onGrid(a->y));
    d.c.setGrid(false, false, 0.5);

    // Duplicate the selection: a second copy, selected.
    d.move({5, 5});  // nothing hovered
    d.c.selectAll();
    d.c.duplicateSelection();
    CHECK(d.c.doc().molecules.size() == 2);
    CHECK(d.c.selection().size() == 1);
    d.c.restackSelection(false);  // the copy goes underneath
    CHECK(d.c.doc().molecules[0].id == *d.c.selection().begin());

    // Bond angle steps (Preferences → Drawing).
    geom::setDefaultSnapStep(M_PI / 12);
    CHECK(geom::snapAngle(0.3) == Catch::Approx(M_PI / 12));
    geom::setDefaultSnapStep(M_PI / 6);
    CHECK(geom::snapAngle(0.3) == Catch::Approx(M_PI / 6));

    // Export format falls back to SVG for anything unknown; the documents folder wins over the last folder.
    QSettings s;
    s.setValue(prefs::kExportFormat, "pdf");
    CHECK(prefs::exportFormat() == "pdf");
    s.setValue(prefs::kExportFormat, "bogus");
    CHECK(prefs::exportFormat() == "svg");
    s.remove(prefs::kExportFormat);
    QTemporaryDir tmp;
    s.setValue(prefs::kDocFolder, tmp.path());
    CHECK(prefs::startFolder() == tmp.path());
    s.remove(prefs::kDocFolder);
}
