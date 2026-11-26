/**********************************************************************
 *  MX Date/Time application.
 **********************************************************************
 *   Copyright (C) 2019 by AK-47
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

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTextCharFormat>
#include <QTimeZone>

#include "about.h"
#include "datetime.h"
#include "chronypaths.h"
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace
{
const QString HelperPath = QStringLiteral(HELPER_PATH);

QString elevationProgram()
{
    const QString pkexec = QStandardPaths::findExecutable(u"pkexec"_s);
    if (!pkexec.isEmpty()) {
        return pkexec;
    }
    const QString gksu = QStandardPaths::findExecutable(u"gksu"_s);
    if (!gksu.isEmpty()) {
        return gksu;
    }
    return {};
}

bool hasEnabledSystemVChronyService()
{
    const QDir etcDir(u"/etc"_s);
    const QStringList rcDirs = etcDir.entryList({u"rc*.d"_s}, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &rcDirName : rcDirs) {
        const QDir rcDir(etcDir.filePath(rcDirName));
        const QStringList entries = rcDir.entryList({u"S*"_s}, QDir::Files | QDir::System | QDir::Hidden);
        for (const QString &entry : entries) {
            if (entry.contains(u"chrony"_s)) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

MXDateTime::MXDateTime(QWidget *parent)
    : QDialog(parent),
      updater(this)
{
    setupUi(this);
    setClockLock(true);
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    QTextCharFormat tcfmt;
    tcfmt.setFontPointSize(calendar->font().pointSizeF() * 0.75);
    calendar->setHeaderTextFormat(tcfmt);
    connect(pushClose, &QPushButton::clicked, this, &MXDateTime::close);
    connect(tabsDateTime, &QTabWidget::currentChanged, this, &MXDateTime::loadTab);
    // This runs the slow startup tasks after the GUI is displayed.
    QTimer::singleShot(0, this, &MXDateTime::startup);
}

void MXDateTime::startup()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    timeEdit->setTimeZone(QTimeZone::systemTimeZone());  // TODO: Implement time zone differences properly.
#else
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    timeEdit->setTimeSpec(Qt::LocalTime);                // TODO: Implement time zone differences properly.
    #pragma GCC diagnostic pop
#endif
    timeEdit->setDateTime(QDateTime::currentDateTime()); // Curtail the sudden jump.
    connect(pushReadHardware, &QPushButton::clicked, this, &MXDateTime::readHardwareClock);

    // Make the NTP server table columns the right proportions.
    int colSizes[3];
    addServerRow(true, QString(), QString(), QString());
    tableServers->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    for (int ixi = 0; ixi < 3; ++ixi) {
        colSizes[ixi] = tableServers->columnWidth(ixi);
    }
    tableServers->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    for (int ixi = 0; ixi < 3; ++ixi) {
        tableServers->setColumnWidth(ixi, colSizes[ixi]);
    }
    tableServers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    tableServers->removeRow(0);

    // Server page slots
    connect(pushServerMoveUp, &QPushButton::clicked, pushServerMoveUp, [this]() { moveServerRow(-1); });
    connect(pushServerMoveDown, &QPushButton::clicked, pushServerMoveDown, [this]() { moveServerRow(1); });
    connect(tableServers, &QTableWidget::itemChanged, this, &MXDateTime::serverRowChanged);

    // Used to decide the type of commands to run on this system.
    if (QFile::exists(u"/run/openrc"_s)) {
        sysInit = OpenRC;
    } else {
        QFile proc1comm(u"/proc/1/comm"_s);
        if (proc1comm.open(QIODevice::ReadOnly)) {
            if (proc1comm.readAll().contains("systemd")) {
                sysInit = SystemD;
            }
            proc1comm.close();
        }
    }
    static const char *sysInitNames[] = {"SystemV", "OpenRC", "SystemD"};
    qDebug() << "Init system:" << sysInitNames[sysInit];

    // Time zone areas.
    QByteArray zoneOut;
    execute(u"find"_s,
        {u"-L"_s, u"/usr/share/zoneinfo"_s, u"-mindepth"_s, u"2"_s, u"!"_s,
            u"-path"_s, u"*/posix/*"_s, u"!"_s, u"-path"_s, u"*/right/*"_s,
            u"-type"_s, u"f"_s, u"-printf"_s, u"%P\n"_s},
        &zoneOut);
    zones = zoneOut.trimmed().split('\n');
    comboTimeZone->blockSignals(true); // Keep blocked until loadSysTimeConfig().
    comboTimeArea->clear();
    for (const QByteArray &zone : std::as_const(zones)) {
        const QString &area = QString(zone).section('/', 0, 0);
        if (comboTimeArea->findData(area) < 0) {
            QString text(area);
            if (area == "Indian"_L1 || area == "Pacific"_L1 || area == "Atlantic"_L1 || area == "Arctic"_L1) {
                text.append(" Ocean");
            }
            comboTimeArea->addItem(text, area);
        }
    }
    comboTimeArea->model()->sort(0);

    // Connect editing controls once. Applying changes reloads tab data.
    connect(comboTimeArea, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MXDateTime::timeAreaIndexChanged);
    connect(comboTimeZone, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &MXDateTime::timeZoneIndexChanged);
    connect(calendar, &QCalendarWidget::selectionChanged, this, &MXDateTime::calendarSelectionChanged);
    connect(timeEdit, &MTimeEdit::dateTimeChanged, this, &MXDateTime::timeEditDateTimeChanged);
    connect(pushHardwareAdjust, &QPushButton::clicked, this, &MXDateTime::hardwareAdjustClicked);
    connect(pushSystemToHardware, &QPushButton::clicked, this, &MXDateTime::systemToHardwareClicked);
    connect(pushHardwareToSystem, &QPushButton::clicked, this, &MXDateTime::hardwareToSystemClicked);
    connect(pushSyncNow, &QPushButton::clicked, this, &MXDateTime::syncNowClicked);
    connect(tableServers, &QTableWidget::itemSelectionChanged,
        this, &MXDateTime::serversItemSelectionChanged);
    connect(pushServerAdd, &QPushButton::clicked, this, &MXDateTime::serverAddClicked);
    connect(pushServerRemove, &QPushButton::clicked, this, &MXDateTime::serverRemoveClicked);

    // Prepare the GUI.
    connect(pushAbout, &QPushButton::clicked, this, &MXDateTime::aboutClicked);
    connect(pushHelp, &QPushButton::clicked, this, &MXDateTime::helpClicked);
    connect(pushApply, &QPushButton::clicked, this, &MXDateTime::applyClicked);
    setClockLock(false);
    // Setup the display update timer.
    connect(&updater, &QTimer::timeout, this, QOverload<>::of(&MXDateTime::update));
    updater.start(0);
}
void MXDateTime::setClockLock(bool locked)
{
    if (clockLock != locked) {
        clockLock = locked;
        if (locked) {
            qApp->setOverrideCursor(QCursor(Qt::BusyCursor));
        } else {
            loadTab(tabsDateTime->currentIndex());
        }
        tabsDateTime->blockSignals(locked);
        tabDateTime->setDisabled(locked);
        tabHardware->setDisabled(locked);
        tabNetwork->setDisabled(locked);
        pushApply->setDisabled(locked);
        pushClose->setDisabled(locked);
        if (!locked) {
            qApp->restoreOverrideCursor();
        }
    }
}
bool MXDateTime::execute(const QString &program, const QStringList &arguments, QByteArray *output, QByteArray *error,
                         const QByteArray &input)
{
    qDebug() << "Exec:" << program << arguments;
    QProcess proc(this);
    QEventLoop eloop;
    bool failedToStart = false;
    connect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), &eloop, &QEventLoop::quit);
    connect(&proc, &QProcess::errorOccurred, &eloop, [&](QProcess::ProcessError processError) {
        if (processError == QProcess::FailedToStart) {
            failedToStart = true;
            eloop.quit();
        }
    });
    proc.start(program, arguments);
    if (!output) {
        proc.closeReadChannel(QProcess::StandardOutput);
    }
    if (!input.isEmpty()) {
        proc.write(input);
    }
    proc.closeWriteChannel();
    eloop.exec();
    disconnect(&proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), nullptr, nullptr);
    const QByteArray &sout = proc.readAllStandardOutput();
    if (output) {
        output->append(sout);
    } else if (!sout.isEmpty()) {
        qDebug() << "SOut:" << sout;
    }
    QByteArray serr = proc.readAllStandardError();
    if (failedToStart && serr.isEmpty()) {
        serr = proc.errorString().toUtf8();
    }
    if (error) {
        error->append(serr);
    } else if (!serr.isEmpty()) {
        qDebug() << "SErr:" << serr;
    }
    if (!failedToStart) {
        qDebug() << "Exit:" << proc.exitCode() << proc.exitStatus();
    }
    return !failedToStart && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

bool MXDateTime::runHelper(const QStringList &arguments, QByteArray *output, QByteArray *error, const QByteArray &input)
{
    qDebug() << "Helper:" << arguments;
    QString program = HelperPath;
    QStringList programArgs = arguments;
    if (getuid() != 0) {
        program = elevationProgram();
        if (program.isEmpty()) {
            if (error) {
                *error = tr("No suitable elevation command found (pkexec or gksu).").toUtf8();
            }
            qWarning() << "No suitable elevation command found (pkexec or gksu)";
            return false;
        }
        programArgs.prepend(HelperPath);
    }
    return execute(program, programArgs, output, error, input);
}

bool MXDateTime::installManagedFileAsRoot(const QByteArray &content, const QString &destinationPath, QByteArray *error)
{
    return runHelper({u"install-chrony-sources"_s, destinationPath}, nullptr, error, content);
}

bool MXDateTime::setHwclockModeAsRoot(bool utc, QByteArray *error)
{
    return runHelper({u"set-hwclock-mode"_s, utc ? u"utc"_s : u"local"_s}, nullptr, error);
}

bool MXDateTime::setLocaltimeLinkAsRoot(const QString &timeZone, QByteArray *error)
{
    return runHelper({u"set-localtime-link"_s, timeZone}, nullptr, error);
}

bool MXDateTime::writeTimeZoneAsRoot(const QString &timeZone, QByteArray *error)
{
    return runHelper({u"write-timezone"_s, timeZone}, nullptr, error);
}

void MXDateTime::loadTab(int index)
{
    const unsigned int loaded = 1 << index;
    if ((loadedTabs & loaded) == 0) {
        loadedTabs |= loaded;
        setClockLock(true);
        switch (index) {
        case 0:
            loadDateTime();
            break; // Date & Time.
        case 1:
            readHardwareClock();
            break; // Hardware Clock.
        case 2:
            loadNetworkTime();
            break; // Network Time.
        }
        setClockLock(false);
    }
}

// DATE & TIME

void MXDateTime::timeAreaIndexChanged(int index)
{
    if (index < 0 || index >= comboTimeArea->count()) {
        return;
    }
    const QByteArray &area = comboTimeArea->itemData(index).toByteArray();
    comboTimeZone->clear();
    for (const QByteArray &zone : std::as_const(zones)) {
        if (zone.startsWith(area)) {
            QString text(QString(zone).section('/', 1));
            text.replace('_', ' ');
            comboTimeZone->addItem(text, QVariant(zone));
        }
    }
    comboTimeZone->model()->sort(0);
}
void MXDateTime::timeZoneIndexChanged(int index)
{
    if (index < 0 || index >= comboTimeZone->count()) {
        return;
    }
    // Calculate and store the difference between current and newly selected time zones.
    const QDateTime &current = QDateTime::currentDateTime();
    zoneDelta = QTimeZone(comboTimeZone->itemData(index).toByteArray()).offsetFromUtc(current)
                - QTimeZone::systemTimeZone().offsetFromUtc(current); // Delta = new - old
    // Check IANA-zone id differ
    const QByteArray &currentTimeZone = QTimeZone::systemTimeZoneId();
    const QByteArray &selectedTimeZone = QTimeZone(comboTimeZone->itemData(index).toByteArray()).id();
    zoneIdChanged = (currentTimeZone != selectedTimeZone);
    // Make the change immediately visible
    update();
}
void MXDateTime::calendarSelectionChanged()
{
    dateDelta = static_cast<int>(timeEdit->date().daysTo(calendar->selectedDate()));
}
void MXDateTime::timeEditDateTimeChanged(const QDateTime &dateTime)
{
    clock->setTime(dateTime.time());
    if (!updating) {
        timeDelta = QDateTime::currentDateTime().secsTo(dateTime) - zoneDelta;
        if (abs(dateDelta * 86400 + timeDelta) == 316800) {
            setWindowTitle(u"88 MILES PER HOUR"_s);
        }
    }
}

void MXDateTime::update()
{
    updating = true;
    timeEdit->updateDateTime(QDateTime::currentDateTime().addSecs(timeDelta + zoneDelta));
    updater.setInterval(1000 - QTime::currentTime().msec());
    if (timeEdit->date().daysTo(calendar->selectedDate()) != dateDelta) {
        calendar->setSelectedDate(timeEdit->date().addDays(dateDelta));
    }
    updating = false;
}

void MXDateTime::loadDateTime()
{
    // Time zone.
    comboTimeZone->blockSignals(true);
    const QByteArray &zone = QTimeZone::systemTimeZoneId();
    int index = comboTimeArea->findData(QString(zone).section('/', 0, 0));
    const QSignalBlocker loadingArea(comboTimeArea);
    comboTimeArea->setCurrentIndex(index);
    timeAreaIndexChanged(index);
    index = comboTimeZone->findData(QVariant(zone));
    comboTimeZone->setCurrentIndex(index);
    zoneDelta = 0;
    zoneIdChanged = false;
    comboTimeZone->blockSignals(false);
}
bool MXDateTime::saveDateTime() {
    if (!zoneIdChanged && !zoneDelta && pendingTimeZoneFile.isEmpty() && !dateDelta && !timeDelta) {
        return true;
    }
    updater.stop();
    const auto restartTimer = qScopeGuard([this] { updater.start(0); });

    QByteArray error;
    if (zoneIdChanged || zoneDelta) {
        const QString zone = comboTimeZone->currentData().toString();
        const bool applied = sysInit == SystemD ? runHelper({u"set-zone"_s, zone}, nullptr, &error)
                                                : setLocaltimeLinkAsRoot(zone, &error);
        if (!applied) {
            return showSaveError(tr("Could not set the system time zone to %1.").arg(zone), error);
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        const QSignalBlocker syncingZone(timeEdit);
        timeEdit->setTimeZone(QTimeZone(zone.toUtf8()));
        timeEdit->updateDateTime(QDateTime::currentDateTime().addSecs(timeDelta));
        clock->setTime(timeEdit->time());
#endif
        pendingTimeZoneFile = zone;
        zoneDelta = 0;
        zoneIdChanged = false;
    }
    if (!pendingTimeZoneFile.isEmpty()) {
        error.clear();
        if (!writeTimeZoneAsRoot(pendingTimeZoneFile, &error)) {
            return showSaveError(tr("Could not write the time zone to /etc/timezone."), error);
        }
        pendingTimeZoneFile.clear();
    }

    if (dateDelta || timeDelta) {
        // Build the requested local time in the applied zone, including time spent authenticating.
        const QDateTime desired = QDateTime::currentDateTime().addSecs(timeDelta).addDays(dateDelta);
        const QString value = desired.toString(u"yyyy-MM-ddTHH:mm:ss.zzz"_s);
        error.clear();
        if (!runHelper({sysInit == SystemD ? u"systemd-set-time"_s : u"set-time"_s, value}, nullptr, &error)) {
            return showSaveError(tr("Could not set the system date and time."), error);
        }
        dateDelta = 0;
        timeDelta = 0;
    }
    return true;
}

// HARDWARE CLOCK

void MXDateTime::readHardwareClock()
{
    setClockLock(true);
    const QString btext = pushReadHardware->text();
    pushReadHardware->setText(tr("Reading..."));
    QByteArray rtcout;

    QByteArray error;
    isHardwareUTC.reset();
    const bool readClock = runHelper({u"read-rtc"_s}, &rtcout, &error);
    if (sysInit == SystemD) {
        QByteArray mode;
        if (execute(u"timedatectl"_s, {u"show"_s, u"--property=LocalRTC"_s, u"--value"_s}, &mode, &error)) {
            if (mode.trimmed() == "no") {
                isHardwareUTC = true;
            } else if (mode.trimmed() == "yes") {
                isHardwareUTC = false;
            }
        }
    } else if (readClock) {
        const auto lines = rtcout.split('\n');
        if (lines.contains("Assuming hardware clock is kept in UTC time.")) {
            isHardwareUTC = true;
        } else if (lines.contains("Assuming hardware clock is kept in local time.")) {
            isHardwareUTC = false;
        }
    }

    boxHardwareZone->setEnabled(isHardwareUTC.has_value());
    if (isHardwareUTC.has_value()) {
        (*isHardwareUTC ? radioHardwareUTC : radioHardwareLocal)->setChecked(true);
    } else {
        for (auto *button : {radioHardwareUTC, radioHardwareLocal}) {
            button->setAutoExclusive(false);
            button->setChecked(false);
            button->setAutoExclusive(true);
        }
    }
    QStringList details;
    if (!isHardwareUTC.has_value()) {
        details << tr("The hardware clock mode is unknown.");
    }
    if (!rtcout.trimmed().isEmpty()) {
        details << QString::fromUtf8(rtcout.trimmed());
    }
    if (!error.trimmed().isEmpty()) {
        details << QString::fromUtf8(error.trimmed());
    }
    textHardwareClock->setPlainText(details.join('\n'));
    pushReadHardware->setText(btext);
    setClockLock(false);
}
void MXDateTime::hardwareAdjustClicked()
{
    setClockLock(true);
    const QString btext = pushHardwareAdjust->text();
    pushHardwareAdjust->setText(tr("Adjusting..."));
    QByteArray rtcout;
    runHelper({u"adjust-rtc"_s}, &rtcout);
    textHardwareClock->setPlainText(QString(rtcout.trimmed()));
    pushHardwareAdjust->setText(btext);
    setClockLock(false);
}
void MXDateTime::systemToHardwareClicked()
{
    setClockLock(true);
    const QString action = checkDriftUpdate->isChecked() ? u"copy-to-rtc-update-drift"_s : u"copy-to-rtc"_s;
    transferTime(action, tr("System Clock"), tr("Hardware Clock"));
    checkDriftUpdate->setCheckState(Qt::Unchecked);
    setClockLock(false);
}
void MXDateTime::hardwareToSystemClicked()
{
    setClockLock(true);
    transferTime(u"copy-to-system"_s, tr("Hardware Clock"), tr("System Clock"));
    setClockLock(false);
}
void MXDateTime::transferTime(const QString &action, const QString &from, const QString &to)
{
    if (runHelper({action})) {
        const QString &msg = tr("The %1 time was transferred to the %2.");
        QMessageBox::information(this, windowTitle(), msg.arg(from, to));
    } else {
        const QString &msg = tr("The %1 time could not be transferred to the %2.");
        QMessageBox::warning(this, windowTitle(), msg.arg(from, to));
    }
}

void MXDateTime::saveHardwareClock()
{
    const bool rtcUTC = radioHardwareUTC->isChecked();
    if (isHardwareUTC.has_value() && rtcUTC != *isHardwareUTC) {
        if (sysInit == SystemD) {
            runHelper({rtcUTC ? u"systemd-rtc-utc"_s : u"systemd-rtc-local"_s});
        } else {
            if (sysInit == OpenRC && QFile::exists(u"/etc/conf.d/hwclock"_s)) {
                setHwclockModeAsRoot(rtcUTC);
            }
            runHelper({rtcUTC ? u"rtc-utc"_s : u"rtc-local"_s});
        }
    }
}

// NETWORK TIME

void MXDateTime::syncNowClicked()
{
    if (!validateServerList()) {
        return;
    }
    setClockLock(true);

    if (!saveNetworkTime()) {
        setClockLock(false);
        return;
    }
    QByteArray output;
    bool rexit = false;
    if (enabledNTP) {
        // All commands can be passed via stdin, but chronyc may return 0 even if one command fails.
        rexit = runHelper({u"ntp-burst"_s}, &output);
        if (rexit) {
            rexit = execute(u"chronyc"_s, {u"waitsync 10"_s}, &output);
        }
        if (rexit) {
            rexit = runHelper({u"ntp-step"_s}, &output);
        }
    } else {
        rexit = runHelper({u"ntp-once"_s}, nullptr, &output);
    }

    setClockLock(false);
    dateDelta = 0;
    timeDelta = 0;
    updater.setInterval(0);

    QMessageBox msgbox(this);
    if (rexit) {
        msgbox.setIcon(QMessageBox::Information);
        msgbox.setText(tr("The system clock was updated successfully."));
    } else {
        msgbox.setIcon(QMessageBox::Critical);
        msgbox.setText(tr("The system clock could not be updated."));
    }
    msgbox.setDetailedText(output.trimmed());
    msgbox.exec();
}
void MXDateTime::serversItemSelectionChanged()
{
    const QList<QTableWidgetSelectionRange> &ranges = tableServers->selectedRanges();
    bool remove = false;
    bool up = false;
    bool down = false;
    if (ranges.count() == 1) {
        const QTableWidgetSelectionRange &range = ranges.at(0);
        remove = true;
        if (range.topRow() > 0) {
            up = true;
        }
        if (range.bottomRow() < (tableServers->rowCount() - 1)) {
            down = true;
        }
    }
    pushServerRemove->setEnabled(remove);
    pushServerMoveUp->setEnabled(up);
    pushServerMoveDown->setEnabled(down);
}
void MXDateTime::serverAddClicked()
{
    QTableWidgetItem *item = addServerRow(true, u"server"_s, QString(), QString());
    tableServers->setCurrentItem(item);
    tableServers->editItem(item);
}
void MXDateTime::serverRemoveClicked()
{
    const QList<QTableWidgetSelectionRange> &ranges = tableServers->selectedRanges();
    for (int ixi = ranges.count() - 1; ixi >= 0; --ixi) {
        const int top = ranges.at(ixi).topRow();
        for (int row = ranges.at(ixi).bottomRow(); row >= top; --row) {
            tableServers->removeRow(row);
        }
    }
    changedServers = true;
}

QTableWidgetItem *MXDateTime::addServerRow(bool enabled, const QString &type, const QString &address,
                                           const QString &options)
{
    auto *itemComboType = new QComboBox(tableServers);
    auto *item = new QTableWidgetItem(address);
    auto *itemOptions = new QTableWidgetItem(options);
    itemComboType->addItem(u"Pool"_s, QVariant("pool"));
    itemComboType->addItem(u"Server"_s, QVariant("server"));
    itemComboType->addItem(u"Peer"_s, QVariant("peer"));
    itemComboType->setCurrentIndex(itemComboType->findData(QVariant(type)));
    connect(itemComboType, &QComboBox::currentTextChanged, this, &MXDateTime::serverRowChanged);
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    const int newRow = tableServers->rowCount();
    tableServers->insertRow(newRow);
    tableServers->setCellWidget(newRow, 0, itemComboType);
    tableServers->setItem(newRow, 1, item);
    tableServers->setItem(newRow, 2, itemOptions);
    return item;
}
void MXDateTime::moveServerRow(int movement)
{
    const QList<QTableWidgetSelectionRange> &ranges = tableServers->selectedRanges();
    if (ranges.count() == 1) {
        const QTableWidgetSelectionRange &range = ranges.at(0);
        int end;
        int row;
        if (movement < 0) {
            row = range.topRow();
            end = range.bottomRow();
        } else {
            row = range.bottomRow();
            end = range.topRow();
        }
        row += movement;
        // Save the original row contents.
        int targetType = qobject_cast<QComboBox *>(tableServers->cellWidget(row, 0))->currentIndex();
        QTableWidgetItem *targetItemAddress = tableServers->takeItem(row, 1);
        QTableWidgetItem *targetItemOptions = tableServers->takeItem(row, 2);
        // Update the list selection.
        const QTableWidgetSelectionRange targetRange(row, range.leftColumn(), end + movement, range.rightColumn());
        tableServers->setCurrentItem(nullptr);
        tableServers->setRangeSelected(targetRange, true);
        // Move items one by one.
        do {
            row -= movement;
            int type = qobject_cast<QComboBox *>(tableServers->cellWidget(row, 0))->currentIndex();
            QTableWidgetItem *itemAddress = tableServers->takeItem(row, 1);
            QTableWidgetItem *itemOptions = tableServers->takeItem(row, 2);
            const int step = row + movement;
            qobject_cast<QComboBox *>(tableServers->cellWidget(step, 0))->setCurrentIndex(type);
            tableServers->setItem(step, 1, itemAddress);
            tableServers->setItem(step, 2, itemOptions);
        } while (row != end);
        // Move the target where the range originally finished.
        qobject_cast<QComboBox *>(tableServers->cellWidget(end, 0))->setCurrentIndex(targetType);
        tableServers->setItem(end, 1, targetItemAddress);
        tableServers->setItem(end, 2, targetItemOptions);
    }
}
bool MXDateTime::validateServerList()
{
    bool allValid = true;
    const int serverCount = tableServers->rowCount();
    for (int ixi = 0; ixi < serverCount; ++ixi) {
        QTableWidgetItem *item = tableServers->item(ixi, 1);
        const QString &address = item->text().trimmed();
        if (address.isEmpty()) {
            allValid = false;
        }
    }
    const char *msg = nullptr;
    if (serverCount <= 0) {
        msg = QT_TR_NOOP("There are no NTP servers on the list.");
    } else if (!allValid) {
        msg = QT_TR_NOOP("There are invalid entries on the NTP server list.");
    }
    if (msg) {
        QMessageBox::critical(this, windowTitle(), tr(msg));
        return false;
    }
    return true;
}

QString MXDateTime::systemdChronyUnit()
{
    const QStringList candidates {u"chronyd.service"_s, u"chrony.service"_s};
    for (const QString &unit : candidates) {
        QByteArray output;
        execute(u"systemctl"_s, {u"show"_s, u"-p"_s, u"LoadState"_s, u"--value"_s, unit}, &output, nullptr);
        if (QString::fromUtf8(output).trimmed() == u"loaded"_s) {
            return unit;
        }
    }
    return {};
}

void MXDateTime::loadNetworkTime()
{
    while (tableServers->rowCount() > 0) {
        tableServers->removeRow(0);
    }
    const QString sourcesFile = Chrony::chronySourcesFile();
    const QString configFile = Chrony::chronyConfigFile();
    loadSources(sourcesFile);
    inlineSources.clear();
    const bool move = loadSources(configFile, &inlineSources);

    // Stray change signals may have caused changedServers to be set.
    QApplication::processEvents();
    changedServers = move;

    if (sysInit != SystemD) {
        enabledNTP = hasEnabledSystemVChronyService();
    } else {
        const QString unit = systemdChronyUnit();
        if (unit.isEmpty()) {
            enabledNTP = false;
        } else {
            QByteArray output;
            execute(u"systemctl"_s, {u"show"_s, u"-p"_s, u"UnitFileState"_s, u"--value"_s, unit}, &output, nullptr);
            const QString state = QString::fromUtf8(output).trimmed();
            enabledNTP = (state == u"enabled"_s || state == u"enabled-runtime"_s);
        }
    }
    checkAutoSync->setChecked(enabledNTP);
}
bool MXDateTime::showSaveError(const QString &message, const QByteArray &details) {
    QMessageBox box(QMessageBox::Critical, windowTitle(), message, QMessageBox::Ok, this);
    if (!details.isEmpty()) {
        box.setDetailedText(QString::fromUtf8(details.trimmed()));
    }
    box.exec();
    return false;
}

bool MXDateTime::saveNetworkTime()
{
    // Enable/disable NTP
    const bool ntp = checkAutoSync->isChecked();
    if (ntp != enabledNTP) {
        const QString enable_disable(ntp ? u"enable"_s : u"disable"_s);
        const QString start_stop(ntp ? u"start"_s : u"stop"_s);
        if (sysInit == SystemD) {
            const QString unit = systemdChronyUnit();
            if (!unit.isEmpty()) {
                runHelper({u"systemd-"_s + enable_disable, unit});
                runHelper({u"systemd-"_s + start_stop, unit});
            }
        } else if (sysInit == OpenRC) {
            if (QFile::exists(u"/etc/init.d/chronyd"_s)) {
                runHelper({u"openrc-"_s + enable_disable});
            }
        } else {
            runHelper({u"sysv-"_s + enable_disable});
            runHelper({u"sysv-"_s + start_stop});
        }
        enabledNTP = ntp;
    }

    // Generate chrony sources file.
    if (changedServers) {
        const QString sourcesFile = Chrony::chronySourcesFile();
        const QString configFile = Chrony::chronyConfigFile();
        QByteArray content("# Generated by MX Date & Time - ");
        content.append(QDateTime::currentDateTime().toString(u"yyyy-MM-dd H:mm:ss t"_s).toUtf8());
        content.append('\n');
        for (int ixi = 0; ixi < tableServers->rowCount(); ++ixi) {
            auto *comboType = qobject_cast<QComboBox *>(tableServers->cellWidget(ixi, 0));
            QTableWidgetItem *item = tableServers->item(ixi, 1);
            if (item->checkState() != Qt::Checked) {
                content.append('#');
            }
            content.append(comboType->currentData().toByteArray());
            content.append(' ');
            content.append(item->text().trimmed().toUtf8());
            const QString &options = tableServers->item(ixi, 2)->text().simplified();
            if (!options.isEmpty()) {
                content.append(' ');
                content.append(options.toUtf8());
            }
            content.append('\n');
        }
        QByteArray error;
        if (!installManagedFileAsRoot(content, sourcesFile, &error)) {
            return showSaveError(tr("Could not save the time sources to %1.").arg(sourcesFile), error);
        }
        QByteArray result;
        if (!runHelper({u"activate-chrony-sources"_s, sourcesFile}, &result, &error, inlineSources)) {
            return showSaveError(tr("Could not update the time sources in %1.").arg(configFile), error);
        }
        inlineSources.clear();
        if (ntp) {
            chronyNeedsRestart |= result.trimmed() == "changed";
            QStringList action {u"reload-sources"_s};
            if (chronyNeedsRestart) {
                if (sysInit == SystemD) {
                    const QString unit = systemdChronyUnit();
                    if (unit.isEmpty()) {
                        return showSaveError(tr("Could not find the chrony service to restart."), {});
                    }
                    action = {u"systemd-restart"_s, unit};
                } else {
                    action = {u"sysv-restart"_s};
                }
            }
            if (!runHelper(action, nullptr, &error)) {
                return showSaveError(tr("Could not activate the saved time sources."), error);
            }
        }
        chronyNeedsRestart = false;
        changedServers = false;
    }
    return true;
}

bool MXDateTime::loadSources(const QString &filename, QByteArray *activeLines)
{
    QFile file(filename);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return false;
    }
    const auto findSource = [this](const QString &type, const QString &address, const QString &options) -> QTableWidgetItem * {
        const int serverCount = tableServers->rowCount();
        for (int ixi = 0; ixi < serverCount; ++ixi) {
            auto *comboType = qobject_cast<QComboBox *>(tableServers->cellWidget(ixi, 0));
            if (!comboType) {
                continue;
            }
            const QString rowType = comboType->currentData().toString();
            auto *addressItem = tableServers->item(ixi, 1);
            const QString rowAddress = addressItem->text().trimmed();
            const QString rowOptions = tableServers->item(ixi, 2)->text().simplified();
            if (rowType == type && rowAddress == address && rowOptions == options) {
                return addressItem;
            }
        }
        return nullptr;
    };
    bool hassrc = false;
    while (!file.atEnd()) {
        const QByteArray &bline = file.readLine();
        const QString line(bline.simplified());
        static const QRegularExpression tregex(u"^#?(pool|server|peer)\\s"_s);
        if (line.contains(tregex)) {
            QStringList args = line.split(' ', Qt::SkipEmptyParts);
            QString curarg = args.at(0);
            bool enabled = true;
            if (curarg.startsWith('#')) {
                enabled = false;
                curarg = curarg.remove(0, 1);
            }
            if (enabled && activeLines) {
                activeLines->append(bline.trimmed() + '\n');
            }
            QString options;
            for (int ixi = 2; ixi < args.count(); ++ixi) {
                options.append(' ');
                options.append(args.at(ixi));
            }
            const QString address = args.at(1);
            const QString trimmedOptions = options.trimmed();
            if (auto *existing = findSource(curarg, address, trimmedOptions)) {
                if (enabled) {
                    existing->setCheckState(Qt::Checked);
                }
            } else {
                addServerRow(enabled, curarg, address, trimmedOptions);
            }
            hassrc = true;
        }
    }
    return hassrc;
}

void MXDateTime::serverRowChanged()
{
    changedServers = true;
}

// ACTION BUTTONS

void MXDateTime::applyClicked()
{
    setClockLock(true);

    // Validate all data before trying to save settings.
    if ((loadedTabs & 4) && changedServers && !validateServerList()) {
        setClockLock(false);
        return;
    }

    // Save the settings.
    if (!saveDateTime()) {
        setClockLock(false);
        return;
    }
    if (loadedTabs & 2) {
        saveHardwareClock();
    }
    if ((loadedTabs & 4) && !saveNetworkTime()) {
        setClockLock(false);
        return;
    }

    // Refresh the UI (especially the current tab) with newly set values.
    loadedTabs = 0;
    setClockLock(false);
}

// MX Standard User Interface
void MXDateTime::aboutClicked()
{
    displayAboutMsgBox(tr("About MX Date & Time"),
        u"<p align=\"center\"><b><h2>"_s + tr("MX Date & Time") + u"</h2></b></p><p align=\"center\">"_s
        + tr("Version: ") + qApp->applicationVersion() + u"</p><p align=\"center\"><h3>"_s
        + tr("GUI program for setting the time and date in MX Linux")
        + u"</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br/>"_s
            u"</p><p align=\"center\">"_s
        + tr("Copyright (c) MX Linux") + u"<br /><br /></p>"_s,
        u"/usr/share/doc/mx-datetime/license.html"_s, tr("%1 License").arg(this->windowTitle()));
}
void MXDateTime::helpClicked()
{
    displayHelpDoc(u"/usr/share/doc/mx-datetime/mx-datetime.html"_s, tr("MX Date & Time Help"));
}

// SUBCLASSING FOR QTimeEdit THAT FIXES CURSOR AND SELECTION JUMPING EVERY SECOND

void MTimeEdit::updateDateTime(const QDateTime &dateTime)
{
    QLineEdit *ledit = lineEdit();
    // Original cursor position and selections
    int select = ledit->selectionStart();
    int cursor = ledit->cursorPosition();
    // Calculation for backward selections.
    if (select >= 0 && select >= cursor) {
        const int cslength = ledit->selectedText().length();
        if (cslength > 0) {
            select = cursor + cslength;
        }
    }
    // Set the date/time as normal.
    setDateTime(dateTime);
    // Restore cursor and selection.
    if (select >= 0) {
        ledit->setSelection(select, cursor - select);
    } else {
        ledit->setCursorPosition(cursor);
    }
}
