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

add_executable(nyst
    src/main.cpp
    src/util/debug_log.cpp
    src/model/unit.cpp
    src/model/unit_graph.cpp
    src/source/systemd_bus.cpp
    src/source/package_db.cpp
    src/source/classifier.cpp
    src/source/loader.cpp
)

target_include_directories(nyst PRIVATE src)
target_compile_options(nyst PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(nyst PRIVATE PkgConfig::SDBUS PkgConfig::ALPM)

install(TARGETS nyst RUNTIME DESTINATION bin)
