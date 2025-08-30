# **********************************************************************
# * Copyright (C) 2019-2025 MX Authors
# *
# * Authors: AK-47, Adrian
# *          MX Linux <http://mxlinux.org>
# *
# * This file is part of mx-datetime.
# *
# * mx-datetime is free software: you can redistribute it and/or modify
# * it under the terms of the Apache License, Version 2.0 (the "License");
# * you may not use this file except in compliance with the License.
# * You may obtain a copy of the License at
# *
# *     http://www.apache.org/licenses/LICENSE-2.0
# *
# * Unless required by applicable law or agreed to in writing, software
# * distributed under the License is distributed on an "AS IS" BASIS,
# * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# * See the License for the specific language governing permissions and
# * limitations under the License.
# **********************************************************************/

cmake_minimum_required(VERSION 3.25 FATAL_ERROR)
include(build.cmake)

project(mx-datetime
    VERSION 3.0
    DESCRIPTION "MX Date & Time - Date and time configuration tool for MX Linux"
    LANGUAGES CXX
)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets LinguistTools)

set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(CMAKE_CXX_STANDARD 20)
#set(CMAKE_CXX_SCAN_FOR_MODULES ON)

qt_standard_project_setup()

qt_add_executable(mx-datetime
    main.cpp
    datetime.cpp datetime.h datetime.ui
    about.cpp about.h
    clockface.cpp clockface.h
    images.qrc
)

#target_sources(mx-tweak PRIVATE FILE_SET CXX_MODULES FILES
#    TODO: add files that are C++20 modules.
#)

qt_add_translations(mx-datetime TS_FILES
    "translations/mx-datetime_ca.ts"
    "translations/mx-datetime_cs.ts"
    "translations/mx-datetime_da.ts"
    "translations/mx-datetime_de.ts"
    "translations/mx-datetime_el.ts"
    "translations/mx-datetime_en.ts"
    "translations/mx-datetime_es.ts"
    "translations/mx-datetime_es_ES.ts"
    "translations/mx-datetime_fi.ts"
    "translations/mx-datetime_fr.ts"
    "translations/mx-datetime_fr_BE.ts"
    "translations/mx-datetime_gl_ES.ts"
    "translations/mx-datetime_he_IL.ts"
    "translations/mx-datetime_hi.ts"
    "translations/mx-datetime_hr.ts"
    "translations/mx-datetime_hu.ts"
    "translations/mx-datetime_it.ts"
    "translations/mx-datetime_ja.ts"
    "translations/mx-datetime_lt.ts"
    "translations/mx-datetime_nb.ts"
    "translations/mx-datetime_nl.ts"
    "translations/mx-datetime_nl_BE.ts"
    "translations/mx-datetime_pl.ts"
    "translations/mx-datetime_pt.ts"
    "translations/mx-datetime_pt_BR.ts"
    "translations/mx-datetime_ru.ts"
    "translations/mx-datetime_ru_RU.ts"
    "translations/mx-datetime_sl.ts"
    "translations/mx-datetime_sq.ts"
    "translations/mx-datetime_sv.ts"
    "translations/mx-datetime_tr.ts"
    LRELEASE_OPTIONS -compress -nounfinished -removeidentical -silent
)

target_link_libraries(mx-datetime PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Widgets
)

build_compilation_setup(mx-datetime)
build_version_definition(mx-datetime VERSION)

target_compile_definitions(mx-datetime PRIVATE
    QT_DISABLE_DEPRECATED_UP_TO=0x060400
)

include(GNUInstallDirs)
install(TARGETS mx-datetime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
