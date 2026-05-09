#pragma once

#include "ast.hpp"

namespace Semantic{ 

using Pos = Lexer::SourcePos;
using namespace Parser;

template<typename T>
using Ptr = std::unique_ptr<T>


struct SemanticError{ 
    std::string message;
    Pos pos;

    //функция форматирования ошибки в вид file:line:column: error: msg - Аналогично парсеру
    //нарушение верного смысла
    std::string format(const std::string& filename) const { 
        return filename + ":" + std::to_string(pos.line) + ":" + 
            std::to_string(pos.column) + ": error: semantic error: " + message; 
    }
};

//внутреннее представление типа в семантике
//смысловой анализ происходящего
struct TypeInfo;

struct BuiltinType{ 
    std::string name;
};

struct SimpleType{ 
    std::string name;
};

struct GenericType{ 
    std::string name;
    std::vector<Ptr<TypeInfo>> args;
};

struct TupleType{ 
    std::vector<Ptr<TypeInfo>> elems;
};

struct ListType{ 
    Ptr<TypeInfo> elem;
};

struct FunctionType{ 
    Ptr<TypeInfo> from;
    Ptr<TypeInfo> to;
};

using TypeInfoVar = std::variant<
    BuiltinType,
    SimpleType,
    GenericType,
    TupleType,
    ListType,
    FunctionType
>;

struct TypeInfo{
    TypeInfoVar var;
    
    //проверка на совместимость типов
    bool equals(const TypeInfo& other) const;
    std::string toString() const; //будет возвращать строку типов
};


//добавим удобные конструкторы для создания TypeInfo

Ptr<TypeInfo> makeBiultin(const std::string& name);
Ptr<TypeInfo> makeSimle(const std::string& name);
Ptr<TypeInfo> makeList(Ptr<TypeInfo> elem);
Ptr<TypeInfo> makeTuple(std::vector<Ptr<TypeInfo>> elems);
Ptr<TypeInfo> makeFunction(Ptr<TypeInfo> from, Ptr<TypeInfo> to);
Ptr<TypeInfo> makeGeneric(const std::string& name, std::vector<Ptr<TypeInfo>> args);