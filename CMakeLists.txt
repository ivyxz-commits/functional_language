cmake_minimum_required(VERSION 3.20)
project(lang CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# компилируем runtime_functions.c
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/runtime_functions.o
    COMMAND gcc -c ${CMAKE_SOURCE_DIR}/src/runtime_functions.c
            -o ${CMAKE_BINARY_DIR}/runtime_functions.o
    DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime_functions.c)

add_custom_target(runtime_files DEPENDS
    ${CMAKE_BINARY_DIR}/runtime_functions.o)

add_executable(lang
    src/main.cpp
    src/lexer/lexer.cpp
    src/parser/parser_base.cpp
    src/parser/parser_decls.cpp
    src/parser/parser_exprs.cpp
    src/parser/parser_patterns.cpp
    src/parser/parser_types.cpp
    src/semantic/semantic.cpp
    src/codegen/codegen.cpp)

target_include_directories(lang PRIVATE
    src
    src/lexer
    src/parser
    src/semantic
    src/codegen)

target_compile_options(lang PRIVATE
    -Wall -Wextra)

add_dependencies(lang runtime_files)

target_link_libraries(lang PRIVATE
    ${CMAKE_BINARY_DIR}/runtime_functions.o
    c)

# цель build (по умолчанию)
add_custom_target(build DEPENDS lang)

# цель run
add_custom_target(run
    COMMAND ${CMAKE_BINARY_DIR}/lang ${CMAKE_SOURCE_DIR}/start.lang
    COMMAND nasm -f elf64 ${CMAKE_SOURCE_DIR}/output/output.asm
            -o ${CMAKE_SOURCE_DIR}/output/output.o
    COMMAND gcc ${CMAKE_SOURCE_DIR}/output/output.o
            ${CMAKE_BINARY_DIR}/runtime_functions.o
            -o ${CMAKE_SOURCE_DIR}/output/program
            -nostartfiles -no-pie
    COMMAND ${CMAKE_SOURCE_DIR}/output/program
    DEPENDS lang)

# цель debug — с отладочными символами
add_custom_target(debug
    COMMAND ${CMAKE_COMMAND} -DCMAKE_BUILD_TYPE=Debug ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
    DEPENDS lang)

target_compile_options(lang PRIVATE
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O2>)

# цель clean
add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_SOURCE_DIR}/output)