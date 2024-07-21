cmake_minimum_required(VERSION 3.16.3)
project(Johnny C)

set(CMAKE_C_STANDARD 11)

file(GLOB_RECURSE CMPH RELATIVE ${CMAKE_SOURCE_DIR} "cmph/src/*.c")

add_executable(Johnny johnny.c file_io.c file_io.h ${CMPH})

target_link_libraries(Johnny PRIVATE m)
target_link_libraries(Johnny PRIVATE pthread)