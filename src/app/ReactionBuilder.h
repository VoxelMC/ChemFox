#pragma once
// Reaction builder panel: type reactants, reagents and products as SMILES or
// SMARTS (or one reaction SMILES), add and remove rows, and the scheme on the
// canvas updates live, auto-mapped with broken/formed bonds coloured.

#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace cd {

class Canvas;
struct ReactionSpec;
struct ReactionResult;

class ReactionBuilder : public QWidget {
    Q_OBJECT
public:
    explicit ReactionBuilder(Canvas* canvas, QWidget* parent = nullptr);

    void newReaction();
    void editSelected();
    // Put a structure into the first empty row of a section (0 reactants, 1 reagents, 2 products).
    void addStructure(const QString& smiles, int role);

private:
    struct Row {
        QWidget* widget;
        QLineEdit* edit;
        QLabel* status;
    };
    struct Section {
        QVBoxLayout* rows;
        std::vector<Row> items;
    };

    Row& addRow(Section& s, const QString& text = {});
    void removeRow(Section& s, QWidget* w);
    void load(const ReactionSpec& spec);
    ReactionSpec spec() const;
    void changed();
    void apply();
    void parseLine();
    void syncLine();
    void updateStatus(const ReactionResult& r);
    void documentChanged();
    void refreshSummary();

    Canvas* canvas_;
    QString arrowId_;  // the reaction being edited; empty until the first apply
    QLineEdit* line_;
    Section reactants_, agents_, products_;
    QComboBox* kind_;
    QLineEdit* above_;
    QLineEdit* below_;
    QCheckBox* autoMap_;
    QCheckBox* diff_;
    QCheckBox* showMaps_;
    QPushButton* mapEdit_;
    QLabel* target_;
    QLabel* summary_;
    QTimer timer_;
    bool loading_ = false;
};

}  // namespace cd
