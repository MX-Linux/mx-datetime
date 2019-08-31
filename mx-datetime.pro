QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = mx-datetime
TEMPLATE = app
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x050000

SOURCES += main.cpp datetime.cpp \
    clockface.cpp
HEADERS += datetime.h \
    clockface.h
FORMS += datetime.ui

RESOURCES += \
    images.qrc
