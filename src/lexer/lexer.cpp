#include "lexer.hpp"
#include <cctype>
#include <unordered_map>

namespace Lexer{ 

//конструктор
Lexer::Lexer(std::string source, std::string filename) 
    : m_source(std::move(source)), m_filename(std::move(filename)) {}

 
//основной метод разбиения на токены
//декларативный подход присутствует
LexResult Lexer::tokenize(){ 
    std::vector<Token> tokens;
    while(true){ 
        skipWhitespacesandComments();

            if(atEnd()){ 
                tokens.push_back(Token{TokenType::END_OF_FILE, "", currentPos()});  //объекты конструктора прямо на свое место
                break;
            }
            auto result = nextToken();
            if(!result){ 
                m_errors.push_back(result.error()); //достаем ошибку, тем самым наш LexError
                advance();
                continue;
            }

            tokens.push_back(std::move(*result)); 
    }

    return LexResult{std::move(tokens), std::move(m_errors)};
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

        if(std::isspace(static_cast<unsigned char>(c))){  //использование cctype
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


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//основные функции разбивки на токены
//прочитать один токен
std::expected<Token, LexError> Lexer::nextToken(){ 
    SourcePos start = currentPos();
    char c = advance();

    if(static_cast<unsigned char>(c) > 127){ 
        return std::unexpected(makeError("В тексте присутсвует не ASCII символ"));
    }

    switch(c){ 
        case '(': return Token{TokenType::DELIM_LPAREN, "(", start};
        case ')': return Token{TokenType::DELIM_RPAREN, ")", start};
        case '|': return Token{TokenType::OP_PIPE, "|", start}; //ADT
        case '+': return Token{TokenType::OP_PLUS, "+", start};
        case '%': return Token{TokenType::OP_PERCENT, "%", start};
        case '.': return Token{TokenType::OP_DOT, ".", start};
        case ':': return Token{TokenType::OP_COLON, ":", start};
        case '*': return Token{TokenType::OP_STAR, "*", start};
        case ',': return Token{TokenType::DELIM_COMMA, ",", start};
        case '[': return Token{TokenType::DELIM_LBRACKET, "[", start};
        case ']': return Token{TokenType::DELIM_RBRACKET, "]", start};
        case '{': return Token{TokenType::DELIM_LBRACE, "{", start};
        case '}': return Token{TokenType::DELIM_RBRACE, "}", start};
        case '\\': return Token{TokenType::OP_BACKSLASH, "//", start};
        
        case '=':
            if(match('=')) return Token{TokenType::OP_EQ, "==", start};
            return Token{TokenType::OP_ASSIGN, "=", start};
        case '!': 
            if(match('=')) return Token{TokenType::OP_NEQ, "!=", start};
            return std::unexpected(LexError{"unexpected charatcer '!'", start});
        case '-':
            if(match('>'))return Token{TokenType::OP_ARROW, "->", start};
            return Token{TokenType::OP_MINUS, "-", start};
        case '<':
            if(match('=')) return Token{TokenType::OP_LE, "<=", start};
            return Token{TokenType::OP_LT, "<", start};
        case '>':
            if(match('=')) return Token{TokenType::OP_GE, "<=", start};
            return Token{TokenType::OP_GT, ">", start};
        case '/':
            return Token{TokenType::OP_SLASH, "/", start};
        case '"': 
            return scanString(start);

        default:
            break;
    }

    if(std::isdigit(static_cast<unsigned char>(c))){ 
        return scanNumber(start);
    }

    if(std::isalpha(static_cast<int>(c)) || c == '_'){ 
        return scanIdentOrKeyword(start);
    }

    //если ничего не найдено
    return std::unexpected(LexError{std::string("unexpected character '") + c + "'", start});
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Детальное разбиение некоторых "особых" случаев
// Числовой литерал: INT ::= DIGIT+   REAL ::= DIGIT+ '.' DIGIT+ 
//либо REAL ::= DIGIT+ '.' DIGIT+ (('e' | 'E') ('+' | '-')? DIGIT+)? | DIGIT+ ('e' | 'E') ('+' | '-')? DIGIT+
//ВАЖНО ПОМНИТЬ! мы уже взяли символ c, поэтому его нужно будет взять в этом случае
std::expected<Token, LexError> Lexer::scanNumber(SourcePos start){ 
    //std::string(size_t count, char ch); - конструктор string такого плана
    //накапливать лексему будем начиная с 1 цифры
    int startPos = m_pos - 1;

    while(!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))){ 
        advance();
    }

    bool isReal = false;

    // это вещественная часть?
    if(!atEnd() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        isReal = true;
        advance(); //'.'
        while(!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))){ 
            advance();
        }
    }

    if(!atEnd() && (peek() == 'e' || peek() == 'E')){ 
        char next1 = peek(1); char next2 = peek(2);
        bool hasExp = std::isdigit(static_cast<unsigned char>(next1)) || 
        ((next1 == '+' || next1 == '-') && std::isdigit(static_cast<unsigned char>(next2)));

        if(hasExp){ 
            isReal = true;
            advance (); //e | E

            if(!atEnd() && (peek() == '+' || peek() == '-')){ 
                advance();
            }

            while(!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))){ 
                advance();
            }
        } else { 
            return std::unexpected(LexError{"invalid exponent in numerical literal", currentPos()});
        }
    }

    std::string lexeme = m_source.substr(startPos, m_pos - startPos);

    if(isReal){ 
        return Token{TokenType::LIT_REAL, std::move(lexeme), start};
    }
    return Token{TokenType::LIT_INT, std::move(lexeme), start};    
}


// Строковый литерал: STRING ::= '"' (NONQUOTE | ESCAPE)* '"'
//открывающей кавычки уже нет
std::expected<Token, LexError> Lexer::scanString(SourcePos start){ 
    std::string lexeme;
    
    while(true){ 
        if(atEnd() || peek() == '\n'){ 
            return std::unexpected(LexError{"unterminated string literal", start}); //неоконченный строковый литерал
        }

        char c = advance();

        if(c == '"'){ 
            return Token{TokenType::LIT_STRING, std::move(lexeme), start};
        }

        if(c == '\\'){ 
           auto escaped = scanEscapeSequence();
           if(!escaped){
            m_errors.push_back(makeError("invalid escape sequence"));
            continue;
           }

           lexeme += *escaped;
           continue;
        }

        //опционально: в будущем добавим русский язык

        //поэтому пока проверка на не ASCII символ
        if(static_cast<unsigned char>(c) > 127){ 
            return std::unexpected(LexError{ 
                "non ASCII character in string terminal", currentPos()
            });
        }

        lexeme += c;
    }
}

//вспомогательная функция
std::optional<char> Lexer::scanEscapeSequence(){
    if(atEnd()) return std::nullopt;

    char esc = advance();
    switch(esc){
        case 'n': return '\n';
        case 't': return '\t';
        case 'r': return '\r';
        case '"': return '"';
        case '\\': return '\\';
        default: return std::nullopt;
    }
}



//идентификатор либо ключевое слово
std::expected<Token, LexError> Lexer::scanIdentOrKeyword(SourcePos start){ 
    int startPos = m_pos - 1; //первый символ уже взяли

    while(!atEnd()){ 
        char c = peek();
        if(std::isalnum(static_cast<unsigned char>(c)) || c == '_'){ 
            advance();
        } else { 
            break;
        }
    }

    std::string lexeme = m_source.substr(startPos, m_pos - startPos);
    TokenType type = classifyWord(lexeme);
    return Token{type, std::move(lexeme), start};
}


TokenType Lexer::classifyWord(const std::string& word){ 
    //создаем map один раз и сохраняем между вызовами
    static const std::unordered_map<std::string, TokenType> keywords = { 
        {"fn", TokenType::KW_FN},
        {"if", TokenType::KW_IF},
        {"then", TokenType::KW_THEN},
        {"else", TokenType::KW_ELSE},
        {"match", TokenType::KW_MATCH},
        {"type", TokenType::KW_TYPE},
        {"data", TokenType::KW_DATA},
        {"module", TokenType::KW_MODULE},
        {"let", TokenType::KW_LET},
        {"in", TokenType::KW_IN},
        {"mut", TokenType::KW_MUT},
        {"and", TokenType::KW_AND},
        {"or", TokenType::KW_OR},
        {"not", TokenType::KW_NOT},
        {"unit", TokenType::KW_UNIT},
        {"bool", TokenType::KW_BOOL},
        {"string", TokenType::KW_STRING},
        {"yep", TokenType::LIT_YEP},
        {"nope", TokenType::LIT_NOPE},
        {"int8", TokenType::KW_INT8},
        {"int16", TokenType::KW_INT16},
        {"int32", TokenType::KW_INT32},
        {"int64", TokenType::KW_INT64},
        {"uint8", TokenType::KW_UINT8},
        {"uint16", TokenType::KW_UINT16},
        {"uint32", TokenType::KW_UINT32},
        {"uint64", TokenType::KW_UINT64},
        {"float32", TokenType::KW_FLOAT32},
        {"float64", TokenType::KW_FLOAT64},
    };

    auto it = keywords.find(word); //где it указывает на пару в map
    if(it != keywords.end()) return it->second;
    return TokenType::IDENT; 
}

}


