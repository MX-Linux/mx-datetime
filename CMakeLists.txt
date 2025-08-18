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

cmake_minimum_required(VERSION 3.16)

# Get version from debian/changelog
execute_process(
    COMMAND dpkg-parsechangelog -SVersion
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE PROJECT_VERSION_FROM_CHANGELOG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE DPKG_RESULT
)

if(NOT DPKG_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to get version from debian/changelog using dpkg-parsechangelog")
endif()

# Extract numeric version (25.4) from full version (25.4mx23)
string(REGEX MATCH "^([0-9]+\\.[0-9]+)" PROJECT_VERSION_NUMERIC "${PROJECT_VERSION_FROM_CHANGELOG}")

project(mx-datetime
    VERSION ${PROJECT_VERSION_NUMERIC}
    DESCRIPTION "MX Date & Time - Date and time configuration tool for MX Linux"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compile commands export for IDEs and tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Optimize for Ninja builds
if(CMAKE_GENERATOR STREQUAL "Ninja")
    # Enable colored output for Ninja
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-fdiagnostics-color=always)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-fcolor-diagnostics)
    endif()
endif()

# Option to use clang for testing builds
option(USE_CLANG "Use clang compiler" OFF)
if(USE_CLANG)
    set(CMAKE_C_COMPILER clang)
    set(CMAKE_CXX_COMPILER clang++)
    set(CMAKE_CXX_COMPILER_ID "Clang")
    message(STATUS "Using clang compiler")
endif()

# Find Qt6 components
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    LinguistTools
)

# Enable automatic MOC, UIC, and RCC processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Define source files
set(SOURCES
    main.cpp
    datetime.cpp
    about.cpp
    clockface.cpp
)

set(HEADERS
    datetime.h
    about.h
    clockface.h
    version.h
)

set(UI_FILES
    datetime.ui
)

set(RESOURCE_FILES
    images.qrc
)

# Get all translation files
file(GLOB TRANSLATION_FILES "translations/*.ts")

# Create the executable
add_executable(mx-datetime
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${RESOURCE_FILES}
)

# Link Qt6 libraries
target_link_libraries(mx-datetime
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# Set compiler flags
target_compile_options(mx-datetime PRIVATE
    -Wpedantic
    -pedantic
    -Werror=return-type
    -Werror=switch
    -Werror=uninitialized
    -Werror
)

# Add compiler-specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
    target_compile_options(mx-datetime PRIVATE -Werror=return-stack-address)
else()
    target_compile_options(mx-datetime PRIVATE -Werror=return-local-addr)
endif()

# Set compile definitions
target_compile_definitions(mx-datetime PRIVATE
    QT_DEPRECATED_WARNINGS
    QT_DISABLE_DEPRECATED_UP_TO=0x060400
)

# Release-specific optimizations
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(mx-datetime PRIVATE NDEBUG)
    target_compile_options(mx-datetime PRIVATE -O3)
    
    # Add LTO - different flags for different compilers
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR USE_CLANG)
        target_compile_options(mx-datetime PRIVATE -flto=thin)
        target_link_options(mx-datetime PRIVATE -flto=thin)
    else()
        target_compile_options(mx-datetime PRIVATE -flto=auto)
        target_link_options(mx-datetime PRIVATE -flto=auto)
    endif()
endif()

# Handle translations
qt6_add_translations(mx-datetime
    TS_FILES ${TRANSLATION_FILES}
    QM_FILES_OUTPUT_VARIABLE qm_files
)

# Set target properties
set_target_properties(mx-datetime PROPERTIES
    OUTPUT_NAME "mx-datetime"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Add current source directory to include path for UIC
target_include_directories(mx-datetime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Install target (required by Debian build system)
# Other files are handled by debian/install
install(TARGETS mx-datetime
    RUNTIME DESTINATION bin
)
