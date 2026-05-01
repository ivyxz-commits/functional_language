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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//узлы типов
struct TypeNode;

//int8, bool, string, unit
struct BuiltinTypeNode{ 
    std::string name;
    Pos pos;
};

//user's type: IDENT
struct SimpleTypeNode{ 
    std::string name;
    Pos pos;
};

struct TupleTypeNode{ 
    std::vector<Ptr<TypeNode>> elems;
    Pos pos;
};

struct ListTypeNode{ 
    Ptr<TypeNode> elemType;
    Pos pos; 
};

//что функция принимает и что возвращает
struct FunctionTypeNode{ 
    Ptr<TypeNode> from;
    Ptr<TypeNode> to;
    Pos pos;
};   

//IDENT [type]
struct GenericTypeNode{ 
    std::string name; //IDENT
    std::vector<Ptr<TypeNode>> args;
    Pos pos;
};

using TypeNodeVar = std::variant<
    BuiltinTypeNode,
    SimpleTypeNode,
    TupleTypeNode,
    ListTypeNode,
    FunctionTypeNode,
    GenericTypeNode
>;

struct Typenode{ 
    TypeNodeVar var;
    Pos pos;
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//узлы образцов
struct PatternNode;

//как обработка всех других случаев (default case)
struct WildcardPatternNode{ 
    Pos pos;
};


using PatternNodeVar = std::variant<>;

struct PatternNode{ 
    PatternNodeVar var;
    Pos pos;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Узлы выражений
struct ExprNode;

//Примитивные выражения
struct IntLitExpr{ 
    long long value;
    Pos pos;
};

struct RealLitExpr{ 
    double value;
    Pos pos;
};

struct StringLitExpr{ 
    std::string value;
    Pos pos;
};

struct BoolLitExpr{ 
    bool value;
    Pos pos;
};

struct UnitLitExpr{Pos pos;};

struct IdentExpr{ 
    std::string name; 
    Pos pos;
};


struct UnaryExpr{ 
    std::string op; //"-", "not"
    Ptr<ExprNode> operand;
    Pos pos;
};

struct BinaryExpr{ 
    std::string op; 
    Ptr<ExprNode> left;
    Ptr<ExprNode> right;
    Pos pos;
};

//ExprNode так как obj.method()
struct CallExpr{ 
    Ptr<ExprNode> callee;
    std::vector<Ptr<ExprNode>> args;
    Pos pos;
};

/* пример:
    method(-5, not flag, x + 1)

    *callee = IdentExpr("method")
    *args = [
    UnaryExpr("-", IntLitExpr(5))
    UnaryExpr("not", IdentExpr("flag"))
    BinaryExpr("+", IdentExpr("x"), IntLitExpr(1))
    *]
*/
//point.x //object = IdentExpr 
struct FieldAccessExpr{ 
    Ptr<ExprNode> object;
    std::string field;
    Pos pos;
};

struct IfExpr{ 
    Ptr<ExprNode> cond;
    Ptr<ExprNode> thenBranch;
    Ptr<ExprNode> elseBranch;
    Pos pos;
}; 

//Сравнение патернов
//pattern -> expr
struct MatchArm{ 
    Ptr<PatternNode> pattern;
    Ptr<ExprNode> body;
    Pos pos;
};

//match expr {arm1, arm2, ...}
struct MatchExpr{ 
    Ptr<ExprNode> target; //то, что изучаем
    std::vector<MatchArm> arms;
    Pos pos;
};


//let x = 5 или let x: int64 = 5;
struct LetBinding{ 
    std::string name;
    std::optional<Ptr<TypeNode>> type;
    Ptr<ExprNode> value;
    Pos pos;
};

//let x = 5, y = 6 + 1 in ... // let x = 5 in x + 1
struct LetInExpr{ 
    std::vector<LetBinding> bindings; 
    Ptr<ExprNode> body;
    Pos pos;
};

//пример применения этих двух выражений
/* let result = match x {
  Some(v) -> v,
  None    -> 0
} in result + 1 */


struct LambdaExpr{ 
    std::vector<std::string> params;
    Ptr<ExprNode> body;
    Pos pos;
};

//Tuple(1, Tuple(2, 3), f(x) + 5)
struct TupleExpr{ 
    std::vector<Ptr<ExprNode>> elems;
    Pos pos;
};

struct ListExpr{ 
    std::vector<Ptr<ExprNode>> elems;
    Pos pos; 
};


struct ConstructorExpr{ 
    std::string name;
    std::vector<Ptr<ExprNode>> args;
    Pos pos;
};
/* data Shape = Circle(float64) | Rect(float64, float64) - декларация

/\ отдельные блоки конструктора как выражения
fn area(s: Shape) -> float64 =
  match s {
    Circle(r) -> r * r,
    Rect(w, h) -> w * h
  } */

using ExprNodeVar = std::variant<
    IntLitExpr,
    RealLitExpr, 
    StringLitExpr,
    BoolLitExpr,
    UnitLitExpr,
    IdentExpr,
    UnaryExpr,
    BinaryExpr,
    CallExpr,
    FieldAccessExpr,
    IfExpr,
    MatchExpr,
    LetInExpr,
    LambdaExpr,
    TupleExpr,
    ListExpr,
    ConstructorExpr
>;

struct ExprNode{ 
    ExprNodeVar var;
    Pos pos;
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//самый высокий узел - объявления

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
    DeclNodeVar var;
    Pos pos;
};

//основная программа
struct Program{ 
    std::vector<Ptr<DeclNode>> decls; 
};

}