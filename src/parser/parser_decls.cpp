#include "parser.hpp"

namespace Parser{

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
//вспомогательные функции

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

    std::vector<FieldDecl> fields;
    bool isNamed = false;

    if(match(TT::DELIM_LBRACE)){ 
        isNamed = true;

        auto f = parseFieldDecl();
        if(!f) return std::unexpected(f.error());
        fields.push_back(std::move(*f));

        while(match(TT::DELIM_COMMA)){ 
            auto f = parseFieldDecl();
            if(!f) return std::unexpected(f.error());
            fields.push_back(std::move(*f));
        }

        auto rb = expect(TT::DELIM_RBRACE);
        if(!rb) return std::unexpected(rb.error());
    } else if (match(TT::DELIM_LPAREN)){
        auto ft = parseType();
        if(!ft) return std::unexpected(ft.error());
        fields.push_back(FieldDecl{"", std::move(*ft), pos});

        while(match(TT::DELIM_COMMA)){ 
            auto ft  = parseType();
            if(!ft) return std::unexpected(ft.error());
            fields.push_back(FieldDecl{"", std::move(*ft), pos});
        }
        auto rp = expect(TT::DELIM_RPAREN);
        if(!rp) return std::unexpected(rp.error());
    }

        return ConstructorDecl{std::move(nameTok -> lexeme), std::move(fields), isNamed, pos};
}

//Добавляю именованные поля в конструктор () и {}
std::expected<FieldDecl, ParseError> Parser::parseFieldDecl(){
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());

    auto colon = expect(TT::OP_COLON);
    if(!colon) return std::unexpected(colon.error());

    auto t = parseType();
    if(!t) return std::unexpected(t.error());

    return FieldDecl{std::move(nameTok -> lexeme), std::move(*t), pos};
}


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

}