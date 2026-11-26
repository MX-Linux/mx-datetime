/**********************************************************************
 *  MX Date/Time helper.
 **********************************************************************
 *   Copyright (C) 2026-2026 by MX Authors
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 * This file is part of mx-datetime.
 **********************************************************************/

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>

#include <cstdio>

#include "chronypaths.h"

namespace
{
struct ProcessResult
{
    bool started = false;
    int exitCode = 1;
    QProcess::ExitStatus exitStatus = QProcess::NormalExit;
    QByteArray standardOutput;
    QByteArray standardError;
};

void writeAndFlush(FILE *stream, const QByteArray &data)
{
    if (!data.isEmpty()) {
        std::fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stream);
        std::fflush(stream);
    }
}

void printError(const QString &message)
{
    writeAndFlush(stderr, message.toUtf8() + '\n');
}

[[nodiscard]] bool isValidTimeZone(const QString &zone)
{
    static const QRegularExpression zoneRx(
        QRegularExpression::anchoredPattern(QStringLiteral(R"([A-Za-z0-9._+-]+(?:/[A-Za-z0-9._+-]+)*)")));
    const QString trimmedZone = zone.trimmed();
    if (!zoneRx.match(trimmedZone).hasMatch()) {
        return false;
    }

    const QStringList segments = trimmedZone.split('/');
    return !segments.contains(QStringLiteral(".")) && !segments.contains(QStringLiteral(".."));
}

bool isTrustedChronyPath(const QString &path) {
    if (!path.startsWith(QStringLiteral("/etc/")) || QDir::cleanPath(path) != path) {
        return false;
    }
    QFileInfo info(path);
    while (info.absoluteFilePath() != QStringLiteral("/")) {
        if (info.isSymLink() || (info.exists() && (info.ownerId() != 0
                || info.permissions().testAnyFlags(QFileDevice::WriteGroup | QFileDevice::WriteOther)))) {
            return false;
        }
        info.setFile(info.absolutePath());
    }
    return true;
}

[[nodiscard]] int writeManagedFile(const QString &destinationPath, const QByteArray &content, bool preserveMode = false)
{
    const QFileInfo destinationInfo(destinationPath);
    QDir parentDir;
    if (!parentDir.mkpath(destinationInfo.absolutePath())) {
        printError(QString("Unable to create directory for %1").arg(destinationPath));
        return 1;
    }

    QSaveFile file(destinationPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        printError(QString("Unable to open %1 for writing").arg(destinationPath));
        return 1;
    }

    if (file.write(content) != content.size()) {
        printError(QString("Unable to write %1").arg(destinationPath));
        return 1;
    }
    if (!file.commit()) {
        printError(QString("Unable to commit %1").arg(destinationPath));
        return 1;
    }
    if (!preserveMode && !QFile::setPermissions(destinationPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther)) {
        printError(QString("Unable to set permissions on %1").arg(destinationPath));
        return 1;
    }

    return 0;
}

[[nodiscard]] const QHash<QString, QStringList> &allowedCommands()
{
    static const QHash<QString, QStringList> commands {
        {"chronyc", {"/usr/bin/chronyc", "/bin/chronyc"}},
        {"chronyd", {"/usr/sbin/chronyd", "/sbin/chronyd", "/usr/bin/chronyd"}},
        {"date", {"/usr/bin/date", "/bin/date"}},
        {"hwclock", {"/usr/sbin/hwclock", "/sbin/hwclock", "/usr/bin/hwclock"}},
        {"rc-update", {"/usr/sbin/rc-update", "/sbin/rc-update", "/usr/bin/rc-update"}},
        {"service", {"/usr/sbin/service", "/sbin/service", "/usr/bin/service"}},
        {"systemctl", {"/usr/bin/systemctl", "/bin/systemctl"}},
        {"timedatectl", {"/usr/bin/timedatectl", "/bin/timedatectl"}},
        {"update-rc.d", {"/usr/sbin/update-rc.d", "/sbin/update-rc.d", "/usr/bin/update-rc.d"}},
    };
    return commands;
}

[[nodiscard]] QString resolveBinary(const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isExecutable()) {
            return candidate;
        }
    }
    return {};
}

[[nodiscard]] ProcessResult runProcess(const QString &program, const QStringList &arguments)
{
    ProcessResult result;

    QProcess process;
    process.start(program, arguments, QIODevice::ReadOnly);
    if (!process.waitForStarted()) {
        result.standardError = QString("Failed to start %1").arg(program).toUtf8();
        result.exitCode = 127;
        return result;
    }

    result.started = true;
    process.waitForFinished(-1);
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    return result;
}

[[nodiscard]] int relayResult(const ProcessResult &result)
{
    writeAndFlush(stdout, result.standardOutput);
    writeAndFlush(stderr, result.standardError);
    if (!result.started) {
        return result.exitCode;
    }
    return result.exitStatus == QProcess::NormalExit ? result.exitCode : 1;
}

struct Operation {
    QString program;
    QStringList arguments;
};

int runOperation(const QString &action, const QStringList &operands) {
    using namespace Qt::StringLiterals;
    static const QHash<QString, Operation> operations {
        {u"set-zone"_s, {u"timedatectl"_s, {u"set-timezone"_s}}},
        {u"systemd-set-time"_s, {u"timedatectl"_s, {u"set-time"_s}}},
        {u"set-time"_s, {u"date"_s, {u"-s"_s}}},
        {u"read-rtc"_s, {u"hwclock"_s, {u"--verbose"_s}}},
        {u"adjust-rtc"_s, {u"hwclock"_s, {u"--adjust"_s}}},
        {u"ntp-burst"_s, {u"chronyc"_s, {u"burst 4/4"_s}}},
        {u"ntp-step"_s, {u"chronyc"_s, {u"makestep"_s}}},
        {u"ntp-once"_s, {u"chronyd"_s, {u"-q"_s}}},
        {u"copy-to-system"_s, {u"hwclock"_s, {u"--hctosys"_s}}},
        {u"copy-to-rtc"_s, {u"hwclock"_s, {u"--systohc"_s}}},
        {u"copy-to-rtc-update-drift"_s, {u"hwclock"_s, {u"--systohc"_s, u"--update-drift"_s}}},
        {u"rtc-utc"_s, {u"hwclock"_s, {u"--systohc"_s, u"--utc"_s}}},
        {u"rtc-local"_s, {u"hwclock"_s, {u"--systohc"_s, u"--localtime"_s}}},
        {u"systemd-rtc-utc"_s, {u"timedatectl"_s, {u"set-local-rtc"_s, u"0"_s}}},
        {u"systemd-rtc-local"_s, {u"timedatectl"_s, {u"set-local-rtc"_s, u"1"_s}}},
        {u"reload-sources"_s, {u"chronyc"_s, {u"reload"_s, u"sources"_s}}},
        {u"sysv-enable"_s, {u"update-rc.d"_s, {u"chrony"_s, u"enable"_s}}},
        {u"sysv-disable"_s, {u"update-rc.d"_s, {u"chrony"_s, u"disable"_s}}},
        {u"sysv-start"_s, {u"service"_s, {u"chrony"_s, u"start"_s}}},
        {u"sysv-stop"_s, {u"service"_s, {u"chrony"_s, u"stop"_s}}},
        {u"sysv-restart"_s, {u"service"_s, {u"chrony"_s, u"restart"_s}}},
        {u"openrc-enable"_s, {u"rc-update"_s, {u"add"_s, u"chronyd"_s}}},
        {u"openrc-disable"_s, {u"rc-update"_s, {u"del"_s, u"chronyd"_s}}},
        {u"systemd-enable"_s, {u"systemctl"_s, {u"enable"_s}}},
        {u"systemd-disable"_s, {u"systemctl"_s, {u"disable"_s}}},
        {u"systemd-start"_s, {u"systemctl"_s, {u"start"_s}}},
        {u"systemd-stop"_s, {u"systemctl"_s, {u"stop"_s}}},
        {u"systemd-restart"_s, {u"systemctl"_s, {u"restart"_s}}},
    };

    const auto it = operations.constFind(action);
    if (it == operations.cend()) {
        printError(u"Unsupported helper action: %1"_s.arg(action));
        return 1;
    }
    bool valid = operands.isEmpty();
    const QString value = operands.value(0);
    if (it->program == u"systemctl"_s) {
        valid = operands.size() == 1 && (value == u"chronyd.service"_s || value == u"chrony.service"_s);
    } else if (action == u"set-zone"_s) {
        valid = operands.size() == 1 && isValidTimeZone(value) && value == value.trimmed()
                && !value.startsWith('-') && QFileInfo(u"/usr/share/zoneinfo/"_s + value).isFile();
    } else if (action == u"set-time"_s || action == u"systemd-set-time"_s) {
        const QString format = u"yyyy-MM-ddTHH:mm:ss.zzz"_s;
        const QDateTime time = QDateTime::fromString(value, format);
        valid = operands.size() == 1 && time.isValid() && time.toString(format) == value;
    }
    if (!valid) {
        printError(u"Invalid arguments for %1"_s.arg(action));
        return 1;
    }
    const QString executable = resolveBinary(allowedCommands().value(it->program));
    if (executable.isEmpty()) {
        printError(u"Command is not available: %1"_s.arg(it->program));
        return 127;
    }
    return relayResult(runProcess(executable, it->arguments + operands));
}

[[nodiscard]] int handleWriteTimezone(const QStringList &arguments)
{
    if (arguments.size() != 1) {
        printError(QStringLiteral("write-timezone requires exactly one value"));
        return 1;
    }

    const QString zone = arguments.constFirst().trimmed();
    if (!isValidTimeZone(zone)) {
        printError(QString("Invalid timezone value: %1").arg(zone));
        return 1;
    }
    return writeManagedFile(QStringLiteral("/etc/timezone"), zone.toUtf8() + '\n');
}

[[nodiscard]] int handleSetLocaltimeLink(const QStringList &arguments)
{
    if (arguments.size() != 1) {
        printError(QStringLiteral("set-localtime-link requires exactly one value"));
        return 1;
    }

    const QString zone = arguments.constFirst().trimmed();
    if (!isValidTimeZone(zone)) {
        printError(QString("Invalid timezone value: %1").arg(zone));
        return 1;
    }

    const QFileInfo zoneInfoRoot(QStringLiteral("/usr/share/zoneinfo"));
    const QString canonicalRoot = zoneInfoRoot.canonicalFilePath();
    const QString targetPath = QDir(zoneInfoRoot.filePath()).filePath(zone);
    const QFileInfo targetInfo(targetPath);
    const QString canonicalTarget = targetInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalTarget.isEmpty()
        || !canonicalTarget.startsWith(canonicalRoot + '/') || !targetInfo.isFile()) {
        printError(QString("Timezone data is not available: %1").arg(zone));
        return 1;
    }

    const QFileInfo localtimeInfo(QStringLiteral("/etc/localtime"));
    if (localtimeInfo.exists()) {
        if (localtimeInfo.isDir() || !QFile::remove(localtimeInfo.filePath())) {
            printError(QStringLiteral("Unable to replace /etc/localtime"));
            return 1;
        }
    }
    if (!QFile::link(targetPath, QStringLiteral("/etc/localtime"))) {
        printError(QStringLiteral("Unable to create /etc/localtime symlink"));
        return 1;
    }
    return 0;
}

[[nodiscard]] int handleSetHwclockMode(const QStringList &arguments)
{
    if (arguments.size() != 1) {
        printError(QStringLiteral("set-hwclock-mode requires utc or local"));
        return 1;
    }

    const QString mode = arguments.constFirst().trimmed();
    QString replacement;
    if (mode == QLatin1String("utc")) {
        replacement = QStringLiteral("clock=\"UTC\"");
    } else if (mode == QLatin1String("local")) {
        replacement = QStringLiteral("clock=\"local\"");
    } else {
        printError(QString("Unsupported hwclock mode: %1").arg(mode));
        return 1;
    }

    const QString path = QStringLiteral("/etc/conf.d/hwclock");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        printError(QStringLiteral("Unable to read /etc/conf.d/hwclock"));
        return 1;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    static const QRegularExpression clockLineRx(QStringLiteral(R"((?m)^clock=.*$)"));
    if (content.contains(clockLineRx)) {
        content.replace(clockLineRx, replacement);
    } else {
        if (!content.isEmpty() && !content.endsWith('\n')) {
            content.append('\n');
        }
        content.append(replacement);
        content.append('\n');
    }

    return writeManagedFile(path, content.toUtf8());
}

bool isSourceLine(const QByteArray &line) {
    const QByteArray words = line.simplified();
    return words.startsWith("pool ") || words.startsWith("server ") || words.startsWith("peer ");
}

int handleChronyFile(const QString &action, const QStringList &arguments) {
    using namespace Qt::StringLiterals;
    const QString config = Chrony::chronyConfigFile();
    if (!isTrustedChronyPath(config) || arguments.size() != 1) {
        printError(u"Invalid chrony configuration path or argument count"_s);
        return 1;
    }
    const bool retire = action == u"retire-inline-sources"_s;
    const QString expected = retire ? config : Chrony::chronySourcesFile();
    if (arguments.first() != expected || !isTrustedChronyPath(expected)) {
        printError(u"Invalid chrony destination"_s);
        return 1;
    }
    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly)) {
        printError(u"Cannot read chrony source data"_s);
        return 1;
    }
    const QByteArray content = input.readAll();
    if (input.error() != QFileDevice::NoError || content.contains('\0')) {
        printError(u"Invalid chrony source data"_s);
        return 1;
    }
    if (!retire) {
        for (const QByteArray &raw : content.split('\n')) {
            const QByteArray line = raw.trimmed();
            if (!line.isEmpty() && !line.startsWith('#') && !isSourceLine(line)) {
                printError(u"Only chrony sources are allowed"_s);
                return 1;
            }
        }
        return writeManagedFile(expected, content);
    }

    QFile file(config);
    if (!file.open(QIODevice::ReadOnly)) {
        printError(file.errorString());
        return 1;
    }
    const QList<QByteArray> snapshot = content.split('\n');
    QByteArray updated;
    bool changed = false;
    while (!file.atEnd()) {
        const QByteArray raw = file.readLine();
        if (isSourceLine(raw) && snapshot.contains(raw.trimmed())) {
            updated.append("##");
            changed = true;
        }
        updated.append(raw);
    }
    if (file.error() != QFileDevice::NoError) {
        printError(file.errorString());
        return 1;
    }
    file.close();
    if (changed && writeManagedFile(config, updated, true) != 0) {
        return 1;
    }
    writeAndFlush(stdout, changed ? "changed\n" : "unchanged\n");
    return 0;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList arguments = app.arguments().mid(1);
    if (arguments.isEmpty()) {
        printError(QStringLiteral("Missing helper action"));
        return 1;
    }

    const QString action = arguments.constFirst();
    const QStringList remainingArgs = arguments.mid(1);

    if (action == QStringLiteral("install-chrony-sources") || action == QStringLiteral("retire-inline-sources")) {
        return handleChronyFile(action, remainingArgs);
    }
    if (action == QStringLiteral("set-hwclock-mode")) {
        return handleSetHwclockMode(remainingArgs);
    }
    if (action == QStringLiteral("set-localtime-link")) {
        return handleSetLocaltimeLink(remainingArgs);
    }
    if (action == QStringLiteral("write-timezone")) {
        return handleWriteTimezone(remainingArgs);
    }

    return runOperation(action, remainingArgs);
}
