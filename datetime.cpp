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

#include <QDebug>
#include <QDateTime>
#include <QTimeZone>
#include <QFileInfo>
#include <QProcess>
#include <QTextCharFormat>
#include <QMessageBox>
#include "datetime.h"
#include "ui_datetime.h"
#include "version.h"

MXDateTime::MXDateTime(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MXDateTime)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window); // for the close, min and max buttons
    QTextCharFormat tcfmt;
    tcfmt.setFontPointSize(ui->calendar->font().pointSizeF() * 0.75);
    ui->calendar->setHeaderTextFormat(tcfmt);

    is_systemd = (QFileInfo("/usr/bin/timedatectl").isExecutable()
                  && execute("pidof systemd"));
    is_openrc = QFileInfo::exists("/run/openrc");

    // timezone
    ui->cmbTimeZone->blockSignals(true); // Keep blocked until loadSysTimeConfig().
    ui->cmbTimeZone->clear();
    QFile file("/usr/share/zoneinfo/zone.tab");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        while(!file.atEnd()) {
            const QString line(file.readLine().trimmed());
            if(!line.startsWith('#')) {
                ui->cmbTimeZone->addItem(line.section("\t", 2, 2));
            }
        }
        file.close();
    }
    ui->cmbTimeZone->model()->sort(0);

    // load the system values into the UI
    ui->timeEdit->setDateTime(QDateTime::currentDateTime()); // avoids the sudden jump
    QTimer::singleShot(0, this, &MXDateTime::loadSysTimeConfig);
}

MXDateTime::~MXDateTime()
{
    delete ui;
}

// USER INTERFACE

void MXDateTime::secUpdate()
{
    secUpdating = true;
    ui->timeEdit->setDateTime(QDateTime::currentDateTime().addSecs(timeDelta + zoneDelta));
    timer->setInterval(1000 - QTime::currentTime().msec());
    ui->calendar->setSelectedDate(ui->timeEdit->date());
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

void MXDateTime::on_cmbTimeZone_currentIndexChanged(int index)
{
    if (index < 0 || index >= ui->cmbTimeZone->count()) return;
    // Calculate and store the difference between current and newly selected time zones.
    const QDateTime &current = QDateTime::currentDateTime();
    zoneDelta = QTimeZone(ui->cmbTimeZone->itemText(index).toUtf8()).offsetFromUtc(current)
              - QTimeZone::systemTimeZone().offsetFromUtc(current); // Delta = new - old
    secUpdate(); // Make the change immediately visible
}

void MXDateTime::on_calendar_clicked(const QDate &date)
{
    calChanging = true;
    ui->timeEdit->setDateTime(QDateTime(date, ui->timeEdit->time()));
    calChanging = false;
}

void MXDateTime::on_btnReadRTC_clicked()
{
    setClockLock(true);
    const QString btext = ui->btnReadRTC->text();
    ui->btnReadRTC->setText(tr("Reading..."));
    QByteArray rtcout;
    execute("/sbin/hwclock --verbose", &rtcout);
    isRTCUTC = rtcout.contains("\nHardware clock is on UTC time\n");
    if (isRTCUTC) ui->radRTCUTC->setChecked(true);
    else ui->radRTCLocal->setChecked(true);
    ui->txtRTC->setPlainText(QString(rtcout.trimmed()));
    ui->btnReadRTC->setText(btext);
    setClockLock(false);
}

void MXDateTime::on_btnSystemToRTC_clicked()
{
    setClockLock(true);
    if (execute("/sbin/hwclock --systohc")) {
        QMessageBox::information(this, windowTitle(), "Success.");
    } else {
        QMessageBox::warning(this, windowTitle(), "Failure.");
    }
    setClockLock(false);
}

void MXDateTime::on_btnRTCToSystem_clicked()
{
    setClockLock(true);
    if (execute("/sbin/hwclock -hctosys")) {
        QMessageBox::information(this, windowTitle(), "Success.");
    } else {
        QMessageBox::warning(this, windowTitle(), "Failure.");
    }
    setClockLock(false);
}

void MXDateTime::on_btnSyncNTP_clicked()
{
    setClockLock(true);
    QString btext = ui->btnSyncNTP->text();
    ui->btnSyncNTP->setText(tr("Synchronizing..."));
    if (execute("/usr/sbin/ntpdate -u 0.pool.ntp.org")) {
        QMessageBox::information(this, windowTitle(), "Success.");
    } else {
        QMessageBox::warning(this, windowTitle(), "Failure.");
    }
    ui->btnSyncNTP->setText(btext);
    setClockLock(false);
}

void MXDateTime::on_btnClose_clicked()
{
    qApp->exit(0);
}

void MXDateTime::on_btnApply_clicked()
{
    // Compensation for the execution time of this section.
    QDateTime calcDrift = QDateTime::currentDateTimeUtc();

    // Set the time zone (if changed) before setting the time.
    if (zoneDelta) {
        const QString &newzone = ui->cmbTimeZone->currentText();
        if (is_systemd) execute("timedatectl set-timezone " + newzone);
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
        if (is_systemd) cmd = "timedatectl set-time ";
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
    const bool ntp = ui->chkNTP->isChecked();
    if (ntp != enabledNTP) {
        if(is_systemd) {
            execute("timedatectl set-ntp " + QString(ntp?"1":"0"));
        } else if (is_openrc) {
            if (QFile::exists("/etc/init.d/ntpd")) {
                execute("rc-update " + QString(ntp?"add":"del") + " ntpd");
            }
        }
    }

    // RTC settings
    const bool rtcUTC = ui->radRTCUTC->isChecked();
    if (rtcUTC != isRTCUTC) {
        if (is_systemd) {
            execute("timedatectl set-local-rtc " + QString(rtcUTC?"0":"1"));
        } else if(is_openrc) {
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
    ui->cmbTimeZone->blockSignals(true);
    QFile file("/etc/timezone");
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        const QString line(file.readLine().trimmed());
        ui->cmbTimeZone->setCurrentIndex(ui->cmbTimeZone->findText(line));
        file.close();
    }
    zoneDelta = 0;
    ui->cmbTimeZone->blockSignals(false);

    enabledNTP = execute("bash -c \"timedatectl | grep NTP | grep yes\"");
    ui->chkNTP->setChecked(enabledNTP);
    if (!(is_systemd || is_openrc)) ui->chkNTP->setEnabled(false);
    on_btnReadRTC_clicked();

    timer = new QTimer(this);
    timeDelta = 0;
    secUpdating = true;
    connect(timer, &QTimer::timeout, this, QOverload<>::of(&MXDateTime::secUpdate));
    ui->timeEdit->setDateTime(QDateTime::currentDateTime());
    timeChanged = false;
    timer->start(1000 - QTime::currentTime().msec());
}

void MXDateTime::setClockLock(bool locked)
{
    if (locked) qApp->setOverrideCursor(QCursor(Qt::BusyCursor));
    ui->tabDateTime->setDisabled(locked);
    ui->tabSync->setDisabled(locked);
    ui->btnApply->setDisabled(locked);
    ui->btnClose->setDisabled(locked);
    if (!locked) qApp->restoreOverrideCursor();
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

// MX STANDARD USER INTERFACE

void MXDateTime::on_btnAbout_clicked()
{
    QString url = "/usr/share/doc/mx-datetime/license.html";
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
    QString exec = "su " + user + " -c \"env XDG_RUNTIME_DIR=/run/user/$(id -u " + user + ") xdg-open";
    if (system("command -v mx-viewer") == 0) { // use mx-viewer if available
        exec = "mx-viewer"; // mx-viewer drops rights, no need to run as user
        url += " " + tr("MX Date \\& Time Help");
    }
    QString cmd = QString(exec + " " + url + "\"&");
    system(cmd.toUtf8());
}
