#pragma once
// Document checks with per-instance, per-document and global silencing.
// Warnings never block anything; they show as a marker and a panel entry.

#include "core/chem/Chem.h"
#include "core/model/Document.h"

#include <QHash>
#include <QSet>
#include <vector>

namespace cd {

struct StyleIssue;

struct Warning {
    QString code;      // W101
    QString objectId;  // molecule, arrow or "style"
    QString subId;     // atom/bond id, or style line
    QString message;
    QString key() const { return code + "@" + objectId + (subId.isEmpty() ? QString() : "/" + subId); }
    // Id to highlight / navigate to: the atom or bond if any, else the object.
    QString target() const { return subId.isEmpty() || objectId == "style" ? objectId : subId; }
};

namespace warnings {

QString title(const QString& code);   // "Unusual valence"
QStringList allCodes();

// Run every check. `analyses` maps molecule id -> chem analysis.
std::vector<Warning> check(const Document& d, const QHash<QString, chem::Analysis>& analyses,
                           const std::vector<StyleIssue>& styleIssues);

// Drop warnings silenced in the document or globally.
std::vector<Warning> active(const std::vector<Warning>& all, const Document& d, const QSet<QString>& globalCodes);

// Global (app-wide) silenced codes, stored in app settings.
QSet<QString> globalSilenced();
void setGlobalSilenced(const QSet<QString>& codes);

}  // namespace warnings
}  // namespace cd
