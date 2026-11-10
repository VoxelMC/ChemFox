#include "io/Emf.h"

#ifdef _WIN32

#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>

#include <memory>
#include <windows.h>
// GDI+ headers expect min/max macros, which NOMINMAX removes.
#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace cd::io {

namespace {

constexpr double kPad = 6.0;  // matches the other exports

Gdiplus::Color gdiColor(const QColor& c) { return Gdiplus::Color(BYTE(c.alpha()), BYTE(c.red()), BYTE(c.green()), BYTE(c.blue())); }

// Records QPainter calls as GDI+ calls. Lines, ellipses, rects and text reach drawPath
// through QPaintEngine's default implementations (text as glyph outlines).
class GdiplusEngine : public QPaintEngine {
public:
    explicit GdiplusEngine(Gdiplus::Graphics* g)
        : QPaintEngine(PrimitiveTransform | PainterPaths | AlphaBlend | Antialiasing | ConstantOpacity),
          g_(g) {}

    bool begin(QPaintDevice*) override { return true; }
    bool end() override { return true; }
    Type type() const override { return QPaintEngine::User; }

    void updateState(const QPaintEngineState& st) override {
        auto f = st.state();
        if (f & DirtyPen) pen_ = st.pen();
        if (f & DirtyBrush) brush_ = st.brush();
        if (f & DirtyTransform) xf_ = st.transform();
        if (f & DirtyOpacity) opacity_ = st.opacity();
    }

    void drawPath(const QPainterPath& path) override { record(path, true); }

    void drawPolygon(const QPointF* pts, int n, PolygonDrawMode mode) override {
        if (n < 2) return;
        QPainterPath p;
        p.moveTo(pts[0]);
        for (int i = 1; i < n; ++i) p.lineTo(pts[i]);
        if (mode != PolylineMode) p.closeSubpath();
        p.setFillRule(mode == WindingMode ? Qt::WindingFill : Qt::OddEvenFill);
        record(p, mode != PolylineMode);
    }

    void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override {}  // the renderer draws no images

private:
    void record(const QPainterPath& qp, bool fill) {
        Gdiplus::GraphicsPath gp(qp.fillRule() == Qt::WindingFill ? Gdiplus::FillModeWinding : Gdiplus::FillModeAlternate);
        for (int i = 0; i < qp.elementCount(); ++i) {
            const auto e = qp.elementAt(i);
            if (e.isMoveTo()) {
                gp.StartFigure();
            } else if (e.isLineTo()) {
                const auto s = qp.elementAt(i - 1);
                gp.AddLine(Gdiplus::PointF(float(s.x), float(s.y)), Gdiplus::PointF(float(e.x), float(e.y)));
            } else if (e.isCurveTo()) {
                const auto s = qp.elementAt(i - 1), c2 = qp.elementAt(i + 1), end = qp.elementAt(i + 2);
                gp.AddBezier(float(s.x), float(s.y), float(e.x), float(e.y), float(c2.x), float(c2.y),
                             float(end.x), float(end.y));
                i += 2;
            }
        }
        Gdiplus::Matrix m(float(xf_.m11()), float(xf_.m12()), float(xf_.m21()), float(xf_.m22()), float(xf_.dx()),
                          float(xf_.dy()));
        g_->SetTransform(&m);

        auto withOpacity = [&](QColor c) {
            c.setAlphaF(c.alphaF() * opacity_);
            return c;
        };
        if (fill && brush_.style() != Qt::NoBrush) {
            Gdiplus::SolidBrush b(gdiColor(withOpacity(brush_.color())));
            g_->FillPath(&b, &gp);
        }
        if (pen_.style() != Qt::NoPen) {
            float w = float(pen_.widthF() > 0 ? pen_.widthF() : 0.25);
            Gdiplus::Pen p(gdiColor(withOpacity(pen_.color())), w);
            auto cap = pen_.capStyle() == Qt::RoundCap    ? Gdiplus::LineCapRound
                       : pen_.capStyle() == Qt::SquareCap ? Gdiplus::LineCapSquare
                                                          : Gdiplus::LineCapFlat;
            p.SetLineCap(cap, cap, cap == Gdiplus::LineCapRound ? Gdiplus::DashCapRound : Gdiplus::DashCapFlat);
            p.SetLineJoin(pen_.joinStyle() == Qt::RoundJoin   ? Gdiplus::LineJoinRound
                          : pen_.joinStyle() == Qt::BevelJoin ? Gdiplus::LineJoinBevel
                                                              : Gdiplus::LineJoinMiter);
            if (pen_.style() != Qt::SolidLine) {
                // Both Qt and GDI+ measure dashes in pen widths.
                std::vector<Gdiplus::REAL> dashes;
                for (qreal v : pen_.dashPattern()) dashes.push_back(Gdiplus::REAL(std::max<qreal>(v, 0.01)));
                if (!dashes.empty()) p.SetDashPattern(dashes.data(), INT(dashes.size()));
            }
            g_->DrawPath(&p, &gp);
        }
    }

    Gdiplus::Graphics* g_;
    QPen pen_;
    QBrush brush_;
    QTransform xf_;
    qreal opacity_ = 1.0;
};

// A paint device measured in points (72 per inch), so the renderer's units map 1:1.
class EmfDevice : public QPaintDevice {
public:
    EmfDevice(Gdiplus::Graphics* g, QSizeF size) : engine_(g), size_(size) {}
    QPaintEngine* paintEngine() const override { return &engine_; }

protected:
    int metric(PaintDeviceMetric m) const override {
        switch (m) {
        case PdmWidth: return int(std::ceil(size_.width()));
        case PdmHeight: return int(std::ceil(size_.height()));
        case PdmWidthMM: return int(size_.width() * 25.4 / 72);
        case PdmHeightMM: return int(size_.height() * 25.4 / 72);
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY: return 72;
        case PdmDepth: return 32;
        case PdmNumColors: return INT_MAX;
        case PdmDevicePixelRatio: return 1;
        case PdmDevicePixelRatioScaled: return int(QPaintDevice::devicePixelRatioFScale());
        default: return 0;
        }
    }

private:
    mutable GdiplusEngine engine_;
    QSizeF size_;
};

struct GdiplusSession {
    ULONG_PTR token = 0;
    bool ok = false;
    GdiplusSession() {
        Gdiplus::GdiplusStartupInput in;
        ok = Gdiplus::GdiplusStartup(&token, &in, nullptr) == Gdiplus::Ok;
    }
    ~GdiplusSession() {
        if (ok) Gdiplus::GdiplusShutdown(token);
    }
};

}  // namespace

bool emfSupported() { return true; }

bool exportEmf(const QString& path, const Document& d, const Renderer& r, const ChemOverlay& ov, QString* error) {
    GdiplusSession session;
    if (!session.ok) {
        if (error) *error = "GDI+ is not available.";
        return false;
    }
    QRectF rect = r.documentBounds(d, ov);
    if (rect.isNull()) rect = QRectF(0, 0, 10, 10);
    rect.adjust(-kPad, -kPad, kPad, kPad);

    HDC ref = GetDC(nullptr);
    std::wstring file = path.toStdWString();
    bool ok = false;
    {
        Gdiplus::Metafile mf(file.c_str(), ref, Gdiplus::RectF(0, 0, float(rect.width()), float(rect.height())),
                             Gdiplus::MetafileFrameUnitPoint, Gdiplus::EmfTypeEmfPlusDual, L"ChemFox figure");
        if (mf.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(&mf);
            g.SetPageUnit(Gdiplus::UnitPoint);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            EmfDevice dev(&g, rect.size());
            QPainter p(&dev);
            p.translate(-rect.topLeft());
            if (r.style().background != QColor(Qt::white)) p.fillRect(rect, r.style().background);
            r.draw(p, d, ov);
            p.end();
            ok = g.GetLastStatus() == Gdiplus::Ok;
        }
    }  // Graphics, then Metafile, must be destroyed for the file to be complete.
    ReleaseDC(nullptr, ref);
    if (!ok && error) *error = "Could not write " + path;
    return ok;
}

}  // namespace cd::io

#else

namespace cd::io {
bool emfSupported() { return false; }
bool exportEmf(const QString&, const Document&, const Renderer&, const ChemOverlay&, QString* error) {
    if (error) *error = "EMF export is only available on Windows.";
    return false;
}
}  // namespace cd::io

#endif
