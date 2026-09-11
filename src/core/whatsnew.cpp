#include "include/core/whatsnew.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

#include <algorithm>

WhatsNew::WhatsNew(const QString &currentVersion, QObject *parent)
    : QObject(parent)
    , m_currentVersion(currentVersion)
{
}

bool WhatsNew::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "WhatsNew: cannot open" << path;
        return false;
    }
    return loadFromJson(file.readAll());
}

bool WhatsNew::loadFromJson(const QByteArray &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "WhatsNew: invalid JSON:" << error.errorString();
        return false;
    }

    m_releases.clear();
    const QJsonArray releases = doc.object().value(QStringLiteral("releases")).toArray();
    for (const QJsonValue &value : releases) {
        const QJsonObject release = value.toObject();
        const QString version = release.value(QStringLiteral("version")).toString();
        if (version.isEmpty()) {
            qWarning() << "WhatsNew: skipping release without a version";
            continue;
        }

        QVariantList highlights;
        const QJsonArray highlightArray = release.value(QStringLiteral("highlights")).toArray();
        for (const QJsonValue &hv : highlightArray) {
            const QJsonObject h = hv.toObject();
            if (h.value(QStringLiteral("title")).toString().isEmpty())
                continue;
            highlights.append(QVariantMap{
                { QStringLiteral("title"), h.value(QStringLiteral("title")).toString() },
                { QStringLiteral("description"), h.value(QStringLiteral("description")).toString() },
                { QStringLiteral("showMe"), h.value(QStringLiteral("showMe")).toString() },
            });
        }

        QVariantList changes;
        const QJsonArray changeArray = release.value(QStringLiteral("changes")).toArray();
        for (const QJsonValue &cv : changeArray) {
            if (!cv.toString().isEmpty())
                changes.append(cv.toString());
        }

        m_releases.append(QVariantMap{
            { QStringLiteral("version"), version },
            { QStringLiteral("highlights"), highlights },
            { QStringLiteral("changes"), changes },
        });
    }

    std::stable_sort(m_releases.begin(), m_releases.end(),
                     [](const QVariant &a, const QVariant &b) {
        return compareVersions(a.toMap().value(QStringLiteral("version")).toString(),
                               b.toMap().value(QStringLiteral("version")).toString()) > 0;
    });
    return true;
}

QVariantList WhatsNew::pendingReleases(const QString &sinceVersion) const
{
    QVariantList pending;
    for (const QVariant &release : m_releases) {
        const QString version = release.toMap().value(QStringLiteral("version")).toString();
        if (compareVersions(version, m_currentVersion) > 0)
            continue; // not shipped in this build yet
        if (!sinceVersion.isEmpty() && compareVersions(version, sinceVersion) <= 0)
            continue; // already seen
        pending.append(release);
    }
    return pending;
}

QVariantList WhatsNew::allReleases() const
{
    return m_releases;
}

int WhatsNew::compareVersions(const QString &a, const QString &b)
{
    const QStringList as = a.split('.');
    const QStringList bs = b.split('.');
    for (int i = 0; i < qMax(as.size(), bs.size()); ++i) {
        const int av = (i < as.size()) ? as[i].toInt() : 0;
        const int bv = (i < bs.size()) ? bs[i].toInt() : 0;
        if (av != bv)
            return av < bv ? -1 : 1;
    }
    return 0;
}
