// render_utp — headless template renderer for authoring bundled templates.
//
// Renders a .utp template over a synthetic dive data point and writes the
// resulting overlay PNG, without launching the full application. Also offers
// a --measure mode that prints the exact pixel bounds a text block will
// occupy at render time (same font math as OverlayGenerator).
//
// Usage:
//   render_utp <template.utp|:/templates/X.utp> <out.png> [--time <seconds>]
//   render_utp --measure <fontFamily> <pointSize> <text>   ("\n" splits lines)
//
// Build with -DUNABARA_BUILD_TOOLS=ON. Runs offscreen; no display needed.

#include <QGuiApplication>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFontInfo>
#include <QImage>
#include <QTimeZone>
#include <QDebug>

#include "include/core/dive_data.h"
#include "include/generators/overlay_gen.h"

namespace {

// Synthetic dive exercising every cell type: OC data, two cylinders with a
// gas switch, CCR PO2 sensors, CNS, and a deco phase (ndl == 0, ceiling,
// stop time) between t=2000 and t=3000.
DiveData* makeSyntheticDive()
{
    auto* dive = new DiveData();
    dive->setDiveName(QStringLiteral("Synthetic Render Dive"));
    dive->setStartTime(QDateTime(QDate(2026, 1, 15), QTime(10, 0, 0), QTimeZone::utc()));
    dive->setMeanDepth(14.2);
    dive->setDiveMode(DiveData::ClosedCircuit);

    CylinderInfo c0;
    c0.index = 0;
    c0.description = QStringLiteral("AL80");
    c0.o2Percent = 32.0;
    c0.startPressure = 200.0;
    c0.endPressure = 60.0;
    dive->addCylinder(c0);

    CylinderInfo c1;
    c1.index = 1;
    c1.description = QStringLiteral("D12");
    c1.o2Percent = 21.0;
    c1.hePercent = 35.0;
    c1.startPressure = 200.0;
    c1.endPressure = 150.0;
    dive->addCylinder(c1);

    dive->addGasSwitch(0.0, 0);

    struct Sample {
        double t, depth, temp, ndl, ceiling, tts, cns, stop, p0, p1, po2;
    };
    const Sample samples[] = {
        //  t   depth temp  ndl ceil tts  cns stop   p0   p1   po2
        {   0.0,  0.0, 26.0, 99, 0.0,  0,  1.0, 0.0, 200, 200, 0.21 },
        { 600.0, 12.3, 25.0, 45, 0.0,  0,  4.0, 0.0, 182, 200, 1.05 },
        {1200.0, 18.4, 24.0, 12, 0.0,  0,  8.0, 0.0, 165, 200, 1.21 },
        {2000.0, 32.6, 19.5,  0, 6.0, 12, 15.0, 2.0, 130, 195, 1.28 },
        {2400.0, 30.1, 19.0,  0, 6.0,  8, 18.0, 3.4,  95, 180, 1.26 },
        {3000.0, 12.0, 21.0,  0, 3.0,  4, 20.0, 1.2,  80, 165, 1.15 },
        {3600.0,  4.8, 23.0, 99, 0.0,  0, 21.0, 0.0,  65, 150, 0.90 },
    };
    for (const Sample& s : samples) {
        DiveDataPoint p(s.t, s.depth, s.temp, s.ndl, s.ceiling, 32.0, s.tts);
        p.cns = s.cns;
        p.stopTime = s.stop;
        p.addPressure(s.p0, 0);
        p.addPressure(s.p1, 1);
        // Three slightly divergent sensors so CELL 1/2/3 are distinguishable
        p.addPO2Sensor(s.po2, 0);
        p.addPO2Sensor(s.po2 - 0.02, 1);
        p.addPO2Sensor(s.po2 + 0.02, 2);
        dive->addDataPoint(p);
    }
    return dive;
}

// Mirrors OverlayGenerator: pixelSize = int(pointSize * 1.33 * 1.8), then
// QFontMetrics::boundingRect(0,0,1000,1000, AlignHCenter|TextWordWrap).
// Cell footprint on canvas = bounds + 8 px in each dimension.
int runMeasure(const QString& family, int pointSize, QString text)
{
    text.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    QFont font(family, pointSize);
    font.setPixelSize(static_cast<int>(pointSize * 1.33 * 1.8));
    QFontMetrics fm(font);
    const QRect bounds = fm.boundingRect(QRect(0, 0, 1000, 1000),
                                         Qt::AlignHCenter | Qt::TextWordWrap, text);
    printf("text_w=%d text_h=%d cell_w=%d cell_h=%d line_spacing=%d family_resolved=%s\n",
           bounds.width(), bounds.height(),
           bounds.width() + 8, bounds.height() + 8,
           fm.lineSpacing(),
           QFontInfo(font).family().toUtf8().constData());
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    // Distinct app identity: Config::instance() reads QSettings, and this tool
    // must never touch (or depend on) the real application's settings.
    app.setOrganizationName(QStringLiteral("UnabaraProject"));
    app.setApplicationName(QStringLiteral("UnabaraRenderTool"));

    for (const char* fontPath : {":/fonts/Orbitron.ttf", ":/fonts/ShareTechMono-Regular.ttf"}) {
        if (QFontDatabase::addApplicationFont(QLatin1String(fontPath)) == -1)
            qWarning() << "Failed to register bundled font" << fontPath;
    }

    const QStringList args = app.arguments();

    if (args.size() == 5 && args[1] == QStringLiteral("--measure"))
        return runMeasure(args[2], args[3].toInt(), args[4]);

    if (args.size() < 3) {
        fprintf(stderr,
                "Usage:\n"
                "  %s <template.utp> <out.png> [--time <seconds>]\n"
                "  %s --measure <fontFamily> <pointSize> <text>\n",
                argv[0], argv[0]);
        return 2;
    }

    double timePoint = 1200.0; // mid-dive; use --time 2400 for the deco phase
    const int timeIdx = args.indexOf(QStringLiteral("--time"));
    if (timeIdx > 0 && timeIdx + 1 < args.size())
        timePoint = args[timeIdx + 1].toDouble();

    OverlayGenerator generator;
    if (!generator.loadTemplateFromFile(args[1])) {
        fprintf(stderr, "Failed to load template: %s\n", qPrintable(args[1]));
        return 1;
    }

    DiveData* dive = makeSyntheticDive();
    // Same path as the real exporters: beginExport() disables the editor-only
    // cell backgrounds so the output matches what users actually export.
    generator.beginExport();
    const QImage overlay = generator.generateOverlay(dive, timePoint);
    generator.endExport();
    if (overlay.isNull() || !overlay.save(args[2], "PNG")) {
        fprintf(stderr, "Failed to render/save overlay to %s\n", qPrintable(args[2]));
        return 1;
    }
    printf("%s: %dx%d t=%.0fs -> %s\n", qPrintable(args[1]),
           overlay.width(), overlay.height(), timePoint, qPrintable(args[2]));
    return 0;
}
