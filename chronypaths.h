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

inline QString chronySourcesFile()
{
    const QString configFile = chronyConfigFile();
    QFile file(configFile);
    QString persistentSourcesFile;
    if (file.open(QFile::ReadOnly | QFile::Text)) {
        while (!file.atEnd()) {
            QString line = QString::fromUtf8(file.readLine()).simplified();
            line = line.section('#', 0, 0).trimmed();
            if (line.startsWith(u"sourcedir "_s)) {
                const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 2) {
                    const QString dirPath = parts.at(1);
                    if (dirPath.startsWith(u"/etc/"_s)) {
                        persistentSourcesFile = QDir(dirPath).filePath(u"mx-datetime.sources"_s);
                        break;
                    }
                }
            }
        }
    }
    if (!persistentSourcesFile.isEmpty()) {
        return persistentSourcesFile;
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
