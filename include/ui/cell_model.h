#ifndef CELL_MODEL_H
#define CELL_MODEL_H

#include <QAbstractListModel>
#include <QVector>
#include "include/core/cell_data.h"
#include "include/core/dive_data.h"
#include "include/generators/overlay_gen.h"

/**
 * @brief QML model for overlay cells
 *
 * Exposes cell data from OverlayGenerator to QML for use in Repeater/ListView.
 * Provides bidirectional sync between QML and the generator's cell data.
 */
class CellModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum CellRoles {
        CellIdRole = Qt::UserRole + 1,
        CellTypeRole,
        PositionRole,
        VisibleRole,
        FontRole,
        TextColorRole,
        DisplayTextRole,
        CalculatedSizeRole,
        HasCustomFontRole,
        HasCustomColorRole,
        TankIndexRole
    };

    explicit CellModel(QObject *parent = nullptr);

    // QAbstractListModel interface
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Cell management
    Q_INVOKABLE void updateFromGenerator(OverlayGenerator* generator, DiveData* dive, double timePoint);
    Q_INVOKABLE void updateCellPosition(const QString& cellId, const QPointF& position);
    Q_INVOKABLE void updateCellFont(const QString& cellId, const QFont& font);
    Q_INVOKABLE void updateCellColor(const QString& cellId, const QColor& color);
    Q_INVOKABLE void updateCellVisible(const QString& cellId, bool visible);

    // Getters
    OverlayGenerator* generator() const { return m_generator; }
    void setGenerator(OverlayGenerator* generator);

signals:
    void cellDataChanged(const QString& cellId);
    void modelUpdated();

private:
    OverlayGenerator* m_generator;
    DiveData* m_dive;
    double m_timePoint;
    QVector<Unabara::CellData> m_cells;

    // Helper to generate display text for a cell
    QString generateDisplayText(const Unabara::CellData& cell) const;
    QString formatValue(Unabara::CellType type, const DiveDataPoint& dataPoint, int tankIndex = 0) const;
};

#endif // CELL_MODEL_H
