
cmake_minimum_required(VERSION 3.16)

project(TouchFlow
    VERSION 1.0.0
    DESCRIPTION "Linux touchpad gesture controller"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)

pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)

add_executable(touchflow
    src/main.cpp
)

target_link_libraries(touchflow
    PRIVATE
    PkgConfig::LIBEVDEV
)

target_compile_options(touchflow PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)
