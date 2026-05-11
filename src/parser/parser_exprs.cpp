
#include "parser.hpp" 

namespace Parser{
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

    auto first = parseLetBinding();
    if(!first) return std::unexpected(first.error());
    bindings.push_back(std::move(*first));

    while(match(TT::DELIM_COMMA)){ 
        auto b = parseLetBinding();
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
        auto arm = parseMatchArm();
        if(!arm) return std::unexpected(arm.error());
        arms.push_back(std::move(*arm));
    }

    if(arms.empty()){ 
        return std::unexpected(makeError("Match expression must have at least one branch"));
    }

    auto rb = expect(TT::DELIM_RBRACE);
    if(!rb) return std::unexpected(rb.error());

    MatchExpr me{std::move(*target), std::move(arms), pos};
    return std::make_unique<ExprNode>(ExprNode{std::move(me), pos});
}

//lambdaExpr ::= '\' IDENT+ '->' expr
std::expected<Ptr<ExprNode>, ParseError> Parser::parseLambdaExpr(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto bs = expect(TT::OP_BACKSLASH);
    if(!bs) return std::unexpected(bs.error());

    std::vector<LambdaParam> params;
    auto first = parseLambdaParam();
    if(!first) return std::unexpected(first.error());
    params.push_back(std::move(*first));
    
    while(check(TT::IDENT)){ 
        auto param = parseLambdaParam();
        if(!param) return std::unexpected(param.error());
        params.push_back(std::move(*param));
    }

    auto arrow = expect(TT::OP_ARROW);
    if(!arrow) return std::unexpected(arrow.error());

    auto body = parseExpr();
    if(!body) return std::unexpected(body.error());

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
            case TT::OP_GT : op = ">"; break;
            case TT::OP_GE : op = ">="; break;
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
            auto rp  = expect(TT::DELIM_RPAREN);
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

    if(check(TT::DELIM_LBRACKET)) return parseListExpr();
    if(check(TT::DELIM_LPAREN)) return parseTupleOrGroupedExpr();
    if(check(TT::IDENT)) return parseIdentOrConstructorExpr();

    return std::unexpected(makeError("expected expression, got '" + current().lexeme + "'"));
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//вспомогательные функции 

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

std::expected<LetBinding, ParseError> Parser::parseLetBinding(){ //имена и их типы
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());

    auto typeAnnot = parseOptionalType();
    if(!typeAnnot) return std::unexpected(typeAnnot.error());

    auto eq = expect(TT::OP_ASSIGN);
    if(!eq) return std::unexpected(eq.error());

    auto value = parseExpr();
    if(!value) return std::unexpected(value.error());

    return LetBinding{std::move(nameTok -> lexeme), std::move(*typeAnnot), std::move(*value), pos};
}

std::expected<MatchArm, ParseError> Parser::parseMatchArm(){ //matchExprArm ::= pattern '->' expr ','?
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto pattern = parsePattern();
    if(!pattern) return std::unexpected(pattern.error());

    auto arrow = expect(TT::OP_ARROW);
    if(!arrow) return std::unexpected(arrow.error());

    auto body = parseExpr();
    if(!body) return std::unexpected(body.error());

    match(TT::DELIM_COMMA); //запятая необязательна

    return MatchArm{std::move(*pattern), std::move(*body), pos};
}

std::expected<LambdaParam, ParseError> Parser::parseLambdaParam(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());

    std::optional<Ptr<TypeNode>> type;
    if(match(TT::OP_COLON)){ 
        auto t = parseType();
        if(!t) return std::unexpected(t.error());
        type = std::move(*t);
    }

    return LambdaParam{std::move(nameTok -> lexeme), std::move(type), pos};
}

std::expected<Ptr<ExprNode>, ParseError> Parser::parseListExpr(){ //список '[' (expr (',' expr)*)? ']'
    
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto lb = expect(TT::DELIM_LBRACKET);
    if(!lb) return std::unexpected(lb.error());

    std::vector<Ptr<ExprNode>> elems;
    
    if(!check(TT::DELIM_RBRACKET)){ 
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

std::expected<Ptr<ExprNode>, ParseError> Parser::parseTupleOrGroupedExpr(){ //кортеж или сгрупированное выражение '(' expr (',' expr)* ')'
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto lp = expect(TT::DELIM_LPAREN);
    if(!lp) return std::unexpected(lp.error());

    auto first = parseExpr();
    if(!first) return std::unexpected(first.error());
    
    if(match(TT::DELIM_COMMA)){ 
        //кортеж
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

        auto rp = expect(TT::DELIM_RPAREN);
        if(!rp) return std::unexpected(rp.error());

        TupleExpr te{std::move(elems), pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(te), pos});
    }

    auto rp = expect(TT::DELIM_RPAREN);
    if(!rp) return std::unexpected(rp.error());
    return std::move(*first); //(a + b) * z
}

std::expected<Ptr<ExprNode>, ParseError> Parser::parseIdentOrConstructorExpr(){ 
    using TT = Lexer::TokenType;
    auto pos = currentPos();

    auto nameTok = expect(TT::IDENT);
    if(!nameTok) return std::unexpected(nameTok.error());
    std::string name = nameTok->lexeme;

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
            auto rp = expect(TT::DELIM_RPAREN);
            if(!rp) return std::unexpected(rp.error());
        }
        ConstructorExpr ce{std::move(name), std::move(args), pos};
        return std::make_unique<ExprNode>(ExprNode{std::move(ce), pos});
    }

    IdentExpr ie{std::move(name), pos};
    return std::make_unique<ExprNode>(ExprNode{std::move(ie), pos});
}

}