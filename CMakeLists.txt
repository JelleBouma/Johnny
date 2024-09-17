cmake_minimum_required(VERSION 3.16.3)
project(Johnny C)

set(CMAKE_C_STANDARD 11)

file(GLOB_RECURSE CMPH RELATIVE ${CMAKE_SOURCE_DIR} "cmph/src/*.c")

add_executable(Johnny johnny.c file_io.c file_io.h ${CMPH}
        johnny_config.h
        johnny_global.c
        johnny_global.h
        johnny_setup.c
        johnny_setup.h
        johnny_worker.c
        johnny_worker.h
        johnny_worker_tls.c
        johnny_worker_tls.h)

target_link_libraries(Johnny PRIVATE m)
target_link_libraries(Johnny PRIVATE pthread)

add_compile_options(-Ofast)