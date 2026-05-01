#pragma once 

#include <memory>
#include <vector>
#include <optional>
#include <variant>
#include "tokens.hpp"


namespace Parser{ 

//в связи с тем, что кода как и логики будет много, предлагаю сразу завести псевдонимы
using Pos = Lexer::SourcePos;

template<typename T>
using Ptr = std::unique_ptr<T>;
//это намного упрощает запись на Ptr<Typenode>, т.к дерево рекурсивного спуска


//Основные типы узлов


//узлы типов
struct TypeNode;


//узлы образцов
struct PatternNode;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Узлы выражений
struct ExprNode;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//самые высокий узел - объявления

struct DeclNode; 

//просто один параметр функции
struct FucnParam{ 
    std::string name;
    Ptr<TypeNode> type; //принадлежит только этому параметру, не нужно хранить в нескольких местах
    Pos pos;
};

//fn name(params) -> returnType = expr
struct FuncDecl{ 
    std::string name;
    std::vector<FucnParam> params;
    Ptr<ExprNode> body;
    std::optional<Ptr<TypeNode>> returnType;
    Pos pos;
};


//конструктор ADT
struct ConstructorDecl{ 
    std::string name;
    std::vector<Ptr<TypeNode>> fields; //типы полей конструктора
    Pos pos;
};

//data name[typeParams1, typeParams2] = | None | First(type1) | Second(type2)
struct DataDecl{ 
    std::string name;
    std::vector<std::string> typeParams;
    std::vector<ConstructorDecl> constructors;
    Pos pos;
};

//module name{ other decls }
struct ModuleDecl{ 
    std::string name;
    std::vector<Ptr<DeclNode>> decls;
    Pos pos;
};

//псведонимы type Alias = type
struct TypeAliasDecl{ 
    std::string name; //IDENT
    Ptr<TypeNode> type;
    Pos pos;
}; 

//еще псевдоним для лучшего хранения вариантов объявления
using DeclNodeVar = std::variant<
    FuncDecl,
    DataDecl,
    TypeAliasDecl,
    ModuleDecl
>;

struct DeclNode{ 
    DeclNodeVar Var;
    Pos pos;
};

struct Program{ 
    std::vector<Ptr<DeclNode>> decls; 
};

}