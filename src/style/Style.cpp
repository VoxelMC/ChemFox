#include "style/Style.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>
#include <functional>

namespace cd::styles {

namespace {

const char* kDefault = R"(style "default"
  bond length: 15 pt
  bond width: 0.75 pt
  bold width: 2.4 pt
  bond spacing: 18%
  margin: 1.8 pt
  hash spacing: 2.4 pt
  text font: Arial
  text size: 10 pt
  color: black
  background: white
  atom colors: no
  aromatic circles: no
  formed bonds: #1f9d55
  broken bonds: #d64545
  changed bonds: #d08b00
  page width: 520 pt

  arrows
    width: 0.75 pt
    head: filled
    head length: 7 pt
    head width: 4.5 pt
    min length: 45 pt
    text gap: 3.6 pt

  annotations
    font: Arial
    size: 9 pt

  numbers
    font: Arial
    size: 10 pt
    weight: bold
    gap: 7 pt

  scheme
    spacing: 14 pt
    row spacing: 36 pt
)";

// ChemDraw ACS Document 1996 settings.
const char* kAcs = R"(style "acs" based on default
  bond length: 14.4 pt
  bond width: 0.6 pt
  bold width: 2.0 pt
  bond spacing: 18%
  margin: 1.6 pt
  hash spacing: 2.5 pt
  text font: Arial
  text size: 10 pt

  arrows
    width: 0.6 pt
    head length: 6 pt
    head width: 4 pt
    text gap: 3.2 pt

  annotations
    size: 8 pt

  numbers
    size: 10 pt
)";

const char* kSlides = R"(style "slides" based on default
  bond length: 26 pt
  bond width: thick
  bold width: 4.5 pt
  hash spacing: 3.5 pt
  margin: 2.5 pt
  text font: Arial
  text size: 16 pt
  atom colors: yes
  page width: 900 pt

  arrows
    width: thick
    head: filled
    head length: 11 pt
    head width: 7 pt
    min length: 70 pt
    text gap: 5.6 pt

  annotations
    size: 14 pt

  numbers
    size: 16 pt
    gap: 11 pt

  scheme
    spacing: 22 pt
    row spacing: 50 pt
)";

const char* kPoster = R"(style "poster" based on slides
  bond length: 38 pt
  bond width: 2.2 pt
  bold width: 7 pt
  hash spacing: 5 pt
  margin: 3.5 pt
  text size: 24 pt
  page width: 1400 pt

  arrows
    width: 2.2 pt
    head length: 16 pt
    head width: 10 pt
    min length: 100 pt
    text gap: 8 pt

  annotations
    size: 20 pt

  numbers
    size: 24 pt
    gap: 17 pt
)";

const char* kDark = R"(style "dark" based on default
  background: #1d1f23
  color: #e6e6e6
  text color: #e6e6e6
  atom colors: yes
  formed bonds: #4ade80
  broken bonds: #f87171
  changed bonds: #fbbf24

  arrows
    color: #e6e6e6

  annotations
    color: #cfcfcf

  numbers
    color: #ffffff
)";

QString norm(QString s) { return s.simplified().toLower(); }

bool parseLength(const QString& v, double& out, const QHash<QString, double>& words) {
    QString s = norm(v);
    if (words.contains(s)) { out = words[s]; return true; }
    static const QRegularExpression re("^(-?\\d+(?:\\.\\d+)?)\\s*(pt|mm|cm|in|px|%)?$");
    auto m = re.match(s);
    if (!m.hasMatch()) return false;
    double x = m.captured(1).toDouble();
    QString u = m.captured(2);
    if (u == "mm") x *= 72.0 / 25.4;
    else if (u == "cm") x *= 72.0 / 2.54;
    else if (u == "in") x *= 72.0;
    else if (u == "px") x *= 0.75;
    out = x;
    return true;
}

bool parseColor(const QString& v, QColor& out) {
    QColor c = QColor::fromString(v.trimmed());
    if (!c.isValid()) return false;
    out = c;
    return true;
}

bool parseBool(const QString& v, bool& out) {
    QString s = norm(v);
    if (s == "yes" || s == "on" || s == "true") { out = true; return true; }
    if (s == "no" || s == "off" || s == "false") { out = false; return true; }
    return false;
}

const QHash<QString, double> kWidthWords = {{"hairline", 0.3}, {"thin", 0.5}, {"normal", 0.75},
                                            {"medium", 1.0}, {"thick", 1.6}, {"heavy", 2.4}};
const QHash<QString, double> kSizeWords = {{"tiny", 7}, {"small", 8}, {"normal", 10},
                                           {"large", 14}, {"huge", 20}};
const QHash<QString, double> kLengthWords = {{"short", 12}, {"normal", 15}, {"long", 20}};
const QHash<QString, double> kNoWords = {};

using Setter = std::function<bool(Style&, const QString&)>;

Setter len(double Style::*f, const QHash<QString, double>& words = kNoWords) {
    return [f, &words](Style& s, const QString& v) { return parseLength(v, s.*f, words); };
}
Setter col(QColor Style::*f) {
    return [f](Style& s, const QString& v) { return parseColor(v, s.*f); };
}
Setter boo(bool Style::*f) {
    return [f](Style& s, const QString& v) { return parseBool(v, s.*f); };
}
Setter str(QString Style::*f) {
    return [f](Style& s, const QString& v) { s.*f = v.trimmed(); return !v.trimmed().isEmpty(); };
}

// Settings by "section/name"; the empty section is the top level.
const QHash<QString, Setter>& setters() {
    static const QHash<QString, Setter> t = {
        {"/bond length", len(&Style::bondLength, kLengthWords)},
        {"/bond width", len(&Style::lineWidth, kWidthWords)},
        {"/line width", len(&Style::lineWidth, kWidthWords)},
        {"/bold width", len(&Style::boldWidth, kWidthWords)},
        {"/wedge width", len(&Style::boldWidth, kWidthWords)},
        {"/bond spacing", [](Style& s, const QString& v) {
             double x;
             if (!parseLength(v, x, kNoWords)) return false;
             s.bondSpacing = v.contains('%') || x > 1 ? x / 100.0 : x;
             return true;
         }},
        {"/margin", len(&Style::margin)},
        {"/hash spacing", len(&Style::hashSpacing)},
        {"/text font", str(&Style::font)},
        {"/font", str(&Style::font)},
        {"/text size", len(&Style::fontSize, kSizeWords)},
        {"/font size", len(&Style::fontSize, kSizeWords)},
        {"/color", [](Style& s, const QString& v) { return parseColor(v, s.color); }},
        {"/text color", col(&Style::textColor)},
        {"/background", col(&Style::background)},
        {"/atom colors", boo(&Style::atomColors)},
        {"/aromatic circles", boo(&Style::aromaticCircles)},
        {"/arrow head", [](Style& s, const QString& v) {
             QString n = norm(v);
             if (n != "filled" && n != "open") return false;
             s.arrowHeadFilled = n == "filled";
             return true;
         }},
        {"/arrow width", len(&Style::arrowWidth, kWidthWords)},
        {"/formed bonds", col(&Style::formedBonds)},
        {"/broken bonds", col(&Style::brokenBonds)},
        {"/changed bonds", col(&Style::changedBonds)},
        {"/page width", len(&Style::pageWidth)},

        {"arrows/width", len(&Style::arrowWidth, kWidthWords)},
        {"arrows/head", [](Style& s, const QString& v) {
             QString n = norm(v);
             if (n != "filled" && n != "open") return false;
             s.arrowHeadFilled = n == "filled";
             return true;
         }},
        {"arrows/head length", len(&Style::arrowHeadLength)},
        {"arrows/head width", len(&Style::arrowHeadWidth)},
        {"arrows/min length", len(&Style::arrowMinLength)},
        {"arrows/color", col(&Style::arrowColor)},
        {"arrows/text gap", len(&Style::arrowTextGap)},

        {"mechanism/width", len(&Style::mechWidth, kWidthWords)},
        {"mechanism/head length", len(&Style::mechHeadLength)},
        {"mechanism/head width", len(&Style::mechHeadWidth)},
        {"mechanism/color", col(&Style::mechColor)},

        {"annotations/font", str(&Style::annotationFont)},
        {"annotations/size", len(&Style::annotationSize, kSizeWords)},
        {"annotations/color", col(&Style::annotationColor)},

        {"numbers/font", str(&Style::numberFont)},
        {"numbers/size", len(&Style::numberSize, kSizeWords)},
        {"numbers/color", col(&Style::numberColor)},
        {"numbers/gap", len(&Style::numberGap)},
        {"numbers/weight", [](Style& s, const QString& v) {
             QString n = norm(v);
             if (n != "bold" && n != "normal") return false;
             s.numberBold = n == "bold";
             return true;
         }},
        {"numbers/label", [](Style& s, const QString& v) {
             QString n = norm(v);
             if (n != "bold" && n != "normal") return false;
             s.numberBold = n == "bold";
             return true;
         }},

        {"scheme/spacing", len(&Style::schemeSpacing)},
        {"scheme/row spacing", len(&Style::rowSpacing)},
        {"scheme/page width", len(&Style::pageWidth)},

        {"residues/label", [](Style& s, const QString& v) {
             QString n = norm(v);
             if (n != "bold" && n != "normal") return false;
             s.residueLabelBold = n == "bold";
             return true;
         }},
        {"text/font", str(&Style::font)},
        {"text/size", len(&Style::fontSize, kSizeWords)},
        {"text/color", col(&Style::textColor)},
    };
    return t;
}

const QStringList kSections = {"arrows", "mechanism", "annotations", "numbers", "scheme", "residues", "text"};

struct Header {
    QString name, base;
    bool ok = false;
};

Header parseHeader(const QString& text) {
    static const QRegularExpression re(
        "^\\s*style\\s+(?:\"([^\"]+)\"|(\\S+))(?:\\s+based\\s+on\\s+(?:\"([^\"]+)\"|(\\S+)))?\\s*$",
        QRegularExpression::CaseInsensitiveOption);
    for (const QString& line : text.split('\n')) {
        QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith('#')) continue;
        auto m = re.match(t);
        if (!m.hasMatch()) return {};
        Header h;
        h.name = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
        h.base = m.captured(3).isEmpty() ? m.captured(4) : m.captured(3);
        h.ok = true;
        return h;
    }
    return {};
}

void apply(Style& s, const QString& text, std::vector<StyleIssue>* issues, int depth) {
    Header h = parseHeader(text);
    if (h.ok && !h.base.isEmpty() && depth < 8) {
        QString baseText = textFor(h.base);
        if (baseText.isEmpty()) {
            if (issues) issues->push_back({1, "based on", QString("Unknown base style \"%1\"").arg(h.base)});
        } else {
            apply(s, baseText, issues ? (depth == 0 ? issues : nullptr) : nullptr, depth + 1);
        }
    }
    if (h.ok) s.name = h.name;

    QString section;
    int sectionIndent = -1, childIndent = -1;  // a section's settings: indented at least as much as its first one
    const QStringList lines = text.split('\n');
    bool seenHeader = false;
    for (int i = 0; i < lines.size(); ++i) {
        QString raw = lines[i];
        raw.replace('\t', "  ");
        QString t = raw.trimmed();
        // Inline comments need "# " so hex colours (#1f9d55) survive.
        static const QRegularExpression inlineComment("\\s#\\s.*$");
        t.remove(inlineComment);
        t = t.trimmed();
        if (t.isEmpty() || t.startsWith('#')) continue;
        if (!seenHeader && t.toLower().startsWith("style")) { seenHeader = true; continue; }
        seenHeader = true;
        int indent = 0;
        while (indent < raw.size() && raw[indent] == ' ') ++indent;
        if (sectionIndent >= 0) {
            if (childIndent < 0 && indent > 0) childIndent = indent;
            if (indent == 0 || (childIndent >= 0 && indent < childIndent)) { section.clear(); sectionIndent = -1; childIndent = -1; }
        }

        int colon = t.indexOf(':');
        if (colon < 0) {
            QString name = norm(t);
            if (kSections.contains(name)) {
                section = name;
                sectionIndent = indent;
                childIndent = -1;
            } else if (issues) {
                issues->push_back({i + 1, t, QString("Unknown section \"%1\"").arg(t)});
            }
            continue;
        }
        QString key = norm(t.left(colon));
        QString value = t.mid(colon + 1).trimmed();
        auto it = setters().find(section + "/" + key);
        if (it == setters().end()) {
            if (issues)
                issues->push_back({i + 1, key, section.isEmpty()
                                                   ? QString("Unknown style setting \"%1\"").arg(key)
                                                   : QString("Unknown setting \"%1\" in %2").arg(key, section)});
            continue;
        }
        if (!(*it)(s, value) && issues)
            issues->push_back({i + 1, key, QString("Could not read \"%1\" for %2").arg(value, key)});
    }
}

}  // namespace

QStringList builtinNames() { return {"default", "acs", "slides", "poster", "dark"}; }

QString builtinText(const QString& name) {
    QString n = name.toLower();
    if (n == "default") return kDefault;
    if (n == "acs") return kAcs;
    if (n == "slides") return kSlides;
    if (n == "poster") return kPoster;
    if (n == "dark") return kDark;
    return {};
}

QString userStyleDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/styles";
    QDir().mkpath(dir);
    return dir;
}

QStringList availableNames() {
    QStringList names = builtinNames();
    for (const QString& f : QDir(userStyleDir()).entryList({"*.style"}, QDir::Files)) {
        QString n = f.chopped(6);
        if (!names.contains(n, Qt::CaseInsensitive)) names << n;
    }
    return names;
}

QString textFor(const QString& name) {
    QString t = builtinText(name);
    if (!t.isEmpty()) return t;
    QFile f(userStyleDir() + "/" + name + ".style");
    if (f.open(QIODevice::ReadOnly)) return QString::fromUtf8(f.readAll());
    return {};
}

Style resolve(const QString& text, std::vector<StyleIssue>* issues) {
    Style s;
    apply(s, text, issues, 0);
    return s;
}

Style resolveNamed(const QString& name, std::vector<StyleIssue>* issues) {
    QString t = textFor(name);
    if (t.isEmpty()) t = kDefault;
    return resolve(t, issues);
}

QString declaredName(const QString& text) { return parseHeader(text).name; }

namespace {

// Aliases map to one canonical spelling so the editor sees each setting once.
QPair<QString, QString> canonicalKey(QString section, QString key) {
    static const QHash<QString, QString> alias = {
        {"/line width", "/bond width"}, {"/wedge width", "/bold width"}, {"/font", "/text font"},
        {"/font size", "/text size"}, {"/arrow head", "arrows/head"}, {"/arrow width", "arrows/width"},
        {"text/font", "/text font"}, {"text/size", "/text size"}, {"text/color", "/text color"},
        {"numbers/label", "numbers/weight"}, {"scheme/page width", "/page width"},
    };
    QString full = alias.value(section + "/" + key, section + "/" + key);
    int slash = full.indexOf('/');
    return {full.left(slash), full.mid(slash + 1)};
}

}  // namespace

QString Source::value(const QString& section, const QString& key) const {
    for (const auto& e : entries) if (e.section == section && e.key == key) return e.value;
    return {};
}
bool Source::has(const QString& section, const QString& key) const {
    for (const auto& e : entries) if (e.section == section && e.key == key) return true;
    return false;
}
void Source::set(const QString& section, const QString& key, const QString& value) {
    for (auto& e : entries)
        if (e.section == section && e.key == key) { e.value = value; return; }
    entries.push_back({section, key, value});
}
void Source::unset(const QString& section, const QString& key) {
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const Entry& e) { return e.section == section && e.key == key; }),
                  entries.end());
}

Source parseSource(const QString& text) {
    Source s;
    Header h = parseHeader(text);
    s.name = h.name;
    s.base = h.base;
    QString section;
    int sectionIndent = -1, childIndent = -1;  // a section's settings: indented at least as much as its first one
    bool seenHeader = false;
    static const QRegularExpression inlineComment("\\s#\\s.*$");
    for (QString raw : text.split('\n')) {
        raw.replace('\t', "  ");
        QString t = raw.trimmed();
        t.remove(inlineComment);
        t = t.trimmed();
        if (t.isEmpty() || t.startsWith('#')) continue;
        if (!seenHeader && t.toLower().startsWith("style")) { seenHeader = true; continue; }
        seenHeader = true;
        int indent = 0;
        while (indent < raw.size() && raw[indent] == ' ') ++indent;
        if (sectionIndent >= 0) {
            if (childIndent < 0 && indent > 0) childIndent = indent;
            if (indent == 0 || (childIndent >= 0 && indent < childIndent)) { section.clear(); sectionIndent = -1; childIndent = -1; }
        }
        int colon = t.indexOf(':');
        if (colon < 0) {
            section = norm(t);
            sectionIndent = indent;
            childIndent = -1;
            continue;
        }
        auto [sec, key] = canonicalKey(section, norm(t.left(colon)));
        s.set(sec, key, t.mid(colon + 1).trimmed());
    }
    return s;
}

QString writeSource(const Source& s) {
    QString out = QString("style \"%1\"").arg(s.name.isEmpty() ? QString("custom") : s.name);
    if (!s.base.isEmpty()) out += QString(" based on %1").arg(s.base);
    out += "\n";
    for (const auto& e : s.entries)
        if (e.section.isEmpty()) out += QString("  %1: %2\n").arg(e.key, e.value);
    // Known sections first in a fixed order, then any others in the order they appear.
    QStringList order = kSections;
    for (const auto& e : s.entries)
        if (!e.section.isEmpty() && !order.contains(e.section)) order << e.section;
    for (const auto& sec : order) {
        QString block;
        for (const auto& e : s.entries)
            if (e.section == sec) block += QString("    %1: %2\n").arg(e.key, e.value);
        if (!block.isEmpty()) out += "\n  " + sec + "\n" + block;
    }
    return out;
}

}  // namespace cd::styles
