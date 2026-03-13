#include "include/core/update_checker.h"
#include "version.h"

#include <QNetworkReply>
#include <QStringList>

const QString UpdateChecker::VERSION_URL =
    QStringLiteral("https://raw.githubusercontent.com/arnauddupuis/unabara/refs/heads/main/VERSION.md");
const QString UpdateChecker::RELEASE_URL =
    QStringLiteral("https://github.com/arnauddupuis/unabara/releases");

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_currentVersion(UNABARA_VERSION_STR)
{
    connect(&m_networkManager, &QNetworkAccessManager::finished,
            this, &UpdateChecker::onReplyFinished);
}

void UpdateChecker::checkForUpdates()
{
    QNetworkRequest request(VERSION_URL);
    m_networkManager.get(request);
}

void UpdateChecker::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        return;
    }

    QString remoteVersion = QString::fromUtf8(reply->readAll()).trimmed();

    if (isNewerVersion(remoteVersion, m_currentVersion)) {
        emit updateAvailable(remoteVersion, RELEASE_URL);
    }
}

bool UpdateChecker::isNewerVersion(const QString &remote, const QString &local)
{
    QStringList r = remote.split('.');
    QStringList l = local.split('.');

    for (int i = 0; i < qMax(r.size(), l.size()); ++i) {
        int rv = (i < r.size()) ? r[i].toInt() : 0;
        int lv = (i < l.size()) ? l[i].toInt() : 0;
        if (rv > lv) return true;
        if (rv < lv) return false;
    }
    return false;
}
