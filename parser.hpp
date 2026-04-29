#pragma once

#include "ast.hpp"
#include <vector>
#include <expected>

namespace Parser{ 

struct ParseError{ 
    std::string message;
    Lexer::SourcePos pos;

    //функция форматирования ошибки в вид file:line:column: error: msg - Аналогично лексеру
    //нарушает правила связи (слов) лексических единиц
    std::string format(const std::string& filename) const { 
        return filename + ":" + std::to_string(pos.line) + ":" + 
            std::to_string(pos.column) + ": error: syntax error: " + message; 
    }
};

class Parser{ 
public:
    Parser(std::vector<Lexer::Token> tokens, std::string filename = "<input>");
    std::expected<Program, ParseError> parse();  //аналогично tokenize() в public

private:
    std::vector<Lexer::Token> m_tokens;
    std::string m_filename;
    std::size_t m_pos; //текущий индекс в m_pos

    const Lexer::Token& peek(std::size_t offset = 0) const;
    const Lexer::Token& current() const;
    bool atEnd() const;
    Lexer::SourcePos currentPos() const; //вернуть позицию текущего токена

    const Lexer::Token& advance();
    bool check(Lexer::TokenType t) const; //проверить тип токена
    bool match(Lexer::TokenType t); //"съесть" токен

    //взять токен ожидаемого типа, либо выдать ошибку
    std::expected<Lexer::Token, ParseError> expect(Lexer::TokenType t);

    //формирование ParseError для настоящей позиции
    ParseError makeError(std::string msg) const;
    ParseError makeErrorAt(std::string msg, Lexer::SourcePos pos) const; 

    

    //Разбор выражений
    std::expected<Ptr<ExprNode>, ParseError> parseExpr();
    std::expected<Ptr<ExprNode>, ParseError> parseLetInExpr();
    std::expected<Ptr<ExprNode>, ParseError> parseIfExpr();
    std::expected<Ptr<ExprNode>, ParseError> parseMatchExpr();
    std::expected<Ptr<ExprNode>, ParseError> parseLambdaExpr();   
    std::expected<Ptr<ExprNode>, ParseError> parseLogicalOr();
    std::expected<Ptr<ExprNode>, ParseError> parseLogicalAnd();
    std::expected<Ptr<ExprNode>, ParseError> parseEquality(); 
    std::expected<Ptr<ExprNode>, ParseError> parseComparison();    
    std::expected<Ptr<ExprNode>, ParseError> parseAdditive();
    std::expected<Ptr<ExprNode>, ParseError> parseMultiplicative();
    std::expected<Ptr<ExprNode>, ParseError> parseUnary();
    std::expected<Ptr<ExprNode>, ParseError> parsePostfix();
    std::expected<Ptr<ExprNode>, ParseError> parsePrimary();

};

}