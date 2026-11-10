#pragma once
// One offscreen QApplication shared by all tests that need fonts or widgets.

#include <QApplication>

inline QApplication& testApp() {
    static int argc = 1;
    static char name[] = "chemfox_tests";
    static char* argv[] = {name, nullptr};
    static bool env = (qputenv("QT_QPA_PLATFORM", "offscreen"),
#ifdef Q_OS_WIN
                       qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts"),  // the offscreen platform finds no fonts otherwise
#endif
                       true);
    (void)env;
    static QApplication app(argc, argv);
    return app;
}
