// MX Date/Time application.
//
//   Copyright (C) 2019 by AK-47
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//
// This file is part of mx-datetime.

#include <unistd.h>
#include <QDebug>
#include <QDateTime>
#include <QTimeZone>
#include <QFileInfo>
#include <QProcess>
#include <QTextCharFormat>
#include <QMessageBox>
#include <QLineEdit>
#include "datetime.h"
#include "version.h"

MXDateTime::MXDateTime(QWidget *parent) :
    QDialog(parent), updater(this)
{
    setupUi(this);
    setClockLock(true);
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    QTextCharFormat tcfmt;
    tcfmt.setFontPointSize(calendar->font().pointSizeF() * 0.75);
    calendar->setHeaderTextFormat(tcfmt);
    // Operate with reduced functionality if not running as root.
    userRoot = (getuid() == 0);
    if (!userRoot) {
        btnAbout->hide();
        btnHelp->hide();
        lblLogo->hide();
        btnApply->hide();
        btnClose->hide();
        tabWidget->tabBar()->hide();
        tabWidget->setDocumentMode(true);
        gridWindow->setMargin(0);
        gridDateTime->setMargin(0);
        gridDateTime->setSpacing(1);
    }
    // This runs the slow startup tasks after the GUI is displayed.
    QTimer::singleShot(0, this, &MXDateTime::startup);
}
MXDateTime::~MXDateTime()
{
}
void MXDateTime::startup()
{
    timeEdit->setDateTime(QDateTime::currentDateTime()); // Curtail the sudden jump.
    if (userRoot) {
        // Make the NTP server table columns the right proportions.
        int colSizes[3];
        addServerRow(true, QString(), QString(), QString());
        tblServers->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        for (int ixi = 0; ixi < 3; ++ixi) colSizes[ixi] = tblServers->columnWidth(ixi);
        tblServers->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        for (int ixi = 0; ixi < 3; ++ixi) tblServers->setColumnWidth(ixi, colSizes[ixi]);
        tblServers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        tblServers->removeRow(0);

        // Used to decide the type of commands to run on this system.
        QByteArray testSystemD;
        if (QFileInfo::exists("/run/openrc")) sysInit = OpenRC;
        else if(QFileInfo("/usr/bin/timedatectl").isExecutable()
                && execute("ps -hp1", &testSystemD) && testSystemD.contains("systemd")) {
            sysInit = SystemD;
        } else sysInit = SystemV;
        static const char *sysInitNames[] = {"SystemV", "OpenRC", "SystemD"};
        qDebug() << "Init system:" << sysInitNames[sysInit];
    }

    // Time zone areas.
    QByteArray zoneOut;
    execute("find -L /usr/share/zoneinfo -mindepth 2 ! -path */posix/* ! -path */right/* -type f -printf %P\\n", &zoneOut);
    zones = zoneOut.trimmed().split('\n');
    cmbTimeZone->blockSignals(true); // Keep blocked until loadSysTimeConfig().
    cmbTimeArea->clear();
    for (const QByteArray &zone : zones) {
        const QString &area = QString(zone).section('/', 0, 0);
        if (cmbTimeArea->findData(area) < 0) {
            QString text(area);
            if (area == "Indian" || area == "Pacific"
                || area == "Atlantic" || area == "Arctic") text.append(" Ocean");
            cmbTimeArea->addItem(text, QVariant(area.toUtf8()));
        }
    }
    cmbTimeArea->model()->sort(0);

    // Prepare the GUI.
    setClockLock(false);
    if (!userRoot) calendar->setFocus();
    // Setup the display update timer.
    connect(&updater, &QTimer::timeout, this, QOverload<>::of(&MXDateTime::update));
    updater.start(0);
}
void MXDateTime::setClockLock(bool locked)
{
    if (clockLock != locked) {
        clockLock = locked;
        if (locked) qApp->setOverrideCursor(QCursor(Qt::BusyCursor));
        else on_tabWidget_currentChanged(tabWidget->currentIndex());
        tabWidget->blockSignals(locked);
        tabDateTime->setDisabled(locked);
        tabHardware->setDisabled(locked);
        tabNetwork->setDisabled(locked);
        btnApply->setDisabled(locked);
        btnClose->setDisabled(locked);
        if (!locked) qApp->restoreOverrideCursor();
    }
}
bool MXDateTime::execute(const QString &cmd, QByteArray *output)
{
    qDebug() << "Exec:" << cmd;
    QProcess proc(this);
    QEventLoop eloop;
    connect(&proc, static_cast<void (QProcess::*)(int)>(&QProcess::finished), &eloop, &QEventLoop::quit);
    proc.start(cmd);
    if (!output) proc.closeReadChannel(QProcess::StandardOutput);
    proc.closeWriteChannel();
    eloop.exec();
    disconnect(&proc, static_cast<void (QProcess::*)(int)>(&QProcess::finished), 0, 0);
    const QByteArray &sout = proc.readAllStandardOutput();
    if (output) *output = sout;
    else if (!sout.isEmpty()) qDebug() << "SOut:" << proc.readAllStandardOutput();
    const QByteArray &serr = proc.readAllStandardError();
    if (!serr.isEmpty()) qDebug() << "SErr:" << serr;
    qDebug() << "Exit:" << proc.exitCode() << proc.exitStatus();
    return (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0);
}

void MXDateTime::on_tabWidget_currentChanged(int index)
{
    const unsigned int loaded = 1 << index;
    if ((loadedTabs & loaded) == 0) {
        loadedTabs |= loaded;
        setClockLock(true);
        switch(index) {
        case 0: loadDateTime(); break; // Date & Time.
        case 1: on_btnReadHardware_clicked(); break; // Hardware Clock.
        case 2: loadNetworkTime(); break; // Network Time.
        }
        setClockLock(false);
    }
}

// DATE & TIME

void MXDateTime::on_cmbTimeArea_currentIndexChanged(int index)
{
    if (index < 0 || index >= cmbTimeArea->count()) return;
    const QByteArray &area = cmbTimeArea->itemData(index).toByteArray();
    cmbTimeZone->clear();
    for (const QByteArray &zone : zones) {
        if (zone.startsWith(area)) {
            QString text(QString(zone).section('/', 1));
            text.replace('_', ' ');
            cmbTimeZone->addItem(text, QVariant(zone));
        }
    }
    cmbTimeZone->model()->sort(0);
}
void MXDateTime::on_cmbTimeZone_currentIndexChanged(int index)
{
    if (index < 0 || index >= cmbTimeZone->count()) return;
    // Calculate and store the difference between current and newly selected time zones.
    const QDateTime &current = QDateTime::currentDateTime();
    zoneDelta = QTimeZone(cmbTimeZone->itemData(index).toByteArray()).offsetFromUtc(current)
              - QTimeZone::systemTimeZone().offsetFromUtc(current); // Delta = new - old
    update(); // Make the change immediately visible
}
void MXDateTime::on_calendar_selectionChanged()
{
    dateDelta = timeEdit->date().daysTo(calendar->selectedDate());
}
void MXDateTime::on_timeEdit_dateTimeChanged(const QDateTime &dateTime)
{
    clock->setTime(dateTime.time());
    if (!updating) {
        timeDelta = QDateTime::currentDateTime().secsTo(dateTime) - zoneDelta;
        if (abs(dateDelta*86400 + timeDelta) == 316800) setWindowTitle("88 MILES PER HOUR");
    }
}

void MXDateTime::update()
{
    updating = true;
    timeEdit->updateDateTime(QDateTime::currentDateTime().addSecs(timeDelta + zoneDelta));
    updater.setInterval(1000 - QTime::currentTime().msec());
    if(timeEdit->date().daysTo(calendar->selectedDate()) != dateDelta) {
        calendar->setSelectedDate(timeEdit->date().addDays(dateDelta));
    }
    updating = false;
}

void MXDateTime::loadDateTime()
{
    // Time zone.
    cmbTimeZone->blockSignals(true);
    const QByteArray &zone = QTimeZone::systemTimeZoneId();
    int index = cmbTimeArea->findData(QVariant(QString(zone).section('/', 0, 0).toUtf8()));
    cmbTimeArea->setCurrentIndex(index);
    qApp->processEvents();
    index = cmbTimeZone->findData(QVariant(zone));
    cmbTimeZone->setCurrentIndex(index);
    zoneDelta = 0;
    cmbTimeZone->blockSignals(false);
}
void MXDateTime::saveDateTime(const QDateTime &driftStart)
{
    // Stop display updates while setting the system clock.
    if (zoneDelta || dateDelta || timeDelta) updater.stop();

    // Set the time zone (if changed) before setting the time.
    if (zoneDelta) {
        const QString newzone(cmbTimeZone->currentData().toByteArray());
        if (sysInit == SystemD) execute("timedatectl set-timezone " + newzone);
        else {
            execute("ln -nfs /usr/share/zoneinfo/" + newzone + " /etc/localtime");
            QFile file("/etc/timezone");
            if (file.open(QFile::WriteOnly | QFile::Text)) {
                file.write(newzone.toUtf8());
                file.close();
            }
        }
        zoneDelta = 0;
    }

    // Set the date and time if their controls have been altered.
    if (dateDelta || timeDelta) {
        QString cmd;
        if (sysInit == SystemD) cmd = "timedatectl set-time ";
        else cmd = "date -s ";
        static const QString dtFormat("yyyy-MM-ddTHH:mm:ss.zzz");
        QDateTime newTime(calendar->selectedDate(),
                          timeEdit->time());
        updater.stop();
        if (timeDelta) {
            const qint64 drift = driftStart.msecsTo(QDateTime::currentDateTimeUtc());
            execute(cmd + newTime.addMSecs(drift).toString(dtFormat));
        } else {
            newTime.setTime(QTime::currentTime());
            execute(cmd + newTime.toString(dtFormat));
        }
        dateDelta = 0;
        timeDelta = 0;
    }

    // Kick the display timer back in action.
    if (!updater.isActive()) updater.start(0);
}

// HARDWARE CLOCK

void MXDateTime::on_btnReadHardware_clicked()
{
    setClockLock(true);
    const QString btext = btnReadHardware->text();
    btnReadHardware->setText(tr("Reading..."));
    QByteArray rtcout;
    execute("/sbin/hwclock --verbose", &rtcout);
    isHardwareUTC = rtcout.contains("\nHardware clock is on UTC time\n");
    if (isHardwareUTC) radHardwareUTC->setChecked(true);
    else radHardwareLocal->setChecked(true);
    txtHardwareClock->setPlainText(QString(rtcout.trimmed()));
    btnReadHardware->setText(btext);
    setClockLock(false);
}
void MXDateTime::on_btnHardwareAdjust_clicked()
{
    setClockLock(true);
    const QString btext = btnHardwareAdjust->text();
    btnHardwareAdjust->setText(tr("Adjusting..."));
    QByteArray rtcout;
    execute("/sbin/hwclock --adjust", &rtcout);
    txtHardwareClock->setPlainText(QString(rtcout.trimmed()));
    btnHardwareAdjust->setText(btext);
    setClockLock(false);
}
void MXDateTime::on_btnSystemToHardware_clicked()
{
    setClockLock(true);
    QString cmd("/sbin/hwclock --systohc");
    if (chkDriftUpdate->isChecked()) cmd.append(" --update-drift");
    transferTime(cmd, tr("System Clock"), tr("Hardware Clock"));
    chkDriftUpdate->setCheckState(Qt::Unchecked);
    setClockLock(false);
}
void MXDateTime::on_btnHardwareToSystem_clicked()
{
    setClockLock(true);
    transferTime("/sbin/hwclock --hctosys", tr("Hardware Clock"), tr("System Clock"));
    setClockLock(false);
}
void MXDateTime::transferTime(const QString &cmd, const QString &from, const QString &to)
{
    if (execute(cmd)) {
        const QString &msg = tr("The %1 time was transferred to the %2.");
        QMessageBox::information(this, windowTitle(), msg.arg(from, to));
    } else {
        const QString &msg = tr("The %1 time could not be transferred to the %2.");
        QMessageBox::warning(this, windowTitle(), msg.arg(from, to));
    }
}

void MXDateTime::saveHardwareClock()
{
    const bool rtcUTC = radHardwareUTC->isChecked();
    if (rtcUTC != isHardwareUTC) {
        if (sysInit == SystemD) {
            execute("timedatectl set-local-rtc " + QString(rtcUTC?"0":"1"));
        } else if(sysInit == OpenRC) {
            if(QFile::exists("/etc/conf.d/hwclock")) {
                execute("sed -i \"s/clock=.*/clock=\\\"UTC\\\"/\" /etc/conf.d/hwclock");
            }
        }
        execute("/sbin/hwclock --systohc --" + QString(rtcUTC?"utc":"localtime"));
    }
}

// NETWORK TIME

void MXDateTime::on_btnSyncNow_clicked()
{
    if (!validateServerList()) return;
    setClockLock(true);
    // Command preparation.
    const int serverCount = tblServers->rowCount();
    QString args;
    for (int ixi = 0; ixi < serverCount; ++ixi) {
        QTableWidgetItem *item = tblServers->item(ixi, 1);
        const QString &address = item->text().trimmed();
        if (item->checkState() == Qt::Checked) args += " " + address;
    }
    // Command execution.
    bool rexit = false;
    if (!args.isEmpty()) {
        QString btext = btnSyncNow->text();
        btnSyncNow->setText(tr("Updating..."));
        rexit = execute("/usr/sbin/ntpdate -u" + args);
        btnSyncNow->setText(btext);
    }
    // Finishing touches.
    setClockLock(false);
    dateDelta = 0;
    timeDelta = 0;
    updater.setInterval(0);
    if (rexit) {
        QMessageBox::information(this, windowTitle(), tr("The system clock was updated successfully."));
    } else if (!args.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("The system clock could not be updated."));
    } else {
        QMessageBox::critical(this, windowTitle(), tr("None of the NTP servers on the list are currently enabled."));
    }
}
void MXDateTime::on_tblServers_itemSelectionChanged()
{
    const QList<QTableWidgetSelectionRange> &ranges = tblServers->selectedRanges();
    bool remove = false, up = false, down = false;
    if (ranges.count() == 1) {
        const QTableWidgetSelectionRange &range = ranges.at(0);
        remove = true;
        if (range.topRow() > 0) up = true;
        if (range.bottomRow() < (tblServers->rowCount() - 1)) down = true;
    }
    btnServerRemove->setEnabled(remove);
    btnServerMoveUp->setEnabled(up);
    btnServerMoveDown->setEnabled(down);
}
void MXDateTime::on_btnServerAdd_clicked()
{
    QTableWidgetItem *item = addServerRow(true, "server", QString(), QString());
    tblServers->setCurrentItem(item);
    tblServers->editItem(item);
}
void MXDateTime::on_btnServerRemove_clicked()
{
    const QList<QTableWidgetSelectionRange> &ranges = tblServers->selectedRanges();
    for (int ixi = ranges.count() - 1; ixi >= 0; --ixi) {
        const int top = ranges.at(ixi).topRow();
        for (int row = ranges.at(ixi).bottomRow(); row >= top; --row) {
            tblServers->removeRow(row);
        }
    }
}
void MXDateTime::on_btnServerMoveUp_clicked()
{
    moveServerRow(-1);
}
void MXDateTime::on_btnServerMoveDown_clicked()
{
    moveServerRow(1);
}

QTableWidgetItem *MXDateTime::addServerRow(bool enabled, const QString &type, const QString &address, const QString &options)
{
    QComboBox *itemComboType = new QComboBox(tblServers);
    QTableWidgetItem *item = new QTableWidgetItem(address);
    QTableWidgetItem *itemOptions = new QTableWidgetItem(options);
    itemComboType->addItem("Pool", QVariant("pool"));
    itemComboType->addItem("Server", QVariant("server"));
    itemComboType->addItem("Peer", QVariant("peer"));
    itemComboType->setCurrentIndex(itemComboType->findData(QVariant(type)));
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    const int newRow = tblServers->rowCount();
    tblServers->insertRow(newRow);
    tblServers->setCellWidget(newRow, 0, itemComboType);
    tblServers->setItem(newRow, 1, item);
    tblServers->setItem(newRow, 2, itemOptions);
    return item;
}
void MXDateTime::moveServerRow(int movement)
{
    const QList<QTableWidgetSelectionRange> &ranges = tblServers->selectedRanges();
    if (ranges.count() == 1) {
        const QTableWidgetSelectionRange &range = ranges.at(0);
        int end, row;
        if (movement < 0) {
            row = range.topRow();
            end = range.bottomRow();
        } else {
            row = range.bottomRow();
            end = range.topRow();
        }
        row += movement;
        // Save the original row contents.
        int targetType = static_cast<QComboBox *>(tblServers->cellWidget(row, 0))->currentIndex();
        QTableWidgetItem *targetItemAddress = tblServers->takeItem(row, 1);
        QTableWidgetItem *targetItemOptions = tblServers->takeItem(row, 2);
        // Update the list selection.
        const QTableWidgetSelectionRange targetRange(row, range.leftColumn(), end + movement, range.rightColumn());
        tblServers->setCurrentItem(nullptr);
        tblServers->setRangeSelected(targetRange, true);
        // Move items one by one.
        do {
            row -= movement;
            int type = static_cast<QComboBox *>(tblServers->cellWidget(row, 0))->currentIndex();
            QTableWidgetItem *itemAddress = tblServers->takeItem(row, 1);
            QTableWidgetItem *itemOptions = tblServers->takeItem(row, 2);
            const int step = row + movement;
            static_cast<QComboBox *>(tblServers->cellWidget(step, 0))->setCurrentIndex(type);
            tblServers->setItem(step, 1, itemAddress);
            tblServers->setItem(step, 2, itemOptions);
        } while (row != end);
        // Move the target where the range originally finished.
        static_cast<QComboBox *>(tblServers->cellWidget(end, 0))->setCurrentIndex(targetType);
        tblServers->setItem(end, 1, targetItemAddress);
        tblServers->setItem(end, 2, targetItemOptions);
    }
}
bool MXDateTime::validateServerList()
{
    bool allValid = true;
    const int serverCount = tblServers->rowCount();
    for (int ixi = 0; ixi < serverCount; ++ixi) {
        QTableWidgetItem *item = tblServers->item(ixi, 1);
        const QString &address = item->text().trimmed();
        if (address.isEmpty()) allValid = false;
    }
    const char *msg = nullptr;
    if (serverCount <= 0) msg = "There are no NTP servers on the list.";
    else if (!allValid) msg = "There are invalid entries on the NTP server list.";
    if (msg) {
        QMessageBox::critical(this, windowTitle(), tr(msg));
        return false;
    }
    return true;
}

void MXDateTime::loadNetworkTime()
{
    QFile file("/etc/ntp.conf");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        QByteArray conf;
        while(tblServers->rowCount() > 0) tblServers->removeRow(0);
        confServers.clear();
        while(!file.atEnd()) {
            const QByteArray &bline = file.readLine();
            const QString line(bline.trimmed());
            const QRegularExpression tregex("^#?(pool|server|peer)\\s");
            if(!line.contains(tregex)) conf.append(bline);
            else {
                QStringList args = line.split(QRegularExpression("\\s"), QString::SkipEmptyParts);
                QString curarg = args.at(0);
                bool enabled = true;
                if (curarg.startsWith('#')) {
                    enabled = false;
                    curarg = curarg.remove(0, 1);
                }
                QString options;
                for (int ixi = 2; ixi < args.count(); ++ixi) {
                    options.append(' ');
                    options.append(args.at(ixi));
                }
                addServerRow(enabled, curarg, args.at(1), options.trimmed());
                confServers.append('\n');
                confServers.append(line);
            }
        }
        confBaseNTP = conf.trimmed();
        file.close();
    }
    if (sysInit == SystemD) enabledNTP = execute("bash -c \"timedatectl | grep NTP | grep yes\"");
    else enabledNTP = execute("bash -c \"ls /etc/rc*.d | grep ntp | grep '^S'");
    chkAutoSync->setChecked(enabledNTP);
}
void MXDateTime::saveNetworkTime()
{
    const bool ntp = chkAutoSync->isChecked();
    if (ntp != enabledNTP) {
        if(sysInit == SystemD) {
            execute("timedatectl set-ntp " + QString(ntp?"1":"0"));
        } else if (sysInit == OpenRC) {
            if (QFile::exists("/etc/init.d/ntpd")) {
                execute("rc-update " + QString(ntp?"add":"del") + " ntpd");
            }
        } else if (ntp){
            execute("update-rc.d ntp enable");
            execute("service ntp start");
        } else {
            execute("service ntp stop");
            execute("update-rc.d ntp disable");
        }
    }
    QByteArray confServersNew;
    for (int ixi = 0; ixi < tblServers->rowCount(); ++ixi) {
        QComboBox *comboType = static_cast<QComboBox *>(tblServers->cellWidget(ixi, 0));
        QTableWidgetItem *item = tblServers->item(ixi, 1);
        confServersNew.append('\n');
        if (item->checkState() != Qt::Checked) confServersNew.append('#');
        confServersNew.append(comboType->currentData().toString());
        confServersNew.append(' ');
        confServersNew.append(item->text().trimmed());
        const QString &options = tblServers->item(ixi, 2)->text().trimmed();
        if (!options.isEmpty()) {
            confServersNew.append(' ');
            confServersNew.append(options);
        }
    }
    if (confServersNew != confServers) {
        if (!QFile::exists("/etc/ntp.conf.bak")) QFile::copy("/etc/ntp.conf", "/etc/ntp.conf.bak");
        QFile file("/etc/ntp.conf");
        if (file.open(QFile::WriteOnly | QFile::Text)){
            file.write(confBaseNTP);
            file.write("\n\n# Generated by MX Date & Time");
            file.write(confServersNew);
            file.close();
        }
    }
}

// ACTION BUTTONS

void MXDateTime::on_btnApply_clicked()
{
    // Compensation for the execution time of this section.
    QDateTime driftStart = QDateTime::currentDateTimeUtc();
    setClockLock(true);

    // Validate all data before trying to save settings.
    if ((loadedTabs & 4) && !validateServerList()) {
        setClockLock(false);
        return;
    }

    // Save the settings.
    saveDateTime(driftStart);
    if (loadedTabs & 2) saveHardwareClock();
    if (loadedTabs & 4) saveNetworkTime();

    // Refresh the UI (especially the current tab) with newly set values.
    loadedTabs = 0;
    setClockLock(false);
}
void MXDateTime::on_btnClose_clicked()
{
    qApp->exit(0);
}
// MX Standard User Interface
void MXDateTime::on_btnAbout_clicked()
{
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About MX Date & Time"), "<p align=\"center\"><b><h2>" +
                       tr("MX Date & Time") + "</h2></b></p><p align=\"center\">" + tr("Version: ") + VERSION + "</p><p align=\"center\"><h3>" +
                       tr("GUI program for setting the time and date in MX Linux") +
                       "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>");
    QPushButton *btnLicense = msgBox.addButton(tr("License"), QMessageBox::HelpRole);
    QPushButton *btnChangelog = msgBox.addButton(tr("Changelog"), QMessageBox::HelpRole);
    QPushButton *btnCancel = msgBox.addButton(tr("Cancel"), QMessageBox::NoRole);
    btnCancel->setIcon(QIcon::fromTheme("window-close"));

    msgBox.exec();

    if (msgBox.clickedButton() == btnLicense) {
        QString url = "/usr/share/doc/mx-datetime/license.html";
        QByteArray user;
        execute("logname", &user);
        user = user.trimmed();
        QString env_run = userRoot ? "runuser -l " + user + " -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u " + user + ") " : QString();
        if (system("command -v mx-viewer") == 0) { // use mx-viewer if available
            system("mx-viewer " + url.toUtf8() + " " + tr("License").toUtf8() + "&");
        } else {
            system(env_run.toUtf8() + "xdg-open " + url.toUtf8() + "\"&");
        }
    } else if (msgBox.clickedButton() == btnChangelog) {
        QDialog *changelog = new QDialog(this);
        changelog->resize(600, 500);

        QTextEdit *text = new QTextEdit;
        text->setReadOnly(true);
        QByteArray rtcout;
        execute("zless /usr/share/doc/" + QFileInfo(QCoreApplication::applicationFilePath()).fileName()  + "/changelog.gz", &rtcout);
        text->setText(rtcout);

        QPushButton *btnClose = new QPushButton(tr("&Close"));
        btnClose->setIcon(QIcon::fromTheme("window-close"));
        connect(btnClose, &QPushButton::clicked, changelog, &QDialog::close);

        QVBoxLayout *layout = new QVBoxLayout;
        layout->addWidget(text);
        layout->addWidget(btnClose);
        changelog->setLayout(layout);
        changelog->exec();
    }
}
void MXDateTime::on_btnHelp_clicked()
{
    QString url = "/usr/share/doc/mx-datetime/help/mx-datetime.html";
    QByteArray user;
    execute("logname", &user);
    user = user.trimmed();
    if (system("command -v mx-viewer") == 0) {
        system ("mx-viewer " + url.toUtf8() + " \"" + tr("MX Date & Time Help").toUtf8() + "\"&");
    } else {
        QString env_run = userRoot ? "runuser -l " + user + " -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u " + user + ") " : QString();
        system(env_run.toUtf8() + "xdg-open " + url.toUtf8() + "\"&");
    }
}

// SUBCLASSING FOR QTimeEdit THAT FIXES CURSOR AND SELECTION JUMPING EVERY SECOND

MTimeEdit::MTimeEdit(QWidget *parent) : QTimeEdit(parent)
{
    // Ensure the widget is not too wide.
    QString fmt(displayFormat());
    if (fmt.section(':', 0, 0).length() < 2) fmt.insert(0, QChar('X'));
    setMaximumWidth(fontMetrics().width(fmt) + (width() - lineEdit()->width()));
}
void MTimeEdit::updateDateTime(const QDateTime &dateTime)
{
    QLineEdit *ledit = lineEdit();
    // Original cursor position and selections
    int select = ledit->selectionStart();
    int cursor = ledit->cursorPosition();
    // Calculation for backward selections.
    if (select >= 0 && select >= cursor) {
        const int cslength = ledit->selectedText().length();
        if (cslength > 0) select = cursor + cslength;
    }
    // Set the date/time as normal.
    setDateTime(dateTime);
    // Restore cursor and selection.
    if (select >= 0) ledit->setSelection(select, cursor - select);
    else ledit->setCursorPosition(cursor);
}
