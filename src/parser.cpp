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

//functionDecl ::= 'fn' IDENT '(' parameterList ')' returnType? '=' expr 
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
        nameTok -> lexeme,  //ну либо (*nameTok).lexeme - достали Token из expected 
        std::move(params),
        std::move(*body),
        std::move(returnType),
        fnPos
    };
}

//typeAliasDecl ::= 'type' IDENT '=' type
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

    return TypeAliasDecl{nameTok -> lexeme, std::move(*t), pos};
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//вспомогательные функции 

//функция работы с параметрами функции
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

    return FuncParam{nameTok -> lexeme, std::move(*t), pos};
}


}