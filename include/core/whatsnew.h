#ifndef WHATSNEW_H
#define WHATSNEW_H

#include <QObject>
#include <QString>
#include <QVariantList>

/**
 * Release-notes model for the in-app "What's New" dialog.
 *
 * Content ships in resources as whatsnew.json: a list of releases, each
 * carrying user-facing "highlights" (title, description, optional showMe
 * navigation target) and minor "changes" lines. The dialog composes itself
 * from the releases newer than the version the user last saw, so writing
 * the JSON entry for a release is the only per-release work.
 *
 * Version gating: pendingReleases() returns entries with
 * lastSeen < version <= current. Entries for versions newer than the
 * running build stay hidden until the VERSION.md bump ships (the bump is
 * merged last on purpose — see the release process), which arms the
 * startup popup exactly once per user per release.
 */
class WhatsNew : public QObject
{
    Q_OBJECT

public:
    explicit WhatsNew(const QString &currentVersion, QObject *parent = nullptr);

    bool loadFromFile(const QString &path);
    bool loadFromJson(const QByteArray &json);

    // Releases with sinceVersion < version <= currentVersion, newest first.
    // An empty sinceVersion means "never seen anything" and matches all
    // releases up to the current version.
    Q_INVOKABLE QVariantList pendingReleases(const QString &sinceVersion) const;

    // Every release in the file, newest first (the full changelog shown by
    // the manual "Show What's New" entry point).
    Q_INVOKABLE QVariantList allReleases() const;

    QString currentVersion() const { return m_currentVersion; }

    // Numeric dotted-version comparison: <0 if a<b, 0 if equal, >0 if a>b.
    // Missing segments count as 0 ("0.3" == "0.3.0").
    static int compareVersions(const QString &a, const QString &b);

private:
    QString m_currentVersion;
    QVariantList m_releases; // QVariantMaps sorted newest first
};

#endif // WHATSNEW_H
