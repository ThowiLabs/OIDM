cmake_minimum_required(VERSION 3.25)

project(OIDM
    VERSION 0.4.0
    DESCRIPTION "Open Internet Download Manager"
    HOMEPAGE_URL "https://github.com/ThowiLabs/OIDM"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.6 REQUIRED COMPONENTS Core Gui Widgets Network Sql)

qt_standard_project_setup(REQUIRES 6.6)

add_executable(oidm WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/integration/native_bridge.h
    src/integration/native_bridge.cpp
    src/core/download.h
    src/core/download.cpp
    src/core/download_repository.h
    src/core/download_repository.cpp
    src/core/download_task.h
    src/core/download_task.cpp
    src/core/file_publish.h
    src/core/file_publish.cpp
    src/core/download_manager.h
    src/core/download_manager.cpp
    src/ui/download_table_model.h
    src/ui/download_table_model.cpp
    src/ui/progress_delegate.h
    src/ui/progress_delegate.cpp
    src/ui/add_download_dialog.h
    src/ui/add_download_dialog.cpp
    src/ui/download_progress_dialog.h
    src/ui/download_progress_dialog.cpp
    src/ui/browser_setup_dialog.h
    src/ui/browser_setup_dialog.cpp
    src/ui/settings_dialog.h
    src/ui/settings_dialog.cpp
    src/ui/download_offer_dialog.h
    src/ui/download_offer_dialog.cpp
    src/ui/main_window.h
    src/ui/main_window.cpp
)

add_executable(oidm-native-host
    src/integration/native_host.cpp
    src/integration/native_bridge.h
    src/integration/native_bridge.cpp
)
target_include_directories(oidm-native-host PRIVATE src)
target_link_libraries(oidm-native-host PRIVATE Qt6::Core Qt6::Network)
if(WIN32)
    target_link_libraries(oidm-native-host PRIVATE advapi32)
endif()

target_include_directories(oidm PRIVATE src)
target_link_libraries(oidm PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
)
target_compile_definitions(oidm PRIVATE
    OIDM_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

if(MSVC)
    target_compile_options(oidm PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(oidm PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

install(TARGETS oidm-native-host RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(TARGETS oidm
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(BUILD_TESTING)
    add_test(NAME oidm_smoke COMMAND oidm --self-test)
    add_test(NAME oidm_ui_smoke COMMAND oidm --ui-self-test)
    set_tests_properties(oidm_ui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    set_tests_properties(oidm_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    find_package(Qt6 6.6 REQUIRED COMPONENTS Test)
    qt_add_executable(oidm_download_tests
        tests/download_task_tests.cpp
        tests/download_task_tests.h
        src/core/download.h
        src/core/download.cpp
        src/core/download_task.h
        src/core/download_task.cpp
        src/core/file_publish.h
        src/core/file_publish.cpp
        src/core/download_manager.h
        src/core/download_manager.cpp
        src/core/download_repository.h
        src/core/download_repository.cpp
    )
    target_include_directories(oidm_download_tests PRIVATE src)
    target_link_libraries(oidm_download_tests PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Sql
        Qt6::Test
    )
    add_test(NAME oidm_download_tests COMMAND oidm_download_tests)
    set_tests_properties(oidm_download_tests PROPERTIES ENVIRONMENT "LC_ALL=C.UTF-8")
endif()
