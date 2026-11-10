#pragma once

#include <QMainWindow>
#include "app/Shortcuts.h"
#include "core/model/Document.h"

#include <QLockFile>
#include <QTimer>
#include <memory>
#include <functional>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTreeWidget;
class QAction;
class QMenu;
class QToolBar;
class QTextBrowser;

namespace cd {

class Canvas;
struct Warning;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    bool openPath(const QString& path);
    // Preferences → General → "Reopen the last document": opens it if that's on and the file exists.
    bool reopenLastFile();
    // Offers work left unsaved when ChemFox last closed without saving (a crash). Returns true if a
    // document was recovered into the window.
    bool offerRecovery();
    // Where this window keeps its recovery copy (exposed for tests).
    QString recoveryPath() const { return recoveryPath_; }
    void writeRecovery();   // save a recovery copy now if there are unsaved changes, else remove it

protected:
    void closeEvent(QCloseEvent*) override;

private:
    void buildMenus();
    void buildDocks();
    void newDocument();
    void open();
    bool save();
    bool saveAs();
    void importStructure();
    void pasteSmilesDialog();
    void exportAs(const QString& kind);
    void showSilenced();
    void showKeyReference();
    bool maybeSave();
    void updateTitle();
    void updateInfo();
    void updateWarnings();
    void syncStyleUi();
    Document blankDocument() const;  // empty, in the preferred default style
    void applyPreferences();         // canvas options, update toggle, default style
    void showPreferences();
    void openShortcuts();
    const Warning* currentWarning() const;

    Canvas* canvas_;
    QString path_;
    QTextBrowser* info_;
    QTreeWidget* warnTree_;
    QComboBox* styleBox_;
    QAction* stepLetters_;
    QMenu* viewMenuDocks_ = nullptr;
    QMenu* reactionMenu_ = nullptr;
    QAction* mapModeAction_ = nullptr;
    QAction* autoUpdateAction_ = nullptr;
    QAction* pageGuideAction_ = nullptr;
    QAction* gridAction_ = nullptr;
    QAction* snapGridAction_ = nullptr;
    QAction* exportDefaultAction_ = nullptr;
    QTimer autosave_;
    QString recoveryPath_;
    std::unique_ptr<QLockFile> recoveryLock_;  // held while this window runs, so another ChemFox won't take our copy
    void clearRecovery();
    std::function<void()> refreshToolKeys_;  // re-labels the tools with their current hover keys
    QToolBar* toolbar_ = nullptr;
    ShortcutRegistry shortcuts_;
    bool syncingStyle_ = false;
    QString lastStyleSource_;
};

}  // namespace cd
