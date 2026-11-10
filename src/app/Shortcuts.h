#pragma once
// Every command in the app, with a rebindable keyboard shortcut, and the dialog that edits
// both these and the hover-and-type keys. Overrides live in shortcuts.json (commands) and
// keymap.json (hover keys) in the app data folder; changes apply immediately.

#include <QDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QList>
#include <vector>

class QAction;
class QLabel;
class QKeySequenceEdit;
class QMenu;
class QTreeWidget;
class QTreeWidgetItem;

namespace cd {

class Canvas;

class ShortcutRegistry {
public:
    struct Entry {
        QString id, category, label;
        QAction* action;
        QList<QKeySequence> defaults;
    };

    // Register an action under a stable id; its current shortcuts become the defaults.
    void add(QAction* a, const QString& id, const QString& category);
    // Register every action in a menu tree, deriving ids from the menu path ("file.save").
    void addMenu(QMenu* menu, const QString& prefix = {}, const QString& parentCategory = {});
    void load();  // apply saved overrides
    bool save() const;
    QString path() const;

    const std::vector<Entry>& entries() const { return entries_; }
    void set(const QString& id, const QList<QKeySequence>& keys);
    void reset(const QString& id);
    void resetAll();
    // The other command already using a key sequence, if any.
    const Entry* holder(const QKeySequence& key, const QString& except) const;

private:
    Entry* find(const QString& id);
    std::vector<Entry> entries_;
};

// A line edit that records one hover key in the keymap's own naming ("C", "Shift+6", "+").
class KeyCapture : public QLineEdit {
    Q_OBJECT
public:
    explicit KeyCapture(QWidget* parent = nullptr);
signals:
    void captured(const QString& key);
protected:
    void keyPressEvent(QKeyEvent* e) override;
    bool event(QEvent* e) override;
};

class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    ShortcutsDialog(ShortcutRegistry& registry, Canvas* canvas, QWidget* parent = nullptr);

private:
    QWidget* buildCommandsTab();
    QWidget* buildHoverTab();
    void fillCommands();
    void fillHover();
    void assignCommand(const QKeySequence& key);
    void setHover(const QString& key, bool add);

    ShortcutRegistry& reg_;
    Canvas* canvas_;
    QTreeWidget* cmdTree_;
    QLineEdit* cmdFilter_;
    QKeySequenceEdit* cmdKey_;
    QLabel* cmdNote_;
    QTreeWidget* hoverTree_;
    QLineEdit* hoverFilter_;
    KeyCapture* hoverKey_;
    QLabel* hoverNote_;
};

}  // namespace cd
