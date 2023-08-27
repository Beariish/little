cmake_minimum_required(VERSION 3.27)
project(little VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list (APPEND LITTLE_CORE_SOURCES
    "src/little_std.c"
    "src/little.c"
)

add_library(little-static STATIC ${LITTLE_CORE_SOURCES})
target_include_directories(little-static PUBLIC "src")

add_executable(little "main.c")
target_link_libraries(little PRIVATE little-static)
