cmake_minimum_required(VERSION 3.24)

project(OIDM
    VERSION 0.1.0
    DESCRIPTION "Open-source download manager for Windows and Linux"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets)
find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

qt_standard_project_setup()

qt_add_executable(OIDM
    src/main.cpp
    src/core/DownloadInfo.h
    src/core/DownloadEngine.h
    src/core/DownloadEngine.cpp
    src/core/DownloadRepository.h
    src/core/DownloadRepository.cpp
    src/ui/AddDownloadDialog.h
    src/ui/AddDownloadDialog.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
)

target_include_directories(OIDM PRIVATE src)
target_link_libraries(OIDM PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    CURL::libcurl
    SQLite::SQLite3
    Threads::Threads
)

target_compile_definitions(OIDM PRIVATE
    OIDM_VERSION="\${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
)

if(MSVC)
    target_compile_options(OIDM PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(OIDM PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

set_target_properties(OIDM PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE FALSE
    OUTPUT_NAME "OIDM"
)

include(GNUInstallDirs)
install(TARGETS OIDM
    RUNTIME DESTINATION \${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    qt_add_executable(oidm_engine_tests
        tests/engine_tests.cpp
        src/core/DownloadEngine.h
        src/core/DownloadEngine.cpp
    )
    target_include_directories(oidm_engine_tests PRIVATE src)
    target_link_libraries(oidm_engine_tests PRIVATE
        Qt6::Core
        Qt6::Test
        CURL::libcurl
        Threads::Threads
    )
    add_test(NAME oidm_engine_tests COMMAND oidm_engine_tests)
endif()
