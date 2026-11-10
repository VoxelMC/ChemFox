#include "io/Export.h"

#include "core/chem/Chem.h"
#include "io/Native.h"

#include <QBuffer>
#include <QFile>
#include <QJsonDocument>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSvgGenerator>

namespace cd::io {

namespace {

constexpr double kPad = 6.0;  // points of white space around exported figures

QRectF exportRect(const Document& d, const Renderer& r, const ChemOverlay& ov) {
    QRectF b = r.documentBounds(d, ov);
    if (b.isNull()) b = QRectF(0, 0, 10, 10);
    return b.adjusted(-kPad, -kPad, kPad, kPad);
}

bool needsBackground(const Style& s) { return s.background != QColor(Qt::white); }

bool writeBytes(const QString& path, const QByteArray& bytes, QString* error) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(bytes);
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

}  // namespace

Document subset(const Document& d, const QSet<QString>& ids) {
    if (ids.isEmpty()) return d;
    Document out = d;
    out.molecules.clear();
    out.arrows.clear();
    out.texts.clear();
    for (const auto& m : d.molecules) {
        bool any = ids.contains(m.id);
        for (const auto& a : m.atoms) any |= ids.contains(a.id);
        for (const auto& b : m.bonds) any |= ids.contains(b.id);
        if (any) out.molecules.push_back(m);
    }
    for (const auto& a : d.arrows) if (ids.contains(a.id)) out.arrows.push_back(a);
    for (const auto& t : d.texts) if (ids.contains(t.id)) out.texts.push_back(t);
    // Mechanism arrows come along when the structures they join do (free ones only if picked themselves).
    out.curved.clear();
    for (const auto& c : d.curved)
        if (c.from.attached() || c.to.attached() || ids.contains(c.id)) out.curved.push_back(c);
    out.pruneCurved();
    out.pruneSchemes();
    return out;
}

QByteArray svgBytes(const Document& d, const Renderer& r, const ChemOverlay& ov) {
    QRectF rect = exportRect(d, r, ov);
    QBuffer buf;
    buf.open(QIODevice::WriteOnly);
    QSvgGenerator gen;
    gen.setOutputDevice(&buf);
    gen.setResolution(72);  // one SVG unit = one point
    gen.setSize(rect.size().toSize());
    gen.setViewBox(QRectF(QPointF(0, 0), rect.size()));
    gen.setTitle("ChemFox figure");
    {
        QPainter p(&gen);
        p.translate(-rect.topLeft());
        if (needsBackground(r.style())) p.fillRect(rect, r.style().background);
        r.draw(p, d, ov);
    }
    QString svg = QString::fromUtf8(buf.data());
    // Size in points so the figure lands at its true size in documents.
    svg.replace(QRegularExpression("width=\"([\\d.]+)mm\""), QString("width=\"%1pt\"").arg(rect.width()));
    svg.replace(QRegularExpression("height=\"([\\d.]+)mm\""), QString("height=\"%1pt\"").arg(rect.height()));
    QString json = QString::fromUtf8(QJsonDocument(toJsonObject(d)).toJson(QJsonDocument::Compact));
    json.replace("]]>", "]]]]><![CDATA[>");
    QString meta = "<metadata><chemfox:document xmlns:chemfox=\"urn:chemfox\"><![CDATA[" + json +
                   "]]></chemfox:document></metadata>";
    int svgTag = svg.indexOf("<svg");
    int close = svgTag >= 0 ? svg.indexOf('>', svgTag) : -1;
    if (close >= 0) svg.insert(close + 1, "\n" + meta + "\n");
    return svg.toUtf8();
}

std::optional<Document> documentFromSvg(const QByteArray& svg, QString* error) {
    QString s = QString::fromUtf8(svg);
    static const QRegularExpression re("<(?:chemfox|chemdoc):document[^>]*><!\\[CDATA\\[(.*)\\]\\]></(?:chemfox|chemdoc):document>",
                                       QRegularExpression::DotMatchesEverythingOption);
    auto m = re.match(s);
    if (!m.hasMatch()) {
        if (error) *error = "This SVG has no embedded ChemFox document.";
        return std::nullopt;
    }
    QString json = m.captured(1);
    json.replace("]]]]><![CDATA[>", "]]>");
    return fromJson(json.toUtf8(), error);
}

bool exportSvg(const QString& path, const Document& d, const Renderer& r, const ChemOverlay& ov, QString* error) {
    return writeBytes(path, svgBytes(d, r, ov), error);
}

bool exportPdf(const QString& path, const Document& d, const Renderer& r, const ChemOverlay& ov, QString* error) {
    QRectF rect = exportRect(d, r, ov);
    QPdfWriter pdf(path);
    pdf.setCreator("ChemFox");
    pdf.setResolution(1200);
    pdf.setPageSize(QPageSize(rect.size(), QPageSize::Point));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p;
    if (!p.begin(&pdf)) {
        if (error) *error = "Could not write " + path;
        return false;
    }
    p.scale(1200.0 / 72.0, 1200.0 / 72.0);
    p.translate(-rect.topLeft());
    if (needsBackground(r.style())) p.fillRect(rect, r.style().background);
    r.draw(p, d, ov);
    p.end();
    return true;
}

QImage renderImage(const Document& d, const Renderer& r, const ChemOverlay& ov, double dpi, bool transparent) {
    QRectF rect = exportRect(d, r, ov);
    double k = dpi / 72.0;
    QImage img(QSize(int(std::ceil(rect.width() * k)), int(std::ceil(rect.height() * k))),
               QImage::Format_ARGB32_Premultiplied);
    img.fill(transparent ? QColor(Qt::transparent) : r.style().background);
    img.setDotsPerMeterX(int(dpi / 0.0254));
    img.setDotsPerMeterY(int(dpi / 0.0254));
    QPainter p(&img);
    p.scale(k, k);
    p.translate(-rect.topLeft());
    r.draw(p, d, ov);
    return img;
}

bool exportPng(const QString& path, const Document& d, const Renderer& r, const ChemOverlay& ov, double dpi,
               bool transparent, QString* error) {
    QImage img = renderImage(d, r, ov, dpi, transparent);
    if (!img.save(path, "PNG")) {
        if (error) *error = "Could not write " + path;
        return false;
    }
    return true;
}

bool exportMol(const QString& path, const Document& d, QString* error) {
    QString out;
    for (const auto& m : d.molecules) {
        out += chem::toMolBlock(m);
        if (d.molecules.size() > 1) out += "$$$$\n";
    }
    return writeBytes(path, out.toUtf8(), error);
}

QMimeData* mimeData(const Document& d, const Renderer& r, const ChemOverlay& ov) {
    auto* mime = new QMimeData;
    mime->setData(kMimeType, toJson(d, false));
    mime->setData("image/svg+xml", svgBytes(d, r, ov));
    mime->setImageData(renderImage(d, r, ov, 300));
    QStringList smiles;
    for (const auto& m : d.molecules) smiles << chem::smiles(m);
    mime->setText(smiles.join('.'));
    return mime;
}

std::vector<Molecule> importFile(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return {};
    }
    QString text = QString::fromUtf8(f.readAll());
    QString lower = path.toLower();
    if (lower.endsWith(".mol") || lower.endsWith(".sdf") || lower.endsWith(".sd")) return chem::fromMolBlocks(text, error);
    return chem::fromText(text, error);
}

}  // namespace cd::io
