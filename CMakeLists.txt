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
    src/semantic.cpp
    src/codegen.cpp)

target_include_directories(lang PRIVATE
    src
    src/lexer
    src/parser)

add_dependencies(lang runtime_files)

target_link_libraries(lang PRIVATE
    ${CMAKE_BINARY_DIR}/runtime_functions.o
    c)