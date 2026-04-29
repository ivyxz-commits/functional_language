#pragma once

//можем использовать для отладки программ, путем проверки логических условий во время выполнения (в runtime)
//в целом, на лекциях говорилось об возможности использования
#include <cassert>
#include "lexer.hpp"

namespace Lexer{ 

//конструктор
Lexer::Lexer(std::string source, std::string filename) 
    : m_source(std::move(source)), m_filename(std::move(filename)) {}


//основной метод разбиения на токены
//декларативный подход присутствует
std::expected<std::vector<Token>, LexError> Lexer::tokenize(){ 
    std::vector<Token> tokens;
    while(true){ 
        skipWhitespacesandComments();

            if(atEnd()){ 
                tokens.emplace_back(TokenType::END_OF_FILE, "", currentPos());  //объекты конструктора прямо на свое место
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//вспомогательные функции
bool Lexer::atEnd() const { 
    return m_pos >= m_source.size(); 
}

char Lexer::peek(std::size_t offset) const { 
    std::size_t idx = m_pos + offset;
    if(idx >= m_source.size()) return '\0'; //Like the end of string
    return m_source[idx];
}

//взятие символа со сдвигом курсора
char Lexer::advance(){ 
    char c = m_source[m_pos++];
    if(c == '\n'){ 
        m_line++; m_col = 1;
    } else { 
        m_col++;
    }
    return c;
}

//проверка на совпадение, и взятие символа
bool Lexer::match(char expected){ 
    if(atEnd() || m_source[m_pos] != expected){ 
        return false;
    }
    advance();
    return true;
}

SourcePos Lexer:: currentPos() const { 
    return {m_line, m_col};
}

//формирование ошибок
LexError Lexer::makeError(std::string msg) const { 
    return{std::move(msg), currentPos()};
}



void Lexer::skipWhitespacesandComments(){ 
    while(!atEnd()){ 
        char c = peek();

        if(std::isspace(static_cast<unsigned char>(c))){ 
            advance();
        } else if (c == '/' && peek(1) == '/') { 
            while(!atEnd() && peek() != '\n'){ 
                advance();
            }
        } else { 
            break;
        }
    }
}


}


