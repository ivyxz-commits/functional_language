#pragma once

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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//объявления
//declaration ::= functionDecl | typeAliasDecl | dataDecl | moduleDecl 
std::expected<Ptr<DeclNode>, ParseError> Parser::parseDecl(){ 
    using TT = Lexer::TokenType;
    Pos pos = currentPos();

    if(check(TT::KW_FN)){ 
        auto r = parseFuncDecl();
        if(!r) return std::unexpected(r.error());
        return std::make_unique<DeclNode>(DeclNode{std::move(*r), pos}); //cоздает указатлеь в куче и вовзращает Ptr<DeclNode>
    }

    if(check(TT::KW_TYPE)){ 
        auto r = parseTypeAliasDecl();
        if(!r) return std::unexpected(r.error());
        return std::make_unique<DeclNode>(DeclNode{std::move(*r), pos});
    }

    if(check(TT::KW_DATA)){ 
        auto r = parseDataDecl();
        if(!r) return std::unexpected(r.error());
        return std::make_unique<DeclNode>(DeclNode{std::move(*r), pos});
    }

    if(check(TT::KW_MODULE)){ 
        auto r = parseModuleDecl();
        if(!r) return std::unexpected(r.error());
        return std::make_unique<DeclNode>(DeclNode{std::move(*r), pos});
    }

    //если ошибка объявления
    return std::unexpected(makeError(
        "expected declaration (fn / type / data / module), got '" + 
        current().lexeme + "'"));
}

/*
*functionDecl ::= 'fn' IDENT '(' parameterList ')' returnType? '=' expr 
*/
std::expected<FuncDecl, ParseError> Parser::parseFuncDecl(){ 
    using TT = Lexer::TokenType;
    auto fnPos = currentPos();
    auto fnTok = expect(TT::KW_FN); // auto вместо std::expected<Lexer::Token, ParseError>
    if(!fnTok) return std::unexpected(fnTok.error());

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());
    
    auto lp = expect(TT::DELIM_LPAREN);
    if(!lp) return std::unexpected(lp.error());

    //проверка на параметры
    std::vector<FuncParam> params;
    if(!check(TT::DELIM_RPAREN)){ 
        auto p = parseFuncParam();
        if(!p) return std::unexpected(p.error());
        params.push_back(std::move(*p));
    
        while(match(TT::DELIM_COMMA)){ 
            auto p = parseFuncParam();
            if(!p) return std::unexpected(p.error());
            params.push_back(std::move(*p));
        }
    }

    auto rp = expect(TT::DELIM_RPAREN);
    if(!rp) return std::unexpected(rp.error());

    //опционально возвращаемый тип
    std::optional<Ptr<TypeNode>> returnType; //nullopt
    if(match(TT::OP_ARROW)){ 
        auto rt = parseType();
        if(!rt) return std::unexpected(rt.error());
        returnType = std::move(*rt);
    }
  
    auto eq = expect(TT::OP_ASSIGN);
    if(!eq) return std::unexpected(eq.error());
    
    auto body = parseExpr();
    if(!body) return std::unexpected(body.error());
  
    return FuncDecl{ 
        std::move(nameTok -> lexeme),  //ну либо (*nameTok).lexeme - достали Token из expected 
        std::move(params),
        std::move(*body),
        std::move(returnType),
        fnPos
    };
}

/*
*typeAliasDecl ::= 'type' IDENT '=' type
*/
std::expected<TypeAliasDecl, ParseError> Parser::parseTypeAliasDecl(){ 
    using TT = Lexer::TokenType;

    auto pos = currentPos();
    auto kw = expect(TT::KW_TYPE);
    if(!kw) return std::unexpected(kw.error());

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());

    auto eq = expect(TT::OP_ASSIGN);
    if(!eq) return std::unexpected(eq.error());

    auto t = parseType(); //тут auto вместо std::expected<Ptr<TypeNode>, ParseError> 
    if(!eq) return std::unexpected(t.error());

    return TypeAliasDecl{std::move(nameTok -> lexeme), std::move(*t), pos};
}

/*
*moduleDecl ::= 'module' IDENT '{' declaration* '}'
*/

std::expected<ModuleDecl, ParseError> Parser::parseModuleDecl(){ 
    using TT = Lexer::TokenType;

    auto pos = currentPos();
    auto kw = expect(TT::KW_MODULE);
    if(!kw) return std::unexpected(kw.error());

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());

    auto lb = expect(TT::DELIM_LBRACE);
    if(!lb) return std::unexpected(lb.error());

    std::vector<Ptr<DeclNode>> decls;
    while(!check(TT::DELIM_RBRACE) && !atEnd()){ 
        auto d = parseDecl();
        if(!d) return std::unexpected(d.error());
        decls.push_back(std::move(*d)); //аналогично parse()
    }

    auto rb = expect(TT::DELIM_RBRACE);
    if(!rb) return std::unexpected(rb.error());

    return ModuleDecl{std::move(nameTok -> lexeme), std::move(decls), pos};
}

/*
*dataDecl ::= 'data' IDENT typeParams? '=' consturtorDecl ('|' constructorDecl)*
*typeParams ::= '[' IDENT (',' IDENT)* ']'
*/

std::expected<DataDecl, ParseError> Parser::parseDataDecl(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();
    auto kw = expect(TT::KW_DATA);
    if(!kw) return std::unexpected(kw.error());

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());

    //параметры типа: [a, b, ...]
    auto typeParams = parseTypeParams();
    if(!typeParams) return std::unexpected(typeParams.error());

    auto eq = expect (TT::OP_ASSIGN);
    if(!eq) return std::unexpected(eq.error());

    //data name[typeParams1, typeParams2] = | None | First(type1) | Second(type2)
    std::vector<ConstructorDecl> constructors;

    auto first = parseConstructorDecl();
    if(!first) return std::unexpected(first.error());
    constructors.push_back(std::move(*first));

    while(match(TT::OP_PIPE)){ 
        auto c = parseConstructorDecl();
        if(!c) return std::unexpected(c.error());
        constructors.push_back(std::move(*c));
    }

    return DataDecl{std::move(nameTok -> lexeme), std::move(*typeParams), std::move(constructors), pos};
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//типы 
//type ::= atomicType ('->' type)?
//правоассоциативность - т.к a -> (b -> c), функция принимает 'a' и возвращает '(b -> c)'
std::expected<Ptr<TypeNode>, ParseError> Parser::parseType(){ 
    using TT = Lexer::TokenType;

    auto pos = currentPos();
    auto lhs = parseAtomicType(); //atomic уже возвращает указатель
    if(!lhs) return std::unexpected(lhs.error());

    if(match(TT::OP_ARROW)){ 
        auto rhs = parseType();
        if(!rhs) return std::unexpected(rhs.error());

        FunctionTypeNode fn{std::move(*lhs), std::move(*rhs), pos};
        return std::make_unique<TypeNode>(TypeNode{std::move(fn), pos});
    }
    return std::move(*lhs); //возвращаем значение вызывающему коду
}

//без FunctionTypeNode atomicType ::= builtinType | simpleType | genericType | tupleType | listType | '(' type ')' - сгруп тип (приоритет)
std::expected<Ptr<TypeNode>, ParseError> Parser::parseAtomicType(){
    using TT = Lexer::TokenType;

    if(check(TT::DELIM_LBRACKET)) return parseListType();
    if(check(TT::DELIM_LPAREN)) return parseTupleOrGroupedType();
    if(check(TT::IDENT)) return parseUserType();
    return parseBuiltinType();

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//шаблоны (образцы) - patterns
//Обработка cons 
std::expected<Ptr<PatternNode>, ParseError> Parser::parsePattern(){
    using TT = Lexer::TokenType;
    auto pos = currentPos();
    
    auto lhs = parsePrimaryPattern();
    if(!lhs) return std::unexpected(lhs.error());

    if(match(TT::OP_COLON)){ 
        auto rhs = parsePattern();
        if(!rhs) return std::unexpected(rhs.error());
        ConsPatternNode cn{std::move(*lhs), std::move(*rhs), pos};
        return std::make_unique<PatternNode>(PatternNode{std::move(cn), pos});
    }
}

std::expected<Ptr<PatternNode>, ParseError> Parser::parsePrimaryPattern(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    //WildCard
    if(check(TT::IDENT) && current().lexeme == "_"){ 
        advance();
        return std::make_unique<PatternNode>(PatternNode{WildcardPatternNode{pos}, pos});
    }

    //булевые литералы
    if(match(TT::LIT_YEP)){ 
        LiteralPatternNode lp{"yep", LiteralPatternNode::Kind::Bool, pos};
        std::make_unique<PatternNode>(PatternNode{std::move(lp), pos});
    }
    if(match(TT::LIT_NOPE)){ 
        LiteralPatternNode lp{"nope", LiteralPatternNode::Kind::Bool, pos};
        std::make_unique<PatternNode>(PatternNode{std::move(lp), pos});
    }

    //целый литерал
    if(check(TT::LIT_INT)){ 
        std::string value = advance().lexeme;
        LiteralPatternNode lp{std::move(value), LiteralPatternNode::Kind::Int, pos};
        std::make_unique<PatternNode>(PatternNode{std::move(lp), pos});
    }

    if(check(TT::LIT_REAL)){ 
        std::string value = advance().lexeme;
        LiteralPatternNode lp{std::move(value), LiteralPatternNode::Kind::Real, pos};
        std::make_unique<PatternNode>(PatternNode{std::move(lp), pos});
    }

    //строковый
    if(check(TT::LIT_STRING)){ 
        std::string value = advance().lexeme;
        LiteralPatternNode lp{std::move(value), LiteralPatternNode::Kind::String, pos};
        std::make_unique<PatternNode>(PatternNode{std::move(lp), pos});
    }

    //отрицательный числовой литерал - INT or REAL
    if(check(TT::OP_MINUS) && (peek(1).type == TT::LIT_INT || peek(1).type == TT::LIT_REAL)){ 
        advance();
        
        if(check(TT::LIT_INT)){ 
            std::string value = "-" + advance().lexeme;
            LiteralPatternNode lp{std::move(value), LiteralPatternNode::Kind::Int, pos};
            return std::make_unique<PatternNode>(PatternNode{std::move(lp), pos});
        } else { 
            std::string value = "-" + advance().lexeme;
            LiteralPatternNode lp{std::move(value), LiteralPatternNode::Kind::Int, pos};
            return std::make_unique<PatternNode>(PatternNode{std::move(lp), pos});
        }
    }

    if(check(TT::DELIM_LBRACKET)) return parseListPattern();
    if(check(TT::DELIM_LPAREN)) return parseTupleOrGroupedPattern();
    if(check(TT::IDENT)) return parseIdentPattern();

    return std::unexpected(makeError( "expected pattern, got '" + current().lexeme + "'" ));
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//вспомогательные функции 

/*
*функция работы с параметрами функции
*/
//IDENT ':' type
std::expected<FuncParam, ParseError> Parser::parseFuncParam(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());

    auto colon = expect(TT::OP_COLON);
    if(!colon) return std::unexpected(colon.error());

    auto t = parseType();
    if(!t) return std::unexpected(t.error());

    return FuncParam{std::move(nameTok -> lexeme), std::move(*t), pos};
}

/*
*работa с ADT
*/

//функция разбора параметров обобщенного типа
std::expected<std::vector<std::string>, ParseError> Parser::parseTypeParams(){ 
    using TT = Lexer::TokenType;

    std::vector<std::string> typeParams;

    if(!match(TT::DELIM_LBRACKET)){
        return typeParams; 
    }

    auto tp = expect(TT::IDENT);
    if(!tp) return std::unexpected(tp.error());
    typeParams.push_back(tp -> lexeme);

    while(match(TT::DELIM_COMMA)){ 
        auto tp = expect(TT::IDENT);
        if(!tp) return std::unexpected(tp.error());
        typeParams.push_back(tp -> lexeme);
    }

    auto rb = expect(TT::DELIM_RBRACKET); 
    if(!rb) return std::unexpected(rb.error());

    return typeParams;
}

//Разбираем один конструктор ADT
std::expected<ConstructorDecl, ParseError> Parser::parseConstructorDecl(){ 
    using TT = Lexer::TokenType;

    auto pos = currentPos();
    auto nameTok = expect(TT::IDENT);
    if (!nameTok) return std::unexpected(nameTok.error());

    std::vector<Ptr<TypeNode>> fields;
        if(match(TT::DELIM_LPAREN)){ 
            auto ft = parseType();
            if(!ft) return std::unexpected(ft.error());
            fields.push_back(std::move(*ft)); //unique_ptr

            while(match(TT::DELIM_COMMA)){ 
                auto ft  = parseType();
                if(!ft) return std::unexpected(ft.error());
                fields.push_back(std::move(*ft));
            }
            auto rp = expect(TT::DELIM_RPAREN);
            if(!rp) return std::unexpected(rp.error());
        }
        return ConstructorDecl{std::move(nameTok -> lexeme), std::move(fields), pos};
}


/*
*для работы с типами
*/

std::expected<Ptr<TypeNode>, ParseError> Parser::parseBuiltinType(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto builtinKw = [&]() -> std::optional<std::string>{
        switch (current().type) {
            case TT::KW_INT8: return "int8";
            case TT::KW_INT16: return "int16";
            case TT::KW_INT32: return "int32";
            case TT::KW_INT64: return "int64";
            case TT::KW_UINT8: return "uint8";
            case TT::KW_UINT16: return "uint16";
            case TT::KW_UINT32: return "uint32";
            case TT::KW_UINT64: return "uint64";
            case TT::KW_FLOAT32: return "float32";
            case TT::KW_FLOAT64: return "float64";
            case TT::KW_BOOL: return "bool";
            case TT::KW_STRING: return "string";
            case TT::KW_UNIT: return "unit";
            default: return std::nullopt;
        }
    }(); //чтобы builtinKw сразу стал типом
    /* fn greet(name: string) -> unit =
        print(name) - пример на unit*/

    if(!builtinKw){ 
        return std::unexpected(makeError("expected type, got '" + current().lexeme + "'"));
    }

    advance();
    BuiltinTypeNode bt{std::move(*builtinKw), pos};
    return std::make_unique<TypeNode>(TypeNode{std::move(bt), pos});
}

std::expected<Ptr<TypeNode>, ParseError> Parser::parseListType(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    //тип список '[' type ']'/
    auto lb = expect(TT::DELIM_LBRACKET);
    if (!lb) return std::unexpected(lb.error());

    auto inner = parseType();
    if (!inner) return std::unexpected(inner.error());

    auto rb = expect(TT::DELIM_RBRACKET);
    if (!rb) return std::unexpected(rb.error());

    ListTypeNode lt{std::move(*inner), pos};
    return std::make_unique<TypeNode>(TypeNode{std::move(lt), pos});
}

//кортеж или сгрупированный тип
std::expected<Ptr<TypeNode>, ParseError> Parser::parseTupleOrGroupedType(){ 
  
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto lp = expect(TT::DELIM_LPAREN);
    if(!lp) return std::unexpected(lp.error());
    
    auto first = parseType();
    if(!first) return std::unexpected(first.error());

    //кортеж
    if(match(TT::DELIM_COMMA)){
        std::vector<Ptr<TypeNode>> elems;
        elems.push_back(std::move(*first));

        auto second = parseType();
        if(!second) return std::unexpected(second.error());
        elems.push_back(std::move(*second));

        while(match(TT::DELIM_COMMA)){ 
            auto el = parseType();
            if(!el) return std::unexpected(el.error());
            elems.push_back(std::move(*el));
        }

        auto rp = expect(TT::DELIM_RPAREN);
        if(!rp) return std::unexpected(rp.error());

        TupleTypeNode tt{std::move(elems), pos};
        return std::make_unique<TypeNode>(TypeNode{std::move(tt), pos});
    }

    //сгрупированный тип //fn bar(f: (int64 -> bool) -> string) = ...
    auto rp = expect(TT::DELIM_RPAREN);
    if(!rp) return std::unexpected(rp.error());
    return std::move(*first);
}

//пользовательский тип: IDENT or IDENT '[' type, ...']' - generic type
std::expected<Ptr<TypeNode>, ParseError> Parser::parseUserType(){ 

    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) std::unexpected(nameTok.error());
    
    //generic
    if(match(TT::DELIM_LBRACKET)){ 
        std::vector<Ptr<TypeNode>> args;
        auto el = parseType();
        if(!el) return std::unexpected(el.error());
        args.push_back(std::move(*el));

        while(match(TT::DELIM_COMMA)){ 
            auto el = parseType();
            if(!el) return std::unexpected(el.error());
            args.push_back(std::move(*el));
        }

        auto rb = expect(TT::DELIM_RBRACKET);
        if(!rb) return std::unexpected(rb.error());

        GenericTypeNode gt{std::move(nameTok -> lexeme), std::move(args), pos};
        return std::make_unique<TypeNode>(TypeNode{std::move(gt), pos});
    }

    SimpleTypeNode st{std::move(nameTok -> lexeme), pos};
    return std::make_unique<TypeNode>(TypeNode{std::move(st), pos});
}


/* 
*для работы с шаблонами
*/

//список '[' (pattern (',' pattern)*)? ']' - допустима возможность пустого списка
/* match xs {
    [] -> "пустой",
    x : rest -> "непустой"
  } */
std::expected<Ptr<PatternNode>, ParseError> Parser::parseListPattern(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto lb = expect(TT::DELIM_LBRACKET);
    if(!lb) return std::unexpected(lb.error());
        
    std::vector<Ptr<PatternNode>> elems;
    if(!check(TT::DELIM_RBRACKET)){ 
        auto p = parsePattern();
        if(!p) return std::unexpected(p.error());
        elems.push_back(std::move(*p));

        while(match(TT::DELIM_COMMA)){ 
            auto p = parsePattern();
            if(!p) return std::unexpected(p.error());
            elems.push_back(std::move(*p));
        }
    }

    auto rb = expect(TT::DELIM_RBRACKET);
    if(!rb) return std::unexpected(rb.error());

    ListPatternNode lp{std::move(elems), pos};
    return std::make_unique<PatternNode>(PatternNode{std::move(lp), pos});
}

//кортеж или сгрупированный тип
std::expected<Ptr<PatternNode>, ParseError> Parser::parseTupleOrGroupedPattern(){
    using TT = Lexer::TokenType;
    auto pos = currentPos();
    
    auto lp = expect(TT::DELIM_LPAREN);
    if(!lp) return std::unexpected(lp.error());

    auto first = parsePattern();
    if(!first) return std::unexpected(first.error());

    //кортеж
    if(match(TT::DELIM_COMMA)){
        std::vector<Ptr<PatternNode>> elems;
        elems.push_back(std::move(*first));
        auto second = parsePattern();
        if(!second) return std::unexpected(second.error());

        while(match(TT::DELIM_COMMA)){ 
            auto p = parsePattern();
            if(!p) return std::unexpected(p.error());
            elems.push_back(std::move(*p));
        }

        auto rp = expect(TT::DELIM_RPAREN);
        if(!rp) return std::unexpected(rp.error());

        TuplePatternNode tp{std::move(elems), pos};
        return std::make_unique<PatternNode>(PatternNode{std::move(tp), pos});
    }
    //сгрупированный тип
    auto rp = expect(TT::DELIM_RPAREN);
    if(!rp) return std::unexpected(rp.error());
    return std::move(*first); //он уже является Ptr<TypeNode>
}

//конструктор или переменная, которая связывается со значениями
std::expected<Ptr<PatternNode>, ParseError> Parser::parseIdentPattern(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();
    
    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());
    std::string name = nameTok -> lexeme;

    bool isCtor = !name.empty() && std::isupper(static_cast<unsigned char>(name[0]));
    if(isCtor){ 
        std::vector <Ptr<PatternNode>> args;
        if(match(TT::DELIM_LPAREN)){ 
            auto p = parsePattern();
            if(!p) return std::unexpected(p.error());
            args.push_back(std::move(*p));

            while(match(TT::DELIM_COMMA)){ 
                auto p = parsePattern();
                if(!p) return std::unexpected(p.error());
                args.push_back(std::move(*p));
            }
            auto rp = expect(TT::DELIM_RPAREN);
            if(!rp) return std::unexpected(rp.error());
        }
        ConstructorPatternNode cp{std::move(name), std::move(args), pos};
        return std::make_unique<PatternNode>(PatternNode{std::move(cp), pos});
    }
    NamePatternNode np{std::move(name), pos};
    return std::make_unique<PatternNode>(PatternNode{std::move(np), pos});
}

}