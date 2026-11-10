#pragma once
// Export (SVG, PDF, PNG, clipboard) and file import (MOL/SDF, SMILES, InChI).

#include "core/model/Document.h"
#include "render/Renderer.h"

#include <QImage>
#include <QMimeData>
#include <optional>

namespace cd::io {

constexpr const char* kMimeType = "application/x-chemfox+json";

// A subset of the document for copy/export: selected molecules, arrows and texts.
Document subset(const Document& d, const QSet<QString>& ids);

// SVG with the native JSON embedded in <metadata>, so figures can be reopened.
QByteArray svgBytes(const Document& d, const Renderer& r, const ChemOverlay& ov);
std::optional<Document> documentFromSvg(const QByteArray& svg, QString* error = nullptr);

bool exportSvg(const QString& path, const Document& d, const Renderer& r, const ChemOverlay& ov, QString* error = nullptr);
bool exportPdf(const QString& path, const Document& d, const Renderer& r, const ChemOverlay& ov, QString* error = nullptr);
QImage renderImage(const Document& d, const Renderer& r, const ChemOverlay& ov, double dpi, bool transparent = false);
bool exportPng(const QString& path, const Document& d, const Renderer& r, const ChemOverlay& ov, double dpi,
               bool transparent, QString* error = nullptr);
bool exportMol(const QString& path, const Document& d, QString* error = nullptr);

// Image + SVG + SMILES text + native JSON, ready for the clipboard.
QMimeData* mimeData(const Document& d, const Renderer& r, const ChemOverlay& ov);

// Structures from a MOL/SDF/SMILES/InChI file.
std::vector<Molecule> importFile(const QString& path, QString* error = nullptr);

}  // namespace cd::io
