#include "app/Shortcuts.h"

#include "editor/Canvas.h"
#include "editor/Keymap.h"

#include <QAction>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace cd {

namespace {

QString clean(QString text) {
    text.remove('&');
    text.remove(QChar(0x2026));  // …
    text.remove("...");
    return text.trimmed();
}

QString slug(const QString& text) {
    QString s = clean(text).toLower();
    s.replace(QRegularExpression("[^a-z0-9]+"), "-");
    return s.remove(QRegularExpression("^-+|-+$"));
}

QString keysText(const QList<QKeySequence>& keys) {
    QStringList out;
    for (const auto& k : keys) out << k.toString(QKeySequence::NativeText);
    return out.join(", ");
}

const QStringList kTargetOrder = {"atom", "bond", "arrow", "text", "number", "empty", "any"};
const QHash<QString, QString> kTargetNames = {{"atom", "On an atom"},        {"bond", "On a bond"},
                                              {"arrow", "On an arrow"},      {"text", "On text"},
                                              {"number", "On a compound name"}, {"empty", "On empty canvas"},
                                              {"any", "Anywhere"}};

}  // namespace

// ---------------------------------------------------------------------------
// Registry

QString ShortcutRegistry::path() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/shortcuts.json";
}

void ShortcutRegistry::add(QAction* a, const QString& id, const QString& category) {
    if (!a || a->isSeparator() || a->menu() || find(id)) return;
    a->setObjectName(id);
    entries_.push_back({id, category, clean(a->text()), a, a->shortcuts()});
}

void ShortcutRegistry::addMenu(QMenu* menu, const QString& prefix, const QString& parentCategory) {
    const QString cat = parentCategory.isEmpty() ? clean(menu->title()) : parentCategory + " › " + clean(menu->title());
    const QString base = prefix.isEmpty() ? slug(menu->title()) : prefix + "." + slug(menu->title());
    for (QAction* a : menu->actions()) {
        if (a->isSeparator()) continue;
        if (a->menu()) { addMenu(a->menu(), base, cat); continue; }
        // Actions given an id up front keep it (their text may change, like Undo).
        add(a, a->objectName().isEmpty() ? base + "." + slug(a->text()) : a->objectName(), cat);
    }
}
ShortcutRegistry::Entry* ShortcutRegistry::find(const QString& id) {
    for (auto& e : entries_) if (e.id == id) return &e;
    return nullptr;
}

void ShortcutRegistry::load() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        Entry* e = find(it.key());
        if (!e) continue;
        QList<QKeySequence> keys;
        for (const auto& v : it.value().toArray()) keys << QKeySequence::fromString(v.toString(), QKeySequence::PortableText);
        e->action->setShortcuts(keys);
    }
}

bool ShortcutRegistry::save() const {
    // Only what differs from the defaults, so new default shortcuts reach existing users.
    QJsonObject root;
    for (const auto& e : entries_) {
        if (e.action->shortcuts() == e.defaults) continue;
        QJsonArray keys;
        for (const auto& k : e.action->shortcuts()) keys.append(k.toString(QKeySequence::PortableText));
        root[e.id] = keys;
    }
    QFile f(path());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson());
    return true;
}

void ShortcutRegistry::set(const QString& id, const QList<QKeySequence>& keys) {
    if (Entry* e = find(id)) e->action->setShortcuts(keys);
}

void ShortcutRegistry::reset(const QString& id) {
    if (Entry* e = find(id)) e->action->setShortcuts(e->defaults);
}

void ShortcutRegistry::resetAll() {
    for (auto& e : entries_) e.action->setShortcuts(e.defaults);
}

const ShortcutRegistry::Entry* ShortcutRegistry::holder(const QKeySequence& key, const QString& except) const {
    for (const auto& e : entries_)
        if (e.id != except && e.action->shortcuts().contains(key)) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Key capture for hover keys

KeyCapture::KeyCapture(QWidget* parent) : QLineEdit(parent) {
    setReadOnly(true);
    setPlaceholderText("Click here, then press a key");
}

bool KeyCapture::event(QEvent* e) {
    // Take Tab, Enter and Escape as keys to bind rather than dialog navigation.
    if (e->type() == QEvent::KeyPress) {
        keyPressEvent(static_cast<QKeyEvent*>(e));
        return true;
    }
    if (e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    return QLineEdit::event(e);
}

void KeyCapture::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Shift || e->key() == Qt::Key_Control || e->key() == Qt::Key_Alt || e->key() == Qt::Key_Meta)
        return;
    QString k = Keymap::keyName(e);
    if (k.isEmpty() || k.trimmed().isEmpty()) {
        setText("Hover keys are single keys (Shift allowed)");
        return;
    }
    setText(k);
    emit captured(k);
}

// ---------------------------------------------------------------------------
// Dialog

ShortcutsDialog::ShortcutsDialog(ShortcutRegistry& registry, Canvas* canvas, QWidget* parent)
    : QDialog(parent), reg_(registry), canvas_(canvas) {
    setWindowTitle("Keyboard Shortcuts");
    auto* lay = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    tabs->addTab(buildCommandsTab(), "Commands");
    tabs->addTab(buildHoverTab(), "Hover keys");
    lay->addWidget(tabs);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::accept);
    lay->addWidget(close);
    resize(720, 640);
}

QWidget* ShortcutsDialog::buildCommandsTab() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->addWidget(new QLabel("Every command can have shortcuts. Select one, then press the keys. "
                              "Commands that act on a structure or arrow use the one under the cursor, else the selection."));
    cmdFilter_ = new QLineEdit;
    cmdFilter_->setPlaceholderText("Search commands or keys");
    cmdFilter_->setClearButtonEnabled(true);
    lay->addWidget(cmdFilter_);
    cmdTree_ = new QTreeWidget;
    cmdTree_->setColumnCount(3);
    cmdTree_->setHeaderLabels({"Command", "Shortcut", "Default"});
    cmdTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    cmdTree_->setUniformRowHeights(true);
    lay->addWidget(cmdTree_, 1);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("Shortcut:"));
    cmdKey_ = new QKeySequenceEdit;
    cmdKey_->setMaximumSequenceLength(1);
    cmdKey_->setClearButtonEnabled(true);
    row->addWidget(cmdKey_, 1);
    auto* add = new QPushButton("Add");
    add->setToolTip("Keep the current shortcut and add this one too");
    auto* clear = new QPushButton("Remove All");
    auto* reset = new QPushButton("Reset");
    auto* resetAll = new QPushButton("Reset All");
    for (auto* b : {add, clear, reset, resetAll}) row->addWidget(b);
    lay->addLayout(row);
    cmdNote_ = new QLabel;
    cmdNote_->setWordWrap(true);
    lay->addWidget(cmdNote_);

    auto currentId = [this] {
        auto* it = cmdTree_->currentItem();
        return it ? it->data(0, Qt::UserRole).toString() : QString();
    };
    connect(cmdFilter_, &QLineEdit::textChanged, this, &ShortcutsDialog::fillCommands);
    connect(cmdKey_, &QKeySequenceEdit::editingFinished, this, [this] {
        if (!cmdKey_->keySequence().isEmpty()) assignCommand(cmdKey_->keySequence());
    });
    connect(add, &QPushButton::clicked, this, [this, currentId] {
        QString id = currentId();
        QKeySequence k = cmdKey_->keySequence();
        if (id.isEmpty() || k.isEmpty()) return;
        for (const auto& e : reg_.entries())
            if (e.id == id) {
                QList<QKeySequence> keys = e.action->shortcuts();
                if (!keys.contains(k)) keys << k;
                if (const auto* other = reg_.holder(k, id)) {
                    QList<QKeySequence> theirs = other->action->shortcuts();
                    theirs.removeAll(k);
                    reg_.set(other->id, theirs);
                    cmdNote_->setText(QString("%1 was used by “%2”; it now belongs to “%3”.")
                                          .arg(k.toString(QKeySequence::NativeText), other->label, e.label));
                }
                reg_.set(id, keys);
            }
        reg_.save();
        fillCommands();
    });
    connect(clear, &QPushButton::clicked, this, [this, currentId] {
        if (currentId().isEmpty()) return;
        reg_.set(currentId(), {});
        reg_.save();
        fillCommands();
    });
    connect(reset, &QPushButton::clicked, this, [this, currentId] {
        if (currentId().isEmpty()) return;
        reg_.reset(currentId());
        reg_.save();
        fillCommands();
    });
    connect(resetAll, &QPushButton::clicked, this, [this] {
        reg_.resetAll();
        reg_.save();
        cmdNote_->setText("All command shortcuts are back to their defaults.");
        fillCommands();
    });
    connect(cmdTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        cmdKey_->clear();
        if (it && !it->data(0, Qt::UserRole).toString().isEmpty())
            cmdNote_->setText(QString("“%1” — press the new shortcut in the box below.").arg(it->text(0)));
    });
    fillCommands();
    return w;
}

void ShortcutsDialog::fillCommands() {
    QString keep;
    if (auto* it = cmdTree_->currentItem()) keep = it->data(0, Qt::UserRole).toString();
    const QString filter = cmdFilter_->text().trimmed();
    cmdTree_->clear();
    QMap<QString, QTreeWidgetItem*> cats;
    QStringList catOrder;
    for (const auto& e : reg_.entries()) {
        QString keys = keysText(e.action->shortcuts()), defs = keysText(e.defaults);
        if (!filter.isEmpty() && !e.label.contains(filter, Qt::CaseInsensitive) && !e.category.contains(filter, Qt::CaseInsensitive) &&
            !keys.contains(filter, Qt::CaseInsensitive))
            continue;
        if (!cats.contains(e.category)) {
            auto* c = new QTreeWidgetItem(cmdTree_, {e.category});
            QFont f = c->font(0);
            f.setBold(true);
            c->setFont(0, f);
            c->setFirstColumnSpanned(true);
            cats[e.category] = c;
        }
        auto* it = new QTreeWidgetItem(cats[e.category], {e.label, keys, defs});
        it->setData(0, Qt::UserRole, e.id);
        it->setToolTip(0, e.id);
        if (keys != defs) {
            QFont f = it->font(1);
            f.setBold(true);
            it->setFont(1, f);
        }
        if (e.id == keep) cmdTree_->setCurrentItem(it);
    }
    cmdTree_->expandAll();
}

void ShortcutsDialog::assignCommand(const QKeySequence& key) {
    auto* it = cmdTree_->currentItem();
    QString id = it ? it->data(0, Qt::UserRole).toString() : QString();
    if (id.isEmpty()) {
        cmdNote_->setText("Pick a command first.");
        return;
    }
    QString label = it->text(0), note;
    if (const auto* other = reg_.holder(key, id)) {
        // One key, one command: take it from the other (Qt would otherwise fire neither).
        QList<QKeySequence> theirs = other->action->shortcuts();
        theirs.removeAll(key);
        reg_.set(other->id, theirs);
        note = QString("%1 was used by “%2”; it now belongs to “%3”. ")
                   .arg(key.toString(QKeySequence::NativeText), other->label, label);
    }
    // A bare key (no Ctrl/Alt/Meta) would steal typing from the hover keys on the canvas.
    const auto combo = key[0];
    if (!(combo.keyboardModifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        !(combo.key() >= Qt::Key_F1 && combo.key() <= Qt::Key_F35))
        note += "Note: a single key here takes that key away from the hover keys.";
    reg_.set(id, {key});
    reg_.save();
    cmdNote_->setText(note.isEmpty() ? QString("“%1” is now %2.").arg(label, key.toString(QKeySequence::NativeText)) : note);
    fillCommands();
}

QWidget* ShortcutsDialog::buildHoverTab() {
    auto* w = new QWidget;
    auto* lay = new QVBoxLayout(w);
    lay->addWidget(new QLabel("Hover keys act on whatever is under the cursor, without Ctrl. Every action is listed, "
                              "bound or not; an action can have several keys."));
    hoverFilter_ = new QLineEdit;
    hoverFilter_->setPlaceholderText("Search actions or keys");
    hoverFilter_->setClearButtonEnabled(true);
    lay->addWidget(hoverFilter_);
    hoverTree_ = new QTreeWidget;
    hoverTree_->setColumnCount(2);
    hoverTree_->setHeaderLabels({"Action", "Keys"});
    hoverTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    hoverTree_->setUniformRowHeights(true);
    lay->addWidget(hoverTree_, 1);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("Key:"));
    hoverKey_ = new KeyCapture;
    row->addWidget(hoverKey_, 1);
    auto* add = new QPushButton("Add");
    add->setToolTip("Keep the current keys and add this one too");
    auto* clear = new QPushButton("Remove All");
    auto* reset = new QPushButton("Reset");
    auto* resetAll = new QPushButton("Reset All");
    auto* file = new QPushButton("Open File");
    file->setToolTip("keymap.json, if you'd rather edit it by hand (reopen this dialog afterwards)");
    for (auto* b : {add, clear, reset, resetAll, file}) row->addWidget(b);
    lay->addLayout(row);
    hoverNote_ = new QLabel;
    hoverNote_->setWordWrap(true);
    lay->addWidget(hoverNote_);

    connect(hoverFilter_, &QLineEdit::textChanged, this, &ShortcutsDialog::fillHover);
    connect(hoverKey_, &KeyCapture::captured, this, [this](const QString& k) { setHover(k, false); });
    connect(add, &QPushButton::clicked, this, [this] {
        if (!hoverKey_->text().isEmpty()) setHover(hoverKey_->text(), true);
    });
    auto current = [this]() -> std::pair<QString, QString> {
        auto* it = hoverTree_->currentItem();
        if (!it || it->data(0, Qt::UserRole).toString().isEmpty()) return {};
        return {it->data(0, Qt::UserRole).toString(), it->data(1, Qt::UserRole).toString()};
    };
    auto apply = [this](const Keymap::Map& m) {
        canvas_->keymap().setMap(m);
        canvas_->keymap().save();
        fillHover();
    };
    connect(clear, &QPushButton::clicked, this, [this, current, apply] {
        auto [target, action] = current();
        if (target.isEmpty()) return;
        Keymap::Map m = canvas_->keymap().map();
        for (const auto& k : m[target].keys()) if (m[target][k] == action) m[target].remove(k);
        apply(m);
    });
    connect(reset, &QPushButton::clicked, this, [this, current, apply] {
        auto [target, action] = current();
        if (target.isEmpty()) return;
        Keymap::Map m = canvas_->keymap().map();
        for (const auto& k : m[target].keys()) if (m[target][k] == action) m[target].remove(k);
        const auto d = Keymap::defaults().value(target);
        for (auto it = d.begin(); it != d.end(); ++it)
            if (it.value() == action) m[target][it.key()] = action;
        apply(m);
    });
    connect(resetAll, &QPushButton::clicked, this, [this, apply] {
        apply(Keymap::defaults());
        hoverNote_->setText("All hover keys are back to their defaults.");
    });
    connect(file, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(canvas_->keymap().path())); });
    connect(hoverTree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* it) {
        hoverKey_->clear();
        if (it && !it->data(0, Qt::UserRole).toString().isEmpty())
            hoverNote_->setText(QString("“%1” — click the Key box and press the new key.").arg(it->text(0)));
    });
    fillHover();
    return w;
}

void ShortcutsDialog::fillHover() {
    std::pair<QString, QString> keep;
    if (auto* it = hoverTree_->currentItem()) keep = {it->data(0, Qt::UserRole).toString(), it->data(1, Qt::UserRole).toString()};
    const QString filter = hoverFilter_->text().trimmed();
    const Keymap::Map& m = canvas_->keymap().map();
    const Keymap::Map d = Keymap::defaults();
    hoverTree_->clear();
    QMap<QString, QTreeWidgetItem*> groups;
    for (const auto& target : kTargetOrder) {
        auto* g = new QTreeWidgetItem(hoverTree_, {kTargetNames.value(target)});
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setFirstColumnSpanned(true);
        groups[target] = g;
    }
    // Actions known to the app, plus anything custom found in the keymap.
    auto actions = Keymap::availableActions();
    for (auto t = m.begin(); t != m.end(); ++t)
        for (auto k = t->begin(); k != t->end(); ++k)
            if (std::find(actions.begin(), actions.end(), std::make_pair(t.key(), k.value())) == actions.end())
                actions.push_back({t.key(), k.value()});
    for (const auto& [target, action] : actions) {
        if (!groups.contains(target)) continue;
        QStringList keys, defs;
        const auto bound = m.value(target), byDefault = d.value(target);  // copies: iterate one hash, not two temporaries
        for (auto k = bound.begin(); k != bound.end(); ++k) if (k.value() == action) keys << k.key();
        for (auto k = byDefault.begin(); k != byDefault.end(); ++k) if (k.value() == action) defs << k.key();
        keys.sort();
        defs.sort();
        QString label = Keymap::describe(action);
        if (!filter.isEmpty() && !label.contains(filter, Qt::CaseInsensitive) && !keys.join(' ').contains(filter, Qt::CaseInsensitive))
            continue;
        auto* it = new QTreeWidgetItem(groups[target], {label, keys.join(", ")});
        it->setData(0, Qt::UserRole, target);
        it->setData(1, Qt::UserRole, action);
        it->setToolTip(0, action);
        if (keys != defs) {
            QFont f = it->font(1);
            f.setBold(true);
            it->setFont(1, f);
        }
        if (keys.isEmpty()) it->setForeground(1, QColor(150, 150, 150));
        if (target == keep.first && action == keep.second) hoverTree_->setCurrentItem(it);
    }
    for (auto* g : groups) if (g->childCount() == 0) delete g;
    hoverTree_->expandAll();
}

void ShortcutsDialog::setHover(const QString& key, bool add) {
    auto* it = hoverTree_->currentItem();
    if (!it || it->data(0, Qt::UserRole).toString().isEmpty()) {
        hoverNote_->setText("Pick an action first.");
        return;
    }
    const QString target = it->data(0, Qt::UserRole).toString(), action = it->data(1, Qt::UserRole).toString();
    Keymap::Map m = canvas_->keymap().map();
    QString note;
    if (m[target].contains(key) && m[target][key] != action)
        note = QString("%1 was “%2” here; now it's “%3”.").arg(key, Keymap::describe(m[target][key]), Keymap::describe(action));
    if (!add)
        for (const auto& k : m[target].keys()) if (m[target][k] == action) m[target].remove(k);
    m[target][key] = action;
    // A key bound here hides the same key's "Anywhere" binding on this target.
    if (target != "any" && m.value("any").contains(key) && m["any"][key] != action)
        note += QString(" (On this target it replaces the “Anywhere” key %1: %2.)").arg(key, Keymap::describe(m["any"][key]));
    canvas_->keymap().setMap(m);
    canvas_->keymap().save();
    hoverNote_->setText(note.isEmpty() ? QString("“%1” is now %2.").arg(Keymap::describe(action), key) : note);
    fillHover();
}

}  // namespace cd
