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
    std::size_t m_pos = 0; //текущий индекс в m_pos

    //основные функции работы с токенами
    const Lexer::Token& peek(std::size_t offset = 0) const;
    const Lexer::Token& current() const;
    bool atEnd() const;
    Lexer::SourcePos currentPos() const; //вернуть позицию текущего токена

    const Lexer::Token& advance();
    bool check(Lexer::TokenType t) const; //проверить тип токена
    bool match(Lexer::TokenType t); //"съесть" токен

    //взять токен ожидаемого типа, либо выдать ошибку
    std::expected<Lexer::Token, ParseError> expect(Lexer::TokenType t);

    //формирование ParseError
    ParseError makeError(std::string msg) const; //В текущем месте ошибка
    ParseError makeErrorAt(std::string msg, Lexer::SourcePos pos) const; //ошибка там, где запомнили позицию раньше
    //допустим если необходимо сказать об ошибке в самом блоке выражения, а не текущем месте

    //разбор объявлений 
    std::expected<Ptr<DeclNode>, ParseError> parseDecl(); //финальный узел дерева
    std::expected<FuncDecl, ParseError> parseFuncDecl();  //промежуточный результат
    std::expected<TypeAliasDecl, ParseError> parseTypeAliasDecl();
    std::expected<DataDecl, ParseError> parseDataDecl();
    std::expected<ModuleDecl, ParseError> parseModuleDecl();

    //разбор типов
    std::expected<Ptr<TypeNode>, ParseError> parseType();
    std::expected<Ptr<TypeNode>, ParseError> parseAtomicType(); //во избежание бесконечного зацикливания parseType()

    std::expected<Ptr<PatternNode>, ParseError> parsePattern();
    std::expected<Ptr<PatternNode>, ParseError> parsePrimaryPattern(); 

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
    std::expected<Ptr<ExprNode>, ParseError> parsePostfix(); //цепочка операций после основного выражения
    std::expected<Ptr<ExprNode>, ParseError> parsePrimary();

    //вспомогательные функции
    std::expected<std::vector<Ptr<ExprNode>>, ParseError> parseArgList(); //работает в связке с parsePostfix()
    std::expected<std::optional<Ptr<TypeNode>>, ParseError> parseOptionalType(); //для let и mut

    //для функции
    std::expected<FuncParam, ParseError> parseFuncParam();

    //для ADT - объявления
    //data name[typeParams1, typeParams2] = | None | First(type1) | Second(type2) //typeParams ::= '[' IDENT (',' IDENT)* ']'
    std::expected<std::vector<std::string>, ParseError> parseTypeParams();
    //IDENT '(' type (',' type)* ')' - разбираем один конструктор ADT
    std::expected<ConstructorDecl, ParseError> parseConstructorDecl();
      std::expected<FieldDecl, ParseError> parseFieldDecl(); //именованные поля в конструкторе () {}

    //для типов
    std::expected<Ptr<TypeNode>, ParseError> parseListType();
    std::expected<Ptr<TypeNode>, ParseError> parseTupleOrGroupedType();
    std::expected<Ptr<TypeNode>, ParseError> parseBuiltinType();
    std::expected<Ptr<TypeNode>, ParseError> parseUserType();

    //для шаблонов
    std::expected<Ptr<PatternNode>, ParseError> parseListPattern();
    std::expected<Ptr<PatternNode>, ParseError> parseTupleOrGroupedPattern();
    std::expected<Ptr<PatternNode>, ParseError> parseIdentPattern();
    
};

}