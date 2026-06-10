#pragma once

#include "ast.hpp"
#include <string>

namespace Semantic{

using Pos = Lexer::SourcePos;
using namespace Parser;


struct SemanticError{ 
    std::string message;
    Pos pos;

    //функция форматирования ошибки в вид file:line:column: error: msg - Аналогично парсеру
    //нарушение верного смысла
    std::string format(const std::string& filename) const { 
        return filename + ":" + std::to_string(pos.line) + ":" + 
            std::to_string(pos.column) + ": error: semantic error: " + message; 
    }
};

}