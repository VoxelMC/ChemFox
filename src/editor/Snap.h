#pragma once
// Positioning helpers for the canvas: snap guides while dragging, and align / distribute.
// Pure geometry on rectangles (in points), so it can be tested without a canvas.

#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <vector>

namespace cd::snap {

// What an object offers to snap to: its box (edges and centre) and any text baselines.
struct Box {
    QRectF rect;
    std::vector<double> baselines;  // y of each
};

struct Result {
    QPointF delta;               // extra movement that makes the dragged box line up
    std::vector<QLineF> guides;  // lines to draw while it does
};

// Snap a dragged box against the others: edges and centres line up, baselines line up, and gaps
// repeat (the box sits one equal gap past a row of two, or centred between two neighbours).
// Each axis snaps on its own, to the nearest candidate within `tol`.
Result snapMove(const Box& moving, const std::vector<Box>& others, double tol);

enum class Align { Left, HCenter, Right, Top, VCenter, Bottom, DistributeH, DistributeV };

// How far each rectangle moves. Aligning lines every box up with the group's own edge or centre;
// distributing keeps the two outermost where they are and evens out the gaps between the rest.
std::vector<QPointF> alignDeltas(const std::vector<QRectF>& rects, Align mode);

}  // namespace cd::snap
