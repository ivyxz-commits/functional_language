#pragma once

#include "semantic_types.hpp"

namespace Semantic{

//сам семантический анализатор
class Analyzer{ 
public: 
    Analyzer(std::string filename = "<input>");
    std::vector<SemanticError> analyze(const Program& prog); //если вектор пустой, то все прекрасно с семантической точки зрения

    const TypeRegistry& get_registry() const {
        return m_registry;
    }

    const std::unordered_map<const ExprNode*, sPtr<TypeInfo>>& getExprTypes() const {
        return m_exprTypes;  //поле типа для кодогена
    }

    const std::unordered_map<std::string, std::vector<std::string>>& getFuncTypeParams() const{
        return m_funcTypeParams;
    }

    const std::unordered_map<std::string, const FuncDecl*>& getGenericFuncDecls() const {
        return m_genericFuncDecls;
    }

    const std::unordered_map<const CallExpr*, 
        std::unordered_map<std::string, sPtr<TypeInfo>>>& getCallTypeMaps() const {
            return m_callTypeMaps;
        }

    const std::unordered_map<const CallExpr*, const FuncDecl*>& getResolvedOverloads() const {
        return m_resolvedOverloads;
    }

private:
    std::string m_filename;
    TypeRegistry m_registry; //объект реестра типов
    std::unordered_map<const ExprNode*, sPtr<TypeInfo>> m_exprTypes;
    std::unordered_map<std::string, sPtr<Environment>> m_moduleEnvs;
    std::unordered_map<std::string, std::vector<std::string>> m_funcTypeParams;
    std::unordered_map<std::string, const FuncDecl*> m_genericFuncDecls;
    std::unordered_map<const CallExpr*, 

    //хранит typeVarMap для каждого вызова дженерик функции, чтобы не переводить astnode -> typeinfo 
    std::unordered_map<std::string, sPtr<TypeInfo>>> m_callTypeMaps; 
    
    //перегрузка функций
    std::unordered_map<std::string, std::vector<const FuncDecl*>> m_overloads;
    std::unordered_map<const CallExpr*, const FuncDecl*> m_resolvedOverloads;

    //Создание ошибки
    SemanticError makeError(std::string msg, Pos pos) const;



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //разбор объявлений

    using TypeVarMap = const std::unordered_map<std::string, sPtr<TypeInfo>>&; // for Generic

    void analyzeDecl(const DeclNode& decl, sPtr<Environment> env, std::vector<SemanticError>& errors);

    //анализация функции и ее тела
    void analyzeFuncDecl(const FuncDecl& fn, sPtr<Environment> env, std::vector<SemanticError>& errors);
    void checkFuncBody(const FuncDecl& fn, sPtr<Environment> funcEnv, std::vector<SemanticError>& errors);

    //выбрать нужную перегрузку по типам аргументов
    const FuncDecl* resolveOverload(const std::string& name, 
    const std::vector<sPtr<TypeInfo>>& argTypes, 
    std::vector<SemanticError>& errors, const Pos& pos);

    void analyzeModuleDecl(const ModuleDecl& mod, sPtr<Environment> env, std::vector<SemanticError>& errors);

    void analyzeAliasDecl(const TypeAliasDecl& alias, std::vector<SemanticError>& errors);

    //дата типам не нужно окружение, сразу в реестр типов
    void analyzeDataDecl(const DataDecl& data, std::vector<SemanticError>& errors);
    ConstructorInfo buildConstructorInfo(const ConstructorDecl& ctor, const std::string& dataName,
    const TypeVarMap& typeVarMap, std::vector<SemanticError>& errors);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

    //вспомогательные функции обработки образца
    bool analyzeWildcardPattern();

    bool analyzeLiteralPattern(const LiteralPatternNode& p, const sPtr<TypeInfo>& expectedType, 
        std::vector<SemanticError>& errors);

    bool analyzeNamePattern(const NamePatternNode& p, const sPtr<TypeInfo>& expectedType, 
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    bool analyzeTuplePattern(const TuplePatternNode& p, const sPtr<TypeInfo>& expectedType, 
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    bool analyzeListPattern(const ListPatternNode& p, const sPtr<TypeInfo>& expectedType,
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    bool analyzeConsPattern(const ConsPatternNode& p, const sPtr<TypeInfo>& expectedType, 
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    bool analyzeConstructorPattern(const ConstructorPatternNode& p, const sPtr<TypeInfo>& expectedType, 
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    //вспомогательные функции для конструктора
    bool checkConstructorOwnership(const ConstructorPatternNode& p, const ConstructorInfo& ctorInfo,
        const sPtr<TypeInfo>& expectedType, std::vector<SemanticError>& errors);

    bool checkConstructorPatternArgCount(const ConstructorPatternNode& p, const ConstructorInfo& ctorInfo,
        std::vector<SemanticError>& errors);




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //разбор типов (TypeNode -> TypeInfo)
    std::optional<sPtr<TypeInfo>> resolveType(
        const TypeNode& node,
        //таблица подстановки параметров типа (разбираем типы внутри ADT) - на будущее
        const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap,
        std::vector<SemanticError>& errors);

    //вспомогательные функции для каждого типа
    std::optional<sPtr<TypeInfo>> resolveBuiltinType(const BuiltinTypeNode& n);

    std::optional<sPtr<TypeInfo>> resolveSimpleType(const SimpleTypeNode& n, 
        TypeVarMap typeVarMap, 
        std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> resolveGenericType(const GenericTypeNode& n, 
        TypeVarMap typeVarMap, 
        std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> resolveListType(const ListTypeNode& n, 
        TypeVarMap typeVarMap, 
        std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> resolveTupleType(const TupleTypeNode& n, 
        TypeVarMap typeVarMap, 
        std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> resolveFunctionType(const FunctionTypeNode& n, 
        TypeVarMap typeVarMap, 
        std::vector<SemanticError>& errors);




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////        
    //разбор выражений (тип выажения или nullopt)
    std::optional<sPtr<TypeInfo>> analyzeExpr(const ExprNode&, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeIdent(const IdentExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeIf(const IfExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeUnary(const UnaryExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeTuple(const TupleExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeList(const ListExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    std::optional<sPtr<TypeInfo>> analyzeCons(const ConsExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);

    ////////////////////////////////////////
    //Binary
    std::optional<sPtr<TypeInfo>> analyzeBinary(const BinaryExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    //вспомогательные
    std::optional<sPtr<TypeInfo>> checkArithmetic(const BinaryExpr& e, const sPtr<TypeInfo>& left, 
        const sPtr<TypeInfo>& right, std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> checkComparison(const BinaryExpr& e, const sPtr<TypeInfo>& left,
        const sPtr<TypeInfo>& right, std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> checkEquality(const BinaryExpr& e, const sPtr<TypeInfo>& left,
        const sPtr<TypeInfo>& right, std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> checkLogical(const BinaryExpr& e, const sPtr<TypeInfo>& left,
        const sPtr<TypeInfo>& right, std::vector<SemanticError>& errors);

    ////////////////////////////////////////
    //Call
    std::optional<sPtr<TypeInfo>> analyzeCall(const CallExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    //вспомогательные
    std::optional<sPtr<TypeInfo>> analyzeCallPrint(const CallExpr& e, 
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> analyzeCallInput(const std::string& name, const CallExpr& e,
        const std::string& retType, std::vector<SemanticError>& errors);
        
    std::optional<sPtr<TypeInfo>> analyzeCallBuiltin(const IdentExpr& ident, const CallExpr& e, 
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    //вспомогательные typeof, min, max, len
    std::optional<sPtr<TypeInfo>> analyzeBuiltinTypeof(const CallExpr& e, sPtr<Environment> env, 
        std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> analyzeBuiltinMaxMin(const std::string& name, const CallExpr& e,
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> analyzeBuiltinLength(const CallExpr& e, sPtr<Environment> env, 
        std::vector<SemanticError>& errors);


    std::optional<sPtr<TypeInfo>> analyzeCallArgs(const CallExpr& e, sPtr<TypeInfo> calleeType,
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    /*
    *вспомогательные для generic
    */

    std::optional<std::unordered_map<std::string, sPtr<TypeInfo>>>
    buildCallTypeVarMap(const std::string& funcName, const std::vector<Ptr<TypeNode>>& typeArgs,
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    sPtr<TypeInfo> changeTypeVars(const TypeInfo& type,
        const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap);

    void checkGenericFuncBody(const FuncDecl& fn,
        const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap, sPtr<Environment> env,
        std::vector<SemanticError>& errors);

    /*
    *вспомогательные для A.3.4 generic - унификация
    */

    //сопоставляет типы рекурсивно
    void unify(sPtr<TypeInfo> param, sPtr<TypeInfo> arg,
        std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap,
        std::vector<SemanticError>& errors, const Pos& pos);

    //вывод всех параметров
    std::optional<std::unordered_map<std::string, sPtr<TypeInfo>>> 
        inferenceTypeArgs(const std::string& funcName, const CallExpr& e, 
        sPtr<Environment> env, std::vector<SemanticError>& errors);

    //для конструктора, для функций логика другая
    std::optional<std::unordered_map<std::string, sPtr<TypeInfo>>> inferenceConstructorTypeArgs(
        const ConstructorExpr& e, const std::optional<DataTypeInfo>& dataInfo, 
        const std::optional<ConstructorInfo>& ctorInfo, sPtr<Environment> env, 
        std::vector<SemanticError>& errors);

    /*
    *две сокращающие для дженериков и перегрузки
    */

    //дженерик вызовы - какие типы вместо Т
    bool analyzeCallGeneric(const CallExpr& e, std::optional<sPtr<TypeInfo>>& calleeType,
    sPtr<Environment> env, std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> analyzeCallOverload(const IdentExpr& ident,
        const CallExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);

    ///////////////////////////////////////
    //LetIn
    std::optional<sPtr<TypeInfo>> analyzeLetIn(const LetInExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    //вспомогательные
    bool checkLetAnnotation(const LetBinding& binding, const sPtr<TypeInfo>& valueType, 
        std::vector<SemanticError>& errors);

    bool defineLetBinding(const LetBinding& binding, const sPtr<TypeInfo>& valueType,
        sPtr<Environment> letEnv, std::vector<SemanticError>& errors);    


    ///////////////////////////////////////
    //Match
    std::optional<sPtr<TypeInfo>> analyzeMatch(const MatchExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    //вспомогательные
    void checkMatchArm(const MatchArm& arm, std::optional<sPtr<TypeInfo>>& resultType, 
        const sPtr<TypeInfo>& bodyType, std::vector<SemanticError>& errors);

    ///////////////////////////////////////
    //FieldAccess
    std::optional<sPtr<TypeInfo>> analyzeFieldAccess(const FieldAccessExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    //вспомогательные
    std::optional<sPtr<TypeInfo>> accessModuleField(const IdentExpr& ident, 
        const FieldAccessExpr& e, std::vector<SemanticError>& errors);

    std::optional<sPtr<TypeInfo>> accessDataField(const sPtr<TypeInfo>& objType,
        const FieldAccessExpr& e, std::vector<SemanticError>& errors);

    
    ///////////////////////////////////////
    //Constructor
    std::optional<sPtr<TypeInfo>> analyzeConstructor(const ConstructorExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    //вспомогательные
    void checkConstructorArgs(const ConstructorExpr& e, const ConstructorInfo& ctorInfo, sPtr<Environment> env, 
        std::vector<SemanticError>& errors);

    bool checkConstructorArgCount(const ConstructorExpr& e, const ConstructorInfo& ctorInfo,
        std::vector<SemanticError>& errors);

    ///////////////////////////////////////
    //Lambda
    std::optional<sPtr<TypeInfo>> analyzeLambda(const LambdaExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors);
    //вспомогательная
    std::optional<std::vector<sPtr<TypeInfo>>> resolveLambdaParams(const LambdaExpr& e, sPtr<Environment> lambdaEnv, 
        std::vector<SemanticError>& errors);



    //Общие вспомогательные функции
    //функции проверки типов
    bool typesCompatible(const TypeInfo& a, const TypeInfo& b) const; //совместимость
    bool isNumericType(const TypeInfo& t) const;
    bool isBoolType(const TypeInfo& t) const;
    bool isNumericWidening(const TypeInfo& from, const TypeInfo& to) const; //приведение типов

    //функция для перевода оператора в строку для ошибки
    static std::string binaryOpToString(BinaryOp op);

    sPtr<Environment> makeBuiltinEnv(); //создаем начальное окружение с 4 функциями (print, input, exit, panic)
    
    //регистрация всех объявлений верхнего уровня
    void firstPass(const std::vector<Ptr<DeclNode>>& decls, sPtr<Environment> env, std::vector<SemanticError>& errors);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////    
    //вспомогательные функции - первоначальный обход
    void firstPassAlias(const TypeAliasDecl& alias, std::vector<SemanticError>& errors);
    void firstPassData(const DataDecl& data, std::vector<SemanticError>& errors);
    void firstPassModule(const ModuleDecl& mod, sPtr<Environment> env, std::vector<SemanticError>& errors);
   
    void firstPassFunc(const FuncDecl& fn, sPtr<Environment> env, std::vector<SemanticError>& errors);
    //для нее отдельные вспомогательные функции
    sPtr<TypeInfo> buildFuncType(const FuncDecl& fn,
        const TypeVarMap& typeVarMap, std::vector<SemanticError>& errors);

    bool checkOverloadDuplicate(const FuncDecl& fn,
        const TypeVarMap& typeVarMap, std::vector<SemanticError>& errors);

    std::string buildMangledName(const FuncDecl& fn,
        const TypeVarMap& typeVarMap, std::vector<SemanticError>& errors);

    /* //если идти по порядку при анализации smth, bar еще не будет зарегестрирован и получим ошибку
    /поэтому сначала регистрируем все обозначения и типы до проверки тел
    *fn smth -> int64 = bar()
    *fn bar -> int64 = number
    */
};

}