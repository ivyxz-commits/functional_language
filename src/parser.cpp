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
//выражения

//expr ::= letExpr | ifExpr | matchExpr | lambdaExpr | logicalOr - состоит из цепочки последовательных включений других expr
std::expected<Ptr<ExprNode>, ParseError> Parser::parseExpr(){ 
    using TT = Lexer::TokenType;

    //4 (целые) независимые конструкции, они не могут встретиться в одном месте одновременно
    if(check(TT::KW_LET)) return parseLetInExpr();
    if(check(TT::KW_IF)) return parseIfExpr();
    if(check(TT::KW_MATCH)) return parseMatchExpr();
    if(check(TT::OP_BACKSLASH)) return parseLambdaExpr();

    return parseLogicalOr();
}

//letExpr ::= 'let' bindingList 'in' expr
//bindingList ::= binding (',' binding)*
//binding ::= IDENT typeAnnotation? '=' expr
//создать локальное имя внутри функции, не объявляя его
std::expected<Ptr<ExprNode>, ParseError> Parser::parseLetInExpr(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();
    auto kw = expect(TT::KW_LET);
    if(!kw) return std::unexpected(kw.error());

    std::vector<LetBinding> bindings;

    auto parseBinding = [&]() -> std::expected<LetBinding, ParseError> { 
    
        auto bpos = currentPos();
        auto nameTok = expect(TT::IDENT);
        if(!nameTok) return std::unexpected(nameTok.error());

        auto typeAnnot = parseOptionalType();
        if(!typeAnnot) return std::unexpected(typeAnnot.error());

        auto eq = expect(TT::OP_ASSIGN);
        if(!eq) return std::unexpected(eq.error());

        auto value = parseExpr();
        if(!value) return std::unexpected(value.error());

        return LetBinding{std::move(nameTok -> lexeme), std::move(*typeAnnot), std::move(*value), bpos};
    };

    auto first = parseBinding();
    if(!first) return std::unexpected(first.error());
    bindings.push_back(std::move(*first));

    while(match(TT::DELIM_COMMA)){ 
        auto b = parseBinding();
        if(!b) return std::unexpected(b.error());
        bindings.push_back(std::move(*b));
    }

    auto inKw = expect(TT::KW_IN);
    if(!inKw) return std::unexpected(inKw.error());

    auto body = parseExpr();
    if(!body) return std::unexpected(body.error());

    LetInExpr le{std::move(bindings), std::move(*body), pos};
    return std::make_unique<ExprNode>(ExprNode{std::move(le), pos});
}

//ifExpr ::= 'if' expr 'then' expr 'else' expr
std::expected<Ptr<ExprNode>, ParseError> Parser::parseIfExpr(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto kw = expect(TT::KW_IF);
    if(!kw) return std::unexpected(kw.error());

    auto cond = parseExpr();
    if(!cond) return std::unexpected(cond.error());

    auto thenKw = expect(TT::KW_THEN);
    if(!thenKw) return std::unexpected(thenKw.error());

    auto thenBr = parseExpr();
    if(!thenBr) return std::unexpected(thenBr.error());

    auto elseKw = expect(TT::KW_ELSE);
    if(!elseKw) return std::unexpected(elseKw.error());

    auto elseBr = parseExpr();
    if(!elseBr) return std::unexpected(elseBr.error());

    IfExpr ie{std::move(*cond), std::move(*thenBr), std::move(*elseBr), pos};
    return std::make_unique<ExprNode>(ExprNode{std::move(ie), pos});   
}

//matchExpr ::= 'match' expr '{' matchExprArm+ '}'
//matchExprArm ::= pattern '->' expr ','?
std::expected<Ptr<ExprNode>, ParseError> Parser::parseMatchExpr(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();
    auto kw = expect(TT::KW_MATCH);
    if(!kw) return std::unexpected(kw.error());

    auto target = parseExpr(); //что изучаем
    if(!target) return std::unexpected(target.error());

    auto lb = expect(TT::DELIM_LBRACE);
    if(!lb) return std::unexpected(lb.error());

    std::vector<MatchArm> arms;
    while(!check(TT::DELIM_RBRACE) && !atEnd()){ 
        auto armPos = currentPos();
        auto pattern = parsePattern();
        if(!pattern) return std::unexpected(pattern.error());

        auto arrow = expect(TT::OP_ARROW);
        if(!arrow) return std::unexpected(arrow.error());

        auto body = parseExpr();
        if(!body) return std::unexpected(body.error());

        arms.push_back(MatchArm{std::move(*pattern), std::move(*body), armPos});
        match(TT::DELIM_COMMA); //запятая необязательна
    }

    if(arms.empty()){ 
        return std::unexpected(makeError("Match expression должен иметь хотя бы одну ветвь"));
    }

    auto rb = expect(TT::DELIM_RBRACE);
    if(!rb) return std::unexpected(rb.error());

    MatchExpr me{std::move(*target), std::move(arms), pos};
    std::make_unique<ExprNode>(ExprNode{std::move(me), pos});
}

//lambdaExpr ::= '\' IDENT+ '->' expr
std::expected<Ptr<ExprNode>, ParseError> Parser::parseLambdaExpr(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto bs = expect(TT::OP_BACKSLASH);
    if(!bs) return std::unexpected(bs.error());

    std::vector<std::string> params;
    auto first = expect(TT::IDENT);
    if(!first) return std::unexpected(first.error());
    params.push_back(first -> lexeme);
    
    while(check(TT::IDENT)){ 
        params.push_back(advance().lexeme);
    }

    auto arrow = expect(TT::OP_ARROW);
    if(!arrow) return std::unexpected(arrow.error());

    auto body = parseExpr();
    if(!bs) return std::unexpected(bs.error());

    LambdaExpr le{std::move(params), std::move(*body), pos};
    return std::make_unique<ExprNode>(ExprNode{std::move(le), pos});
}

//последняя конструкция в которую рекурсивно входят другие (от наименьшего к наибольшему приоритету)
//logicalOr ::= logicalAnd ('or' logicalAnd)*
std::expected<Ptr<ExprNode>, ParseError> Parser::parseLogicalOr(){ 
    using TT = Lexer::TokenType;
    auto lhs = parseLogicalAnd();
    if(!lhs) return std::unexpected(lhs.error());

    while(check(TT::KW_OR)){ 
        auto pos = currentPos(); //позиция or для BinaryExpr
        advance();
        auto rhs = parseLogicalAnd();
        if(!rhs) return std::unexpected(rhs.error());
        BinaryExpr bin{"or", std::move(*lhs), std::move(*rhs), pos};
        *lhs = std::make_unique<ExprNode>(ExprNode{std::move(bin), pos}); //(a or b) or c
    }
    return std::move(*lhs);
}

//logicalAnd ::= equality ('and' equality)*
std::expected<Ptr<ExprNode>, ParseError> Parser::parseLogicalAnd(){ 
    using TT = Lexer::TokenType;
    auto lhs = parseEquality();
    if(!lhs) return std::unexpected(lhs.error());

    while(check(TT::KW_AND)){ 
        auto pos = currentPos();
        advance();
        auto rhs = parseEquality();
        if(!rhs) return std::unexpected(rhs.error());
        BinaryExpr bin{"and", std::move(*lhs), std::move(*rhs), pos};
        *lhs = std::make_unique<ExprNode>(ExprNode{std::move(bin), pos});
    }
    return std::move(*lhs);
}

//equality ::= comparison (('==' | '!=') comparison)* - 0 <= 5 == 1
std::expected<Ptr<ExprNode>, ParseError> Parser::parseEquality(){ 
    using TT = Lexer::TokenType;
    auto lhs = parseComparison();
    if(!lhs) return std::unexpected(lhs.error());

    while(check(TT::OP_EQ) || check(TT::OP_NEQ)){ 
        auto pos = currentPos();
        std::string op = (current().type == TT::OP_EQ) ? "==" : "!=";
        advance();
        auto rhs = parseComparison();
        if(!rhs) return std::unexpected(rhs.error());
        BinaryExpr bin{op, std::move(*lhs), std::move(*rhs), pos};
        *lhs = std::make_unique<ExprNode>(ExprNode{std::move(bin), pos});
    }
    return std::move(*lhs);
}

//comparison ::= additive(('<') | ('>') | '<=' | '>=') additive)*
std::expected<Ptr<ExprNode>, ParseError> Parser::parseComparison(){ 
    using TT = Lexer::TokenType;
    auto lhs = parseAdditive();
    if(!lhs) return std::unexpected(lhs.error());

    while(check(TT::OP_LT) || check(TT::OP_LE) || check(TT::OP_GT) || check(TT::OP_GE)){ 
        auto pos = currentPos();
        std::string op;
        switch (current().type){ 
            case TT::OP_LT : op = "<"; break;
            case TT::OP_LE : op = "<="; break;
            case TT::OP_GT : op = ">="; break;
            case TT::OP_GE : op = ">"; break;
            default: break;
        }
        advance();
        auto rhs = parseAdditive();
        if(!rhs) return std::unexpected(rhs.error());
        BinaryExpr bin{op, std::move(*lhs), std::move(*rhs), pos};
        *lhs = std::make_unique<ExprNode>(ExprNode{std::move(bin), pos});
    }
    return std::move(*lhs);
}

//additive ::= multiplicative(('+' | '-') multiplicative)*
std::expected<Ptr<ExprNode>, ParseError> Parser::parseAdditive(){ 
    using TT = Lexer::TokenType;
    auto lhs = parseMultiplicative();
    if(!lhs) return std::unexpected(lhs.error());

    while(check(TT::OP_PLUS) || check(TT::OP_MINUS)){ 
        auto pos = currentPos();
        std::string op = (current().type == TT::OP_PLUS) ? "+" : "-";
        advance();
        auto rhs = parseMultiplicative();
        if(!rhs) return std::unexpected(rhs.error());
        BinaryExpr bin{op, std::move(*lhs), std::move(*rhs), pos};
        *lhs = std::make_unique<ExprNode>(ExprNode{std::move(bin), pos});
    }
    return std::move(*lhs);
}

//additive ::= unary (('*' | '/' | '%') unary)*
std::expected<Ptr<ExprNode>, ParseError> Parser::parseMultiplicative(){ 
    using TT = Lexer::TokenType;
    auto lhs = parseUnary();
    if(!lhs) return std::unexpected(lhs.error());

    while(check(TT::OP_STAR) || check(TT::OP_SLASH) || check(TT::OP_PERCENT)){ 
        auto pos = currentPos();
        std::string op;
        switch(current().type){ 
            case TT::OP_STAR : op = "*"; break;
            case TT::OP_SLASH: op = "/"; break;
            case TT::OP_PERCENT: op = "%"; break;
            default: break;
        }
        advance();
        auto rhs = parseUnary();
        if(!rhs) return std::unexpected(rhs.error());
        BinaryExpr bin{op, std::move(*lhs), std::move(*rhs), pos};
        *lhs = std::make_unique<ExprNode>(ExprNode{std::move(bin), pos});
    }
    return std::move(*lhs);
}

//unary ::=('-' | 'not') unary | postfix
std::expected<Ptr<ExprNode>, ParseError> Parser::parseUnary(){ 
    using TT = Lexer::TokenType;
    if(check(TT::OP_MINUS)){ 
        auto pos = currentPos();
        advance();
        auto operand = parseUnary();
        if(!operand) return std::unexpected(operand.error());
        UnaryExpr ue{"-", std::move(*operand), pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(ue), pos});
    } else if (check(TT::KW_NOT)){ 
        auto pos = currentPos();
        advance();
        auto operand = parseUnary();
        if(!operand) return std::unexpected(operand.error());
        UnaryExpr ue{"not", std::move(*operand), pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(ue), pos});
        
    }
    return parsePostfix();
}


//postfix ::primary postfixOp* // postfixOp ::= callOp | fieldOp
//callOp ::= '(' argumentList? ')' //fieldOp ::= '.' IDENT
std::expected<Ptr<ExprNode>, ParseError> Parser::parsePostfix(){ 
    using TT = Lexer::TokenType;
    auto expr = parsePrimary();
    if(!expr) return std::unexpected(expr.error());

    while(true){

        if(check(TT::DELIM_LPAREN)){
            auto pos = currentPos();
            advance();
            auto args = parseArgList();
            if(!args) return std::unexpected(args.error());
            auto rp  = expect(TT::DELIM_LPAREN);
            if(!rp) return std::unexpected(rp.error());

            CallExpr ce{std::move(*expr), std::move(*args), pos};
            *expr = std::make_unique<ExprNode>(ExprNode{std::move(ce), pos});
        } else if (check(TT::OP_DOT)){
            auto pos = currentPos();
            advance();
            auto field = expect(TT::IDENT);
            if(!field) return std::unexpected(field.error());

            FieldAccessExpr fae{std::move(*expr), std::move(field -> lexeme), pos};
            *expr = std::make_unique<ExprNode>(ExprNode{std::move(fae), pos});
        } else { 
            break;
        }
    }
    return std::move(*expr);
}

//primary ::= literal | IDENT | groupedExpr | tupleExpr | ListExpr | constructorExpr
std::expected<Ptr<ExprNode>, ParseError> Parser::parsePrimary(){
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    if(check(TT::LIT_INT)){ 
        std::string lex = advance().lexeme;
        long long value = 0;
        for(char ch : lex) value = value * 10 + (ch - '0');
        IntLitExpr ile{std::move(value), pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(ile), pos});
    }

    if(check(TT::LIT_REAL)){ 
        std::string lex = advance().lexeme;
        double value = std::strtod(lex.c_str(), nullptr);
        RealLitExpr rle{std::move(value), pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(rle), pos});
    }

    if(check(TT::LIT_STRING)){ 
        std::string value = advance().lexeme;
        StringLitExpr sle{std::move(value), pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(sle), pos});
    }

    if(match(TT::LIT_YEP)){
        BoolLitExpr ble{true, pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(ble), pos});
    }

    if(match(TT::LIT_NOPE)){
        BoolLitExpr ble{false, pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(ble), pos});
    }

    if(match(TT::KW_UNIT)){ 
        UnitLitExpr ule{pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(ule), pos});
    }

    //список '[' (expr (',' expr)*)? ']'
    if(match(TT::DELIM_LBRACKET)){ 
        std::vector<Ptr<ExprNode>> elems;
        if(!check(TT::DELIM_LBRACKET)){ 
            auto el = parseExpr();
            if(!el) return std::unexpected(el.error());
            elems.push_back(std::move(*el));

            while(match(TT::DELIM_COMMA)){ 
                auto el = parseExpr();
                if(!el) return std::unexpected(el.error());
                elems.push_back(std::move(*el));
            }
        }
        auto rb = expect(TT::DELIM_RBRACKET);
        if(!rb) return std::unexpected(rb.error());
        ListExpr le{std::move(elems), pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(le), pos});
    } 

    //кортеж или сгрупированное выражение '(' expr (',' expr)* ')'
    if(match(TT::DELIM_LPAREN)){ 
        auto first = parseExpr();
        if(!first) return std::unexpected(first.error());
        
        if(match(TT::DELIM_COMMA)){ 
            std::vector<Ptr<ExprNode>> elems;
            elems.push_back(std::move(*first));

            auto second = parseExpr();
            if(!second) return std::unexpected(second.error());
            elems.push_back(std::move(*second));

            while(match(TT::DELIM_COMMA)){ 
                auto el = parseExpr();
                if(!el) return std::unexpected(el.error());
                elems.push_back(std::move(*el));
            }

            auto rp = expect(TT::DELIM_LPAREN);
            if(!rp) return std::unexpected(rp.error());

            TupleExpr te{std::move(elems), pos};
            return std::make_unique<ExprNode>(ExprNode{std::move(te), pos});
        }

        auto rp = expect(TT::DELIM_LPAREN);
        if(!rp) return std::unexpected(rp.error());
        return std::move(*first); //(a + b) * z
    }

    //Идентификатор, конструктор или вызов встроенной функции
    if(check(TT::IDENT)){ 
        std::string name = advance().lexeme;

        bool isConstructor = !name.empty() && std::isupper(static_cast<unsigned char>(name[0]));
        if(isConstructor){ 
            std::vector<Ptr<ExprNode>> args;
            if(match(TT::DELIM_LPAREN)){ 
                auto arg = parseExpr();
                if(!arg) return std::unexpected(arg.error());
                args.push_back(std::move(*arg));

                while(match(TT::DELIM_COMMA)){ 
                    auto arg = parseExpr();
                    if(!arg) return std::unexpected(arg.error());
                    args.push_back(std::move(*arg));
                }
                auto rp = expect(TT::DELIM_LPAREN);
                if(!rp) return std::unexpected(rp.error());
            }
            ConstructorExpr ce{std::move(name), std::move(args), pos};
            return std::make_unique<ExprNode>(ExprNode{std::move(ce), pos});
        }

        IdentExpr ie{std::move(name), pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(ie), pos});
    }

    return std::unexpected(makeError("expected expression, got '" + current().lexeme + "'"));
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

        auto rb = expect(TT::DELIM_LBRACE);
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

    FieldDecl fd{std::move(nameTok -> lexeme), std::move(*t), pos};
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

/*
*Для выражений
*/

std::expected<std::vector<Ptr<ExprNode>>, ParseError> Parser::parseArgList(){ //работает в связке с parsePostfix()
    using TT = Lexer::TokenType;
    std::vector<Ptr<ExprNode>> args;

    if(!check(TT::DELIM_RPAREN)){ 
        auto arg = parseExpr();
        if(!arg) return std::unexpected(arg.error());
        args.push_back(std::move(*arg));

        while(match(TT::DELIM_COMMA)){ 
            auto arg = parseExpr();
            if(!arg) return std::unexpected(arg.error());
            args.push_back(std::move(*arg));
        }
    }

    return args;
}




std::expected<std::optional<Ptr<TypeNode>>, ParseError> Parser::parseOptionalType(){ //для let
    using TT = Lexer::TokenType;

    if(!match(TT::OP_COLON)){ 
        return std::optional<Ptr<TypeNode>>{std::nullopt};
    }

    auto t = parseType();
    if(!t) return std::unexpected(t.error());

    return std::optional<Ptr<TypeNode>>{std::move(*t)};
}

}