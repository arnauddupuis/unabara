#include "include/core/cell_data.h"
#include <QJsonArray>

namespace Unabara {

CellData::CellData()
    : m_cellType(CellType::Unknown)
    , m_position(0.0, 0.0)
    , m_visible(true)
    , m_font(QFont("Arial", 12))
    , m_textColor(Qt::white)
    , m_calculatedSize(0.0, 0.0)
    , m_hasCustomFont(false)
    , m_hasCustomColor(false)
    , m_tankIndex(-1)
{
}

CellData::CellData(const QString& cellId, CellType cellType)
    : m_cellId(cellId)
    , m_cellType(cellType)
    , m_position(0.0, 0.0)
    , m_visible(true)
    , m_font(QFont("Arial", 12))
    , m_textColor(Qt::white)
    , m_calculatedSize(0.0, 0.0)
    , m_hasCustomFont(false)
    , m_hasCustomColor(false)
    , m_tankIndex(-1)
{
}

void CellData::setFont(const QFont& font, bool isCustom)
{
    m_font = font;
    m_hasCustomFont = isCustom;
}

void CellData::setTextColor(const QColor& color, bool isCustom)
{
    m_textColor = color;
    m_hasCustomColor = isCustom;
}

QJsonObject CellData::toJson() const
{
    QJsonObject json;

    json["cellId"] = m_cellId;
    json["cellType"] = cellTypeToString(m_cellType);

    // Position (normalized coordinates)
    QJsonObject posJson;
    posJson["x"] = m_position.x();
    posJson["y"] = m_position.y();
    json["position"] = posJson;

    json["visible"] = m_visible;

    // Font (only if custom)
    if (m_hasCustomFont) {
        QJsonObject fontJson;
        fontJson["family"] = m_font.family();
        fontJson["pointSize"] = m_font.pointSize();
        fontJson["weight"] = m_font.weight();
        fontJson["italic"] = m_font.italic();
        fontJson["bold"] = m_font.bold();
        json["font"] = fontJson;
    }

    // Text color (only if custom)
    if (m_hasCustomColor) {
        json["textColor"] = m_textColor.name(QColor::HexArgb);
    }

    // Calculated size (for reference, not strictly necessary)
    QJsonObject sizeJson;
    sizeJson["width"] = m_calculatedSize.width();
    sizeJson["height"] = m_calculatedSize.height();
    json["calculatedSize"] = sizeJson;

    // Tank index (for pressure cells)
    if (m_tankIndex >= 0) {
        json["tankIndex"] = m_tankIndex;
    }

    json["hasCustomFont"] = m_hasCustomFont;
    json["hasCustomColor"] = m_hasCustomColor;

    return json;
}

CellData CellData::fromJson(const QJsonObject& json)
{
    CellData cell;

    cell.m_cellId = json["cellId"].toString();
    cell.m_cellType = cellTypeFromString(json["cellType"].toString());

    // Position
    QJsonObject posJson = json["position"].toObject();
    cell.m_position = QPointF(posJson["x"].toDouble(), posJson["y"].toDouble());

    cell.m_visible = json["visible"].toBool(true);

    // Font (if present, it's custom)
    if (json.contains("font")) {
        QJsonObject fontJson = json["font"].toObject();
        QFont font;
        font.setFamily(fontJson["family"].toString("Arial"));
        font.setPointSize(fontJson["pointSize"].toInt(12));
        font.setWeight(static_cast<QFont::Weight>(fontJson["weight"].toInt(QFont::Normal)));
        font.setItalic(fontJson["italic"].toBool(false));
        font.setBold(fontJson["bold"].toBool(false));
        cell.m_font = font;
        cell.m_hasCustomFont = json["hasCustomFont"].toBool(true);
    }

    // Text color (if present, it's custom)
    if (json.contains("textColor")) {
        cell.m_textColor = QColor(json["textColor"].toString());
        cell.m_hasCustomColor = json["hasCustomColor"].toBool(true);
    }

    // Calculated size
    if (json.contains("calculatedSize")) {
        QJsonObject sizeJson = json["calculatedSize"].toObject();
        cell.m_calculatedSize = QSizeF(sizeJson["width"].toDouble(),
                                       sizeJson["height"].toDouble());
    }

    // Tank index
    cell.m_tankIndex = json["tankIndex"].toInt(-1);

    return cell;
}

QString CellData::cellTypeToString(CellType type)
{
    switch (type) {
        case CellType::Depth: return "Depth";
        case CellType::Temperature: return "Temperature";
        case CellType::Time: return "Time";
        case CellType::NDL: return "NDL";
        case CellType::TTS: return "TTS";
        case CellType::Pressure: return "Pressure";
        case CellType::PO2Cell1: return "PO2Cell1";
        case CellType::PO2Cell2: return "PO2Cell2";
        case CellType::PO2Cell3: return "PO2Cell3";
        case CellType::CompositePO2: return "CompositePO2";
        default: return "Unknown";
    }
}

CellType CellData::cellTypeFromString(const QString& str)
{
    if (str == "Depth") return CellType::Depth;
    if (str == "Temperature") return CellType::Temperature;
    if (str == "Time") return CellType::Time;
    if (str == "NDL") return CellType::NDL;
    if (str == "TTS") return CellType::TTS;
    if (str == "Pressure") return CellType::Pressure;
    if (str == "PO2Cell1") return CellType::PO2Cell1;
    if (str == "PO2Cell2") return CellType::PO2Cell2;
    if (str == "PO2Cell3") return CellType::PO2Cell3;
    if (str == "CompositePO2") return CellType::CompositePO2;
    return CellType::Unknown;
}

} // namespace Unabara
