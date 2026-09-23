cmake_minimum_required(VERSION 3.20)
project(nyst LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDBUS REQUIRED IMPORTED_TARGET sdbus-c++>=2)
pkg_check_modules(ALPM REQUIRED IMPORTED_TARGET libalpm)

# FTXUI is not packaged in the Arch repos, so pin a release.
include(FetchContent)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v7.0.3
    GIT_SHALLOW TRUE
    SYSTEM
)
FetchContent_MakeAvailable(ftxui)

add_executable(nyst
    src/main.cpp
    src/util/debug_log.cpp
    src/model/unit.cpp
    src/model/unit_graph.cpp
    src/source/systemd_bus.cpp
    src/source/package_db.cpp
    src/source/classifier.cpp
    src/source/loader.cpp
    src/source/journal.cpp
    src/ui/app.cpp
    src/ui/details_pane.cpp
    src/ui/filters.cpp
    src/ui/journal_pane.cpp
    src/ui/tree_view.cpp
)

target_include_directories(nyst PRIVATE src)
target_compile_options(nyst PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(nyst PRIVATE
    PkgConfig::SDBUS
    PkgConfig::ALPM
    ftxui::component
    ftxui::dom
    ftxui::screen
)

install(TARGETS nyst RUNTIME DESTINATION bin)
