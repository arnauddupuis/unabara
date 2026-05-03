#ifndef LOG_PARSER_H
#define LOG_PARSER_H

#include <QFile>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

#include "include/core/dive_data.h"
#include "include/core/format_parsers/idive_log_format_parser.h"

class LogParser : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    Q_PROPERTY(bool busy READ isBusy NOTIFY busyChanged)

public:
    explicit LogParser(QObject *parent = nullptr);
    ~LogParser() override;

    Q_INVOKABLE bool importFile(const QString &filePath);
    Q_INVOKABLE bool importDive(const QString &filePath, int diveNumber);
    Q_INVOKABLE QList<QString> getDiveList(const QString &filePath);

    QString lastError() const { return m_lastError; }
    bool isBusy() const { return m_busy; }

signals:
    void diveImported(DiveData* dive);
    void multipleImported(QList<DiveData*> dives);
    void errorOccurred(const QString &error);
    void busyChanged();

private:
    IDiveLogFormatParser *selectParser(QFile &file);

    std::vector<std::unique_ptr<IDiveLogFormatParser>> m_parsers;
    QString m_lastError;
    bool m_busy;
};

#endif // LOG_PARSER_H
