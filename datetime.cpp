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
#include "ui_datetime.h"
#include "version.h"

MXDateTime::MXDateTime(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MXDateTime)
{
    ui->setupUi(this);
    setClockLock(true);
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    QTextCharFormat tcfmt;
    tcfmt.setFontPointSize(ui->calendar->font().pointSizeF() * 0.75);
    ui->calendar->setHeaderTextFormat(tcfmt);

    // Operate with reduced functionality if not running as root.
    if (getuid() != 0) {
        ui->btnAbout->hide();
        ui->btnHelp->hide();
        ui->lblLogo->hide();
        ui->btnApply->hide();
        ui->btnClose->hide();
        ui->tabWidget->tabBar()->hide();
        ui->gridWindow->setMargin(0);
        ui->gridDateTime->setMargin(ui->gridDateTime->spacing());
    }

    QTimer::singleShot(0, this, &MXDateTime::startup);
}

MXDateTime::~MXDateTime()
{
    delete ui;
}

void MXDateTime::startup()
{
    // Make the NTP server table columns the right proportions.
    int colSizes[3];
    addServerRow(true, QString(), QString(), QString());
    ui->tblServers->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    for (int ixi = 0; ixi < 3; ++ixi) colSizes[ixi] = ui->tblServers->columnWidth(ixi);
    ui->tblServers->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    for (int ixi = 0; ixi < 3; ++ixi) ui->tblServers->setColumnWidth(ixi, colSizes[ixi]);
    ui->tblServers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->tblServers->removeRow(0);

    // Used to decide the type of commands to run on this system.
    QByteArray testSystemD;
    if (QFileInfo::exists("/run/openrc")) sysInit = OpenRC;
    else if(QFileInfo("/usr/bin/timedatectl").isExecutable()
            && execute("ps -hp1", &testSystemD) && testSystemD.contains("systemd")) {
        sysInit = SystemD;
    } else sysInit = SystemV;
    static const char *sysInitNames[] = {"SystemV", "OpenRC", "SystemD"};
    qDebug() << "Init system:" << sysInitNames[sysInit];

    // Time zone areas.
    QByteArray zoneOut;
    execute("find -L /usr/share/zoneinfo/posix -mindepth 2 -type f -printf %P\\n", &zoneOut);
    zones = zoneOut.trimmed().split('\n');
    ui->cmbTimeZone->blockSignals(true); // Keep blocked until loadSysTimeConfig().
    ui->cmbTimeArea->clear();
    for (const QByteArray &zone : zones) {
        const QString &area = QString(zone).section('/', 0, 0);
        if (ui->cmbTimeArea->findData(area) < 0) {
            ui->cmbTimeArea->addItem(area, QVariant(area.toUtf8()));
        }
    }
    ui->cmbTimeArea->model()->sort(0);

    // load the system values into the UI
    ui->timeEdit->setDateTime(QDateTime::currentDateTime()); // avoids the sudden jump
    loadSysTimeConfig();
}

// USER INTERFACE

void MXDateTime::secUpdate()
{
    secUpdating = true;
    ui->timeEdit->updateDateTime(QDateTime::currentDateTime().addSecs(timeDelta + zoneDelta));
    timer->setInterval(1000 - QTime::currentTime().msec());
    if(ui->calendar->selectedDate() != ui->timeEdit->date()) {
        ui->calendar->setSelectedDate(ui->timeEdit->date());
    }
    secUpdating = false;
}

void MXDateTime::on_timeEdit_dateTimeChanged(const QDateTime &dateTime)
{
    ui->clock->setTime(dateTime.time());
    if (!secUpdating) {
        timeDelta = QDateTime::currentDateTime().secsTo(dateTime) - zoneDelta;
        if (!calChanging) timeChanged = true;
    }
}

void MXDateTime::on_cmbTimeArea_currentIndexChanged(int index)
{
    if (index < 0 || index >= ui->cmbTimeArea->count()) return;
    const QByteArray &area = ui->cmbTimeArea->itemData(index).toByteArray();
    ui->cmbTimeZone->clear();
    for (const QByteArray &zone : zones) {
        if (zone.startsWith(area)) {
            ui->cmbTimeZone->addItem(QString(zone).section('/', 1), QVariant(zone));
        }
    }
    ui->cmbTimeZone->model()->sort(0);
}
void MXDateTime::on_cmbTimeZone_currentIndexChanged(int index)
{
    if (index < 0 || index >= ui->cmbTimeZone->count()) return;
    // Calculate and store the difference between current and newly selected time zones.
    const QDateTime &current = QDateTime::currentDateTime();
    zoneDelta = QTimeZone(ui->cmbTimeZone->itemData(index).toByteArray()).offsetFromUtc(current)
              - QTimeZone::systemTimeZone().offsetFromUtc(current); // Delta = new - old
    secUpdate(); // Make the change immediately visible
}

void MXDateTime::on_calendar_clicked(const QDate &date)
{
    calChanging = true;
    ui->timeEdit->updateDateTime(QDateTime(date, ui->timeEdit->time()));
    calChanging = false;
}

// HARDWARE CLOCK

void MXDateTime::on_btnReadHardware_clicked()
{
    setClockLock(true);
    const QString btext = ui->btnReadHardware->text();
    ui->btnReadHardware->setText(tr("Reading..."));
    QByteArray rtcout;
    execute("/sbin/hwclock --verbose", &rtcout);
    isHardwareUTC = rtcout.contains("\nHardware clock is on UTC time\n");
    if (isHardwareUTC) ui->radHardwareUTC->setChecked(true);
    else ui->radHardwareLocal->setChecked(true);
    ui->txtHardwareClock->setPlainText(QString(rtcout.trimmed()));
    ui->btnReadHardware->setText(btext);
    setClockLock(false);
}

void MXDateTime::on_btnSystemToHardware_clicked()
{
    setClockLock(true);
    if (execute("/sbin/hwclock --systohc")) {
        QMessageBox::information(this, windowTitle(), "Success.");
    } else {
        QMessageBox::warning(this, windowTitle(), "Failure.");
    }
    setClockLock(false);
}

void MXDateTime::on_btnHardwareToSystem_clicked()
{
    setClockLock(true);
    if (execute("/sbin/hwclock -hctosys")) {
        QMessageBox::information(this, windowTitle(), "Success.");
    } else {
        QMessageBox::warning(this, windowTitle(), "Failure.");
    }
    setClockLock(false);
}

// NETWORK TIME

void MXDateTime::on_btnSyncNow_clicked()
{
    if (!validateServerList()) return;
    setClockLock(true);
    // Command preparation.
    const int serverCount = ui->tblServers->rowCount();
    QString args;
    for (int ixi = 0; ixi < serverCount; ++ixi) {
        QTableWidgetItem *item = ui->tblServers->item(ixi, 1);
        const QString &address = item->text().trimmed();
        if (item->checkState() == Qt::Checked) args += " " + address;
    }
    // Command execution.
    bool rexit = false;
    if (!args.isEmpty()) {
        QString btext = ui->btnSyncNow->text();
        ui->btnSyncNow->setText(tr("Updating..."));
        rexit = execute("/usr/sbin/ntpdate -u" + args);
        ui->btnSyncNow->setText(btext);
    }
    // Finishing touches.
    setClockLock(false);
    if (rexit) {
        QMessageBox::information(this, windowTitle(), tr("The system clock was updated successfully."));
    } else if (!args.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), tr("The system clock could not be updated."));
    } else {
        QMessageBox::critical(this, windowTitle(), tr("None of the NTP servers on the list are currently enabled."));
    }
}
bool MXDateTime::validateServerList()
{
    bool allValid = true;
    const int serverCount = ui->tblServers->rowCount();
    for (int ixi = 0; ixi < serverCount; ++ixi) {
        QTableWidgetItem *item = ui->tblServers->item(ixi, 1);
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
    if (serverCount == 88) setWindowTitle("88 MILES PER HOUR");
    return true;
}

void MXDateTime::on_tblServers_itemSelectionChanged()
{
    const QList<QTableWidgetSelectionRange> &ranges = ui->tblServers->selectedRanges();
    bool remove = false, up = false, down = false;
    if (ranges.count() == 1) {
        const QTableWidgetSelectionRange &range = ranges.at(0);
        remove = true;
        if (range.topRow() > 0) up = true;
        if (range.bottomRow() < (ui->tblServers->rowCount() - 1)) down = true;
    }
    ui->btnServerRemove->setEnabled(remove);
    ui->btnServerMoveUp->setEnabled(up);
    ui->btnServerMoveDown->setEnabled(down);
}

void MXDateTime::on_btnServerAdd_clicked()
{
    QTableWidgetItem *item = addServerRow(true, "server", QString(), QString());
    ui->tblServers->setCurrentItem(item);
    ui->tblServers->editItem(item);
}
QTableWidgetItem *MXDateTime::addServerRow(bool enabled, const QString &type, const QString &address, const QString &options)
{
    QComboBox *itemComboType = new QComboBox(ui->tblServers);
    QTableWidgetItem *item = new QTableWidgetItem(address);
    QTableWidgetItem *itemOptions = new QTableWidgetItem(options);
    itemComboType->addItem("Pool", QVariant("pool"));
    itemComboType->addItem("Server", QVariant("server"));
    itemComboType->addItem("Peer", QVariant("peer"));
    itemComboType->setCurrentIndex(itemComboType->findData(QVariant(type)));
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
    item->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    const int newRow = ui->tblServers->rowCount();
    ui->tblServers->insertRow(newRow);
    ui->tblServers->setCellWidget(newRow, 0, itemComboType);
    ui->tblServers->setItem(newRow, 1, item);
    ui->tblServers->setItem(newRow, 2, itemOptions);
    return item;
}

void MXDateTime::on_btnServerRemove_clicked()
{
    const QList<QTableWidgetSelectionRange> &ranges = ui->tblServers->selectedRanges();
    for (int ixi = ranges.count() - 1; ixi >= 0; --ixi) {
        const int top = ranges.at(ixi).topRow();
        for (int row = ranges.at(ixi).bottomRow(); row >= top; --row) {
            ui->tblServers->removeRow(row);
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
void MXDateTime::moveServerRow(int movement)
{
    const QList<QTableWidgetSelectionRange> &ranges = ui->tblServers->selectedRanges();
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
        int targetType = static_cast<QComboBox *>(ui->tblServers->cellWidget(row, 0))->currentIndex();
        QTableWidgetItem *targetItemAddress = ui->tblServers->takeItem(row, 1);
        QTableWidgetItem *targetItemOptions = ui->tblServers->takeItem(row, 2);
        // Update the list selection.
        const QTableWidgetSelectionRange targetRange(row, range.leftColumn(), end + movement, range.rightColumn());
        ui->tblServers->setCurrentItem(nullptr);
        ui->tblServers->setRangeSelected(targetRange, true);
        // Move items one by one.
        do {
            row -= movement;
            int type = static_cast<QComboBox *>(ui->tblServers->cellWidget(row, 0))->currentIndex();
            QTableWidgetItem *itemAddress = ui->tblServers->takeItem(row, 1);
            QTableWidgetItem *itemOptions = ui->tblServers->takeItem(row, 2);
            const int step = row + movement;
            static_cast<QComboBox *>(ui->tblServers->cellWidget(step, 0))->setCurrentIndex(type);
            ui->tblServers->setItem(step, 1, itemAddress);
            ui->tblServers->setItem(step, 2, itemOptions);
        } while (row != end);
        // Move the target where the range originally finished.
        static_cast<QComboBox *>(ui->tblServers->cellWidget(end, 0))->setCurrentIndex(targetType);
        ui->tblServers->setItem(end, 1, targetItemAddress);
        ui->tblServers->setItem(end, 2, targetItemOptions);
    }
}

// OTHER

void MXDateTime::on_btnClose_clicked()
{
    qApp->exit(0);
}

void MXDateTime::on_btnApply_clicked()
{
    // Validation
    if (!validateServerList()) return;

    // Compensation for the execution time of this section.
    QDateTime calcDrift = QDateTime::currentDateTimeUtc();

    // Set the time zone (if changed) before setting the time.
    if (zoneDelta) {
        const QString newzone(ui->cmbTimeZone->currentData().toByteArray());
        if (sysInit == SystemD) execute("timedatectl set-timezone " + newzone);
        else {
            execute("ln -nfs /usr/share/zoneinfo/" + newzone + " /etc/localtime");
            QFile file("/etc/timezone");
            if (file.open(QFile::WriteOnly | QFile::Text)) {
                file.write(newzone.toUtf8());
                file.close();
            }
        }
    }

    // Set the date and time if their controls have been altered.
    if (timeDelta) {
        QString cmd;
        if (sysInit == SystemD) cmd = "timedatectl set-time ";
        else cmd = "date -s ";
        static const QString dtFormat("yyyy-MM-ddTHH:mm:ss.zzz");
        QDateTime newTime = ui->timeEdit->dateTime();
        if (timeChanged) {
            qint64 drift = calcDrift.msecsTo(QDateTime::currentDateTimeUtc());
            execute(cmd + newTime.addMSecs(drift).toString(dtFormat));
        } else {
            newTime.setTime(QTime::currentTime());
            execute(cmd + newTime.toString(dtFormat));
        }
    }

    // NTP settings
    const bool ntp = ui->chkAutoSync->isChecked();
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
    for (int ixi = 0; ixi < ui->tblServers->rowCount(); ++ixi) {
        QComboBox *comboType = static_cast<QComboBox *>(ui->tblServers->cellWidget(ixi, 0));
        QTableWidgetItem *item = ui->tblServers->item(ixi, 1);
        confServersNew.append('\n');
        if (item->checkState() != Qt::Checked) confServersNew.append('#');
        confServersNew.append(comboType->currentData().toString());
        confServersNew.append(' ');
        confServersNew.append(item->text().trimmed());
        const QString &options = ui->tblServers->item(ixi, 2)->text().trimmed();
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

    // RTC settings
    const bool rtcUTC = ui->radHardwareUTC->isChecked();
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

    // Refresh the UI with newly set values
    loadSysTimeConfig();
}

// HELPER FUNCTIONS

void MXDateTime::loadSysTimeConfig()
{
    // Time zone.
    ui->cmbTimeZone->blockSignals(true);
    const QByteArray &zone = QTimeZone::systemTimeZoneId();
    int index = ui->cmbTimeArea->findData(QVariant(QString(zone).section('/', 0, 0).toUtf8()));
    ui->cmbTimeArea->setCurrentIndex(index);
    qApp->processEvents();
    index = ui->cmbTimeZone->findData(QVariant(zone));
    ui->cmbTimeZone->setCurrentIndex(index);
    zoneDelta = 0;
    ui->cmbTimeZone->blockSignals(false);

    // Network time.
    QFile file("/etc/ntp.conf");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        QByteArray conf;
        while(ui->tblServers->rowCount() > 0) ui->tblServers->removeRow(0);
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
    ui->chkAutoSync->setChecked(enabledNTP);

    // Date and time.
    timer = new QTimer(this);
    timeDelta = 0;
    secUpdating = true;
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&MXDateTime::secUpdate));
    ui->timeEdit->setDateTime(QDateTime::currentDateTime());
    timeChanged = false;
    setClockLock(false);
    timer->start(1000 - QTime::currentTime().msec());
}

void MXDateTime::setClockLock(bool locked)
{
    if (clockLock != locked) {
        if (locked) qApp->setOverrideCursor(QCursor(Qt::BusyCursor));
        ui->tabDateTime->setDisabled(locked);
        ui->tabHardware->setDisabled(locked);
        ui->tabNetwork->setDisabled(locked);
        ui->btnApply->setDisabled(locked);
        ui->btnClose->setDisabled(locked);
        if (!locked) qApp->restoreOverrideCursor();
        clockLock = locked;
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

// SUBCLASSING FOR QTimeEdit THAT FIXES CURSOR AND SELECTION JUMPING EVERY SECOND

MTimeEdit::MTimeEdit(QWidget *parent) : QTimeEdit(parent)
{
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

// MX STANDARD USER INTERFACE

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
        QString env_run = "su " + user + " -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u " + user + ") ";
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
        system ("su " + user + " -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u " + user + ") xdg-open " + url.toUtf8() + "\"&");
    }
}
