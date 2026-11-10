#pragma once
// The user's custom groups, kept in groups.json in the app data folder as {"name": "SMILES"}.

#include <QMap>
#include <QString>

namespace cd::groups {

QString path();
// Read groups.json into the label table (labels::setCustomGroups). Missing file = no groups.
void load();
// Write and apply. Returns false if the file couldn't be written (the groups still apply).
bool save(const QMap<QString, QString>& groups);

}  // namespace cd::groups
