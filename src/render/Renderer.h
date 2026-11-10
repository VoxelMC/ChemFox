#pragma once
// Draws the model with a resolved style. The same code renders the screen and
// every export. World units here are points: model coordinates * bond length.

#include "core/model/Document.h"
#include "core/model/Labels.h"
#include "style/Style.h"

#include <QColor>
#include <QHash>
#include <QPainter>
#include <QSet>

namespace cd {

// Chemistry facts computed elsewhere (by core/chem) that affect drawing.
struct ChemOverlay {
    QHash<QString, int> implicitH;                           // atom id -> H count
    QHash<QString, std::vector<QStringList>> aromaticRings;  // molecule id -> rings
    QHash<QString, QString> cip;                             // atom id -> R/S, bond id -> E/Z
};

// Editor-only decorations, never part of exports.
struct Decorations {
    QString hoverId;          // atom, bond, arrow, text, or "num:<molId>"
    QSet<QString> selected;   // ids of atoms, bonds, molecules, arrows, texts
    QSet<QString> warned;     // ids that carry an active warning
    QString editingId;        // object whose text is being edited inline (hidden)
    QSet<QString> linked;     // atoms mapped to the hovered atom (mapping mode)
    bool forceMaps = false;   // show map numbers regardless of the document setting
};

struct FontSpec {
    QString family;
    double size = 10;
    bool bold = false;
    bool italic = false;
};

struct TextBlock {
    std::vector<TextRun> runs;
    QPointF origin;  // left end of the baseline, in points
};

struct AtomLabel {
    bool visible = false;
    std::vector<TextBlock> blocks;
    QRectF clip;     // bonds stop at this box (anchor + margin)
    QRectF bounds;
    QColor color;
    // A group label (OMe, NHBoc) colours only its anchor element; the rest uses `restColor`.
    // anchorLeft/anchorRight bound the anchor element horizontally, in points.
    bool splitColor = false;
    double anchorLeft = 0, anchorRight = 0;
    QColor restColor;
};

struct ArrowGeom {
    QPointF tail, head;  // after stretching to fit text
    std::vector<QPointF> path;  // tail, corners, head (points)
    std::vector<TextBlock> above, below;
    QRectF textRect;     // union of annotation blocks
    QRectF bounds;
};

// A mechanism arrow as drawn: a quadratic curve from `start` to `tip`, already shortened so it stops
// short of the atoms, bonds or lone pairs it joins. All in points.
struct CurvedGeom {
    bool ok = false;         // false when an end no longer exists
    QPointF a, b;            // where the ends attach (untrimmed): atom centre, bond midpoint, lone pair
    QPointF start, tip;      // the arc as drawn, trimmed short of what it joins
    bool isArc = true;       // a circular arc (hooks onto an atom of the arrow's own bond), else a cubic curve
    QPointF centre;          // ... the arc's circle (radius 0 / sweep 0: a straight line)
    double radius = 0, angle0 = 0, sweep = 0;
    QPointF c1, c2;          // ... the cubic curve's control points
    QPointF notch;           // where the line meets the head
    QPointF anchorFrom, anchorTo;  // what the ends attach to: the atom, the bond's middle, or the free point
    QPointF tipDir;          // unit direction of the head (the arc's direction where the line meets it)
    QPointF headTip;         // where the head's point is drawn (a hair from `tip`, along tipDir)
    double headLength = 0, headWidth = 0;  // the style's head, made smaller on a very short arrow
    QPointF apex;            // middle of the curve: the handle that reshapes it
    double bulge = 0;        // signed height / chord actually used (automatic or by hand)
    QPointF normal;          // unit side the bulge is measured along (+bulge bows this way)
    std::vector<QPointF> points;  // the curve sampled, for hit-testing
    QRectF bounds;
};

// A "+" drawn between two structures on one side of a reaction step.
struct PlusSign {
    QRectF rect;    // points
    QString arrow;  // the step's arrow
    QString after;  // the structure it follows
    QString id() const { return "plus:" + arrow + ":" + after; }
};

class Renderer {
public:
    explicit Renderer(const Style& style) : s_(style) {}
    const Style& style() const { return s_; }

    void draw(QPainter& p, const Document& d, const ChemOverlay& ov, const Decorations* deco = nullptr) const;

    QPointF toPt(QPointF model) const { return model * s_.bondLength; }
    QPointF toModel(QPointF pt) const { return pt / s_.bondLength; }

    AtomLabel atomLabel(const Molecule& m, const Atom& a, const ChemOverlay& ov) const;
    ArrowGeom arrowGeom(const Arrow& a, const Document* d = nullptr) const;
    QRectF moleculeRect(const Molecule& m, const ChemOverlay& ov) const;  // structure only
    QRectF numberRect(const Molecule& m, const ChemOverlay& ov) const;    // number + caption
    QRectF textRect(const TextBox& t) const;
    CurvedGeom curvedGeom(const CurvedArrow& c, const Document& d, const ChemOverlay& ov) const;
    // Where each of an atom's lone pairs is drawn (points), in lonePairDirections order.
    std::vector<QPointF> lonePairPoints(const Molecule& m, const Atom& a, const ChemOverlay& ov) const;
    FontSpec textFont(const TextBox& t) const;
    QRectF documentBounds(const Document& d, const ChemOverlay& ov) const;
    // The plus signs shown between structures of each step (removed ones left out).
    std::vector<PlusSign> plusSigns(const Document& d, const ChemOverlay& ov) const;

    // Text helpers, exposed for layout and inline editors.
    double runsWidth(const std::vector<TextRun>& runs, const FontSpec& f) const;
    void drawRuns(QPainter& p, const std::vector<TextRun>& runs, QPointF origin, const FontSpec& f,
                  const QColor& c) const;
    double capHeight(const FontSpec& f) const;
    FontSpec labelFont() const { return {s_.font, s_.fontSize}; }
    FontSpec annotationFont() const { return {s_.annotationFont, s_.annotationSize}; }
    FontSpec numberFont() const { return {s_.numberFont, s_.numberSize, s_.numberBold}; }

    QColor elementColor(const QString& el) const;

private:
    void drawMolecule(QPainter& p, const Molecule& m, const ChemOverlay& ov, const Decorations* deco,
                      const QHash<QString, int>& bondChanges, bool showMaps) const;
    void drawBond(QPainter& p, const Molecule& m, const Bond& b, const QHash<QString, AtomLabel>& labels,
                  bool inAromaticCircle, const QColor& color, double widthScale = 1.0) const;
    void drawArrow(QPainter& p, const Arrow& a, const Document& d, const Decorations* deco) const;
    void drawArrowHead(QPainter& p, QPointF tip, QPointF dir, int half, int filledMode = -1) const;  // half: 0 full, +1/-1 one barb; filledMode -1 = style
    void drawCurved(QPainter& p, const CurvedArrow& c, const Document& d, const ChemOverlay& ov, const Decorations* deco) const;
    void drawSchemeExtras(QPainter& p, const Document& d, const ChemOverlay& ov) const;
    QString stepLetter(const Document& d, const QString& arrowId) const;

    Style s_;
};

}  // namespace cd
