#pragma once
// Scheme layout: places each scheme's molecules and arrows on one baseline
// with spacing from the style, wrapping to new rows at the page width.

#include "render/Renderer.h"

namespace cd::layout {

// With `only`, just the steps whose arrow is in it (or whose reactant and product both are) are laid
// out, and only the objects in it move; everything else stays put but still counts, as an anchor
// for the steps that touch it and as something not to land on.
void layoutSchemes(Document& d, const Renderer& r, const ChemOverlay& ov, const QSet<QString>* only = nullptr);

}  // namespace cd::layout
