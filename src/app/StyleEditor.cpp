#include "app/StyleEditor.h"

#include "editor/Canvas.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace cd {

namespace {

QIcon swatch(const QColor& c) {
    QPixmap pm(28, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QColor(128, 128, 128));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, 27, 15), 3, 3);
    return QIcon(pm);
}

}  // namespace

StyleEditor::StyleEditor(Canvas* canvas, QWidget* parent) : QWidget(parent), canvas_(canvas) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);

    auto* top = new QFormLayout;
    baseBox_ = new QComboBox;
    baseBox_->addItems(styles::builtinNames());
    baseBox_->setToolTip("Settings you don't change here come from this style");
    nameLabel_ = new QLabel;
    nameLabel_->setStyleSheet("color: palette(placeholder-text)");
    top->addRow("Based on", baseBox_);
    top->addRow("", nameLabel_);
    lay->addLayout(top);

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->setUsesScrollButtons(true);
    lay->addWidget(tabs_, 1);

    buildFields();
    QMap<QString, QFormLayout*> forms;
    QStringList tabOrder;
    for (auto& f : fields_) {
        if (!forms.contains(f.tab)) {
            auto* page = new QWidget;
            auto* form = new QFormLayout(page);
            form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
            form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
            auto* scroll = new QScrollArea;
            scroll->setWidgetResizable(true);
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidget(page);
            tabs_->addTab(scroll, f.tab);
            forms[f.tab] = form;
        }
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        f.editor = makeEditor(f);
        f.revert = new QToolButton;
        f.revert->setText("↺");
        f.revert->setAutoRaise(true);
        f.revert->setToolTip("Use the base style's value");
        h->addWidget(f.editor, 1);
        h->addWidget(f.revert);
        f.labelWidget = new QLabel(f.label);
        f.labelWidget->setToolTip(f.tip);
        f.editor->setToolTip(f.tip);
        forms[f.tab]->addRow(f.labelWidget, row);
        Field* fp = &f;
        connect(f.revert, &QToolButton::clicked, this, [this, fp] { revertField(*fp); });
    }

    // Source: the style as text, for anyone who prefers typing (or settings without a control).
    sourceEdit_ = new QPlainTextEdit;
    QFont mono("Consolas");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    sourceEdit_->setFont(mono);
    sourceEdit_->setTabChangesFocus(false);
    sourceEdit_->setTabStopDistance(QFontMetricsF(mono).horizontalAdvance(' ') * 2);
    tabs_->addTab(sourceEdit_, "Source");

    auto* buttons = new QHBoxLayout;
    auto* reset = new QPushButton("Reset All");
    reset->setToolTip("Drop every change and use the base style as it is");
    auto* save = new QPushButton("Save as .style…");
    save->setToolTip("Save this style so other documents can use it");
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(save);
    lay->addLayout(buttons);

    sourceTimer_.setSingleShot(true);
    sourceTimer_.setInterval(500);
    connect(&sourceTimer_, &QTimer::timeout, this, [this] {
        // Typed source wins: apply it as written and let the form follow.
        canvas_->setStyleText(sourceEdit_->toPlainText());
    });
    connect(sourceEdit_, &QPlainTextEdit::textChanged, this, [this] {
        if (!updating_) sourceTimer_.start();
    });
    connect(baseBox_, &QComboBox::activated, this, [this](int i) {
        base_ = baseBox_->itemText(i);
        source_.base = base_;
        apply();
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        source_.entries.clear();
        apply();
    });
    connect(save, &QPushButton::clicked, this, &StyleEditor::saveAs);
    connect(canvas_, &Canvas::documentChanged, this, &StyleEditor::refresh);
    refresh();
}

void StyleEditor::buildFields() {
    using K = Kind;
    auto add = [&](const char* tab, const char* section, const char* key, const char* label, K kind,
                   std::function<QVariant(const Style&)> get, const char* tip, QStringList choices = {}) {
        fields_.push_back({tab, section, key, label, tip, kind, std::move(get), choices});
    };
    add("Bonds", "", "bond length", "Bond length", K::Length, [](const Style& s) { return s.bondLength; },
        "Length of one bond; everything else scales with the drawing");
    add("Bonds", "", "bond width", "Line width", K::Length, [](const Style& s) { return s.lineWidth; }, "Width of bond lines");
    add("Bonds", "", "bold width", "Wedge width", K::Length, [](const Style& s) { return s.boldWidth; },
        "Width of the wide end of wedges and hashes");
    add("Bonds", "", "bond spacing", "Double-bond spacing", K::Percent,
        [](const Style& s) { return s.bondSpacing * 100; }, "Gap between the lines of a double bond, as a share of bond length");
    add("Bonds", "", "hash spacing", "Hash spacing", K::Length, [](const Style& s) { return s.hashSpacing; },
        "Distance between the lines of a hashed bond");
    add("Bonds", "", "margin", "Gap at labels", K::Length, [](const Style& s) { return s.margin; },
        "How far bonds stop short of atom labels");
    add("Bonds", "", "aromatic circles", "Aromatic circles", K::Bool, [](const Style& s) { return s.aromaticCircles; },
        "Draw aromatic rings with a circle instead of alternating double bonds");

    add("Atom labels", "", "text font", "Font", K::Font, [](const Style& s) { return s.font; }, "Font for atom labels and text boxes");
    add("Atom labels", "", "text size", "Size", K::Length, [](const Style& s) { return s.fontSize; }, "Size of atom labels and text");
    add("Atom labels", "", "text color", "Colour", K::Color, [](const Style& s) { return s.textColor; }, "Colour of atom labels");
    add("Atom labels", "", "atom colors", "Colour by element", K::Bool, [](const Style& s) { return s.atomColors; },
        "N blue, O red, S yellow, halogens green…");

    add("Arrows", "arrows", "width", "Line width", K::Length, [](const Style& s) { return s.arrowWidth; }, "Width of arrow lines");
    add("Arrows", "arrows", "head", "Head", K::Choice, [](const Style& s) { return s.arrowHeadFilled ? "filled" : "open"; },
        "Filled triangle or open chevron", {"filled", "open"});
    add("Arrows", "arrows", "head length", "Head length", K::Length, [](const Style& s) { return s.arrowHeadLength; }, "");
    add("Arrows", "arrows", "head width", "Head width", K::Length, [](const Style& s) { return s.arrowHeadWidth; }, "");
    add("Arrows", "arrows", "min length", "Shortest arrow", K::Length, [](const Style& s) { return s.arrowMinLength; },
        "Arrows are never shorter than this; they also stretch to fit their text");
    add("Arrows", "arrows", "text gap", "Text distance", K::Length, [](const Style& s) { return s.arrowTextGap; },
        "Space between an arrow and the conditions written above and below it");
    add("Arrows", "arrows", "color", "Colour", K::Color, [](const Style& s) { return s.arrowColor; },
        "Arrow colour (an arrow's own colour overrides it)");

    add("Conditions", "annotations", "font", "Font", K::Font, [](const Style& s) { return s.annotationFont; },
        "Font for the text above and below arrows, and captions");
    add("Conditions", "annotations", "size", "Size", K::Length, [](const Style& s) { return s.annotationSize; }, "");
    add("Conditions", "annotations", "color", "Colour", K::Color, [](const Style& s) { return s.annotationColor; }, "");

    add("Names", "numbers", "font", "Font", K::Font, [](const Style& s) { return s.numberFont; },
        "Font for compound names and numbers under structures");
    add("Names", "numbers", "size", "Size", K::Length, [](const Style& s) { return s.numberSize; }, "");
    add("Names", "numbers", "gap", "Distance from structure", K::Length, [](const Style& s) { return s.numberGap; },
        "Space between a structure and the name or number underneath it");
    add("Names", "numbers", "weight", "Weight", K::Choice, [](const Style& s) { return s.numberBold ? "bold" : "normal"; },
        "", {"bold", "normal"});
    add("Names", "numbers", "color", "Colour", K::Color, [](const Style& s) { return s.numberColor; }, "");

    add("Layout", "scheme", "spacing", "Space between items", K::Length, [](const Style& s) { return s.schemeSpacing; },
        "Gap between structures, plus signs and arrows when arranging a scheme");
    add("Layout", "scheme", "row spacing", "Space between rows", K::Length, [](const Style& s) { return s.rowSpacing; }, "");
    add("Layout", "", "page width", "Page width", K::Length, [](const Style& s) { return s.pageWidth; },
        "Long left-to-right schemes wrap at this width");

    add("Colours", "", "color", "Bonds", K::Color, [](const Style& s) { return s.color; }, "Colour of bonds");
    add("Colours", "", "background", "Background", K::Color, [](const Style& s) { return s.background; },
        "Page colour (white is left transparent in SVG, PDF and EMF)");
    add("Colours", "", "formed bonds", "Formed bonds", K::Color, [](const Style& s) { return s.formedBonds; },
        "Bond changes in mapped reactions");
    add("Colours", "", "broken bonds", "Broken bonds", K::Color, [](const Style& s) { return s.brokenBonds; }, "");
    add("Colours", "", "changed bonds", "Changed order", K::Color, [](const Style& s) { return s.changedBonds; }, "");
}

QWidget* StyleEditor::makeEditor(Field& f) {
    Field* fp = &f;
    switch (f.kind) {
    case Kind::Length: {
        auto* w = new QDoubleSpinBox;
        w->setRange(0, 2000);
        w->setDecimals(2);
        w->setSingleStep(f.key.contains("size") ? 0.5 : (f.key.contains("width") && !f.key.contains("page") ? 0.1 : 1.0));
        w->setSuffix(" pt");
        w->setKeyboardTracking(false);
        connect(w, &QDoubleSpinBox::valueChanged, this, [this, fp](double v) { fieldChanged(*fp, format(Kind::Length, v)); });
        return w;
    }
    case Kind::Percent: {
        auto* w = new QSpinBox;
        w->setRange(1, 100);
        w->setSuffix(" %");
        w->setKeyboardTracking(false);
        connect(w, &QSpinBox::valueChanged, this, [this, fp](int v) { fieldChanged(*fp, format(Kind::Percent, v)); });
        return w;
    }
    case Kind::Color: {
        auto* w = new QToolButton;
        w->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        w->setIconSize(QSize(28, 16));
        w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(w, &QToolButton::clicked, this, [this, fp, w] {
            QColor c = QColorDialog::getColor(QColor::fromString(w->text()), this, fp->label);
            if (c.isValid()) fieldChanged(*fp, c.name());
        });
        return w;
    }
    case Kind::Bool: {
        auto* w = new QCheckBox;
        connect(w, &QCheckBox::toggled, this, [this, fp](bool on) { fieldChanged(*fp, on ? "yes" : "no"); });
        return w;
    }
    case Kind::Font: {
        auto* w = new QFontComboBox;
        connect(w, &QFontComboBox::currentFontChanged, this, [this, fp](const QFont& font) { fieldChanged(*fp, font.family()); });
        return w;
    }
    case Kind::Choice: {
        auto* w = new QComboBox;
        w->addItems(f.choices);
        connect(w, &QComboBox::currentTextChanged, this, [this, fp](const QString& t) { fieldChanged(*fp, t); });
        return w;
    }
    }
    return new QWidget;
}

QString StyleEditor::format(Kind kind, const QVariant& v) {
    switch (kind) {
    case Kind::Length: return QString::number(v.toDouble(), 'g', 4) + " pt";
    case Kind::Percent: return QString::number(qRound(v.toDouble())) + "%";
    case Kind::Color: return v.value<QColor>().name();
    case Kind::Bool: return v.toBool() ? "yes" : "no";
    default: return v.toString();
    }
}

void StyleEditor::refresh() {
    const Document& d = canvas_->doc();
    if (d.styleText.isEmpty()) {
        // A built-in (or saved) style used as it is: nothing overridden yet.
        source_ = {};
        source_.name = d.styleName;
        base_ = d.styleName;
        source_.base = base_;
    } else {
        source_ = styles::parseSource(d.styleText);
        base_ = source_.base;
    }
    updating_ = true;
    int bi = baseBox_->findText(base_);
    if (bi < 0 && !base_.isEmpty()) { baseBox_->addItem(base_); bi = baseBox_->count() - 1; }
    baseBox_->setCurrentIndex(bi);
    nameLabel_->setText(d.styleText.isEmpty() ? QString("Using “%1” unchanged").arg(d.styleName)
                                              : QString("Custom style “%1”, saved in this document").arg(source_.name));

    const Style& st = canvas_->style();  // resolved: own settings over the base
    for (auto& f : fields_) {
        QVariant v = f.get(st);
        switch (f.kind) {
        case Kind::Length: static_cast<QDoubleSpinBox*>(f.editor)->setValue(v.toDouble()); break;
        case Kind::Percent: static_cast<QSpinBox*>(f.editor)->setValue(qRound(v.toDouble())); break;
        case Kind::Color: {
            auto* b = static_cast<QToolButton*>(f.editor);
            QColor c = v.value<QColor>();
            b->setIcon(swatch(c));
            b->setText(c.name());
            break;
        }
        case Kind::Bool: static_cast<QCheckBox*>(f.editor)->setChecked(v.toBool()); break;
        case Kind::Font: static_cast<QFontComboBox*>(f.editor)->setCurrentFont(QFont(v.toString())); break;
        case Kind::Choice: static_cast<QComboBox*>(f.editor)->setCurrentText(v.toString()); break;
        }
        // Settings this style sets itself are bold and can be reverted to the base.
        bool own = source_.has(f.section, f.key) && !source_.base.isEmpty();
        QFont lf = f.labelWidget->font();
        lf.setBold(own);
        f.labelWidget->setFont(lf);
        f.revert->setEnabled(own);
    }
    if (!sourceEdit_->hasFocus()) {
        QString text = d.styleText.isEmpty() ? styles::textFor(d.styleName) : d.styleText;
        if (text != sourceEdit_->toPlainText()) sourceEdit_->setPlainText(text);
    }
    updating_ = false;
}

void StyleEditor::fieldChanged(Field& f, const QString& value) {
    if (updating_) return;
    source_.set(f.section, f.key, value);
    apply();
}

void StyleEditor::revertField(Field& f) {
    source_.unset(f.section, f.key);
    apply();
}

void StyleEditor::apply() {
    if (source_.base.isEmpty()) source_.base = base_.isEmpty() ? "default" : base_;
    if (source_.entries.empty() && styles::builtinNames().contains(source_.base)) {
        // Nothing changed from a built-in: just use it, no embedded copy.
        canvas_->setStyleName(source_.base);
        return;
    }
    if (source_.name.isEmpty() || source_.name == source_.base || styles::builtinNames().contains(source_.name))
        source_.name = source_.base + " (custom)";
    canvas_->setStyleText(styles::writeSource(source_));
}

void StyleEditor::saveAs() {
    bool ok = false;
    QString name = source_.name.isEmpty() ? "my style" : source_.name;
    name = QInputDialog::getText(this, "Save style", "Name:", QLineEdit::Normal, name.remove(" (custom)") + "", &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (styles::builtinNames().contains(name, Qt::CaseInsensitive)) name += " custom";
    styles::Source s = source_;
    s.name = name;
    if (s.base.isEmpty()) s.base = "default";
    QFile f(styles::userStyleDir() + "/" + name + ".style");
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(styles::writeSource(s).toUtf8());
    f.close();
    canvas_->setStyleName(name);
}

}  // namespace cd
