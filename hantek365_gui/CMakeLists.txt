cmake_minimum_required(VERSION 3.16)
project(Hantek365GUI LANGUAGES CXX)

# CI passes the release tag; local builds report "dev"
set(APP_VERSION "dev" CACHE STRING "Application version shown in the window title")

# Arch Linux: sudo pacman -S qt6-base qt6-serialport qt6-charts

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort Charts)

add_executable(hantek365_gui
    main.cpp
    mainwindow.h
    mainwindow.cpp
    hantekdevice.h
    hantekdevice.cpp
    usbbulkport.h
    usbbulkport.cpp
)

target_link_libraries(hantek365_gui PRIVATE
    Qt6::Widgets
    Qt6::SerialPort
    Qt6::Charts
)

target_compile_definitions(hantek365_gui PRIVATE APP_VERSION="${APP_VERSION}")

# GUI tests need Qt Test and Python 3 (meter emulator); off for regular builds
option(BUILD_TESTING "Build the GUI tests" OFF)
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
