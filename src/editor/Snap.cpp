#include "editor/Snap.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace cd::snap {

namespace {

// One way the dragged box could line up along an axis: move by `d`, and show `guide` if chosen.
struct Candidate {
    double d;
    double rank;  // |d|, plus a hair for the less obvious kinds so ties go to the plain alignment
    QLineF guide;
};

// Everything is written for the x axis; the y axis runs the same code on transposed boxes.
QRectF flip(const QRectF& r) { return QRectF(r.y(), r.x(), r.height(), r.width()); }
QLineF flip(const QLineF& l) { return QLineF(l.y1(), l.x1(), l.y2(), l.x2()); }

void axisCandidates(const QRectF& m, const std::vector<QRectF>& others, double tol, std::vector<Candidate>& out) {
    auto add = [&](double d, double penalty, QLineF guide) {
        if (std::abs(d) <= tol) out.push_back({d, std::abs(d) + penalty, guide});
    };
    // Edges and centres.
    const double mv[3] = {m.left(), m.center().x(), m.right()};
    for (const auto& o : others) {
        const double ov[3] = {o.left(), o.center().x(), o.right()};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const double top = std::min(m.top(), o.top()), bottom = std::max(m.bottom(), o.bottom());
                add(ov[j] - mv[i], i == j ? 0 : tol * 0.02, QLineF(ov[j], top, ov[j], bottom));
            }
    }
    // Equal spacing, among the objects in the same row as the dragged box.
    std::vector<QRectF> row;
    for (const auto& o : others)
        if (o.bottom() >= m.top() && o.top() <= m.bottom()) row.push_back(o);
    std::sort(row.begin(), row.end(), [](const QRectF& a, const QRectF& b) { return a.left() < b.left(); });
    const double y = m.center().y();
    for (size_t i = 0; i + 1 < row.size(); ++i) {
        const QRectF &a = row[i], &b = row[i + 1];
        const double gap = b.left() - a.right();
        if (gap <= 0) continue;
        // One more gap past the pair, on either side.
        add(b.right() + gap - m.left(), tol * 0.01, QLineF(a.right(), y, b.left(), y));
        add(a.left() - gap - m.right(), tol * 0.01, QLineF(a.right(), y, b.left(), y));
        // Centred between the two.
        if (gap >= m.width()) add((a.right() + b.left()) / 2 - m.center().x(), tol * 0.01, QLineF(a.right(), y, b.left(), y));
    }
}

// The nearest candidate, and the guides of every candidate that agrees with it.
double choose(const std::vector<Candidate>& cands, std::vector<QLineF>& guides) {
    if (cands.empty()) return 0;
    const auto best = std::min_element(cands.begin(), cands.end(),
                                       [](const Candidate& a, const Candidate& b) { return a.rank < b.rank; });
    for (const auto& c : cands)
        if (std::abs(c.d - best->d) < 1e-6) guides.push_back(c.guide);
    return best->d;
}

}  // namespace

Result snapMove(const Box& moving, const std::vector<Box>& others, double tol) {
    Result res;
    std::vector<QRectF> rects, flipped;
    for (const auto& o : others) { rects.push_back(o.rect); flipped.push_back(flip(o.rect)); }

    std::vector<Candidate> xs, ys;
    axisCandidates(moving.rect, rects, tol, xs);
    axisCandidates(flip(moving.rect), flipped, tol, ys);
    for (auto& c : ys) c.guide = flip(c.guide);
    // Baselines: text sits level with other text.
    for (double mb : moving.baselines)
        for (const auto& o : others)
            for (double ob : o.baselines)
                if (std::abs(ob - mb) <= tol)
                    ys.push_back({ob - mb, std::abs(ob - mb),
                                  QLineF(std::min(moving.rect.left(), o.rect.left()), ob,
                                         std::max(moving.rect.right(), o.rect.right()), ob)});

    std::vector<QLineF> gx, gy;
    res.delta = QPointF(choose(xs, gx), choose(ys, gy));
    // Guides were measured before the box moved along the other axis: stretch them to reach it.
    const QRectF moved = moving.rect.translated(res.delta);
    for (auto g : gx) {
        if (g.x1() == g.x2())  // an alignment line (vertical); spacing marks are horizontal
            g = QLineF(g.x1(), std::min({g.y1(), g.y2(), moved.top()}), g.x1(), std::max({g.y1(), g.y2(), moved.bottom()}));
        else g.translate(0, res.delta.y());
        res.guides.push_back(g);
    }
    for (auto g : gy) {
        if (g.y1() == g.y2())
            g = QLineF(std::min({g.x1(), g.x2(), moved.left()}), g.y1(), std::max({g.x1(), g.x2(), moved.right()}), g.y1());
        else g.translate(res.delta.x(), 0);
        res.guides.push_back(g);
    }
    return res;
}

std::vector<QPointF> alignDeltas(const std::vector<QRectF>& rects, Align mode) {
    std::vector<QPointF> out(rects.size(), QPointF(0, 0));
    if (rects.size() < 2) return out;
    QRectF all = rects[0];
    for (const auto& r : rects) all = all.united(r);

    if (mode == Align::DistributeH || mode == Align::DistributeV) {
        if (rects.size() < 3) return out;
        const bool horizontal = mode == Align::DistributeH;
        auto lo = [&](const QRectF& r) { return horizontal ? r.left() : r.top(); };
        auto size = [&](const QRectF& r) { return horizontal ? r.width() : r.height(); };
        std::vector<int> order(rects.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return lo(rects[a]) + size(rects[a]) / 2 < lo(rects[b]) + size(rects[b]) / 2;
        });
        double total = 0;
        for (const auto& r : rects) total += size(r);
        const double span = (horizontal ? all.width() : all.height());
        const double gap = (span - total) / double(rects.size() - 1);
        double at = horizontal ? all.left() : all.top();
        for (int i : order) {
            const double d = at - lo(rects[i]);
            out[i] = horizontal ? QPointF(d, 0) : QPointF(0, d);
            at += size(rects[i]) + gap;
        }
        return out;
    }
    for (size_t i = 0; i < rects.size(); ++i) {
        const QRectF& r = rects[i];
        switch (mode) {
        case Align::Left: out[i] = {all.left() - r.left(), 0}; break;
        case Align::HCenter: out[i] = {all.center().x() - r.center().x(), 0}; break;
        case Align::Right: out[i] = {all.right() - r.right(), 0}; break;
        case Align::Top: out[i] = {0, all.top() - r.top()}; break;
        case Align::VCenter: out[i] = {0, all.center().y() - r.center().y()}; break;
        case Align::Bottom: out[i] = {0, all.bottom() - r.bottom()}; break;
        default: break;
        }
    }
    return out;
}

}  // namespace cd::snap
