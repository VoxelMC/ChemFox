#pragma once
// Style editor: one tab per part of the drawing with standard controls (fonts, sizes in pt,
// colours, switches), plus a Source tab. The form writes only what differs from the base
// style, in the standard layout, and stays in sync with the document's embedded style.

#include "style/Style.h"

#include <QTimer>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTabWidget;
class QToolButton;

namespace cd {

class Canvas;

class StyleEditor : public QWidget {
    Q_OBJECT
public:
    explicit StyleEditor(Canvas* canvas, QWidget* parent = nullptr);

private:
    enum class Kind { Length, Percent, Color, Bool, Font, Choice };
    struct Field {
        QString tab, section, key, label, tip;
        Kind kind;
        std::function<QVariant(const Style&)> get;
        QStringList choices;
        QWidget* editor = nullptr;
        QLabel* labelWidget = nullptr;
        QToolButton* revert = nullptr;
    };

    void buildFields();
    QWidget* makeEditor(Field& f);
    void refresh();                 // document -> form
    void fieldChanged(Field& f, const QString& value);
    void revertField(Field& f);
    void apply();                   // form -> document
    void saveAs();
    static QString format(Kind kind, const QVariant& v);

    Canvas* canvas_;
    styles::Source source_;         // the style being edited (its own settings only)
    QString base_;                  // what unset settings fall back to
    std::vector<Field> fields_;
    QComboBox* baseBox_;
    QLabel* nameLabel_;
    QTabWidget* tabs_;
    QPlainTextEdit* sourceEdit_;
    QTimer sourceTimer_;
    bool updating_ = false;
};

}  // namespace cd
