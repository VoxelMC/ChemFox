#pragma once
// EMF export for Word and PowerPoint (Windows only). The normal renderer draws
// into a QPaintDevice whose paint engine records GDI+ calls into an EMF+ dual
// metafile, so the figure stays vector and matches every other export.

#include "core/model/Document.h"
#include "render/Renderer.h"

namespace cd::io {

bool emfSupported();
bool exportEmf(const QString& path, const Document& d, const Renderer& r, const ChemOverlay& ov,
               QString* error = nullptr);

}  // namespace cd::io
