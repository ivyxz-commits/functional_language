#include <iostream>
#include <string> 
#include <fstream> //чтение из файла
#include <sstream> //для чтения файла в строку

#include "lexer.hpp"
#include "parser.hpp"
#include "ast.hpp"

void printTokens(const std::vector<Lexer::Token>& tokens){
    for(const auto& tok : tokens){
        std::cout << tok.pos.line << ":" << tok.pos.column 
        << "\t" << Lexer::TokenTypeName(tok.type) 
        << "\t'" << tok.lexeme << "'\n" ;
    }
}


//вывод типа
void printType(const Parser::TypeNode& node, int space){
    std::string padding(space * 2, ' ');

    if(const auto* t = std::get_if<Parser::BuiltinTypeNode>(&node.var)){ 
        std::cout << padding << "BuiltinType: " << t -> name << "\n";
    } 

    else if(const auto* t = std::get_if<Parser::SimpleTypeNode>(&node.var)){
        std::cout << padding << "SimpleType: " << t -> name << "\n";
    }

    else if(const auto* t = std::get_if<Parser::GenericTypeNode>(&node.var)){
        std::cout << padding << "GenericType: " << t -> name << "\n";
        for(const auto& arg : t -> args) printType(*arg, space + 1);
    }

    else if(const auto* t = std::get_if<Parser::TupleTypeNode>(&node.var)){
        std::cout << padding << "TupleType:\n";
        for(const auto& elem : t -> elems) printType(*elem, space + 1);
    }

    else if(const auto* t = std::get_if<Parser::ListTypeNode>(&node.var)){
        std::cout << padding << "ListType:\n";
        printType(*t -> elemType, space + 1);
    }

    else if(const auto* t = std::get_if<Parser::FunctionTypeNode>(&node.var)){
        std::cout << padding << "FunctionType:\n";
        printType(*t -> from, space + 1);
        printType(*t -> to, space + 1);
    }
}

//вывод шаблона(образца)
void printPattern(const Parser::PatternNode& node, int space){
    std::string padding(space * 2, ' ');

    if(const auto* p = std::get_if<Parser::WildcardPatternNode>(&node.var)){
        std::cout << padding << "Wildcard\n";
    }

    else if(const auto* p = std::get_if<Parser::LiteralPatternNode>(&node.var)){
        std::cout << padding << "LiteralPattern: " << p->value << "\n";
    }

    else if(const auto* p = std::get_if<Parser::NamePatternNode>(&node.var)){
        std::cout << padding << "NamePattern: " << p->name << "\n";
    }

    else if(const auto* p = std::get_if<Parser::TuplePatternNode>(&node.var)){
        std::cout << padding << "TuplePattern:\n";
        for (const auto& elem : p -> elems) printPattern(*elem, space + 1);
    }

    else if(const auto* p = std::get_if<Parser::ListPatternNode>(&node.var)){
        std::cout << padding << "ListPattern:\n";
        for(const auto& elem : p->elems) printPattern(*elem, space + 1); 
    }

    else if(const auto* p = std::get_if<Parser::ConstructorPatternNode>(&node.var)){
        std::cout << padding << "ConstructorPattern: " << p->name << "\n";
        for(const auto& arg : p -> args) printPattern(*arg, space + 1);
    }

    else if(const auto* p = std::get_if<Parser::ConsPatternNode>(&node.var)){
        std::cout << padding << "ConsPattern:\n";
        printPattern(*p->head, space + 1);
        printPattern(*p->tail, space + 1);
    }
}


void printDecl(const Parser::DeclNode& node, int space);

void printExpr(const Parser::ExprNode& node, int space){
    std::string padding(space * 2, ' ');

    if(const auto* e = std::get_if<Parser::LiteralExpr>(&node.var)){
    
        if(const auto* v = std::get_if<long long>(&e->value)){
            std::cout << padding << "IntLit: " << *v << "\n";
        }

        else if(const auto* v = std::get_if<double>(&e->value)){
            std::cout << padding << "RealLit: " << *v << "\n";
        }

        else if(const auto* v = std::get_if<std::string>(&e->value)){
            std::cout << padding << "StringLit: \"" << *v << "\"\n";
        }

        else if(const auto* v = std::get_if<bool>(&e->value)){
            std::cout << padding << "BoolLit: " << (*v ? "yep" : "nope") << "\n";
        }

        else if(std::get_if<std::monostate>(&e->value)){
            std::cout << padding << "UnitLit\n";
        }
    }

    else if(const auto* e = std::get_if<Parser::IdentExpr>(&node.var)){
        std::cout << padding << "Ident: " << e->name << "\n";
    }

    else if(const auto* e = std::get_if<Parser::UnaryExpr>(&node.var)){
        std::string opStr;
    
        switch(e->op){
            case Parser::UnaryOp::Neg: opStr = "-";   break;
            case Parser::UnaryOp::Not: opStr = "not"; break;
        }

        std::cout << padding << "Unary: " << opStr << "\n";
        printExpr(*e->operand, space + 1);
    }

    else if(const auto* e = std::get_if<Parser::BinaryExpr>(&node.var)){
        std::string opStr;

        switch(e->op){
            case Parser::BinaryOp::Add: opStr = "+";   break;
            case Parser::BinaryOp::Sub: opStr = "-";   break;
            case Parser::BinaryOp::Mul: opStr = "*";   break;
            case Parser::BinaryOp::Div: opStr = "/";   break;
            case Parser::BinaryOp::Mod: opStr = "%";   break;
            case Parser::BinaryOp::Eq:  opStr = "==";  break;
            case Parser::BinaryOp::Neq: opStr = "!=";  break;
            case Parser::BinaryOp::Lt:  opStr = "<";   break;
            case Parser::BinaryOp::Le:  opStr = "<=";  break;
            case Parser::BinaryOp::Gt:  opStr = ">";   break;
            case Parser::BinaryOp::Ge:  opStr = ">=";  break;
            case Parser::BinaryOp::And: opStr = "and"; break;
            case Parser::BinaryOp::Or:  opStr = "or";  break;
        }

        std::cout << padding << "Binary: " << opStr << "\n";
        printExpr(*e->left,  space + 1);
        printExpr(*e->right, space + 1);
    }

    else if(const auto* e = std::get_if<Parser::CallExpr>(&node.var)){
        std::cout << padding << "Call:\n";
        printExpr(*e -> callee, space + 1);
        for(const auto& arg : e->args) printExpr(*arg, space + 1);
    }

    else if(const auto* e = std::get_if<Parser::FieldAccessExpr>(&node.var)){
        std::cout << padding << "FieldAccess: ." << e->field << "\n";
        printExpr(*e->object, space + 1);
    }

    else if(const auto* e = std::get_if<Parser::IfExpr>(&node.var)){
        std::cout << padding << "If:\n";
        std::cout << padding << "  cond:\n";
        printExpr(*e -> cond, space + 2);
        std::cout << padding << "  then:\n";
        printExpr(*e -> thenBranch, space + 2);
        std::cout << padding << " else:\n";
        printExpr(*e -> elseBranch, space + 2);
    }

    else if(const auto* e = std::get_if<Parser::MatchExpr>(&node.var)){
        std::cout << padding << "Match:\n";
        printExpr(*e -> target, space + 1);
        for(const auto& arm : e->arms){
            std::cout << padding << "  arm:\n";
            printPattern(*arm.pattern, space + 2);
            printExpr(*arm.body, space + 2);
        }
    }

    else if(const auto* e = std::get_if<Parser::LetInExpr>(&node.var)){
        std::cout << padding << "LetIn:\n";
        for(const auto& b : e->bindings){
            std::cout << padding << "  bindings: " << b.name << "\n";
            printExpr(*b.value, space + 2); 
        }
    }

    else if(const auto* e = std::get_if<Parser::LambdaExpr>(&node.var)){
        std::cout << padding << "Lambda:\n";
        for(const auto& p : e->params){
            std::cout << padding << "  param" << p.name << "\n";
        }
        printExpr(*e -> body, space + 1);
    }

    else if(const auto* e = std::get_if<Parser::TupleExpr>(&node.var)){
        std::cout << padding << "Tuple:\n";
        for(const auto& elem : e -> elems) printExpr(*elem, space + 1);
    }

    else if(const auto* e = std::get_if<Parser::ListExpr>(&node.var)){
        std::cout << padding << "List:\n";
        for(const auto& elem : e -> elems) printExpr(*elem, space + 1);
    }

    else if(const auto* e = std::get_if<Parser::ConstructorExpr>(&node.var)){
        std::cout << padding << "Constructor: " << e->name << "\n";
        for(const auto& arg : e->args) printExpr(*arg, space + 1);
    }
}


//вывод объявлений
void printDecl(const Parser::DeclNode& node, int space){

    std::string padding(space * 2, ' ');

    if(const auto* d = std::get_if<Parser::FuncDecl>(&node.var)){
        std::cout << padding << "FuncDecl: " << d->name << "\n";
        for(const auto& p : d-> params){
            std::cout << padding << "  param: " << p.name << "\n";
            printType(*p.type, space + 2);
        }

        if(d -> returnType){
            std::cout << padding << " returnType:\n";
            printType(**d -> returnType, space + 2); //optional, ptr
        }
        std::cout << padding << "  body:\n";
        printExpr(*d -> body, space + 2);
    }

    else if(const auto* d = std::get_if<Parser::TypeAliasDecl>(&node.var)){
        std::cout << padding << "TypeAlias: " << d->name << "\n";
        printType(*d -> type, space + 1);
    }

    else if(const auto* d = std::get_if<Parser::DataDecl>(&node.var)){
        std::cout << padding << "DataDecl: " << d->name << "\n";
        for(const auto& ctor : d->constructors){
            std::cout << padding << "  constructor: " << ctor.name << "\n";
            for(const auto& f : ctor.fields){
                std::cout << padding << "    field: " << f.name << "\n";
                printType(*f.type, space + 3);
            }
        }
    }

    else if(const auto* d = std::get_if<Parser::ModuleDecl>(&node.var)){
        std::cout << padding << "ModuleDecl: " << d->name << "\n";
        for(const auto& decl : d->decls){
            printDecl(*decl, space + 1);
        }
    }
}

void printAst(const Parser::Program& prog){
    std::cout << "Program:\n";
    for(const auto& decl : prog.decls){
        printDecl(*decl, 1);
    }
}

int main(int argc, char* argv[]){

    if(argc < 2){
        std::cerr <<"usage: lang <file> [--dump-tokens] [--dump-ast]\n"; 
        return 1;
    }

    std::string filename = argv[1];

    std::ifstream file(filename); //переменная поток с именем file, пытается открыть файл

    if(!file){
        std::cerr << "error: cannot open file '" << filename << "'\n"; //печатаем ошибку
        return 1;
    }

    std::ostringstream buf; //создает буфер обертку
    buf << file.rdbuf();

    std::string source = buf.str(); //копируем все из буфера в строку


    //работа с выводом
    bool dumpTokens = false;
    bool dumpAst = false;

    for(int i = 2; i < argc; i++){
        std::string flag = argv[i];

        if(flag == "--dump-tokens") dumpTokens = true;
        if(flag == "--dump-ast") dumpAst = true;
    }


    //лексер
    Lexer::Lexer lexer(source, filename);

    auto lexResult = lexer.tokenize(); //указатель на вектор токенов

    if(dumpTokens){
        printTokens(lexResult.tokens);
    }

    for(const auto& err : lexResult.errors){
        std::cerr << err.format(filename) << "\n";
    }

    if(lexResult.hasErrors()) return 1;
    auto tokens = std::move(lexResult.tokens);

    //теперь парсер
    Parser::Parser parser(tokens, filename);
    auto progResult = parser.parse();

    if(!progResult){
        std::cerr << progResult.error().format(filename) << "\n";
        return 1;
    }

    auto prog = std::move(*progResult);

    if(dumpAst){
        printAst(prog);
        return 0;
    }

    return 0;
}