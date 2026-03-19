cmake_minimum_required(VERSION 3.16)
project(Hantek365GUI VERSION 1.0 LANGUAGES CXX)

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
)

target_link_libraries(hantek365_gui PRIVATE
    Qt6::Widgets
    Qt6::SerialPort
    Qt6::Charts
)
