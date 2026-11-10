#include "editor/Groups.h"

#include "core/model/Labels.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace cd::groups {

QString path() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/groups.json";
}

void load() {
    QMap<QString, QString> g;
    QFile f(path());
    if (f.open(QIODevice::ReadOnly)) {
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = o.begin(); it != o.end(); ++it) {
            QString smi = it.value().toString().trimmed();
            if (labels::checkGroup(it.key(), smi).isEmpty()) g[it.key()] = smi;
        }
    }
    labels::setCustomGroups(g);
}

bool save(const QMap<QString, QString>& groups) {
    labels::setCustomGroups(groups);
    QJsonObject o;
    for (auto it = groups.begin(); it != groups.end(); ++it) o[it.key()] = it.value();
    QFile f(path());
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson());
    return true;
}

}  // namespace cd::groups
