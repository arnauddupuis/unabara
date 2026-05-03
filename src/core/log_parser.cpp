#include "include/core/log_parser.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>

#include "include/core/format_parsers/subsurface_parser.h"
#include "include/core/format_parsers/uddf_parser.h"

LogParser::LogParser(QObject *parent)
    : QObject(parent)
    , m_busy(false)
{
    m_parsers.push_back(std::make_unique<SubsurfaceParser>());
    m_parsers.push_back(std::make_unique<UDDFParser>());
}

LogParser::~LogParser() = default;

IDiveLogFormatParser *LogParser::selectParser(QFile &file)
{
    for (const auto &parser : m_parsers) {
        if (parser->canParse(file)) {
            return parser.get();
        }
    }
    return nullptr;
}

bool LogParser::importFile(const QString &filePath)
{
    qDebug() << "LogParser::importFile called with path:" << filePath;

    if (m_busy) {
        m_lastError = tr("Already processing a file");
        emit errorOccurred(m_lastError);
        return false;
    }

    m_busy = true;
    emit busyChanged();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = tr("Could not open file: %1 - Error: %2").arg(filePath).arg(file.errorString());
        qDebug() << "File open error:" << m_lastError;
        emit errorOccurred(m_lastError);
        m_busy = false;
        emit busyChanged();
        return false;
    }

    IDiveLogFormatParser *parser = selectParser(file);
    if (!parser) {
        m_lastError = tr("Unsupported file format: %1").arg(QFileInfo(filePath).fileName());
        qDebug() << "Unsupported file format:" << filePath;
        emit errorOccurred(m_lastError);
        file.close();
        m_busy = false;
        emit busyChanged();
        return false;
    }

    qDebug() << "Selected parser:" << parser->formatName();
    QString parserError;
    QList<DiveData *> dives = parser->parse(file, -1, parserError);
    file.close();

    bool success = parserError.isEmpty();
    if (!success) {
        m_lastError = parserError;
        emit errorOccurred(m_lastError);
        qDeleteAll(dives);
        dives.clear();
    } else if (dives.size() == 1) {
        qDebug() << "Emitting diveImported signal for dive:" << dives.first()->diveName();
        emit diveImported(dives.first());
    } else if (dives.size() > 1) {
        qDebug() << "Emitting multipleImported signal with" << dives.size() << "dives";
        emit multipleImported(dives);
    } else {
        m_lastError = tr("No dives found in file");
        qDebug() << "No dives found in file";
        emit errorOccurred(m_lastError);
        success = false;
    }

    m_busy = false;
    emit busyChanged();

    return success;
}

bool LogParser::importDive(const QString &filePath, int diveNumber)
{
    if (m_busy) {
        m_lastError = tr("Already processing a file");
        emit errorOccurred(m_lastError);
        return false;
    }

    m_busy = true;
    emit busyChanged();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = tr("Could not open file: %1").arg(file.errorString());
        emit errorOccurred(m_lastError);
        m_busy = false;
        emit busyChanged();
        return false;
    }

    IDiveLogFormatParser *parser = selectParser(file);
    if (!parser) {
        m_lastError = tr("Unsupported file format: %1").arg(QFileInfo(filePath).fileName());
        emit errorOccurred(m_lastError);
        file.close();
        m_busy = false;
        emit busyChanged();
        return false;
    }

    QString parserError;
    QList<DiveData *> dives = parser->parse(file, diveNumber, parserError);
    file.close();

    bool success = parserError.isEmpty();
    if (!success) {
        m_lastError = parserError;
        emit errorOccurred(m_lastError);
        qDeleteAll(dives);
        dives.clear();
    } else if (!dives.isEmpty()) {
        emit diveImported(dives.first());
    } else {
        m_lastError = tr("Dive number %1 not found in file").arg(diveNumber);
        emit errorOccurred(m_lastError);
        success = false;
    }

    m_busy = false;
    emit busyChanged();

    return success;
}

QList<QString> LogParser::getDiveList(const QString &filePath)
{
    QList<QString> result;

    if (m_busy) {
        m_lastError = tr("Already processing a file");
        emit errorOccurred(m_lastError);
        return result;
    }

    m_busy = true;
    emit busyChanged();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = tr("Could not open file: %1").arg(file.errorString());
        emit errorOccurred(m_lastError);
        m_busy = false;
        emit busyChanged();
        return result;
    }

    IDiveLogFormatParser *parser = selectParser(file);
    if (!parser) {
        m_lastError = tr("Unsupported file format: %1").arg(QFileInfo(filePath).fileName());
        emit errorOccurred(m_lastError);
        file.close();
        m_busy = false;
        emit busyChanged();
        return result;
    }

    QString parserError;
    result = parser->listDives(file, parserError);
    file.close();

    if (!parserError.isEmpty()) {
        m_lastError = parserError;
        emit errorOccurred(m_lastError);
        result.clear();
    }

    m_busy = false;
    emit busyChanged();

    return result;
}
