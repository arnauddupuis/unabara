#include "include/core/overlay_template.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

namespace Unabara {

const QString OverlayTemplate::TEMPLATE_VERSION = "1.0";

OverlayTemplate::OverlayTemplate()
    : m_templateName("Untitled Template")
    , m_backgroundImagePath(":/images/DC_Faces/unabara_round_ocean.png")
    , m_backgroundOpacity(1.0)
    , m_defaultFont(QFont("Bitstream Vera Sans", 12))
    , m_defaultTextColor(Qt::white)
{
}

void OverlayTemplate::addCell(const CellData& cell)
{
    // Check if cell with this ID already exists
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cell.cellId()) {
            m_cells[i] = cell;  // Replace existing cell
            return;
        }
    }
    m_cells.append(cell);
}

void OverlayTemplate::removeCell(const QString& cellId)
{
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cellId) {
            m_cells.remove(i);
            return;
        }
    }
}

CellData* OverlayTemplate::getCellData(const QString& cellId)
{
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cellId) {
            return &m_cells[i];
        }
    }
    return nullptr;
}

const CellData* OverlayTemplate::getCellData(const QString& cellId) const
{
    for (int i = 0; i < m_cells.size(); ++i) {
        if (m_cells[i].cellId() == cellId) {
            return &m_cells[i];
        }
    }
    return nullptr;
}

bool OverlayTemplate::hasCell(const QString& cellId) const
{
    return getCellData(cellId) != nullptr;
}

bool OverlayTemplate::isValid() const
{
    return validate().isEmpty();
}

QStringList OverlayTemplate::validate() const
{
    QStringList errors;

    if (m_templateName.isEmpty()) {
        errors << "Template name is empty";
    }

    if (m_backgroundImagePath.isEmpty()) {
        errors << "Background image path is empty";
    }

    if (m_backgroundOpacity < 0.0 || m_backgroundOpacity > 1.0) {
        errors << "Background opacity out of range (must be 0.0-1.0)";
    }

    if (m_cells.isEmpty()) {
        errors << "No cells defined in template";
    }

    // Check for duplicate cell IDs
    QSet<QString> cellIds;
    for (const auto& cell : m_cells) {
        if (cellIds.contains(cell.cellId())) {
            errors << QString("Duplicate cell ID: %1").arg(cell.cellId());
        }
        cellIds.insert(cell.cellId());

        // Validate cell positions (should be normalized 0.0-1.0)
        QPointF pos = cell.position();
        if (pos.x() < 0.0 || pos.x() > 1.0 || pos.y() < 0.0 || pos.y() > 1.0) {
            errors << QString("Cell '%1' has invalid position: (%2, %3)")
                          .arg(cell.cellId())
                          .arg(pos.x())
                          .arg(pos.y());
        }
    }

    return errors;
}

QJsonObject OverlayTemplate::toJson() const
{
    QJsonObject json;

    json["version"] = TEMPLATE_VERSION;
    json["templateName"] = m_templateName;
    json["backgroundImage"] = m_backgroundImagePath;
    json["backgroundOpacity"] = m_backgroundOpacity;

    // Default font
    QJsonObject fontJson;
    fontJson["family"] = m_defaultFont.family();
    fontJson["pointSize"] = m_defaultFont.pointSize();
    fontJson["weight"] = m_defaultFont.weight();
    fontJson["italic"] = m_defaultFont.italic();
    fontJson["bold"] = m_defaultFont.bold();
    json["defaultFont"] = fontJson;

    // Default text color
    json["defaultTextColor"] = m_defaultTextColor.name(QColor::HexArgb);

    // Cells
    QJsonArray cellsArray;
    for (const auto& cell : m_cells) {
        cellsArray.append(cell.toJson());
    }
    json["cells"] = cellsArray;

    return json;
}

OverlayTemplate OverlayTemplate::fromJson(const QJsonObject& json)
{
    OverlayTemplate templ;

    // Check version
    QString version = json["version"].toString();
    if (version != TEMPLATE_VERSION) {
        qWarning() << "Template version mismatch. Expected:" << TEMPLATE_VERSION
                   << "Got:" << version << "- Attempting to load anyway";
    }

    templ.m_templateName = json["templateName"].toString("Untitled Template");
    templ.m_backgroundImagePath = json["backgroundImage"].toString();
    templ.m_backgroundOpacity = json["backgroundOpacity"].toDouble(1.0);

    // Default font
    if (json.contains("defaultFont")) {
        QJsonObject fontJson = json["defaultFont"].toObject();
        QFont font;
        font.setFamily(fontJson["family"].toString("Arial"));
        font.setPointSize(fontJson["pointSize"].toInt(12));
        font.setWeight(static_cast<QFont::Weight>(fontJson["weight"].toInt(QFont::Normal)));
        font.setItalic(fontJson["italic"].toBool(false));
        font.setBold(fontJson["bold"].toBool(false));
        templ.m_defaultFont = font;
    }

    // Default text color
    if (json.contains("defaultTextColor")) {
        templ.m_defaultTextColor = QColor(json["defaultTextColor"].toString());
    }

    // Cells
    if (json.contains("cells")) {
        QJsonArray cellsArray = json["cells"].toArray();
        for (const QJsonValue& cellValue : cellsArray) {
            CellData cell = CellData::fromJson(cellValue.toObject());
            templ.m_cells.append(cell);
        }
    }

    return templ;
}

bool OverlayTemplate::saveToFile(const QString& filePath) const
{
    // Validate before saving
    QStringList errors = validate();
    if (!errors.isEmpty()) {
        qWarning() << "Template validation failed:";
        for (const QString& error : errors) {
            qWarning() << "  -" << error;
        }
        return false;
    }

    QJsonObject json = toJson();
    QJsonDocument doc(json);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Failed to open file for writing:" << filePath;
        qWarning() << "Error:" << file.errorString();
        return false;
    }

    qint64 bytesWritten = file.write(doc.toJson(QJsonDocument::Indented));
    if (bytesWritten == -1) {
        qWarning() << "Failed to write template to file:" << filePath;
        qWarning() << "Error:" << file.errorString();
        return false;
    }

    file.close();
    qDebug() << "Template saved successfully to:" << filePath;
    return true;
}

OverlayTemplate OverlayTemplate::loadFromFile(const QString& filePath, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        QString error = QString("Failed to open file for reading: %1 - %2")
                            .arg(filePath)
                            .arg(file.errorString());
        qWarning() << error;
        if (errorMessage) *errorMessage = error;
        return OverlayTemplate();  // Return default template
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull()) {
        QString error = QString("Failed to parse JSON: %1 at offset %2")
                            .arg(parseError.errorString())
                            .arg(parseError.offset);
        qWarning() << error;
        if (errorMessage) *errorMessage = error;
        return OverlayTemplate();
    }

    OverlayTemplate templ = fromJson(doc.object());

    // Resolve background image path relative to template file
    if (!templ.m_backgroundImagePath.startsWith(":/") &&
        !templ.m_backgroundImagePath.startsWith("qrc:")) {

        templ.m_backgroundImagePath = resolveBackgroundImagePath(filePath,
                                                                 templ.m_backgroundImagePath);
    }

    qDebug() << "Template loaded successfully from:" << filePath;
    qDebug() << "  Name:" << templ.m_templateName;
    qDebug() << "  Cells:" << templ.m_cells.size();

    // Validate loaded template
    QStringList errors = templ.validate();
    if (!errors.isEmpty()) {
        qWarning() << "Loaded template has validation warnings:";
        for (const QString& error : errors) {
            qWarning() << "  -" << error;
        }
    }

    return templ;
}

QString OverlayTemplate::resolveBackgroundImagePath(const QString& templatePath,
                                                    const QString& backgroundPath)
{
    // If backgroundPath is already absolute, return as-is
    QFileInfo bgInfo(backgroundPath);
    if (bgInfo.isAbsolute()) {
        return backgroundPath;
    }

    // If backgroundPath is relative, resolve it relative to template directory
    QFileInfo templateInfo(templatePath);
    QDir templateDir = templateInfo.absoluteDir();
    QString resolvedPath = templateDir.absoluteFilePath(backgroundPath);

    qDebug() << "Resolving background image path:";
    qDebug() << "  Template path:" << templatePath;
    qDebug() << "  Background path (relative):" << backgroundPath;
    qDebug() << "  Resolved path:" << resolvedPath;

    return resolvedPath;
}

} // namespace Unabara
