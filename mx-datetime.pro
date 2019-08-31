QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = mx-datetime
TEMPLATE = app
DEFINES += QT_DEPRECATED_WARNINGS
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x050000

SOURCES += main.cpp datetime.cpp \
    clockface.cpp
HEADERS += datetime.h \
    clockface.h \
    version.h
FORMS += datetime.ui

RESOURCES += \
    images.qrc

TRANSLATIONS += translations/mx-datetime_ca.ts \
                translations/mx-datetime_de.ts \
                translations/mx-datetime_el.ts \
                translations/mx-datetime_es.ts \
                translations/mx-datetime_fr.ts \
                translations/mx-datetime_it.ts \
                translations/mx-datetime_ja.ts \
                translations/mx-datetime_nl.ts \
                translations/mx-datetime_ro.ts \
                translations/mx-datetime_sv.ts
