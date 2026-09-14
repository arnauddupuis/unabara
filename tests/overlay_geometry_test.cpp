// Tests for OverlayGenerator's v1.2 cell geometry: anchor-resolved boxes, the
// invariant that changing alignment or freezing the size never moves a cell,
// hit-testing against those boxes, and the selection/visibility bookkeeping
// the inspector relies on.
//
// The generator is constructed without the app's resource bundle, so no
// template image loads and it falls back to its 640x120 default canvas.

#include <QtTest>

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtMath>

#include "include/core/dive_data.h"
#include "include/core/overlay_template.h"
#include "include/generators/overlay_gen.h"

using Unabara::CellData;
using Unabara::CellType;
using Unabara::HAlign;
using Unabara::OverlayTemplate;
using Unabara::VAlign;

namespace {

OverlayTemplate makeTemplate(const QVector<CellData> &cells)
{
    OverlayTemplate templ;
    templ.setTemplateName(QStringLiteral("Geometry"));
    templ.setBackgroundImagePath(QStringLiteral(":/does-not-exist.png"));
    templ.setDefaultFont(QFont(QStringLiteral("DejaVu Sans"), 12));
    for (const CellData &cell : cells)
        templ.addCell(cell);
    return templ;
}

CellData depthCell(const QString &id, double x, double y)
{
    CellData cell(id, CellType::Depth);
    cell.setPosition(QPointF(x, y));
    cell.setVisible(true);
    return cell;
}

QRectF boxOf(const OverlayGenerator &gen, DiveData *dive, double t, const QString &id)
{
    const auto rects = gen.cellRects(dive, t);
    for (const auto &r : rects) {
        if (r.first == id)
            return r.second;
    }
    return QRectF();
}

// Re-anchoring goes through a normalized position and qFloor, so allow one
// pixel of rounding slack
bool sameBox(const QRectF &a, const QRectF &b)
{
    return qAbs(a.x() - b.x()) <= 1.0 && qAbs(a.y() - b.y()) <= 1.0
        && qAbs(a.width() - b.width()) <= 1.0 && qAbs(a.height() - b.height()) <= 1.0;
}

} // namespace

class OverlayGeometryTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_settingsDir;
    DiveData *m_dive = nullptr;
    const double m_t = 120.0;

private slots:
    void initTestCase()
    {
        // OverlayGenerator reads Config (QSettings) in its constructor; keep
        // the test away from the developer's real settings file. Config uses
        // the QSettings(org, app) constructor, i.e. NativeFormat: on Linux
        // that is IniFormat and setPath() redirects it into the temp dir,
        // but on Windows (registry) and macOS (plists) Qt ignores setPath()
        // for NativeFormat — there is no way to redirect those, so rather
        // than silently touching the developer's real settings, skip.
#if !defined(Q_OS_LINUX)
        QSKIP("Config's NativeFormat QSettings (registry/plist) cannot be redirected off Linux");
#endif
        QVERIFY(m_settingsDir.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());

        m_dive = new DiveData(this);
        for (int t = 0; t <= 600; t += 10)
            m_dive->addDataPoint(DiveDataPoint(t, 18.0 + (t % 60) / 10.0, 22.0, 40.0));
    }

    void legacyAnchorIsTheTopLeftCorner()
    {
        OverlayGenerator gen;
        gen.loadTemplate(makeTemplate({depthCell(QStringLiteral("depth"), 0.25, 0.5)}));

        const QRectF box = boxOf(gen, m_dive, m_t, QStringLiteral("depth"));
        QVERIFY(!box.isNull());
        // Pre-1.2 rendering: position is the box's top-left, truncated like
        // the old static_cast<int>
        QCOMPARE(box.x(), double(qFloor(0.25 * gen.templateWidth())));
        QCOMPARE(box.y(), double(qFloor(0.5 * gen.templateHeight())));
        // Auto-size: measured text plus 8 px padding
        QVERIFY(box.width() > 8.0);
        QVERIFY(box.height() > 8.0);
    }

    void alignmentShiftsTheBoxByItsOwnSize()
    {
        CellData left = depthCell(QStringLiteral("l"), 0.5, 0.5);
        CellData center = depthCell(QStringLiteral("c"), 0.5, 0.5);
        center.setHAlign(HAlign::Center);
        center.setVAlign(VAlign::Middle);
        CellData right = depthCell(QStringLiteral("r"), 0.5, 0.5);
        right.setHAlign(HAlign::Right);
        right.setVAlign(VAlign::Bottom);

        OverlayGenerator gen;
        gen.loadTemplate(makeTemplate({left, center, right}));

        const QRectF l = boxOf(gen, m_dive, m_t, QStringLiteral("l"));
        const QRectF c = boxOf(gen, m_dive, m_t, QStringLiteral("c"));
        const QRectF r = boxOf(gen, m_dive, m_t, QStringLiteral("r"));

        // Same content → same size whatever the anchor
        QCOMPARE(c.size(), l.size());
        QCOMPARE(r.size(), l.size());
        QVERIFY(qAbs((l.x() - c.x()) - l.width() / 2.0) <= 1.0);
        QVERIFY(qAbs((l.y() - c.y()) - l.height() / 2.0) <= 1.0);
        QVERIFY(qAbs((l.x() - r.x()) - l.width()) <= 1.0);
        QVERIFY(qAbs((l.y() - r.y()) - l.height()) <= 1.0);
    }

    void fixedSizeOverridesTheMeasuredSize()
    {
        CellData cell = depthCell(QStringLiteral("depth"), 0.1, 0.1);
        cell.setFixedSize(QSizeF(0.5, 0.25));

        OverlayGenerator gen;
        gen.loadTemplate(makeTemplate({cell}));

        const QRectF box = boxOf(gen, m_dive, m_t, QStringLiteral("depth"));
        QCOMPARE(box.width(), 0.5 * gen.templateWidth());
        QCOMPARE(box.height(), 0.25 * gen.templateHeight());
    }

    void changingAlignmentKeepsTheBoxInPlace()
    {
        OverlayGenerator gen;
        gen.loadTemplate(makeTemplate({depthCell(QStringLiteral("depth"), 0.3, 0.4)}));
        const QString id = QStringLiteral("depth");
        const QRectF before = boxOf(gen, m_dive, m_t, id);

        gen.setCellHAlign(id, int(HAlign::Center), m_dive, m_t);
        QCOMPARE(gen.getCellHAlign(id), int(HAlign::Center));
        QVERIFY2(sameBox(boxOf(gen, m_dive, m_t, id), before), "center anchor moved the cell");

        gen.setCellVAlign(id, int(VAlign::Bottom), m_dive, m_t);
        QVERIFY2(sameBox(boxOf(gen, m_dive, m_t, id), before), "bottom anchor moved the cell");

        gen.setCellHAlign(id, int(HAlign::Right), m_dive, m_t);
        QVERIFY2(sameBox(boxOf(gen, m_dive, m_t, id), before), "right anchor moved the cell");

        gen.setCellHAlign(id, int(HAlign::Left), m_dive, m_t);
        gen.setCellVAlign(id, int(VAlign::Top), m_dive, m_t);
        QVERIFY2(sameBox(boxOf(gen, m_dive, m_t, id), before), "back to legacy anchor moved the cell");

        // Out-of-range values clamp to the enum instead of corrupting state
        gen.setCellHAlign(id, 99, m_dive, m_t);
        QCOMPARE(gen.getCellHAlign(id), int(HAlign::Right));
    }

    void freezingTheSizeKeepsTheBoxInPlace()
    {
        OverlayGenerator gen;
        gen.loadTemplate(makeTemplate({depthCell(QStringLiteral("depth"), 0.3, 0.4)}));
        const QString id = QStringLiteral("depth");
        const QRectF before = boxOf(gen, m_dive, m_t, id);

        // Without a dive there is nothing to measure: the request is declined
        // and the cell stays auto-sized (the QML re-syncs its checkbox)
        QSignalSpy layoutSpy(&gen, &OverlayGenerator::cellLayoutChanged);
        gen.setCellAutoSize(id, false, nullptr, m_t);
        QVERIFY(!gen.getCellHasFixedSize(id));
        QCOMPARE(layoutSpy.count(), 0);

        gen.setCellAutoSize(id, false, m_dive, m_t);
        QVERIFY(gen.getCellHasFixedSize(id));
        QVERIFY2(sameBox(boxOf(gen, m_dive, m_t, id), before), "freezing the size moved the cell");

        gen.setCellAutoSize(id, true, m_dive, m_t);
        QVERIFY(!gen.getCellHasFixedSize(id));
        QVERIFY(sameBox(boxOf(gen, m_dive, m_t, id), before));
    }

    void hitTestMatchesTheBoxesTopmostFirst()
    {
        // Two cells at the same spot: the later one paints on top and wins
        OverlayGenerator gen;
        gen.loadTemplate(makeTemplate({depthCell(QStringLiteral("under"), 0.2, 0.2),
                                       depthCell(QStringLiteral("over"), 0.2, 0.2)}));

        const QRectF box = boxOf(gen, m_dive, m_t, QStringLiteral("over"));
        const QPointF center(box.center().x() / gen.templateWidth(),
                             box.center().y() / gen.templateHeight());
        QCOMPARE(gen.cellIdAt(m_dive, m_t, center), QStringLiteral("over"));

        // Hidden cells are not hit
        gen.setCellVisible(QStringLiteral("over"), false);
        QCOMPARE(gen.cellIdAt(m_dive, m_t, center), QStringLiteral("under"));

        // Empty space and a null dive yield no id
        QCOMPARE(gen.cellIdAt(m_dive, m_t, QPointF(0.99, 0.99)), QString());
        QCOMPARE(gen.cellIdAt(nullptr, m_t, center), QString());
    }

    void selectionIsDroppedWhenItsCellDisappears()
    {
        OverlayGenerator gen;
        gen.loadTemplate(makeTemplate({depthCell(QStringLiteral("depth"), 0.2, 0.2),
                                       depthCell(QStringLiteral("other"), 0.6, 0.6)}));
        const QString id = QStringLiteral("depth");

        // Hiding the selected cell clears the selection
        gen.setSelectedCellId(id);
        gen.setCellVisible(id, false);
        QVERIFY(gen.selectedCellId().isEmpty());

        // Hiding a different cell leaves it alone
        gen.setCellVisible(id, true);
        gen.setSelectedCellId(id);
        gen.setCellTypeVisible(QStringLiteral("other"), false);
        QCOMPARE(gen.selectedCellId(), id);

        // A template without the selected cell clears it
        gen.loadTemplate(makeTemplate({depthCell(QStringLiteral("other"), 0.6, 0.6)}));
        QVERIFY(gen.selectedCellId().isEmpty());
    }

    void unchangedVisibilityEmitsNothing()
    {
        // loadTemplate() fires every show*Changed and the canvas answers each
        // with setCellTypeVisible(); a no-op must not cost a layout refresh
        OverlayGenerator gen;
        gen.loadTemplate(makeTemplate({depthCell(QStringLiteral("depth"), 0.2, 0.2)}));

        QSignalSpy layoutSpy(&gen, &OverlayGenerator::cellLayoutChanged);
        gen.setCellTypeVisible(QStringLiteral("depth"), true);
        gen.setCellVisible(QStringLiteral("depth"), true);
        QCOMPARE(layoutSpy.count(), 0);

        gen.setCellTypeVisible(QStringLiteral("depth"), false);
        QCOMPARE(layoutSpy.count(), 1);
    }
};

QTEST_MAIN(OverlayGeometryTest)
#include "overlay_geometry_test.moc"
