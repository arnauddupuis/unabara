#include "include/ui/cell_model.h"
#include "include/core/units.h"
#include <QDebug>

CellModel::CellModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_generator(nullptr)
    , m_dive(nullptr)
    , m_timePoint(0.0)
{
}

int CellModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_cells.size();
}

QVariant CellModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_cells.size())
        return QVariant();

    const Unabara::CellData& cell = m_cells[index.row()];

    switch (role) {
    case CellIdRole:
        return cell.cellId();
    case CellTypeRole:
        return static_cast<int>(cell.cellType());
    case PositionRole:
        return cell.position();
    case VisibleRole:
        return cell.visible();
    case FontRole:
        return cell.font();
    case TextColorRole:
        return cell.textColor();
    case DisplayTextRole:
        return generateDisplayText(cell);
    case CalculatedSizeRole:
        return cell.calculatedSize();
    case HasCustomFontRole:
        return cell.hasCustomFont();
    case HasCustomColorRole:
        return cell.hasCustomColor();
    case TankIndexRole:
        return cell.tankIndex();
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> CellModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[CellIdRole] = "cellId";
    roles[CellTypeRole] = "cellType";
    roles[PositionRole] = "position";
    roles[VisibleRole] = "visible";
    roles[FontRole] = "font";
    roles[TextColorRole] = "textColor";
    roles[DisplayTextRole] = "displayText";
    roles[CalculatedSizeRole] = "calculatedSize";
    roles[HasCustomFontRole] = "hasCustomFont";
    roles[HasCustomColorRole] = "hasCustomColor";
    roles[TankIndexRole] = "tankIndex";
    return roles;
}

void CellModel::setGenerator(OverlayGenerator* generator)
{
    if (m_generator != generator) {
        m_generator = generator;

        if (m_generator) {
            // Connect to generator signals to auto-update
            connect(m_generator, &OverlayGenerator::cellsChanged,
                    this, [this]() {
                if (m_generator && m_dive) {
                    updateFromGenerator(m_generator, m_dive, m_timePoint);
                }
            });

            connect(m_generator, &OverlayGenerator::cellLayoutChanged,
                    this, [this]() {
                if (m_generator && m_dive) {
                    updateFromGenerator(m_generator, m_dive, m_timePoint);
                }
            });
        }
    }
}

void CellModel::updateFromGenerator(OverlayGenerator* generator, DiveData* dive, double timePoint)
{
    if (!generator) {
        qWarning() << "CellModel::updateFromGenerator: No generator provided";
        return;
    }

    m_generator = generator;
    m_dive = dive;
    m_timePoint = timePoint;

    beginResetModel();
    m_cells = generator->cells();
    endResetModel();

    emit modelUpdated();
}

void CellModel::updateCellPosition(const QString& cellId, const QPointF& position)
{
    if (!m_generator) {
        qWarning() << "CellModel::updateCellPosition: No generator set";
        return;
    }

    m_generator->setCellPosition(cellId, position);

    // Update our local copy
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cellId) {
            m_cells[i].setPosition(position);
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {PositionRole});
            emit cellDataChanged(cellId);
            break;
        }
    }
}

void CellModel::updateCellFont(const QString& cellId, const QFont& font)
{
    if (!m_generator) {
        qWarning() << "CellModel::updateCellFont: No generator set";
        return;
    }

    m_generator->setCellFont(cellId, font);

    // Update our local copy
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cellId) {
            m_cells[i].setFont(font, true);
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {FontRole, HasCustomFontRole});
            emit cellDataChanged(cellId);
            break;
        }
    }
}

void CellModel::updateCellColor(const QString& cellId, const QColor& color)
{
    if (!m_generator) {
        qWarning() << "CellModel::updateCellColor: No generator set";
        return;
    }

    m_generator->setCellColor(cellId, color);

    // Update our local copy
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cellId) {
            m_cells[i].setTextColor(color, true);
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {TextColorRole, HasCustomColorRole});
            emit cellDataChanged(cellId);
            break;
        }
    }
}

void CellModel::updateCellVisible(const QString& cellId, bool visible)
{
    if (!m_generator) {
        qWarning() << "CellModel::updateCellVisible: No generator set";
        return;
    }

    m_generator->setCellVisible(cellId, visible);

    // Update our local copy
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cellId) {
            m_cells[i].setVisible(visible);
            QModelIndex idx = index(i);
            emit dataChanged(idx, idx, {VisibleRole});
            emit cellDataChanged(cellId);
            break;
        }
    }
}

QString CellModel::generateDisplayText(const Unabara::CellData& cell) const
{
    if (!m_dive)
        return "No Data";

    DiveDataPoint dataPoint = m_dive->dataAtTime(m_timePoint);
    return formatValue(cell.cellType(), dataPoint, cell.tankIndex());
}

QString CellModel::formatValue(Unabara::CellType type, const DiveDataPoint& dataPoint, int tankIndex) const
{
    Units::UnitSystem unitSystem = Config::instance()->unitSystem();

    switch (type) {
    case Unabara::CellType::Depth: {
        QString value = Units::formatDepthValue(dataPoint.depth, unitSystem);
        return QString("DEPTH\n%1").arg(value);
    }

    case Unabara::CellType::Temperature: {
        QString value = Units::formatTemperatureValue(dataPoint.temperature, unitSystem);
        return QString("TEMP\n%1").arg(value);
    }

    case Unabara::CellType::Time: {
        int totalSeconds = static_cast<int>(m_timePoint);
        int minutes = totalSeconds / 60;
        int seconds = totalSeconds % 60;
        QString value = QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
        return QString("DIVE TIME\n%1").arg(value);
    }

    case Unabara::CellType::NDL: {
        // Dynamically switch between NDL and TTS based on deco status
        bool inDeco = (dataPoint.ndl <= 0);

        if (inDeco) {
            // Show TTS with DECO indicator when in decompression
            QString value;
            if (dataPoint.tts > 0) {
                value = QString("%1 min").arg(static_cast<int>(dataPoint.tts));
            } else {
                value = "---";
            }

            // Add ceiling info if available
            QString decoInfo;
            if (dataPoint.ceiling > 0) {
                QString ceilingStr = Units::formatDepthValue(dataPoint.ceiling, unitSystem);
                decoInfo = QString("\nDECO (%1)").arg(ceilingStr);
            }

            return QString("TTS\n%1%2").arg(value).arg(decoInfo);
        } else {
            // Show NDL when not in decompression
            QString value;
            if (dataPoint.ndl > 0) {
                value = QString("%1 min").arg(static_cast<int>(dataPoint.ndl));
            } else {
                value = "---";
            }
            return QString("NDL\n%1").arg(value);
        }
    }

    case Unabara::CellType::TTS: {
        QString value;
        if (dataPoint.tts > 0) {
            value = QString("%1 min").arg(static_cast<int>(dataPoint.tts));
        } else {
            value = "---";
        }
        return QString("TTS\n%1").arg(value);
    }

    case Unabara::CellType::Pressure: {
        // Get the pressure value for the specified tank
        QString value;
        if (tankIndex >= 0 && tankIndex < dataPoint.pressures.size()) {
            value = Units::formatPressureValue(dataPoint.pressures[tankIndex], unitSystem);
        } else if (!dataPoint.pressures.isEmpty()) {
            // Fallback to first tank if index is out of bounds
            value = Units::formatPressureValue(dataPoint.pressures[0], unitSystem);
        } else {
            value = "---";
        }

        // Create tank label with gas mix info
        QString label;
        int tankCount = dataPoint.tankCount();

        if (tankCount > 1 && m_dive && tankIndex < m_dive->cylinderCount()) {
            // Multi-tank: show short label with gas mix
            const CylinderInfo& cylinder = m_dive->cylinderInfo(tankIndex);

            QString gasMix;
            if (cylinder.hePercent > 0.0) {
                // Trimix: show O2/He
                gasMix = QString("(%1/%2)").arg(qRound(cylinder.o2Percent)).arg(qRound(cylinder.hePercent));
            } else if (cylinder.o2Percent != 21.0) {
                // Nitrox: show O2%
                gasMix = QString("(%1%)").arg(qRound(cylinder.o2Percent));
            }

            if (!gasMix.isEmpty()) {
                label = QString("T%1 %2").arg(tankIndex + 1).arg(gasMix);
            } else {
                label = QString("TNK %1").arg(tankIndex + 1);
            }
        } else if (tankCount == 1 && m_dive && tankIndex < m_dive->cylinderCount()) {
            // Single tank: show full label with gas mix
            const CylinderInfo& cylinder = m_dive->cylinderInfo(tankIndex);

            QString gasMix;
            if (cylinder.hePercent > 0.0) {
                gasMix = QString("(%1/%2)").arg(qRound(cylinder.o2Percent)).arg(qRound(cylinder.hePercent));
            } else if (cylinder.o2Percent != 21.0) {
                gasMix = QString("(%1%)").arg(qRound(cylinder.o2Percent));
            }

            if (!gasMix.isEmpty()) {
                label = QString("TANK %1 %2").arg(tankIndex + 1).arg(gasMix);
            } else {
                label = QString("TANK %1").arg(tankIndex + 1);
            }
        } else {
            // Generic pressure label (no tank info available)
            label = "PRESSURE";
        }

        return QString("%1\n%2").arg(label).arg(value);
    }

    case Unabara::CellType::PO2Cell1: {
        QString value;
        if (!dataPoint.po2Sensors.isEmpty() && dataPoint.po2Sensors.size() > 0) {
            value = QString("%1").arg(dataPoint.po2Sensors[0], 0, 'f', 2);
        } else {
            value = "---";
        }
        return QString("CELL 1\n%1").arg(value);
    }

    case Unabara::CellType::PO2Cell2: {
        QString value;
        if (dataPoint.po2Sensors.size() > 1) {
            value = QString("%1").arg(dataPoint.po2Sensors[1], 0, 'f', 2);
        } else {
            value = "---";
        }
        return QString("CELL 2\n%1").arg(value);
    }

    case Unabara::CellType::PO2Cell3: {
        QString value;
        if (dataPoint.po2Sensors.size() > 2) {
            value = QString("%1").arg(dataPoint.po2Sensors[2], 0, 'f', 2);
        } else {
            value = "---";
        }
        return QString("CELL 3\n%1").arg(value);
    }

    case Unabara::CellType::CompositePO2: {
        QString value;
        double compositePO2 = dataPoint.getCompositePO2();
        if (compositePO2 > 0) {
            value = QString("%1").arg(compositePO2, 0, 'f', 2);
        } else {
            value = "---";
        }
        return QString("PO2\n%1").arg(value);
    }

    default:
        return "Unknown";
    }
}
