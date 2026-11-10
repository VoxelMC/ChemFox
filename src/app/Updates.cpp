#include "app/Updates.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QUrl>
#include <QWidget>

namespace cd::updates {

Checker::Checker(QString repoUrl, QString current, QWidget* parent)
    : QObject(parent), repo_(std::move(repoUrl)), current_(std::move(current)), parent_(parent) {
    while (repo_.endsWith('/')) repo_.chop(1);
}

void Checker::checkIfDue() {
    QSettings s;
    if (!s.value("updates/auto", true).toBool() || repo_.isEmpty()) return;
    QDateTime last = s.value("updates/lastCheck").toDateTime();
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < 20 * 3600) return;
    check(false);
}

void Checker::check(bool manual) {
    if (busy_) return;
    // https://github.com/owner/repo -> https://api.github.com/repos/owner/repo/releases
    const QString path = QUrl(repo_).path();
    if (repo_.isEmpty() || path.count('/') < 2) {
        if (manual)
            QMessageBox::information(parent_, "Check for Updates",
                                     "This build doesn't know where its releases are published, so it can't check.");
        return;
    }
    if (!net_) net_ = new QNetworkAccessManager(this);
    QNetworkRequest req(QUrl("https://api.github.com/repos" + path + "/releases?per_page=20"));
    req.setHeader(QNetworkRequest::UserAgentHeader, "ChemFox/" + current_);  // GitHub requires one; nothing else is sent
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    busy_ = true;
    QNetworkReply* reply = net_->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        busy_ = false;
        reply->deleteLater();
        QSettings s;
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (manual)
                QMessageBox::warning(parent_, "Check for Updates",
                                     status == 404 ? "GitHub has no public releases for ChemFox to check against "
                                                     "(the repository may be private)."
                                                   : "Couldn't reach GitHub to check for updates.\n\n" + reply->errorString());
            return;
        }
        s.setValue("updates/lastCheck", QDateTime::currentDateTimeUtc());
        Release r = newestRelease(QJsonDocument::fromJson(reply->readAll()).array());
        const bool newer = !r.tag.isEmpty() && compareVersions(r.tag, current_) > 0;
        if (!newer) {
            if (manual)
                QMessageBox::information(parent_, "Check for Updates",
                                         QString("You're up to date: ChemFox %1 is the newest version.").arg(current_));
            return;
        }
        if (!manual && s.value("updates/skip").toString() == r.tag) return;

        const QString version = QString(r.tag).remove(QRegularExpression("^v"));
        QMessageBox box(parent_);
        box.setWindowTitle("ChemFox Update");
        box.setIcon(QMessageBox::Information);
        box.setText(QString("<b>ChemFox %1 is available.</b> You have %2.").arg(version, current_));
        box.setInformativeText("Download it from the release page and install it over this version; your settings are kept.");
        if (!r.notes.isEmpty()) box.setDetailedText(r.notes);
        QPushButton* download = box.addButton("Download", QMessageBox::AcceptRole);
        box.addButton("Later", QMessageBox::RejectRole);
        QPushButton* skip = manual ? nullptr : box.addButton("Skip This Version", QMessageBox::DestructiveRole);
        box.setDefaultButton(download);
        box.exec();
        if (box.clickedButton() == download) QDesktopServices::openUrl(QUrl(r.url.isEmpty() ? repo_ + "/releases" : r.url));
        else if (skip && box.clickedButton() == skip) s.setValue("updates/skip", r.tag);
    });
}

}  // namespace cd::updates
