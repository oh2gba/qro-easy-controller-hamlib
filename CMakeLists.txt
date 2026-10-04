cmake_minimum_required(VERSION 3.21)
project(qro-easy-controller-hamlib VERSION 0.1.0 DESCRIPTION "Switch a QRO.cz Easy Controller 6-2 by the band of a Hamlib radio" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(ech_core STATIC
    src/bands.cpp src/bands.h
    src/config.cpp src/config.h
    src/logbus.cpp src/logbus.h
    src/websocketclient.cpp src/websocketclient.h
    src/rigclient.cpp src/rigclient.h
    src/easycontroller.cpp src/easycontroller.h
    src/discovery.cpp src/discovery.h
    src/engine.cpp src/engine.h)
target_include_directories(ech_core PUBLIC src)
target_compile_definitions(ech_core PUBLIC ECH_VERSION="${PROJECT_VERSION}")
target_link_libraries(ech_core PUBLIC Qt6::Core Qt6::Network)

add_library(ech_ui STATIC
    src/statusdot.cpp src/statusdot.h
    src/mainwindow.cpp src/mainwindow.h
    src/settingsdialog.cpp src/settingsdialog.h
    src/desktopintegration.cpp src/desktopintegration.h)
target_link_libraries(ech_ui PUBLIC ech_core Qt6::Widgets)

set(APP qro-easy-controller-hamlib)
set(APP_DISPLAY_NAME "Easy Controller Hamlib")

add_executable(${APP} WIN32 MACOSX_BUNDLE src/main.cpp data/${APP}.qrc)
target_link_libraries(${APP} PRIVATE ech_ui)
target_compile_definitions(${APP} PRIVATE ECH_DISPLAY_NAME="${APP_DISPLAY_NAME}")

if(WIN32)
    set(ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/data/${APP}.ico") # rc.exe accepts forward slashes
    configure_file(data/windows.rc.in ${CMAKE_CURRENT_BINARY_DIR}/windows.rc @ONLY)
    target_sources(${APP} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/windows.rc)
endif()

if(APPLE)
    set(MACOS_ICON data/${APP}.icns)
    set_source_files_properties(${MACOS_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(${APP} PRIVATE ${MACOS_ICON})
    set_target_properties(${APP} PROPERTIES
        OUTPUT_NAME "${APP_DISPLAY_NAME}"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/data/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "${APP_DISPLAY_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.oh2gba.${APP}
        MACOSX_BUNDLE_ICON_FILE ${APP}.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION})
endif()

include(GNUInstallDirs)
install(TARGETS ${APP} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} BUNDLE DESTINATION .)
if(UNIX AND NOT APPLE)
    install(FILES data/${APP}.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES data/${APP}.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES data/icons/${size}.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME ${APP}.png)
    endforeach()
endif()

option(ECH_TESTS "Build the tests and the simulated Easy Controller (needs Qt6 Test and WebSockets)" OFF)
if(ECH_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test WebSockets)

    add_library(fakecontroller STATIC tests/fakecontroller.cpp tests/fakecontroller.h)
    target_include_directories(fakecontroller PUBLIC tests)
    target_link_libraries(fakecontroller PUBLIC Qt6::Core Qt6::WebSockets)

    add_executable(fake-easycontroller tests/fake_main.cpp)
    target_link_libraries(fake-easycontroller PRIVATE fakecontroller)

    foreach(name core rig engine discovery ui)
        add_executable(tst_${name} tests/tst_${name}.cpp tests/testutil.h data/${APP}.qrc)
        target_link_libraries(tst_${name} PRIVATE ech_ui fakecontroller Qt6::Test)
        add_test(NAME ${name} COMMAND tst_${name})
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endforeach()
endif()
