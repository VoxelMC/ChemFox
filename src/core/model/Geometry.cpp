#include "core/model/Geometry.h"

#include <QHash>
#include <QQueue>
#include <algorithm>

namespace cd::geom {

double distToSegment(QPointF p, QPointF a, QPointF b) {
    QPointF d = b - a;
    double l2 = dot(d, d);
    double t = l2 > 0 ? std::clamp(dot(p - a, d) / l2, 0.0, 1.0) : 0.0;
    return len(p - (a + d * t));
}

QStringList smallestRing(const Molecule& m, const Bond& b, int maxSize) {
    // BFS from b.a to b.b without using the bond itself.
    QHash<QString, QString> prev;
    QHash<QString, int> depth;
    QQueue<QString> q;
    q.enqueue(b.a);
    depth[b.a] = 1;
    while (!q.isEmpty()) {
        QString cur = q.dequeue();
        if (depth[cur] >= maxSize) continue;
        for (const auto& bd : m.bonds) {
            if (!bd.has(cur) || &bd == &b || bd.id == b.id) continue;
            QString nx = bd.other(cur);
            if (depth.contains(nx)) continue;
            depth[nx] = depth[cur] + 1;
            prev[nx] = cur;
            if (nx == b.b) {
                QStringList ring;
                for (QString c = nx; ; c = prev[c]) {
                    ring.prepend(c);
                    if (c == b.a) break;
                }
                return ring;
            }
            q.enqueue(nx);
        }
    }
    return {};
}

int doubleBondSide(const Molecule& m, const Bond& b) {
    if (b.dbpos == DbPos::Left) return 1;
    if (b.dbpos == DbPos::Right) return -1;
    if (b.dbpos == DbPos::Center) return 0;
    const Atom* A = m.atom(b.a);
    const Atom* B = m.atom(b.b);
    if (!A || !B) return 0;
    QPointF d = B->pos() - A->pos();
    QPointF n = perp(d);
    QPointF mid = (A->pos() + B->pos()) / 2;

    QStringList ring = smallestRing(m, b);
    if (!ring.isEmpty()) {
        QPointF c(0, 0);
        for (const auto& id : ring) c += m.atom(id)->pos();
        c /= ring.size();
        return dot(c - mid, n) >= 0 ? 1 : -1;
    }
    QStringList na = m.neighbors(b.a), nb = m.neighbors(b.b);
    na.removeAll(b.b);
    nb.removeAll(b.a);
    if (na.isEmpty() || nb.isEmpty()) return 0;  // terminal double bonds are centred
    // cross(d, v) > 0 means v lies on the perp(d) side. Count substituents per side...
    double score = 0;
    for (const auto& id : na) score += (cross(d, m.atom(id)->pos() - A->pos()) > 0 ? 1 : -1);
    for (const auto& id : nb) score += (cross(d, m.atom(id)->pos() - B->pos()) > 0 ? 1 : -1);
    if (score > 0) return 1;
    if (score < 0) return -1;
    // ...a tie (typical for trans alkenes) goes to the side of the bigger branch...
    auto branchSize = [&](const QString& start) {
        QSet<QString> seen{b.a, b.b};
        QQueue<QString> q;
        q.enqueue(start);
        seen.insert(start);
        int n = 0;
        while (!q.isEmpty()) {
            QString c = q.dequeue();
            ++n;
            for (const auto& nx : m.neighbors(c))
                if (!seen.contains(nx)) { seen.insert(nx); q.enqueue(nx); }
        }
        return n;
    };
    double weighted = 0;
    for (const auto& id : na) weighted += (cross(d, m.atom(id)->pos() - A->pos()) > 0 ? 1 : -1) * branchSize(id);
    for (const auto& id : nb) weighted += (cross(d, m.atom(id)->pos() - B->pos()) > 0 ? 1 : -1) * branchSize(id);
    if (weighted > 0) return 1;
    if (weighted < 0) return -1;
    // ...and an exact tie goes below the bond (right of a vertical one), never centred.
    // Only terminal double bonds (handled above) are centred. 'f' flips it.
    QPointF below = std::abs(n.y()) > 1e-6 ? QPointF(0, 1) : QPointF(1, 0);
    return dot(n, below) > 0 ? 1 : -1;
}

QPointF freeDirection(const Molecule& m, const QString& atomId) {
    const Atom* a = m.atom(atomId);
    QStringList nbs = m.neighbors(atomId);
    if (!a || nbs.isEmpty()) return unit(QPointF(std::cos(-M_PI / 6), std::sin(-M_PI / 6)));
    std::vector<double> angs;
    for (const auto& id : nbs) {
        QPointF v = m.atom(id)->pos() - a->pos();
        angs.push_back(std::atan2(v.y(), v.x()));
    }
    if (angs.size() == 1) return -unit(m.atom(nbs[0])->pos() - a->pos());
    std::sort(angs.begin(), angs.end());
    double best = -1, bestAng = 0;
    for (size_t i = 0; i < angs.size(); ++i) {
        double a0 = angs[i], a1 = (i + 1 < angs.size()) ? angs[i + 1] : angs[0] + 2 * M_PI;
        if (a1 - a0 > best) { best = a1 - a0; bestAng = (a0 + a1) / 2; }
    }
    return {std::cos(bestAng), std::sin(bestAng)};
}

namespace {
QPointF defaultBondDirection(const Molecule& m, const QString& atomId, int order);
}

QPointF newBondDirection(const Molecule& m, const QString& atomId, int order, std::optional<QPointF> toward) {
    QPointF def = defaultBondDirection(m, atomId, order);
    const Atom* a = m.atom(atomId);
    if (!a || !toward) return def;
    QPointF want = *toward - a->pos();
    if (len(want) < 1e-6) return def;
    want = unit(want);
    QStringList nbs = m.neighbors(atomId);
    if (nbs.isEmpty()) {
        double ang = snapAngle(std::atan2(want.y(), want.x()), M_PI / 6);
        return {std::cos(ang), std::sin(ang)};
    }
    // Linear centres (triple bonds, cumulenes) have only one sensible direction.
    if (nbs.size() == 1 && len(def + unit(m.atom(nbs[0])->pos() - a->pos())) < 1e-6) return def;
    // A point behind the atom (back along its bonds, e.g. where the chain started) says nothing
    // about which way to go next.
    QPointF back;
    for (const auto& id : nbs) back += unit(m.atom(id)->pos() - a->pos());
    if (len(back) > 1e-6 && dot(want, unit(back)) > 0.5) return def;
    std::vector<QPointF> cands;
    if (nbs.size() == 1) {
        QPointF u = unit(m.atom(nbs[0])->pos() - a->pos());
        cands = {rotate(u, 2 * M_PI / 3), rotate(u, -2 * M_PI / 3)};
    } else {
        // The middle of every wide gap between existing bonds.
        std::vector<double> angs;
        for (const auto& id : nbs) {
            QPointF v = m.atom(id)->pos() - a->pos();
            angs.push_back(std::atan2(v.y(), v.x()));
        }
        std::sort(angs.begin(), angs.end());
        for (size_t i = 0; i < angs.size(); ++i) {
            double a0 = angs[i], a1 = (i + 1 < angs.size()) ? angs[i + 1] : angs[0] + 2 * M_PI;
            if (a1 - a0 >= M_PI * 150 / 180 - 1e-6) cands.push_back({std::cos((a0 + a1) / 2), std::sin((a0 + a1) / 2)});
        }
    }
    if (cands.empty()) return def;
    std::sort(cands.begin(), cands.end(), [&](QPointF p, QPointF q) { return dot(p, want) > dot(q, want); });
    // A near tie (the mouse straight behind or ahead) keeps the usual choice.
    if (cands.size() > 1 && dot(cands[0], want) - dot(cands[1], want) < 0.1) return def;
    return cands[0];
}

namespace {
QPointF defaultBondDirection(const Molecule& m, const QString& atomId, int order) {
    const Atom* a = m.atom(atomId);
    if (!a) return {1, 0};
    QStringList nbs = m.neighbors(atomId);
    auto clear = [&](QPointF dir) {
        QPointF p = a->pos() + dir;
        double nearest = 1e9;
        for (const auto& at : m.atoms) if (at.id != atomId) nearest = std::min(nearest, len(at.pos() - p));
        return nearest;
    };
    if (nbs.isEmpty()) return {std::cos(-M_PI / 6), std::sin(-M_PI / 6)};
    if (nbs.size() == 1) {
        const Atom* n = m.atom(nbs[0]);
        QPointF u = unit(n->pos() - a->pos());
        const Bond* existing = m.bondBetween(atomId, nbs[0]);
        if (order == 3 || (existing && existing->order == 3) || (order == 2 && existing && existing->order == 2))
            return -u;
        QPointF c1 = rotate(u, 2 * M_PI / 3), c2 = rotate(u, -2 * M_PI / 3);
        // Zig-zag: go away from the neighbour's other neighbours.
        QStringList nn = m.neighbors(nbs[0]);
        nn.removeAll(atomId);
        if (!nn.isEmpty()) {
            QPointF w = m.atom(nn[0])->pos();
            double d1 = len(a->pos() + c1 - w), d2 = len(a->pos() + c2 - w);
            QPointF pick = d1 >= d2 ? c1 : c2, other = d1 >= d2 ? c2 : c1;
            return clear(pick) > 0.5 ? pick : other;
        }
        // Prefer heading right, then up.
        auto score = [](QPointF c) { return c.x() * 2 - c.y(); };
        QPointF pick = score(c1) >= score(c2) ? c1 : c2, other = score(c1) >= score(c2) ? c2 : c1;
        return clear(pick) > 0.5 ? pick : other;
    }
    return freeDirection(m, atomId);
}
}  // namespace

double snapAngle(double rad, double step) { return std::round(rad / step) * step; }

}  // namespace cd::geom
