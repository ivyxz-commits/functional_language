#include "parser.hpp"

namespace Parser{ 

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
//вспомогательные функции

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