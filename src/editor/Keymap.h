#pragma once
// Hover-and-type keymap. Keys map to actions per target (atom, bond, arrow,
// text, number, empty canvas, any). Rebindable through keymap.json in the
// app data folder, which is written with the defaults on first run.

#include <QHash>
#include <QKeyEvent>
#include <QString>
#include <vector>

namespace cd {

class Keymap {
public:
    using Map = QHash<QString, QHash<QString, QString>>;  // target -> key -> action

    Keymap();
    static Map defaults();
    const Map& map() const { return map_; }
    void setMap(const Map& m);
    // Write the current bindings (and explicit unbinds of defaults) to keymap.json.
    bool save() const;
    // Every action a key can be bound to, per target, bound by default or not.
    static std::vector<std::pair<QString, QString>> availableActions();
    static QString describe(const QString& action);
    // Load user overrides on top of the defaults; writes the default file if missing.
    void load();
    QString path() const;

    // Action for a key on a target, falling back to the "any" table.
    QString action(const QString& target, const QString& key) const;

    // Canonical key name for an event ("C", "Shift+6", "+", "Enter"); empty for shortcuts.
    static QString keyName(const QKeyEvent* e);

private:
    Map map_;
};

}  // namespace cd
