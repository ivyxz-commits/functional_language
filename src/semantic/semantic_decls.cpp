#include "semantic.hpp"
#include "semantic_utils.cpp"

namespace Semantic{

//семантический анализатор Analyzer

SemanticError Analyzer::makeError(std::string msg, Pos pos) const{
    return SemanticError{std::move(msg), pos};
}

//вспомогательные функции
//функции проверки типов
bool Analyzer::typesCompatible(const TypeInfo& a, const TypeInfo& b) const{
    if(std::get_if<SimpleType>(&a.var)) return true; //T совместим с любым типом
    if(std::get_if<SimpleType>(&b.var)) return true; //int64 совместим с T
    if(a.equals(b)) return true;
    if(isNumericWidening(a, b) || isNumericWidening(b, a)) return true;
    return false;
}

//помогут в analyzeBinary и analyzeIf
bool Analyzer::isNumericType(const TypeInfo& t) const {
    if(auto* bt = std::get_if<BuiltinType>(&t.var)){ 
        const std::string& name = bt->name;
        return name == "int8" || name == "int16" || name == "int32" || name == "int64" ||
        name == "uint8" || name == "uint16" || name == "uint32" || name == "uint64" || 
        name == "float32" || name == "float64";
    }
    return false;
}

bool Analyzer::isBoolType(const TypeInfo& t) const{ 
    if(auto* bt = std::get_if<BuiltinType>(&t.var)){
        return bt->name == "bool";
    }
    return false;
}

bool Analyzer::isNumericWidening(const TypeInfo& from, const TypeInfo& to) const{
    auto* btf = std::get_if<BuiltinType>(&from.var);
    auto* btt = std::get_if<BuiltinType>(&to.var);
    if(!btf || !btt) return false;
    return btf->name == "int64" && btt -> name == "float64";
}

//создание начального окружения со встроенными функциями 
sPtr<Environment> Analyzer::makeBuiltinEnv(){ 
    //глобальная область видимости parent = nullptr - конструктор по умолчанию
    auto env = std::make_shared<Environment>(); //глобальная область видимости parent = nullptr //создание таблицы символов

    env->define("print", Symbol{
        "print", makeFunction(makeBuiltin("unit"), makeBuiltin("unit")),
        false, {0, 0}}); //т.к встроен, а не написан пользователем

    env->define("input", Symbol{
        "input", makeFunction(makeBuiltin("unit"), makeBuiltin("string")),
        false, {0, 0}});

    env -> define("exit", Symbol{ 
        "exit", makeFunction(makeBuiltin("int64"), makeBuiltin("unit")),
        false, {0, 0}});

    env->define("panic", Symbol{
        "panic", makeFunction(makeBuiltin("string"), makeBuiltin("unit")),
        false, {0, 0}});
        
    env->define("input_int", Symbol{
        "input_int", makeFunction(makeBuiltin("unit"), makeBuiltin("int64")),
        false, {0,0}});

    env->define("input_float", Symbol{
        "input_float", makeFunction(makeBuiltin("unit"), makeBuiltin("float64")),
        false, {0,0}});

    return env;
}



//реализация firstPass()
void Analyzer::firstPass(const std::vector<Ptr<DeclNode>>& decls, sPtr<Environment> env, std::vector<SemanticError>& errors){ 
    for(const auto& decl : decls){
        if(const auto* alias = std::get_if<TypeAliasDecl>(&decl->var)) firstPassAlias(*alias, errors);
        else if(const auto* data = std::get_if<DataDecl>(&decl->var)) firstPassData(*data, errors);
        else if(const auto* func = std::get_if<FuncDecl>(&decl->var)) firstPassFunc(*func, env, errors);
        else if(const auto* mod = std::get_if<ModuleDecl>(&decl->var)) firstPassModule(*mod, env, errors);
    }
}

void Analyzer::firstPassAlias(const TypeAliasDecl& alias, std::vector<SemanticError>& errors){
    analyzeAliasDecl(alias, errors);
}

void Analyzer::firstPassData(const DataDecl& data, std::vector<SemanticError>& errors){
    analyzeDataDecl(data, errors);
}

//правоассоцитвное построение + левый обход - возможный доп для частичного применения
void Analyzer::firstPassFunc(const FuncDecl& fn, sPtr<Environment> env, std::vector<SemanticError>& errors){
    if(!fn.returnType){
        errors.push_back(makeError(
            "function '" + fn.name + "' missing return type annotation", fn.pos)); //не указали тип
    }

    auto typeVarMap = buildTypeVarMap(fn.typeParams);

    auto rt = resolveType(**fn.returnType, typeVarMap, errors);
    sPtr<TypeInfo> retType = rt ? *rt : makeBuiltin("unit"); //чтобы продолжили обрабатывать функцию

    sPtr<TypeInfo> funcType;
    if(fn.params.empty()){
        funcType = makeFunction(makeBuiltin("unit"), retType);
    } else {
        funcType = retType;
        bool hasError = false;

        for(int i = static_cast<int>(fn.params.size()) - 1; i >= 0; i--){
            auto paramType = resolveType(*fn.params[i].type, typeVarMap, errors);
            
            if(!paramType){ //если не разрешили тип
                hasError = true; 
                continue; 
            }

            if(!hasError) funcType = makeFunction(*paramType, funcType);
        }

        if(hasError) funcType = makeBuiltin("unit");
    }

    m_overloads[fn.name].push_back(&fn);

    auto& overloads = m_overloads[fn.name]; //проверка на дубликат
    for(std::size_t i = 0; i < overloads.size() - 1; i++){

        const FuncDecl* other = overloads[i];
        if(other->params.size() == fn.params.size()){ //сравнение типов параметров
            bool same = true;

            for(std::size_t j = 0; j < fn.params.size(); j++){
                auto t1 = resolveType(*fn.params[j].type, typeVarMap, errors);
                auto t2 = resolveType(*other->params[j].type, typeVarMap, errors);
                if(!t1 || !t2 || !(*t1)->equals(**t2)){
                    same = false; break; 
                }
            }

            if(same){
                errors.push_back(makeError(
                    "function '" + fn.name + "' with same parameter types is already declared", fn.pos));
                return;
            }
        }
    }

    // в окружение уже с модифицированным именем
    std::string mangledName = fn.name;
    for(const auto& p : fn.params){
        auto pt = resolveType(*p.type, typeVarMap, errors);
        if(pt) mangledName += "_" + (*pt)->toString();
    }

    if(!env->define(mangledName, Symbol{mangledName, funcType, false, fn.pos})){
        errors.push_back(makeError(
            "function '" + fn.name + "' is already declared", fn.pos));
    }

    if(overloads.size() == 1){ //первая перегрузка
        env->define(fn.name, Symbol{fn.name, funcType, false, fn.pos});
    }


    if(!fn.typeParams.empty()){ //с отсутсвием параметров не записываем
        m_funcTypeParams[fn.name] = fn.typeParams;
        m_genericFuncDecls[fn.name] = &fn;
    }
}

//выбор нужной перегрузки
const FuncDecl* Analyzer::resolveOverload(const std::string& name,
    const std::vector<sPtr<TypeInfo>>& argTypes,
    std::vector<SemanticError>& errors, const Pos& pos){

    auto it = m_overloads.find(name);
    if(it == m_overloads.end()) return nullptr;

    for(const FuncDecl* fn : it->second){ //точное совпадение без неявных приведений
        if(fn->params.size() != argTypes.size()) continue;
        
        bool match = true;

        auto typeVarMap = buildTypeVarMap(fn->typeParams);
        for(std::size_t i = 0; i < fn->params.size(); i++){
            auto paramType = resolveType(*fn->params[i].type, typeVarMap, errors);
            if(!paramType || !(*paramType)->equals(*argTypes[i])){
                match = false; break;
            }
        }

        if(match) return fn;
    }

    errors.push_back(makeError(
        "no matching overload for '" + name + "'", pos));
    return nullptr;
}

void Analyzer::firstPassModule(const ModuleDecl& mod, sPtr<Environment> env, std::vector<SemanticError>& errors){
    auto modEnv = std::make_shared<Environment>(env);
    firstPass(mod.decls, modEnv, errors);
            
    //положи в словарь по ключу mod -> name значение modEnv
    m_moduleEnvs[mod.name] = modEnv; //это для будущего обращения к модулю через analyzeFieldAccess

    if(!env -> define(mod.name, Symbol{mod.name, makeBuiltin("unit"), false, mod.pos})){
        errors.push_back(makeError(
            "module '" + mod.name + "' is already declared", mod.pos));
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Analyzer::Analyzer(std::string filename) : m_filename(std::move(filename)) {}

//точка входа, публичная функция analyze
std::vector<SemanticError> Analyzer::analyze(const Program& prog){ 
    std::vector <SemanticError> errors;

    auto globalEnv = makeBuiltinEnv();
    
    //все объявления высшего уровня, для возможности ссылания друг на друга
    firstPass(prog.decls, globalEnv, errors);

    //проверяем тела функций
    for(const auto& decl : prog.decls){
        analyzeDecl(*decl, globalEnv, errors); //т.к unique_ptr
    }

    if(!hasMainFunction(prog.decls)){
        errors.push_back(makeError("program must have a 'main' function", lastDeclPos(prog.decls))); //дошли до конца файла не нашли main
    }

    return errors;
}

//псевдонимом укоротим
using TypeVarMap = const std::unordered_map<std::string, sPtr<TypeInfo>>&;

//преобразуем тип TypeNode из AST в TypeInfo
//typeVarMap - Таблица подстановки параметров (a -> int64)
//удобнее сравнивать, два одинаковых узла типа, могут быть разными узлами AST
//TypeNode name - просто строка, в TypeInfo after resolveAlias it is BuiltinType("string") - сразу можно сравнить
std::optional<sPtr<TypeInfo>> Analyzer::resolveType(const TypeNode& node, 
        const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap,
        std::vector<SemanticError>& errors){    

    //BuiltinTypeNode -> BuiltinType
    if(auto* n = std::get_if<BuiltinTypeNode>(&node.var)) return resolveBuiltinType(*n);
    //параметры типа, псведоним или ADT
    if(auto* n = std::get_if<SimpleTypeNode>(&node.var)) return resolveSimpleType(*n, typeVarMap, errors);
    if(auto* n = std::get_if<GenericTypeNode>(&node.var)) return resolveGenericType(*n, typeVarMap, errors);
    if(auto* n = std::get_if<ListTypeNode>(&node.var)) return resolveListType(*n, typeVarMap, errors);
    if(auto* n = std::get_if<TupleTypeNode>(&node.var)) return resolveTupleType(*n, typeVarMap, errors);
    if(auto* n = std::get_if<FunctionTypeNode>(&node.var)) return resolveFunctionType(*n, typeVarMap, errors);

    __builtin_unreachable();
}

//вспомогательные функции перевода
std::optional<sPtr<TypeInfo>> Analyzer::resolveBuiltinType(const BuiltinTypeNode& n){
    return makeBuiltin(n.name); 
}

std::optional<sPtr<TypeInfo>> Analyzer::resolveSimpleType(const SimpleTypeNode& n, 
    TypeVarMap typeVarMap, 
    std::vector<SemanticError>& errors){
        
        auto it = typeVarMap.find(n.name);
        if(it != typeVarMap.end()) return it -> second;

        auto alias = m_registry.lookupAlias(n.name);
        if(alias) return *alias; //возвращаем тип на который он указывает

        auto data = m_registry.lookupData(n.name);
        if(data) return makeSimple(n.name);

        errors.push_back(makeError("unknown type '" + n.name + "'", n.pos));
        return std::nullopt;
    }

std::optional<sPtr<TypeInfo>> Analyzer::resolveGenericType(const GenericTypeNode& n, 
    TypeVarMap typeVarMap, 
    std::vector<SemanticError>& errors){

        auto data = m_registry.lookupData(n.name);

        //ищем data тип в реестре
        if(!data){
            errors.push_back(makeError("unknown generic type'" + n.name + "'", n.pos));
            return std::nullopt;
        }

        //data Option[a] = Ok(a) | Err(e) - ошибка
        if(n.args.size() != data->typeParams.size()){
            errors.push_back(makeError(
                "type '" + n.name + "' expects" + std::to_string(data->typeParams.size()) + 
                " type parameter(s), got " + std::to_string(n.args.size()), n.pos));
            return std::nullopt;
        }

        std::vector<sPtr<TypeInfo>> resolvedArgs;
        bool hasError = false;
        for(const auto& arg: n.args){
            auto resolved = resolveType(*arg, typeVarMap, errors);

            if(!resolved){
                hasError = true; 
                continue;
            }

            resolvedArgs.push_back(std::move(*resolved));
        }

        if(hasError) return std::nullopt;
        return makeGeneric(n.name, std::move(resolvedArgs));
    }

std::optional<sPtr<TypeInfo>> Analyzer::resolveListType(const ListTypeNode& n, 
    TypeVarMap typeVarMap, 
    std::vector<SemanticError>& errors){

        auto elem = resolveType(*n.elemType, typeVarMap, errors);
        if(!elem) return std::nullopt;
        return makeList(std::move(*elem));

    }

std::optional<sPtr<TypeInfo>> Analyzer::resolveTupleType(const TupleTypeNode& n, 
    TypeVarMap typeVarMap, 
    std::vector<SemanticError>& errors){

        std::vector<sPtr<TypeInfo>> elems;
        bool hasError = false;

        for(const auto& elem : n.elems){
            auto resolved = resolveType(*elem, typeVarMap, errors);
           
            if(!resolved){
                hasError = true;
                continue;
            }
            
            elems.push_back(std::move(*resolved));
        }
        
        if(hasError) return std::nullopt;
        return makeTuple(std::move(elems));
    }

std::optional<sPtr<TypeInfo>> Analyzer::resolveFunctionType(const FunctionTypeNode& n, 
    TypeVarMap typeVarMap, 
    std::vector<SemanticError>& errors){

        auto from = resolveType(*n.from, typeVarMap, errors);
        auto to = resolveType(*n.to, typeVarMap, errors);
        if(!from || !to) return std::nullopt;
        return makeFunction(std::move(*from), std::move(*to));
        
    }


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//анализирование объявлений(а на деле это второй проход функций и модуля)
//псевдонимы и ADT зарегетсрированы в m_registry, нужно проверить только тела
void Analyzer::analyzeDecl(const DeclNode& decl, sPtr<Environment> env, std::vector<SemanticError>& errors){
    if(const auto* fn = std::get_if<FuncDecl>(&decl.var)){
        analyzeFuncDecl(*fn, env, errors);
    } 

    else if(const auto* mod = std::get_if<ModuleDecl>(&decl.var)){
        analyzeModuleDecl(*mod, env, errors);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//анализ объявлений

//analyzeFuncDecl - проверка тела функции
void Analyzer::analyzeFuncDecl(const FuncDecl& fn, sPtr<Environment> env, std::vector<SemanticError>& errors){
    auto funcEnv = std::make_shared<Environment>(env); 

    //update теперь fn smth[a](x: a) -> a = x - у  - параметризирована типом
    auto typeVarMap = buildTypeVarMap(fn.typeParams);

    for(const auto& param : fn.params){
        auto paramType = resolveType(*param.type, typeVarMap, errors); //просто также пустоту передали, так как в таблице и так ничего не будет
        if(!paramType) continue; //ошибку уже добавили

        if(!funcEnv->define(param.name, Symbol{param.name, *paramType, false, param.pos}))
        errors.push_back(makeError(
            "parameter '" + param.name + "' is already declared", param.pos));
    }

    auto symbol = env->lookup(fn.name); //function name
    if(symbol){
        funcEnv -> define(fn.name, *symbol); //в свое локальное окружение для рекурсии
    } else {
        errors.push_back(makeError(
            "function '" + fn.name + "' not found in environment", fn.pos));
    }

    //проверка тела функции
    checkFuncBody(fn, funcEnv, errors);
}

void Analyzer::checkFuncBody(const FuncDecl& fn, sPtr<Environment> funcEnv, std::vector<SemanticError>& errors){
    //проверка тела функции
    auto bodyType = analyzeExpr(*fn.body, funcEnv, errors);
    if(!bodyType) return;

    auto typeVarMap = buildTypeVarMap(fn.typeParams);

    auto expectedType = resolveType(**fn.returnType, typeVarMap, errors);
    if(!expectedType) return;

    //своеобразный способ обработки print - без let _ = print() in 0
    if(fn.name == "main"){
        if(const auto* bt = std::get_if<BuiltinType>(&(*bodyType)->var)){
            if(bt -> name == "unit") return;
        }
    }

    if(!typesCompatible(**bodyType, **expectedType)){
        errors.push_back(makeError(
            "function '" + fn.name + "'body type '" + 
            (*bodyType)->toString() + "'does not match declared return type '" + 
            (*expectedType)->toString() + "'", fn.pos));
    }
}

//type Name = unknownType - прекратим выполнение функции без регстрации псевдонима
void Analyzer::analyzeAliasDecl(const TypeAliasDecl& alias, std::vector<SemanticError>& errors){
    auto resolved = resolveType(*alias.type, {}, errors);
    if(!resolved) return;

    if(!m_registry.registerAlias(alias.name, *resolved)){
        errors.push_back(makeError(
            "type alias '" + alias.name + "' is already declared", alias.pos));
    }
}

//Data Types declaration
void Analyzer::analyzeDataDecl(const DataDecl& data, std::vector<SemanticError>& errors){
    DataTypeInfo info; //информацию собираем о дата-типе

    info.name = data.name; //data Option[a] = //info.name = "Option"
    info.typeParams = data.typeParams;


   auto typeVarMap = buildTypeVarMap(data.typeParams);

    for(const auto& ctor : data.constructors){
        info.constructors.push_back(buildConstructorInfo(ctor, data.name, typeVarMap, errors));
    }

    if(!m_registry.registerData(std::move(info))){
        errors.push_back(makeError(
            "type '" + data.name + "' is already declared", data.pos));
    }
}

ConstructorInfo Analyzer::buildConstructorInfo(const ConstructorDecl& ctor, const std::string& dataName,
    const TypeVarMap& typeVarMap, std::vector<SemanticError>& errors){

    ConstructorInfo ctorInfo;
    ctorInfo.name = ctor.name; 
    ctorInfo.dataName = dataName; //имя дата типа которому принадлежит конструктор
    ctorInfo.isNamed = ctor.isNamed;

    for(const auto& field : ctor.fields){
        auto fieldType = resolveType(*field.type, typeVarMap, errors);
        if(!fieldType) continue; //если тип не удалось разрешить
        ctorInfo.fieldTypes.push_back(*fieldType);
        ctorInfo.fieldNames.push_back(field.name);
    }

    return ctorInfo;
}

//Module
void Analyzer::analyzeModuleDecl(const ModuleDecl& mod, sPtr<Environment> env, std::vector<SemanticError>& errors){
    auto it = m_moduleEnvs.find(mod.name);
    if(it == m_moduleEnvs.end()){
        errors.push_back(makeError(
            "module '" + mod.name + "' not found in registry", mod.pos));
        return;
    }

    //второй проход - проверяем тела всех объявлений внутри модуля
    for(const auto& decl : mod.decls){
        analyzeDecl(*decl, it->second, errors);
    }
}

    
}