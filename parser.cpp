#pragma once

#include "parser.hpp"

namespace Parser{ 

Parser::Parser(std::vector<Lexer::Token> tokens, std::string filename)
    : m_tokens(std::move(tokens)), m_filename(std::move(filename)) {}


//точка входа через Program
std::expected<Program, ParseError> Parser::parse(){ 
    Program prog; //пустой объект программы

    while(!atEnd()){ 
    }
}


}