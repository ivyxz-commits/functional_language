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
    src/run/main.cpp
    src/run/cli/options.cpp
    src/run/debug/dump.cpp
    src/run/pipeline.cpp
    src/lexer/lexer.cpp
    src/parser/parser_base.cpp
    src/parser/parser_decls.cpp
    src/parser/parser_exprs.cpp
    src/parser/parser_patterns.cpp
    src/parser/parser_types.cpp
    src/semantic/semantic_types.cpp
    src/semantic/semantic_utilities.cpp
    src/semantic/semantic_decls.cpp
    src/semantic/semantic_exprs.cpp
    src/semantic/semantic_call.cpp
    src/semantic/semantic_match.cpp
    src/codegen/codegen_utilities.cpp
    src/codegen/codegen_decls.cpp
    src/codegen/codegen_exprs.cpp
    src/codegen/codegen_match.cpp)

target_include_directories(lang PRIVATE
    src
    src/run
    src/run/cli
    src/run/debug
    src/lexer
    src/parser
    src/semantic/headers_semantic
    src/codegen/codegen_headers)

target_compile_options(lang PRIVATE
    -Wall -Wextra)

add_dependencies(lang runtime_files)

target_link_libraries(lang PRIVATE
    ${CMAKE_BINARY_DIR}/runtime_functions.o
    c)

# цель build
add_custom_target(build DEPENDS lang)

# цель run
add_custom_target(run
    COMMAND ${CMAKE_BINARY_DIR}/lang ${CMAKE_SOURCE_DIR}/test.txt
    COMMAND nasm -f elf64 ${CMAKE_BINARY_DIR}/output/output.asm
        -o ${CMAKE_BINARY_DIR}/output/output.o
    COMMAND gcc ${CMAKE_BINARY_DIR}/output/output.o
        ${CMAKE_BINARY_DIR}/runtime_functions.o
        -o ${CMAKE_BINARY_DIR}/output/program
        -nostartfiles -no-pie
    COMMAND ${CMAKE_BINARY_DIR}/output/program
    DEPENDS lang)

# цель debug - с отладочными символами
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