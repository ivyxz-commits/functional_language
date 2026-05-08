
#include "parser.hpp"

namespace Parser{ 

Parser::Parser(std::vector<Lexer::Token> tokens, std::string filename)
    : m_tokens(std::move(tokens)), m_filename(std::move(filename)) {}


//точка входа через Program
std::expected<Program, ParseError> Parser::parse(){ 
    Program prog; //пустой объект программы

    while(!atEnd()){
        auto decl = parseDecl();
        if(!decl) return std::unexpected(decl.error()); //просто читаем ошибку //ссылка на объект ошибки &
        prog.decls.push_back(std::move(*decl)); //ссылка на то, что лежит в expected<>
    }

    return prog;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//основные функции работы с токенами
const Lexer::Token& Parser::peek(std::size_t offset) const{ 
    std::size_t idx = m_pos + offset;
    if(idx >= m_tokens.size()) return m_tokens.back(); //EOF
    return m_tokens[idx];
}

const Lexer::Token& Parser::current() const{ return peek(0); }

bool Parser::atEnd() const { 
    return current().type == Lexer::TokenType::END_OF_FILE;
}

Lexer::SourcePos Parser::currentPos() const { 
    return current().pos; 
}

const Lexer::Token& Parser::advance(){ 
    if(!atEnd) m_pos++;
    return m_tokens[m_pos - 1];
}

bool Parser::check(Lexer::TokenType t) const { 
    return current().type == t;
}

bool Parser::match(Lexer::TokenType t){ 
    if(check(t)) {advance(); return true; }
    return false;
}

//обработка ошибок
ParseError Parser::makeError(std::string msg) const { 
    return{std::move(msg), currentPos()};
}

ParseError Parser::makeErrorAt(std::string msg, Lexer::SourcePos pos) const { 
    return {std::move(msg), pos};
}

//токен ожидаемого типа или ошибка
std::expected<Lexer::Token, ParseError> Parser::expect(Lexer::TokenType t){ 
    if(check(t)) return advance();
    std::string msg = std::string("expected '") + std::string(Lexer::TokenTypeName(t)) + 
    "', got '" + current().lexeme + "'";
    return std::unexpected(makeError(std::move(msg)));
}

}