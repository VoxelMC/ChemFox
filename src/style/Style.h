#pragma once
// Styling language: parser, resolver and built-in styles.
//
//   style "slides" based on default
//     bond width: thick
//     text size: 16 pt
//     arrows
//       head: open
//
// One setting per line, plain words or exact values with units, `based on`
// inheritance, indented sections scope settings to one kind of object.

#include <QColor>
#include <QString>
#include <QStringList>
#include <vector>

namespace cd {

struct Style {
    QString name = "default";

    // Lengths are in points.
    double bondLength = 15.0;
    double lineWidth = 0.75;
    double boldWidth = 2.4;       // wide end of wedges
    double bondSpacing = 0.18;    // double-bond spacing as a fraction of bond length
    double margin = 1.8;          // gap between bond ends and labels
    double hashSpacing = 2.4;
    QString font = "Arial";
    double fontSize = 10.0;
    QColor color = Qt::black;     // bonds
    QColor textColor = Qt::black; // atom labels
    QColor background = Qt::white;
    bool atomColors = false;
    bool aromaticCircles = false;

    double arrowWidth = 0.75;
    double arrowHeadLength = 7.0;
    double arrowHeadWidth = 4.5;
    bool arrowHeadFilled = true;
    double arrowMinLength = 45.0;
    QColor arrowColor = Qt::black;
    double arrowTextGap = 3.6;    // clearance between an arrow's head and the text above/below it

    // Mechanism (curved) arrows. Unset (0 / invalid), they follow the reaction arrows: the same line, a
    // slimmer head. So a style that scales its arrows up (slides, posters) scales these too.
    double mechWidth = 0;
    double mechHeadLength = 0;
    double mechHeadWidth = 0;     // half the width of the head, like arrowHeadWidth
    QColor mechColor;
    double mechanismWidth() const { return mechWidth > 0 ? mechWidth : arrowWidth; }
    // The reaction arrows' head, half the size.
    double mechanismHeadLength() const { return mechHeadLength > 0 ? mechHeadLength : arrowHeadLength * 0.5; }
    double mechanismHeadWidth() const { return mechHeadWidth > 0 ? mechHeadWidth : arrowHeadWidth * 0.5; }
    QColor mechanismColor() const { return mechColor.isValid() ? mechColor : arrowColor; }

    QString annotationFont = "Arial";
    double annotationSize = 9.0;
    QColor annotationColor = Qt::black;

    QString numberFont = "Arial";
    double numberSize = 10.0;
    bool numberBold = true;
    QColor numberColor = Qt::black;
    double numberGap = 7.0;       // space between a structure and its name/number

    double schemeSpacing = 14.0;  // gap between objects in a scheme row
    double rowSpacing = 36.0;     // gap between wrapped rows
    double pageWidth = 520.0;     // wrap scheme rows at this width

    QColor formedBonds = QColor("#1f9d55");
    QColor brokenBonds = QColor("#d64545");
    QColor changedBonds = QColor("#d08b00");

    bool residueLabelBold = true;

    double spacingPt() const { return bondSpacing * bondLength; }
};

struct StyleIssue {
    int line = 0;
    QString setting;
    QString message;
};

namespace styles {

QStringList builtinNames();               // default, acs, slides, poster, dark
QString builtinText(const QString& name); // source text of a built-in style
QString userStyleDir();                   // where user .style files live
QStringList availableNames();             // built-ins plus user styles

// Source text for a named style (user file overrides nothing: built-ins win).
QString textFor(const QString& name);

// Parse style source and resolve inheritance. Unknown settings become issues (W401);
// the style still loads with defaults for anything it could not read.
Style resolve(const QString& text, std::vector<StyleIssue>* issues = nullptr);
Style resolveNamed(const QString& name, std::vector<StyleIssue>* issues = nullptr);

// Name declared in a style's header line, or empty.
QString declaredName(const QString& text);

// --- editing style source -------------------------------------------------------
// A style file as written: header plus its settings, without resolving inheritance.
// Keys are canonical ("bond width", not the alias "line width"); `section` is empty at top level.
struct Entry {
    QString section, key, value;
};
struct Source {
    QString name, base;
    std::vector<Entry> entries;  // settings this style sets itself (overrides of its base)
    QString value(const QString& section, const QString& key) const;
    bool has(const QString& section, const QString& key) const;
    void set(const QString& section, const QString& key, const QString& value);
    void unset(const QString& section, const QString& key);
};
Source parseSource(const QString& text);
// Standard layout: header, top-level settings, then sections in a fixed order, two-space indents.
QString writeSource(const Source& s);

}  // namespace styles
}  // namespace cd
