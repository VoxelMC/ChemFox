#include "render/Renderer.h"

#include "core/model/Geometry.h"
#include "core/model/Mapping.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace cd {

using namespace geom;

namespace {

// Fonts are measured and drawn at a fixed reference pixel size and scaled, so
// layout is identical on screen, SVG, PDF and PNG regardless of device DPI.
constexpr double kRef = 100.0;

QFont refFont(const FontSpec& f, Script sc) {
    QFont font(f.family);
    font.setPixelSize(int(kRef));
    font.setBold(f.bold);
    font.setItalic(f.italic);
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setStyleStrategy(QFont::PreferAntialias);
    Q_UNUSED(sc);
    return font;
}

double scriptScale(Script s) { return s == Script::Normal ? 1.0 : 0.7; }
double scriptShift(Script s, double size) {
    if (s == Script::Sub) return 0.28 * size;
    if (s == Script::Super) return -0.42 * size;
    return 0;
}

QPointF clipToRect(QPointF from, QPointF to, const QRectF& r) {
    // Point where the segment from `from` (inside r) leaves r.
    if (!r.contains(from)) return from;
    QPointF d = to - from;
    double t = 1.0;
    if (d.x() > 1e-9) t = std::min(t, (r.right() - from.x()) / d.x());
    if (d.x() < -1e-9) t = std::min(t, (r.left() - from.x()) / d.x());
    if (d.y() > 1e-9) t = std::min(t, (r.bottom() - from.y()) / d.y());
    if (d.y() < -1e-9) t = std::min(t, (r.top() - from.y()) / d.y());
    return from + d * std::clamp(t, 0.0, 1.0);
}

// A circular arc (or, with sweep 0, a straight line) from p to q: centre c, radius r, starting at angle a0
// (radians, y down) and turning through `sweep`.
struct Arc {
    QPointF p, q, c;
    double r = 0, a0 = 0, sweep = 0;
    QPointF at(double t) const {
        if (sweep == 0) return p + (q - p) * t;
        const double a = a0 + sweep * t;
        return c + QPointF(std::cos(a), std::sin(a)) * r;
    }
    double length() const { return sweep == 0 ? len(q - p) : r * std::abs(sweep); }
    QPointF tangent(double t) const {  // unit direction of travel
        if (sweep == 0) return unit(q - p);
        const double a = a0 + sweep * t;
        return QPointF(-std::sin(a), std::cos(a)) * (sweep > 0 ? 1.0 : -1.0);
    }
    Arc sub(double t0, double t1) const {
        Arc s = *this;
        s.p = at(t0);
        s.q = at(t1);
        if (sweep != 0) { s.a0 = a0 + sweep * t0; s.sweep = sweep * (t1 - t0); }
        return s;
    }
    void addTo(QPainterPath& path) const {
        path.moveTo(p);
        if (sweep == 0) { path.lineTo(q); return; }
        // Qt measures angles in degrees, counter-clockwise on screen.
        path.arcTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), -a0 * 180 / M_PI, -sweep * 180 / M_PI);
    }
    // The arc from p to q whose middle stands `b` times the chord away from it, along the unit normal n.
    static Arc make(QPointF p, QPointF q, QPointF n, double b) {
        Arc arc{p, q, {}};
        const double L = len(q - p);
        if (L < 1e-9 || std::abs(b) < 1e-4) return arc;
        const double h = b * L, ah = std::abs(h), sgn = h > 0 ? 1 : -1;
        arc.r = (L * L / 4 + ah * ah) / (2 * ah);
        const QPointF apex = (p + q) / 2 + n * h;
        arc.c = apex - n * (sgn * arc.r);
        auto ang = [&](QPointF x) { return std::atan2(x.y() - arc.c.y(), x.x() - arc.c.x()); };
        const double a0 = ang(p), aApex = ang(apex);
        const double d = std::remainder(ang(q) - a0, 2 * M_PI), alt = d - std::copysign(2 * M_PI, d);
        auto off = [&](double sw) { return std::abs(std::remainder(a0 + sw / 2 - aApex, 2 * M_PI)); };
        arc.a0 = a0;
        arc.sweep = off(d) <= off(alt) ? d : alt;  // the way round that passes through the middle
        return arc;
    }
};

// A cubic Bezier curve.
struct Cubic {
    QPointF p0, p1, p2, p3;
    QPointF at(double t) const {
        const double u = 1 - t;
        return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
    }
    QPointF tangent(double t) const {
        const double u = 1 - t;
        const QPointF d = (p1 - p0) * (3 * u * u) + (p2 - p1) * (6 * u * t) + (p3 - p2) * (3 * t * t);
        return len(d) > 1e-9 ? unit(d) : unit(p3 - p0);
    }
    double length() const {
        double l = 0;
        for (int i = 0; i < 32; ++i) l += len(at((i + 1) / 32.0) - at(i / 32.0));
        return l;
    }
    Cubic sub(double t0, double t1) const {
        auto split = [](const Cubic& c, double t, bool first) {
            const QPointF a = c.p0 + (c.p1 - c.p0) * t, b = c.p1 + (c.p2 - c.p1) * t, d = c.p2 + (c.p3 - c.p2) * t;
            const QPointF ab = a + (b - a) * t, bd = b + (d - b) * t, m = ab + (bd - ab) * t;
            return first ? Cubic{c.p0, a, ab, m} : Cubic{m, bd, d, c.p3};
        };
        if (t1 <= 1e-9) return {p0, p0, p0, p0};
        const Cubic h = split(*this, t1, true);
        return split(h, t0 / t1, false);
    }
    void addTo(QPainterPath& path) const { path.moveTo(p0); path.cubicTo(p1, p2, p3); }
};

// The line a mechanism arrow follows: a circular arc (hooks onto an atom's own bond) or a cubic curve.
struct Curve {
    bool isArc = true;
    Arc arc;
    Cubic cb;
    Curve() = default;
    Curve(const Arc& a) : isArc(true), arc(a) {}
    Curve(const Cubic& c) : isArc(false), cb(c) {}
    QPointF at(double t) const { return isArc ? arc.at(t) : cb.at(t); }
    QPointF tangent(double t) const { return isArc ? arc.tangent(t) : cb.tangent(t); }
    double length() const { return isArc ? arc.length() : cb.length(); }
    Curve sub(double t0, double t1) const { return isArc ? Curve(arc.sub(t0, t1)) : Curve(cb.sub(t0, t1)); }
    // The parameter a distance `d` back from the end, measured along the curve (a cubic's parameter doesn't
    // move at an even pace, so a fraction of its length would land in the wrong place).
    double tBack(double d) const {
        const double total = length();
        if (total < 1e-9) return 0;
        if (isArc) return std::max(0.0, 1.0 - d / total);
        double acc = 0, t = 1;
        QPointF prev = at(1);
        for (int i = 255; i >= 0 && acc < d; --i) {
            const double tt = i / 256.0;
            const QPointF q = at(tt);
            const double step = len(prev - q);
            if (acc + step >= d) return tt + (t - tt) * ((acc + step - d) / std::max(step, 1e-12));
            acc += step;
            prev = q;
            t = tt;
        }
        return 0;
    }
    void addTo(QPainterPath& path) const { if (isArc) arc.addTo(path); else cb.addTo(path); }
};

// The curve chemists draw from one place to another: it rises a little more steeply off its tail and
// settles more gently into its head, standing about `h` points off the line between them along n.
Cubic mechanismCurve(QPointF a, QPointF b, QPointF n, double h) {
    const QPointF v = b - a;
    return {a, a + v * 0.22 + n * (1.5 * h), b - v * 0.32 + n * (1.1 * h), b};
}

// Where the notch of a filled head sits, as a fraction of its length back from the point (the same
// proportion as reaction arrows).
constexpr double kNotch = 0.8;

const QColor kHover(66, 133, 244, 110);
const QColor kSelect(66, 133, 244, 70);
const QColor kWarn(232, 152, 0);
const QColor kLinked(240, 160, 30, 130);

}  // namespace

// ---------------------------------------------------------------------------
// Text

double Renderer::runsWidth(const std::vector<TextRun>& runs, const FontSpec& f) const {
    double w = 0;
    for (const auto& r : runs) {
        FontSpec rf = f;
        rf.bold = f.bold || r.bold;
        rf.italic = f.italic || r.italic;
        QFontMetricsF fm(refFont(rf, r.script));
        w += fm.horizontalAdvance(r.text) * f.size * scriptScale(r.script) / kRef;
    }
    return w;
}

double Renderer::capHeight(const FontSpec& f) const {
    QFontMetricsF fm(refFont(f, Script::Normal));
    return fm.capHeight() * f.size / kRef;
}

void Renderer::drawRuns(QPainter& p, const std::vector<TextRun>& runs, QPointF origin, const FontSpec& f,
                        const QColor& c) const {
    double x = origin.x();
    p.save();
    p.setPen(c);
    for (const auto& r : runs) {
        FontSpec rf = f;
        rf.bold = f.bold || r.bold;
        rf.italic = f.italic || r.italic;
        QFont font = refFont(rf, r.script);
        double k = f.size * scriptScale(r.script) / kRef;
        QFontMetricsF fm(font);
        p.save();
        p.translate(x, origin.y() + scriptShift(r.script, f.size));
        p.scale(k, k);
        p.setFont(font);
        p.drawText(QPointF(0, 0), r.text);
        p.restore();
        x += fm.horizontalAdvance(r.text) * k;
    }
    p.restore();
}

QColor Renderer::elementColor(const QString& el) const {
    if (!s_.atomColors) return s_.textColor;
    bool dark = s_.background.lightnessF() < 0.5;
    static const QHash<QString, QPair<QColor, QColor>> pal = {
        {"N", {QColor("#2342c8"), QColor("#7b9cff")}}, {"O", {QColor("#d01c1c"), QColor("#ff7b7b")}},
        {"S", {QColor("#b08a00"), QColor("#f2d04b")}}, {"P", {QColor("#d26a00"), QColor("#ffad5c")}},
        {"F", {QColor("#1d8f2e"), QColor("#7be08b")}}, {"Cl", {QColor("#1d8f2e"), QColor("#7be08b")}},
        {"Br", {QColor("#9a2a2a"), QColor("#e48b6b")}}, {"I", {QColor("#7a1fa2"), QColor("#d29bf0")}},
        {"B", {QColor("#c06a6a"), QColor("#ffb5b5")}}, {"Si", {QColor("#8a6d3b"), QColor("#e0c08a")}},
    };
    auto it = pal.find(el);
    if (it == pal.end()) return s_.textColor;
    return dark ? it->second : it->first;
}

// ---------------------------------------------------------------------------
// Atom labels

AtomLabel Renderer::atomLabel(const Molecule& m, const Atom& a, const ChemOverlay& ov) const {
    AtomLabel L;
    const QStringList nbs = m.neighbors(a.id);
    const int deg = nbs.size();
    const bool plain = a.label.isEmpty();
    if (plain && a.el == "C" && deg > 0 && !m.showCarbons && a.isotope == 0) return L;  // implicit carbon
    L.visible = true;

    const FontSpec f = labelFont();
    const double cap = capHeight(f);
    const QPointF c = toPt(a.pos());

    // Bonds mostly to the right -> text extends to the left (flipped orientation).
    double sx = 0;
    for (const auto& id : nbs) sx += m.atom(id)->x - a.x;
    const bool flip = deg > 0 && sx > 0.1;
    QPointF freeDir = deg ? freeDirection(m, a.id) : QPointF(1, 0);

    std::vector<TextRun> main;
    std::vector<TextRun> prefix;  // text drawn before the anchor element
    QString anchorText;

    if (plain) {
        int h = ov.implicitH.value(a.id, 0);
        anchorText = a.el;
        std::vector<TextRun> hRuns;
        if (h > 0) hRuns = {{"H"}};
        if (h > 1) hRuns.push_back({QString::number(h), Script::Sub});
        bool hFirst = deg == 0 ? QStringList{"O", "S", "Se", "F", "Cl", "Br", "I"}.contains(a.el) : flip;
        bool hVertical = deg >= 2 && std::abs(freeDir.y()) > 0.8;
        // An isotope's mass number sits just before the element, as a superscript (13C, 2H).
        std::vector<TextRun> iso;
        if (a.isotope > 0) iso = {{QString::number(a.isotope), Script::Super}};
        if (!hRuns.empty() && !hVertical && hFirst) {
            prefix = hRuns;
            prefix.insert(prefix.end(), iso.begin(), iso.end());
            main = prefix;
            main.push_back({a.el});
        } else {
            prefix = iso;
            main = iso;
            main.push_back({a.el});
            if (!hVertical) main.insert(main.end(), hRuns.begin(), hRuns.end());
        }
        if (hVertical && !hRuns.empty()) {
            // H goes above or below the element, in the free direction.
            double y = freeDir.y() > 0 ? c.y() + cap / 2 + cap * 1.35 : c.y() - cap / 2 - cap * 0.35;
            L.blocks.push_back({hRuns, QPointF(c.x() - runsWidth({{"H"}}, f) / 2, y)});
        }
    } else {
        QString text = flip ? labels::flipped(a.label) : a.label;
        QStringList tok = labels::tokens(text);
        QString anchorTok = tok.isEmpty() ? text : (flip ? tok.back() : tok.front());
        anchorText = anchorTok;
        while (anchorText.size() > 1 && anchorText.back().isDigit()) anchorText.chop(1);
        main = labels::labelRuns(text);
        if (flip) prefix = labels::labelRuns(text.left(text.size() - anchorTok.size()));
    }
    // Position: centre the anchor element on the atom.
    double prefixW = runsWidth(prefix, f);
    double anchorW = runsWidth({{anchorText}}, f);
    QPointF origin(c.x() - prefixW - anchorW / 2, c.y() + cap / 2);
    L.blocks.insert(L.blocks.begin(), {main, origin});

    if (a.charge != 0) {
        // The charge goes top-right of the label, unless a bond leaves that way: then the first free corner
        // of top-left, bottom-right, bottom-left (it used to sit on top of such a bond).
        const QString q = (std::abs(a.charge) > 1 ? QString::number(std::abs(a.charge)) : QString()) +
                          (a.charge > 0 ? "+" : QString(QChar(0x2212)));
        const std::vector<TextRun> qRuns{{q, Script::Super}};
        const double qw = runsWidth(qRuns, f);
        const double left = origin.x(), right = origin.x() + runsWidth(main, f);
        const double top = origin.y() - cap * 0.85, low = origin.y() + cap * 0.15;  // centre heights of the charge
        const QPointF corners[] = {{right + qw / 2, top}, {left - qw / 2, top}, {right + qw / 2, low}, {left - qw / 2, low}};
        QPointF centre = corners[0];
        for (const QPointF& k : corners) {
            const QPointF dir = unit(k - c);
            bool clear = true;
            for (const auto& nb : nbs) clear &= dot(unit(toPt(m.atom(nb)->pos()) - c), dir) < std::cos(M_PI * 40 / 180);
            if (clear) { centre = k; break; }
        }
        // A superscript's centre sits about (qw/2, 0.85 cap) up and right of its block origin.
        L.blocks.push_back({qRuns, centre - QPointF(qw / 2, -cap * 0.85)});
    }

    const double mg = s_.margin;
    L.clip = QRectF(origin.x() + prefixW - mg, c.y() - cap / 2 - mg, anchorW + 2 * mg, cap + 2 * mg);
    QRectF bounds;
    for (const auto& b : L.blocks) {
        double w = runsWidth(b.runs, f);
        bounds |= QRectF(b.origin.x(), b.origin.y() - cap - 0.45 * f.size * 0.7, w, cap + 0.45 * f.size);
    }
    L.bounds = bounds;
    L.color = plain ? elementColor(a.el) : elementColor(anchorText);
    if (!plain && L.color != s_.textColor) {
        // Only the anchor element (the O of OMe) takes the element colour; Me stays in the text colour.
        L.splitColor = true;
        L.anchorLeft = origin.x() + prefixW;
        L.anchorRight = L.anchorLeft + anchorW;
        L.restColor = s_.textColor;
    }
    return L;
}

// ---------------------------------------------------------------------------
// Bonds

void Renderer::drawBond(QPainter& p, const Molecule& m, const Bond& b, const QHash<QString, AtomLabel>& labels,
                        bool inAromaticCircle, const QColor& color, double widthScale) const {
    const Atom* A = m.atom(b.a);
    const Atom* B = m.atom(b.b);
    if (!A || !B) return;
    QPointF pa = toPt(A->pos()), pb = toPt(B->pos());
    auto ia = labels.constFind(b.a), ib = labels.constFind(b.b);
    const AtomLabel* la = ia != labels.constEnd() ? &ia.value() : nullptr;
    const AtomLabel* lb = ib != labels.constEnd() ? &ib.value() : nullptr;
    auto clipA = [&](QPointF from, QPointF to) {
        return la && la->visible ? clipToRect(from, to, la->clip) : from;
    };
    auto clipB = [&](QPointF from, QPointF to) {
        return lb && lb->visible ? clipToRect(from, to, lb->clip) : from;
    };
    QPointF a = clipA(pa, pb), e = clipB(pb, pa);
    QPointF dir = unit(pb - pa);
    QPointF n = perp(dir);
    const double w = s_.lineWidth * widthScale;

    QPen pen(color, w, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (b.order == 1 || inAromaticCircle) {
        switch (b.stereo) {
        case BondStereo::Wedge: {
            QPolygonF poly;
            double hw = s_.boldWidth / 2;
            poly << a << e + n * hw << e - n * hw;
            p.setBrush(color);
            p.setPen(QPen(color, w * 0.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolygon(poly);
            return;
        }
        case BondStereo::Hash: {
            double L = len(e - a);
            int count = std::max(2, int(L / s_.hashSpacing));
            p.setPen(QPen(color, w, Qt::SolidLine, Qt::FlatCap));
            for (int i = 1; i <= count; ++i) {
                double t = double(i) / count;
                QPointF c = a + (e - a) * t;
                double hw = std::max(w, s_.boldWidth * t) / 2;
                p.drawLine(c + n * hw, c - n * hw);
            }
            return;
        }
        case BondStereo::Wavy: {
            QPainterPath path(a);
            double L = len(e - a);
            int waves = std::max(2, int(std::round(L / (s_.hashSpacing * 2.2))));
            double amp = s_.boldWidth * 0.45;
            const int steps = waves * 12;
            for (int i = 1; i <= steps; ++i) {
                double t = double(i) / steps;
                path.lineTo(a + (e - a) * t + n * (amp * std::sin(t * waves * 2 * M_PI)));
            }
            p.drawPath(path);
            return;
        }
        default:
            p.drawLine(a, e);
            return;
        }
    }

    const double sp = s_.spacingPt();
    if (b.order == 3) {
        p.drawLine(a, e);
        p.drawLine(clipA(pa + n * sp, pb + n * sp), clipB(pb + n * sp, pa + n * sp));
        p.drawLine(clipA(pa - n * sp, pb - n * sp), clipB(pb - n * sp, pa - n * sp));
        return;
    }

    int side = doubleBondSide(m, b);
    if (side == 0) {
        QPointF o = n * (sp / 2);
        p.drawLine(clipA(pa + o, pb + o), clipB(pb + o, pa + o));
        p.drawLine(clipA(pa - o, pb - o), clipB(pb - o, pa - o));
        return;
    }
    p.drawLine(a, e);
    QPointF o = n * (sp * side);
    QPointF s1 = pa + o, s2 = pb + o;
    // Shorten the inner line at unlabelled ends; labelled ends clip to the label.
    double trim = sp * 0.9;
    bool aLab = la && la->visible, bLab = lb && lb->visible;
    QPointF i1 = aLab ? clipA(s1, s2) : (m.neighbors(b.a).size() > 1 ? s1 + dir * trim : s1);
    QPointF i2 = bLab ? clipB(s2, s1) : (m.neighbors(b.b).size() > 1 ? s2 - dir * trim : s2);
    p.drawLine(i1, i2);
}

// ---------------------------------------------------------------------------
// Molecules

QRectF Renderer::moleculeRect(const Molecule& m, const ChemOverlay& ov) const {
    QRectF r;
    for (const auto& a : m.atoms) {
        QPointF c = toPt(a.pos());
        QRectF ar(c - QPointF(s_.boldWidth / 2, s_.boldWidth / 2), QSizeF(s_.boldWidth, s_.boldWidth));
        r = r.isNull() ? ar : r.united(ar);
        AtomLabel L = atomLabel(m, a, ov);
        if (L.visible) r = r.united(L.bounds);
        if (a.lonePairs > 0) {
            const double k = s_.fontSize * 0.3;
            for (const auto& q : lonePairPoints(m, a, ov)) r = r.united(QRectF(q - QPointF(k, k), QSizeF(2 * k, 2 * k)));
        }
    }
    return r;
}

std::vector<QPointF> Renderer::lonePairPoints(const Molecule& m, const Atom& a, const ChemOverlay& ov) const {
    std::vector<QPointF> out;
    if (a.lonePairs <= 0) return out;
    const QPointF c = toPt(a.pos());
    const AtomLabel L = atomLabel(m, a, ov);
    // The side the label writes its hydrogens (or the rest of a group) on is taken, like a bond: OH puts its
    // pairs above and below the O, not out past the H.
    std::vector<QPointF> taken;
    if (L.visible) {
        const double more = s_.fontSize * 0.4;  // a written H or group, not a charge or the box's own slack
        if (L.bounds.left() < L.clip.left() - more) taken.push_back({-1, 0});
        if (L.bounds.right() > L.clip.right() + more) taken.push_back({1, 0});
    }
    std::vector<QPointF> dirs = lonePairDirections(m, a.id, a.lonePairs, taken);
    if (m.neighbors(a.id).isEmpty() && L.visible) {
        // A lone atom: keep off the side its hydrogens are written on (HO⁻ has its H on the left).
        const bool textLeft = (c.x() - L.bounds.left()) > (L.bounds.right() - c.x()) + 1;
        std::vector<QPointF> order = {{0, -1}, {0, 1}};
        if (textLeft) order.insert(order.end(), {{1, 0}, {-1, 0}});
        else order.insert(order.end(), {{-1, 0}, {1, 0}});
        dirs.clear();
        for (int i = 0; i < a.lonePairs; ++i) dirs.push_back(order[i % 4]);
    }
    if (!L.visible && a.charge != 0 && !m.neighbors(a.id).isEmpty()) {
        // The charge sits in the widest gap: lay out one pair more and leave out the one nearest it.
        const QPointF fd = freeDirection(m, a.id);
        dirs = lonePairDirections(m, a.id, a.lonePairs + 1);
        auto nearest = std::max_element(dirs.begin(), dirs.end(), [&](QPointF p1, QPointF p2) { return dot(p1, fd) < dot(p2, fd); });
        dirs.erase(nearest);
    }
    if (L.visible && !m.neighbors(a.id).isEmpty()) {
        // On a lettered atom the pairs sit square to the letter, above, below or beside it (as drawn by hand),
        // each on the free side nearest where the gaps put it.
        std::vector<QPointF> blocked = taken;
        for (const auto& nb : m.neighbors(a.id)) blocked.push_back(unit(m.atom(nb)->pos() - a.pos()));
        const QPointF sides[] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        std::vector<QPointF> used;
        for (auto& dir : dirs) {
            QPointF pick = dir;
            double bestDot = -2;
            for (const auto& sd : sides) {
                bool free = true;
                for (const auto& b : blocked) free &= dot(sd, b) < 0.8;
                for (const auto& u2 : used) free &= dot(sd, u2) < 0.5;
                if (free && dot(sd, dir) > bestDot) { bestDot = dot(sd, dir); pick = sd; }
            }
            if (bestDot > -2) dir = pick;
            used.push_back(dir);
        }
    }
    for (const auto& dir : dirs) {
        // Against the element itself (the O of OH, not the whole label), clear of the rest of the label;
        // a little way off a bare atom.
        if (L.visible && L.clip.contains(c)) {
            QPointF q = clipToRect(c, c + dir * 1000.0, L.clip) + dir * (s_.fontSize * 0.12);
            for (int i = 0; i < 12 && L.bounds.contains(q) && !L.clip.contains(q); ++i) q += dir * (s_.fontSize * 0.1);
            out.push_back(q);
        } else {
            out.push_back(c + dir * (s_.bondLength * 0.3));
        }
    }
    return out;
}

QRectF Renderer::numberRect(const Molecule& m, const ChemOverlay& ov) const {
    if (m.number.isEmpty() && m.caption.isEmpty()) return {};
    QRectF r = moleculeRect(m, ov);
    FontSpec nf = numberFont();
    double top = r.bottom() + s_.numberGap;
    double w = runsWidth(labels::markupRuns(m.number), nf);
    double h = nf.size * 1.2;
    QRectF out(r.center().x() - w / 2, top, w, m.number.isEmpty() ? 0 : h);
    if (!m.caption.isEmpty()) {
        FontSpec af = annotationFont();
        double cw = runsWidth(labels::markupRuns(m.caption), af);
        out |= QRectF(r.center().x() - cw / 2, out.bottom(), cw, af.size * 1.25);
    }
    return out;
}

void Renderer::drawMolecule(QPainter& p, const Molecule& m, const ChemOverlay& ov, const Decorations* deco,
                            const QHash<QString, int>& bondChanges, bool showMaps) const {
    QHash<QString, AtomLabel> labelMap;
    for (const auto& a : m.atoms) labelMap[a.id] = atomLabel(m, a, ov);

    // Aromatic circles replace alternating double bonds when the style asks for them.
    QSet<QString> circleBonds;
    std::vector<QStringList> circles;
    const bool useCircles = m.aromatic.isEmpty() ? s_.aromaticCircles : m.aromatic == "circles";
    if (useCircles) {
        for (const auto& ring : ov.aromaticRings.value(m.id)) {
            circles.push_back(ring);
            for (int i = 0; i < ring.size(); ++i)
                if (auto* b = m.bondBetween(ring[i], ring[(i + 1) % ring.size()])) circleBonds.insert(b->id);
        }
    }

    // Editor decorations underneath.
    if (deco) {
        const bool molSel = deco->selected.contains(m.id);
        p.setPen(Qt::NoPen);
        for (const auto& b : m.bonds) {
            bool sel = molSel || deco->selected.contains(b.id);
            bool hov = deco->hoverId == b.id;
            if (!sel && !hov) continue;
            p.setPen(QPen(hov ? kHover : kSelect, s_.bondLength * 0.22, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(toPt(m.atom(b.a)->pos()), toPt(m.atom(b.b)->pos()));
        }
        p.setPen(Qt::NoPen);
        for (const auto& a : m.atoms) {
            bool sel = molSel || deco->selected.contains(a.id);
            bool hov = deco->hoverId == a.id;
            bool link = deco->linked.contains(a.id);
            if (!sel && !hov && !link) continue;
            p.setBrush(link ? kLinked : hov ? kHover : kSelect);
            double r = s_.bondLength * (hov || link ? 0.24 : 0.16);
            p.drawEllipse(toPt(a.pos()), r, r);
        }
    }

    for (const auto& b : m.bonds) {
        // Molecular diff: broken, formed and changed bonds in the style's colours, a little heavier.
        int change = bondChanges.value(b.id, 0);
        QColor c = change == int(mapping::Change::Broken)   ? s_.brokenBonds
                   : change == int(mapping::Change::Formed) ? s_.formedBonds
                   : change == int(mapping::Change::Changed) ? s_.changedBonds
                                                             : s_.color;
        drawBond(p, m, b, labelMap, circleBonds.contains(b.id), c, change ? 1.8 : 1.0);
    }

    if (m.showStereo) {
        // CIP descriptors in small italics: (R)/(S) beside the stereocentre, (E)/(Z) beside the double bond.
        FontSpec sf{s_.font, s_.fontSize * 0.7, false, true};
        QColor sc = s_.background.lightnessF() < 0.5 ? QColor("#c9a0ff") : QColor("#7a3fb5");
        auto put = [&](const QString& code, QPointF c) {
            QString t = "(" + code + ")";
            double w = runsWidth({{t}}, sf);
            drawRuns(p, {{t}}, QPointF(c.x() - w / 2, c.y() + capHeight(sf) / 2), sf, sc);
        };
        // Keep clear of atoms, bond midpoints and atom labels: try spots around the atom, take the emptiest.
        auto clearance = [&](QPointF c, const QString& self) {
            double best = 1e9;
            for (const auto& o : m.atoms) {
                if (o.id == self) continue;
                best = std::min(best, len(toPt(o.pos()) - c));
                const AtomLabel& OL = labelMap[o.id];
                if (OL.visible) {
                    QRectF r = OL.bounds;
                    double dx = std::max({r.left() - c.x(), 0.0, c.x() - r.right()});
                    double dy = std::max({r.top() - c.y(), 0.0, c.y() - r.bottom()});
                    best = std::min(best, std::hypot(dx, dy));
                }
            }
            for (const auto& b : m.bonds)
                best = std::min(best, distToSegment(c, toPt(m.atom(b.a)->pos()), toPt(m.atom(b.b)->pos())));
            return best;
        };
        for (const auto& a : m.atoms) {
            QString code = ov.cip.value(a.id);
            if (code.isEmpty()) continue;
            const AtomLabel& L = labelMap[a.id];
            double r = L.visible ? std::max(L.bounds.width(), L.bounds.height()) * 0.6 + s_.fontSize * 0.6
                                 : s_.bondLength * 0.5;
            QPointF base = toPt(a.pos()), fd = freeDirection(m, a.id), bestPt = base + fd * r;
            double bestScore = -1;
            for (int k = 0; k < 16; ++k) {
                QPointF c = base + rotate(fd, k * M_PI / 8) * r;
                double score = clearance(c, a.id) + (k == 0 ? 0.5 : 0);  // slight preference for the free side
                if (score > bestScore) { bestScore = score; bestPt = c; }
            }
            put(code, bestPt);
        }
        for (const auto& b : m.bonds) {
            QString code = ov.cip.value(b.id);
            if (code.isEmpty()) continue;
            QPointF pa = toPt(m.atom(b.a)->pos()), pb = toPt(m.atom(b.b)->pos());
            QPointF nn = unit(perp(pb - pa));
            int side = doubleBondSide(m, b);
            QPointF away = nn * (side == 0 ? 1.0 : -double(side));  // opposite the second line
            put(code, (pa + pb) / 2 + away * (s_.fontSize * 0.95));
        }
    }

    if (showMaps) {
        FontSpec mf{s_.font, s_.fontSize * 0.62, false, true};
        QColor mc = s_.background.lightnessF() < 0.5 ? QColor("#8ab4ff") : QColor("#2a62c9");
        for (const auto& a : m.atoms) {
            if (!a.map) continue;
            QString t = QString::number(a.map);
            double w = runsWidth({{t}}, mf);
            const AtomLabel& L = labelMap[a.id];
            QPointF c;
            if (L.visible) {
                // Just past the top-right corner of the label.
                c = QPointF(L.bounds.right() + w / 2 + s_.margin, L.clip.top());
            } else {
                QPointF dir = rotate(freeDirection(m, a.id), -M_PI / 4);
                c = toPt(a.pos()) + dir * (s_.bondLength * 0.42);
            }
            drawRuns(p, {{t}}, QPointF(c.x() - w / 2, c.y() + capHeight(mf) / 2), mf, mc);
        }
    }

    for (const auto& ring : circles) {
        QPointF c(0, 0);
        for (const auto& id : ring) c += toPt(m.atom(id)->pos());
        c /= ring.size();
        double apo = s_.bondLength / (2 * std::tan(M_PI / ring.size()));
        p.setPen(QPen(s_.color, s_.lineWidth));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, apo * 0.62, apo * 0.62);
    }

    const FontSpec f = labelFont();
    for (const auto& a : m.atoms) {
        const AtomLabel& L = labelMap[a.id];
        if (L.visible) {
            if (!L.splitColor) {
                for (const auto& blk : L.blocks) drawRuns(p, blk.runs, blk.origin, f, L.color);
            } else {
                // Draw the label twice, clipped: the anchor element's column in its colour, the rest plain.
                const QRectF all = L.bounds.adjusted(-f.size, -f.size, f.size, f.size);
                const QRectF anchor(L.anchorLeft, all.top(), L.anchorRight - L.anchorLeft, all.height());
                p.save();
                p.setClipRect(anchor, Qt::IntersectClip);
                for (const auto& blk : L.blocks) drawRuns(p, blk.runs, blk.origin, f, L.color);
                p.restore();
                p.save();
                QPainterPath restPath;
                restPath.addRect(all);
                QPainterPath anchorPath;
                anchorPath.addRect(anchor);
                p.setClipPath(restPath.subtracted(anchorPath), Qt::IntersectClip);
                for (const auto& blk : L.blocks) drawRuns(p, blk.runs, blk.origin, f, L.restColor);
                p.restore();
            }
        } else if (a.charge != 0) {
            QPointF fd = freeDirection(m, a.id);
            QString q = (std::abs(a.charge) > 1 ? QString::number(std::abs(a.charge)) : QString()) +
                        (a.charge > 0 ? "+" : QString(QChar(0x2212)));
            FontSpec cf = f;
            cf.size *= 0.8;
            double w = runsWidth({{q}}, cf);
            QPointF c = toPt(a.pos()) + fd * (s_.bondLength * 0.38);
            drawRuns(p, {{q}}, QPointF(c.x() - w / 2, c.y() + capHeight(cf) / 2), cf, s_.textColor);
        }
        if (a.radical) {
            QPointF c = toPt(a.pos());
            QPointF at = L.visible ? QPointF(c.x(), L.clip.top() - s_.fontSize * 0.15)
                                   : c + freeDirection(m, a.id) * (s_.bondLength * 0.3);
            p.setPen(Qt::NoPen);
            p.setBrush(s_.textColor);
            for (int i = 0; i < a.radical; ++i) {
                double dx = (i - (a.radical - 1) / 2.0) * s_.fontSize * 0.3;
                p.drawEllipse(at + QPointF(dx, 0), s_.fontSize * 0.1, s_.fontSize * 0.1);
            }
        }
        if (a.lonePairs > 0) {
            // Two dots side by side, facing away from the atom.
            const QPointF c = toPt(a.pos());
            p.setPen(Qt::NoPen);
            p.setBrush(L.visible ? L.color : s_.textColor);
            const double r = s_.fontSize * 0.085, half = s_.fontSize * 0.17;
            for (const auto& q : lonePairPoints(m, a, ov)) {
                const QPointF side = perp(unit(q - c)) * half;
                p.drawEllipse(q + side, r, r);
                p.drawEllipse(q - side, r, r);
            }
        }
    }

    // Warning markers: a small dot at the top right of the atom.
    if (deco) {
        for (const auto& a : m.atoms) {
            if (!deco->warned.contains(a.id)) continue;
            QPointF c = toPt(a.pos()) + QPointF(s_.bondLength * 0.28, -s_.bondLength * 0.28);
            p.setPen(QPen(s_.background, s_.lineWidth * 0.6));
            p.setBrush(kWarn);
            p.drawEllipse(c, s_.bondLength * 0.09, s_.bondLength * 0.09);
        }
    }

    // Compound number and caption.
    if (!m.number.isEmpty() || !m.caption.isEmpty()) {
        QRectF r = moleculeRect(m, ov);
        FontSpec nf = numberFont();
        double y = r.bottom() + s_.numberGap;
        if (deco && (deco->hoverId == "num:" + m.id)) {
            p.setPen(Qt::NoPen);
            p.setBrush(kHover);
            p.drawRoundedRect(numberRect(m, ov).adjusted(-2, -1, 2, 1), 2, 2);
        }
        if (!m.number.isEmpty() && !(deco && deco->editingId == "num:" + m.id)) {
            auto numRuns = labels::markupRuns(m.number);
            double w = runsWidth(numRuns, nf);
            drawRuns(p, numRuns, QPointF(r.center().x() - w / 2, y + capHeight(nf)), nf, s_.numberColor);
            y += nf.size * 1.2;
        }
        if (!m.caption.isEmpty()) {
            FontSpec af = annotationFont();
            auto runs = labels::markupRuns(m.caption);
            double w = runsWidth(runs, af);
            drawRuns(p, runs, QPointF(r.center().x() - w / 2, y + capHeight(af) + af.size * 0.1), af,
                     s_.annotationColor);
        }
    }
}

// ---------------------------------------------------------------------------
// Arrows

QString Renderer::stepLetter(const Document& d, const QString& arrowId) const {
    // Arrows whose letter is hidden are skipped, so the rest still run a, b, c…
    auto hidden = [&](const QString& id) { const Arrow* a = d.arrow(id); return a && a->hideLetter; };
    if (hidden(arrowId)) return {};
    int i = 0;
    for (const auto& s : d.schemes)
        for (const auto& st : s.steps) {
            if (st.arrow == arrowId) return QString(QChar('a' + i % 26));
            if (!hidden(st.arrow)) ++i;
        }
    i = 0;
    for (const auto& a : d.arrows) {
        if (a.id == arrowId) return QString(QChar('a' + i % 26));
        if (!a.hideLetter) ++i;
    }
    return {};
}

ArrowGeom Renderer::arrowGeom(const Arrow& a, const Document* d) const {
    ArrowGeom g;
    g.tail = toPt(a.tail());
    g.head = toPt(a.head());
    const FontSpec f = annotationFont();
    const double cap = capHeight(f);
    const double lineH = f.size * 1.25;

    QStringList above = a.above.isEmpty() ? QStringList() : a.above.split('\n');
    QStringList below = a.below.isEmpty() ? QStringList() : a.below.split('\n');
    if (d && d->stepLetters) {
        QString letter = stepLetter(*d, a.id);
        if (!letter.isEmpty()) above.prepend(letter);
    }
    double maxW = 0;
    std::vector<std::vector<TextRun>> ra, rb;
    for (const auto& l : above) { ra.push_back(labels::markupRuns(l)); maxW = std::max(maxW, runsWidth(ra.back(), f)); }
    for (const auto& l : below) { rb.push_back(labels::markupRuns(l)); maxW = std::max(maxW, runsWidth(rb.back(), f)); }

    for (const auto& q : a.path()) g.path.push_back(toPt(q));
    const bool bent = g.path.size() > 2;
    // Text goes on the longest segment (the only one for a straight arrow).
    size_t seg = 0;
    for (size_t i = 1; i + 1 < g.path.size(); ++i)
        if (len(g.path[i + 1] - g.path[i]) > len(g.path[seg + 1] - g.path[seg])) seg = i;
    QPointF s0 = g.path[seg], s1 = g.path[seg + 1];
    QPointF v = s1 - s0;
    bool horizontal = std::abs(v.x()) >= std::abs(v.y());
    if (!bent && horizontal && maxW > 0) {
        // Stretch to fit the text, keeping the tail fixed.
        double need = maxW + f.size * 1.4;
        double L = len(v);
        if (L < need) {
            g.head = g.tail + unit(v) * need;
            g.path.back() = s1 = g.head;
        }
    }
    QPointF mid = (s0 + s1) / 2;
    // Clear the head (and the second shaft of equilibrium/retro arrows).
    double gap = s_.arrowHeadWidth + s_.arrowTextGap;
    if (a.kind == ArrowKind::Equilibrium || a.kind == ArrowKind::Retro) gap += s_.arrowHeadWidth * 0.6;
    if (horizontal) {
        // On a slanted arrow the line climbs across the text's width: clear the higher end.
        const double slope = std::abs(v.y() / v.x());
        double y = mid.y() - gap;
        for (int i = int(ra.size()) - 1; i >= 0; --i) {
            double w = runsWidth(ra[i], f);
            double lift = i == int(ra.size()) - 1 ? slope * w / 2 : 0;
            y -= lift;
            g.above.insert(g.above.begin(), {ra[i], QPointF(mid.x() - w / 2, y)});
            y -= lineH;
        }
        y = mid.y() + gap + cap;
        for (size_t i = 0; i < rb.size(); ++i) {
            double w = runsWidth(rb[i], f);
            if (i == 0) y += slope * w / 2;
            g.below.push_back({rb[i], QPointF(mid.x() - w / 2, y)});
            y += lineH;
        }
    } else {
        // Vertical arrows: "above" text sits to the right, "below" text to the left,
        // pushed sideways by the slant across the block's height.
        const double slant = std::abs(v.x() / v.y());
        double ha = ra.size() * lineH, hb = rb.size() * lineH;
        double y = mid.y() - ha / 2 + cap;
        for (auto& r : ra) { g.above.push_back({r, QPointF(mid.x() + gap + slant * ha / 2, y)}); y += lineH; }
        y = mid.y() - hb / 2 + cap;
        for (auto& r : rb) {
            double w = runsWidth(r, f);
            g.below.push_back({r, QPointF(mid.x() - gap - slant * hb / 2 - w, y)});
            y += lineH;
        }
    }
    QRectF tr;
    for (const auto* list : {&g.above, &g.below})
        for (const auto& b : *list) {
            double w = runsWidth(b.runs, f);
            tr |= QRectF(b.origin.x(), b.origin.y() - cap - 0.2 * f.size, w, lineH);
        }
    g.textRect = tr;
    double hw = s_.arrowHeadWidth + 2;
    QRectF pr;
    for (const auto& q : g.path) pr |= QRectF(q, QSizeF(0.01, 0.01));
    g.bounds = pr.adjusted(-hw, -hw, hw, hw) | tr;
    return g;
}

void Renderer::drawArrowHead(QPainter& p, QPointF tip, QPointF dir, int half, int filledMode) const {
    const bool filled = filledMode < 0 ? s_.arrowHeadFilled : filledMode > 0;
    QPointF n = perp(dir);
    QPointF base = tip - dir * s_.arrowHeadLength;
    QPointF l = base + n * s_.arrowHeadWidth, r = base - n * s_.arrowHeadWidth;
    if (filled) {
        QPolygonF poly;
        QPointF notch = tip - dir * (s_.arrowHeadLength * 0.8);
        if (half == 0) poly << tip << l << notch << r;
        else poly << tip << (half > 0 ? l : r) << notch;
        p.save();
        p.setBrush(p.pen().color());
        p.setPen(QPen(p.pen().color(), s_.arrowWidth * 0.5, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin));
        p.drawPolygon(poly);
        p.restore();
    } else {
        if (half >= 0) p.drawLine(tip, l);
        if (half <= 0) p.drawLine(tip, r);
    }
}

void Renderer::drawArrow(QPainter& p, const Arrow& a, const Document& d, const Decorations* deco) const {
    ArrowGeom g = arrowGeom(a, &d);
    if (deco && (deco->hoverId == a.id || deco->selected.contains(a.id))) {
        // Hug the shaft and the text rather than boxing the whole extent (bent arrows are big).
        QColor hc = deco->hoverId == a.id ? kHover : kSelect;
        p.setPen(QPen(hc, s_.arrowHeadWidth * 2.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(QPolygonF(QList<QPointF>(g.path.begin(), g.path.end())));
        p.setPen(Qt::NoPen);
        p.setBrush(hc);
        for (const auto* list : {&g.above, &g.below})
            for (const auto& b : *list) {
                FontSpec f = annotationFont();
                double cap = capHeight(f);
                p.drawRoundedRect(QRectF(b.origin.x() - 2, b.origin.y() - cap - 2, runsWidth(b.runs, f) + 4, cap + f.size * 0.45 + 2),
                                  2, 2);
            }
        // Handles show what can be dragged: round ends (re-aim), square corners (reshape).
        p.setPen(QPen(QColor(66, 133, 244), 0.8));
        p.setBrush(Qt::white);
        double r = s_.arrowHeadWidth * 0.55;
        for (size_t i = 1; i + 1 < g.path.size(); ++i) p.drawRect(QRectF(g.path[i] - QPointF(r, r), QSizeF(2 * r, 2 * r)));
        p.drawEllipse(g.tail, r * 1.1, r * 1.1);
        p.drawEllipse(g.head, r * 1.1, r * 1.1);
    }
    QColor col = !a.color.isEmpty() && QColor::isValidColorName(a.color) ? QColor::fromString(a.color) : s_.arrowColor;
    QPen pen(col, s_.arrowWidth * a.widthScale, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPointF dir = unit(g.head - g.tail), n = perp(dir);
    const double hl = s_.arrowHeadLength;

    const bool singleShaft = g.path.size() > 2 || a.kind == ArrowKind::Forward || a.kind == ArrowKind::Dashed ||
                             a.kind == ArrowKind::Crossed || a.kind == ArrowKind::Resonance;
    if (singleShaft) {
        // One shaft (straight or bent) with the arrow's own head and line style.
        const bool filled = a.headStyle.isEmpty() ? s_.arrowHeadFilled : (a.headStyle == "filled" || a.headStyle == "half");
        const int half = a.headStyle == "half" ? -1 : 0;
        const bool atEnd = a.headStyle != "none";
        const bool atStart = atEnd && (a.bothHeads || a.kind == ArrowKind::Resonance);
        QPolygonF line(QList<QPointF>(g.path.begin(), g.path.end()));
        QPointF endDir = unit(line[line.size() - 1] - line[line.size() - 2]);
        QPointF startDir = unit(line[1] - line[0]);
        if (atEnd && filled) line.back() -= endDir * (hl * 0.7);
        if (atStart && filled) line.front() += startDir * (hl * 0.7);
        QPen lp = pen;
        lp.setJoinStyle(Qt::RoundJoin);
        if (a.line == "dotted") {
            lp.setCapStyle(Qt::RoundCap);
            lp.setDashPattern({0.01, 2.6});
        } else if (a.line == "dashed" || a.kind == ArrowKind::Dashed) {
            lp.setDashPattern({4, 3});
        }
        p.setPen(lp);
        p.drawPolyline(line);
        p.setPen(pen);
        if (atEnd) drawArrowHead(p, g.head, endDir, half, filled);
        if (atStart) drawArrowHead(p, g.tail, -startDir, half, filled);
        if (a.kind == ArrowKind::Crossed && g.path.size() == 2) {
            QPointF m = (g.tail + g.head) / 2;
            double k = s_.arrowHeadWidth * 1.6;
            p.drawLine(m + (dir + n) * k * 0.7, m - (dir + n) * k * 0.7);
            p.drawLine(m + (dir - n) * k * 0.7, m - (dir - n) * k * 0.7);
        }
    } else switch (a.kind) {
    case ArrowKind::Equilibrium: {
        double off = s_.arrowHeadWidth * 0.55;
        QPointF t1 = g.tail - n * off, h1 = g.head - n * off;  // upper (toward -n in y-down for left->right)
        QPointF t2 = g.tail + n * off, h2 = g.head + n * off;
        bool wasFilled = s_.arrowHeadFilled;
        Q_UNUSED(wasFilled);
        p.drawLine(t1, h1 - dir * (s_.arrowHeadFilled ? hl * 0.5 : 0));
        p.drawLine(h2, t2 + dir * (s_.arrowHeadFilled ? hl * 0.5 : 0));
        drawArrowHead(p, h1, dir, -1);
        drawArrowHead(p, t2, -dir, -1);
        break;
    }
    case ArrowKind::Retro: {
        double off = s_.arrowHeadWidth * 0.5;
        QPointF tip = g.head;
        QPointF back = tip - dir * (hl * 1.1);
        p.drawLine(g.tail + n * off, back + n * off + dir * (off * 0.9));
        p.drawLine(g.tail - n * off, back - n * off + dir * (off * 0.9));
        QPen hp = pen;
        hp.setJoinStyle(Qt::MiterJoin);
        p.setPen(hp);
        QPolygonF chev;
        chev << back + n * (s_.arrowHeadWidth * 1.2) << tip << back - n * (s_.arrowHeadWidth * 1.2);
        p.drawPolyline(chev);
        break;
    }
    }

    if (!(deco && deco->editingId == a.id)) {
        const FontSpec f = annotationFont();
        // A coloured arrow colours its "above" text too (e.g. the enzyme name).
        QColor aboveCol = a.color.isEmpty() ? s_.annotationColor : col;
        for (const auto& b : g.above) drawRuns(p, b.runs, b.origin, f, aboveCol);
        for (const auto& b : g.below) drawRuns(p, b.runs, b.origin, f, s_.annotationColor);
    }
}

// ---------------------------------------------------------------------------
// Mechanism arrows

CurvedGeom Renderer::curvedGeom(const CurvedArrow& c, const Document& d, const ChemOverlay& ov) const {
    CurvedGeom g;
    QPointF ma, mb;
    if (!d.resolve(c.from, &ma) || !d.resolve(c.to, &mb)) return g;

    // Each end: where it attaches, and how far the arrow stops short of it.
    // Where an end attaches; how far it stops short (a gap, or a label's box); for a bare atom, the lines
    // its bonds draw there, which the end keeps `clearance` from.
    struct End { QPointF p; double gap = 0; QRectF clip; std::vector<QLineF> avoid; double clearance = 0; double maxReach = 0; };
    auto bondLines = [&](const Molecule& m, const QString& atomId) {
        std::vector<QLineF> out;
        const double sp = s_.spacingPt();
        for (const auto* bd : m.bondsOf(atomId)) {
            const QPointF pa = toPt(m.atom(bd->a)->pos()), pb = toPt(m.atom(bd->b)->pos());
            const QPointF nb = perp(unit(pb - pa));
            std::vector<double> offs{0};
            if (bd->order == 2) {
                const int side = doubleBondSide(m, *bd);
                offs = side == 0 ? std::vector<double>{-sp / 2, sp / 2} : std::vector<double>{0, side * sp};
            } else if (bd->order >= 3) {
                offs = {-sp, 0, sp};
            }
            for (double o : offs) out.emplace_back(pa + nb * o, pb + nb * o);
        }
        return out;
    };
    auto end = [&](const CurvedEnd& e, QPointF model, QPointF other) {
        End out{toPt(model)};
        if (!e.atom.isEmpty()) {
            const Molecule* m = d.moleculeOfAtom(e.atom);
            const Atom* a = m->atom(e.atom);
            if (e.lonePair && a->lonePairs > 0) {
                // The lone pair that faces the other end.
                double best = -2;
                for (const auto& q : lonePairPoints(*m, *a, ov)) {
                    double facing = dot(unit(q - out.p), unit(other - out.p));
                    if (facing > best) { best = facing; out.gap = s_.fontSize * 0.3; out.clip = {}; g.ok = true; g.a = q; }
                }
                out.p = g.a;
                return out;
            }
            const AtomLabel L = atomLabel(*m, *a, ov);
            if (L.visible) out.clip = L.clip.adjusted(-1, -1, 1, 1);
            else {
                out.gap = s_.bondLength * 0.2;
                out.avoid = bondLines(*m, a->id);  // and clear of the lines its bonds draw (both of a double bond)
                out.maxReach = s_.bondLength * 0.4;
            }
        } else if (!e.bond.isEmpty()) {
            const Bond* b = d.moleculeOfBond(e.bond)->bond(e.bond);
            out.gap = b->order > 1 ? std::max(s_.bondLength * 0.14, s_.spacingPt() * 1.2) : s_.bondLength * 0.14;
        }
        return out;
    };
    End B = end(c.to, mb, toPt(ma));
    End A = end(c.from, ma, B.p);
    g.anchorFrom = toPt(ma);  // what a hand-set shape's ends are measured from
    g.anchorTo = toPt(mb);
    B.clearance = s_.mechanismHeadWidth() * 0.9 + 0.6;  // the head's barbs, not just its point
    A.clearance = s_.mechanismWidth() + 0.6;
    QPointF v = B.p - A.p;
    double L = len(v);
    if (L < 1e-6) { g.ok = false; return g; }
    const QPointF n0 = perp(v / L);  // the curve's sides are measured against this

    // Which side it bulges to: as set by hand, else the side with fewer atoms in the way.
    double side = 1;
    if (c.manual) {
        side = c.bulge < 0 ? -1 : 1;
    } else {
        QSet<QString> seen;
        std::vector<QPointF> atoms;
        for (const CurvedEnd* e : {&c.from, &c.to}) {
            const Molecule* m = !e->atom.isEmpty() ? d.moleculeOfAtom(e->atom) : !e->bond.isEmpty() ? d.moleculeOfBond(e->bond) : nullptr;
            if (!m || seen.contains(m->id)) continue;
            seen.insert(m->id);
            for (const auto& a : m->atoms) atoms.push_back(toPt(a.pos()));
        }
        auto crowding = [&](QPointF apex) {
            double sum = 0;
            const double soft = 0.3 * s_.bondLength;
            for (const auto& q : atoms) sum += 1.0 / (dot(q - apex, q - apex) + soft * soft);
            return sum;
        };
        const QPointF mid0 = (A.p + B.p) / 2;
        const double h0 = 0.6 * s_.bondLength;
        const double minus = crowding(mid0 - n0 * h0), plus = crowding(mid0 + n0 * h0);
        if (std::abs(plus - minus) <= 1e-3 * std::max(plus, minus)) side = n0.y() <= 0 ? 1 : -1;  // a tie: above
        else side = minus < plus ? -1 : 1;
    }

    // An arrow from a bond to one of its own atoms (C–Br to Br, C=O to O), or from an atom to a bond of its
    // own, is drawn the way it is by hand: up off the middle of the bond, over, and down onto the top of the
    // atom. Aimed straight at the atom it would be shorter than its own head.
    auto ownAtom = [&](const CurvedEnd& atomEnd, const CurvedEnd& bondEnd) {
        if (atomEnd.atom.isEmpty() || bondEnd.bond.isEmpty()) return false;
        const Molecule* m = d.moleculeOfBond(bondEnd.bond);
        return m && m->bond(bondEnd.bond)->has(atomEnd.atom);
    };
    const bool hopTo = ownAtom(c.to, c.from), hopFrom = !hopTo && ownAtom(c.from, c.to);
    const bool hop = hopTo || hopFrom;
    const double bl = s_.bondLength;
    Curve full;
    // A shape set by hand: the ends where they were put (offset from what they're attached to), the curve
    // through its two control points. Drawn exactly as set; nothing is trimmed or aimed.
    if (c.shaped) {
        const QPointF P0 = toPt(ma + c.fromOff), P3 = toPt(mb + c.toOff);
        full = Cubic{P0, P0 + c.c1 * bl, P3 + c.c2 * bl, P3};
        A = End{P0};
        B = End{P3};
    } else if (hop) {
        const End& atomE = hopTo ? B : A;
        const QPointF X = toPt(hopTo ? mb : ma), M = toPt(hopTo ? ma : mb);  // the atom, the bond's middle
        const QPointF u = unit(X - M);                                     // along the bond, toward the atom
        const QPointF lc = atomE.clip.isNull() ? atomE.p : atomE.clip.center();
        auto insideLabel = [&](QPointF q) {
            if (atomE.clip.isNull()) return len(q - X) < std::max(atomE.gap, 0.28 * bl);
            const QPointF k = q - lc;
            const double ea = atomE.clip.width() / 2 + 0.4, eb = atomE.clip.height() / 2 + 0.4;
            return (k.x() / ea) * (k.x() / ea) + (k.y() / eb) * (k.y() / eb) < 0.999;
        };
        // Two arrows from one bond (a homolysis) both start at its middle; otherwise, where the label covers
        // the middle, the tail slides along the bond toward the other atom to the part that is drawn.
        bool shared = false;
        for (const auto& o : d.curved)
            shared |= o.id != c.id && !c.from.bond.isEmpty() && o.from.bond == c.from.bond;
        // A starting shape for a hook (drag its handles to change it): an arc that leaves the bond beside its
        // drawn part, swings out, and comes down onto the edge of the atom's label pointing into it. Each
        // candidate is the exact circle through the tail S with a start direction that also passes through a
        // point T on the label's edge; the best-aimed of a comfortable size and turn wins. Returns its score.
        auto searchSide = [&](double sd, Arc& out) {
            const QPointF ns = n0 * sd;
            // The tail clears every line the bond draws on its side: the second line of a double bond (when
            // that's this side, or half a spacing for a centred one), both outer lines of a triple bond.
            double clearLines = 0;
            const CurvedEnd& bondEnd = hopTo ? c.from : c.to;
            if (const Molecule* bm = d.moleculeOfBond(bondEnd.bond)) {
                const Bond* bb = bm->bond(bondEnd.bond);
                const QPointF nb = perp(unit(bm->atom(bb->b)->pos() - bm->atom(bb->a)->pos()));
                const double sp = s_.spacingPt();
                if (bb->order >= 3) clearLines = sp;
                else if (bb->order == 2) {
                    const int dbSide = doubleBondSide(*bm, *bb);
                    if (dbSide == 0) clearLines = sp / 2;
                    else if ((dbSide > 0) == (dot(nb, ns) > 0)) clearLines = sp;
                }
            }
            const double off = clearLines + 0.07 * bl;
            QPointF onBond = M;
            for (int i = 0; i < 12 && !shared && insideLabel(onBond + ns * off); ++i) onBond -= u * (0.04 * bl);
            QPointF S = onBond + ns * off;
            for (int i = 0; i < 20 && insideLabel(S); ++i) S += ns * (0.04 * bl);
            const double ea = atomE.clip.isNull() ? std::max(atomE.gap, 0.28 * bl) : atomE.clip.width() / 2 + 0.4;
            const double eb = atomE.clip.isNull() ? ea : atomE.clip.height() / 2 + 0.4;
            out = Arc::make(S, lc + ns * eb, ns, 0.5);  // fallback (nothing fits): onto the top of the label
            double best = 1e9, loose = 1e9;
            Arc looseArc = out;
            const double hl = s_.mechanismHeadLength();
            for (int il = shared ? 0 : -15; il <= 12; ++il) {
                const double lean = il * M_PI / 36;  // -75° (swinging out first) to 60° toward the atom
                const QPointF t0 = unit(ns * std::cos(lean) + u * std::sin(lean));
                QPointF nt = perp(t0);
                if (dot(nt, u) < 0) nt = -nt;  // the circle turns toward the atom
                for (int ip = 0; ip < 72; ++ip) {
                    const double psi = ip * M_PI / 36;
                    const QPointF T = lc + QPointF(ea * std::cos(psi), eb * std::sin(psi));
                    if (dot(T - X, ns) < 0) continue;  // land on the curve's side of the label
                    const QPointF w = T - S;
                    const double k = dot(nt, w);
                    if (k < 1e-6) continue;
                    const double R = dot(w, w) / (2 * k);
                    if (R < 0.22 * bl || R > 0.75 * bl) continue;
                    const QPointF C = S + nt * R;
                    const double aS = std::atan2(S.y() - C.y(), S.x() - C.x()), aT = std::atan2(T.y() - C.y(), T.x() - C.x());
                    const double dir = dot(QPointF(-std::sin(aS), std::cos(aS)), t0) > 0 ? 1 : -1;
                    double sw = std::remainder(aT - aS, 2 * M_PI);
                    if (dir > 0 && sw < 0) sw += 2 * M_PI;
                    if (dir < 0 && sw > 0) sw -= 2 * M_PI;
                    const double turn = std::abs(sw) * 180 / M_PI;
                    if (turn < 70 || turn > 290) continue;
                    bool clear = true;  // it mustn't cut through the label on the way
                    for (int j = 1; j < 24 && clear; ++j) {
                        const double a = aS + sw * j / 24.0;
                        clear = !insideLabel(C + QPointF(std::cos(a), std::sin(a)) * R);
                    }
                    if (!clear) continue;
                    // The head's aim, as drawn: along the arc where the line meets it.
                    const double notch = kNotch * std::min(hl, 0.9 * R);
                    const double an = aT - dir * notch / R;
                    const QPointF headDir = QPointF(-std::sin(an), std::cos(an)) * dir;
                    if (dot(headDir, X - T) <= 0) continue;
                    const double miss = std::abs(cross(headDir, X - T));
                    if (miss > 0.6 * bl) continue;
                    const QPointF across = perp(headDir);
                    const double reachAcross = std::sqrt((ea * across.x()) * (ea * across.x()) + (eb * across.y()) * (eb * across.y()));
                    const bool intoLabel = miss < std::max(reachAcross - 2.2, 0.15 * bl);
                    const double score = miss / (0.2 * bl) + (lean < 0 ? 0.8 * -lean : 0.4 * std::abs(lean - M_PI / 12)) +
                                         3.0 * std::abs(R - 0.3 * bl) / bl + 0.004 * std::max(0.0, turn - 230) +
                                         0.003 * std::max(0.0, 110 - turn);
                    if (intoLabel && score < best) { best = score; out = Arc{S, T, C, R, aS, sw}; }
                    if (score < loose) { loose = score; looseArc = Arc{S, T, C, R, aS, sw}; }
                }
            }
            if (best > 1e8 && loose < 1e8) { out = looseArc; best = loose + 1.0; }
            return best;
        };
        Arc arc;
        double best = searchSide(side, arc);
        if (!c.manual && !shared) {
            Arc other;
            // The other side wins only if it aims clearly better (the emptier side is the default).
            if (searchSide(-side, other) + 0.15 < best) { arc = other; side = -side; }
        }
        if (hopFrom) {  // the same curve, travelled the other way
            Arc rev = arc;
            rev.p = arc.q;
            rev.q = arc.p;
            rev.a0 = arc.a0 + arc.sweep;
            rev.sweep = -arc.sweep;
            arc = rev;
        }
        full = arc;
        const double chord = len(arc.q - arc.p);
        const double h = chord > 1e-6 ? dot(arc.at(0.5) - (arc.p + arc.q) / 2, n0) / chord : side * 0.5;
        g.bulge = std::copysign(0.5, h);
        A.p = arc.p; A.clip = {}; A.gap = 0; A.avoid.clear();
        B.p = arc.q; B.clip = {}; B.gap = 0; B.avoid.clear();
    }
    g.a = A.p;
    g.b = B.p;
    v = g.b - g.a;
    L = len(v);
    if (L < 1e-6) { g.ok = false; return g; }
    g.ok = true;
    g.normal = n0;
    if (!hop && !c.shaped) {
        // A standard height, a little rounder for short arrows.
        const double h = std::min(std::clamp(0.3 * L, 0.45 * bl, 0.9 * bl), 0.55 * L);
        g.bulge = c.manual ? std::clamp(c.bulge, -0.9, 0.9) : side * h / L;
        full = mechanismCurve(g.a, g.b, n0, g.bulge * L);
    }

    auto outside = [](const End& e, QPointF q) {
        if (!e.clip.isNull()) {
            const QPointF k = q - e.clip.center();
            const double a = e.clip.width() / 2, b = e.clip.height() / 2;
            // An ellipse in the label's box: corners of the box are empty space, not label.
            return (k.x() / a) * (k.x() / a) + (k.y() / b) * (k.y() / b) >= 1;
        }
        const double d0 = len(q - e.p);
        if (d0 < e.gap) return false;
        if (!e.avoid.empty() && d0 > e.maxReach) return true;  // never so far back that it floats free of the atom
        for (const auto& l : e.avoid)
            if (distToSegment(q, l.p1(), l.p2()) < e.clearance) return false;
        return true;
    };
    const int steps = 200;
    int i0 = 0, i1 = steps;
    while (i0 < steps && !outside(A, full.at(double(i0) / steps))) ++i0;
    while (i1 > 0 && !outside(B, full.at(double(i1) / steps))) --i1;
    if (i0 >= i1) { i0 = 0; i1 = steps; }  // the ends overlap: draw it whole rather than nothing
    const Curve q = full.sub(double(i0) / steps, double(i1) / steps);
    g.start = q.at(0);
    g.tip = q.at(1);
    g.isArc = q.isArc;
    if (q.isArc) {
        g.centre = q.arc.c;
        g.radius = q.arc.r;
        g.angle0 = q.arc.a0;
        g.sweep = q.arc.sweep;
    } else {
        g.c1 = q.cb.p1;
        g.c2 = q.cb.p2;
    }
    // A head never takes more than a third of a short arrow, or so much of a tight arc that it bends off it.
    const double arcLen = q.length();
    const double shrink = std::min({1.0, arcLen * 0.33 / s_.mechanismHeadLength(),
                                    !q.isArc || q.arc.sweep == 0 ? 1.0 : q.arc.r * 0.9 / s_.mechanismHeadLength()});
    g.headLength = s_.mechanismHeadLength() * shrink;
    g.headWidth = s_.mechanismHeadWidth() * std::max(shrink, 0.8);
    // The line stops at the head's notch, and the head carries straight on from there.
    const double notch = g.headLength * kNotch;
    const double tn = arcLen > 1e-9 ? q.tBack(notch) : 0.0;
    // The head carries on straight from where the line ends (its notch), so line and head meet without a
    // kink; its point lands a hair off the arc's end, which nobody can see.
    g.tipDir = arcLen > 1e-9 ? q.tangent(tn) : unit(v);
    g.notch = q.at(tn);
    g.headTip = g.notch + g.tipDir * std::min(notch, arcLen);
    g.apex = full.at(0.5);
    QRectF r;
    for (int i = 0; i <= 32; ++i) {
        g.points.push_back(q.at(i / 32.0));
        r |= QRectF(g.points.back(), QSizeF(0.01, 0.01));
    }
    const double pad = s_.mechanismHeadWidth() + 1;
    g.bounds = r.adjusted(-pad, -pad, pad, pad);
    return g;
}

void Renderer::drawCurved(QPainter& p, const CurvedArrow& c, const Document& d, const ChemOverlay& ov,
                          const Decorations* deco) const {
    const CurvedGeom g = curvedGeom(c, d, ov);
    if (!g.ok) return;
    const Curve q = g.isArc ? Curve(Arc{g.start, g.tip, g.centre, g.radius, g.angle0, g.sweep})
                            : Curve(Cubic{g.start, g.c1, g.c2, g.tip});
    const double hl = g.headLength, hw = g.headWidth, lw = s_.mechanismWidth();
    const bool hov = deco && deco->hoverId == c.id, sel = deco && deco->selected.contains(c.id);
    if (hov || sel) {
        QPainterPath whole;
        q.addTo(whole);
        p.setPen(QPen(hov ? kHover : kSelect, std::max(hw * 3.0, 5.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(whole);
        // Handles: round ends (drag to move or re-attach), and the curve's two control points on thin lines from
        // their ends, as in other drawing programs (drag to shape it).
        const QColor blue(66, 133, 244);
        const double r = std::max(hw * 0.6, 1.8);
        if (!g.isArc) {
            QPen guide(blue, 0.5);
            guide.setDashPattern({2, 2});
            p.setPen(guide);
            p.drawLine(g.start, g.c1);
            p.drawLine(g.tip, g.c2);
            p.setPen(QPen(blue, 0.8));
            p.setBrush(blue);
            p.drawRect(QRectF(g.c1 - QPointF(r * 0.8, r * 0.8), QSizeF(1.6 * r, 1.6 * r)));
            p.drawRect(QRectF(g.c2 - QPointF(r * 0.8, r * 0.8), QSizeF(1.6 * r, 1.6 * r)));
        }
        p.setPen(QPen(blue, 0.8));
        p.setBrush(Qt::white);
        p.drawEllipse(g.start, r * 1.1, r * 1.1);
        p.drawEllipse(g.tip, r * 1.1, r * 1.1);
    }
    const QColor col = !c.color.isEmpty() && QColor::isValidColorName(c.color) ? QColor::fromString(c.color) : s_.mechanismColor();

    // The head is the document's own arrowhead (as on its reaction arrows), a size smaller: filled with a
    // shallow notch, or an open chevron if the style says so. The line runs into the notch, or to the point.
    const bool filled = s_.arrowHeadFilled;
    const double arcLen = q.length(), notch = hl * kNotch;
    const double tn = arcLen > 1e-9 ? q.tBack(notch) : 0.0;
    const QPointF dir = g.tipDir, notchPt = g.notch, tip = g.headTip, back = tip - dir * hl;
    QPointF nrm = perp(dir);
    if (dot(nrm, g.apex - (g.start + g.tip) / 2) < 0) nrm = -nrm;  // toward the outside of the curve

    QPainterPath path;
    q.sub(0, tn).addTo(path);
    path.lineTo(filled ? notchPt + dir * std::min(lw, notch * 0.5) : tip);
    p.setPen(QPen(col, lw, filled ? Qt::SolidLine : Qt::SolidLine, filled ? Qt::RoundCap : Qt::FlatCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);

    // An electron pair gets a whole head; a single electron (fishhook) one barb, on the outside of the curve.
    if (filled) {
        QPolygonF head;
        if (c.half) head << tip << back + nrm * hw << notchPt;
        else head << tip << back + nrm * hw << notchPt << back - nrm * hw;
        p.setPen(QPen(col, lw * 0.5, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin));
        p.setBrush(col);
        p.drawPolygon(head);
    } else {
        p.setPen(QPen(col, lw, Qt::SolidLine, Qt::RoundCap, Qt::MiterJoin));
        p.drawLine(tip, back + nrm * hw);
        if (!c.half) p.drawLine(tip, back - nrm * hw);
    }
}

FontSpec Renderer::textFont(const TextBox& t) const {
    FontSpec f = labelFont();
    if (t.size > 0) f.size = t.size;
    return f;
}

QRectF Renderer::textRect(const TextBox& t) const {
    const FontSpec f = textFont(t);
    QStringList lines = t.text.split('\n');
    double w = 0;
    for (const auto& l : lines) w = std::max(w, runsWidth(labels::markupRuns(l), f));
    QPointF o = toPt({t.x, t.y});
    return QRectF(o.x(), o.y(), std::max(w, f.size * 0.5), lines.size() * f.size * 1.25);
}

void Renderer::drawSchemeExtras(QPainter& p, const Document& d, const ChemOverlay& ov) const {
    // Plus signs between molecules on the same side of a step.
    const FontSpec f{s_.font, s_.fontSize * 1.3};
    for (const auto& ps : plusSigns(d, ov)) {
        const double w = runsWidth({{"+"}}, f);
        const QPointF c = ps.rect.center();
        drawRuns(p, {{"+"}}, QPointF(c.x() - w / 2, c.y() + capHeight(f) / 2), f, s_.color);
    }
}

std::vector<PlusSign> Renderer::plusSigns(const Document& d, const ChemOverlay& ov) const {
    std::vector<PlusSign> out;
    const FontSpec f{s_.font, s_.fontSize * 1.3};
    const double w = runsWidth({{"+"}}, f), h = capHeight(f);
    for (const auto& s : d.schemes)
        for (const auto& st : s.steps)
            for (const QStringList* ids : {&st.reactants, &st.products})
                for (int i = 0; i + 1 < ids->size(); ++i) {
                    if (st.noPlus.contains((*ids)[i])) continue;
                    const Molecule* a = d.molecule((*ids)[i]);
                    const Molecule* b = d.molecule((*ids)[i + 1]);
                    if (!a || !b) continue;
                    const QRectF ra = moleculeRect(*a, ov), rb = moleculeRect(*b, ov);
                    const QPointF c((ra.right() + rb.left()) / 2, (ra.center().y() + rb.center().y()) / 2);
                    out.push_back({QRectF(c.x() - w / 2, c.y() - h / 2, w, h), st.arrow, (*ids)[i]});
                }
    return out;
}

QRectF Renderer::documentBounds(const Document& d, const ChemOverlay& ov) const {
    QRectF r;
    for (const auto& m : d.molecules) {
        r |= moleculeRect(m, ov);
        QRectF nr = numberRect(m, ov);
        if (!nr.isNull()) r |= nr;
    }
    for (const auto& a : d.arrows) r |= arrowGeom(a, &d).bounds;
    for (const auto& t : d.texts) r |= textRect(t);
    for (const auto& c : d.curved)
        if (const CurvedGeom g = curvedGeom(c, d, ov); g.ok) r |= g.bounds;
    return r;
}

void Renderer::draw(QPainter& p, const Document& d, const ChemOverlay& ov, const Decorations* deco) const {
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    QHash<QString, int> changes;
    const auto diff = mapping::allBondChanges(d);
    for (auto it = diff.begin(); it != diff.end(); ++it) changes[it.key()] = int(it.value());
    const bool maps = d.showMaps || (deco && deco->forceMaps);
    for (const auto& m : d.molecules) drawMolecule(p, m, ov, deco, changes, maps);
    for (const auto& a : d.arrows) drawArrow(p, a, d, deco);
    for (const auto& c : d.curved) drawCurved(p, c, d, ov, deco);
    if (deco && deco->hoverId.startsWith("plus:"))
        for (const auto& ps : plusSigns(d, ov))
            if (ps.id() == deco->hoverId) {
                p.setPen(Qt::NoPen);
                p.setBrush(kHover);
                p.drawRoundedRect(ps.rect.adjusted(-2, -2, 2, 2), 2, 2);
            }
    drawSchemeExtras(p, d, ov);
    for (const auto& t : d.texts) {
        const FontSpec f = textFont(t);
        const QColor tc = !t.color.isEmpty() && QColor::isValidColorName(t.color) ? QColor::fromString(t.color) : s_.textColor;
        QRectF r = textRect(t);
        if (deco && (deco->hoverId == t.id || deco->selected.contains(t.id))) {
            p.setPen(Qt::NoPen);
            p.setBrush(deco->hoverId == t.id ? kHover : kSelect);
            p.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 2, 2);
        }
        if (deco && deco->editingId == t.id) continue;
        double y = r.top() + capHeight(f) + f.size * 0.1;
        for (const auto& l : t.text.split('\n')) {
            const auto runs = labels::markupRuns(l);
            double x = r.left();
            if (!t.align.isEmpty()) {
                double spare = r.width() - runsWidth(runs, f);
                x += t.align == "right" ? spare : t.align == "center" ? spare / 2 : 0;
            }
            drawRuns(p, runs, QPointF(x, y), f, tc);
            y += f.size * 1.25;
        }
    }
}

}  // namespace cd
