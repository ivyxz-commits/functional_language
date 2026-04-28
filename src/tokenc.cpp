#pragma once

#include <string>

namespace Lexer{ 

//перечисление типов токенов
enum class TokenType{ 

    //ключевые слова
    KW_FN,
    KW_MAIN,
    KW_IF,
    KW_THEN,
    KW_ELSE, 
    KW_MATCH,
    KW_TYPE, //синоним типа
    KW_DATA, //Algebraic data type
    KW_MODULE,
    KW_LET,
    KW_IN,   //let in
    KW_MUT,
    KW_AND, 
    KW_OR,
    KW_NOT,
    KW_UNIT,

    //встроенные типы
    KW_INT8,
    KW_INT16,
    KW_INT32,
    KW_INT64,
    KW_UINT8,
    KW_UINT16,
    KW_UINT32,
    KW_UINT64,
    KW_FLOAT32,
    KW_FLOAT64,
    KW_BOOL,
    KW_STRING,

    //литералы, (как значение записано в коде)
    LIT_INT,
    LIT_REAL,
    LIT_STRING,
    LIT_YEP,
    LIT_NOPE,

    //Идентификатор - пользовательское имя, переменная, функция, тип, конструктор 
    IDENT,

    //арифметические операторы
    OP_PLUS,
    OP_MINUS,
    OP_STAR,
    OP_SLASH,
    OP_PERCENT,
    
    //операторы сравнения
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_LE, 
    OP_GT,
    OP_GE,


    //служебные операторы, то есть операторы для синтаксиса языка
    OP_ASSIGN,   //(назначить) let = 
    OP_ARROW,    //lyamda, match
    OP_DOT,      //доступ к полю
    OP_COLON,    // x : int64 - явное указание типа
    OP_BACKSLASH, //lyamda
    OP_PIPE,      //ADT

    //разделители, скобка и запятая к примеру
    DELIM_LPAREN,
    DELIM_RPAREN,
    DELIM_LBRACKET,
    DELIM_RBRACKET,
    DELIM_LBRACE, //{
    DELIM_RBRACE, 
    DELIM_COMMA,

    //служебный 
    END_OF_FILE //конец файла, помощь парсеру, указать факт, что токены закончились
};

//возвратим читаемое имя типа токена (чтобы отлаживать и сообщать об ошибках)
constexpr const char* TokenTypeName(TokenType t) noexcept { 
    switch(t){ 
        case TokenType::KW_FN: return "fn";
        case TokenType::KW_IF: return "if";
        case TokenType::KW_THEN: return "then";
        case TokenType::KW_ELSE: return "else";
        case TokenType::KW_MATCH: return "match";
        case TokenType::KW_TYPE: return "type";
        case TokenType::KW_DATA: return "data";
        case TokenType::KW_MODULE: return "module";
        case TokenType::KW_LET: return "let";
        case TokenType::KW_IN: return "in";
        case TokenType::KW_MUT: return "mut";
        case TokenType::KW_AND: return "and";
        case TokenType::KW_OR: return "or";
        case TokenType::KW_NOT: return "not";
        case TokenType::KW_UNIT: return "unit";
        case TokenType::KW_INT8: return "int8";
        case TokenType::KW_INT16: return "int16";
        case TokenType::KW_INT32: return "int32";
        case TokenType::KW_INT64: return "int64";
        case TokenType::KW_UINT8: return "uint8";
        case TokenType::KW_UINT16: return "uint16";
        case TokenType::KW_UINT32: return "uint32";
        case TokenType::KW_UINT64: return "uint64";
        case TokenType::KW_FLOAT32: return "float32";
        case TokenType::KW_FLOAT64: return "float64";
        case TokenType::KW_BOOL: return "bool";
        case TokenType::KW_STRING: return "string";
        case TokenType::LIT_INT: return "<int>";
        case TokenType::LIT_REAL: return "<real>";
        case TokenType::LIT_STRING: return "<string>";
        case TokenType::LIT_YEP: return "yep";
        case TokenType::LIT_NOPE: return "nope";
        case TokenType::IDENT: return "<ident>";
        case TokenType::OP_PLUS: return "+";
        case TokenType::OP_MINUS: return "-";
        case TokenType::OP_STAR: return "*";
        case TokenType::OP_SLASH: return "/";
        case TokenType::OP_PERCENT: return "%";
        case TokenType::OP_EQ: return "=";
        case TokenType::OP_LT: return "<";
        case TokenType::OP_LE: return "<=";
        case TokenType::OP_GT: return ">";
        case TokenType::OP_GE: return ">=";
        case TokenType::OP_ASSIGN: return "="; //назначить
        case TokenType::OP_ARROW: return "->";
        case TokenType::OP_DOT: return ".";
        case TokenType::OP_COLON: return ":";
        case TokenType::OP_BACKSLASH: return "\\";
        case TokenType::OP_PIPE: return "|";
        case TokenType::DELIM_LPAREN: return "(";
        case TokenType::DELIM_RPAREN: return ")";
        case TokenType::DELIM_LBRACKET: return "[";
        case TokenType::DELIM_RBRACKET: return "]";
        case TokenType::DELIM_LBRACE: return "{";
        case TokenType::DELIM_RBRACE: return "}";
        case TokenType::DELIM_COMMA: return "(";
        case TokenType::END_OF_FILE: return "<EOF>";
        default: return "<unknown>"; //если неизвестный нам тип
    }
}

}