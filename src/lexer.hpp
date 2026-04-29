#pragma once

#include <string>
#include <vector>
#include <expected> //шаблон для явной обработки ошибки std::expected<T, E>
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


//превращение текста в токены, наш лексер
class Lexer{ 

public:
    Lexer(std::string source, std::string filename = "<input>");
    std::expected<std::vector<Token>, LexError> tokenize(); //разбивает на токены

private: 
    std::string m_source;  //наш исходный текст
    std::string m_filename;

    //позиции в исходном тексте
    std::size_t m_pos = 0;
    int m_line = 1;
    int m_col = 1;

    //вспомогательные методы
    bool atEnd() const; //дошли ли до конца текста
    char peek(std::size_t offset = 0) const;
    char advance(); //взять символ, сдвинуть нашу позицию и вернуть символ
    bool match(char expected); //часто для логических операторов
    SourcePos currentPos() const; //текущая позиция

    void skipWhitespacesandComments();

    std::expected<Token, LexError> nextToken(); //прочитать один токен

    //проверяет является ли слово ключевым, либо это литерал
    //если слово простое и не зарезервированно, то возвращает IDENT
    static TokenType classifyWord(const std::string& word);    

    //отдельные функции для разных токенов
    std::expected<Token, LexError>scanNumber(SourcePos start);
    std::expected<Token, LexError>scanString(SourcePos start); //строковый литерал
    std::expected<Token, LexError>scanIdentOrKeyword(SourcePos start);

    LexError makeError(std::string msg) const; //удобная функция для создания ошибки

}; 

}