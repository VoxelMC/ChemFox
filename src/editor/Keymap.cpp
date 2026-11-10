#include "editor/Keymap.h"

#include "core/model/Labels.h"

#include <QDir>
#include <algorithm>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

namespace cd {

Keymap::Map Keymap::defaults() {
    Map m;
    auto& atom = m["atom"];
    for (const char* el : {"C", "N", "O", "S", "P", "F", "H"}) atom[el] = QString("element:") + el;
    atom["L"] = "element:Cl";
    atom["B"] = "element:Br";
    atom["I"] = "element:I";
    atom["Enter"] = "label";
    atom["+"] = "charge:+1";
    atom["="] = "charge:+1";
    atom["-"] = "charge:-1";
    atom["."] = "radical";
    atom["1"] = "grow:1";
    atom["2"] = "grow:2";
    atom["3"] = "grow:3";
    for (int n = 3; n <= 8; ++n) atom[QString("Shift+%1").arg(n)] = QString("ring:%1").arg(n);
    atom["R"] = "benzene";
    atom["Shift+N"] = "number";
    atom["Shift+C"] = "group:Ac";
    atom["E"] = "expand";

    auto& bond = m["bond"];
    bond["1"] = "order:1";
    bond["2"] = "order:2";
    bond["3"] = "order:3";
    bond["W"] = "stereo:wedge";
    bond["H"] = "stereo:hash";
    bond["Y"] = "stereo:wavy";
    bond["F"] = "flip";
    for (int n = 3; n <= 8; ++n) bond[QString("Shift+%1").arg(n)] = QString("ring:%1").arg(n);
    bond["R"] = "benzene";
    bond["Shift+N"] = "number";

    auto& arrow = m["arrow"];
    arrow["Enter"] = "annotate";
    arrow["K"] = "kind";

    m["text"]["Enter"] = "edit";
    m["number"]["Enter"] = "edit";

    auto& empty = m["empty"];
    for (const char* el : {"C", "N", "O", "S", "P", "F", "H"}) empty[el] = QString("element:") + el;
    empty["L"] = "element:Cl";
    empty["B"] = "element:Br";
    empty["I"] = "element:I";
    for (int n = 3; n <= 8; ++n) empty[QString("Shift+%1").arg(n)] = QString("ring:%1").arg(n);
    empty["R"] = "benzene";
    empty["Shift+C"] = "group:Ac";
    empty["A"] = "arrow";
    empty["T"] = "text";

    auto& any = m["any"];
    any["Delete"] = "delete";
    any["Backspace"] = "delete";
    any["Tab"] = "cycle";
    any["Escape"] = "escape";
    // Tools, chosen not to clash with element letters.
    any["V"] = "tool:select";
    any["G"] = "tool:chain";
    any["Shift+A"] = "tool:arrow";
    any["Shift+B"] = "tool:bent";
    any["Shift+T"] = "tool:text";
    return m;
}

Keymap::Keymap() : map_(defaults()) {}

QString Keymap::path() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/keymap.json";
}

void Keymap::load() {
    map_ = defaults();
    QFile f(path());
    if (!f.exists()) {
        QJsonObject root;
        for (auto t = map_.begin(); t != map_.end(); ++t) {
            QJsonObject o;
            for (auto k = t->begin(); k != t->end(); ++k) o[k.key()] = k.value();
            root[t.key()] = o;
        }
        if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(root).toJson());
        return;
    }
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto t = root.begin(); t != root.end(); ++t) {
        QJsonObject o = t.value().toObject();
        for (auto k = o.begin(); k != o.end(); ++k) {
            if (k.value().toString().isEmpty()) map_[t.key()].remove(k.key());  // "" unbinds
            else map_[t.key()][k.key()] = k.value().toString();
        }
    }
}

void Keymap::setMap(const Map& m) { map_ = m; }

bool Keymap::save() const {
    // Everything bound, plus "" for each default key that is no longer bound (an explicit unbind).
    Map d = defaults();
    QSet<QString> targets;
    for (auto it = d.begin(); it != d.end(); ++it) targets.insert(it.key());
    for (auto it = map_.begin(); it != map_.end(); ++it) targets.insert(it.key());
    QJsonObject root;
    for (const auto& t : targets) {
        QJsonObject o;
        const auto dt = d.value(t), mt = map_.value(t);
        for (auto k = dt.begin(); k != dt.end(); ++k) if (!mt.contains(k.key())) o[k.key()] = "";
        for (auto k = mt.begin(); k != mt.end(); ++k) o[k.key()] = k.value();
        root[t] = o;
    }
    QFile f(path());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(root).toJson());
    return true;
}

std::vector<std::pair<QString, QString>> Keymap::availableActions() {
    std::vector<std::pair<QString, QString>> out;
    const QStringList elements = {"C", "N", "O", "S", "P", "F", "Cl", "Br", "I", "H", "B", "Si", "Se", "Na", "K", "Li", "Mg"};
    for (const auto& el : elements) out.push_back({"atom", "element:" + el});
    for (const char* a : {"label", "charge:+1", "charge:-1", "radical", "grow:1", "grow:2", "grow:3"}) out.push_back({"atom", a});
    for (int n = 3; n <= 8; ++n) out.push_back({"atom", QString("ring:%1").arg(n)});
    for (const char* a : {"benzene", "number", "expand"}) out.push_back({"atom", a});
    for (const char* a : {"order:1", "order:2", "order:3", "stereo:wedge", "stereo:hash", "stereo:wavy", "flip"})
        out.push_back({"bond", a});
    for (int n = 3; n <= 8; ++n) out.push_back({"bond", QString("ring:%1").arg(n)});
    for (const char* a : {"benzene", "number"}) out.push_back({"bond", a});
    out.push_back({"arrow", "annotate"});
    out.push_back({"arrow", "kind"});
    out.push_back({"text", "edit"});
    out.push_back({"number", "edit"});
    for (const auto& el : elements) out.push_back({"empty", "element:" + el});
    for (int n = 3; n <= 8; ++n) out.push_back({"empty", QString("ring:%1").arg(n)});
    for (const char* a : {"benzene", "arrow", "text"}) out.push_back({"empty", a});
    // Groups: the common built-ins and every custom one, on atoms and on empty canvas.
    QStringList groups = {"Ac", "Me", "Et", "iPr", "tBu", "Ph", "Bn", "Bz", "Boc", "Cbz", "Ts", "TBS",
                          "OH", "OMe", "OAc", "NH2", "CO2H", "CO2Me", "CO2Et", "CHO", "CN", "CF3", "NO2"};
    for (const auto& g : labels::customGroups().keys()) if (!groups.contains(g)) groups << g;
    for (const char* t : {"atom", "empty"})
        for (const auto& g : groups) out.push_back({t, "group:" + g});
    for (const char* a : {"delete", "cycle", "escape", "tool:select", "tool:chain", "tool:arrow", "tool:bent", "tool:text", "mapmode"})
        out.push_back({"any", a});
    return out;
}

QString Keymap::describe(const QString& action) {
    const QString verb = action.section(':', 0, 0), arg = action.section(':', 1);
    static const QHash<QString, QString> plain = {
        {"label", "Type a label"}, {"radical", "Toggle radical"}, {"benzene", "Benzene ring"},
        {"number", "Compound name / number"}, {"flip", "Flip wedge or double-bond side"},
        {"annotate", "Edit conditions"}, {"kind", "Next arrow kind"}, {"edit", "Edit"}, {"arrow", "New arrow"},
        {"text", "New text"}, {"delete", "Delete"}, {"cycle", "Next overlapping target"}, {"escape", "Cancel / deselect"},
        {"mapmode", "Toggle mapping mode"}, {"expand", "Expand / collapse group label"}};
    if (plain.contains(verb)) return plain[verb];
    if (verb == "element") return "Element " + arg;
    if (verb == "charge") return arg.startsWith('+') ? "Increase charge" : "Decrease charge";
    if (verb == "grow") return QStringList{"", "Grow single bond", "Grow double bond", "Grow triple bond"}.value(arg.toInt(), action);
    if (verb == "order") return QStringList{"", "Single bond", "Double bond", "Triple bond"}.value(arg.toInt(), action);
    if (verb == "stereo") return arg == "wedge" ? "Wedge" : arg == "hash" ? "Hash" : "Wavy";
    if (verb == "ring") return arg + "-membered ring";
    if (verb == "group") return "Group " + arg + (labels::customGroups().contains(arg) ? " (custom)" : "");
    if (verb == "tool") return QString("%1 tool").arg(arg.left(1).toUpper() + arg.mid(1));
    return action;
}
QStringList Keymap::keysFor(const QString& target, const QString& action) const {
    QStringList keys;
    const auto bound = map_.value(target);
    for (auto it = bound.begin(); it != bound.end(); ++it) if (it.value() == action) keys << it.key();
    std::sort(keys.begin(), keys.end(), [](const QString& a, const QString& b) {
        return a.size() != b.size() ? a.size() < b.size() : a < b;
    });
    return keys;
}

QString Keymap::action(const QString& target, const QString& key) const {
    QString a = map_.value(target).value(key);
    if (a.isEmpty()) a = map_.value("any").value(key);
    return a;
}

QString Keymap::keyName(const QKeyEvent* e) {
    const auto mods = e->modifiers();
    if (mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) return {};
    const bool shift = mods & Qt::ShiftModifier;
    const QString sp = shift ? "Shift+" : "";
    const int k = e->key();
    const quint32 vk = e->nativeVirtualKey();

    // Digits by physical key so Shift+3 stays "Shift+3" on any layout. Windows virtual-key codes only:
    // on macOS the same numbers mean other keys (0x30 is Tab, 0x33 is Backspace).
#ifdef Q_OS_WIN
    if (!(mods & Qt::KeypadModifier) && vk >= '0' && vk <= '9') return sp + QChar(char(vk));
#else
    Q_UNUSED(vk);
#endif
    if (k >= Qt::Key_0 && k <= Qt::Key_9)
        return ((mods & Qt::KeypadModifier) ? QString() : sp) + QChar(char('0' + k - Qt::Key_0));
    if (k >= Qt::Key_A && k <= Qt::Key_Z) return sp + QChar(char('A' + k - Qt::Key_A));
    switch (k) {
    case Qt::Key_Return:
    case Qt::Key_Enter: return "Enter";
    case Qt::Key_Delete: return "Delete";
    case Qt::Key_Backspace: return "Backspace";
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return "Tab";
    case Qt::Key_Escape: return "Escape";
    case Qt::Key_Plus: return "+";
    case Qt::Key_Minus:
    case Qt::Key_Underscore: return "-";
    case Qt::Key_Equal: return "=";
    case Qt::Key_Period: return ".";
    // Shifted digit symbols when the virtual key is unavailable (US layout).
    case Qt::Key_Exclam: return "Shift+1";
    case Qt::Key_At: return "Shift+2";
    case Qt::Key_NumberSign: return "Shift+3";
    case Qt::Key_Dollar: return "Shift+4";
    case Qt::Key_Percent: return "Shift+5";
    case Qt::Key_AsciiCircum: return "Shift+6";
    case Qt::Key_Ampersand: return "Shift+7";
    case Qt::Key_Asterisk: return "Shift+8";
    default: break;
    }
    return e->text();
}

}  // namespace cd
