#include "app/MainWindow.h"
#include "version.h"

#include "app/ReactionBuilder.h"
#include "app/Shortcuts.h"
#include "app/StyleEditor.h"

#include "core/chem/Chem.h"
#include "core/model/Labels.h"
#include "editor/Canvas.h"
#include "editor/Templates.h"
#include "io/Emf.h"
#include "io/Export.h"
#include "io/Native.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTextBrowser>
#include <QActionGroup>
#include <QPainter>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace cd {

namespace {

QString richFormula(const QString& f) {
    // Trailing charge ("2-") becomes a superscript; other digits are subscripts.
    static const QRegularExpression chargeRe("(\\d*[+-])$");
    QString body = f, charge;
    if (auto m = chargeRe.match(f); m.hasMatch()) {
        charge = m.captured(1);
        body = f.left(m.capturedStart());
    }
    QString out;
    for (const auto& r : labels::hillRuns(body)) {
        if (r.script == Script::Sub) out += "<sub>" + r.text.toHtmlEscaped() + "</sub>";
        else out += r.text.toHtmlEscaped();
    }
    if (!charge.isEmpty()) out += "<sup>" + charge.toHtmlEscaped() + "</sup>";
    return out;
}

// Simple line icons drawn in the palette's text colour, so they suit light and dark themes.
QIcon toolIcon(const QString& kind, const QColor& ink) {
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(ink, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    auto head = [&](QPointF tip, QPointF dir) {
        QPointF n(-dir.y(), dir.x());
        QPolygonF h;
        h << tip << tip - dir * 10 + n * 6 << tip - dir * 10 - n * 6;
        p.setBrush(ink);
        p.drawPolygon(h);
        p.setBrush(Qt::NoBrush);
    };
    if (kind == "select") {
        QPen d = pen;
        d.setDashPattern({2, 2});
        p.setPen(d);
        p.drawRect(QRectF(8, 8, 32, 32));
    } else if (kind == "chain") {
        p.drawPolyline(QPolygonF({QPointF(6, 32), QPointF(17, 18), QPointF(29, 32), QPointF(41, 18)}));
    } else if (kind == "arrow") {
        p.drawLine(QPointF(6, 24), QPointF(34, 24));
        head(QPointF(43, 24), QPointF(1, 0));
    } else if (kind == "bent") {
        p.drawPolyline(QPolygonF({QPointF(8, 8), QPointF(8, 30), QPointF(34, 30)}));
        head(QPointF(43, 30), QPointF(1, 0));
    } else if (kind == "text") {
        QFont f("Arial");
        f.setPixelSize(34);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRectF(0, 0, 48, 48), Qt::AlignCenter, "T");
    } else if (kind == "map") {
        p.setBrush(ink);
        p.drawEllipse(QPointF(10, 34), 5, 5);
        p.drawEllipse(QPointF(38, 14), 5, 5);
        p.setBrush(Qt::NoBrush);
        QPen d = pen;
        d.setDashPattern({2, 2});
        p.setPen(d);
        p.drawLine(QPointF(14, 31), QPointF(34, 17));
    }
    return QIcon(pm);
}

}  // namespace

MainWindow::MainWindow() {
    canvas_ = new Canvas(this);
    setCentralWidget(canvas_);
    resize(1280, 820);

    buildMenus();
    buildDocks();
    for (QAction* top : menuBar()->actions())
        if (top->menu()) shortcuts_.addMenu(top->menu());
    shortcuts_.load();

    connect(canvas_, &Canvas::status, this, [this](const QString& s) { statusBar()->showMessage(s); });
    connect(canvas_, &Canvas::infoTargetChanged, this, &MainWindow::updateInfo);
    connect(canvas_, &Canvas::documentChanged, this, &MainWindow::updateInfo);
    connect(canvas_, &Canvas::documentChanged, this, &MainWindow::syncStyleUi);
    connect(canvas_, &Canvas::warningsChanged, this, &MainWindow::updateWarnings);
    connect(canvas_->undoStack(), &QUndoStack::cleanChanged, this, &MainWindow::updateTitle);


    QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
    restoreState(s.value("window/state").toByteArray());

    updateTitle();
    updateInfo();
    updateWarnings();
    syncStyleUi();
    canvas_->setFocus();
    statusBar()->showMessage("Drag to select; pick Chain or Arrow on the toolbar to draw. Hover an atom or bond and type to change it.");
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu("&File");
    file->addAction("&New", QKeySequence::New, this, &MainWindow::newDocument);
    file->addAction("&Open…", QKeySequence::Open, this, &MainWindow::open);
    file->addAction("&Save", QKeySequence::Save, this, &MainWindow::save);
    file->addAction("Save &As…", QKeySequence::SaveAs, this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction("&Import Structure (MOL, SDF, SMILES, InChI)…", QKeySequence("Ctrl+I"), this, &MainWindow::importStructure);
    auto* exp = file->addMenu("&Export");
    exp->addAction("SVG (reopenable)…", QKeySequence("Ctrl+E"), this, [this] { exportAs("svg"); });
    exp->addAction("PDF…", this, [this] { exportAs("pdf"); });
    exp->addAction("PNG…", this, [this] { exportAs("png"); });
    if (io::emfSupported()) exp->addAction("EMF (Word, PowerPoint)…", this, [this] { exportAs("emf"); });
    exp->addAction("MOL / SDF…", this, [this] { exportAs("mol"); });
    file->addSeparator();
    file->addAction("E&xit", QKeySequence::Quit, this, &QWidget::close);

    auto* edit = menuBar()->addMenu("&Edit");
    QAction* undo = canvas_->undoStack()->createUndoAction(this, "&Undo");
    undo->setObjectName("edit.undo");  // its text changes ("Undo Add bond"), so fix the id
    undo->setShortcuts({QKeySequence::Undo});
    QAction* redo = canvas_->undoStack()->createRedoAction(this, "&Redo");
    redo->setObjectName("edit.redo");
    redo->setShortcuts({QKeySequence("Ctrl+Shift+Z"), QKeySequence("Ctrl+Y")});
    edit->addAction(undo);
    edit->addAction(redo);
    edit->addSeparator();
    edit->addAction("Cu&t", QKeySequence::Cut, canvas_, &Canvas::cut);
    edit->addAction("&Copy", QKeySequence::Copy, canvas_, &Canvas::copy);
    edit->addAction("&Paste", QKeySequence::Paste, canvas_, &Canvas::paste);
    edit->addAction("Paste SMILES or InChI…", QKeySequence("Ctrl+Shift+V"), this, &MainWindow::pasteSmilesDialog);
    edit->addAction("Select &All", QKeySequence::SelectAll, canvas_, &Canvas::selectAll);
    auto* copyAs = edit->addMenu("Copy &As");
    // Commands that act on the hovered structure or arrow, else the selection.
    auto cmd = [this](QMenu* m, const QString& text, const QString& id) {
        QAction* a = m->addAction(text, this, [this, id] { canvas_->command(id); });
        a->setObjectName(id);
        return a;
    };
    cmd(copyAs, "SMILES", "copy.smiles");
    cmd(copyAs, "InChI", "copy.inchi");
    cmd(copyAs, "InChIKey", "copy.inchikey");
    cmd(copyAs, "MOL Block", "copy.mol");
    cmd(copyAs, "Image (PNG + SVG)", "copy.image");
    edit->addSeparator();
    edit->addAction("Silenced Warnings…", this, &MainWindow::showSilenced);
    edit->addAction("&Keyboard Shortcuts…", QKeySequence("Ctrl+Alt+K"), this, [this] {
        ShortcutsDialog dlg(shortcuts_, canvas_, this);
        dlg.exec();
    });

    auto* structure = menuBar()->addMenu("&Structure");
    structure->addAction("&Clean Structure", QKeySequence("Ctrl+L"), canvas_, &Canvas::cleanStructure);
    auto* transform = structure->addMenu("&Transform");
    cmd(transform, "Flip Horizontal", "structure.flipH");
    cmd(transform, "Flip Vertical", "structure.flipV");
    cmd(transform, "Rotate 90° Clockwise", "structure.rotateCW");
    cmd(transform, "Rotate 90° Counter-clockwise", "structure.rotateCCW");
    cmd(transform, "Rotate 180°", "structure.rotate180");
    cmd(structure, "Align to Reactant / Product", "structure.align");
    structure->addSeparator();
    cmd(structure, "Show Stereo Labels (R/S, E/Z)", "structure.toggleStereo");
    cmd(structure, "Show All Carbons", "structure.toggleCarbons");
    cmd(structure, "Show Labels as Structures", "structure.expandLabels");
    cmd(structure, "Collapse Structures to Labels", "structure.collapseLabels");
    structure->addSeparator();
    for (const auto& group : templates::all()) {
        auto* sub = structure->addMenu(group.name);
        for (const auto& t : group.items) {
            QString smi = t.smiles;
            sub->addAction(t.name, this, [this, smi] { canvas_->insertSmiles(smi); });
        }
    }

    auto* rxn = menuBar()->addMenu("&Reaction");
    reactionMenu_ = rxn;
    rxn->addAction("&Arrange Scheme", QKeySequence("Ctrl+Shift+A"), canvas_, &Canvas::arrangeScheme);
    rxn->addAction("A&lign Products to Reactants", QKeySequence("Ctrl+Shift+L"), canvas_, &Canvas::alignAllProducts)
        ->setStatusTip("Redraw each product so the part it shares with its reactant matches (selection only, if any)");
    rxn->addAction("&Number Compounds", QKeySequence("Ctrl+Shift+N"), canvas_, &Canvas::numberCompounds);
    QAction* mapMode = rxn->addAction("Edit &Mapping");
    mapMode->setCheckable(true);
    mapMode->setShortcut(QKeySequence("Ctrl+M"));
    mapMode->setToolTip("Drag from a reactant atom to a product atom to map them");
    connect(mapMode, &QAction::toggled, canvas_, &Canvas::setMapMode);
    connect(canvas_, &Canvas::mapModeChanged, mapMode, &QAction::setChecked);
    mapModeAction_ = mapMode;
    auto* arrowMenu = rxn->addMenu("A&rrow");
    cmd(arrowMenu, "Swap Above / Below Text", "arrow.swapText");
    cmd(arrowMenu, "Reverse Direction", "arrow.reverse");
    cmd(arrowMenu, "Straighten (Remove Corners)", "arrow.straighten");
    cmd(arrowMenu, "Next Head Style", "arrow.nextHead");
    cmd(arrowMenu, "Heads at Both Ends", "arrow.bothHeads");
    cmd(arrowMenu, "Next Line Style", "arrow.nextLine");
    stepLetters_ = rxn->addAction("Step &Letters");
    stepLetters_->setCheckable(true);
    connect(stepLetters_, &QAction::toggled, canvas_, &Canvas::setStepLetters);

    auto* view = menuBar()->addMenu("&View");
    view->addAction("Zoom &In", QKeySequence::ZoomIn, this, [this] { canvas_->zoomBy(1.25); });
    view->addAction("Zoom &Out", QKeySequence::ZoomOut, this, [this] { canvas_->zoomBy(0.8); });
    view->addAction("Zoom to &Fit", QKeySequence("Ctrl+0"), canvas_, &Canvas::zoomToFit);
    view->addAction("Actual &Size", QKeySequence("Ctrl+1"), canvas_, &Canvas::resetZoom);
    QAction* guide = view->addAction("&Page Width Guide");
    guide->setCheckable(true);
    guide->setToolTip("Show the style's page width (the row width Arrange Scheme wraps at), measured from the origin");
    connect(guide, &QAction::toggled, canvas_, &Canvas::setShowPageGuide);
    view->addSeparator();
    viewMenuDocks_ = view;

    auto* help = menuBar()->addMenu("&Help");
    help->addAction("&Keyboard Reference", QKeySequence::HelpContents, this, &MainWindow::showKeyReference);
    const QString repo = QString(CHEMFOX_REPO_URL);
    auto openRepo = [this, repo](const QString& page, const QString& what) {
        if (repo.isEmpty()) {
            QMessageBox::information(this, what, "This build doesn't know its GitHub page. Please report problems to "
                                                 "whoever gave you ChemFox, with your version (Help → About).");
            return;
        }
        QDesktopServices::openUrl(QUrl(repo + page));
    };
    help->addSeparator();
    help->addAction("&Report a Problem…", this, [openRepo] { openRepo("/issues/new/choose", "Report a Problem"); })
        ->setStatusTip("Open a bug report or feature request on GitHub");
    help->addAction("Releases && &Updates…", this, [openRepo] { openRepo("/releases", "Releases"); })
        ->setStatusTip("See whether a newer version is out");
    help->addAction("Third-Party &Licences", this, [this] {
        // Installed builds carry them next to the app (inside the bundle's Resources on macOS).
        QString dir = QCoreApplication::applicationDirPath() + "/licenses";
        if (!QDir(dir).exists()) dir = QCoreApplication::applicationDirPath() + "/../Resources/licenses";
        if (QDir(dir).exists()) QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        else QMessageBox::information(this, "Third-Party Licences", "See the NOTICE file in the ChemFox source code.");
    });
    help->addAction("&About ChemFox", this, [this, repo] {
        QMessageBox::about(this, "About ChemFox",
                           QString("<h3>ChemFox %1</h3>"
                                   "<p>A keyboard-first editor for chemical structures and reaction schemes.</p>"
                                   "<p>Apache License 2.0.%2</p>"
                                   "<p>Chemistry by <a href='https://www.rdkit.org'>RDKit</a>; interface by "
                                   "<a href='https://www.qt.io'>Qt</a> (LGPL v3). See Help → Third-Party Licences.</p>")
                               .arg(CHEMFOX_VERSION, repo.isEmpty() ? QString() : QString(" <a href='%1'>%1</a>").arg(repo)));
    });

    // Tools: what a drag on empty canvas does. Exclusive, with Select as the default.
    auto* tools = new QToolBar("Tools", this);
    tools->setObjectName("toolsToolbar");
    tools->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tools->setIconSize(QSize(24, 24));
    tools->setMovable(false);
    addToolBar(Qt::LeftToolBarArea, tools);
    auto* toolMenu = new QMenu("&Tools", this);
    menuBar()->insertMenu(help->menuAction(), toolMenu);
    auto* group = new QActionGroup(this);
    const QColor ink = palette().color(QPalette::WindowText);
    struct ToolDef { const char* label; const char* icon; Canvas::Tool tool; const char* tip; };
    const ToolDef defs[] = {
        {"Select", "select", Canvas::Tool::Select, "Drag on empty canvas to select (box)"},
        {"Chain", "chain", Canvas::Tool::Chain, "Drag on empty canvas to draw a zig-zag chain"},
        {"Arrow", "arrow", Canvas::Tool::Arrow, "Drag to draw a reaction arrow (15° steps; Alt: free)"},
        {"Bent", "bent", Canvas::Tool::BentArrow,
         "Click each corner of a bent arrow; double-click or Enter to finish, Esc to cancel"},
        {"Text", "text", Canvas::Tool::Text, "Click to add text (**bold**, *italic*)"},
    };
    QList<QAction*> toolActions;
    for (const auto& t : defs) {
        QAction* a = tools->addAction(toolIcon(t.icon, ink), t.label);
        a->setCheckable(true);
        a->setToolTip(QString("%1 — %2").arg(t.label, t.tip));
        a->setStatusTip(t.tip);
        group->addAction(a);
        toolMenu->addAction(a);
        Canvas::Tool tool = t.tool;
        connect(a, &QAction::triggered, canvas_, [this, tool] { canvas_->setTool(tool); });
        toolActions << a;
    }
    toolActions.front()->setChecked(true);
    connect(canvas_, &Canvas::toolChanged, this, [toolActions](int t) { toolActions[t]->setChecked(true); });
    tools->addSeparator();
    toolMenu->addSeparator();
    mapModeAction_->setIcon(toolIcon("map", ink));
    mapModeAction_->setIconText("Map");
    tools->addAction(mapModeAction_);
    toolMenu->addAction(mapModeAction_);

    auto* tb = addToolBar("Main");
    toolbar_ = tb;
    tb->setObjectName("mainToolbar");
    tb->setMovable(false);
    tb->addAction(undo);
    tb->addAction(redo);
    tb->addSeparator();
    tb->addAction("Clean", canvas_, &Canvas::cleanStructure)->setToolTip("Clean structure (Ctrl+L)");
    tb->addAction("Arrange scheme", canvas_, &Canvas::arrangeScheme)->setToolTip("Lay out the reaction scheme (Ctrl+Shift+A)");
    tb->addAction("Number", canvas_, &Canvas::numberCompounds)->setToolTip("Number compounds (Ctrl+Shift+N)");
    tb->addSeparator();
    tb->addWidget(new QLabel(" Style: "));
    styleBox_ = new QComboBox;
    styleBox_->setMinimumWidth(110);
    tb->addWidget(styleBox_);
    connect(styleBox_, &QComboBox::activated, this, [this](int i) {
        if (syncingStyle_) return;
        canvas_->setStyleName(styleBox_->itemText(i));
    });
}

void MainWindow::buildDocks() {
    // Properties
    info_ = new QTextBrowser;
    info_->setOpenLinks(false);
    info_->setFrameShape(QFrame::NoFrame);
    auto* infoDock = new QDockWidget("Properties", this);
    infoDock->setObjectName("propertiesDock");
    infoDock->setWidget(info_);
    addDockWidget(Qt::RightDockWidgetArea, infoDock);

    // Warnings
    auto* wWidget = new QWidget;
    auto* wLay = new QVBoxLayout(wWidget);
    wLay->setContentsMargins(4, 4, 4, 4);
    warnTree_ = new QTreeWidget;
    warnTree_->setHeaderHidden(true);
    warnTree_->setRootIsDecorated(true);
    warnTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    wLay->addWidget(warnTree_);
    auto* bLay = new QHBoxLayout;
    auto* goBtn = new QPushButton("Go to");
    auto* silInst = new QPushButton("Silence");
    auto* silDoc = new QPushButton("Silence code here");
    auto* silAll = new QPushButton("…everywhere");
    goBtn->setToolTip("Select and centre the object");
    silInst->setToolTip("Silence this instance");
    silDoc->setToolTip("Silence this code in this document");
    silAll->setToolTip("Silence this code in every document");
    for (auto* b : {goBtn, silInst, silDoc, silAll}) bLay->addWidget(b);
    wLay->addLayout(bLay);
    auto* warnDock = new QDockWidget("Warnings", this);
    warnDock->setObjectName("warningsDock");
    warnDock->setWidget(wWidget);
    addDockWidget(Qt::RightDockWidgetArea, warnDock);

    auto act = [this](const QString& scope) {
        const Warning* w = currentWarning();
        if (!w) return;
        Warning copy = *w;
        if (scope == "goto") canvas_->goTo(copy);
        else canvas_->silence(copy, scope);
    };
    connect(goBtn, &QPushButton::clicked, this, [act] { act("goto"); });
    connect(silInst, &QPushButton::clicked, this, [act] { act("instance"); });
    connect(silDoc, &QPushButton::clicked, this, [act] { act("document"); });
    connect(silAll, &QPushButton::clicked, this, [act] { act("global"); });
    connect(warnTree_, &QTreeWidget::itemDoubleClicked, this, [act] { act("goto"); });
    connect(warnTree_, &QTreeWidget::customContextMenuRequested, this, [this, act](const QPoint& p) {
        if (!currentWarning()) return;
        QMenu m;
        m.addAction("Go to", [act] { act("goto"); });
        m.addAction("Silence this instance", [act] { act("instance"); });
        m.addAction("Silence this code in this document", [act] { act("document"); });
        m.addAction("Silence this code everywhere", [act] { act("global"); });
        m.exec(warnTree_->viewport()->mapToGlobal(p));
    });

    // Style editor: tabs of standard controls plus a Source tab.
    auto* styleDock = new QDockWidget("Style", this);
    styleDock->setObjectName("styleDock");
    styleDock->setWidget(new StyleEditor(canvas_));
    addDockWidget(Qt::RightDockWidgetArea, styleDock);
    tabifyDockWidget(warnDock, styleDock);
    warnDock->raise();
    // Reaction builder
    auto* builder = new ReactionBuilder(canvas_);
    auto* builderDock = new QDockWidget("Reaction Builder", this);
    builderDock->setObjectName("reactionBuilderDock");
    builderDock->setWidget(builder);
    addDockWidget(Qt::LeftDockWidgetArea, builderDock);
    builderDock->hide();
    QAction* toggleBuilder = builderDock->toggleViewAction();
    toggleBuilder->setText("Reaction &Builder");
    toggleBuilder->setShortcut(QKeySequence("Ctrl+Shift+R"));
    reactionMenu_->addSeparator();
    reactionMenu_->addAction(toggleBuilder);
    toolbar_->addSeparator();
    toolbar_->addAction(toggleBuilder);
    connect(toggleBuilder, &QAction::toggled, builder, [builder](bool on) {
        if (on) builder->setFocus();
    });
    connect(canvas_, &Canvas::addToReaction, this, [builder, builderDock](const QString& smiles, int role) {
        builderDock->show();
        builderDock->raise();
        builder->addStructure(smiles, role);
    });

    viewMenuDocks_->addAction(infoDock->toggleViewAction());
    viewMenuDocks_->addAction(warnDock->toggleViewAction());
    viewMenuDocks_->addAction(styleDock->toggleViewAction());
}

// ---------------------------------------------------------------------------

void MainWindow::updateTitle() {
    QString name = path_.isEmpty() ? "Untitled" : QFileInfo(path_).fileName();
    setWindowTitle(QString("%1%2 — ChemFox").arg(name, canvas_->undoStack()->isClean() ? "" : " •"));
}

void MainWindow::updateInfo() {
    const Molecule* m = canvas_->infoMolecule();
    QString html = "<style>td{padding:2px 8px 2px 0;} .k{color:gray;} .mono{font-family:Consolas,monospace;}</style>";
    if (!m) {
        const Document& d = canvas_->doc();
        html += "<p style='color:gray'>Hover or select a structure to see its formula and mass.</p>";
        html += QString("<table><tr><td class=k>Structures</td><td>%1</td></tr><tr><td class=k>Arrows</td><td>%2</td></tr>"
                        "<tr><td class=k>Style</td><td>%3</td></tr></table>")
                    .arg(d.molecules.size()).arg(d.arrows.size()).arg(canvas_->style().name.toHtmlEscaped());
        info_->setHtml(html);
        return;
    }
    chem::MolInfo mi = canvas_->info(*m);
    QString title = m->number.isEmpty() ? "Structure" : "Compound <b>" + m->number.toHtmlEscaped() + "</b>";
    html += "<p>" + title + "</p><table>";
    if (mi.ok) {
        html += "<tr><td class=k>Formula</td><td>" + richFormula(mi.formula) + "</td></tr>";
        html += QString("<tr><td class=k>MW</td><td>%1 g/mol</td></tr>").arg(mi.mw, 0, 'f', 2);
        html += QString("<tr><td class=k>Exact mass</td><td>%1</td></tr>").arg(mi.exactMass, 0, 'f', 4);
        html += "</table><p class=k>SMILES</p><p class=mono>" + mi.smiles.toHtmlEscaped() + "</p>";
        if (!mi.inchi.isEmpty()) {
            html += "<p class=k>InChI</p><p class=mono>" + mi.inchi.toHtmlEscaped() + "</p>";
            html += "<p class=k>InChIKey</p><p class=mono>" + mi.inchiKey.toHtmlEscaped() + "</p>";
        }
    } else {
        html += "</table><p style='color:#b00'>Could not compute: " + mi.error.toHtmlEscaped() + "</p>";
    }
    info_->setHtml(html);
}

void MainWindow::updateWarnings() {
    QString selKey;
    if (auto* cur = currentWarning()) selKey = cur->key();
    warnTree_->clear();
    const auto& ws = canvas_->warnings();
    QMap<QString, QTreeWidgetItem*> groups;
    for (int i = 0; i < (int)ws.size(); ++i) {
        const auto& w = ws[i];
        if (!groups.contains(w.code)) {
            auto* g = new QTreeWidgetItem(warnTree_);
            g->setFirstColumnSpanned(true);
            groups[w.code] = g;
        }
        auto* it = new QTreeWidgetItem(groups[w.code]);
        it->setText(0, w.message);
        it->setData(0, Qt::UserRole, i);
        if (w.key() == selKey) warnTree_->setCurrentItem(it);
    }
    for (auto g = groups.begin(); g != groups.end(); ++g) {
        g.value()->setText(0, QString("%1  %2 (%3)").arg(g.key(), warnings::title(g.key())).arg(g.value()->childCount()));
        QFont f = g.value()->font(0);
        f.setBold(true);
        g.value()->setFont(0, f);
        g.value()->setExpanded(true);
    }
    if (ws.empty()) {
        auto* it = new QTreeWidgetItem(warnTree_);
        it->setText(0, "No warnings");
        it->setForeground(0, Qt::gray);
    }
    if (auto* dock = findChild<QDockWidget*>("warningsDock"))
        dock->setWindowTitle(ws.empty() ? "Warnings" : QString("Warnings (%1)").arg(ws.size()));
}

const Warning* MainWindow::currentWarning() const {
    auto* it = warnTree_->currentItem();
    if (!it) return nullptr;
    QVariant v = it->data(0, Qt::UserRole);
    if (!v.isValid()) return nullptr;
    int i = v.toInt();
    const auto& ws = canvas_->warnings();
    return i >= 0 && i < (int)ws.size() ? &ws[i] : nullptr;
}

void MainWindow::syncStyleUi() {
    syncingStyle_ = true;
    const Document& d = canvas_->doc();
    QStringList names = styles::availableNames();
    if (!names.contains(d.styleName)) names << d.styleName;
    styleBox_->clear();
    styleBox_->addItems(names);
    styleBox_->setCurrentText(d.styleName);
    stepLetters_->setChecked(d.stepLetters);
    syncingStyle_ = false;
}

// ---------------------------------------------------------------------------
// Files

bool MainWindow::maybeSave() {
    if (canvas_->undoStack()->isClean()) return true;
    auto r = QMessageBox::question(this, "ChemFox", "Save changes to this document?",
                                   QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save) return save();
    return r == QMessageBox::Discard;
}

void MainWindow::newDocument() {
    if (!maybeSave()) return;
    path_.clear();
    canvas_->setDocument(Document{});
    canvas_->resetZoom();
    updateTitle();
    syncStyleUi();
}

bool MainWindow::openPath(const QString& path) {
    QString lower = path.toLower();
    QString err;
    if (lower.endsWith(".mol") || lower.endsWith(".sdf") || lower.endsWith(".smi") || lower.endsWith(".inchi") ||
        lower.endsWith(".txt")) {
        auto mols = io::importFile(path, &err);
        if (mols.empty()) {
            QMessageBox::warning(this, "Open", "Could not read structures: " + err);
            return false;
        }
        Document d;
        canvas_->setDocument(d);
        canvas_->insertMolecules(mols);
        canvas_->undoStack()->clear();
        canvas_->zoomToFit();
        path_.clear();
        updateTitle();
        return true;
    }
    auto d = io::loadFile(path, &err);
    if (!d) {
        QMessageBox::warning(this, "Open", "Could not open " + QFileInfo(path).fileName() + ":\n" + err);
        return false;
    }
    canvas_->setDocument(*d);
    path_ = lower.endsWith(".svg") ? QString() : path;
    updateTitle();
    syncStyleUi();
    return true;
}

void MainWindow::open() {
    if (!maybeSave()) return;
    QSettings s;
    QString path = QFileDialog::getOpenFileName(
        this, "Open", s.value("lastDir").toString(),
        "All supported (*.chemfox *.chemdoc *.json *.svg *.mol *.sdf *.smi);;ChemFox (*.chemfox *.chemdoc *.json);;"
        "SVG exported by ChemFox (*.svg);;Structures (*.mol *.sdf *.smi)");
    if (path.isEmpty()) return;
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    openPath(path);
}

bool MainWindow::save() {
    if (path_.isEmpty()) return saveAs();
    QString err;
    if (!io::saveFile(path_, canvas_->doc(), &err)) {
        QMessageBox::warning(this, "Save", err);
        return false;
    }
    canvas_->undoStack()->setClean();
    statusBar()->showMessage("Saved " + path_);
    return true;
}

bool MainWindow::saveAs() {
    QSettings s;
    QString path = QFileDialog::getSaveFileName(this, "Save As", s.value("lastDir").toString() + "/untitled.chemfox",
                                                "ChemFox (*.chemfox)");
    if (path.isEmpty()) return false;
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    path_ = path;
    bool ok = save();
    updateTitle();
    return ok;
}

void MainWindow::importStructure() {
    QSettings s;
    QString path = QFileDialog::getOpenFileName(this, "Import Structure", s.value("lastDir").toString(),
                                                "Structures (*.mol *.sdf *.smi *.inchi *.txt)");
    if (path.isEmpty()) return;
    QString err;
    auto mols = io::importFile(path, &err);
    if (mols.empty()) QMessageBox::warning(this, "Import", "Could not read structures: " + err);
    else canvas_->insertMolecules(mols);
}

void MainWindow::pasteSmilesDialog() {
    bool ok = false;
    QString text = QInputDialog::getMultiLineText(this, "Insert structure", "SMILES, InChI or MOL block:", {}, &ok);
    if (!ok || text.trimmed().isEmpty()) return;
    QString err;
    auto mols = chem::fromText(text, &err);
    if (mols.empty()) QMessageBox::warning(this, "Insert structure", "Could not read that structure. " + err);
    else canvas_->insertMolecules(mols);
}

void MainWindow::exportAs(const QString& kind) {
    Document d = io::subset(canvas_->doc(), canvas_->selection());
    if (d.empty()) {
        QMessageBox::information(this, "Export", "Nothing to export yet.");
        return;
    }
    QSettings s;
    QString base = path_.isEmpty() ? "figure" : QFileInfo(path_).completeBaseName();
    QString filter = kind == "svg"   ? "SVG (*.svg)"
                     : kind == "pdf" ? "PDF (*.pdf)"
                     : kind == "png" ? "PNG (*.png)"
                     : kind == "emf" ? "Enhanced Metafile (*.emf)"
                                     : "MOL / SDF (*.mol *.sdf)";
    QString ext = kind == "mol" ? (d.molecules.size() > 1 ? "sdf" : "mol") : kind;
    QString path = QFileDialog::getSaveFileName(this, "Export", s.value("lastDir").toString() + "/" + base + "." + ext, filter);
    if (path.isEmpty()) return;
    s.setValue("lastDir", QFileInfo(path).absolutePath());
    QString err;
    bool ok = false;
    const auto& r = canvas_->renderer();
    const auto& ov = canvas_->overlay();
    if (kind == "svg") ok = io::exportSvg(path, d, r, ov, &err);
    else if (kind == "pdf") ok = io::exportPdf(path, d, r, ov, &err);
    else if (kind == "mol") ok = io::exportMol(path, d, &err);
    else if (kind == "emf") ok = io::exportEmf(path, d, r, ov, &err);
    else {
        bool got = false;
        int dpi = QInputDialog::getInt(this, "PNG resolution", "Dots per inch:", s.value("pngDpi", 300).toInt(), 72, 2400, 50, &got);
        if (!got) return;
        s.setValue("pngDpi", dpi);
        ok = io::exportPng(path, d, r, ov, dpi, false, &err);
    }
    if (!ok) QMessageBox::warning(this, "Export", err.isEmpty() ? "Export failed." : err);
    else statusBar()->showMessage("Exported " + path + (canvas_->selection().isEmpty() ? "" : " (selection)"));
}

void MainWindow::showSilenced() {
    QDialog dlg(this);
    dlg.setWindowTitle("Silenced Warnings");
    auto* lay = new QVBoxLayout(&dlg);
    auto* list = new QListWidget;
    lay->addWidget(new QLabel("Everything silenced in this document or in every document. Select an entry to restore it."));
    lay->addWidget(list);
    auto refill = [&] {
        list->clear();
        const Document& d = canvas_->doc();
        for (const auto& c : d.suppressedCodes) {
            auto* it = new QListWidgetItem(QString("%1 %2 — this document").arg(c, warnings::title(c)), list);
            it->setData(Qt::UserRole, "doc:" + c);
        }
        for (const auto& k : d.suppressedInstances) {
            auto* it = new QListWidgetItem(QString("%1 %2 — one instance (%3)").arg(k.section('@', 0, 0),
                                               warnings::title(k.section('@', 0, 0)), k.section('@', 1)), list);
            it->setData(Qt::UserRole, "inst:" + k);
        }
        for (const auto& c : warnings::globalSilenced()) {
            auto* it = new QListWidgetItem(QString("%1 %2 — every document").arg(c, warnings::title(c)), list);
            it->setData(Qt::UserRole, "global:" + c);
        }
        if (list->count() == 0) new QListWidgetItem("Nothing is silenced.", list);
    };
    refill();
    auto* restore = new QPushButton("Restore");
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    auto* row = new QHBoxLayout;
    row->addWidget(restore);
    row->addStretch();
    row->addWidget(close);
    lay->addLayout(row);
    connect(close, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(restore, &QPushButton::clicked, &dlg, [&] {
        auto* it = list->currentItem();
        if (!it) return;
        QString v = it->data(Qt::UserRole).toString();
        QString kind = v.section(':', 0, 0), val = v.section(':', 1);
        if (kind == "doc") canvas_->edit("Restore " + val, [&](Document& d) { d.suppressedCodes.remove(val); });
        else if (kind == "inst") canvas_->edit("Restore warning", [&](Document& d) { d.suppressedInstances.remove(val); });
        else if (kind == "global") {
            auto g = warnings::globalSilenced();
            g.remove(val);
            warnings::setGlobalSilenced(g);
            canvas_->silence(Warning{}, "reload");
        }
        refill();
    });
    dlg.resize(520, 340);
    dlg.exec();
}

void MainWindow::showKeyReference() {
    QString html = R"(
<h3>Hover and type</h3>
<p>A key acts on whatever is under the cursor. Nothing hovered → acts on the selection. Empty canvas → creates something.</p>
<table cellpadding=3>
<tr><th align=left>Target</th><th align=left>Key</th><th align=left>Action</th></tr>
<tr><td>Atom</td><td>C N O S P F H</td><td>Change element</td></tr>
<tr><td>Atom</td><td>l / b / i</td><td>Cl / Br / I</td></tr>
<tr><td>Atom</td><td>Enter</td><td>Type a label (OMe, CO2Et, NHBoc)</td></tr>
<tr><td>Atom</td><td>+ / −</td><td>Increase / decrease charge</td></tr>
<tr><td>Atom</td><td>.</td><td>Radical</td></tr>
<tr><td>Atom</td><td>1 / 2 / 3</td><td>Grow a single / double / triple bond</td></tr>
<tr><td>Atom</td><td>Shift+3…8</td><td>Attach a ring of that size</td></tr>
<tr><td>Atom / bond</td><td>r</td><td>Attach / fuse a benzene ring</td></tr>
<tr><td>Atom / bond</td><td>Shift+N</td><td>Compound number and caption</td></tr>
<tr><td>Bond</td><td>1 / 2 / 3</td><td>Set bond order</td></tr>
<tr><td>Bond</td><td>w / h / y</td><td>Wedge / hash / wavy (again: reverse)</td></tr>
<tr><td>Bond</td><td>f</td><td>Flip wedge direction or double-bond side</td></tr>
<tr><td>Bond</td><td>Shift+3…8</td><td>Fuse a ring of that size</td></tr>
<tr><td>Arrow</td><td>Enter / k</td><td>Edit conditions / cycle arrow kind</td></tr>
<tr><td>Empty</td><td>C N O … / Shift+3…8 / r</td><td>New atom / ring / benzene at cursor</td></tr>
<tr><td>Empty</td><td>a / t</td><td>New arrow / text</td></tr>
<tr><td>Any</td><td>Delete</td><td>Remove hovered object</td></tr>
<tr><td>Any</td><td>Tab</td><td>Cycle between overlapping hover targets</td></tr>
<tr><td>Global</td><td>Ctrl+Z / Ctrl+Shift+Z</td><td>Undo / redo</td></tr>
<tr><td>Global</td><td>Ctrl+L</td><td>Clean structure (RDKit 2D layout)</td></tr>
<tr><td>Global</td><td>Ctrl+Shift+A / Ctrl+Shift+N</td><td>Arrange scheme / number compounds</td></tr>
<tr><td>Global</td><td>Ctrl+Shift+R</td><td>Reaction builder (SMILES / SMARTS)</td></tr>
<tr><td>Global</td><td>Ctrl+M</td><td>Mapping mode: drag reactant atom → product atom; drag to empty space unmaps</td></tr>
<tr><td>Global</td><td>Arrow keys (Shift: ×10)</td><td>Nudge selection</td></tr>
</table>
<h3>Mouse</h3>
<p>Drag from an atom → new bond (30° steps, fixed length); keep dragging → chain.<br>
Drag on empty canvas → zig-zag chain. Hold Alt → no snapping.<br>
Click → select molecule (Shift adds, Alt: single atom). Shift+drag → box select.<br>
Drag a bond, or Ctrl+drag anything → move. Wheel → zoom. Middle/right drag or Space+drag → pan.</p>
<p>Every key and command is rebindable in <i>Edit → Keyboard Shortcuts</i> (Ctrl+Alt+K).</p>)";
    QDialog dlg(this);
    dlg.setWindowTitle("Keyboard Reference");
    auto* tb = new QTextBrowser;
    tb->setHtml(html);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(tb);
    dlg.resize(620, 680);
    dlg.exec();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
    e->accept();
}

}  // namespace cd
