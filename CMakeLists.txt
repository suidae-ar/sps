cmake_minimum_required(VERSION 3.21)
project(S5Sim VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Kern ohne Qt: Parser und CPU
add_library(s5core STATIC
    core/program.cpp
    core/parser.cpp
    core/cpu.cpp
    core/mc5.cpp
)
target_include_directories(s5core PUBLIC core)
target_compile_options(s5core PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(s5core_tests tests/test_core.cpp)
target_link_libraries(s5core_tests PRIVATE s5core)
target_compile_definitions(s5core_tests PRIVATE EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
add_test(NAME s5core_tests COMMAND s5core_tests)
add_executable(s5mc5_tests tests/test_mc5.cpp)
target_link_libraries(s5mc5_tests PRIVATE s5core)
target_compile_definitions(s5mc5_tests PRIVATE EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
add_test(NAME s5mc5_tests COMMAND s5mc5_tests)

# Oberfläche
find_package(Qt6 COMPONENTS Widgets QUIET)
if(Qt6_FOUND)
    qt_standard_project_setup()
    qt_add_executable(s5sim MACOSX_BUNDLE
        gui/main.cpp
        gui/mainwindow.cpp gui/mainwindow.h
        gui/codeeditor.cpp gui/codeeditor.h
        gui/awlhighlighter.cpp gui/awlhighlighter.h
        gui/iopanel.cpp gui/iopanel.h
        gui/watchpanel.cpp gui/watchpanel.h
        gui/format.cpp gui/format.h
        gui/mc5view.cpp gui/mc5view.h
    )
    qt_add_resources(s5sim "examples"
        PREFIX "/"
        FILES
            examples/grundlagen.awl
            examples/zeiten.awl
            examples/zaehler.awl
            examples/ampel.awl
            examples/bausteine.awl
            examples/fb_parameter.awl
    )
    target_link_libraries(s5sim PRIVATE s5core Qt6::Widgets)
    set_target_properties(s5sim PROPERTIES
        OUTPUT_NAME "S5Sim"
        MACOSX_BUNDLE_BUNDLE_NAME "S5Sim"
        MACOSX_BUNDLE_GUI_IDENTIFIER "eu.yatts.s5sim"
    )
else()
    message(STATUS "Qt6 nicht gefunden – es wird nur der Kern gebaut")
endif()
