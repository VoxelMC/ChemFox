#pragma once
// Edit → Preferences (ChemFox → Settings… on macOS): app-wide choices, kept in QSettings and applied at once.
//
//   General  default style for new documents, reopen the last file, automatic update checks
//   Styles   your saved styles: import, export, delete, open the folder
//   Drawing  new bonds lean toward the mouse; scroll wheel zooms or pans
//   Export   PNG resolution, whether to ask each time, transparent background
//   Keys     opens Keyboard Shortcuts (keys and custom groups)

#include <QDialog>
#include <QString>
#include <functional>

class QCheckBox;
class QComboBox;
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

QString defaultStyle();
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

    std::function<void()> openShortcuts_;
    QComboBox* defaultStyle_ = nullptr;
    QListWidget* userStyles_ = nullptr;
};

}  // namespace cd
