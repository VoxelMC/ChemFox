#pragma once
// Text helpers shared by chemistry and rendering: abbreviation labels,
// condensed-formula parsing and automatic sub/superscripts.

#include <QMap>
#include <QString>
#include <QStringList>
#include <vector>

namespace cd {

enum class Script { Normal, Sub, Super };
struct TextRun {
    QString text;
    Script script = Script::Normal;
    bool bold = false;
    bool italic = false;
};

namespace labels {

// SMILES fragment for a known abbreviation, written with a leading '*' as the
// attachment point ("OMe" -> "*OC"). Empty if unknown.
QString fragmentSmiles(const QString& label);

// Canonical spelling for a label typed in either orientation ("MeO" -> "OMe").
QString canonical(const QString& label);

// True if the text is just an element symbol (so it should set the element instead).
bool isElementSymbol(const QString& text);

// Split a label into display groups: "CO2Et" -> {"C", "O2", "Et"}.
QStringList tokens(const QString& label);

// Reversed orientation for bonds coming from the right: "CO2Et" -> "EtO2C".
QString flipped(const QString& label);

// Element counts for a condensed formula such as "CH2OH" or "N(CH3)2"; empty if it fails.
QMap<QString, int> parseCondensed(const QString& text);

// Digits in a label become subscripts.
std::vector<TextRun> labelRuns(const QString& label);

// Annotation text: subscript digits inside formula-like words (H2O, Pd(PPh3)4),
// superscript a trailing + or - on them. "rt, 2 h" stays as typed.
std::vector<TextRun> formulaRuns(const QString& text);

// formulaRuns plus light markup: **bold** and *italic* ("*trans*-cinnamic acid").
// An unmatched marker is kept as a literal asterisk.
std::vector<TextRun> markupRuns(const QString& text);

// Formula string with digits marked: "C9H8O4" -> runs.
std::vector<TextRun> hillRuns(const QString& formula);

const QStringList& knownAbbreviations();

// Custom groups: name -> SMILES with one '*' for the attachment point. The user's own (kept in
// groups.json) can replace built-ins; groups read from an opened file fill in names the user
// doesn't define, for the session only.
QMap<QString, QString> customGroups();
QMap<QString, QString> documentGroups();
void setCustomGroups(const QMap<QString, QString>& groups);
void addFileGroups(const QMap<QString, QString>& groups);
bool isBuiltin(const QString& name);
// Bumped whenever the group table changes, so caches of computed chemistry can be dropped.
int groupsGeneration();
// Why a custom group can't be used as given (empty if fine). The SMILES itself is checked by chem.
QString checkGroup(const QString& name, const QString& smiles);

}  // namespace labels

// Whole-line bold and italic for text boxes (right-click → Bold / Italic): each non-empty line is wrapped
// in markup, **bold**, *italic* or ***both***.
namespace textlines {
bool allBold(const QString& text);
bool allItalic(const QString& text);
QString restyle(const QString& text, bool bold, bool italic);
}  // namespace textlines
}  // namespace cd
