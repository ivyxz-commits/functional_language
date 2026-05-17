#pragma once

#include "ast.hpp"
#include <unordered_map>

namespace Semantic{ 

using Pos = Lexer::SourcePos;
using namespace Parser;

template<typename T>
using sPtr = std::shared_ptr<T>;


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

// index() возвращние номера активного типа
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
sPtr<TypeInfo> makeSimle(const std::string& name);
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


//таблица символов - что стоит за каждым именем
struct Symbol{ 
    std::string name;
    sPtr<TypeInfo> type;
    bool isMutable = false; //true - на случай добавления mut
    Pos declPos;
};




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


//сам семантический анализатор
class Analyzer{ 
public: 
    Analyzer(std::string filename = "<input>");
    std::vector<SemanticError> analyze(const Program& prog); //если вектор пустой, то все прекрасно с семантической точки зрения

private:
    std::string m_filename;
    TypeRegistry m_registry; //объект реестра типов
    std::unordered_map<std::string, sPtr<Environment>> m_moduleEnvs;

    //Создание ошибки
    SemanticError makeError(std::string msg, Pos pos) const;

    //разбор объявлений
    void analyzeDecl(const DeclNode& decl, sPtr<Environment> env, std::vector<SemanticError>& errors);
    void analyzeFuncDecl(const FuncDecl& fn, sPtr<Environment> env, std::vector<SemanticError>& errors);
    void analyzeModuleDecl(const ModuleDecl& mod, sPtr<Environment> env, std::vector<SemanticError>& errors);
    //дата типам не нужно окружение, сразу в реестр типов
    void analyzeDataDecl(const DataDecl& data, std::vector<SemanticError>& errors);
    void analyzeAliasDecl(const TypeAliasDecl& alias, std::vector<SemanticError>&errors);

    //разбор образцов(шаблонов) //образец хороший если он структурно совместим с типом сопоставляемого значения
    //ConsPattern x : xs — хороший если expectedType это список, а иначе зачем нам внутрь заходить и проверять match и получать доп. ошибки
    bool analyzePattern(
        const PatternNode& pattern, //constructorPatternNode
        sPtr<TypeInfo> expectedType, //SimpleType("Option[int64]")
        sPtr<Environment> env,
        std::vector<SemanticError>& errors);

    //data Option[a] = None | Some(a)
    /* fn smth(x: Option[int64]) -> int64 =
        match x {
            None -> 0,
            Some(v) -> v + 1
    } */

    //разбор типов (TypeNode -> TypeInfo)
    std::optional<sPtr<TypeInfo>> resolveType(
        const TypeNode& node,
        //таблица подстановки параметров типа (разбираем типы внутри ADT)
        const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap,
        std::vector<SemanticError>& errors);


    //разбор выражений (тип выажения или nullopt)
    std::optional<sPtr<TypeInfo>> analyzeExpr(const ExprNode&, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeLetIn(const LetInExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeIf(const IfExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeMatch(const MatchExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeLambda(const LambdaExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeBinary(const BinaryExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeUnary(const UnaryExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeFieldAccess(const FieldAccessExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeCall(const CallExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeConstructor(const ConstructorExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeTuple(const TupleExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeList(const ListExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);

    //вспомогательные функции
    //функции проверки типов
    bool typesCompatible(const TypeInfo& a, const TypeInfo& b) const; //совместимость
    bool isNumericType(const TypeInfo& t) const;
    bool isBoolType(const TypeInfo& t) const;

    //функция для перевода оператора в строку для ошибки
    static std::string binaryOpToString(BinaryOp op);

    sPtr<Environment> makeBuiltinEnv(); //создаем начальное окружение с 4 функциями (print, input, exit, panic)
    
    //регистрация всех объявлений верхнего уровня
    void firstPass(const std::vector<Ptr<DeclNode>>& decls, sPtr<Environment> env, std::vector<SemanticError>& errors);

    /* //если идти по порядку при анализации smth, bar еще не будет зарегестрирован и получим ошибку
    /поэтому сначала регистрируем все обозначения и типы до проверки тел
    *fn smth -> int64 = bar()
    *fn bar -> int64 = number
    */
};

}