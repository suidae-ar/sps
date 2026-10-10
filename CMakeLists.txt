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

# s5io: E/A-Dienst für den Raspberry Pi (ohne Qt)
add_library(s5ioproto STATIC s5io/protocol.cpp)
target_include_directories(s5ioproto PUBLIC s5io)
target_compile_options(s5ioproto PRIVATE -Wall -Wextra -Wpedantic)

add_executable(s5io s5io/main.cpp s5io/server.cpp s5io/gpio_sim.cpp)
target_link_libraries(s5io PRIVATE s5ioproto)
target_compile_options(s5io PRIVATE -Wall -Wextra -Wpedantic)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(GPIOD IMPORTED_TARGET libgpiod>=2.0)
endif()
if(GPIOD_FOUND)
    target_sources(s5io PRIVATE s5io/gpio_gpiod.cpp)
    target_compile_definitions(s5io PRIVATE S5IO_HAVE_GPIOD)
    target_link_libraries(s5io PRIVATE PkgConfig::GPIOD)
    message(STATUS "s5io: libgpiod ${GPIOD_VERSION} gefunden – GPIO-Betrieb möglich")
else()
    message(STATUS "s5io: libgpiod 2 nicht gefunden – s5io nur mit --sim verwendbar")
endif()
install(TARGETS s5io RUNTIME DESTINATION bin)

add_executable(s5io_tests tests/test_s5io.cpp s5io/server.cpp s5io/gpio_sim.cpp)
target_link_libraries(s5io_tests PRIVATE s5ioproto)
add_test(NAME s5io_tests COMMAND s5io_tests)

# Oberfläche
find_package(Qt6 COMPONENTS Widgets Network QUIET)
if(Qt6_FOUND)
    qt_standard_project_setup()
    qt_add_executable(s5sim MACOSX_BUNDLE
        gui/main.cpp
        gui/mainwindow.cpp gui/mainwindow.h
        gui/codeeditor.cpp gui/codeeditor.h
        gui/awlhighlighter.cpp gui/awlhighlighter.h
        gui/plccontroller.cpp gui/plccontroller.h
        gui/rackmodules.cpp gui/rackmodules.h
        gui/rackwindow.cpp gui/rackwindow.h
        gui/pilink.cpp gui/pilink.h
        gui/piconfig.cpp gui/piconfig.h
        gui/piconfigdialog.cpp gui/piconfigdialog.h
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
    target_link_libraries(s5sim PRIVATE s5core s5ioproto Qt6::Widgets Qt6::Network)
    set_target_properties(s5sim PROPERTIES
        OUTPUT_NAME "S5Sim"
        MACOSX_BUNDLE_BUNDLE_NAME "S5Sim"
        MACOSX_BUNDLE_GUI_IDENTIFIER "eu.yatts.s5sim"
    )
else()
    message(STATUS "Qt6 nicht gefunden – es wird nur der Kern gebaut")
endif()
