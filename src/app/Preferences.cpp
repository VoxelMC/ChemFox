#include "app/Preferences.h"

#include "core/chem/Chem.h"
#include "io/Emf.h"
#include "io/Export.h"
#include "render/Renderer.h"
#include "style/Style.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace cd {

namespace prefs {

QString defaultStyle() {
    QString name = QSettings().value(kDefaultStyle, "default").toString();
    return styles::availableNames().contains(name) ? name : QString("default");
}

QString exportFormat() {
    QString f = QSettings().value(kExportFormat, "svg").toString();
    if (f == "emf" && !io::emfSupported()) f = "svg";
    return QStringList{"svg", "pdf", "png", "emf"}.contains(f) ? f : QString("svg");
}

QString startFolder() {
    QSettings s;
    QString docs = s.value(kDocFolder).toString();
    if (!docs.isEmpty() && QDir(docs).exists()) return docs;
    return s.value("lastDir").toString();
}

QString importStyle(const QString& file, bool overwrite, QString* error) {
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error) *error = in.errorString();
        return {};
    }
    const QString text = QString::fromUtf8(in.readAll());
    std::vector<StyleIssue> issues;
    styles::resolve(text, &issues);
    // A file with nothing recognisable in it isn't a style.
    if (!text.contains(':') && !text.contains('=')) {
        if (error) *error = QFileInfo(file).fileName() + " doesn't look like a ChemFox style.";
        return {};
    }
    // A user style is known by its file name, so save it under the name it declares (style "Lab notebook").
    QString name = styles::declaredName(text).trimmed();
    if (name.isEmpty()) name = QFileInfo(file).completeBaseName();
    name.remove(QRegularExpression(R"([\\/:*?"<>|])"));
    if (name.isEmpty()) name = "Imported style";
    if (styles::builtinNames().contains(name, Qt::CaseInsensitive)) name += " (imported)";
    QDir dir(styles::userStyleDir());
    dir.mkpath(".");
    const QString dest = dir.filePath(name + ".style");
    if (QFileInfo(dest).absoluteFilePath() == QFileInfo(file).absoluteFilePath()) return name;  // already there
    if (QFile::exists(dest)) {
        if (!overwrite) {
            if (error) *error = "exists";
            return {};
        }
        QFile::remove(dest);
    }
    if (!QFile::copy(file, dest)) {
        if (error) *error = "Couldn't copy the style into " + dir.absolutePath();
        return {};
    }
    return name;
}

}  // namespace prefs

PreferencesDialog::PreferencesDialog(std::function<void()> openShortcuts, QWidget* parent)
    : QDialog(parent), openShortcuts_(std::move(openShortcuts)) {
    setWindowTitle("Preferences");
    auto* lay = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    tabs->addTab(generalTab(), "General");
    tabs->addTab(stylesTab(), "Styles");
    tabs->addTab(drawingTab(), "Drawing");
    tabs->addTab(exportTab(), "Export");
    tabs->addTab(keysTab(), "Keys");
    lay->addWidget(tabs);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    lay->addWidget(buttons);
    resize(560, 420);
}

namespace {
// A checkbox bound straight to a setting.
QCheckBox* settingBox(const QString& text, const char* key, bool def, QObject* owner, std::function<void()> changed) {
    auto* box = new QCheckBox(text);
    box->setChecked(QSettings().value(key, def).toBool());
    QObject::connect(box, &QCheckBox::toggled, owner, [key, changed](bool on) {
        QSettings().setValue(key, on);
        changed();
    });
    return box;
}

QLabel* note(const QString& text) {
    auto* l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet("color: palette(placeholder-text);");
    return l;
}
}  // namespace

QWidget* PreferencesDialog::generalTab() {
    auto* w = new QWidget;
    auto* form = new QFormLayout(w);
    defaultStyle_ = new QComboBox;
    form->addRow("Style for new documents:", defaultStyle_);
    connect(defaultStyle_, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        if (name.isEmpty()) return;
        QSettings().setValue(prefs::kDefaultStyle, name);
        emit changed();
    });
    form->addRow(note("Documents you open keep the style they were saved with."));
    auto emitChanged = [this] { emit changed(); };

    auto* tool = new QComboBox;
    const QList<QPair<QString, QString>> tools = {{"Select", "select"}, {"Chain", "chain"}, {"Arrow", "arrow"},
                                                  {"Bent arrow", "bent"}, {"Text", "text"},
                                                  {"Mechanism arrow", "mechanism"}};
    for (const auto& t : tools) tool->addItem(t.first, t.second);
    tool->setCurrentIndex(std::max(0, tool->findData(QSettings().value(prefs::kStartTool, "select").toString())));
    connect(tool, &QComboBox::currentIndexChanged, this, [tool](int) { QSettings().setValue(prefs::kStartTool, tool->currentData()); });
    form->addRow("Tool when ChemFox starts:", tool);

    auto* folderRow = new QHBoxLayout;
    auto* folder = new QLineEdit(QSettings().value(prefs::kDocFolder).toString());
    folder->setPlaceholderText("The last folder you used");
    folder->setClearButtonEnabled(true);
    auto* browse = new QPushButton("Choose…");
    folderRow->addWidget(folder, 1);
    folderRow->addWidget(browse);
    connect(folder, &QLineEdit::textChanged, this, [](const QString& t) { QSettings().setValue(prefs::kDocFolder, t.trimmed()); });
    connect(browse, &QPushButton::clicked, this, [this, folder] {
        QString dir = QFileDialog::getExistingDirectory(this, "Documents Folder", folder->text());
        if (!dir.isEmpty()) folder->setText(QDir::toNativeSeparators(dir));
    });
    form->addRow("Open and save documents in:", folderRow);

    form->addRow(settingBox("Reopen the last document when ChemFox starts", prefs::kReopenLast, false, this, emitChanged));
    auto* autosaveRow = new QHBoxLayout;
    autosaveRow->addWidget(settingBox("Keep a recovery copy of unsaved work every", prefs::kAutosave, true, this, emitChanged));
    auto* minutes = new QSpinBox;
    minutes->setRange(1, 30);
    minutes->setSuffix(" min");
    minutes->setValue(QSettings().value(prefs::kAutosaveMinutes, 2).toInt());
    connect(minutes, &QSpinBox::valueChanged, this, [this](int v) { QSettings().setValue(prefs::kAutosaveMinutes, v); emit changed(); });
    autosaveRow->addWidget(minutes);
    autosaveRow->addStretch(1);
    form->addRow(autosaveRow);
    form->addRow(note("If ChemFox closes without saving (a crash or power cut), it offers the recovery copy next time. "
                      "Your own file is never overwritten by it."));
    form->addRow(settingBox("Check for updates automatically (at most once a day)", prefs::kAutoUpdate, true, this, emitChanged));
    form->addRow(note("The update check only asks GitHub for the list of ChemFox releases; nothing about you or your "
                      "documents is sent."));
    fillStyles();
    return w;
}

QWidget* PreferencesDialog::stylesTab() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->addWidget(note("Styles you've saved (Style panel → Save as style…) or imported. Built-in styles aren't listed."));
    auto* split = new QHBoxLayout;
    userStyles_ = new QListWidget;
    split->addWidget(userStyles_, 1);
    preview_ = new QLabel;
    preview_->setMinimumSize(260, 150);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFrameShape(QFrame::StyledPanel);
    split->addWidget(preview_, 1);
    lay->addLayout(split, 1);
    connect(userStyles_, &QListWidget::currentRowChanged, this, [this] { showPreview(); });
    auto* row = new QHBoxLayout;
    auto* import = new QPushButton("Import…");
    auto* exportBtn = new QPushButton("Export…");
    auto* rename = new QPushButton("Rename…");
    auto* remove = new QPushButton("Delete");
    auto* makeDefault = new QPushButton("Use for New Documents");
    auto* folder = new QPushButton("Open Folder");
    for (auto* b : {import, exportBtn, rename, remove, makeDefault}) row->addWidget(b);
    row->addStretch(1);
    row->addWidget(folder);
    lay->addLayout(row);

    auto selectedFile = [this]() -> QString {
        auto* it = userStyles_->currentItem();
        return it ? it->data(Qt::UserRole).toString() : QString();
    };
    connect(import, &QPushButton::clicked, this, [this] {
        QStringList files = QFileDialog::getOpenFileNames(this, "Import Styles", QString(), "ChemFox styles (*.style);;All files (*)");
        QStringList done, failed;
        for (const auto& f : files) {
            QString err;
            QString name = prefs::importStyle(f, false, &err);
            if (name.isEmpty() && err == "exists") {
                if (QMessageBox::question(this, "Import Styles",
                                          QFileInfo(f).fileName() + " is already in your styles. Replace it?") == QMessageBox::Yes)
                    name = prefs::importStyle(f, true, &err);
                else continue;
            }
            if (name.isEmpty()) failed << err;
            else done << name;
        }
        fillStyles();
        if (!done.isEmpty()) emit changed();
        if (!failed.isEmpty()) QMessageBox::warning(this, "Import Styles", failed.join("\n"));
    });
    connect(exportBtn, &QPushButton::clicked, this, [this, selectedFile] {
        QString src = selectedFile();
        if (src.isEmpty()) return;
        QString dest = QFileDialog::getSaveFileName(this, "Export Style", QFileInfo(src).fileName(), "ChemFox styles (*.style)");
        if (dest.isEmpty()) return;
        QFile::remove(dest);
        if (!QFile::copy(src, dest)) QMessageBox::warning(this, "Export Style", "Couldn't write " + dest);
    });
    connect(remove, &QPushButton::clicked, this, [this, selectedFile] {
        QString src = selectedFile();
        if (src.isEmpty()) return;
        if (QMessageBox::question(this, "Delete Style",
                                  "Delete the style \"" + userStyles_->currentItem()->text() + "\"? Documents that use it "
                                  "keep their own copy.") != QMessageBox::Yes)
            return;
        QFile::remove(src);
        fillStyles();
        emit changed();
    });
    connect(rename, &QPushButton::clicked, this, [this, selectedFile] {
        QString src = selectedFile();
        if (src.isEmpty()) return;
        const QString old = userStyles_->currentItem()->text();
        bool ok = false;
        QString name = QInputDialog::getText(this, "Rename Style", "New name:", QLineEdit::Normal, old, &ok).trimmed();
        name.remove(QRegularExpression(R"([\\/:*?"<>|])"));
        if (!ok || name.isEmpty() || name == old) return;
        if (styles::availableNames().contains(name, Qt::CaseInsensitive)) {
            QMessageBox::warning(this, "Rename Style", "There's already a style called \"" + name + "\".");
            return;
        }
        // The name lives in the file too (style "name" based on ...), so rewrite it there as well.
        QFile in(src);
        if (!in.open(QIODevice::ReadOnly)) return;
        styles::Source s = styles::parseSource(QString::fromUtf8(in.readAll()));
        in.close();
        s.name = name;
        const QString dest = QFileInfo(src).absolutePath() + "/" + name + ".style";
        QFile out(dest);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, "Rename Style", "Couldn't write " + dest);
            return;
        }
        out.write(styles::writeSource(s).toUtf8());
        out.close();
        if (QFileInfo(dest).absoluteFilePath() != QFileInfo(src).absoluteFilePath()) QFile::remove(src);
        if (QSettings().value(prefs::kDefaultStyle).toString() == old) QSettings().setValue(prefs::kDefaultStyle, name);
        fillStyles();
        emit changed();
    });
    connect(makeDefault, &QPushButton::clicked, this, [this] {
        auto* it = userStyles_->currentItem();
        if (!it) return;
        QSettings().setValue(prefs::kDefaultStyle, it->text());
        fillStyles();
        emit changed();
    });
    connect(folder, &QPushButton::clicked, this, [] {
        QDir().mkpath(styles::userStyleDir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(styles::userStyleDir()));
    });
    fillStyles();
    return w;
}

QWidget* PreferencesDialog::drawingTab() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    auto emitChanged = [this] { emit changed(); };
    lay->addWidget(settingBox("New bonds, rings and groups lean toward the mouse", prefs::kSteerToMouse, true, this, emitChanged));
    lay->addWidget(note("When building from the keyboard, point where the next bond should go. Off: ChemFox always "
                        "picks the usual zig-zag direction."));
    auto* stepRow = new QHBoxLayout;
    stepRow->addWidget(new QLabel("Bonds drawn with the mouse snap to"));
    auto* step = new QComboBox;
    step->addItem(QString("30%1 steps").arg(QChar(0x00b0)), 30);
    step->addItem(QString("15%1 steps").arg(QChar(0x00b0)), 15);
    step->setCurrentIndex(QSettings().value(prefs::kBondStep, 30).toInt() == 15 ? 1 : 0);
    connect(step, &QComboBox::currentIndexChanged, this, [this, step](int) {
        QSettings().setValue(prefs::kBondStep, step->currentData());
        emit changed();
    });
    stepRow->addWidget(step);
    stepRow->addStretch(1);
    lay->addLayout(stepRow);
    lay->addWidget(note("Hold Alt while dragging to draw at any angle."));
    lay->addWidget(settingBox("Scroll wheel pans (Ctrl+scroll zooms)", prefs::kWheelPans, false, this, emitChanged));
    lay->addWidget(note("Handy on a trackpad. Off: the wheel zooms and Shift+scroll pans."));
    lay->addWidget(settingBox("Show the page width guide", prefs::kPageGuide, false, this, emitChanged));
    lay->addWidget(note("Dashed lines at the style's page width, where Arrange Scheme wraps rows (also View → Page Width Guide)."));
    auto* gridRow = new QHBoxLayout;
    gridRow->addWidget(settingBox("Show a grid", prefs::kShowGrid, false, this, emitChanged));
    gridRow->addWidget(settingBox("Snap moved objects to it", prefs::kSnapGrid, false, this, emitChanged));
    gridRow->addWidget(new QLabel("  spacing"));
    auto* gridSize = new QDoubleSpinBox;
    gridSize->setRange(0.1, 2.0);
    gridSize->setSingleStep(0.25);
    gridSize->setDecimals(2);
    gridSize->setSuffix(" bond");
    gridSize->setValue(QSettings().value(prefs::kGridSize, 0.5).toDouble());
    connect(gridSize, &QDoubleSpinBox::valueChanged, this, [this](double v) { QSettings().setValue(prefs::kGridSize, v); emit changed(); });
    gridRow->addWidget(gridSize);
    gridRow->addStretch(1);
    lay->addLayout(gridRow);
    lay->addWidget(note("Screen only: the grid is never exported. Also on the right-click menu of empty canvas."));
    lay->addStretch(1);
    return w;
}

QWidget* PreferencesDialog::exportTab() {
    auto* w = new QWidget;
    auto* form = new QFormLayout(w);
    auto* format = new QComboBox;
    format->addItem("SVG (reopenable)", "svg");
    format->addItem("PDF", "pdf");
    format->addItem("PNG", "png");
    if (io::emfSupported()) format->addItem("EMF (Word, PowerPoint)", "emf");
    format->setCurrentIndex(std::max(0, format->findData(prefs::exportFormat())));
    connect(format, &QComboBox::currentIndexChanged, this, [this, format](int) {
        QSettings().setValue(prefs::kExportFormat, format->currentData());
        emit changed();
    });
    form->addRow("File → Export (Ctrl+E) saves as:", format);
    auto* dpi = new QSpinBox;
    dpi->setRange(72, 2400);
    dpi->setSingleStep(50);
    dpi->setSuffix(" dpi");
    dpi->setValue(QSettings().value(prefs::kPngDpi, 300).toInt());
    connect(dpi, &QSpinBox::valueChanged, this, [](int v) { QSettings().setValue(prefs::kPngDpi, v); });
    form->addRow("PNG resolution:", dpi);
    auto nothing = [] {};
    form->addRow(settingBox("Ask for the resolution each time", prefs::kPngAsk, true, this, nothing));
    form->addRow(settingBox("Transparent background for PNG", prefs::kPngTransparent, false, this, nothing));
    form->addRow(note("300 dpi suits print; 150 is plenty for slides."));
    return w;
}

QWidget* PreferencesDialog::keysTab() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->addWidget(note("Every command and hover key can be changed, and you can add your own groups (like Ac or Boc) "
                        "from SMILES."));
    auto* open = new QPushButton("Keyboard Shortcuts and Groups…");
    connect(open, &QPushButton::clicked, this, [this] { if (openShortcuts_) openShortcuts_(); });
    lay->addWidget(open, 0, Qt::AlignLeft);
    lay->addStretch(1);
    return w;
}

void PreferencesDialog::fillStyles() {
    if (defaultStyle_) {
        QSignalBlocker block(defaultStyle_);
        defaultStyle_->clear();
        defaultStyle_->addItems(styles::availableNames());
        defaultStyle_->setCurrentText(prefs::defaultStyle());
    }
    if (userStyles_) {
        userStyles_->clear();
        QDir dir(styles::userStyleDir());
        for (const auto& fi : dir.entryInfoList({"*.style"}, QDir::Files, QDir::Name)) {
            auto* it = new QListWidgetItem(fi.completeBaseName(), userStyles_);  // a user style's name is its file name
            it->setData(Qt::UserRole, fi.absoluteFilePath());
            it->setToolTip(fi.absoluteFilePath());
            if (fi.completeBaseName() == prefs::defaultStyle()) {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
                it->setToolTip(fi.absoluteFilePath() + "\nUsed for new documents");
            }
        }
        if (userStyles_->count()) userStyles_->setCurrentRow(0);
        showPreview();
    }
}

void PreferencesDialog::showPreview() {
    if (!preview_) return;
    auto* it = userStyles_ ? userStyles_->currentItem() : nullptr;
    if (!it) {
        preview_->setPixmap({});
        preview_->setText("No saved styles yet.");
        return;
    }
    // A small sample scheme drawn in the style: salicylic acid to aspirin.
    static const Document sample = [] {
        Document d;
        auto add = [&](const QString& smi, double x) {
            if (auto m = chem::fromSmiles(smi)) {
                m->id = d.newId("m");
                QHash<QString, QString> ren;
                for (const auto& a : m->atoms) ren[a.id] = d.newId("a");
                m->renameAtoms(ren);
                for (auto& b : m->bonds) b.id = d.newId("b");
                m->translate(QPointF(x, 0) - m->center());
                d.molecules.push_back(*m);
            }
        };
        add("OC(=O)c1ccccc1O", 0);
        add("CC(=O)Oc1ccccc1C(=O)O", 9);
        Arrow a;
        a.id = d.newId("r");
        a.x1 = 3.0; a.x2 = 6.0; a.y1 = a.y2 = 0;
        a.above = "Ac2O";
        d.arrows.push_back(a);
        return d;
    }();
    Style st = styles::resolve(styles::textFor(it->text()));
    Renderer r(st);
    ChemOverlay ov;
    for (const auto& m : sample.molecules) {
        auto an = chem::analyze(m);
        for (auto h = an.implicitH.begin(); h != an.implicitH.end(); ++h) ov.implicitH[h.key()] = h.value();
        ov.aromaticRings[m.id] = an.aromaticRings;
    }
    QImage img = io::renderImage(sample, r, ov, 150);
    if (img.isNull()) { preview_->setText("(no preview)"); return; }
    // The label's fixed minimum size: its current size isn't known until the dialog is laid out.
    const QSize box = preview_->minimumSize() - QSize(16, 16);
    preview_->setPixmap(QPixmap::fromImage(img.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

}  // namespace cd
