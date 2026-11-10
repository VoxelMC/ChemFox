#include "app/ReactionBuilder.h"

#include "core/chem/Chem.h"
#include "core/model/Mapping.h"
#include "editor/Canvas.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace cd {

namespace {

QString okStyle() { return "color: palette(placeholder-text)"; }
QString errStyle() { return "color: #d64545"; }

}  // namespace

ReactionBuilder::ReactionBuilder(Canvas* canvas, QWidget* parent) : QWidget(parent), canvas_(canvas) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);
    auto* body = new QWidget;
    scroll->setWidget(body);
    auto* lay = new QVBoxLayout(body);
    lay->setContentsMargins(6, 6, 6, 6);

    auto* top = new QHBoxLayout;
    auto* newBtn = new QPushButton("New reaction");
    auto* editBtn = new QPushButton("Edit selected");
    editBtn->setToolTip("Load the reaction whose arrow or structure is selected on the canvas");
    top->addWidget(newBtn);
    top->addWidget(editBtn);
    lay->addLayout(top);
    target_ = new QLabel;
    target_->setStyleSheet(okStyle());
    target_->setWordWrap(true);
    lay->addWidget(target_);

    lay->addWidget(new QLabel("Reaction SMILES / SMARTS"));
    line_ = new QLineEdit;
    line_->setPlaceholderText("CC(=O)O.OCC>>CCOC(C)=O");
    line_->setToolTip("reactants>reagents>products, dots between structures. Enter splits it into rows.");
    lay->addWidget(line_);

    auto section = [&](const QString& title, Section& s, const QString& addText) {
        auto* box = new QGroupBox(title);
        auto* v = new QVBoxLayout(box);
        v->setContentsMargins(6, 6, 6, 6);
        s.rows = new QVBoxLayout;
        s.rows->setSpacing(2);
        v->addLayout(s.rows);
        auto* add = new QPushButton(addText);
        add->setFlat(true);
        connect(add, &QPushButton::clicked, this, [this, &s] {
            addRow(s).edit->setFocus();
        });
        v->addWidget(add, 0, Qt::AlignLeft);
        lay->addWidget(box);
    };
    section("Reactants", reactants_, "+ Add reactant");
    section("Reagents (above the arrow)", agents_, "+ Add reagent");
    section("Products", products_, "+ Add product");

    auto* arrowBox = new QGroupBox("Arrow");
    auto* form = new QFormLayout(arrowBox);
    kind_ = new QComboBox;
    kind_->addItems({"Forward", "Equilibrium", "Retrosynthesis", "Dashed", "Crossed (no reaction)", "Resonance"});
    above_ = new QLineEdit;
    above_->setPlaceholderText("e.g. H2SO4 (cat.)");
    below_ = new QLineEdit;
    below_->setPlaceholderText("e.g. reflux, 4 h");
    form->addRow("Kind", kind_);
    form->addRow("Above", above_);
    form->addRow("Below", below_);
    lay->addWidget(arrowBox);

    auto* mapBox = new QGroupBox("Atom mapping");
    auto* mv = new QVBoxLayout(mapBox);
    autoMap_ = new QCheckBox("Map automatically");
    autoMap_->setChecked(true);
    autoMap_->setToolTip("Fewest bond changes, via the largest common substructure.\nOff: use the map numbers you type, e.g. [CH3:1][C:2](=O)[OH:3]");
    diff_ = new QCheckBox("Colour bond changes");
    diff_->setToolTip("Broken bonds red, formed green, changed order amber (colours come from the style)");
    diff_->setChecked(true);
    showMaps_ = new QCheckBox("Show map numbers");
    mv->addWidget(autoMap_);
    mv->addWidget(diff_);
    mv->addWidget(showMaps_);
    mapEdit_ = new QPushButton("Fix mapping on canvas…");
    mapEdit_->setCheckable(true);
    mapEdit_->setToolTip("Drag from a reactant atom to a product atom to map them (Ctrl+M)");
    mv->addWidget(mapEdit_, 0, Qt::AlignLeft);
    connect(mapEdit_, &QPushButton::toggled, canvas_, &Canvas::setMapMode);
    connect(canvas_, &Canvas::mapModeChanged, mapEdit_, [this](bool on) {
        QSignalBlocker b(mapEdit_);
        mapEdit_->setChecked(on);
    });
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    mv->addWidget(summary_);
    lay->addWidget(mapBox);
    lay->addStretch();

    timer_.setSingleShot(true);
    timer_.setInterval(350);
    connect(&timer_, &QTimer::timeout, this, &ReactionBuilder::apply);
    connect(newBtn, &QPushButton::clicked, this, &ReactionBuilder::newReaction);
    connect(editBtn, &QPushButton::clicked, this, &ReactionBuilder::editSelected);
    connect(line_, &QLineEdit::returnPressed, this, &ReactionBuilder::parseLine);
    connect(kind_, &QComboBox::currentIndexChanged, this, &ReactionBuilder::changed);
    connect(above_, &QLineEdit::textEdited, this, &ReactionBuilder::changed);
    connect(below_, &QLineEdit::textEdited, this, &ReactionBuilder::changed);
    connect(diff_, &QCheckBox::toggled, this, &ReactionBuilder::changed);
    connect(autoMap_, &QCheckBox::toggled, this, [this](bool on) {
        // Switching to manual mapping shows the current map numbers so they can be edited.
        if (!on && !arrowId_.isEmpty()) {
            if (auto s = canvas_->reactionFor(arrowId_, true)) {
                s->autoMap = false;
                load(*s);
            }
        }
        changed();
    });
    connect(showMaps_, &QCheckBox::toggled, canvas_, &Canvas::setShowMaps);
    connect(canvas_, &Canvas::documentChanged, this, &ReactionBuilder::documentChanged);

    newReaction();
}

ReactionBuilder::Row& ReactionBuilder::addRow(Section& s, const QString& text) {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto* h = new QHBoxLayout;
    auto* edit = new QLineEdit(text);
    edit->setPlaceholderText("SMILES or SMARTS");
    auto* del = new QToolButton;
    del->setText("✕");
    del->setToolTip("Remove");
    del->setAutoRaise(true);
    h->addWidget(edit);
    h->addWidget(del);
    v->addLayout(h);
    auto* status = new QLabel;
    status->setStyleSheet(okStyle());
    QFont f = status->font();
    f.setPointSizeF(f.pointSizeF() * 0.9);
    status->setFont(f);
    v->addWidget(status);
    s.rows->addWidget(w);
    s.items.push_back({w, edit, status});
    connect(edit, &QLineEdit::textEdited, this, &ReactionBuilder::changed);
    connect(del, &QToolButton::clicked, this, [this, &s, w] { removeRow(s, w); });
    return s.items.back();
}

void ReactionBuilder::removeRow(Section& s, QWidget* w) {
    s.items.erase(std::remove_if(s.items.begin(), s.items.end(), [&](const Row& r) { return r.widget == w; }),
                  s.items.end());
    w->hide();
    w->deleteLater();
    changed();
}

void ReactionBuilder::load(const ReactionSpec& sp) {
    loading_ = true;
    for (Section* s : {&reactants_, &agents_, &products_}) {
        for (auto& r : s->items) {
            r.widget->hide();
            r.widget->deleteLater();
        }
        s->items.clear();
    }
    for (const auto& t : sp.reactants) addRow(reactants_, t);
    for (const auto& t : sp.agents) addRow(agents_, t);
    for (const auto& t : sp.products) addRow(products_, t);
    if (reactants_.items.empty()) addRow(reactants_);
    if (products_.items.empty()) addRow(products_);
    kind_->setCurrentIndex(int(sp.kind));
    above_->setText(sp.above);
    below_->setText(sp.below);
    autoMap_->setChecked(sp.autoMap);
    diff_->setChecked(sp.showDiff);
    showMaps_->setChecked(canvas_->doc().showMaps);
    syncLine();
    loading_ = false;
}

ReactionSpec ReactionBuilder::spec() const {
    ReactionSpec s;
    auto texts = [](const Section& sec) {
        QStringList out;
        for (const auto& r : sec.items) out << r.edit->text();
        return out;
    };
    s.reactants = texts(reactants_);
    s.agents = texts(agents_);
    s.products = texts(products_);
    s.kind = ArrowKind(kind_->currentIndex());
    s.above = above_->text();
    s.below = below_->text();
    s.autoMap = autoMap_->isChecked();
    s.showDiff = diff_->isChecked();
    return s;
}

void ReactionBuilder::newReaction() {
    arrowId_.clear();
    ReactionSpec empty;
    load(empty);
    target_->setText("New reaction — it appears on the canvas as you type.");
    summary_->clear();
    if (!reactants_.items.empty()) reactants_.items.front().edit->setFocus();
}

void ReactionBuilder::editSelected() {
    QString a = canvas_->selectedArrow();
    if (a.isEmpty()) {
        target_->setText("Select a reaction arrow (or one of its structures) on the canvas first.");
        return;
    }
    auto s = canvas_->reactionFor(a, !autoMap_->isChecked());
    if (!s) return;
    arrowId_ = a;
    load(*s);
    target_->setText("Editing the selected reaction.");
}

void ReactionBuilder::addStructure(const QString& smiles, int role) {
    Section& s = role == 0 ? reactants_ : role == 1 ? agents_ : products_;
    QLineEdit* target = nullptr;
    for (auto& r : s.items)
        if (r.edit->text().trimmed().isEmpty()) { target = r.edit; break; }
    if (!target) target = addRow(s).edit;
    target->setText(smiles);
    target->setFocus();
    changed();
}

void ReactionBuilder::changed() {
    if (loading_) return;
    syncLine();
    timer_.start();
}

void ReactionBuilder::syncLine() {
    if (line_->hasFocus()) return;
    ReactionSpec s = spec();
    chem::ReactionText t;
    for (const auto& x : s.reactants) if (!x.trimmed().isEmpty()) t.reactants << x.trimmed();
    for (const auto& x : s.agents) if (!x.trimmed().isEmpty()) t.agents << x.trimmed();
    for (const auto& x : s.products) if (!x.trimmed().isEmpty()) t.products << x.trimmed();
    line_->setText(chem::joinReaction(t));
}

void ReactionBuilder::parseLine() {
    chem::ReactionText t = chem::splitReaction(line_->text());
    ReactionSpec s = spec();
    s.reactants = t.reactants;
    s.agents = t.agents;
    s.products = t.products;
    // Typed map numbers mean the user wants their own mapping.
    if (line_->text().contains(QRegularExpression(":\\d+\\]"))) s.autoMap = false;
    line_->clearFocus();
    load(s);
    apply();
}

void ReactionBuilder::apply() {
    timer_.stop();
    ReactionSpec s = spec();
    bool any = false;
    for (const auto& list : {s.reactants, s.agents, s.products})
        for (const auto& x : list) any |= !x.trimmed().isEmpty();
    if (!any && arrowId_.isEmpty()) return;
    // Consecutive edits of one reaction merge into a single undo step.
    ReactionResult r = canvas_->applyReaction(s, arrowId_, 3);
    if (arrowId_.isEmpty()) target_->setText("Editing the new reaction. Undo removes it.");
    arrowId_ = r.arrowId;
    updateStatus(r);
}

void ReactionBuilder::updateStatus(const ReactionResult& r) {
    int i = 0;
    for (Section* s : {&reactants_, &agents_, &products_}) {
        for (auto& row : s->items) {
            QString err = i < r.errors.size() ? r.errors[i] : QString();
            ++i;
            QString text = row.edit->text().trimmed();
            if (text.isEmpty()) { row.status->clear(); continue; }
            if (!err.isEmpty()) {
                row.status->setStyleSheet(errStyle());
                row.status->setText("Can't read this: " + err.section('\n', 0, 0));
                continue;
            }
            auto m = chem::fromSmilesOrSmarts(text);
            auto info = m ? chem::info(*m, false) : chem::MolInfo{};
            row.status->setStyleSheet(okStyle());
            row.status->setText(info.ok ? QString("%1 · %2 g/mol").arg(info.formula).arg(info.mw, 0, 'f', 2) : QString());
        }
    }
    refreshSummary();
}

void ReactionBuilder::refreshSummary() {
    // Mapping summary from the document as it now stands.
    const Document& d = canvas_->doc();
    for (const auto& sc : d.schemes)
        for (const auto& st : sc.steps) {
            if (st.arrow != arrowId_) continue;
            auto ch = mapping::bondChanges(d, st);
            int broken = 0, formed = 0, changedN = 0;
            for (auto it = ch.begin(); it != ch.end(); ++it) {
                if (it.value() == mapping::Change::Broken) ++broken;
                else if (it.value() == mapping::Change::Formed) ++formed;
                else if (it.value() == mapping::Change::Changed) ++changedN;
            }
            int mapped = 0;
            for (const auto& id : st.products)
                if (const Molecule* m = d.molecule(id))
                    for (const auto& a : m->atoms) mapped += a.map > 0;
            int unmapped = mapping::unmappedProductAtoms(d, st).size();
            QString t = QString("%1 product atoms mapped%2 · %3 broken · %4 formed · %5 changed order")
                            .arg(mapped).arg(st.manualMap ? " (by hand)" : "").arg(broken).arg(formed).arg(changedN / 2);
            if (unmapped) t += QString("<br><span style='color:#d08b00'>%1 product atoms unmapped (W203)</span>").arg(unmapped);
            summary_->setText(t);
        }
}

void ReactionBuilder::documentChanged() {
    // Undo may remove the reaction being edited.
    if (!arrowId_.isEmpty() && !canvas_->doc().arrow(arrowId_)) {
        arrowId_.clear();
        target_->setText("The reaction was removed. Typing creates a new one.");
    }
    if (loading_) return;
    {
        QSignalBlocker b(showMaps_);
        showMaps_->setChecked(canvas_->doc().showMaps);
    }
    if (arrowId_.isEmpty()) return;
    // Mapping fixed by hand on the canvas: switch the builder to typed maps so the next
    // edit here keeps the user's mapping instead of re-mapping automatically.
    bool manual = false;
    for (const auto& sc : canvas_->doc().schemes)
        for (const auto& st : sc.steps)
            if (st.arrow == arrowId_) manual = st.manualMap;
    if (manual && autoMap_->isChecked()) {
        if (auto s = canvas_->reactionFor(arrowId_, true)) {
            s->autoMap = false;
            QSignalBlocker b(autoMap_);
            load(*s);
        }
    }
    refreshSummary();
}

}  // namespace cd
