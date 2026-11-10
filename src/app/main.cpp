#include "app/MainWindow.h"
#include "core/chem/Chem.h"
#include "core/commands/Edits.h"
#include "editor/Groups.h"
#include "io/Emf.h"
#include "io/Export.h"
#include "io/Native.h"
#include "render/Layout.h"

#include "version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>
#include <functional>
#include <cstdio>

namespace {

// Headless figure export: ChemFox --export in out [--preset s] [--dpi n] [--arrange]
int exportCli(const QCommandLineParser& cli) {
    const QStringList pos = cli.positionalArguments();
    if (pos.size() != 2) {
        std::fprintf(stderr, "usage: ChemFox --export <input> <output.svg|pdf|png|mol>\n");
        return 2;
    }
    const QString in = pos[0], out = pos[1];
    QString err;
    cd::Document doc;
    QString lower = in.toLower();
    if (lower.startsWith("smiles:") || lower.startsWith("inchi=")) {
        auto mols = cd::chem::fromText(in.startsWith("smiles:", Qt::CaseInsensitive) ? in.mid(7) : in, &err);
        cd::edits::insertMolecules(doc, mols, {0, 0});
    } else if (lower.endsWith(".mol") || lower.endsWith(".sdf") || lower.endsWith(".smi")) {
        cd::edits::insertMolecules(doc, cd::io::importFile(in, &err), {0, 0});
    } else if (auto d = cd::io::loadFile(in, &err)) {
        doc = *d;
    }
    if (doc.empty()) {
        std::fprintf(stderr, "could not read %s: %s\n", qPrintable(in), qPrintable(err));
        return 1;
    }
    if (cli.isSet("preset")) { doc.styleName = cli.value("preset"); doc.styleText.clear(); }
    cd::Style style = doc.styleText.isEmpty() ? cd::styles::resolveNamed(doc.styleName) : cd::styles::resolve(doc.styleText);
    cd::Renderer r(style);
    cd::ChemOverlay ov;
    for (const auto& m : doc.molecules) {
        auto an = cd::chem::analyze(m);
        for (auto it = an.implicitH.begin(); it != an.implicitH.end(); ++it) ov.implicitH[it.key()] = it.value();
        ov.aromaticRings[m.id] = an.aromaticRings;
        for (auto c = an.cip.begin(); c != an.cip.end(); ++c) ov.cip[c.key()] = c.value();
    }
    // Like the editor, arrange files whose coordinates were generated (SMILES-only molecules).
    if (cli.isSet("arrange") || (doc.needsLayout && !doc.arrows.empty())) {
        if (doc.schemes.empty()) cd::edits::buildScheme(doc);
        else cd::edits::renumber(doc);
        cd::layout::layoutSchemes(doc, r, ov);
    }
    bool ok = false;
    QString ext = QFileInfo(out).suffix().toLower();
    if (ext == "svg") ok = cd::io::exportSvg(out, doc, r, ov, &err);
    else if (ext == "pdf") ok = cd::io::exportPdf(out, doc, r, ov, &err);
    else if (ext == "png") ok = cd::io::exportPng(out, doc, r, ov, cli.value("dpi").toDouble(), false, &err);
    else if (ext == "emf") ok = cd::io::exportEmf(out, doc, r, ov, &err);
    else if (ext == "mol" || ext == "sdf") ok = cd::io::exportMol(out, doc, &err);
    else if (ext == "cmfx" || ext == "chemfox" || ext == "chemdoc" || ext == "json") ok = cd::io::saveFile(out, doc, &err);
    else err = "unknown output type ." + ext;
    if (!ok) std::fprintf(stderr, "export failed: %s\n", qPrintable(err));
    return ok ? 0 : 1;
}

// Settings, keymap, styles and shortcuts saved under the old working name move across once.
void migrateFromChemdoc() {
    QSettings now;
    if (now.value("migratedFromChemdoc").toBool()) return;
    QSettings old("chemdoc", "chemdoc");
    for (const auto& key : old.allKeys())
        if (!now.contains(key)) now.setValue(key, old.value(key));
    const QString newDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString oldDir = newDir;
    oldDir.replace("ChemFox/ChemFox", "chemdoc/chemdoc");
    if (oldDir != newDir && QDir(oldDir).exists()) {
        QDirIterator it(oldDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString from = it.next();
            QString to = newDir + from.mid(oldDir.size());
            QDir().mkpath(QFileInfo(to).absolutePath());
            if (!QFile::exists(to)) QFile::copy(from, to);
        }
    }
    now.setValue("migratedFromChemdoc", true);
}

// macOS opens documents (double-click in Finder, drop on the Dock icon) with an event, not argv.
class ChemFoxApp : public QApplication {
public:
    using QApplication::QApplication;
    std::function<void(const QString&)> openFile;
    QStringList pending;  // files that arrived before the window existed

protected:
    bool event(QEvent* e) override {
        if (e->type() == QEvent::FileOpen) {
            QString path = static_cast<QFileOpenEvent*>(e)->file();
            if (openFile) openFile(path);
            else pending << path;
            return true;
        }
        return QApplication::event(e);
    }
};

}  // namespace

int main(int argc, char** argv) {
    ChemFoxApp app(argc, argv);
    QApplication::setOrganizationName("ChemFox");
    QApplication::setApplicationName("ChemFox");
    QApplication::setApplicationVersion(CHEMFOX_VERSION);
    QApplication::setWindowIcon(QIcon(":/icons/chemfox.png"));
    migrateFromChemdoc();
    cd::groups::load();  // custom groups, before anything reads a label

    QCommandLineParser cli;
    cli.addPositionalArgument("file", "Document or structure file to open");
    cli.addOption({"export", "Export the input to the output file and exit."});
    cli.addOption({"preset", "Style for --export (default, acs, slides, poster, dark or a user style).", "name"});
    cli.addOption({"dpi", "PNG resolution for --export.", "dpi", "300"});
    cli.addOption({"arrange", "Arrange the reaction scheme before exporting."});
    cli.process(app);
    if (cli.isSet("export")) return exportCli(cli);

    cd::MainWindow w;
    w.show();
    app.openFile = [&w](const QString& path) { w.openPath(path); };
    for (const auto& path : app.pending) w.openPath(path);
    if (!cli.positionalArguments().isEmpty()) w.openPath(cli.positionalArguments().first());
    else if (app.pending.isEmpty() && !w.offerRecovery()) w.reopenLastFile();  // only if Preferences asks for it
    return app.exec();
}
