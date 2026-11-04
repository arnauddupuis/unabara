#ifndef UNABARA_TESTS_SETTINGS_ISOLATION_H
#define UNABARA_TESTS_SETTINGS_ISOLATION_H

#include <QByteArray>
#include <QSettings>
#include <QString>
#include <QtGlobal>

// Redirects every QSettings the process will create — including Config's
// QSettings("UnabaraProject", "Unabara"), which is NativeFormat — into
// `dir`. Call it in initTestCase(), BEFORE the first QSettings /
// Config::instance() use anywhere in the process: QSettings resolves its
// default search paths once, on first use.
//
// QSettings::setPath(IniFormat, ...) alone is NOT enough: on current Qt
// (observed with 6.10 on Linux) it does not redirect NativeFormat
// settings, and the suites that relied on it wrote their seeds into the
// developer's real ~/.config/UnabaraProject/Unabara.conf on every run.
// XDG_CONFIG_HOME is what NativeFormat resolves through on Linux, so it is
// overridden as well; setPath stays for IniFormat users. Off Linux
// (registry / plists) neither mechanism applies — those suites QSKIP.
inline void isolateUserSettings(const QString& dir)
{
    qputenv("XDG_CONFIG_HOME", dir.toLocal8Bit());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, dir);
}

#endif // UNABARA_TESTS_SETTINGS_ISOLATION_H
