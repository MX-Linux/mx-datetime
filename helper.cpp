/**********************************************************************
 *  MX Date/Time helper.
 **********************************************************************
 *   Copyright (C) 2026 by MX Authors
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
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>

#include <cstdio>

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

[[nodiscard]] const QHash<QString, QStringList> &allowedCommands()
{
    static const QHash<QString, QStringList> commands {
        {"chmod", {"/usr/bin/chmod", "/bin/chmod"}},
        {"chown", {"/usr/bin/chown", "/bin/chown"}},
        {"chronyc", {"/usr/bin/chronyc", "/bin/chronyc"}},
        {"chronyd", {"/usr/sbin/chronyd", "/sbin/chronyd", "/usr/bin/chronyd"}},
        {"date", {"/usr/bin/date", "/bin/date"}},
        {"hwclock", {"/usr/sbin/hwclock", "/sbin/hwclock", "/usr/bin/hwclock"}},
        {"ln", {"/usr/bin/ln", "/bin/ln"}},
        {"mkdir", {"/usr/bin/mkdir", "/bin/mkdir"}},
        {"mv", {"/usr/bin/mv", "/bin/mv"}},
        {"rc-update", {"/usr/sbin/rc-update", "/sbin/rc-update", "/usr/bin/rc-update"}},
        {"sed", {"/usr/bin/sed", "/bin/sed"}},
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

[[nodiscard]] int handleExec(const QStringList &arguments)
{
    if (arguments.isEmpty()) {
        printError(QStringLiteral("exec requires a command name"));
        return 1;
    }

    const QString command = arguments.constFirst();
    const auto commandIt = allowedCommands().constFind(command);
    if (commandIt == allowedCommands().constEnd()) {
        printError(QString("Command is not allowed: %1").arg(command));
        return 127;
    }

    const QString executable = resolveBinary(commandIt.value());
    if (executable.isEmpty()) {
        printError(QString("Command is not available: %1").arg(command));
        return 127;
    }

    return relayResult(runProcess(executable, arguments.mid(1)));
}

[[nodiscard]] int handleWriteTimezone(const QStringList &arguments)
{
    if (arguments.size() != 1) {
        printError(QStringLiteral("write-timezone requires exactly one value"));
        return 1;
    }

    const QString zone = arguments.constFirst().trimmed();
    static const QRegularExpression zoneRx(
        QRegularExpression::anchoredPattern(QStringLiteral(R"([A-Za-z0-9._+-]+(?:/[A-Za-z0-9._+-]+)*)")));
    if (!zoneRx.match(zone).hasMatch()) {
        printError(QString("Invalid timezone value: %1").arg(zone));
        return 1;
    }

    QFile file(QStringLiteral("/etc/timezone"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        printError(QStringLiteral("Unable to write /etc/timezone"));
        return 1;
    }

    if (file.write(zone.toUtf8()) < 0 || file.write("\n") < 0) {
        printError(QStringLiteral("Unable to update /etc/timezone"));
        return 1;
    }

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

    if (action == QStringLiteral("exec")) {
        return handleExec(remainingArgs);
    }
    if (action == QStringLiteral("write-timezone")) {
        return handleWriteTimezone(remainingArgs);
    }

    printError(QString("Unsupported helper action: %1").arg(action));
    return 1;
}
