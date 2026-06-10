#pragma once


#include "semantic_errors.hpp"

#include <memory>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant> 

namespace Semantic{

//псевдоним для удобства
template<typename T>
using sPtr = std::shared_ptr<T>;

//внутреннее представление типа в семантике
struct TypeInfo;

struct BuiltinType{ 
    std::string name;
};

//data Color - пользовательский тип или в alias
struct SimpleType{ 
    std::string name;
};

struct GenericType{ 
    std::string name;
    std::vector<sPtr<TypeInfo>> args;
};

struct TupleType{ 
    std::vector<sPtr<TypeInfo>> elems;
};

struct ListType{ 
    sPtr<TypeInfo> elem;
};

struct FunctionType{ 
    sPtr<TypeInfo> from;
    sPtr<TypeInfo> to;
};

using TypeInfoVar = std::variant<
    BuiltinType,
    SimpleType,
    GenericType,
    TupleType,
    ListType,
    FunctionType
>;

//представление типа во время семантического анализа (хранит смысл) | (аналог TypeNode, но только для семантики)
struct TypeInfo{
    TypeInfoVar var;
    
    //проверка на совместимость типов
    bool equals(const TypeInfo& other) const;
    std::string toString() const; //будет возвращать строку типов
};

//добавим удобные конструкторы для создания TypeInfo
sPtr<TypeInfo> makeBuiltin(const std::string& name);
sPtr<TypeInfo> makeSimple(const std::string& name);
sPtr<TypeInfo> makeList(sPtr<TypeInfo> elem);
sPtr<TypeInfo> makeTuple(std::vector<sPtr<TypeInfo>> elems);
sPtr<TypeInfo> makeFunction(sPtr<TypeInfo> from, sPtr<TypeInfo> to);
sPtr<TypeInfo> makeGeneric(const std::string& name, std::vector<sPtr<TypeInfo>> args);

//информация о конструкторе
//data Shape = Circle{ radius: float64 } | Rect{ width: float64, height: float64 }
struct ConstructorInfo{
    std::string name; //Circle
    std::string dataName; //Shape = dataName
    std::vector<sPtr<TypeInfo>> fieldTypes; //BuiltinType("float64");
    bool isNamed; //() or {} //true
    std::vector<std::string> fieldNames; //для именованных полей //radius
};

//data Result[a, e] = Ok(a) | Error(e)
struct DataTypeInfo{
    std::string name; //Result
    std::vector<std::string> typeParams; 
    std::vector<ConstructorInfo> constructors;
};


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Таблица символов - то, что стоит за каждым именем

struct Symbol{ 
    std::string name;
    sPtr<TypeInfo> type;
    bool isMutable = false; //true - на случай добавления mut
    Pos declPos;
};


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//окружение (область видимости)
//будет хранить таблицу символов и указатель на родительское окружение
class Environment{ 
public:
    Environment(sPtr<Environment> parent = nullptr);

    bool define(const std::string& name, Symbol sym);
    std::optional<Symbol> lookup(const std::string& name) const; //для вложенностей
    std::optional<Symbol> lookupLocal(const std::string& name) const; //для проверки повторного объявления

private:
    std::unordered_map<std::string, Symbol> m_symbols; //словарь, как ключ значение
    sPtr<Environment> m_parent; //каждая область видимости на родительскую область, которая тоже Environment
};




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//справочник типов(ADT, Alliases) - хранилище - получение информации о типах данных
class TypeRegistry{
public:
    //зарегестрировать ADT
    bool registerData(DataTypeInfo info);
    bool registerAlias(const std::string& name, sPtr<TypeInfo> type);

    //найти ADT по имени
    std::optional<DataTypeInfo> lookupData(const std::string& name) const;
    std::optional<ConstructorInfo> lookupConstructor (const std::string& name) const;
    std::optional<sPtr<TypeInfo>> lookupAlias(const std::string& name) const;

    sPtr<TypeInfo> resolveAlias(sPtr<TypeInfo> type) const;
    /*на случай: //FullName -> Name -> string
    *type Name = string
    *type FullName = Name
    */
private:
    std::unordered_map<std::string, DataTypeInfo> m_dataTypes;
    std::unordered_map<std::string, ConstructorInfo> m_constructors;
    std::unordered_map<std::string, sPtr<TypeInfo>> m_aliases;
};





}