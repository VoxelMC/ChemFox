#include "app/Preferences.h"

#include "style/Style.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
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
    form->addRow(settingBox("Reopen the last document when ChemFox starts", prefs::kReopenLast, false, this, emitChanged));
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
    userStyles_ = new QListWidget;
    lay->addWidget(userStyles_, 1);
    auto* row = new QHBoxLayout;
    auto* import = new QPushButton("Import…");
    auto* exportBtn = new QPushButton("Export…");
    auto* remove = new QPushButton("Delete");
    auto* folder = new QPushButton("Open Folder");
    for (auto* b : {import, exportBtn, remove}) row->addWidget(b);
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
    lay->addWidget(settingBox("Scroll wheel pans (Ctrl+scroll zooms)", prefs::kWheelPans, false, this, emitChanged));
    lay->addWidget(note("Handy on a trackpad. Off: the wheel zooms and Shift+scroll pans."));
    lay->addStretch(1);
    return w;
}

QWidget* PreferencesDialog::exportTab() {
    auto* w = new QWidget;
    auto* form = new QFormLayout(w);
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
        }
    }
}

}  // namespace cd
