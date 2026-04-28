#pragma once

#include <string>
#include "tokens.hpp" 

namespace Lexer{ 

//хранение ошибок лексического анализа    
struct LexError{ 
    std::string message;
    SourcePos pos;

    //функция форматирования ошибки в вид file:line:column: error: msg
    std::string format(const std::string& filename) const { 
        return filename + ":" + std::to_string(pos.line) + ":" + 
            std::to_string(pos.column) + ": error: lexical error: " + message; 
    }
};



}