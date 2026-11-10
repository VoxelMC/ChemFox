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
    if (plain && a.el == "C" && deg > 0 && !m.showCarbons) return L;  // implicit carbon
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
        if (!hRuns.empty() && !hVertical && hFirst) {
            prefix = hRuns;
            main = hRuns;
            main.push_back({a.el});
        } else {
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
    if (a.charge != 0) {
        QString q = (std::abs(a.charge) > 1 ? QString::number(std::abs(a.charge)) : QString()) +
                    (a.charge > 0 ? "+" : QString(QChar(0x2212)));
        main.push_back({q, Script::Super});
    }

    // Position: centre the anchor element on the atom.
    double prefixW = runsWidth(prefix, f);
    double anchorW = runsWidth({{anchorText}}, f);
    QPointF origin(c.x() - prefixW - anchorW / 2, c.y() + cap / 2);
    L.blocks.insert(L.blocks.begin(), {main, origin});

    const double mg = s_.margin;
    L.clip = QRectF(origin.x() + prefixW - mg, c.y() - cap / 2 - mg, anchorW + 2 * mg, cap + 2 * mg);
    QRectF bounds;
    for (const auto& b : L.blocks) {
        double w = runsWidth(b.runs, f);
        bounds |= QRectF(b.origin.x(), b.origin.y() - cap - 0.45 * f.size * 0.7, w, cap + 0.45 * f.size);
    }
    L.bounds = bounds;
    L.color = plain ? elementColor(a.el) : elementColor(anchorText);
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
    }
    return r;
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
    if (s_.aromaticCircles) {
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
            for (const auto& blk : L.blocks) drawRuns(p, blk.runs, blk.origin, f, L.color);
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
    int i = 0;
    for (const auto& s : d.schemes)
        for (const auto& st : s.steps) {
            if (st.arrow == arrowId) return QString(QChar('a' + i % 26));
            ++i;
        }
    i = 0;
    for (const auto& a : d.arrows) {
        if (a.id == arrowId) return QString(QChar('a' + i % 26));
        ++i;
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
    auto plusBetween = [&](const QStringList& ids) {
        for (int i = 0; i + 1 < ids.size(); ++i) {
            const Molecule* a = d.molecule(ids[i]);
            const Molecule* b = d.molecule(ids[i + 1]);
            if (!a || !b) continue;
            QRectF ra = moleculeRect(*a, ov), rb = moleculeRect(*b, ov);
            QPointF c((ra.right() + rb.left()) / 2, (ra.center().y() + rb.center().y()) / 2);
            double w = runsWidth({{"+"}}, f);
            drawRuns(p, {{"+"}}, QPointF(c.x() - w / 2, c.y() + capHeight(f) / 2), f, s_.color);
        }
    };
    for (const auto& s : d.schemes)
        for (const auto& st : s.steps) {
            plusBetween(st.reactants);
            plusBetween(st.products);
        }
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
            drawRuns(p, labels::markupRuns(l), QPointF(r.left(), y), f, tc);
            y += f.size * 1.25;
        }
    }
}

}  // namespace cd
