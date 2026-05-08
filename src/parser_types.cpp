#include "parser.hpp"

namespace Parser{ 

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
//вспомогательные функции

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





}