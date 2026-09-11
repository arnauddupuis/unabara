#include <QtTest>

#include "include/core/whatsnew.h"

// Gating rules under test: pendingReleases(since) returns releases with
// since < version <= currentVersion, newest first; an empty since matches
// everything up to the current version; entries newer than the running
// build stay hidden until the version bump ships.
class WhatsNewTest : public QObject
{
    Q_OBJECT

private:
    static QByteArray sampleJson()
    {
        return R"({
            "releases": [
                {
                    "version": "0.2.0",
                    "highlights": [
                        {"title": "Video sync", "description": "Drag to sync.", "showMe": "video"}
                    ],
                    "changes": ["Camera profiles"]
                },
                {
                    "version": "0.3.0",
                    "highlights": [
                        {"title": "One canvas", "description": "Edit and Render.", "showMe": "canvas"},
                        {"title": "", "description": "invalid, no title"}
                    ],
                    "changes": ["Toasts", ""]
                },
                {
                    "version": "0.4.0",
                    "highlights": [
                        {"title": "Composite export", "description": "Future.", "showMe": "video"}
                    ],
                    "changes": []
                }
            ]
        })";
    }

private slots:
    void compareVersions_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("sign");

        QTest::newRow("equal") << "0.3.0" << "0.3.0" << 0;
        QTest::newRow("patch") << "0.3.1" << "0.3.0" << 1;
        QTest::newRow("minor") << "0.2.9" << "0.3.0" << -1;
        QTest::newRow("major") << "1.0.0" << "0.9.9" << 1;
        QTest::newRow("missing segments equal") << "0.3" << "0.3.0" << 0;
        QTest::newRow("missing segments less") << "0.3" << "0.3.1" << -1;
        QTest::newRow("double digit segment") << "0.10.0" << "0.9.0" << 1;
        QTest::newRow("empty vs zero") << "" << "0.0.0" << 0;
    }

    void compareVersions()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, sign);

        const int result = WhatsNew::compareVersions(a, b);
        if (sign == 0)
            QCOMPARE(result, 0);
        else
            QVERIFY(sign > 0 ? result > 0 : result < 0);
    }

    void parseAndSort()
    {
        WhatsNew wn(QStringLiteral("9.9.9"));
        QVERIFY(wn.loadFromJson(sampleJson()));

        const QVariantList all = wn.allReleases();
        QCOMPARE(all.size(), 3);
        // Newest first regardless of file order
        QCOMPARE(all[0].toMap()["version"].toString(), QStringLiteral("0.4.0"));
        QCOMPARE(all[2].toMap()["version"].toString(), QStringLiteral("0.2.0"));

        // Invalid entries are dropped: the title-less highlight and the
        // empty change line
        const QVariantMap v030 = all[1].toMap();
        QCOMPARE(v030["highlights"].toList().size(), 1);
        QCOMPARE(v030["changes"].toStringList(), QStringList{QStringLiteral("Toasts")});
    }

    void pendingNeverSeenShowsAllShipped()
    {
        WhatsNew wn(QStringLiteral("0.3.0"));
        QVERIFY(wn.loadFromJson(sampleJson()));

        // Empty since = pre-feature user: everything up to the running
        // version, but never the unreleased 0.4.0 entry
        const QVariantList pending = wn.pendingReleases(QString());
        QCOMPARE(pending.size(), 2);
        QCOMPARE(pending[0].toMap()["version"].toString(), QStringLiteral("0.3.0"));
        QCOMPARE(pending[1].toMap()["version"].toString(), QStringLiteral("0.2.0"));
    }

    void pendingAfterUpdateShowsOnlyNewer()
    {
        WhatsNew wn(QStringLiteral("0.3.0"));
        QVERIFY(wn.loadFromJson(sampleJson()));

        const QVariantList pending = wn.pendingReleases(QStringLiteral("0.2.0"));
        QCOMPARE(pending.size(), 1);
        QCOMPARE(pending[0].toMap()["version"].toString(), QStringLiteral("0.3.0"));
    }

    void pendingUpToDateShowsNothing()
    {
        WhatsNew wn(QStringLiteral("0.3.0"));
        QVERIFY(wn.loadFromJson(sampleJson()));

        QVERIFY(wn.pendingReleases(QStringLiteral("0.3.0")).isEmpty());
        // A downgrade (seen version newer than the build) must not re-show
        // old notes either
        QVERIFY(wn.pendingReleases(QStringLiteral("0.4.0")).isEmpty());
    }

    void entriesAheadOfBuildStayHidden()
    {
        // Pre-release state: the JSON already carries 0.3.0 notes but
        // VERSION.md still says 0.2.0 — nothing may pop up yet
        WhatsNew wn(QStringLiteral("0.2.0"));
        QVERIFY(wn.loadFromJson(sampleJson()));

        QVERIFY(wn.pendingReleases(QStringLiteral("0.2.0")).isEmpty());
        QCOMPARE(wn.pendingReleases(QString()).size(), 1); // only 0.2.0 itself
    }

    void invalidJsonRejected()
    {
        WhatsNew wn(QStringLiteral("0.3.0"));
        QVERIFY(!wn.loadFromJson("not json"));
        QVERIFY(wn.allReleases().isEmpty());
    }

    void bundledFileParses()
    {
        // The real resource file must always load and contain only valid,
        // fully-formed entries
        WhatsNew wn(QStringLiteral("999.0.0"));
        QVERIFY(wn.loadFromFile(QStringLiteral(WHATSNEW_JSON_PATH)));
        const QVariantList all = wn.allReleases();
        QVERIFY(!all.isEmpty());
        for (const QVariant &release : all) {
            const QVariantMap map = release.toMap();
            QVERIFY(!map["version"].toString().isEmpty());
            QVERIFY(!map["highlights"].toList().isEmpty() || !map["changes"].toList().isEmpty());
        }
    }
};

QTEST_GUILESS_MAIN(WhatsNewTest)
#include "whatsnew_test.moc"
