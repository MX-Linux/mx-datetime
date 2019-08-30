// Clock Face widget implementation.
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

#include <QPainter>
#include "clockface.h"

ClockFace::ClockFace(QWidget *parent) : QWidget(parent)
{
}

void ClockFace::setTime(const QTime &newtime)
{
    time = newtime;
    update();
}

void ClockFace::paintEvent(QPaintEvent *)
{
    // Polygon points for hands and marks.
    static const QPoint handHour[] = {
        QPoint(7, 0), QPoint(0, 12),
        QPoint(-7, 0), QPoint(0, -60)
    };
    static const QPoint handMinute[] = {
        QPoint(3, 0), QPoint(0, 6),
        QPoint(-3, 0), QPoint(0, -80)
    };
    static const QPoint handSecond[] = {
        QPoint(1, 0), QPoint(0, 3),
        QPoint(-1, 0), QPoint(0, -95)
    };
    static const QPoint markMinute[] = {
        QPoint(92, 0), QPoint(93, 1),
        QPoint(96, 0), QPoint(93, -1)
    };
    static const QPoint markHour[] = {
        QPoint(91, 0), QPoint(93, 2),
        QPoint(98, 0), QPoint(93, -2)
    };
    // Colour calculations.
    QColor colHour = palette().foreground().color();
    colHour.setAlpha(150);
    QColor colMinute = colHour;
    QColor colSecond = colMinute;
    colHour.setRed(255 - colHour.red());
    colMinute.setBlue(255 - colSecond.blue());

    // Paint the clock.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(width() / 2, height() / 2);
    const int side = qMin(width(), height());
    painter.scale(side / 200.0, side / 200.0);
    // Hour hand.
    painter.setPen(colHour.darker());
    painter.setBrush(colHour);
    painter.save();
    painter.rotate(30.0 * ((time.hour() + time.minute() / 60.0)));
    painter.drawConvexPolygon(handHour, 4);
    painter.restore();
    // Minute hand.
    painter.setPen(colMinute.darker());
    painter.setBrush(colMinute);
    painter.save();
    painter.rotate(6.0 * (time.minute() + time.second() / 60.0));
    painter.drawConvexPolygon(handMinute, 4);
    painter.restore();
    // Second hand.
    painter.setPen(colSecond.darker());
    painter.setBrush(colSecond);
    painter.save();
    painter.rotate(6.0 * time.second());
    painter.drawConvexPolygon(handSecond, 4);
    painter.restore();
    // Marks around the circumference.
    painter.setPen(colHour.darker());
    painter.setBrush(colHour);
    for (int ixi = 0; ixi < 12; ++ixi) {
        painter.drawConvexPolygon(markHour, 4);
        painter.rotate(30.0);
    }
    painter.setPen(colMinute.darker());
    painter.setBrush(colMinute);
    for (int ixi = 0; ixi < 60; ++ixi) {
        if ((ixi % 5) != 0) painter.drawConvexPolygon(markMinute, 4);
        painter.rotate(6.0);
    }
}
