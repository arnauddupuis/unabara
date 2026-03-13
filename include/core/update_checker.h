#ifndef UPDATE_CHECKER_H
#define UPDATE_CHECKER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    Q_INVOKABLE void checkForUpdates();

signals:
    void updateAvailable(const QString &latestVersion, const QString &releaseUrl);

private slots:
    void onReplyFinished(QNetworkReply *reply);

private:
    static bool isNewerVersion(const QString &remote, const QString &local);

    QNetworkAccessManager m_networkManager;
    QString m_currentVersion;
    static const QString VERSION_URL;
    static const QString RELEASE_URL;
};

#endif // UPDATE_CHECKER_H
