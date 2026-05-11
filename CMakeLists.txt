#если cmake - это генератор генераторов то make это конкретный сценарий по которому компилятор сейчас генерирует код
cmake_minimum_required(VERSION 3.20)
project(lang CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON) #если компилятор не поддерживает C++23 ошибка сборки

#собрать исполняемый файл
add_executable(lang

    src/main.cpp

    src/lexer/lexer.cpp

    src/parser/parser_base.cpp
    src/parser/parser_decls.cpp
    src/parser/parser_exprs.cpp
    src/parser/parser_patterns.cpp
    src/parser/parser_types.cpp
)

#ищет заголовочные файлы | src только для lang
target_include_directories(lang PRIVATE
    src
    src/lexer
    src/parser
)

#cmake -B build - папку build создать
#cmake --build build - собрать / скомпилировать все .cpp и слинковать в исполняемый файл
#./build/myc file.txt --dump-tokens --dump-ast 