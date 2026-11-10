#include "core/model/Labels.h"

#include <QHash>
#include <QRegularExpression>
#include <algorithm>

namespace cd::labels {

namespace {

int& generationRef() { static int g = 0; return g; }
int generation() { return generationRef(); }

const QHash<QString, QString>& builtins() {
    static const QHash<QString, QString> t = {
        {"Me", "*C"}, {"Et", "*CC"}, {"Pr", "*CCC"}, {"nPr", "*CCC"}, {"iPr", "*C(C)C"},
        {"Bu", "*CCCC"}, {"nBu", "*CCCC"}, {"tBu", "*C(C)(C)C"}, {"iBu", "*CC(C)C"},
        {"Ph", "*c1ccccc1"}, {"Bn", "*Cc1ccccc1"}, {"Bz", "*C(=O)c1ccccc1"}, {"Ac", "*C(C)=O"},
        {"Piv", "*C(=O)C(C)(C)C"}, {"PMB", "*Cc1ccc(OC)cc1"},
        {"Boc", "*C(=O)OC(C)(C)C"}, {"Cbz", "*C(=O)OCc1ccccc1"},
        {"Fmoc", "*C(=O)OCC1c2ccccc2-c2ccccc21"},
        {"Ts", "*S(=O)(=O)c1ccc(C)cc1"}, {"Ms", "*S(C)(=O)=O"}, {"Tf", "*S(=O)(=O)C(F)(F)F"},
        {"TMS", "*[Si](C)(C)C"}, {"TBS", "*[Si](C)(C)C(C)(C)C"}, {"TBDMS", "*[Si](C)(C)C(C)(C)C"},
        {"TIPS", "*[Si](C(C)C)(C(C)C)C(C)C"},
        {"OH", "*O"}, {"SH", "*S"}, {"NH2", "*N"}, {"NH", "*N"},
        {"OMe", "*OC"}, {"OEt", "*OCC"}, {"OPh", "*Oc1ccccc1"}, {"OBn", "*OCc1ccccc1"},
        {"OAc", "*OC(C)=O"}, {"OBz", "*OC(=O)c1ccccc1"}, {"OPiv", "*OC(=O)C(C)(C)C"},
        {"OTs", "*OS(=O)(=O)c1ccc(C)cc1"}, {"OMs", "*OS(C)(=O)=O"}, {"OTf", "*OS(=O)(=O)C(F)(F)F"},
        {"OTMS", "*O[Si](C)(C)C"}, {"OTBS", "*O[Si](C)(C)C(C)(C)C"},
        {"OTIPS", "*O[Si](C(C)C)(C(C)C)C(C)C"}, {"OPMB", "*OCc1ccc(OC)cc1"}, {"OtBu", "*OC(C)(C)C"},
        {"NHMe", "*NC"}, {"NMe2", "*N(C)C"}, {"NEt2", "*N(CC)CC"}, {"NHAc", "*NC(C)=O"},
        {"NHBoc", "*NC(=O)OC(C)(C)C"}, {"NHCbz", "*NC(=O)OCc1ccccc1"},
        {"NHFmoc", "*NC(=O)OCC1c2ccccc2-c2ccccc21"}, {"NHTs", "*NS(=O)(=O)c1ccc(C)cc1"},
        {"NBoc", "*N(C(=O)OC(C)(C)C)"}, {"NHPh", "*Nc1ccccc1"},
        {"CO2H", "*C(=O)O"}, {"COOH", "*C(=O)O"}, {"CO2Me", "*C(=O)OC"}, {"CO2Et", "*C(=O)OCC"},
        {"CO2tBu", "*C(=O)OC(C)(C)C"}, {"CO2Bn", "*C(=O)OCc1ccccc1"}, {"COMe", "*C(C)=O"},
        {"CONH2", "*C(N)=O"}, {"CHO", "*C=O"}, {"CN", "*C#N"}, {"NO2", "*[N+](=O)[O-]"},
        {"N3", "*N=[N+]=[N-]"}, {"CF3", "*C(F)(F)F"}, {"CCl3", "*C(Cl)(Cl)Cl"}, {"OCF3", "*OC(F)(F)F"},
        {"SO3H", "*S(=O)(=O)O"}, {"SO2Me", "*S(C)(=O)=O"}, {"SMe", "*SC"}, {"SPh", "*Sc1ccccc1"},
        {"CH3", "*C"}, {"CH2", "*C"}, {"OCH3", "*OC"}, {"CH2OH", "*CO"}, {"CH2CH3", "*CC"},
        {"SiMe3", "*[Si](C)(C)C"}, {"PPh3", "*[P+](c1ccccc1)(c1ccccc1)c1ccccc1"},
        {"PPh2", "*P(c1ccccc1)c1ccccc1"}, {"Bpin", "*B1OC(C)(C)C(C)(C)O1"},
        {"B(OH)2", "*B(O)O"}, {"SnBu3", "*[Sn](CCCC)(CCCC)CCCC"},
        // Coenzyme A without its thiol sulfur, as drawn in "S–CoA" thioesters.
        {"CoA", "*CCNC(=O)CCNC(=O)[C@H](O)C(C)(C)COP(=O)(O)OP(=O)(O)OC[C@H]1O[C@@H](n2cnc3c(N)ncnc32)"
                "[C@H](O)[C@@H]1OP(=O)(O)O"},
    };
    return t;
}

// The user's own groups (groups.json) and ones met in opened files, which never replace the user's.
QMap<QString, QString>& userGroups() { static QMap<QString, QString> g; return g; }
QMap<QString, QString>& fileGroups() { static QMap<QString, QString> g; return g; }

// Built-ins, then file groups, then the user's: the later wins.
const QHash<QString, QString>& table() {
    static QHash<QString, QString> t;
    static int builtAt = -1;
    if (builtAt != generation()) {
        t = builtins();
        for (auto it = fileGroups().begin(); it != fileGroups().end(); ++it) t[it.key()] = it.value();
        for (auto it = userGroups().begin(); it != userGroups().end(); ++it) t[it.key()] = it.value();
        builtAt = generation();
    }
    return t;
}

// Multi-letter group names that must stay as one display token.
const QStringList& groupNames() {
    static QStringList g;
    static int builtAt = -1;
    if (builtAt != generation()) {
        g = {"CoA", "TBDMS", "TIPS", "Fmoc", "TBS", "TMS", "PMB", "Piv", "Boc", "Cbz", "nPr", "iPr",
             "nBu", "tBu", "iBu", "pin", "Ph", "Bn", "Bz", "Ac", "Ts", "Ms", "Tf", "Me", "Et", "Pr", "Bu"};
        // Custom names are single tokens too, except ones that are plain formulas ("CH2Cl").
        for (const auto& n : fileGroups().keys() + userGroups().keys())
            if (!g.contains(n) && parseCondensed(n).isEmpty()) g << n;
        std::sort(g.begin(), g.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
        builtAt = generation();
    }
    return g;
}

const QStringList& elementSymbols() {
    static const QStringList e = {
        "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl",
        "Ar", "K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As",
        "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In",
        "Sn", "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd", "Sm", "Eu", "Gd", "Tb", "Dy",
        "Ho", "Er", "Tm", "Yb", "Lu", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb",
        "Bi", "Po", "At", "Rn", "U"};
    return e;
}

}  // namespace

const QStringList& knownAbbreviations() {
    static QStringList k;
    static int builtAt = -1;
    if (builtAt != generation()) {
        k = table().keys();
        k.sort();
        builtAt = generation();
    }
    return k;
}

QMap<QString, QString> customGroups() { return userGroups(); }
QMap<QString, QString> documentGroups() { return fileGroups(); }
bool isBuiltin(const QString& name) { return builtins().contains(name); }

void setCustomGroups(const QMap<QString, QString>& groups) {
    userGroups() = groups;
    ++generationRef();
}

void addFileGroups(const QMap<QString, QString>& groups) {
    bool changed = false;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        if (userGroups().contains(it.key()) || builtins().value(it.key()) == it.value()) continue;
        if (fileGroups().value(it.key()) != it.value()) { fileGroups()[it.key()] = it.value(); changed = true; }
    }
    if (changed) ++generationRef();
}

int groupsGeneration() { return generation(); }

QString checkGroup(const QString& name, const QString& smiles) {
    static const QRegularExpression okName("^[A-Za-z][A-Za-z0-9()]*$");
    if (!okName.match(name).hasMatch()) return "Names start with a letter and use letters, digits and brackets.";
    if (isElementSymbol(name)) return name + " is an element symbol.";
    QString s = smiles.trimmed();
    int stars = s.count('*');
    if (stars != 1) return "Mark the attachment point with exactly one * (e.g. *C(C)=O for acetyl).";
    return {};
}

bool isElementSymbol(const QString& text) { return elementSymbols().contains(text); }

QStringList tokens(const QString& label) {
    QStringList out;
    int i = 0;
    const int n = label.size();
    while (i < n) {
        bool matched = false;
        for (const auto& g : groupNames()) {
            if (label.mid(i, g.size()) == g) {
                // "Pr"/"Bu" etc. must not swallow an element prefix like "P" in "PPh3".
                int j = i + g.size();
                while (j < n && label[j].isDigit()) ++j;
                out << label.mid(i, j - i);
                i = j;
                matched = true;
                break;
            }
        }
        if (matched) continue;
        int j = i;
        if (label[i] == '(') {
            int depth = 0;
            for (; j < n; ++j) {
                if (label[j] == '(') ++depth;
                if (label[j] == ')' && --depth == 0) { ++j; break; }
            }
        } else if (label[i].isUpper()) {
            ++j;
            if (j < n && label[j].isLower() && isElementSymbol(label.mid(i, 2))) ++j;
        } else {
            ++j;
        }
        while (j < n && label[j].isDigit()) ++j;
        out << label.mid(i, j - i);
        i = j;
    }
    return out;
}

QString flipped(const QString& label) {
    QStringList t = tokens(label);
    std::reverse(t.begin(), t.end());
    return t.join("");
}

QString canonical(const QString& label) {
    if (table().contains(label) || isElementSymbol(label)) return label;
    QString f = flipped(label);
    if (table().contains(f)) return f;
    if (label == "HOOC") return "COOH";
    return label;
}

QString fragmentSmiles(const QString& label) { return table().value(canonical(label)); }

QMap<QString, int> parseCondensed(const QString& text) {
    QMap<QString, int> counts;
    std::vector<QMap<QString, int>> stack(1);
    int i = 0;
    const int n = text.size();
    auto readNum = [&](int& k) {
        int v = 0, s = k;
        while (k < n && text[k].isDigit()) v = v * 10 + text[k++].digitValue();
        return k == s ? 1 : v;
    };
    while (i < n) {
        QChar c = text[i];
        if (c == '(') { stack.emplace_back(); ++i; continue; }
        if (c == ')') {
            if (stack.size() < 2) return {};
            ++i;
            int mult = readNum(i);
            auto top = stack.back();
            stack.pop_back();
            for (auto it = top.begin(); it != top.end(); ++it) stack.back()[it.key()] += it.value() * mult;
            continue;
        }
        if (!c.isUpper()) return {};
        QString sym(c);
        if (i + 1 < n && text[i + 1].isLower() && isElementSymbol(text.mid(i, 2))) sym = text.mid(i, 2);
        if (!isElementSymbol(sym)) return {};
        i += sym.size();
        stack.back()[sym] += readNum(i);
    }
    if (stack.size() != 1) return {};
    return stack.back();
}

std::vector<TextRun> labelRuns(const QString& label) {
    std::vector<TextRun> runs;
    for (QChar c : label) {
        Script s = c.isDigit() ? Script::Sub : Script::Normal;
        if (!runs.empty() && runs.back().script == s) runs.back().text += c;
        else runs.push_back({QString(c), s});
    }
    return runs;
}

std::vector<TextRun> hillRuns(const QString& formula) { return labelRuns(formula); }

std::vector<TextRun> markupRuns(const QString& text) {
    // Split into styled segments first, then subscript formulas inside each segment.
    struct Seg { QString text; bool bold, italic; };
    std::vector<Seg> segs;
    bool bold = false, italic = false;
    QString cur;
    auto closes = [&](int from, const QString& marker) { return text.indexOf(marker, from) >= 0; };
    for (int i = 0; i < text.size();) {
        if (text.mid(i, 2) == "**" && (bold || closes(i + 2, "**"))) {
            segs.push_back({cur, bold, italic});
            cur.clear();
            bold = !bold;
            i += 2;
        } else if (text[i] == '*' && text.mid(i, 2) != "**" && (italic || closes(i + 1, "*"))) {
            segs.push_back({cur, bold, italic});
            cur.clear();
            italic = !italic;
            i += 1;
        } else {
            cur += text[i++];
        }
    }
    segs.push_back({cur, bold, italic});
    std::vector<TextRun> out;
    for (const auto& s : segs) {
        if (s.text.isEmpty()) continue;
        // Bold text is a name (enzymes such as C4H or 4CL), never a formula to subscript.
        auto runs = s.bold ? std::vector<TextRun>{{s.text}} : formulaRuns(s.text);
        for (auto r : runs) {
            r.bold = s.bold;
            r.italic = s.italic;
            out.push_back(r);
        }
    }
    return out;
}

std::vector<TextRun> formulaRuns(const QString& text) {
    std::vector<TextRun> runs;
    auto push = [&](const QString& s, Script sc) {
        if (s.isEmpty()) return;
        if (!runs.empty() && runs.back().script == sc) runs.back().text += s;
        else runs.push_back({s, sc});
    };
    static const QRegularExpression word("[^\\s,;]+");
    int last = 0;
    auto it = word.globalMatch(text);
    while (it.hasNext()) {
        auto m = it.next();
        push(text.mid(last, m.capturedStart() - last), Script::Normal);
        QString w = m.captured();
        last = m.capturedEnd();
        // Formula-like: starts with an uppercase letter or '(' and contains a letter-digit pair.
        static const QRegularExpression formulaLike("^[A-Z(\\[][A-Za-z0-9()\\[\\]+\\-·]*$");
        static const QRegularExpression hasPair("[A-Za-z)\\]]\\d");
        static const QRegularExpression longNumber("\\d{3,}");  // P450, not a formula count
        bool isFormula = formulaLike.match(w).hasMatch() && !longNumber.match(w).hasMatch() &&
                         (hasPair.match(w).hasMatch() || w.endsWith('+') || w.endsWith('-'));
        if (!isFormula) { push(w, Script::Normal); continue; }
        for (int k = 0; k < w.size(); ++k) {
            QChar c = w[k];
            bool afterAtom = k > 0 && (w[k - 1].isLetter() || w[k - 1] == ')' || w[k - 1] == ']' ||
                                       (w[k - 1].isDigit() && !runs.empty() && runs.back().script == Script::Sub));
            if (c.isDigit() && afterAtom) push(QString(c), Script::Sub);
            else if ((c == '+' || c == '-') && k == w.size() - 1 && k > 0) push(QString(c), Script::Super);
            else push(QString(c), Script::Normal);
        }
    }
    push(text.mid(last), Script::Normal);
    return runs;
}

}  // namespace cd::labels

namespace cd::textlines {
namespace {
bool wrapped(const QString& l, const QString& m) {
    return l.size() >= 2 * m.size() && l.startsWith(m) && l.endsWith(m);
}
// Peel whole-line markup off one line.
QString plain(QString l, bool* bold, bool* italic) {
    *bold = *italic = false;
    if (wrapped(l, "**")) { *bold = true; l = l.mid(2, l.size() - 4); }
    if (wrapped(l, "*") && !wrapped(l, "**")) { *italic = true; l = l.mid(1, l.size() - 2); }
    return l;
}
template <typename F>
bool every(const QString& text, F pred) {
    bool any = false;
    for (const auto& l : text.split('\n')) {
        if (l.isEmpty()) continue;
        bool b, i;
        plain(l, &b, &i);
        if (!pred(b, i)) return false;
        any = true;
    }
    return any;
}
}  // namespace

bool allBold(const QString& text) { return every(text, [](bool b, bool) { return b; }); }
bool allItalic(const QString& text) { return every(text, [](bool, bool i) { return i; }); }

QString restyle(const QString& text, bool bold, bool italic) {
    QStringList out;
    for (const auto& line : text.split('\n')) {
        if (line.isEmpty()) { out << line; continue; }
        bool b, i;
        QString l = plain(line, &b, &i);
        if (italic) l = "*" + l + "*";
        if (bold) l = "**" + l + "**";
        out << l;
    }
    return out.join('\n');
}

}  // namespace cd::textlines
