find_package(Qt6 REQUIRED COMPONENTS Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(gui_test
    gui_test.cpp
    ../mainwindow.h
    ../mainwindow.cpp
    ../hantekdevice.h
    ../hantekdevice.cpp
    ../usbbulkport.h
    ../usbbulkport.cpp
)
target_include_directories(gui_test PRIVATE ..)
target_link_libraries(gui_test PRIVATE
    Qt6::Widgets
    Qt6::SerialPort
    Qt6::Charts
    Qt6::Test
)
target_compile_definitions(gui_test PRIVATE
    PYTHON_EXECUTABLE="${Python3_EXECUTABLE}"
    METER_EMULATOR="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/meter_emulator.py"
)

add_test(NAME gui_test COMMAND gui_test)
set_tests_properties(gui_test PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
