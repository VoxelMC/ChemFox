#pragma once
// The drawing canvas: zoom/pan, hit-testing, hover state, hover-and-type keys
// and mouse tools. Owns the document and its undo stack.

#include "core/chem/Chem.h"
#include "core/commands/Commands.h"
#include "editor/Keymap.h"
#include "editor/Snap.h"
#include "render/Renderer.h"
#include "style/Style.h"
#include "warnings/Warnings.h"

#include <QLineEdit>
#include <QPointer>
#include <QUndoStack>
#include <QWidget>
#include <functional>

namespace cd {

// A reaction typed as text (reaction builder).
struct ReactionSpec {
    QStringList reactants, agents, products;  // one SMILES or SMARTS per structure
    QString above, below;
    ArrowKind kind = ArrowKind::Forward;
    bool autoMap = true;   // map automatically (otherwise use the map numbers typed in)
    bool showDiff = true;  // colour broken/formed/changed bonds
};

struct ReactionResult {
    QString arrowId;
    QStringList errors;  // per row (reactants, agents, products in order); empty = parsed
    int mapped = 0;      // atoms carrying a map number in the products
};

struct Hit {
    enum Kind { None, AtomHit, BondHit, NumberHit, ArrowHit, TextHit, CurvedHit, PlusHit } kind = None;
    QString id;     // atom, bond, arrow, text or mechanism-arrow id; molecule id for numbers
    QString molId;  // owning molecule for atoms, bonds, numbers
    double dist = 0;
};

class Canvas : public QWidget, public DocumentHost {
    Q_OBJECT
public:
    explicit Canvas(QWidget* parent = nullptr);

    // What a drag on empty canvas does. Hover-and-type keys and dragging from atoms work in every tool.
    enum class Tool { Select, Chain, Arrow, BentArrow, Text, Mechanism };
    Tool tool() const { return tool_; }
    void setTool(Tool t);

    // DocumentHost
    Document& document() override { return doc_; }
    void documentReplaced() override;

    const Document& doc() const { return doc_; }
    QUndoStack* undoStack() { return &undo_; }
    const Renderer& renderer() const { return renderer_; }
    const ChemOverlay& overlay() const { return overlay_; }
    const Style& style() const { return style_; }
    const std::vector<Warning>& warnings() const { return active_; }
    const std::vector<Warning>& allWarnings() const { return all_; }
    QSet<QString> selection() const { return selected_; }
    Keymap& keymap() { return keymap_; }
    QPointF modelToScreen(QPointF m) const { return toScreen(renderer_.toPt(m)); }

    // Replace the whole document (open/new); clears undo history.
    void setDocument(const Document& d);
    // Run an edit on a copy of the document and push it as one undoable command.
    void edit(const QString& text, const std::function<void(Document&)>& fn, int mergeKey = -1);

    // Molecule the info panel should describe: hovered, else selected, else none.
    const Molecule* infoMolecule() const;
    // Cached chemistry info for a molecule.
    chem::MolInfo info(const Molecule& m);

    // Commands
    void cleanStructure();
    void arrangeScheme();
    void alignAllProducts();
    // Named commands for actions that act on the hovered thing or the selection (flip, copy as,
    // arrow styles…), so the menus and any keyboard shortcut do exactly the same thing.
    void command(const QString& id);
    static QStringList commandIds();
    void reloadKeymap() { keymap_.load(); }
    // Custom groups were edited: recompute formulas, warnings and labels.
    void groupsChanged() { recompute(); update(); emit infoTargetChanged(); }
    void alignToNeighbour(const QString& molId, const QString& refId);
    void numberCompounds();
    // Inserted at `at` (model units) when given, else at the cursor or the view centre.
    void insertMolecules(const std::vector<Molecule>& mols, std::optional<QPointF> at = std::nullopt);
    void insertSmiles(const QString& smiles, std::optional<QPointF> at = std::nullopt);
    void selectAll();
    void deleteSelection();
    void copy();
    void cut();
    void paste();
    void zoomBy(double factor, QPointF anchor = {});
    void zoomToFit();
    // Draw an arrow from the selected structure in `dir` (a unit axis) and a copy of it past the
    // head, ready to be changed into the product.
    void continueReaction(QPointF dir);
    // Colour bonds broken, formed and changed for these arrows' steps (all steps if none given),
    // auto-mapping steps that have no mapping yet. Toggles: on unless all are already on.
    void toggleBondChanges(QStringList arrowIds = {});
    bool showsBondChanges(const QString& arrowId) const;
    // Preferences: new bonds/rings/groups lean toward the mouse; the scroll wheel pans (Ctrl+wheel zooms).
    void setSteerToMouse(bool on) { steerToMouse_ = on; }
    void setWheelPans(bool on) { wheelPans_ = on; }
    // The focused atom (orange), if any: keys act on it until Escape or a click.
    QString focusedAtom() const { return stickyAtom_; }
    // Move the focused atom to its bonded neighbour in direction `dir` (screen axes, y down).
    void moveFocus(QPointF dir);
    void resetZoom();
    void goTo(const Warning& w);
    void setStyleName(const QString& name);
    void setStyleText(const QString& text);
    void silence(const Warning& w, const QString& scope);  // "instance" | "document" | "global"
    void refreshWarnings();
    void setStepLetters(bool on);
    void setShowMaps(bool on);
    // Mapping mode: drag from a reactant atom to a product atom to map them; drag an atom
    // onto empty canvas to unmap it. Map numbers and partners are shown while it is on.
    bool mapMode() const { return mapMode_; }
    void setShowPageGuide(bool on) { showPageGuide_ = on; update(); }
    bool showsPageGuide() const { return showPageGuide_; }
    // A dot grid (screen only, never exported); with snapping on, dragged objects land on it.
    // `size` is the grid spacing in bond lengths.
    void setGrid(bool show, bool snap, double size);
    bool showsGrid() const { return showGrid_; }
    bool snapsToGrid() const { return snapGrid_; }
    double gridSize() const { return gridSize_; }
    // Copies of the selection (or the hovered object), offset a little and selected.
    void duplicateSelection();
    // Draw the selection (or the hovered object) above / below the others of its kind.
    void restackSelection(bool toFront);
    void setMapMode(bool on);

    // Mechanism arrows from the keyboard: the first press (on an atom, bond or lone pair) picks where the
    // electrons come from, the second (on an atom or bond, or empty canvas) where they go.
    void startCurved(bool half);
    // Remove one "+" between structures of a step ("plus:<arrow>:<structure>", as Hit ids name them).
    void removePlus(const QString& plusId);
    bool curvedPending() const { return curvedPending_; }
    // Line up or space out the selection (or the hovered object with the selection).
    void alignSelection(snap::Align mode);
    // Show a rotation handle on the selection (or the hovered structure); dragging it turns them in 15° steps.
    void startRotate();
    bool rotating() const { return !rotateIds_.isEmpty(); }
    QPointF rotateHandleScreen() const { return modelToScreen(rotateHandle()); }
    // Snap guides drawn during the current Shift+drag (points), for tests.
    const std::vector<QLineF>& guides() const { return guides_; }
    // The atom or bond a mechanism-arrow end being dragged would attach to (empty: a free end), for tests.
    QString dragTarget() const { return curvedTarget_.id; }

    // Reaction builder: create (empty arrowId) or replace the step drawn with that arrow.
    ReactionResult applyReaction(const ReactionSpec& spec, const QString& arrowId, int mergeKey = -1);
    // The step drawn with an arrow, as text. Map numbers are included when withMaps is set.
    std::optional<ReactionSpec> reactionFor(const QString& arrowId, bool withMaps) const;
    // Arrow of the selected reaction: a selected arrow, or the step of a selected molecule.
    QString selectedArrow() const;

signals:
    void documentChanged();
    void infoTargetChanged();
    void warningsChanged();
    void styleChanged();
    void mapModeChanged(bool on);
    void toolChanged(int tool);
    void addToReaction(const QString& smiles, int role);  // 0 reactant, 1 reagent, 2 product
    void editReaction(const QString& arrowId);             // open this arrow's step in the Reaction Builder
    void gridChanged();                                     // grid shown or snapping toggled from the canvas
    void status(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void leaveEvent(QEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    bool event(QEvent*) override;
    bool focusNextPrevChild(bool) override { return false; }

private:
    enum class Drag { None, Pan, BondFromAtom, Chain, Move, Rubber, ArrowHead, ArrowTail, MapLink, DrawArrow,
                      ArrowCorner, ArrowSegment, DrawCurved, CurvedBulge, CurvedFrom, CurvedTo, CurvedC1, CurvedC2, Rotate };

    QPointF toPt(QPointF screen) const { return (screen - pan_) / zoom_; }
    QPointF toScreen(QPointF pt) const { return pt * zoom_ + pan_; }
    QPointF toModel(QPointF screen) const { return renderer_.toModel(toPt(screen)); }

    std::vector<Hit> hitTest(QPointF screen) const;
    void updateHover(QPointF screen);
    Hit hover() const { return hoverIndex_ < (int)hits_.size() ? hits_[hoverIndex_] : Hit{}; }
    QString hoverTargetName(const Hit& h) const;
    void runAction(const QString& action, const Hit& h, QPointF cursorModel);
    void applyToSelection(const QString& action);
    void updateStatus();

    void editLabel(const QString& atomId);
    void editCompound(const QString& molId);
    void editArrow(const QString& arrowId);
    void editText(const QString& textId);
    void startInlineEditor(const QString& initial, QPointF screenPos, std::function<void(const QString&)> done);

    void recompute();
    void resolveStyle();
    QString moleculeKey(const Molecule& m) const;
    QSet<QString> moveSetFor(const Hit& h);

    Document doc_;
    QUndoStack undo_;
    Style style_;
    std::vector<StyleIssue> styleIssues_;
    Renderer renderer_;
    ChemOverlay overlay_;
    QHash<QString, chem::Analysis> analyses_;  // by molecule id
    QHash<QString, chem::Analysis> analysisCache_;  // by molecule content
    QHash<QString, chem::MolInfo> infoCache_;
    std::vector<Warning> all_, active_;
    QSet<QString> globalSilenced_;
    Keymap keymap_;

    double zoom_ = 2.0;
    QPointF pan_{60, 60};
    // After Zoom to Fit (which opening a document does) the view stays fitted and centred while the
    // window settles or is resized (first layout, restored or maximised geometry), until the user
    // pans, zooms or edits.
    bool fitPending_ = false;
    // Building a chain from the keyboard: keys act on the newest atom (marked orange) wherever the
    // mouse is, until Escape or a click; the mouse steers each new bond.
    QString stickyAtom_;
    int groupsGen_ = -1;
    bool steerToMouse_ = true;
    bool wheelPans_ = false;  // labels::groupsGeneration() the chemistry caches were built with
    QPointF lastMouse_;
    bool mouseInside_ = false;
    std::vector<Hit> hits_;
    int hoverIndex_ = 0;
    QSet<QString> selected_;

    Drag drag_ = Drag::None;
    QPointF pressScreen_;
    QPointF pressModel_;
    Hit pressHit_;
    bool dragMoved_ = false;
    Document dragOrig_;
    QSet<QString> moveSet_;
    QPointF lastDragModel_;
    std::vector<QPointF> preview_;  // model points of a bond/chain preview
    QRectF rubber_;
    bool spaceDown_ = false;
    bool mapMode_ = false;
    bool showPageGuide_ = false;
    bool showGrid_ = false;
    bool snapGrid_ = false;
    double gridSize_ = 0.5;
    QPointF moveRef_;  // the point of a dragged object that snaps to the grid (model units)
    QPointF snapToGrid(QPointF model) const;
    QSet<QString> actionTargets() const;  // hovered object (with the selection, if part of it), else the selection
    int dragIndex_ = 0;  // corner or segment of the arrow being reshaped
    void finishMapLink(QPointF screen);
    Tool tool_ = Tool::Select;
    std::vector<QPointF> bentPts_;  // corners of the bent arrow being placed (model units)
    QPointF bentSnap(QPointF model, bool snap) const;
    void finishBentArrow();
    void addTextAt(QPointF model);
    void showContextMenu(QPointF screen);
    // The arrow end (tail or head) within a few pixels of a screen point, if any.
    bool arrowEndAt(QPointF screen, QString* arrowId, bool* head) const;
    QSet<QString> transformTargets(const Hit& h) const;
    std::vector<Step> inferredSteps() const;
    // The structure to line molId up with: its biggest reactant, else its biggest product.
    QString alignReference(const QString& molId, QString* name = nullptr, bool* isReactant = nullptr) const;
    struct CleanCycle {
        QString mol;
        int undoIndex = -1;
        int index = 0;
        int mergeKey = -1;
        std::vector<Molecule> variants;
        QStringList names;
    } cycle_;
    int cycleSerial_ = 0;

    // Mechanism arrows
    bool curvedPending_ = false;
    bool curvedHalf_ = false;
    Hit curvedTarget_;  // what a dragged mechanism-arrow end would attach to (highlighted); None = a free end
    // The atom or bond under the mouse that a dragged arrow end would attach to, and where the preview should end.
    Hit curvedTargetAt(QPointF screen, QPointF model, QPointF* end) const;
    CurvedEnd curvedFrom_;
    // The end a hit stands for: an atom (its lone pair if it shows any or the mouse is on one), a bond, or a point.
    CurvedEnd endFor(const Hit& h, QPointF model) const;
    void finishCurved(const Hit& h, QPointF model);
    // The handle of a hovered or selected mechanism arrow within a few pixels of a screen point:
    // 0 its tail, 1 its head, 2 and 3 its control points. -1 if none.
    int curvedHandleAt(QPointF screen, QString* id) const;
    // The arrow with its current shape (automatic or not) written in as a hand-set one, ready to adjust.
    CurvedArrow withShape(const CurvedArrow& c, const Document& d) const;

    // Moving: the document dragged things are moved within (the original, or the original plus copies
    // for an Alt+drag duplicate), and the guides shown while Shift snaps.
    Document moveBase_;
    bool altDuplicate_ = false;
    bool dupMade_ = false;
    bool atomMove_ = false;  // the current move drags atoms on their own, not whole structures
    double rotateReach_ = 0;  // points from the centre to the top of the objects, when the turn began
    void alignIds(const QSet<QString>& ids, snap::Align mode);
    void startRotateOn(const QSet<QString>& ids);
    std::vector<QLineF> guides_;
    QRectF objectRect(const QString& id) const;  // points
    QPointF snapGuideDelta(const QSet<QString>& ids, QPointF delta);  // model units; fills guides_

    QSet<QString> rotateIds_;
    QPointF rotateCenter_;  // model units
    double rotateStart_ = 0, rotateShown_ = 0;  // radians
    QPointF rotateHandle() const;  // model units

    QPointer<QLineEdit> inline_;
    QString editingId_;
};

}  // namespace cd
