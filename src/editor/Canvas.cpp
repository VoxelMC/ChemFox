#include "editor/Canvas.h"

#include "core/commands/Edits.h"
#include "core/model/Geometry.h"
#include "core/model/Labels.h"
#include "core/model/Mapping.h"
#include "editor/Templates.h"
#include "io/Export.h"
#include "io/Native.h"
#include "render/Layout.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QMenu>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QLabel>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <algorithm>

namespace cd {

using namespace geom;

Canvas::Canvas(QWidget* parent) : QWidget(parent), renderer_(style_) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(400, 300);
    keymap_.load();
    undo_.setUndoLimit(500);  // each step holds a document snapshot
    globalSilenced_ = warnings::globalSilenced();
    recompute();
}

// ---------------------------------------------------------------------------
// Document state

void Canvas::setDocument(const Document& d) {
    undo_.clear();
    doc_ = d;
    selected_.clear();
    stickyAtom_.clear();
    if (doc_.needsLayout) {
        // Generated coordinates (e.g. SMILES-only files): arrange arrows and structures.
        doc_.needsLayout = false;
        recompute();
        if (!doc_.arrows.empty()) {
            if (doc_.schemes.empty()) edits::buildScheme(doc_);
            else edits::renumber(doc_);
            layout::layoutSchemes(doc_, renderer_, overlay_);
        }
    }
    documentReplaced();
    zoomToFit();
}

void Canvas::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (fitPending_) zoomToFit();
}

void Canvas::edit(const QString& text, const std::function<void(Document&)>& fn, int mergeKey) {
    fitPending_ = false;
    Document after = doc_;
    fn(after);
    // Keep compound numbers in scheme order as things change.
    bool autoNumbers = !after.schemes.empty() &&
                       std::any_of(after.molecules.begin(), after.molecules.end(),
                                   [](const Molecule& m) { return !m.number.isEmpty() && !m.numberManual; });
    if (autoNumbers) edits::renumber(after);
    undo_.push(new SnapshotCommand(this, text, doc_, after, mergeKey));
}

void Canvas::documentReplaced() {
    // Undo/redo or the reaction builder replaced the document mid-drag: the drag's ids
    // may be gone, so drop it rather than act on stale objects.
    if (drag_ != Drag::None) {
        drag_ = Drag::None;
        preview_.clear();
        rubber_ = {};
        unsetCursor();
    }
    // Drop selected ids that no longer exist.
    QSet<QString> live;
    for (const auto& m : doc_.molecules) {
        live.insert(m.id);
        for (const auto& a : m.atoms) live.insert(a.id);
        for (const auto& b : m.bonds) live.insert(b.id);
    }
    for (const auto& a : doc_.arrows) live.insert(a.id);
    for (const auto& t : doc_.texts) live.insert(t.id);
    selected_.intersect(live);
    recompute();
    if (drag_ == Drag::None) updateHover(lastMouse_);
    update();
    emit documentChanged();
    emit infoTargetChanged();
}

QString Canvas::moleculeKey(const Molecule& m) const {
    QString k;
    k.reserve(int(m.atoms.size()) * 24);
    for (const auto& a : m.atoms)
        k += QString("%1|%2|%3|%4|%5|%6|%7|%8;").arg(a.id, a.el).arg(a.x, 0, 'f', 3).arg(a.y, 0, 'f', 3)
                 .arg(a.charge).arg(a.radical).arg(a.label).arg(a.isotope);
    for (const auto& b : m.bonds) k += QString("%1-%2:%3:%4;").arg(b.a, b.b).arg(b.order).arg(int(b.stereo));
    return k;
}

void Canvas::resolveStyle() {
    QString prevName = style_.name;
    styleIssues_.clear();
    style_ = doc_.styleText.isEmpty() ? styles::resolveNamed(doc_.styleName, &styleIssues_)
                                      : styles::resolve(doc_.styleText, &styleIssues_);
    // Keep the zoom showing the same on-screen size when the bond length changes.
    double oldBl = renderer_.style().bondLength;
    renderer_ = Renderer(style_);
    if (oldBl > 0 && std::abs(oldBl - style_.bondLength) > 1e-6) zoom_ *= oldBl / style_.bondLength;
    if (prevName != style_.name) emit styleChanged();
}

void Canvas::recompute() {
    // A changed group table changes what labelled atoms mean: drop chemistry computed with the old one.
    if (groupsGen_ != labels::groupsGeneration()) {
        analysisCache_.clear();
        infoCache_.clear();
        groupsGen_ = labels::groupsGeneration();
    }
    resolveStyle();
    overlay_ = ChemOverlay{};
    analyses_.clear();
    if (analysisCache_.size() > 2000) analysisCache_.clear();
    for (const auto& m : doc_.molecules) {
        QString key = moleculeKey(m);
        auto it = analysisCache_.find(key);
        if (it == analysisCache_.end()) it = analysisCache_.insert(key, chem::analyze(m));
        analyses_[m.id] = *it;
        for (auto h = it->implicitH.begin(); h != it->implicitH.end(); ++h) overlay_.implicitH[h.key()] = h.value();
        overlay_.aromaticRings[m.id] = it->aromaticRings;
        for (auto c = it->cip.begin(); c != it->cip.end(); ++c) overlay_.cip[c.key()] = c.value();
    }
    refreshWarnings();
}

void Canvas::refreshWarnings() {
    all_ = warnings::check(doc_, analyses_, styleIssues_);
    active_ = warnings::active(all_, doc_, globalSilenced_);
    emit warningsChanged();
}

chem::MolInfo Canvas::info(const Molecule& m) {
    QString key = moleculeKey(m);
    auto it = infoCache_.find(key);
    if (it != infoCache_.end()) return *it;
    if (infoCache_.size() > 500) infoCache_.clear();
    return *infoCache_.insert(key, chem::info(m));
}

const Molecule* Canvas::infoMolecule() const {
    Hit h = hover();
    if (!h.molId.isEmpty()) return doc_.molecule(h.molId);
    for (const auto& m : doc_.molecules) {
        if (selected_.contains(m.id)) return &m;
        for (const auto& a : m.atoms) if (selected_.contains(a.id)) return &m;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Hit testing and hover

std::vector<Hit> Canvas::hitTest(QPointF screen) const {
    std::vector<Hit> hits;
    const QPointF pt = toPt(screen);
    const QPointF mp = renderer_.toModel(pt);
    const double px = 1.0 / (zoom_ * style_.bondLength);  // one screen pixel in bond units
    const double atomR = std::max(0.3, 7 * px), bondR = std::max(0.18, 5 * px);
    for (const auto& m : doc_.molecules) {
        for (const auto& a : m.atoms) {
            double d = len(a.pos() - mp);
            AtomLabel L = renderer_.atomLabel(m, a, overlay_);
            if (d < atomR) hits.push_back({Hit::AtomHit, a.id, m.id, d});
            else if (L.visible && L.bounds.contains(pt)) hits.push_back({Hit::AtomHit, a.id, m.id, atomR * 0.9});
        }
        for (const auto& b : m.bonds) {
            double d = distToSegment(mp, m.atom(b.a)->pos(), m.atom(b.b)->pos());
            if (d < bondR) hits.push_back({Hit::BondHit, b.id, m.id, d + 0.05});
        }
        QRectF nr = renderer_.numberRect(m, overlay_);
        if (!nr.isNull() && nr.adjusted(-2, -2, 2, 2).contains(pt)) hits.push_back({Hit::NumberHit, m.id, m.id, 0.15});
    }
    for (const auto& a : doc_.arrows) {
        ArrowGeom g = renderer_.arrowGeom(a, &doc_);
        double d = 1e9;
        for (size_t i = 0; i + 1 < g.path.size(); ++i) d = std::min(d, distToSegment(pt, g.path[i], g.path[i + 1]));
        d /= style_.bondLength;
        if (d < std::max(0.25, 6 * px)) hits.push_back({Hit::ArrowHit, a.id, {}, d + 0.02});
        else if (g.textRect.contains(pt)) hits.push_back({Hit::ArrowHit, a.id, {}, 0.2});
    }
    for (const auto& t : doc_.texts)
        if (renderer_.textRect(t).adjusted(-2, -2, 2, 2).contains(pt)) hits.push_back({Hit::TextHit, t.id, {}, 0.1});
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.dist < b.dist; });
    return hits;
}

void Canvas::updateHover(QPointF screen) {
    QString before = hover().id + QString::number(hover().kind);
    auto hits = mouseInside_ ? hitTest(screen) : std::vector<Hit>{};
    if (!stickyAtom_.isEmpty()) {
        // A keyboard chain keeps its atom wherever the mouse goes, until Escape or a click.
        const Molecule* m = doc_.moleculeOfAtom(stickyAtom_);
        if (m && drag_ == Drag::None) hits = {Hit{Hit::AtomHit, stickyAtom_, m->id, 0}};
        else stickyAtom_.clear();
    }
    bool same = hits.size() == hits_.size();
    for (size_t i = 0; same && i < hits.size(); ++i) same = hits[i].id == hits_[i].id && hits[i].kind == hits_[i].kind;
    hits_ = hits;
    if (!same) hoverIndex_ = 0;
    QString after = hover().id + QString::number(hover().kind);
    if (before != after) {
        update();
        emit infoTargetChanged();
        updateStatus();
    }
}

QString Canvas::hoverTargetName(const Hit& h) const {
    switch (h.kind) {
    case Hit::AtomHit: return "atom";
    case Hit::BondHit: return "bond";
    case Hit::ArrowHit: return "arrow";
    case Hit::TextHit: return "text";
    case Hit::NumberHit: return "number";
    default: return {};
    }
}

void Canvas::updateStatus() {
    Hit h = hover();
    if (mapMode_) {
        emit status("Mapping — drag from a reactant atom to a product atom to map them · drag an atom to empty "
                    "space to unmap it · Esc or Ctrl+M: done");
        return;
    }
    QString more = hits_.size() > 1 ? QString("   ·   Tab: next of %1 overlapping").arg(hits_.size()) : QString();
    switch (h.kind) {
    case Hit::AtomHit: {
        const Atom* a = doc_.molecule(h.molId)->atom(h.id);
        QString what = a->label.isEmpty() ? a->el : a->label;
        emit status(QString("Atom %1 — C N O S P F H, l b i: element · Enter: label · +/−: charge · 1 2 3: grow bond · "
                            "Shift+3…8: ring · r: benzene · drag: new bond%2").arg(what, more));
        break;
    }
    case Hit::BondHit:
        emit status("Bond — 1 2 3: order · w h y: wedge/hash/wavy · f: flip · Shift+3…8: fuse ring · r: benzene · "
                    "drag: move molecule" + more);
        break;
    case Hit::ArrowHit:
        emit status("Arrow — Enter: conditions · k: arrow kind · drag ends to resize · bent arrows: drag a corner or leg to reshape, Ctrl+drag to move · right-click: more" + more);
        break;
    case Hit::TextHit: emit status("Text — Enter: edit" + more); break;
    case Hit::NumberHit: emit status("Compound number — Enter: edit number or caption" + more); break;
    default: {
        static const char* drags[] = {"drag: select", "drag: draw a chain", "drag: draw an arrow",
                                      "click corners, double-click or Enter to finish the bent arrow",
                                      "click: add text"};
        emit status(QString("Empty canvas — %1 · C N O…: atom · Shift+3…8: ring · r: benzene · a: arrow · t: text · "
                            "drag from an atom: bond · Ctrl+drag: move · wheel: zoom · middle/Space drag: pan")
                        .arg(drags[int(tool_)]));
    }
    }
}

// ---------------------------------------------------------------------------
// Painting

void Canvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), style_.background);
    p.setRenderHint(QPainter::Antialiasing);

    // Optional dot grid (Preferences → Drawing, or right-click empty canvas). Hidden when the dots
    // would be closer than a few pixels.
    if (showGrid_) {
        const double step = gridSize_ * style_.bondLength * zoom_;  // screen pixels
        if (step >= 6) {
            QColor dot = style_.background.lightnessF() < 0.5 ? QColor(255, 255, 255, 70) : QColor(0, 0, 0, 70);
            p.save();
            p.setPen(QPen(dot, std::clamp(step / 12, 1.0, 2.0), Qt::SolidLine, Qt::RoundCap));
            const QPointF o = toScreen(QPointF(0, 0));
            const double x0 = std::fmod(o.x(), step) - step, y0 = std::fmod(o.y(), step) - step;
            QVector<QPointF> pts;
            for (double y = y0; y < height() + step; y += step)
                for (double x = x0; x < width() + step; x += step) pts << QPointF(x, y);
            p.drawPoints(pts.constData(), int(pts.size()));
            p.restore();
        }
    }

    // Optional page-width guide (View menu): the style's page width, from a fixed origin,
    // which is where Arrange Scheme wraps rows.
    if (showPageGuide_) {
        p.save();
        QColor guide = style_.background.lightnessF() < 0.5 ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 40);
        p.setPen(QPen(guide, 1, Qt::DashLine));
        double x0 = toScreen(QPointF(0, 0)).x(), x1 = toScreen(QPointF(style_.pageWidth, 0)).x();
        p.drawLine(QPointF(x0, 0), QPointF(x0, height()));
        p.drawLine(QPointF(x1, 0), QPointF(x1, height()));
        p.setPen(guide.darker(100));
        p.drawText(QPointF(x1 + 4, 14), QString("page width %1 pt").arg(style_.pageWidth, 0, 'f', 0));
        p.restore();
    }

    p.save();
    p.translate(pan_);
    p.scale(zoom_, zoom_);
    Decorations deco;
    Hit h = hover();
    if (drag_ == Drag::None || drag_ == Drag::BondFromAtom || drag_ == Drag::MapLink)
        deco.hoverId = h.kind == Hit::NumberHit ? "num:" + h.id : h.id;
    deco.selected = selected_;
    deco.forceMaps = mapMode_;
    if (mapMode_ || doc_.showMaps) {
        // Show where the hovered (or dragged-from) atom is mapped to.
        QString atom = drag_ == Drag::MapLink ? pressHit_.id : (h.kind == Hit::AtomHit ? h.id : QString());
        if (!atom.isEmpty())
            if (Step* st = mapping::stepOfAtom(doc_, atom))
                for (const auto& id : mapping::partners(doc_, *st, atom)) deco.linked.insert(id);
    }
    for (const auto& w : active_) deco.warned.insert(w.target());
    deco.editingId = editingId_;
    const Molecule* stickyMol = stickyAtom_.isEmpty() ? nullptr : doc_.moleculeOfAtom(stickyAtom_);
    if (stickyMol) deco.hoverId.clear();  // the orange mark below says where keys go
    renderer_.draw(p, doc_, overlay_, &deco);

    // The atom a keyboard chain is growing from: light orange.
    if (stickyMol) {
        QPointF c = renderer_.toPt(stickyMol->atom(stickyAtom_)->pos());
        double r = style_.bondLength * 0.3;
        p.setPen(QPen(QColor(245, 150, 50), style_.lineWidth * 0.8));
        p.setBrush(QColor(255, 180, 100, 90));
        p.drawEllipse(c, r, r);
    }

    // Warning markers on arrows.
    for (const auto& w : active_) {
        if (const Arrow* a = doc_.arrow(w.objectId)) {
            ArrowGeom g = renderer_.arrowGeom(*a, &doc_);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(232, 152, 0));
            p.drawEllipse(g.head + QPointF(4, -6), style_.bondLength * 0.09, style_.bondLength * 0.09);
        }
    }

    if (preview_.size() >= 2) {
        QPen pen(QColor(66, 133, 244), style_.lineWidth * 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (drag_ == Drag::MapLink) {
            pen.setColor(QColor(230, 140, 20));
            pen.setDashPattern({3, 2});
        }
        p.setPen(pen);
        QPolygonF poly;
        for (auto& q : preview_) poly << renderer_.toPt(q);
        p.drawPolyline(poly);
        p.setBrush(QColor(66, 133, 244));
        p.setPen(Qt::NoPen);
        for (auto& q : poly) p.drawEllipse(q, style_.lineWidth * 1.5, style_.lineWidth * 1.5);
    }
    p.restore();

    if (drag_ == Drag::Rubber && !rubber_.isNull()) {
        p.setPen(QPen(QColor(66, 133, 244), 1, Qt::DashLine));
        p.setBrush(QColor(66, 133, 244, 30));
        p.drawRect(rubber_);
    }
    if (doc_.empty()) {
        p.setPen(style_.background.lightnessF() < 0.5 ? QColor(160, 160, 160) : QColor(140, 140, 140));
        QFont f = font();
        f.setPointSizeF(11);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   "Hover and type: C N O … for atoms, Shift+6 for a ring, r for benzene.\n"
                   "Pick Chain on the toolbar to draw chains, or paste a SMILES or InChI with Ctrl+V.");
    }
}

// ---------------------------------------------------------------------------
// Mouse

QSet<QString> Canvas::moveSetFor(const Hit& h) {
    QString obj = h.molId.isEmpty() ? h.id : h.molId;
    if (selected_.contains(obj)) return selected_;
    return {obj};
}

void Canvas::mousePressEvent(QMouseEvent* e) {
    setFocus();
    lastMouse_ = e->position();
    mouseInside_ = true;
    stickyAtom_.clear();
    updateHover(e->position());
    pressScreen_ = e->position();
    pressModel_ = toModel(e->position());
    lastDragModel_ = pressModel_;
    dragMoved_ = false;
    preview_.clear();
    Hit h = hover();
    pressHit_ = h;
    const auto mods = e->modifiers();

    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton ||
        (e->button() == Qt::LeftButton && spaceDown_)) {
        drag_ = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    auto startMove = [&](QSet<QString> set) {
        drag_ = Drag::Move;
        dragOrig_ = doc_;
        moveSet_ = std::move(set);
        // What snaps to the grid: the atom or bond end under the mouse, an arrow's tail, a text's corner.
        moveRef_ = pressModel_;
        if (h.kind == Hit::AtomHit) {
            if (const Molecule* m = doc_.moleculeOfAtom(h.id)) moveRef_ = m->atom(h.id)->pos();
        } else if (h.kind == Hit::BondHit) {
            if (const Molecule* m = doc_.molecule(h.molId))
                if (const Bond* b = m->bond(h.id)) moveRef_ = m->atom(b->a)->pos();
        } else if (h.kind == Hit::ArrowHit) {
            if (const Arrow* a = doc_.arrow(h.id)) moveRef_ = a->tail();
        } else if (h.kind == Hit::TextHit) {
            for (const auto& t : doc_.texts) if (t.id == h.id) moveRef_ = QPointF(t.x, t.y);
        }
    };
    if (mapMode_) {
        // Mapping mode only links atoms; everything else stays put.
        drag_ = h.kind == Hit::AtomHit ? Drag::MapLink : Drag::None;
        return;
    }
    // Arrow tools draw wherever you press, even over structures.
    if (tool_ == Tool::Arrow) { drag_ = Drag::DrawArrow; return; }
    if (tool_ == Tool::BentArrow) {
        bentPts_.push_back(bentSnap(pressModel_, !(mods & Qt::AltModifier)));
        drag_ = Drag::None;
        update();
        return;
    }
    if (tool_ == Tool::Text && h.kind != Hit::TextHit && h.kind != Hit::ArrowHit) {
        drag_ = Drag::None;
        addTextAt(pressModel_);
        return;
    }
    // Arrow ends win over anything else close by (an arrow usually points right at an atom).
    if (!(mods & Qt::ControlModifier)) {
        QString arrowId;
        bool head = false;
        if (arrowEndAt(e->position(), &arrowId, &head)) {
            pressHit_ = {Hit::ArrowHit, arrowId, {}, 0};
            dragOrig_ = doc_;
            drag_ = head ? Drag::ArrowHead : Drag::ArrowTail;
            return;
        }
    }
    if ((mods & Qt::ControlModifier) && h.kind != Hit::None) { startMove(moveSetFor(h)); return; }
    if ((mods & Qt::ShiftModifier) && h.kind == Hit::None) { drag_ = Drag::Rubber; rubber_ = {}; return; }

    switch (h.kind) {
    case Hit::AtomHit: drag_ = Drag::BondFromAtom; break;
    case Hit::BondHit: startMove(moveSetFor(h)); break;
    case Hit::ArrowHit: {
        ArrowGeom g = renderer_.arrowGeom(*doc_.arrow(h.id), &doc_);
        QPointF pt = toPt(e->position());
        double tol = std::max(style_.arrowHeadLength, 8 / zoom_);
        dragOrig_ = doc_;
        if (len(pt - g.head) < tol) { drag_ = Drag::ArrowHead; break; }
        if (len(pt - g.tail) < tol) { drag_ = Drag::ArrowTail; break; }
        if (g.path.size() > 2) {
            // Bent arrows reshape: grab a corner, or a segment to slide it sideways.
            // (Ctrl+drag, or Move in the right-click menu, moves the whole arrow.)
            for (size_t i = 1; i + 1 < g.path.size(); ++i)
                if (len(pt - g.path[i]) < tol) { drag_ = Drag::ArrowCorner; dragIndex_ = int(i); break; }
            if (drag_ == Drag::ArrowCorner) break;
            double best = 1e9;
            for (size_t i = 0; i + 1 < g.path.size(); ++i) {
                double dd = distToSegment(pt, g.path[i], g.path[i + 1]);
                if (dd < best) { best = dd; dragIndex_ = int(i); }
            }
            if (best < tol) { drag_ = Drag::ArrowSegment; break; }
        }
        startMove(moveSetFor(h));
        break;
    }
    case Hit::TextHit: startMove(moveSetFor(h)); break;
    case Hit::NumberHit: drag_ = Drag::None; break;
    default:
        // Empty canvas: marquee select, or a chain with the Chain tool.
        drag_ = tool_ == Tool::Chain ? Drag::Chain : Drag::Rubber;
        rubber_ = {};
        break;
    }
}

void Canvas::mouseMoveEvent(QMouseEvent* e) {
    QPointF pos = e->position();
    QPointF prev = lastMouse_;
    lastMouse_ = pos;
    mouseInside_ = true;
    if (drag_ == Drag::None) {
        if (tool_ == Tool::BentArrow && !bentPts_.empty()) {
            preview_ = bentPts_;
            preview_.push_back(bentSnap(toModel(pos), !(e->modifiers() & Qt::AltModifier)));
            update();
        }
        updateHover(pos);
        // A move cursor over arrow ends says "grab me" (not while drawing arrows or mapping).
        if (!mapMode_ && !spaceDown_ && tool_ != Tool::Arrow && tool_ != Tool::BentArrow) {
            QString id;
            bool head = false;
            if (arrowEndAt(pos, &id, &head)) setCursor(Qt::SizeAllCursor);
            else if (tool_ == Tool::Select) unsetCursor();
            else setCursor(tool_ == Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
        }
        return;
    }
    if (!dragMoved_ && (pos - pressScreen_).manhattanLength() > 4) dragMoved_ = true;
    const bool snap = !(e->modifiers() & Qt::AltModifier);
    const QPointF mp = toModel(pos);

    switch (drag_) {
    case Drag::Pan:
        fitPending_ = false;
        pan_ += pos - prev;
        break;
    case Drag::BondFromAtom: {
        if (!dragMoved_) break;
        const Molecule* m = doc_.molecule(pressHit_.molId);
        QPointF start = m->atom(pressHit_.id)->pos();
        auto hits = hitTest(pos);
        auto target = std::find_if(hits.begin(), hits.end(),
                                   [&](const Hit& x) { return x.kind == Hit::AtomHit && x.id != pressHit_.id; });
        if (target != hits.end()) {
            preview_ = {start, doc_.moleculeOfAtom(target->id)->atom(target->id)->pos()};
        } else if (len(mp - start) > 1.6) {
            preview_ = edits::chainPoints(doc_, pressHit_.id, start, mp, snap);
        } else {
            QPointF v = mp - start;
            double ang = std::atan2(v.y(), v.x());
            preview_ = {start, snap ? start + QPointF(std::cos(snapAngle(ang)), std::sin(snapAngle(ang))) : mp};
        }
        break;
    }
    case Drag::Chain:
        if (dragMoved_) preview_ = edits::chainPoints(doc_, std::nullopt, pressModel_, mp, snap);
        break;
    case Drag::DrawArrow: {
        if (!dragMoved_) break;
        QPointF v = mp - pressModel_;
        if (snap) {
            double ang = snapAngle(std::atan2(v.y(), v.x()), M_PI / 12);
            v = QPointF(std::cos(ang), std::sin(ang)) * len(v);
        }
        preview_ = {pressModel_, pressModel_ + v};
        break;
    }
    case Drag::MapLink: {
        if (!dragMoved_) break;
        QPointF start = doc_.molecule(pressHit_.molId)->atom(pressHit_.id)->pos();
        auto hits = hitTest(pos);
        auto target = std::find_if(hits.begin(), hits.end(),
                                   [&](const Hit& x) { return x.kind == Hit::AtomHit && x.id != pressHit_.id; });
        preview_ = {start, target != hits.end() ? doc_.molecule(target->molId)->atom(target->id)->pos() : mp};
        updateHover(pos);
        break;
    }
    case Drag::Move: {
        if (!dragMoved_) break;
        if (snapGrid_) {
            // From the original positions, so the reference point lands exactly on a grid point.
            doc_ = dragOrig_;
            edits::moveObjects(doc_, moveSet_, snapToGrid(moveRef_ + (mp - pressModel_)) - moveRef_);
        } else {
            edits::moveObjects(doc_, moveSet_, mp - lastDragModel_);
        }
        lastDragModel_ = mp;
        break;
    }
    case Drag::ArrowHead:
    case Drag::ArrowTail: {
        if (!dragMoved_) break;
        doc_ = dragOrig_;
        Arrow* a = doc_.arrow(pressHit_.id);
        // The end pivots around its neighbouring point (the last corner, for a bent arrow).
        auto path = a->path();
        QPointF fixed = drag_ == Drag::ArrowHead ? path[path.size() - 2] : path[1];
        QPointF v = mp - fixed;
        if (snap) {
            double ang = snapAngle(std::atan2(v.y(), v.x()), M_PI / 12);
            v = QPointF(std::cos(ang), std::sin(ang)) * len(v);
        }
        if (drag_ == Drag::ArrowHead) { a->x2 = fixed.x() + v.x(); a->y2 = fixed.y() + v.y(); }
        else { a->x1 = fixed.x() + v.x(); a->y1 = fixed.y() + v.y(); }
        break;
    }
    case Drag::ArrowCorner:
    case Drag::ArrowSegment: {
        if (!dragMoved_) break;
        doc_ = dragOrig_;
        Arrow* a = doc_.arrow(pressHit_.id);
        auto path = a->path();
        auto set = [&](int j, QPointF q) {
            if (j == 0) { a->x1 = q.x(); a->y1 = q.y(); }
            else if (j == int(path.size()) - 1) { a->x2 = q.x(); a->y2 = q.y(); }
            else a->via[j - 1] = q;
        };
        QPointF delta = mp - pressModel_;
        if (drag_ == Drag::ArrowCorner) {
            QPointF q = path[dragIndex_] + delta;
            if (snap) {
                // Line the corner up with its neighbours so legs stay straight.
                for (int nb : {dragIndex_ - 1, dragIndex_ + 1}) {
                    if (std::abs(q.x() - path[nb].x()) < 0.35) q.setX(path[nb].x());
                    if (std::abs(q.y() - path[nb].y()) < 0.35) q.setY(path[nb].y());
                }
            }
            set(dragIndex_, q);
        } else {
            // Slide the segment across itself: horizontal legs move up/down, vertical ones sideways.
            QPointF s = path[dragIndex_ + 1] - path[dragIndex_];
            QPointF shift = std::abs(s.x()) < 1e-6 ? QPointF(delta.x(), 0)
                            : std::abs(s.y()) < 1e-6 ? QPointF(0, delta.y()) : delta;
            set(dragIndex_, path[dragIndex_] + shift);
            set(dragIndex_ + 1, path[dragIndex_ + 1] + shift);
        }
        break;
    }
    case Drag::Rubber:
        rubber_ = QRectF(pressScreen_, pos).normalized();
        break;
    default: break;
    }
    update();
}

void Canvas::mouseReleaseEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    const QPointF mp = toModel(pos);
    const bool snap = !(e->modifiers() & Qt::AltModifier);
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    Drag d = drag_;
    drag_ = Drag::None;
    unsetCursor();
    if (spaceDown_) setCursor(Qt::OpenHandCursor);
    else if (mapMode_ || tool_ != Tool::Select) setCursor(tool_ == Tool::Text && !mapMode_ ? Qt::IBeamCursor : Qt::CrossCursor);

    auto clickSelect = [&](const QString& id) {
        if (shift) {
            if (selected_.contains(id)) selected_.remove(id);
            else selected_.insert(id);
        } else {
            selected_ = {id};
        }
        emit infoTargetChanged();
    };

    // A right click that didn't pan opens the menu for whatever is under the cursor.
    if (d == Drag::Pan && e->button() == Qt::RightButton && !dragMoved_) {
        preview_.clear();
        showContextMenu(pos);
        return;
    }

    switch (d) {
    case Drag::BondFromAtom: {
        const QString startId = pressHit_.id;
        if (!dragMoved_) {
            // A click on an atom of a structure that's already selected focuses that atom (orange):
            // keys then act on it, as when building a chain from the keyboard.
            const bool inSelection = selected_.contains(pressHit_.molId) || selected_.contains(startId);
            if (inSelection && !shift && !(e->modifiers() & Qt::AltModifier)) {
                stickyAtom_ = startId;
                updateHover(pos);
                update();
                emit status("Focused atom: keys act on it, arrow keys move to a neighbour, Esc finishes.");
                break;
            }
            clickSelect((e->modifiers() & Qt::AltModifier) ? startId : pressHit_.molId);
            break;
        }
        auto hits = hitTest(pos);
        auto target = std::find_if(hits.begin(), hits.end(),
                                   [&](const Hit& x) { return x.kind == Hit::AtomHit && x.id != startId; });
        if (target != hits.end()) {
            QString to = target->id;
            edit("Add bond", [&](Document& doc) { edits::bondAtoms(doc, startId, to, 1); });
        } else if (preview_.size() > 2) {
            QPointF start = preview_.front();
            edit("Add chain", [&](Document& doc) { edits::addChain(doc, startId, start, mp, snap); });
        } else if (preview_.size() == 2) {
            QPointF end = preview_.back();
            edit("Add bond", [&](Document& doc) { edits::bondToPoint(doc, startId, end, 1); });
        }
        break;
    }
    case Drag::Chain: {
        if (!dragMoved_) {
            if (!shift) selected_.clear();
            emit infoTargetChanged();
            break;
        }
        auto hits = hitTest(pos);
        std::optional<QString> endAtom;
        auto target = std::find_if(hits.begin(), hits.end(), [](const Hit& x) { return x.kind == Hit::AtomHit; });
        if (target != hits.end()) endAtom = target->id;
        QPointF start = pressModel_;
        edit("Add chain", [&](Document& doc) { edits::addChain(doc, std::nullopt, start, mp, snap, endAtom); });
        break;
    }
    case Drag::Move:
    case Drag::ArrowHead:
    case Drag::ArrowTail:
    case Drag::ArrowCorner:
    case Drag::ArrowSegment: {
        if (!dragMoved_) {
            doc_ = dragOrig_;
            clickSelect(pressHit_.molId.isEmpty() ? pressHit_.id : pressHit_.molId);
            break;
        }
        Document after = doc_;
        doc_ = dragOrig_;
        undo_.push(new SnapshotCommand(this, d == Drag::Move ? "Move" : "Reshape arrow", dragOrig_, after));
        break;
    }
    case Drag::Rubber: {
        if (!shift) selected_.clear();
        QRectF r(toPt(rubber_.topLeft()), toPt(rubber_.bottomRight()));
        for (const auto& m : doc_.molecules) {
            int inside = 0;
            for (const auto& a : m.atoms) if (r.contains(renderer_.toPt(a.pos()))) { selected_.insert(a.id); ++inside; }
            if (inside == (int)m.atoms.size() && inside > 0) {
                for (const auto& a : m.atoms) selected_.remove(a.id);
                selected_.insert(m.id);
            }
        }
        for (const auto& a : doc_.arrows) if (r.intersects(renderer_.arrowGeom(a, &doc_).bounds)) selected_.insert(a.id);
        for (const auto& t : doc_.texts) if (r.intersects(renderer_.textRect(t))) selected_.insert(t.id);
        rubber_ = {};
        emit infoTargetChanged();
        break;
    }
    case Drag::MapLink:
        if (dragMoved_) finishMapLink(pos);
        break;
    case Drag::DrawArrow: {
        QPointF tail = pressModel_;
        QPointF head = preview_.size() == 2 ? preview_.back() : tail + QPointF(3, 0);  // a click: default length
        if (len(head - tail) < 0.5) head = tail + QPointF(3, 0);
        edit("Add arrow", [&](Document& doc) {
            QString id = edits::addArrow(doc, tail, 3.0);
            Arrow* a = doc.arrow(id);
            a->x2 = head.x();
            a->y2 = head.y();
        });
        break;
    }
    case Drag::None:
        if (pressHit_.kind == Hit::NumberHit) clickSelect(pressHit_.id);
        break;
    default: break;
    }
    preview_.clear();
    updateHover(pos);
    update();
}

void Canvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (tool_ == Tool::BentArrow) { finishBentArrow(); return; }
    updateHover(e->position());
    Hit h = hover();
    switch (h.kind) {
    case Hit::AtomHit: break;  // labels are typed with X (or right-click → Edit Label…), not double-click
    case Hit::BondHit: selected_ = {h.molId}; update(); break;
    case Hit::ArrowHit: editArrow(h.id); break;
    case Hit::TextHit: editText(h.id); break;
    case Hit::NumberHit: editCompound(h.id); break;
    default: break;
    }
}

void Canvas::wheelEvent(QWheelEvent* e) {
    fitPending_ = false;
    if (e->modifiers() & Qt::ShiftModifier) {
        pan_ += QPointF(e->angleDelta().y(), e->angleDelta().x()) * 0.5;
        update();
        return;
    }
    // With "scroll wheel pans" on (trackpads), plain scrolling pans both ways and Ctrl+scroll zooms.
    if (wheelPans_ && !(e->modifiers() & Qt::ControlModifier)) {
        pan_ += QPointF(e->angleDelta().x(), e->angleDelta().y()) * 0.5;
        update();
        return;
    }
    zoomBy(std::pow(1.0015, e->angleDelta().y()), e->position());
}

void Canvas::leaveEvent(QEvent*) {
    mouseInside_ = false;
    if (drag_ == Drag::None) updateHover(lastMouse_);
}

bool Canvas::event(QEvent* e) {
    // Tab cycles hover targets instead of moving focus.
    if (e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            keyPressEvent(ke);
            return true;
        }
    }
    return QWidget::event(e);
}

// ---------------------------------------------------------------------------
// Keys

void Canvas::keyReleaseEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) {
        spaceDown_ = false;
        unsetCursor();
    }
}

void Canvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Space) {
        if (!e->isAutoRepeat()) { spaceDown_ = true; setCursor(Qt::OpenHandCursor); }
        return;
    }
    if (tool_ == Tool::BentArrow && !bentPts_.empty() &&
        (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Escape)) {
        if (e->key() == Qt::Key_Escape) bentPts_.clear();
        finishBentArrow();
        return;
    }
    if (e->key() == Qt::Key_Escape && inline_) {
        inline_->deleteLater();
        inline_ = nullptr;
        editingId_.clear();
        setFocus();
        update();
        return;
    }
    // Arrow keys nudge the selection.
    const QHash<int, QPointF> nudge = {{Qt::Key_Left, {-1, 0}}, {Qt::Key_Right, {1, 0}},
                                       {Qt::Key_Up, {0, -1}}, {Qt::Key_Down, {0, 1}}};
    if (nudge.contains(e->key()) && !stickyAtom_.isEmpty() && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        moveFocus(nudge[e->key()]);
        return;
    }
    if (nudge.contains(e->key()) && !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
        if (selected_.isEmpty()) return;
        QPointF dlt = nudge[e->key()] * ((e->modifiers() & Qt::ShiftModifier) ? 0.5 : 0.05);
        QSet<QString> ids = selected_;
        edit("Nudge", [&](Document& d) { edits::moveObjects(d, ids, dlt); }, 1);
        return;
    }

    QString key = Keymap::keyName(e);
    if (key.isEmpty()) { QWidget::keyPressEvent(e); return; }
    if (drag_ != Drag::None && key != "Escape") return;
    if (key == "Escape" && drag_ != Drag::None) {
        if (drag_ == Drag::Move || drag_ == Drag::ArrowHead || drag_ == Drag::ArrowTail || drag_ == Drag::ArrowCorner ||
            drag_ == Drag::ArrowSegment)
            doc_ = dragOrig_;
        drag_ = Drag::None;
        preview_.clear();
        update();
        return;
    }

    if (key == "Escape" && !stickyAtom_.isEmpty()) {
        // Ends a keyboard chain (and deselects, as Escape always does).
        stickyAtom_.clear();
        selected_.clear();
        updateHover(lastMouse_);
        update();
        emit infoTargetChanged();
        return;
    }

    Hit h = hover();
    QString target = (mouseInside_ || !stickyAtom_.isEmpty()) ? hoverTargetName(h) : QString();
    if (target.isEmpty() && !selected_.isEmpty()) {
        applyToSelection(key);
        return;
    }
    if (target.isEmpty()) target = "empty";
    QString action = keymap_.action(target, key);
    if (action.isEmpty()) { e->ignore(); return; }
    QPointF cursor = mouseInside_ ? toModel(lastMouse_) : toModel(QPointF(width() / 2.0, height() / 2.0));
    runAction(action, h, cursor);
}

void Canvas::applyToSelection(const QString& key) {
    QString any = keymap_.action("any", key);
    if (any == "delete") { deleteSelection(); return; }
    if (any == "escape") {
        if (mapMode_) { setMapMode(false); return; }
        selected_.clear();
        update();
        emit infoTargetChanged();
        return;
    }

    QStringList atoms, bonds;
    for (const auto& m : doc_.molecules) {
        for (const auto& a : m.atoms) if (selected_.contains(a.id)) atoms << a.id;
        for (const auto& b : m.bonds) if (selected_.contains(b.id)) bonds << b.id;
    }
    QString aAct = keymap_.action("atom", key), bAct = keymap_.action("bond", key);
    auto verb = [](const QString& a) { return a.section(':', 0, 0); };
    auto arg = [](const QString& a) { return a.section(':', 1); };
    bool atomOk = !atoms.isEmpty() && QStringList{"element", "charge", "radical"}.contains(verb(aAct));
    bool bondOk = !bonds.isEmpty() && QStringList{"order", "stereo", "flip"}.contains(verb(bAct));
    if (!atomOk && !bondOk) {
        // Nothing applicable in the selection: treat as empty canvas.
        QString action = keymap_.action("empty", key);
        if (!action.isEmpty()) runAction(action, {}, mouseInside_ ? toModel(lastMouse_) : toModel(rect().center()));
        return;
    }
    edit("Edit selection", [&](Document& d) {
        if (atomOk)
            for (const auto& id : atoms) {
                if (verb(aAct) == "element") edits::setElement(d, id, arg(aAct));
                else if (verb(aAct) == "charge") edits::changeCharge(d, id, arg(aAct).toInt());
                else edits::toggleRadical(d, id);
            }
        if (bondOk)
            for (const auto& id : bonds) {
                if (verb(bAct) == "order") edits::setBondOrder(d, id, arg(bAct).toInt());
                else if (verb(bAct) == "flip") edits::flipBond(d, id);
                else {
                    QString s = arg(bAct);
                    edits::setStereo(d, id, s == "wedge" ? BondStereo::Wedge : s == "hash" ? BondStereo::Hash : BondStereo::Wavy);
                }
            }
    });
}

void Canvas::runAction(const QString& action, const Hit& h, QPointF cursor) {
    const QString verb = action.section(':', 0, 0);
    const QString arg = action.section(':', 1);
    const QString id = h.id;

    if (verb == "tool") {
        static const QHash<QString, Tool> tools = {{"select", Tool::Select}, {"chain", Tool::Chain}, {"arrow", Tool::Arrow},
                                                   {"bent", Tool::BentArrow}, {"text", Tool::Text}};
        if (tools.contains(arg)) setTool(tools[arg]);
        return;
    }
    if (verb == "mapmode") {
        setMapMode(!mapMode_);
        return;
    }
    if (verb == "cycle") {
        if (hits_.size() > 1) {
            hoverIndex_ = (hoverIndex_ + 1) % int(hits_.size());
            update();
            updateStatus();
            emit infoTargetChanged();
        }
        return;
    }
    if (verb == "escape") {
        if (mapMode_) { setMapMode(false); return; }
        selected_.clear();
        update();
        emit infoTargetChanged();
        return;
    }
    if (verb == "delete") {
        if (h.kind == Hit::None) { deleteSelection(); return; }
        if (h.kind == Hit::NumberHit) {
            edit("Remove number", [&](Document& d) {
                if (Molecule* m = d.molecule(id)) { m->number.clear(); m->numberManual = true; m->caption.clear(); }
            });
            return;
        }
        edit("Delete", [&](Document& d) { edits::remove(d, id); });
        return;
    }

    switch (h.kind) {
    case Hit::AtomHit:
        if (verb == "element") edit("Set element", [&](Document& d) { edits::setElement(d, id, arg); });
        else if (verb == "label") editLabel(id);
        else if (verb == "charge") edit("Charge", [&](Document& d) { edits::changeCharge(d, id, arg.toInt()); });
        else if (verb == "radical") edit("Radical", [&](Document& d) { edits::toggleRadical(d, id); });
        else if (verb == "grow") {
            // Lean toward the mouse (growBond ignores it when it's behind the chain end, so a mouse
            // left where the chain started doesn't curl the chain back).
            std::optional<QPointF> toward;
            const Molecule* m = doc_.moleculeOfAtom(id);
            if (m && steerToMouse_ && mouseInside_ && len(cursor - m->atom(id)->pos()) > 0.15) toward = cursor;
            QString newAtom;
            edit("Add bond", [&](Document& d) { newAtom = edits::growBond(d, id, arg.toInt(), toward); });
            if (!newAtom.isEmpty() && newAtom != id) {
                stickyAtom_ = newAtom;
                updateHover(lastMouse_);
                update();
            }
        }
        else if (verb == "ring" || verb == "benzene") {
            // Like a new bond, a ring hung off a chain leans toward the mouse.
            std::optional<QPointF> toward;
            const Molecule* m = doc_.moleculeOfAtom(id);
            if (m && steerToMouse_ && mouseInside_ && len(cursor - m->atom(id)->pos()) > 0.15) toward = cursor;
            bool benz = verb == "benzene";
            edit(benz ? "Add benzene" : "Add ring",
                 [&](Document& d) { edits::attachRing(d, id, benz ? 6 : arg.toInt(), benz, toward); });
        }
        else if (verb == "expand") {
            // A label expands to its structure; an atom of an expanded group collapses it back.
            const Molecule* m = doc_.moleculeOfAtom(id);
            QString anchor;
            for (const auto& g : m->groups) if (g.atoms.contains(id)) anchor = g.anchor;
            if (chem::canExpand(*m->atom(id))) {
                edit("Expand label", [&](Document& d) { chem::expandLabel(d, id); });
            } else if (!anchor.isEmpty()) {
                QString molId = m->id;
                edit("Collapse label", [&](Document& d) { edits::contractGroup(d, molId, anchor); });
                if (!stickyAtom_.isEmpty()) stickyAtom_ = anchor;  // the focused atom may be gone: back to the label
                updateHover(lastMouse_);
            } else {
                emit status("Nothing to expand here: this atom has no group label.");
            }
        }
        else if (verb == "group") {
            std::optional<QPointF> toward;
            const Molecule* m = doc_.moleculeOfAtom(id);
            if (m && steerToMouse_ && mouseInside_ && len(cursor - m->atom(id)->pos()) > 0.15) toward = cursor;
            edit("Add " + arg, [&](Document& d) { edits::attachGroup(d, id, arg, toward); });
        }
        else if (verb == "number") editCompound(h.molId);
        return;
    case Hit::BondHit:
        if (verb == "order") edit("Bond order", [&](Document& d) { edits::setBondOrder(d, id, arg.toInt()); });
        else if (verb == "stereo") {
            BondStereo s = arg == "wedge" ? BondStereo::Wedge : arg == "hash" ? BondStereo::Hash : BondStereo::Wavy;
            edit("Bond stereo", [&](Document& d) { edits::setStereo(d, id, s); });
        } else if (verb == "flip") edit("Flip bond", [&](Document& d) { edits::flipBond(d, id); });
        else if (verb == "ring") edit("Fuse ring", [&](Document& d) { edits::fuseRing(d, id, arg.toInt(), false); });
        else if (verb == "benzene") edit("Fuse benzene", [&](Document& d) { edits::fuseRing(d, id, 6, true); });
        else if (verb == "number") editCompound(h.molId);
        return;
    case Hit::ArrowHit:
        if (verb == "annotate") editArrow(id);
        else if (verb == "kind") edit("Arrow kind", [&](Document& d) { edits::cycleArrowKind(d, id); });
        return;
    case Hit::TextHit:
        if (verb == "edit") editText(id);
        return;
    case Hit::NumberHit:
        if (verb == "edit") editCompound(id);
        return;
    default: break;
    }

    // Empty canvas: create something at the cursor.
    if (verb == "element") edit("Add atom", [&](Document& d) { edits::addAtom(d, cursor, arg); });
    else if (verb == "ring") edit("Add ring", [&](Document& d) { edits::placeRing(d, cursor, arg.toInt(), false); });
    else if (verb == "benzene") edit("Add benzene", [&](Document& d) { edits::placeRing(d, cursor, 6, true); });
    else if (verb == "group") edit("Add " + arg, [&](Document& d) { edits::setLabel(d, edits::addAtom(d, cursor), arg); });
    else if (verb == "arrow") edit("Add arrow", [&](Document& d) { edits::addArrow(d, cursor - QPointF(1.5, 0), 3.0); });
    else if (verb == "text") {
        bool ok = false;
        QString t = QInputDialog::getMultiLineText(this, "Add text", "Text:", {}, &ok);
        if (ok && !t.trimmed().isEmpty()) edit("Add text", [&](Document& d) { edits::addText(d, cursor, t); });
    }
}

// ---------------------------------------------------------------------------
// Inline editing and dialogs

void Canvas::startInlineEditor(const QString& initial, QPointF screenPos, std::function<void(const QString&)> done) {
    if (inline_) inline_->deleteLater();
    auto* le = new QLineEdit(this);
    inline_ = le;
    le->setText(initial);
    le->selectAll();
    QFont f = le->font();
    f.setPointSizeF(std::clamp(style_.fontSize * zoom_ * 0.75, 9.0, 28.0));
    le->setFont(f);
    le->setFixedWidth(160);
    le->move(int(screenPos.x() - 20), int(screenPos.y() - le->sizeHint().height() / 2));
    auto* completer = new QCompleter(labels::knownAbbreviations(), le);
    completer->setCaseSensitivity(Qt::CaseSensitive);
    le->setCompleter(completer);
    le->show();
    le->setFocus();
    auto finished = std::make_shared<bool>(false);
    auto commit = [this, le, done, finished]() {
        if (*finished) return;
        *finished = true;
        QString text = le->text();
        le->deleteLater();
        inline_ = nullptr;
        editingId_.clear();
        setFocus();
        done(text);
        update();
    };
    connect(le, &QLineEdit::returnPressed, this, commit);
    connect(le, &QLineEdit::editingFinished, this, commit);
}

void Canvas::editLabel(const QString& atomId) {
    const Molecule* m = doc_.moleculeOfAtom(atomId);
    if (!m) return;
    const Atom* a = m->atom(atomId);
    QString initial = !a->label.isEmpty() ? a->label : (a->el == "C" ? QString() : a->el);
    editingId_ = atomId;
    startInlineEditor(initial, modelToScreen(a->pos()), [this, atomId](const QString& text) {
        edit("Label", [&](Document& d) { edits::setLabel(d, atomId, text); });
    });
}

void Canvas::editCompound(const QString& molId) {
    const Molecule* m = doc_.molecule(molId);
    if (!m) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Compound");
    auto* form = new QFormLayout;
    auto* number = new QLineEdit(m->number);
    auto* manual = new QCheckBox("Set number manually (otherwise numbered in scheme order)");
    manual->setChecked(m->numberManual);
    auto* caption = new QLineEdit(m->caption);
    caption->setPlaceholderText("e.g. 85% or 2.0 equiv");
    form->addRow("Number:", number);
    form->addRow("", manual);
    form->addRow("Caption:", caption);
    connect(number, &QLineEdit::textEdited, manual, [manual] { manual->setChecked(true); });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted) return;
    QString num = number->text().trimmed(), cap = caption->text().trimmed();
    bool man = manual->isChecked();
    edit("Compound number", [&](Document& d) {
        Molecule* mm = d.molecule(molId);
        if (!mm) return;
        mm->number = num;
        mm->numberManual = man;
        mm->caption = cap;
        if (!man) edits::renumber(d);
    });
}

void Canvas::editArrow(const QString& arrowId) {
    const Arrow* a = doc_.arrow(arrowId);
    if (!a) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Arrow");
    auto* kind = new QComboBox;
    const QStringList kinds = {"Forward", "Equilibrium", "Retrosynthesis", "Dashed", "Crossed (no reaction)", "Resonance"};
    kind->addItems(kinds);
    kind->setCurrentIndex(int(a->kind));
    auto* above = new QPlainTextEdit(a->above);
    auto* below = new QPlainTextEdit(a->below);
    above->setPlaceholderText("Reagents, e.g. NaOH, H2O");
    below->setPlaceholderText("Conditions, e.g. rt, 2 h");
    for (auto* t : {above, below}) t->setFixedHeight(70);
    auto* form = new QFormLayout;
    form->addRow("Kind:", kind);
    form->addRow("Above:", above);
    form->addRow("Below:", below);
    auto* hint = new QLabel("Formulas are subscripted automatically (H2O → H₂O). One item per line.");
    hint->setStyleSheet("color: gray");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addLayout(form);
    lay->addWidget(hint);
    lay->addWidget(buttons);
    dlg.resize(420, 0);
    if (dlg.exec() != QDialog::Accepted) return;
    ArrowKind k = ArrowKind(kind->currentIndex());
    QString ab = above->toPlainText().trimmed(), be = below->toPlainText().trimmed();
    edit("Arrow", [&](Document& d) {
        if (Arrow* x = d.arrow(arrowId)) { x->kind = k; x->above = ab; x->below = be; }
    });
}

void Canvas::editText(const QString& textId) {
    TextBox* t = doc_.text(textId);
    if (!t) return;
    bool ok = false;
    QString text = QInputDialog::getMultiLineText(this, "Edit text", "Text:", t->text, &ok);
    if (!ok) return;
    edit("Edit text", [&](Document& d) {
        if (text.trimmed().isEmpty()) edits::remove(d, textId);
        else if (TextBox* x = d.text(textId)) x->text = text;
    });
}

// ---------------------------------------------------------------------------
// Commands

void Canvas::cleanStructure() {
    QStringList targets;
    Hit h = hover();
    if (!h.molId.isEmpty()) targets << h.molId;
    else {
        for (const auto& m : doc_.molecules) {
            bool sel = selected_.contains(m.id);
            for (const auto& a : m.atoms) sel |= selected_.contains(a.id);
            if (sel) targets << m.id;
        }
        if (targets.isEmpty()) for (const auto& m : doc_.molecules) targets << m.id;
    }
    if (targets.isEmpty()) return;

    if (targets.size() == 1) {
        // One structure: pressing Clean again steps through orientations, all as one undo step.
        const QString id = targets.front();
        if (cycle_.mol == id && cycle_.undoIndex == undo_.index() && cycle_.variants.size() > 1) {
            cycle_.index = (cycle_.index + 1) % int(cycle_.variants.size());
            const Molecule v = cycle_.variants[cycle_.index];
            edit("Clean structure", [&](Document& d) {
                if (Molecule* m = d.molecule(id)) { m->atoms = v.atoms; m->bonds = v.bonds; }
            }, cycle_.mergeKey);
            cycle_.undoIndex = undo_.index();
            emit status(QString("Clean: %1 (%2 of %3) — press Ctrl+L again for the next orientation")
                            .arg(cycle_.names[cycle_.index]).arg(cycle_.index + 1).arg(cycle_.variants.size()));
            return;
        }
        Molecule cleaned = *doc_.molecule(id);
        chem::cleanup(cleaned);
        std::vector<Molecule> variants{cleaned};
        QStringList names{"standard layout"};
        QString refName;
        QString ref = alignReference(id, &refName);
        if (!ref.isEmpty()) {
            Molecule aligned = cleaned;
            if (chem::alignTo(aligned, *doc_.molecule(ref))) {
                variants.push_back(aligned);
                names << "matching " + refName;
            }
        }
        const QStringList turns = {"long axis across", "flipped", "turned over", "turned over, flipped",
                                   "upright", "upright, flipped", "upright, turned", "upright, turned, flipped"};
        auto vs = edits::orientationVariants(cleaned);
        for (size_t i = 0; i < vs.size(); ++i) {
            variants.push_back(vs[i]);
            names << (i < size_t(turns.size()) ? turns[int(i)] : QString("orientation %1").arg(i + 1));
        }
        cycle_ = {id, -1, 0, 4000 + (++cycleSerial_ % 1000), variants, names};
        edit("Clean structure", [&](Document& d) {
            if (Molecule* m = d.molecule(id)) { m->atoms = cleaned.atoms; m->bonds = cleaned.bonds; }
        }, cycle_.mergeKey);
        cycle_.undoIndex = undo_.index();
        emit status(QString("Clean: standard layout (1 of %1) — press Ctrl+L again to try other orientations%2")
                        .arg(variants.size()).arg(ref.isEmpty() ? QString() : ", including one matching " + refName));
        return;
    }
    edit("Clean structure", [&](Document& d) {
        for (const auto& id : targets)
            if (Molecule* m = d.molecule(id)) chem::cleanup(*m);
    });
}

void Canvas::arrangeScheme() {
    if (doc_.arrows.empty()) {
        emit status("Arrange scheme needs at least one arrow between structures.");
        return;
    }
    // With a selection, only that part is tidied; the rest stays where you put it.
    QSet<QString> only;
    for (const auto& id : selected_) {
        if (const Molecule* m = doc_.moleculeOfAtom(id)) only.insert(m->id);
        else only.insert(id);
    }
    edit(only.isEmpty() ? "Arrange scheme" : "Arrange selection", [&](Document& d) {
        edits::buildScheme(d);
        layout::layoutSchemes(d, renderer_, overlay_, only.isEmpty() ? nullptr : &only);
    });
    if (!only.isEmpty()) emit status("Arranged the selected part; everything else was left where it was.");
}

std::vector<Step> Canvas::inferredSteps() const {
    // The document's scheme if it has one, else one read from the drawing (without changing it).
    Document tmp = doc_;
    if (tmp.schemes.empty()) edits::buildScheme(tmp);
    std::vector<Step> out;
    for (const auto& s : tmp.schemes) out.insert(out.end(), s.steps.begin(), s.steps.end());
    return out;
}

QString Canvas::alignReference(const QString& molId, QString* name, bool* isReactant) const {
    // A product lines up with its biggest reactant; failing that, a reactant with its biggest product.
    auto biggest = [&](const QStringList& ids) {
        QString best;
        size_t n = 0;
        for (const auto& id : ids)
            if (const Molecule* m = doc_.molecule(id); m && id != molId && m->atoms.size() > n) { n = m->atoms.size(); best = id; }
        return best;
    };
    auto describe = [&](const QString& id) {
        const Molecule* m = doc_.molecule(id);
        QString n = m && !m->number.isEmpty() ? m->number : QString("the neighbouring structure");
        return n.remove('*');
    };
    for (const auto& st : inferredSteps())
        if (st.products.contains(molId)) {
            QString ref = biggest(st.reactants);
            if (!ref.isEmpty()) {
                if (name) *name = describe(ref);
                if (isReactant) *isReactant = true;
                return ref;
            }
        }
    for (const auto& st : inferredSteps())
        if (st.reactants.contains(molId)) {
            QString ref = biggest(st.products);
            if (!ref.isEmpty()) {
                if (name) *name = describe(ref);
                if (isReactant) *isReactant = false;
                return ref;
            }
        }
    return {};
}

void Canvas::alignToNeighbour(const QString& molId, const QString& refId) {
    const Molecule* m0 = doc_.molecule(molId);
    const Molecule* r = doc_.molecule(refId);
    if (!m0 || !r) return;
    Molecule aligned = *m0;  // try first, so a failure leaves no empty undo step
    if (!chem::alignTo(aligned, *r)) {
        emit status("Those structures don't share enough to line up.");
        return;
    }
    edit("Align structure", [&](Document& d) {
        if (Molecule* m = d.molecule(molId)) { m->atoms = aligned.atoms; m->bonds = aligned.bonds; }
    });
}

void Canvas::alignAllProducts() {
    // Down the pathway in order, so each product follows the one before it.
    QSet<QString> only;
    for (const auto& id : selected_) {
        if (const Molecule* m = doc_.moleculeOfAtom(id)) only.insert(m->id);
        else only.insert(id);
    }
    const auto steps = inferredSteps();
    int aligned = 0;
    edit("Align products to reactants", [&](Document& d) {
        for (const auto& st : steps) {
            QString ref;
            size_t n = 0;
            for (const auto& id : st.reactants)
                if (const Molecule* m = d.molecule(id); m && m->atoms.size() > n) { n = m->atoms.size(); ref = id; }
            if (ref.isEmpty()) continue;
            for (const auto& p : st.products) {
                if (!only.isEmpty() && !only.contains(p)) continue;
                if (Molecule* m = d.molecule(p); m && chem::alignTo(*m, *d.molecule(ref))) ++aligned;
            }
        }
    });
    emit status(aligned ? QString("Lined up %1 product%2 with %3 reactant%2.").arg(aligned).arg(aligned == 1 ? "" : "s").arg(aligned == 1 ? "its" : "their")
                        : QString("No products to line up: draw arrows between structures first."));
}
void Canvas::numberCompounds() {
    edit("Number compounds", [&](Document& d) {
        for (auto& m : d.molecules) m.numberManual = false;
        edits::renumber(d);
    });
}

void Canvas::insertMolecules(const std::vector<Molecule>& mols, std::optional<QPointF> where) {
    if (mols.empty()) return;
    QPointF at = where ? *where : mouseInside_ ? toModel(lastMouse_) : toModel(rect().center());
    QStringList ids;
    edit("Insert structure", [&](Document& d) { ids = edits::insertMolecules(d, mols, at); });
    selected_ = QSet<QString>(ids.begin(), ids.end());
    update();
}

void Canvas::insertSmiles(const QString& smiles, std::optional<QPointF> at) {
    QString err;
    if (auto m = chem::fromSmiles(smiles, &err)) insertMolecules({*m}, at);
    else emit status("Could not read SMILES: " + err);
}

void Canvas::selectAll() {
    selected_.clear();
    for (const auto& m : doc_.molecules) selected_.insert(m.id);
    for (const auto& a : doc_.arrows) selected_.insert(a.id);
    for (const auto& t : doc_.texts) selected_.insert(t.id);
    update();
    emit infoTargetChanged();
}

void Canvas::deleteSelection() {
    if (selected_.isEmpty()) return;
    QSet<QString> ids = selected_;
    edit("Delete", [&](Document& d) {
        for (const auto& id : ids) edits::remove(d, id);
    });
    selected_.clear();
}

void Canvas::copy() {
    Document sub = io::subset(doc_, selected_);
    if (sub.empty()) return;
    QApplication::clipboard()->setMimeData(io::mimeData(sub, renderer_, overlay_));
    emit status("Copied as image, SVG and SMILES.");
}

void Canvas::cut() {
    copy();
    deleteSelection();
}

void Canvas::paste() {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    if (!mime) return;
    QPointF at = mouseInside_ ? toModel(lastMouse_) : toModel(rect().center());
    if (mime->hasFormat(io::kMimeType)) {
        auto src = io::fromJson(mime->data(io::kMimeType));
        if (!src) return;
        QSet<QString> newIds;
        edit("Paste", [&](Document& d) {
            QRectF b;
            for (const auto& m : src->molecules) b |= m.bounds().adjusted(-0.01, -0.01, 0.01, 0.01);
            for (const auto& a : src->arrows) b |= QRectF(a.tail(), a.head()).normalized().adjusted(-0.01, -0.01, 0.01, 0.01);
            for (const auto& t : src->texts) b |= QRectF(t.x, t.y, 0.01, 0.01);
            QPointF off = at - b.center();
            for (auto m : src->molecules) {
                QHash<QString, QString> ren;
                for (auto& a : m.atoms) ren[a.id] = d.newId("a");
                m.renameAtoms(ren);
                for (auto& bd : m.bonds) bd.id = d.newId("b");
                m.id = d.newId("m");
                m.translate(off);
                newIds.insert(m.id);
                d.molecules.push_back(m);
            }
            for (auto a : src->arrows) { a.id = d.newId("r"); a.translate(off); newIds.insert(a.id); d.arrows.push_back(a); }
            for (auto t : src->texts) { t.id = d.newId("t"); t.x += off.x(); t.y += off.y(); newIds.insert(t.id); d.texts.push_back(t); }
        });
        selected_ = newIds;
        update();
        return;
    }
    if (mime->hasText()) {
        QString err;
        auto mols = chem::fromText(mime->text(), &err);
        if (mols.empty()) emit status("Clipboard text is not a SMILES, InChI or MOL block." + (err.isEmpty() ? QString() : " (" + err + ")"));
        else insertMolecules(mols);
    }
}

void Canvas::zoomBy(double factor, QPointF anchor) {
    if (anchor.isNull()) anchor = rect().center();
    fitPending_ = false;
    double nz = std::clamp(zoom_ * factor, 0.15, 30.0);
    pan_ = anchor - (anchor - pan_) * (nz / zoom_);
    zoom_ = nz;
    if (inline_) inline_->deleteLater();
    update();
}

void Canvas::resetZoom() {
    fitPending_ = false;
    zoom_ = 2.0 * 15.0 / style_.bondLength;
    pan_ = QPointF(60, 60);
    update();
}

void Canvas::zoomToFit() {
    QRectF b = renderer_.documentBounds(doc_, overlay_);
    if (b.isNull()) { resetZoom(); return; }
    fitPending_ = true;  // stay fitted through resizes until the user pans, zooms or edits
    if (width() < 10) return;
    b.adjust(-20, -20, 20, 20);
    zoom_ = std::clamp(std::min(width() / b.width(), height() / b.height()), 0.15, 4.0 * 15.0 / style_.bondLength);
    pan_ = QPointF(width() / 2.0, height() / 2.0) - b.center() * zoom_;
    update();
}

void Canvas::goTo(const Warning& w) {
    QString t = w.target();
    if (t == "style") return;
    selected_ = {t};
    QPointF c;
    if (const Molecule* m = doc_.moleculeOfAtom(t)) c = renderer_.toPt(m->atom(t)->pos());
    else if (Molecule* m = doc_.moleculeOfBond(t)) {
        const Bond* b = m->bond(t);
        c = renderer_.toPt((m->atom(b->a)->pos() + m->atom(b->b)->pos()) / 2);
    } else if (const Arrow* a = doc_.arrow(t)) c = renderer_.toPt((a->tail() + a->head()) / 2);
    else if (const Molecule* m = doc_.molecule(t)) c = renderer_.toPt(m->center());
    pan_ = QPointF(width() / 2.0, height() / 2.0) - c * zoom_;
    update();
    emit infoTargetChanged();
}

void Canvas::setStyleName(const QString& name) {
    if (name == doc_.styleName && doc_.styleText.isEmpty()) return;
    edit("Change style", [&](Document& d) { d.styleName = name; d.styleText.clear(); });
}

void Canvas::setStyleText(const QString& text) {
    QString name = styles::declaredName(text);
    if (text == doc_.styleText) return;
    edit("Edit style", [&](Document& d) {
        d.styleText = text;
        if (!name.isEmpty()) d.styleName = name;
    }, 2);
}

void Canvas::setStepLetters(bool on) {
    if (doc_.stepLetters == on) return;
    edit(on ? "Show step letters" : "Hide step letters", [&](Document& d) { d.stepLetters = on; });
}

void Canvas::setShowMaps(bool on) {
    if (doc_.showMaps == on) return;
    edit(on ? "Show atom maps" : "Hide atom maps", [&](Document& d) { d.showMaps = on; });
}

QSet<QString> Canvas::transformTargets(const Hit& h) const {
    // The hovered object, or the whole selection when the hovered object is part of it.
    QString obj = h.molId.isEmpty() ? h.id : h.molId;
    if (!selected_.contains(obj)) return {obj};
    QSet<QString> out;
    for (const auto& id : selected_) {
        if (const Molecule* m = doc_.moleculeOfAtom(id)) out.insert(m->id);  // an atom stands for its molecule
        else out.insert(id);
    }
    return out;
}

void Canvas::showContextMenu(QPointF screen) {
    updateHover(screen);
    Hit h = hover();
    QMenu menu(this);
    const QPointF cursor = toModel(screen);

    // Duplicate and stacking order, for anything that can be selected.
    auto addArrange = [&](QMenu* into, const QSet<QString>& ids) {
        into->addAction("Duplicate", this, [this, ids] {
            QSet<QString> copies;
            edit("Duplicate", [&](Document& d) { copies = edits::duplicate(d, ids, QPointF(1.0, 1.0)); });
            selected_ = copies;
            update();
            emit infoTargetChanged();
        });
        into->addAction("Bring to Front", this, [this, ids] { edit("Bring to front", [&](Document& d) { edits::restack(d, ids, true); }); });
        into->addAction("Send to Back", this, [this, ids] { edit("Send to back", [&](Document& d) { edits::restack(d, ids, false); }); });
    };

    auto addTransforms = [&](QMenu* into, const QSet<QString>& ids) {
        struct T { const char* name; double a, b, c, d; };
        const T ts[] = {{"Flip Horizontal", -1, 0, 0, 1}, {"Flip Vertical", 1, 0, 0, -1},
                        {"Rotate 90° Clockwise", 0, -1, 1, 0}, {"Rotate 90° Counter-clockwise", 0, 1, -1, 0},
                        {"Rotate 180°", -1, 0, 0, -1}};
        for (const auto& t : ts) {
            QString name = t.name;
            into->addAction(name, this, [this, ids, t, name] {
                edit(name, [&](Document& d) { edits::transformObjects(d, ids, t.a, t.b, t.c, t.d); });
            });
        }
    };

    if (!h.molId.isEmpty() && h.kind != Hit::NumberHit) {
        const Molecule* m = doc_.molecule(h.molId);
        const QSet<QString> ids = transformTargets(h);
        const QString molId = h.molId;

        if (h.kind == Hit::AtomHit) {
            const Atom* a = m->atom(h.id);
            const QString atomId = h.id;
            if (chem::canExpand(*a)) {
                menu.addAction(QString("Show %1 as Structure").arg(a->label), this, [this, atomId, label = a->label] {
                    edit("Expand " + label, [&](Document& d) { chem::expandLabel(d, atomId); });
                });
            }
            if (const Group* g = m->groupOf(atomId)) {
                menu.addAction(QString("Show as Label (%1)").arg(g->label), this, [this, molId, anchor = g->anchor, label = g->label] {
                    edit("Collapse to " + label, [&](Document& d) { edits::contractGroup(d, molId, anchor); });
                });
            }
            menu.addAction("Edit Label…", this, [this, atomId] { editLabel(atomId); });
            auto* am = menu.addMenu("Atom");
            const QList<QPair<QString, int>> charges = {{"Charge +1", 1}, {QString("Charge ") + QChar(0x2212) + "1", -1}, {"Neutral", 0}};
            for (const auto& entry : charges) {
                const QString label = entry.first;
                const int q = entry.second;
                QAction* act = am->addAction(label, this, [this, atomId, q] { edit("Charge", [&](Document& d) { edits::setCharge(d, atomId, q); }); });
                act->setCheckable(true);
                act->setChecked(a->charge == q);
            }
            QAction* rad = am->addAction("Radical", this, [this, atomId] { edit("Radical", [&](Document& d) { edits::toggleRadical(d, atomId); }); });
            rad->setCheckable(true);
            rad->setChecked(a->radical != 0);
            am->addSeparator();
            const int curIso = a->isotope, curMap = a->map;
            const QString el = a->el;
            am->addAction(curIso ? QString("Isotope (%1%2)…").arg(curIso).arg(el) : QString("Isotope…"), this, [this, atomId, curIso, el] {
                bool ok = false;
                int v = QInputDialog::getInt(this, "Isotope", QString("Mass number for %1 (0 = natural; 13 for carbon-13):").arg(el),
                                             curIso, 0, 300, 1, &ok);
                if (ok) edit("Isotope", [&](Document& d) { edits::setIsotope(d, atomId, v); });
            });
            am->addAction(curMap ? QString("Map Number (%1)…").arg(curMap) : QString("Map Number…"), this, [this, atomId, curMap] {
                bool ok = false;
                int v = QInputDialog::getInt(this, "Map number", "Reaction atom-map number (0 = unmapped):", curMap, 0, 999, 1, &ok);
                if (ok) edit("Map number", [&](Document& d) { edits::setMapNumber(d, atomId, v); });
            });
            if (a->label.isEmpty()) {
                am->addSeparator();
                QAction* addH = am->addAction("Add Explicit H", this, [this, atomId] {
                    edit("Add explicit H", [&](Document& d) { edits::addExplicitH(d, atomId); });
                });
                addH->setEnabled(overlay_.implicitH.value(atomId, 0) > 0);
                bool anyH = false;
                for (const auto& nb : m->neighbors(atomId)) anyH |= m->atom(nb)->el == "H" && m->atom(nb)->label.isEmpty();
                if (anyH)
                    am->addAction("Remove Explicit H", this, [this, atomId] {
                        edit("Remove explicit H", [&](Document& d) { edits::removeExplicitH(d, atomId); });
                    });
            }
            menu.addSeparator();
        }
        if (h.kind == Hit::BondHit) {
            const QString bondId = h.id;
            auto* bm = menu.addMenu("Bond");
            bm->addAction("Single", this, [this, bondId] { edit("Bond order", [&](Document& d) { edits::setBondOrder(d, bondId, 1); }); });
            bm->addAction("Double", this, [this, bondId] { edit("Bond order", [&](Document& d) { edits::setBondOrder(d, bondId, 2); }); });
            bm->addAction("Triple", this, [this, bondId] { edit("Bond order", [&](Document& d) { edits::setBondOrder(d, bondId, 3); }); });
            bm->addSeparator();
            bm->addAction("Wedge", this, [this, bondId] { edit("Bond stereo", [&](Document& d) { edits::setStereo(d, bondId, BondStereo::Wedge); }); });
            bm->addAction("Hash", this, [this, bondId] { edit("Bond stereo", [&](Document& d) { edits::setStereo(d, bondId, BondStereo::Hash); }); });
            bm->addAction("Wavy", this, [this, bondId] { edit("Bond stereo", [&](Document& d) { edits::setStereo(d, bondId, BondStereo::Wavy); }); });
            bm->addSeparator();
            bm->addAction("Flip", this, [this, bondId] { edit("Flip bond", [&](Document& d) { edits::flipBond(d, bondId); }); });
            menu.addSeparator();
        }
        addTransforms(&menu, ids);
        menu.addSeparator();
        menu.addAction("Clean Structure", this, [this, ids] {
            edit("Clean structure", [&](Document& d) {
                for (const auto& id : ids) if (Molecule* mm = d.molecule(id)) chem::cleanup(*mm);
            });
        });
        {
            QString refName;
            bool isReactant = true;
            QString ref = alignReference(molId, &refName, &isReactant);
            if (!ref.isEmpty())
                menu.addAction(QString("Align to %1 (%2)").arg(isReactant ? "Reactant" : "Product", refName), this,
                               [this, molId, ref] { alignToNeighbour(molId, ref); });
        }
        bool anyLabel = false, anyGroup = !m->groups.empty();
        for (const auto& a : m->atoms) anyLabel |= chem::canExpand(a);
        if (anyLabel)
            menu.addAction("Show All Labels as Structures", this, [this, molId] {
                edit("Expand labels", [&](Document& d) {
                    QStringList atoms;
                    for (const auto& a : d.molecule(molId)->atoms) if (chem::canExpand(a)) atoms << a.id;
                    for (const auto& id : atoms) chem::expandLabel(d, id);
                });
            });
        if (anyGroup)
            menu.addAction("Collapse All to Labels", this, [this, molId] {
                edit("Collapse labels", [&](Document& d) {
                    QStringList anchors;
                    for (const auto& g : d.molecule(molId)->groups) anchors << g.anchor;
                    for (const auto& a : anchors) if (d.molecule(molId)) edits::contractGroup(d, molId, a);
                });
            });
        // Group as label: the selected atoms of this structure (all of them if the whole structure is selected).
        {
            QSet<QString> atoms;
            if (selected_.contains(molId)) for (const auto& at : m->atoms) atoms.insert(at.id);
            else for (const auto& at : m->atoms) if (selected_.contains(at.id)) atoms.insert(at.id);
            if (!atoms.isEmpty())
                menu.addAction(QString("Group %1 Atom%2 as Label…").arg(atoms.size()).arg(atoms.size() == 1 ? "" : "s"), this, [this, atoms] {
                    bool ok = false;
                    QString label = QInputDialog::getText(this, "Group as label",
                                                          "Label for the selected atoms (Boc, R1, Ar…):", QLineEdit::Normal, {}, &ok);
                    if (!ok || label.trimmed().isEmpty()) return;
                    QString err;
                    Document probe = doc_;
                    if (edits::groupAsLabel(probe, atoms, label, &err).isEmpty()) { emit status(err); return; }
                    edit("Group as label", [&](Document& d) { edits::groupAsLabel(d, atoms, label); });
                    selected_.clear();
                    update();
                });
        }
        // Per-structure display toggles (apply to every structure in `ids`).
        auto toggle = [&](const QString& name, bool on, bool Molecule::*flag) {
            QAction* act = menu.addAction(name, this, [this, ids, on, flag, name] {
                edit(name, [&](Document& d) {
                    for (const auto& id : ids) if (Molecule* mm = d.molecule(id)) mm->*flag = !on;
                });
            });
            act->setCheckable(true);
            act->setChecked(on);
        };
        menu.addSeparator();
        toggle("Show Stereo Labels (R/S, E/Z)", m->showStereo, &Molecule::showStereo);
        toggle("Show All Carbons", m->showCarbons, &Molecule::showCarbons);
        if (!overlay_.aromaticRings.value(molId).empty()) {
            auto* arom = menu.addMenu("Aromatic Rings");
            const QList<QPair<QString, QString>> modes = {{"Style Default", ""}, {"Alternating Double Bonds", "kekule"}, {"Circles", "circles"}};
            for (const auto& entry : modes) {
                const QString label = entry.first, key = entry.second;
                QAction* act = arom->addAction(label, this, [this, ids, key] {
                    edit("Aromatic rings", [&](Document& d) { for (const auto& id : ids) if (Molecule* mm = d.molecule(id)) mm->aromatic = key; });
                });
                act->setCheckable(true);
                act->setChecked(m->aromatic == key);
            }
        }
        menu.addSeparator();
        menu.addAction("Compound Name / Number…", this, [this, molId] { editCompound(molId); });
        auto* copyAs = menu.addMenu("Copy As");
        const Molecule copy = *m;
        copyAs->addAction("SMILES", this, [copy] { QApplication::clipboard()->setText(chem::smiles(copy)); });
        copyAs->addAction("InChI", this, [copy] { QApplication::clipboard()->setText(chem::info(copy).inchi); });
        copyAs->addAction("InChIKey", this, [copy] { QApplication::clipboard()->setText(chem::info(copy).inchiKey); });
        copyAs->addAction("MOL Block", this, [copy] { QApplication::clipboard()->setText(chem::toMolBlock(copy)); });
        copyAs->addAction("Image (PNG + SVG)", this, [this, ids] {
            QApplication::clipboard()->setMimeData(io::mimeData(io::subset(doc_, ids), renderer_, overlay_));
            emit status("Copied as image.");
        });
        auto* toRx = menu.addMenu("Add to Reaction Builder");
        const QString smi = chem::smiles(*m);
        toRx->addAction("As Reactant", this, [this, smi] { emit addToReaction(smi, 0); });
        toRx->addAction("As Reagent (above the arrow)", this, [this, smi] { emit addToReaction(smi, 1); });
        toRx->addAction("As Product", this, [this, smi] { emit addToReaction(smi, 2); });
        menu.addSeparator();
        addArrange(&menu, ids);
        menu.addAction("Delete Structure", this, [this, ids] {
            edit("Delete", [&](Document& d) { for (const auto& id : ids) edits::remove(d, id); });
        });
    } else if (h.kind == Hit::ArrowHit) {
        const QString arrowId = h.id;
        const Arrow* a = doc_.arrow(arrowId);
        menu.addAction("Edit Conditions…", this, [this, arrowId] { editArrow(arrowId); });
        menu.addAction("Edit in Reaction Builder", this, [this, arrowId] {
            selected_ = {arrowId};
            update();
            emit editReaction(arrowId);
        });
        auto* km = menu.addMenu("Kind");
        const QStringList kinds = {"Forward", "Equilibrium", "Retrosynthesis", "Dashed", "Crossed (no reaction)", "Resonance"};
        for (int k = 0; k < kinds.size(); ++k) {
            QAction* act = km->addAction(kinds[k], this, [this, arrowId, k] {
                edit("Arrow kind", [&](Document& d) { d.arrow(arrowId)->kind = ArrowKind(k); });
            });
            act->setCheckable(true);
            act->setChecked(int(a->kind) == k);
        }
        menu.addAction("Colour…", this, [this, arrowId] {
            const Arrow* x = doc_.arrow(arrowId);
            QColor c = QColorDialog::getColor(x->color.isEmpty() ? style_.arrowColor : QColor::fromString(x->color), this, "Arrow colour");
            if (c.isValid()) edit("Arrow colour", [&](Document& d) { d.arrow(arrowId)->color = c.name(); });
        });
        if (!a->color.isEmpty())
            menu.addAction("Default Colour", this, [this, arrowId] { edit("Arrow colour", [&](Document& d) { d.arrow(arrowId)->color.clear(); }); });
        menu.addAction("Swap Above / Below Text", this, [this, arrowId] {
            edit("Swap arrow text", [&](Document& d) { Arrow* x = d.arrow(arrowId); std::swap(x->above, x->below); });
        });
        QAction* diff = menu.addAction("Show Bond Changes", this, [this, arrowId] { toggleBondChanges({arrowId}); });
        diff->setCheckable(true);
        diff->setChecked(showsBondChanges(arrowId));
        menu.addAction("Reverse Direction", this, [this, arrowId] {
            edit("Reverse arrow", [&](Document& d) {
                Arrow* x = d.arrow(arrowId);
                std::swap(x->x1, x->x2);
                std::swap(x->y1, x->y2);
                std::reverse(x->via.begin(), x->via.end());
            });
        });

        auto* hm = menu.addMenu("Head");
        const QList<QPair<QString, QString>> heads = {{"Style Default", ""}, {"Filled", "filled"}, {"Open", "open"},
                                                      {"Half (Harpoon)", "half"}, {"None", "none"}};
        for (const auto& entry : heads) {
            const QString label = entry.first, key = entry.second;  // plain copies: lambdas capture them (older Apple clang can't capture bindings)
            QAction* act = hm->addAction(label, this, [this, arrowId, key] { edit("Arrow head", [&](Document& d) { d.arrow(arrowId)->headStyle = key; }); });
            act->setCheckable(true);
            act->setChecked(a->headStyle == key);
        }
        hm->addSeparator();
        QAction* both = hm->addAction("Both Ends", this, [this, arrowId] {
            edit("Arrow head", [&](Document& d) { d.arrow(arrowId)->bothHeads = !d.arrow(arrowId)->bothHeads; });
        });
        both->setCheckable(true);
        both->setChecked(a->bothHeads);

        auto* lm = menu.addMenu("Line");
        const QList<QPair<QString, QString>> lines = {{"Solid", ""}, {"Dashed", "dashed"}, {"Dotted", "dotted"}};
        for (const auto& entry : lines) {
            const QString label = entry.first, key = entry.second;
            QAction* act = lm->addAction(label, this, [this, arrowId, key] { edit("Arrow line", [&](Document& d) { d.arrow(arrowId)->line = key; }); });
            act->setCheckable(true);
            act->setChecked(a->line == key);
        }
        lm->addSeparator();
        const QList<QPair<QString, double>> weights = {{"Thin", 0.6}, {"Normal", 1.0}, {"Thick", 1.8}};
        for (const auto& entry : weights) {
            const QString label = entry.first;
            const double w = entry.second;
            QAction* act = lm->addAction(label, this, [this, arrowId, w] { edit("Arrow weight", [&](Document& d) { d.arrow(arrowId)->widthScale = w; }); });
            act->setCheckable(true);
            act->setChecked(std::abs(a->widthScale - w) < 1e-6);
        }

        // Corners: add one where you clicked, or remove the one under the cursor.
        {
            auto path = a->path();
            int nearCorner = -1;
            for (size_t i = 1; i + 1 < path.size(); ++i)
                if (len(path[i] - cursor) < 0.4) nearCorner = int(i) - 1;
            if (nearCorner >= 0) {
                menu.addAction("Remove This Corner", this, [this, arrowId, nearCorner] {
                    edit("Remove corner", [&](Document& d) { auto& v = d.arrow(arrowId)->via; v.erase(v.begin() + nearCorner); });
                });
            } else {
                int seg = 0;
                double best = 1e9;
                for (size_t i = 0; i + 1 < path.size(); ++i) {
                    double dd = distToSegment(cursor, path[i], path[i + 1]);
                    if (dd < best) { best = dd; seg = int(i); }
                }
                // The corner sits on the line, at the point nearest the click; drag it to bend.
                QPointF s0 = path[seg], s1 = path[seg + 1], sv = s1 - s0;
                double t = std::clamp(dot(cursor - s0, sv) / std::max(dot(sv, sv), 1e-9), 0.05, 0.95);
                QPointF corner = s0 + sv * t;
                menu.addAction("Add Corner Here", this, [this, arrowId, seg, corner] {
                    edit("Add corner", [&](Document& d) { auto& v = d.arrow(arrowId)->via; v.insert(v.begin() + seg, corner); });
                });
            }
        }
        if (!a->via.empty())
            menu.addAction("Straighten (Remove Corners)", this, [this, arrowId] { edit("Straighten arrow", [&](Document& d) { d.arrow(arrowId)->via.clear(); }); });
        menu.addSeparator();
        addTransforms(&menu, transformTargets(h));
        menu.addSeparator();
        addArrange(&menu, transformTargets(h));
        menu.addSeparator();
        menu.addAction("Delete Arrow", this, [this, arrowId] { edit("Delete", [&](Document& d) { edits::remove(d, arrowId); }); });
    } else if (h.kind == Hit::TextHit || h.kind == Hit::NumberHit) {
        const QString id = h.id;
        if (h.kind == Hit::TextHit) {
            const TextBox* tb = nullptr;
            for (const auto& t : doc_.texts) if (t.id == id) tb = &t;
            const QString text = tb ? tb->text : QString();
            menu.addAction("Edit Text…", this, [this, id] { editText(id); });
            // Bold and italic wrap each line in markup (**bold**, *italic*), so they mix with styling typed by hand.
            const bool bold = textlines::allBold(text), italic = textlines::allItalic(text);
            QAction* b = menu.addAction("Bold", this, [this, id, bold, italic] {
                edit("Bold", [&](Document& d) { d.text(id)->text = textlines::restyle(d.text(id)->text, !bold, italic); });
            });
            b->setCheckable(true);
            b->setChecked(bold);
            QAction* it = menu.addAction("Italic", this, [this, id, bold, italic] {
                edit("Italic", [&](Document& d) { d.text(id)->text = textlines::restyle(d.text(id)->text, bold, !italic); });
            });
            it->setCheckable(true);
            it->setChecked(italic);
            auto* sm = menu.addMenu("Size");
            const QList<double> sizes = {0, 7, 8, 9, 10, 12, 14, 18, 24, 36};
            for (double sz : sizes) {
                QAction* act = sm->addAction(sz == 0 ? QString("Style Default") : QString("%1 pt").arg(sz), this, [this, id, sz] {
                    edit("Text size", [&](Document& d) { d.text(id)->size = sz; });
                });
                act->setCheckable(true);
                act->setChecked(tb && std::abs(tb->size - sz) < 1e-6);
            }
            auto* al = menu.addMenu("Align");
            const QList<QPair<QString, QString>> aligns = {{"Left", ""}, {"Centre", "center"}, {"Right", "right"}};
            for (const auto& entry : aligns) {
                const QString label = entry.first, key = entry.second;
                QAction* act = al->addAction(label, this, [this, id, key] { edit("Align text", [&](Document& d) { d.text(id)->align = key; }); });
                act->setCheckable(true);
                act->setChecked(tb && tb->align == key);
            }
            const QString curColor = tb ? tb->color : QString();
            menu.addAction("Colour…", this, [this, id, curColor] {
                QColor c = QColorDialog::getColor(curColor.isEmpty() ? style_.textColor : QColor::fromString(curColor), this, "Text colour");
                if (c.isValid()) edit("Text colour", [&](Document& d) { d.text(id)->color = c.name(); });
            });
            if (!curColor.isEmpty())
                menu.addAction("Default Colour", this, [this, id] { edit("Text colour", [&](Document& d) { d.text(id)->color.clear(); }); });
            menu.addSeparator();
            addArrange(&menu, transformTargets(h));
            menu.addSeparator();
            menu.addAction("Delete Text", this, [this, id] { edit("Delete", [&](Document& d) { edits::remove(d, id); }); });
        } else {
            menu.addAction("Compound Name / Number…", this, [this, id] { editCompound(id); });
        }
    } else {
        menu.addAction("Paste", this, &Canvas::paste);
        menu.addAction("Select All", this, &Canvas::selectAll);
        if (!selected_.isEmpty()) {
            menu.addSeparator();
            QSet<QString> ids;
            for (const auto& id : selected_) {
                if (const Molecule* m = doc_.moleculeOfAtom(id)) ids.insert(m->id);
                else ids.insert(id);
            }
            auto* sel = menu.addMenu("Selection");
            addTransforms(sel, ids);
            sel->addSeparator();
            addArrange(sel, ids);
        }
        if (!doc_.empty()) {
            menu.addSeparator();
            menu.addAction("Arrange Scheme", this, &Canvas::arrangeScheme);
            menu.addAction("Number Compounds", this, &Canvas::numberCompounds);
        }
        menu.addSeparator();
        auto* tm = menu.addMenu("Insert Template");
        for (const auto& group : templates::all()) {
            auto* sub = tm->addMenu(group.name);
            for (const auto& t : group.items) {
                QString smi = t.smiles;
                sub->addAction(t.name, this, [this, smi, cursor] { insertSmiles(smi, cursor); });
            }
        }
        menu.addAction("Insert SMILES / InChI Here…", this, [this, cursor] {
            bool ok = false;
            QString text = QInputDialog::getMultiLineText(this, "Insert structure", "SMILES, InChI or MOL block:", {}, &ok);
            if (!ok || text.trimmed().isEmpty()) return;
            QString err;
            auto mols = chem::fromText(text, &err);
            if (mols.empty()) emit status("Could not read that structure. " + err);
            else insertMolecules(mols, cursor);
        });
        menu.addAction("Add Text Here…", this, [this, cursor] { addTextAt(cursor); });
        menu.addSeparator();
        auto* styleMenu = menu.addMenu("Style");
        QStringList names = styles::availableNames();
        if (!names.contains(doc_.styleName)) names << doc_.styleName;
        for (const auto& n : names) {
            QAction* act = styleMenu->addAction(n, this, [this, n] { setStyleName(n); });
            act->setCheckable(true);
            act->setChecked(n == doc_.styleName);
        }
        QAction* grid = menu.addAction("Show Grid", this, [this] { setGrid(!showGrid_, snapGrid_, gridSize_); emit gridChanged(); });
        grid->setCheckable(true);
        grid->setChecked(showGrid_);
        QAction* snap = menu.addAction("Snap to Grid", this, [this] { setGrid(showGrid_, !snapGrid_, gridSize_); emit gridChanged(); });
        snap->setCheckable(true);
        snap->setChecked(snapGrid_);
        menu.addSeparator();
        menu.addAction("Zoom to Fit", this, &Canvas::zoomToFit);
        menu.addAction("Actual Size", this, &Canvas::resetZoom);
    }
    menu.exec(mapToGlobal(screen.toPoint()));
}

bool Canvas::arrowEndAt(QPointF screen, QString* arrowId, bool* head) const {
    const double tol = 9.0;  // screen pixels
    double best = tol;
    for (const auto& a : doc_.arrows) {
        ArrowGeom g = renderer_.arrowGeom(a, &doc_);
        double dh = len(toScreen(g.head) - screen), dt = len(toScreen(g.tail) - screen);
        if (dh < best) { best = dh; *arrowId = a.id; *head = true; }
        if (dt < best) { best = dt; *arrowId = a.id; *head = false; }
    }
    return best < tol;
}

QStringList Canvas::commandIds() {
    return {"structure.flipH", "structure.flipV", "structure.rotateCW", "structure.rotateCCW", "structure.rotate180",
            "structure.align", "structure.toggleStereo", "structure.toggleCarbons", "structure.expandLabels",
            "structure.collapseLabels", "copy.smiles", "copy.inchi", "copy.inchikey", "copy.mol", "copy.image",
            "arrow.swapText", "arrow.reverse", "arrow.straighten", "arrow.nextHead", "arrow.nextLine", "arrow.bothHeads",
            "arrow.toggleDiff", "reaction.continueRight", "reaction.continueLeft", "reaction.continueUp", "reaction.continueDown"};
}

void Canvas::setGrid(bool show, bool snap, double size) {
    showGrid_ = show;
    snapGrid_ = snap;
    gridSize_ = std::clamp(size, 0.05, 5.0);
    update();
}

QPointF Canvas::snapToGrid(QPointF model) const {
    return {std::round(model.x() / gridSize_) * gridSize_, std::round(model.y() / gridSize_) * gridSize_};
}

QSet<QString> Canvas::actionTargets() const {
    Hit h = mouseInside_ ? hover() : Hit{};
    if (h.kind != Hit::None && h.kind != Hit::NumberHit) return transformTargets(h);
    QSet<QString> ids;
    for (const auto& s : selected_) {
        if (const Molecule* m = doc_.moleculeOfAtom(s)) ids.insert(m->id);
        else ids.insert(s);
    }
    return ids;
}

void Canvas::duplicateSelection() {
    const QSet<QString> ids = actionTargets();
    if (ids.isEmpty()) { emit status("Hover over or select something to duplicate."); return; }
    QSet<QString> copies;
    edit("Duplicate", [&](Document& d) { copies = edits::duplicate(d, ids, QPointF(1.0, 1.0)); });
    selected_ = copies;
    update();
    emit infoTargetChanged();
}

void Canvas::restackSelection(bool toFront) {
    const QSet<QString> ids = actionTargets();
    if (ids.isEmpty()) { emit status("Hover over or select something first."); return; }
    edit(toFront ? "Bring to front" : "Send to back", [&](Document& d) { edits::restack(d, ids, toFront); });
}

void Canvas::continueReaction(QPointF dir) {
    // The selected structures (or the hovered one): a whole molecule counts if any part is selected.
    QStringList mols;
    auto addMol = [&](const Molecule* m) { if (m && !mols.contains(m->id)) mols << m->id; };
    for (const auto& s : selected_) {
        if (const Molecule* m = doc_.molecule(s)) addMol(m);
        else if (const Molecule* m = doc_.moleculeOfAtom(s)) addMol(m);
        else addMol(doc_.moleculeOfBond(s));
    }
    if (mols.isEmpty() && mouseInside_ && !hover().molId.isEmpty()) addMol(doc_.molecule(hover().molId));
    if (mols.isEmpty()) { emit status("Select a structure first, then Ctrl+arrow key continues the reaction from it."); return; }

    // Lay out in points, like Arrange Scheme: structure, gap, arrow, gap, copy.
    QRectF body, withNames;
    for (const auto& id : mols) {
        const Molecule* m = doc_.molecule(id);
        QRectF r = renderer_.moleculeRect(*m, overlay_);
        body = body.isNull() ? r : body.united(r);
        QRectF n = renderer_.numberRect(*m, overlay_);
        r = n.isNull() ? r : r.united(n);
        withNames = withNames.isNull() ? r : withNames.united(r);
    }
    const double gap = style_.schemeSpacing, L = style_.arrowMinLength;
    const bool horizontal = std::abs(dir.x()) > 0.5;
    // The arrow leaves from the edge it points out of (below the names when going down).
    const QRectF& from = dir.y() > 0.5 ? withNames : body;
    QPointF edge = horizontal ? QPointF(dir.x() > 0 ? from.right() : from.left(), body.center().y())
                              : QPointF(body.center().x(), dir.y() > 0 ? from.bottom() : from.top());
    QPointF tail = edge + dir * gap, head = tail + dir * L;
    // The copy's near side sits one gap past the head.
    double nearSide = horizontal ? (dir.x() > 0 ? body.left() : body.right()) : (dir.y() > 0 ? body.top() : body.bottom());
    double target = horizontal ? head.x() + dir.x() * gap : head.y() + dir.y() * gap;
    QPointF delta = horizontal ? QPointF(target - nearSide, 0) : QPointF(0, target - nearSide);

    QStringList copies;
    edit("Continue reaction", [&](Document& d) {
        copies = edits::continueReaction(d, mols, renderer_.toModel(tail), renderer_.toModel(head), renderer_.toModel(delta));
    });
    // Select the copy so the next Ctrl+arrow carries on from it.
    selected_.clear();
    for (const auto& c : copies) selected_.insert(c);
    update();
    emit infoTargetChanged();
}

void Canvas::moveFocus(QPointF dir) {
    // To the bonded neighbour that lies most nearly in that direction (within about 70°).
    const Molecule* m = doc_.moleculeOfAtom(stickyAtom_);
    if (!m) return;
    QPointF here = m->atom(stickyAtom_)->pos();
    QString best;
    double bestDot = 0.35;
    for (const auto& nb : m->neighbors(stickyAtom_)) {
        double dd = dot(unit(m->atom(nb)->pos() - here), dir);
        if (dd > bestDot) { bestDot = dd; best = nb; }
    }
    if (best.isEmpty()) return;
    stickyAtom_ = best;
    updateHover(lastMouse_);
    update();
}

bool Canvas::showsBondChanges(const QString& arrowId) const {
    for (const auto& s : doc_.schemes)
        for (const auto& st : s.steps)
            if (st.arrow == arrowId) return st.showDiff;
    return false;
}

void Canvas::toggleBondChanges(QStringList arrowIds) {
    // No arrows given: every step.
    Document probe = doc_;
    edits::buildScheme(probe);
    if (arrowIds.isEmpty())
        for (const auto& s : probe.schemes) for (const auto& st : s.steps) arrowIds << st.arrow;
    bool anyStep = false, turnOn = false;
    for (const auto& s : probe.schemes)
        for (const auto& st : s.steps)
            if (arrowIds.contains(st.arrow)) { anyStep = true; turnOn |= !st.showDiff; }
    if (!anyStep) {
        emit status("That arrow has no reactant and product to compare: draw them behind its tail and past its head.");
        return;
    }
    edit(turnOn ? "Show bond changes" : "Hide bond changes", [&](Document& d) {
        edits::buildScheme(d);
        for (auto& s : d.schemes)
            for (auto& st : s.steps) {
                if (!arrowIds.contains(st.arrow)) continue;
                st.showDiff = turnOn;
                if (!turnOn || st.manualMap) continue;
                // Colouring needs a mapping: map the step automatically if none of its atoms are.
                bool mapped = false;
                for (const auto& id : st.reactants + st.products)
                    if (const Molecule* m = d.molecule(id))
                        for (const auto& a : m->atoms) mapped |= a.map != 0;
                if (mapped) continue;
                std::vector<Molecule*> rs, ps;
                for (const auto& id : st.reactants) if (Molecule* m = d.molecule(id)) rs.push_back(m);
                for (const auto& id : st.products) if (Molecule* m = d.molecule(id)) ps.push_back(m);
                if (!rs.empty() && !ps.empty()) chem::autoMap(rs, ps);
            }
    });
    emit status(turnOn ? "Bonds broken, formed and changed are coloured (Reaction → Arrow → Show Bond Changes to hide)."
                       : "Bond changes hidden.");
}

void Canvas::command(const QString& id) {
    static const QHash<QString, QPointF> continueDirs = {{"reaction.continueRight", {1, 0}}, {"reaction.continueLeft", {-1, 0}},
                                                         {"reaction.continueUp", {0, -1}}, {"reaction.continueDown", {0, 1}}};
    if (continueDirs.contains(id)) { continueReaction(continueDirs[id]); return; }
    // Acts on what's under the cursor (with the selection, if that's part of it), else on the selection.
    Hit h = mouseInside_ ? hover() : Hit{};
    QSet<QString> ids;
    if (h.kind != Hit::None && h.kind != Hit::NumberHit) ids = transformTargets(h);
    else if (h.kind == Hit::NumberHit) ids = {h.id};
    else
        for (const auto& s : selected_) {
            if (const Molecule* m = doc_.moleculeOfAtom(s)) ids.insert(m->id);
            else ids.insert(s);
        }
    QStringList mols, arrows;
    for (const auto& x : ids) {
        if (doc_.molecule(x)) mols << x;
        else if (doc_.arrow(x)) arrows << x;
    }
    auto need = [&](bool ok, const char* what) {
        if (!ok) emit status(QString("Hover over or select %1 first.").arg(what));
        return ok;
    };

    if (id == "arrow.toggleDiff") { toggleBondChanges(arrows); return; }

    struct T { const char* id; double a, b, c, d; };
    static const T transforms[] = {{"structure.flipH", -1, 0, 0, 1}, {"structure.flipV", 1, 0, 0, -1},
                                   {"structure.rotateCW", 0, -1, 1, 0}, {"structure.rotateCCW", 0, 1, -1, 0},
                                   {"structure.rotate180", -1, 0, 0, -1}};
    for (const auto& t : transforms)
        if (id == t.id) {
            if (!need(!ids.isEmpty(), "something")) return;
            edit("Transform", [&](Document& d) { edits::transformObjects(d, ids, t.a, t.b, t.c, t.d); });
            return;
        }

    if (id == "structure.align") {
        if (!need(mols.size() == 1, "one structure")) return;
        QString ref = alignReference(mols.front());
        if (ref.isEmpty()) emit status("No reactant or product to line up with: draw an arrow to it first.");
        else alignToNeighbour(mols.front(), ref);
        return;
    }
    if (id == "structure.toggleStereo" || id == "structure.toggleCarbons") {
        if (!need(!mols.isEmpty(), "a structure")) return;
        bool Molecule::*flag = id == "structure.toggleStereo" ? &Molecule::showStereo : &Molecule::showCarbons;
        bool on = !(doc_.molecule(mols.front())->*flag);
        edit(id == "structure.toggleStereo" ? "Stereo labels" : "All carbons", [&](Document& d) {
            for (const auto& m : mols) d.molecule(m)->*flag = on;
        });
        return;
    }
    if (id == "structure.expandLabels" || id == "structure.collapseLabels") {
        if (!need(!mols.isEmpty(), "a structure")) return;
        const bool expand = id == "structure.expandLabels";
        // A hovered label atom (or a hovered expanded group) does just that one; otherwise all of them.
        QString only = h.kind == Hit::AtomHit ? h.id : QString();
        edit(expand ? "Expand labels" : "Collapse labels", [&](Document& d) {
            for (const auto& mid : mols) {
                Molecule* m = d.molecule(mid);
                if (!m) continue;
                if (expand) {
                    QStringList atoms;
                    for (const auto& a : m->atoms)
                        if (chem::canExpand(a) && (only.isEmpty() || a.id == only)) atoms << a.id;
                    for (const auto& a : atoms) chem::expandLabel(d, a);
                } else {
                    QStringList anchors;
                    for (const auto& g : m->groups)
                        if (only.isEmpty() || g.atoms.contains(only)) anchors << g.anchor;
                    for (const auto& a : anchors) if (d.molecule(mid)) edits::contractGroup(d, mid, a);
                }
            }
        });
        return;
    }
    if (id.startsWith("copy.")) {
        if (!need(!mols.isEmpty(), "a structure")) return;
        if (id == "copy.image") {
            QApplication::clipboard()->setMimeData(io::mimeData(io::subset(doc_, ids), renderer_, overlay_));
        } else {
            QStringList out;
            for (const auto& mid : mols) {
                const Molecule& m = *doc_.molecule(mid);
                if (id == "copy.smiles") out << chem::smiles(m);
                else if (id == "copy.inchi") out << chem::info(m).inchi;
                else if (id == "copy.inchikey") out << chem::info(m).inchiKey;
                else out << chem::toMolBlock(m);
            }
            QApplication::clipboard()->setText(out.join(id == "copy.mol" ? "$$$$\n" : "\n"));
        }
        emit status(QString("Copied %1 structure%2.").arg(mols.size()).arg(mols.size() == 1 ? "" : "s"));
        return;
    }
    if (id.startsWith("arrow.")) {
        if (!need(!arrows.isEmpty(), "an arrow")) return;
        static const QStringList heads = {"", "filled", "open", "half", "none"};
        static const QStringList lines = {"", "dashed", "dotted"};
        edit("Arrow", [&](Document& d) {
            for (const auto& aid : arrows) {
                Arrow* a = d.arrow(aid);
                if (id == "arrow.swapText") std::swap(a->above, a->below);
                else if (id == "arrow.reverse") {
                    std::swap(a->x1, a->x2);
                    std::swap(a->y1, a->y2);
                    std::reverse(a->via.begin(), a->via.end());
                } else if (id == "arrow.straighten") a->via.clear();
                else if (id == "arrow.nextHead") a->headStyle = heads[(heads.indexOf(a->headStyle) + 1) % heads.size()];
                else if (id == "arrow.nextLine") a->line = lines[(lines.indexOf(a->line) + 1) % lines.size()];
                else if (id == "arrow.bothHeads") a->bothHeads = !a->bothHeads;
            }
        });
        return;
    }
}

void Canvas::setTool(Tool t) {
    if (tool_ == t) return;
    tool_ = t;
    bentPts_.clear();
    preview_.clear();
    drag_ = Drag::None;
    if (t == Tool::Select) unsetCursor();
    else setCursor(t == Tool::Text ? Qt::IBeamCursor : Qt::CrossCursor);
    updateStatus();
    update();
    emit toolChanged(int(t));
}

QPointF Canvas::bentSnap(QPointF model, bool snap) const {
    if (!snap || bentPts_.empty()) return model;
    // Each new corner continues horizontally or vertically from the last one.
    QPointF prev = bentPts_.back(), v = model - prev;
    return std::abs(v.x()) >= std::abs(v.y()) ? QPointF(model.x(), prev.y()) : QPointF(prev.x(), model.y());
}

void Canvas::finishBentArrow() {
    std::vector<QPointF> pts;
    for (const auto& p : bentPts_)
        if (pts.empty() || len(p - pts.back()) > 0.2) pts.push_back(p);  // a double-click repeats its point
    bentPts_.clear();
    preview_.clear();
    update();
    if (pts.size() < 2) return;
    edit("Add bent arrow", [&](Document& d) {
        QString id = edits::addArrow(d, pts.front(), 3.0);
        Arrow* a = d.arrow(id);
        a->x2 = pts.back().x();
        a->y2 = pts.back().y();
        a->via.assign(pts.begin() + 1, pts.end() - 1);
    });
}

void Canvas::addTextAt(QPointF model) {
    bool ok = false;
    QString t = QInputDialog::getMultiLineText(this, "Add text", "Text (**bold**, *italic*):", {}, &ok);
    if (ok && !t.trimmed().isEmpty()) edit("Add text", [&](Document& d) { edits::addText(d, model, t); });
}

void Canvas::setMapMode(bool on) {
    if (mapMode_ == on) return;
    mapMode_ = on;
    drag_ = Drag::None;
    preview_.clear();
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
    if (!on) unsetCursor();
    updateStatus();
    update();
    emit mapModeChanged(on);
}

void Canvas::finishMapLink(QPointF screen) {
    const QString from = pressHit_.id;
    auto hits = hitTest(screen);
    auto target = std::find_if(hits.begin(), hits.end(),
                               [&](const Hit& x) { return x.kind == Hit::AtomHit && x.id != from; });
    if (target == hits.end()) {
        // Dropped on empty canvas: unmap.
        if (!mapping::stepOfAtom(doc_, from)) {
            emit status("That atom isn't part of a reaction step.");
            return;
        }
        edit("Unmap atom", [&](Document& d) {
            if (Step* st = mapping::stepOfAtom(d, from)) mapping::unlink(d, *st, from);
        });
        return;
    }
    const QString to = target->id;
    QString r, p;
    if (!mapping::stepLinking(doc_, from, to, &r, &p)) {
        emit status("Map a reactant atom to a product atom of the same reaction step.");
        return;
    }
    const Atom* ra = doc_.moleculeOfAtom(r)->atom(r);
    const Atom* pa = doc_.moleculeOfAtom(p)->atom(p);
    if (ra->el != pa->el) {
        emit status(QString("Can't map %1 to %2: atoms keep their element across a reaction.").arg(ra->el, pa->el));
        return;
    }
    edit("Map atoms", [&](Document& d) {
        QString rr, pp;
        if (Step* st = mapping::stepLinking(d, r, p, &rr, &pp)) mapping::link(d, *st, rr, pp);
    });
}

QString Canvas::selectedArrow() const {
    for (const auto& a : doc_.arrows) if (selected_.contains(a.id)) return a.id;
    for (const auto& s : doc_.schemes)
        for (const auto& st : s.steps)
            for (const auto& list : {st.reactants, st.agents, st.products})
                for (const auto& id : list)
                    if (selected_.contains(id)) return st.arrow;
    return {};
}

std::optional<ReactionSpec> Canvas::reactionFor(const QString& arrowId, bool withMaps) const {
    const Arrow* a = doc_.arrow(arrowId);
    if (!a) return std::nullopt;
    ReactionSpec spec;
    spec.above = a->above;
    spec.below = a->below;
    spec.kind = a->kind;
    spec.autoMap = !withMaps;
    for (const auto& s : doc_.schemes)
        for (const auto& st : s.steps) {
            if (st.arrow != arrowId) continue;
            auto text = [&](const QStringList& ids) {
                QStringList out;
                for (const auto& id : ids)
                    if (const Molecule* m = doc_.molecule(id)) out << (withMaps ? chem::smilesWithMaps(*m) : chem::smiles(*m));
                return out;
            };
            spec.reactants = text(st.reactants);
            spec.agents = text(st.agents);
            spec.products = text(st.products);
            spec.showDiff = st.showDiff;
        }
    return spec;
}

ReactionResult Canvas::applyReaction(const ReactionSpec& spec, const QString& arrowId, int mergeKey) {
    ReactionResult res;
    // Parse every row first; rows that fail are reported and left out.
    auto parse = [&](const QStringList& rows) {
        std::vector<Molecule> out;
        for (const auto& row : rows) {
            QString err;
            auto m = row.trimmed().isEmpty() ? std::nullopt : chem::fromSmilesOrSmarts(row, &err);
            res.errors << (row.trimmed().isEmpty() ? QString() : (m ? QString() : (err.isEmpty() ? "Invalid" : err)));
            if (m) out.push_back(*m);
        }
        return out;
    };
    auto rMols = parse(spec.reactants), aMols = parse(spec.agents), pMols = parse(spec.products);

    edit(arrowId.isEmpty() ? "New reaction" : "Edit reaction", [&](Document& d) {
        Arrow* arrow = arrowId.isEmpty() ? nullptr : d.arrow(arrowId);
        QPointF anchor;
        if (arrow) {
            anchor = arrow->tail();
            // Drop the old structures of this step unless another step still uses them.
            QStringList old;
            for (const auto& s : d.schemes)
                for (const auto& st : s.steps)
                    if (st.arrow == arrowId) old = st.reactants + st.agents + st.products;
            for (const auto& id : old) {
                bool shared = false;
                for (const auto& s : d.schemes)
                    for (const auto& st : s.steps)
                        if (st.arrow != arrowId &&
                            (st.reactants.contains(id) || st.agents.contains(id) || st.products.contains(id)))
                            shared = true;
                if (!shared) edits::remove(d, id);
            }
        } else {
            // New reactions go below everything else.
            QRectF b = renderer_.documentBounds(d, overlay_);
            anchor = b.isNull() ? QPointF(0, 0) : renderer_.toModel(QPointF(b.left(), b.bottom())) + QPointF(0, 4);
            res.arrowId = edits::addArrow(d, anchor, 3.0);
            arrow = d.arrow(res.arrowId);
        }
        if (res.arrowId.isEmpty()) res.arrowId = arrowId;
        arrow = d.arrow(res.arrowId);
        arrow->above = spec.above;
        arrow->below = spec.below;
        arrow->kind = spec.kind;

        Step* step = nullptr;
        for (auto& s : d.schemes)
            for (auto& st : s.steps)
                if (st.arrow == res.arrowId) step = &st;
        if (!step) {
            Scheme s;
            s.id = d.newId("s");
            s.steps.push_back(Step{});
            s.steps.back().arrow = res.arrowId;
            d.schemes.push_back(s);
            step = &d.schemes.back().steps.back();
        }
        step->reactants = edits::insertMolecules(d, rMols, anchor - QPointF(3, 0));
        step->agents = edits::insertMolecules(d, aMols, anchor + QPointF(1.5, -3));
        step->products = edits::insertMolecules(d, pMols, anchor + QPointF(6, 0));
        step->showDiff = spec.showDiff;
        step->manualMap = !spec.autoMap;

        std::vector<Molecule*> rs, ps;
        for (const auto& id : step->reactants) rs.push_back(d.molecule(id));
        for (const auto& id : step->products) ps.push_back(d.molecule(id));
        if (spec.autoMap && !rs.empty() && !ps.empty()) chem::autoMap(rs, ps);
        for (auto* m : ps)
            for (const auto& a : m->atoms) res.mapped += a.map > 0;

        // Lay out with chemistry for the new structures so label widths are right.
        ChemOverlay ov = overlay_;
        for (const auto& list : {step->reactants, step->agents, step->products})
            for (const auto& id : list) {
                auto an = chem::analyze(*d.molecule(id));
                for (auto it = an.implicitH.begin(); it != an.implicitH.end(); ++it) ov.implicitH[it.key()] = it.value();
                ov.aromaticRings[id] = an.aromaticRings;
            }
        edits::renumber(d);
        layout::layoutSchemes(d, renderer_, ov);
    }, mergeKey);
    return res;
}

void Canvas::silence(const Warning& w, const QString& scope) {
    if (scope == "instance") {
        edit("Silence warning", [&](Document& d) { d.suppressedInstances.insert(w.key()); });
    } else if (scope == "document") {
        edit("Silence " + w.code, [&](Document& d) { d.suppressedCodes.insert(w.code); });
    } else if (scope == "global") {
        globalSilenced_.insert(w.code);
        warnings::setGlobalSilenced(globalSilenced_);
        refreshWarnings();
        update();
    } else if (scope == "reload") {
        globalSilenced_ = warnings::globalSilenced();
        refreshWarnings();
        update();
    }
}

}  // namespace cd
