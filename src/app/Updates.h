#pragma once
// Checking GitHub for a newer ChemFox: Help → Check for Updates, and a quiet daily check at startup that can be
// turned off. The only network request ChemFox makes. The comparison helpers are inline so tests can use them
// without the network.

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <algorithm>

class QNetworkAccessManager;
class QWidget;

namespace cd::updates {

// Semantic-version comparison of "0.2.1", "v1.0.0-beta.2" etc.: <0, 0 or >0. A pre-release sorts before its
// release (1.0.0-beta < 1.0.0); pre-release labels compare by dot-separated parts, numbers numerically.
inline int compareVersions(QString a, QString b) {
    auto strip = [](QString v) { v = v.trimmed(); if (v.startsWith('v') || v.startsWith('V')) v.remove(0, 1); return v; };
    a = strip(a);
    b = strip(b);
    const QString preA = a.contains('-') ? a.section('-', 1) : QString(), preB = b.contains('-') ? b.section('-', 1) : QString();
    const QStringList na = a.section('-', 0, 0).split('.'), nb = b.section('-', 0, 0).split('.');
    for (int i = 0; i < 3; ++i) {
        int x = i < na.size() ? na[i].toInt() : 0, y = i < nb.size() ? nb[i].toInt() : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (preA.isEmpty() != preB.isEmpty()) return preA.isEmpty() ? 1 : -1;
    const QStringList pa = preA.split('.', Qt::SkipEmptyParts), pb = preB.split('.', Qt::SkipEmptyParts);
    for (int i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        if (i >= pa.size()) return -1;
        if (i >= pb.size()) return 1;
        bool okA = false, okB = false;
        int x = pa[i].toInt(&okA), y = pb[i].toInt(&okB);
        if (okA && okB) { if (x != y) return x < y ? -1 : 1; }
        else if (okA != okB) return okA ? -1 : 1;
        else if (int c = QString::compare(pa[i], pb[i]); c != 0) return c < 0 ? -1 : 1;
    }
    return 0;
}

struct Release {
    QString tag, name, url, notes;
};

// The newest published release in a GitHub /releases response (drafts skipped; pre-releases count, since
// ChemFox's betas are pre-releases). Empty tag if none.
inline Release newestRelease(const QJsonArray& list) {
    Release best;
    for (const auto& v : list) {
        QJsonObject o = v.toObject();
        if (o.value("draft").toBool()) continue;
        QString tag = o.value("tag_name").toString();
        if (!QRegularExpression("^v?\\d+\\.\\d+").match(tag).hasMatch()) continue;
        if (best.tag.isEmpty() || compareVersions(tag, best.tag) > 0)
            best = {tag, o.value("name").toString(), o.value("html_url").toString(), o.value("body").toString()};
    }
    return best;
}

class Checker : public QObject {
    Q_OBJECT
public:
    // repoUrl: https://github.com/<owner>/<repo>; current: this build's version.
    Checker(QString repoUrl, QString current, QWidget* parent);
    // Ask GitHub now. Manual checks always report back; automatic ones only when there's something new
    // (and not a version the user chose to skip).
    void check(bool manual);
    // The daily startup check, if it's on and due.
    void checkIfDue();

private:
    QString repo_, current_;
    QWidget* parent_;
    QNetworkAccessManager* net_ = nullptr;
    bool busy_ = false;
};

}  // namespace cd::updates
