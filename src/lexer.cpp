#pragma once

//можем использовать для отладки программ, путем проверки логических условий во время выполнения (в runtime)
//в целом, на лекциях говорилось об возможности использования
#include <cassert>
#include "lexer.hpp"

namespace Lexer{ 

Lexer::Lexer(std::string source, std::string filename) 
    : m_source(std::move(source)), m_filename(std::move(filename)) {}


//основной метод разбиения на токены
//декларативный подход присутствует
std::expected<std::vector<Token>, LexError> Lexer::tokenize(){ 
    std::vector<Token> tokens;
    while(true){ 
        skipWhitespacesandComments();

            if(atEnd()){ 
                tokens.emplace_back(TokenType::END_OF_FILE, "", CurrentPos());
                break;
            }
            auto result = nextToken();
            if(!result){ 
                return(std::unexpected(result.error())); //достаем ошибку, тем самым наш LexError
            }
            tokens.push_back(std::move(*result)); 
    }

    return tokens;
}



}


