#pragma once
// Edit → Preferences (ChemFox → Settings… on macOS): app-wide choices, kept in QSettings and applied at once.
//
//   General  default style for new documents, tool at startup, documents folder, reopen the last file,
//            autosave for crash recovery, automatic update checks
//   Styles   your saved styles: preview, import, export, rename, delete, set as default, open the folder
//   Drawing  bonds lean toward the mouse; bond angle steps; scroll wheel; page width guide; grid
//   Export   default format (Ctrl+E), PNG resolution, whether to ask each time, transparent background
//   Keys     opens Keyboard Shortcuts (keys and custom groups)

#include <QDialog>
#include <QString>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QSpinBox;

namespace cd {

namespace prefs {
// Keys and defaults, in one place so the app, the dialog and tests agree.
inline const char* kDefaultStyle = "prefs/defaultStyle";   // QString, "default"
inline const char* kReopenLast = "prefs/reopenLast";       // bool, false
inline const char* kLastFile = "prefs/lastFile";           // QString: the last file opened or saved
inline const char* kAutoUpdate = "updates/auto";           // bool, true
inline const char* kSteerToMouse = "prefs/steerToMouse";   // bool, true
inline const char* kWheelPans = "prefs/wheelPans";         // bool, false (Ctrl+wheel then zooms)
inline const char* kPngDpi = "pngDpi";                     // int, 300
inline const char* kPngAsk = "prefs/pngAsk";               // bool, true
inline const char* kPngTransparent = "prefs/pngTransparent"; // bool, false
inline const char* kStartTool = "prefs/startTool";         // QString: select, chain, arrow, bent, text
inline const char* kDocFolder = "prefs/documentFolder";    // QString; empty = the last folder used
inline const char* kAutosave = "prefs/autosave";           // bool, true: keep a recovery copy of unsaved work
inline const char* kAutosaveMinutes = "prefs/autosaveMinutes"; // int, 2
inline const char* kBondStep = "prefs/bondStep";           // int degrees, 30 (or 15)
inline const char* kPageGuide = "prefs/pageGuide";         // bool, false
inline const char* kShowGrid = "prefs/showGrid";           // bool, false
inline const char* kSnapGrid = "prefs/snapGrid";           // bool, false
inline const char* kGridSize = "prefs/gridSize";           // double, bond lengths, 0.5
inline const char* kExportFormat = "prefs/exportFormat";   // QString: svg, pdf, png, emf; default svg

QString defaultStyle();
QString exportFormat();  // a format this build can write
// Where Open and Save As start: the documents folder if one is set, else the last folder used.
QString startFolder();
// Copy a .style file into the user style folder. Returns the style's name, or empty with *error set.
// An existing file of the same name is only replaced when overwrite is true.
QString importStyle(const QString& file, bool overwrite, QString* error);
}  // namespace prefs

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    // openShortcuts: shows the Keyboard Shortcuts dialog (owned by the main window).
    PreferencesDialog(std::function<void()> openShortcuts, QWidget* parent = nullptr);

signals:
    // Something changed: the main window re-applies settings (canvas options, style list).
    void changed();

private:
    QWidget* generalTab();
    QWidget* stylesTab();
    QWidget* drawingTab();
    QWidget* exportTab();
    QWidget* keysTab();
    void fillStyles();
    void showPreview();

    std::function<void()> openShortcuts_;
    QComboBox* defaultStyle_ = nullptr;
    QListWidget* userStyles_ = nullptr;
    QLabel* preview_ = nullptr;
};

}  // namespace cd
