/**********************************************************************
 *  Chrony configuration paths.
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

#ifndef CHRONYPATHS_H
#define CHRONYPATHS_H

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace Chrony {
using namespace Qt::StringLiterals;

inline QString openRcServiceName() {
    if (QFileInfo::exists(u"/etc/init.d/chronyd"_s)) {
        return u"chronyd"_s;
    }
    return u"chrony"_s;
}

inline QString chronyConfigFile()
{
    if (QFile::exists(u"/etc/chrony/chrony.conf"_s)) {
        return u"/etc/chrony/chrony.conf"_s;
    }
    if (QFile::exists(u"/etc/chrony.conf"_s)) {
        return u"/etc/chrony.conf"_s;
    }
    return u"/etc/chrony/chrony.conf"_s;
}

inline QStringList sourceDirectories(const QByteArray &raw) {
    const QString line = QString::fromUtf8(raw).section('#', 0, 0).simplified();
    QStringList tokens = line.split(' ', Qt::SkipEmptyParts);
    if (tokens.isEmpty() || tokens.takeFirst().compare(u"sourcedir"_s, Qt::CaseInsensitive) != 0) {
        return {};
    }
    return tokens;
}

inline QString chronySourcesFile() {
    QFile file(chronyConfigFile());
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        while (!file.atEnd()) {
            const QStringList dirs = sourceDirectories(file.readLine());
            for (const QString &dir : dirs) {
                if (dir.startsWith(u"/etc/"_s)) {
                    return QDir(dir).filePath(u"mx-datetime.sources"_s);
                }
            }
        }
    }
    if (QFileInfo(u"/etc/chrony/sources.d"_s).isDir()) {
        return u"/etc/chrony/sources.d/mx-datetime.sources"_s;
    }
    if (QFileInfo(u"/etc/chrony.d"_s).isDir()) {
        return u"/etc/chrony.d/mx-datetime.sources"_s;
    }
    return u"/etc/chrony/sources.d/mx-datetime.sources"_s;
}

}

#endif // CHRONYPATHS_H
