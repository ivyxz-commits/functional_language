#include "semantic.hpp"

namespace Semantic{ 


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//вспомогательные static - функции


//вывод типа из TypeInfo
static std::string builtinToString(const BuiltinType& t){
    return t.name;
}

static std::string simpleToString(const SimpleType& t){
    return t.name;
}

static std::string genericToString(const GenericType& t){
    std::string s = t.name + "[";
    for(int i = 0; i < t.args.size(); i++){ 
        if(i) s += ", ";
        s += t.args[i]->toString();
    }
    return s + "]";
}

static std::string tupleToString(const TupleType& t){
    std::string s = "(";
    for(int i = 0; i < t.elems.size(); i++){ 
        if(i) s += ", ";
        s += t.elems[i]->toString();
    }
    return s + ")";
}

static std::string listToString(const ListType& t){
    return "[" + t.elem->toString() + "]";
}

static std::string functionToString(const FunctionType& t){
    return t.from->toString() + " -> " + t.to->toString();
}


//analyze()
static bool hasMainFunction(const std::vector<Ptr<DeclNode>>& decls){
    
    for(const auto& decl : decls){
        if(const auto* fn = std::get_if<FuncDecl>(&decl->var)){
            if(fn->name == "main") return true;
        }
    }

    return false;
}

static Pos lastDeclPos(const std::vector<Ptr<DeclNode>>& decls){
    if(decls.empty()) return {1, 1};
    
    const auto& last = decls.back()->var;
    
    if(const auto* func = std::get_if<FuncDecl>(&last)) return func->pos;
    if(const auto* mod = std::get_if<ModuleDecl>(&last)) return mod->pos;
    if(const auto* data = std::get_if<DataDecl>(&last)) return data->pos;
    if(const auto* alias = std::get_if<TypeAliasDecl>(&last)) return alias->pos;
    
    __builtin_unreachable();
}


//analyzeExpr()
static std::optional<sPtr<TypeInfo>> analyzeLiteral(const LiteralExpr& e){
    if(std::get_if<int64_t>(&e.value)) return makeBuiltin("int64");
    if(std::get_if<double>(&e.value)) return makeBuiltin("float64");
    if(std::get_if<std::string>(&e.value)) return makeBuiltin("string");
    if(std::get_if<bool>(&e.value)) return makeBuiltin("bool");
    if(std::get_if<std::monostate>(&e.value)) return makeBuiltin("unit");
    return std::nullopt;
}


//analyzeDataDecl()
static std::unordered_map<std::string, sPtr<TypeInfo>> buildTypeVarMap(const std::vector<std::string>& typeParams){
    std::unordered_map<std::string, sPtr<TypeInfo>> typeVarMap;
    
    //тут пригодится таблица параметров типа для разрешения полей конструктора
    for(const auto& tp : typeParams){
        typeVarMap[tp] = makeSimple(tp); //не неизвестный тип, а simpleType - пока заглушка
    }

    return typeVarMap;
}


//analyzeBinary()
static bool isArithmetic(BinaryOp op){
    return op == BinaryOp::Add || op == BinaryOp::Sub || 
           op == BinaryOp::Mul || op == BinaryOp::Div || op == BinaryOp::Mod;
}

static bool isComparison(BinaryOp op){
    return op == BinaryOp::Lt || op == BinaryOp::Le ||
           op == BinaryOp::Gt || op == BinaryOp::Ge;
}

static bool isEquality(BinaryOp op){
    return op == BinaryOp::Eq || op == BinaryOp::Neq;
}

static bool isLogical(BinaryOp op){
    return op == BinaryOp::And || op == BinaryOp::Or;
}


//analyzeLambda()

//int64 -> (int64 -> int64) - потом обход слева направо
static sPtr<TypeInfo> buildLambdaType(const std::vector<sPtr<TypeInfo>>& paramTypes, sPtr<TypeInfo> bodyType){
    //идем справа налево, что получили, что возвратили
    sPtr<TypeInfo> funcType = bodyType;
    //так как лямбда это функция, то ее тип должен быть FunctionType
    for(int i = static_cast<int>(paramTypes.size()) - 1; i >= 0; i--){
        funcType = makeFunction(paramTypes[i], funcType); //первый проход там bodyType
    }

    return funcType; // -> ... -> ... -> ......
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




//реализация вспомогательных конструкторов для TypeInfo
sPtr<TypeInfo> makeBuiltin(const std::string& name){
    return std::make_shared<TypeInfo>(TypeInfo{BuiltinType{name}});
}

sPtr<TypeInfo> makeSimple(const std::string& name){ 
    return std::make_shared<TypeInfo>(TypeInfo{SimpleType{name}});
}

sPtr<TypeInfo> makeList(sPtr<TypeInfo> elem){ 
    return std::make_shared<TypeInfo>(TypeInfo{ListType{std::move(elem)}});
}

sPtr<TypeInfo> makeTuple(std::vector<sPtr<TypeInfo>> elems){
    return std::make_shared<TypeInfo>(TypeInfo{TupleType{std::move(elems)}});
}

sPtr<TypeInfo> makeGeneric(const std::string& name, std::vector<sPtr<TypeInfo>> args){
    return std::make_shared<TypeInfo>(TypeInfo{GenericType{name, std::move(args)}});
}   

sPtr<TypeInfo> makeFunction(sPtr<TypeInfo> from, sPtr<TypeInfo> to){
    return std::make_shared<TypeInfo>(TypeInfo{FunctionType{std::move(from), std::move(to)}});
}
//без functionType не могли бы отличить функцию от обычного значения

//совместимость типов
bool TypeInfo::equals(const TypeInfo& other) const { 
    if(var.index() != other.var.index()) return false;

    //возвращаем указатель на тип(BuiltinType) //get_if не бросает исключений
    //уаказатель
    if(auto* a = std::get_if<BuiltinType>(&var)){ 
        auto* b = std::get_if<BuiltinType>(&other.var);
        return a->name == b->name;
    }

    if(auto* a = std::get_if<SimpleType>(&var)){ 
        auto* b = std::get_if<SimpleType>(&other.var);
        return a->name == b->name;
    }

    if(auto* a = std::get_if<GenericType>(&var)){ 
        auto* b = std::get_if<GenericType>(&other.var);
        if(a->name != b->name) return false;
        if(a->args.size() != b->args.size()) return false;
        for(int i = 0; i < a->args.size(); i++){ 
            if(!(*a->args[i]).equals(*b->args[i])) return false;
        }
        return true;
    }

    if(auto* a = std::get_if<TupleType>(&var)){ 
        auto* b = std::get_if<TupleType>(&other.var);
        if(a->elems.size() != b->elems.size()) return false;
        for(int i = 0; i < a->elems.size(); i++){ 
            if(!(*a->elems[i]).equals(*b->elems[i])) return false;
        }
        return true;
    }

    if(auto* a = std::get_if<ListType>(&var)){ 
        auto* b = std::get_if<ListType>(&other.var);
        return (a->elem)->equals(*b->elem);
    }

    //fn apply(f: int64 -> bool, x: string) -> bool = f(x) //Ошибка 
    if(auto* a = std::get_if<FunctionType>(&var)){ 
        auto* b = std::get_if<FunctionType>(&other.var);
        return a->from->equals(*b->from) && a->to->equals(*b -> to);
    }

    return false; //типы неизвестного вида несовместимы - никогда не дойдем до этого
}

/* bool TypeInfo::equals(const TypeInfo& other) const { //можно было и так, но лишнее создание строк -> медленее
    return toString() == other.toString();
} */


std::string TypeInfo::toString() const{ 

    if(auto* t = std::get_if<BuiltinType>(&var)) return builtinToString(*t);
    if(auto* t = std::get_if<SimpleType>(&var)) return simpleToString(*t);
    if(auto* t = std::get_if<GenericType>(&var)) return genericToString(*t);
    if(auto* t = std::get_if<TupleType>(&var)) return tupleToString(*t);
    if(auto* t = std::get_if<ListType>(&var)) return listToString(*t);
    if(auto* t = std::get_if<FunctionType>(&var)) return functionToString(*t);

    __builtin_unreachable(); //недостижим, хотя бы один из variant подойдет 
}

//красивый вывод ошибок
std::string Analyzer::binaryOpToString(Parser::BinaryOp op){
    switch(op){
        case BinaryOp::Add: return "+";
        case BinaryOp::Sub: return "-";
        case BinaryOp::Mul: return "*";
        case BinaryOp::Div: return "/";
        case BinaryOp::Mod: return "%";
        case BinaryOp::Eq: return "==";
        case BinaryOp::Neq: return "!=";
        case BinaryOp::Lt: return "<";
        case BinaryOp::Le: return "<=";
        case BinaryOp::Gt: return ">";
        case BinaryOp::Ge: return ">=";
        case BinaryOp::And: return "and";
        case BinaryOp::Or: return "or";
    }
    __builtin_unreachable();
}


//окружение
Environment::Environment(sPtr<Environment> parent) : m_parent(std::move(parent)){}

bool Environment::define(const std::string& name, Symbol sym){ 
    if(m_symbols.count(name)) return false; //потворое объявление запрещенно

    m_symbols[name] = std::move(sym); //ячейку с ключом, (создается если не было)
    return true;
}

std::optional<Symbol>Environment::lookup(const std::string& name) const{ 
    auto it = m_symbols.find(name);
    if(it != m_symbols.end()) return it->second; //возвращем symbol, если итератор не равен end()

    if(m_parent) return m_parent->lookup(name);
    return std::nullopt;
}

std::optional<Symbol>Environment::lookupLocal(const std::string& name) const{
    auto it = m_symbols.find(name); //ищем в таблице символов
    if(it != m_symbols.end()) return it->second;
    return std::nullopt;
}


//реестр ADT и псевдонимов
//регистрируем новый ADT тип
bool TypeRegistry::registerData(DataTypeInfo info){ 
    if(m_dataTypes.count(info.name)) return false;

    for(const auto& ctor : info.constructors){ 
        m_constructors[ctor.name] = ctor;
    }

    m_dataTypes[info.name] = std::move(info);
    return true;
}

bool TypeRegistry::registerAlias(const std::string& name, sPtr<TypeInfo> type){
    if(m_aliases.count(name)) return false;

    m_aliases[name] = std::move(type);
    return true;
}


//Найти ADT по имени
std::optional<DataTypeInfo> TypeRegistry::lookupData(const std::string& name) const{
    auto it = m_dataTypes.find(name);
    if(it != m_dataTypes.end()) return it -> second;
    return std::nullopt;
}

std::optional<ConstructorInfo> TypeRegistry::lookupConstructor(const std::string& name) const{ 
    auto it = m_constructors.find(name);
    if(it != m_constructors.end()) return it -> second;
    return std::nullopt;
}

//Найти псевдоним по имени
std::optional<sPtr<TypeInfo>> TypeRegistry::lookupAlias(const std::string& name) const{
    auto it = m_aliases.find(name);
    if(it != m_aliases.end()) return it -> second;
    return std::nullopt;
}

sPtr<TypeInfo> TypeRegistry::resolveAlias(sPtr<TypeInfo> type) const{
    if(auto* st = std::get_if<SimpleType>(&type->var)){ //лежит ли внутри type простой тип
        auto alias = lookupAlias(st->name);   //если это псевдоним, то смотрим его
        if(alias) return resolveAlias(*alias); //чтобы получить sPtr
    }
    return type;   
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//семантический анализатор Analyzer

SemanticError Analyzer::makeError(std::string msg, Pos pos) const{
    return SemanticError{std::move(msg), pos};
}

//вспомогательные функции
//функции проверки типов
bool Analyzer::typesCompatible(const TypeInfo& a, const TypeInfo& b) const{
    if(std::get_if<SimpleType>(&a.var)) return true; //T совместим с любым типом
    if(std::get_if<SimpleType>(&b.var)) return true; //int64 совместим с T
    return a.equals(b);
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

    if(!env->define(fn.name, Symbol{fn.name, funcType, false, fn.pos})){
        errors.push_back(makeError(
            "function '" + fn.name + "' is already declared", fn.pos));
    }

    if(!fn.typeParams.empty()){ //с отсутсвием параметров не записываем
        m_funcTypeParams[fn.name] = fn.typeParams;
        m_genericFuncDecls[fn.name] = &fn;
    }
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


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//анализ выражений
std::optional<sPtr<TypeInfo>> Analyzer::analyzeExpr(
    const ExprNode& expr, sPtr<Environment> env, std::vector<SemanticError>& errors){
        
    std::optional<sPtr<TypeInfo>> result;

    if(const auto* e = std::get_if<LiteralExpr>(&expr.var)) result = analyzeLiteral(*e);
    else if(const auto* e = std::get_if<IdentExpr>(&expr.var)) result = analyzeIdent(*e, env, errors);
    else if(const auto* e = std::get_if<UnaryExpr>(&expr.var)) result = analyzeUnary(*e, env, errors);
    else if(const auto* e = std::get_if<BinaryExpr>(&expr.var)) result = analyzeBinary(*e, env, errors);
    else if(const auto* e = std::get_if<CallExpr>(&expr.var)) result = analyzeCall(*e, env, errors);
    else if(const auto* e = std::get_if<FieldAccessExpr>(&expr.var)) result = analyzeFieldAccess(*e, env, errors);
    else if(const auto* e = std::get_if<IfExpr>(&expr.var)) result =  analyzeIf(*e, env, errors);
    else if(const auto* e = std::get_if<MatchExpr>(&expr.var)) result = analyzeMatch(*e, env, errors);
    else if(const auto* e = std::get_if<LetInExpr>(&expr.var)) result = analyzeLetIn(*e, env, errors);
    else if(const auto* e = std::get_if<LambdaExpr>(&expr.var)) result = analyzeLambda(*e, env, errors);
    else if(const auto* e = std::get_if<TupleExpr>(&expr.var)) result = analyzeTuple(*e, env, errors);
    else if(const auto* e = std::get_if<ListExpr>(&expr.var)) result = analyzeList(*e, env, errors);
    else if(const auto* e = std::get_if<ConstructorExpr>(&expr.var)) result = analyzeConstructor(*e, env, errors);
    else if(const auto* e = std::get_if<ConsExpr>(&expr.var)) result = analyzeCons(*e, env, errors); 

    if(result && *result){
        m_exprTypes[&expr] = *result; //сохраняем тип
    }

    return result; //может быть и nullopt
}

//вспомогательные

//Ident
std::optional<sPtr<TypeInfo>> Analyzer::analyzeIdent(const IdentExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){
    auto symbol = env->lookup(e.name);
    if(!symbol){
        errors.push_back(makeError(
            "undefined variable '" + e.name + "'", e.pos));
        return std::nullopt;
    }

    //встроенные функции нельзя использовать как значения
    if(e.name == "print" || e.name == "input" || 
        e.name == "exit"  || e.name == "panic" || 
        e.name == "input_int" || e.name == "input_float"){
        errors.push_back(makeError(
            "'" + e.name + "' must be called with ()", e.pos));
        return std::nullopt;
    }

    return symbol -> type; //тип идентификатора возвращаем - написан при объявлении - таблица символов
}



//analyzeIf
std::optional<sPtr<TypeInfo>> Analyzer::analyzeIf(
    const IfExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){
        
    auto condType = analyzeExpr(*e.cond, env, errors); //возвращает тип условия
    if(condType && !isBoolType(**condType)){
        errors.push_back(makeError(
            "if condition must be bool, got '" + 
            (*condType) -> toString() + "'", e.pos));
    }

    auto thenType = analyzeExpr(*e.thenBranch, env, errors);
    auto elseType = analyzeExpr(*e.elseBranch, env, errors);

    if(!thenType || !elseType) return std::nullopt; //не удалось типизировать, тип if определить нельзя

    //если разные типы - нарушает статическую типизацию
    if(!typesCompatible(**thenType, **elseType)){
        errors.push_back(makeError(
            "if branches have different types: '" + 
            (*thenType) -> toString() + "' and '" + 
            (*elseType) -> toString() + "'", e.pos));
    }

    return *thenType;
}



//analyzeUnary
std::optional<sPtr<TypeInfo>> Analyzer::analyzeUnary(
    const UnaryExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    auto operandType = analyzeExpr(*e.operand, env, errors);
    if(!operandType) return std::nullopt;

    if(e.op == UnaryOp::Neg){
        if(!isNumericType(**operandType)){
            errors.push_back(makeError(
                "unary '-' requires numeric type, got '" +
                (*operandType)->toString() + "'", e.pos));
            return std::nullopt;
        }
        return *operandType;
    }

    if(e.op == UnaryOp::Not){
        if(!isBoolType(**operandType)){
            errors.push_back(makeError(
                "'not' requires bool, got '" + 
                (*operandType) -> toString() + "'", e.pos));
            return std::nullopt;
        }
        return makeBuiltin("bool");
    }

    return std::nullopt;
}


//analyzeBinary

/*
*fn add(x: int64, y: int64) -> int64 = x + y;
*/

std::optional<sPtr<TypeInfo>> Analyzer::analyzeBinary(
    const BinaryExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){
        
    auto leftType = analyzeExpr(*e.left, env, errors);
    auto rightType = analyzeExpr(*e.right, env, errors);
    if(!leftType || !rightType) return std::nullopt; //неизвестная переменная, невалидный вызов функции, вложенная ошибка

    if(isArithmetic(e.op)) return checkArithmetic(e, *leftType, *rightType, errors);
    if(isComparison(e.op)) return checkComparison(e, *leftType, *rightType, errors);
    if(isEquality(e.op)) return checkEquality(e, *leftType, *rightType, errors);
    if(isLogical(e.op)) return checkLogical(e, *leftType, *rightType, errors);

    return std::nullopt;
}


//реализуем каждую группу операторов

std::optional<sPtr<TypeInfo>> Analyzer::checkArithmetic(const BinaryExpr& e, const sPtr<TypeInfo>& left,
    const sPtr<TypeInfo>& right, std::vector<SemanticError>& errors){

        if(!isNumericType(*left)){
            errors.push_back(makeError(
                "operator '" + binaryOpToString(e.op) + "' requires numeric type, got '" + 
                left->toString() + "'", e.pos));
            return std::nullopt;
        }

        if(!typesCompatible(*left, *right)){
            errors.push_back(makeError(
                "operator '" + binaryOpToString(e.op) + "' operands have different types: '" + 
                left->toString() + "' and '" + 
                right->toString() + "'", e.pos));
            return std::nullopt;
        }

        return left; //тип левого так как оба уже проверили
    }

std::optional<sPtr<TypeInfo>> Analyzer::checkComparison(const BinaryExpr& e, const sPtr<TypeInfo>& left,
    const sPtr<TypeInfo>& right, std::vector<SemanticError>& errors){

        if(!isNumericType(*left)){ 
            errors.push_back(makeError(
                "operator '" + binaryOpToString(e.op) + "' requires numeric type, got '" + 
                left->toString() + "'", e.pos));
            return std::nullopt;
        }

        //со строками - лексикографически не вижу пока смысла работать

        if(!typesCompatible(*left, *right)){
            errors.push_back(makeError(
                "opeator '" + binaryOpToString(e.op) + "' operands have different types: '" + 
                left->toString() + "' and '" + 
                right->toString() + "'", e.pos));
            return std::nullopt;
        }

        return makeBuiltin("bool");
    }


std::optional<sPtr<TypeInfo>> Analyzer::checkEquality(const BinaryExpr& e, const sPtr<TypeInfo>& left,
    const sPtr<TypeInfo>& right, std::vector<SemanticError>& errors){

        if(!typesCompatible(*left, *right)){
            errors.push_back(makeError(
                "opeator '" + binaryOpToString(e.op) + "' operands have different types: '" + 
                left->toString() + "' and '" + 
                right->toString() + "'", e.pos));
            return std::nullopt;
        }

        return makeBuiltin("bool");
    }

std::optional<sPtr<TypeInfo>> Analyzer::checkLogical(const BinaryExpr& e, const sPtr<TypeInfo>& left,
    const sPtr<TypeInfo>& right, std::vector<SemanticError>& errors){

        if(!isBoolType(*left) || !isBoolType(*right)){
            errors.push_back(makeError(
                "'" + binaryOpToString(e.op) + "' requires bool, got '" + 
                (!isBoolType(*left) ? left->toString() : right->toString()) + "'", e.pos));
            return std::nullopt;
        }

        return makeBuiltin("bool");    
    }



//analyzeCall 
//Для вызываемой функции - для встроенных функций
std::optional<sPtr<TypeInfo>> Analyzer::analyzeCall(
    const CallExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    if(const auto* ident = std::get_if<IdentExpr>(&e.callee->var)){
        auto builtin = analyzeCallBuiltin(*ident, e, env, errors);
        if(builtin) return builtin;
    }
    
    //тип вызываемого add - add(1, 2)
    auto calleeType = analyzeExpr(*e.callee, env, errors); //из окружения получим Тип int64 -> int64 -> int64 
    if(!calleeType) return std::nullopt;


    //Если дженерик без аргументов - ошибка
    if(e.typeArgs.empty()){
        if(const auto* ident = std::get_if<IdentExpr>(&e.callee->var)){
            if(m_funcTypeParams.count(ident->name)){
                errors.push_back(makeError(
                    "generic function '" + ident->name +
                    "' requires explicit type arguments", e.pos));
                return std::nullopt;
            }
        }
    }

    //для аргументов 
    if(!e.typeArgs.empty()){
        if(const auto* ident = std::get_if<IdentExpr>(&e.callee->var)){ //ищем дженерик функцию в окружении
            auto symbol = env->lookup(ident -> name);
            if(symbol){ //получаем typeParams функции
                auto typeVarMap = buildCallTypeVarMap(ident->name, e.typeArgs, env, errors);
                if(!typeVarMap) return std::nullopt;
                calleeType = changeTypeVars(**calleeType, *typeVarMap);

                auto it = m_genericFuncDecls.find(ident -> name);
                if(it != m_genericFuncDecls.end()){
                    checkGenericFuncBody(*it->second, *typeVarMap, env, errors);
                }
            }
        }
    }

    

    return analyzeCallArgs(e, *calleeType, env, errors);
}


//вспомогательные функции обработки generic`ов функции и ADT

//обрабатотать тело после подстановки
void Analyzer::checkGenericFuncBody(const FuncDecl& fn,
    const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap, sPtr<Environment> env,
    std::vector<SemanticError>& errors){

        auto funcEnv = std::make_shared<Environment>(env);

        for(const auto& param : fn.params){ //из параметра конкретный тип
            auto paramType = resolveType(*param.type, typeVarMap, errors);
            if(!paramType) continue; //x с типом int64, а не SimpleType("T")
            funcEnv->define(param.name, Symbol{param.name, *paramType, false, param.pos});
        }

        //сама функция
        auto symbol = env->lookup(fn.name);
        if(symbol) funcEnv->define(fn.name, *symbol);

        auto bodyType = analyzeExpr(*fn.body, funcEnv, errors);
        if(!bodyType) return;

        //ожидаемый возвращамеый тип с подстановкой
        auto expectedType = resolveType(**fn.returnType, typeVarMap, errors);
        if(!expectedType) return;

        if(!typesCompatible(**bodyType, **expectedType)){
            errors.push_back(makeError(
                "function '" + fn.name + "' body type '" +
                (*bodyType)->toString() + "' does not match return type '" +
                (*expectedType)->toString() + "' for type arguments", fn.pos));
        }
}

//получить тип переменной
std::optional<std::unordered_map<std::string, sPtr<TypeInfo>>>
    Analyzer::buildCallTypeVarMap(const std::string& funcName, 
        const std::vector<Ptr<TypeNode>>& typeArgs, sPtr<Environment> env,
        std::vector<SemanticError>& errors){ 
            
            auto it = m_funcTypeParams.find(funcName); //ищем typeParams функции
            if(it == m_funcTypeParams.end()){
                errors.push_back(makeError(
                    "function '" + funcName + "' is not generic", {}));
                return std::nullopt;
            }

            auto& typeParams = it->second;
            if(typeParams.size() != typeArgs.size()){
                errors.push_back(makeError(
                    "wrong number of type arguments", {}));
                return std::nullopt;
            }

            std::unordered_map<std::string, sPtr<TypeInfo>> typeVarMap;
            for(int i = 0; i < typeParams.size(); i++){
                auto resolved = resolveType(*typeArgs[i], {}, errors); //в семантику переводим
                if(!resolved) return std::nullopt;
                typeVarMap[typeParams[i]] = *resolved; //кладем во временную таблицу T -> int64
            }
            
            return typeVarMap;
}

//подставить полученный тип
sPtr<TypeInfo> Analyzer::changeTypeVars(const TypeInfo& type,
    const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap){

        if(const auto* st = std::get_if<SimpleType>(&type.var)){ //может быть просто строка типа T
            auto it = typeVarMap.find(st->name);
            if(it != typeVarMap.end()) return it->second;
            return std::make_shared<TypeInfo>(type);
        }

        if(const auto* ft = std::get_if<FunctionType>(&type.var)){
            return makeFunction(
                changeTypeVars(*ft->from, typeVarMap),
                changeTypeVars(*ft->to, typeVarMap));
        }

        if(const auto* lt = std::get_if<ListType>(&type.var)){
            return makeList(changeTypeVars(*lt->elem, typeVarMap));
        }

        if(const auto* tt = std::get_if<TupleType>(&type.var)){
            std::vector<sPtr<TypeInfo>> elems;
            for(const auto& e : tt->elems)
                elems.push_back(changeTypeVars(*e, typeVarMap));
            return makeTuple(std::move(elems));
        }
        return std::make_shared<TypeInfo>(type);
}



//вспомогательные функции analyzeCall()
std::optional<sPtr<TypeInfo>> Analyzer::analyzeCallPrint(const CallExpr& e, 
    sPtr<Environment> env, std::vector<SemanticError>& errors){

       if(e.args.size() != 1){
            errors.push_back(makeError("print expects 1 argument", e.pos));
            return std::nullopt;
        }

        //вызываем анализ аргумента, чтобы запомнить тип для m_exprTypes
        auto argType = analyzeExpr(*e.args[0], env, errors);
        if(!argType) return std::nullopt;

        if(const auto* bt = std::get_if<BuiltinType>(&(*argType)->var)){
            if(bt->name != "int64" && bt->name != "float64" && 
                bt->name != "string" && bt->name != "bool"){
                errors.push_back(makeError(
                    "print does not support type '" + (*argType)->toString() + "'", e.pos));
                return std::nullopt;
            }
        }
        return makeBuiltin("unit"); //своебрзная заглушка, так как тип пока не знаем 
}

std::optional<sPtr<TypeInfo>> Analyzer::analyzeCallInput(const std::string& name, const CallExpr& e,
    const std::string& retType, std::vector<SemanticError>& errors){

        if(!e.args.empty()){
            errors.push_back(makeError(name + " expects no arguments", e.pos));
            return std::nullopt;
        }

        return makeBuiltin(retType);
    }
        
std::optional<sPtr<TypeInfo>> Analyzer::analyzeCallBuiltin(const IdentExpr& ident, const CallExpr& e, 
    sPtr<Environment> env, std::vector<SemanticError>& errors){

        if(ident.name == "print") return analyzeCallPrint(e, env, errors);
        if(ident.name == "input_int") return analyzeCallInput("input_int", e, "int64", errors);
        if(ident.name == "input_float") return analyzeCallInput("input_float", e, "float64", errors);
        return std::nullopt; //не встроенная функция

        }

std::optional<sPtr<TypeInfo>> Analyzer::analyzeCallArgs(const CallExpr& e, sPtr<TypeInfo> calleeType,
    sPtr<Environment> env, std::vector<SemanticError>& errors){

        auto currentType = calleeType; //передаем имя вызываемой функции

        //если функция без параметров (unit -> x)
        auto* ft0 = std::get_if<FunctionType>(&currentType->var);
        if(ft0 && e.args.empty()){
            auto* fromBt = std::get_if<BuiltinType>(&ft0->from->var);
            if(fromBt && fromBt->name == "unit"){
                currentType = ft0->to;
            }
        }

        //каждый аргумент вызываемой функции сопоставляется с полностью прописанной версией функции
        for(const auto& arg : e.args){ //каждый аргумент || x: int64, flag: bool smth(50, nope)
            auto* funcType = std::get_if<FunctionType>(&currentType->var); //functype = makeFunction(int64, makeFunct(int64, int64))

            if(!funcType){
                errors.push_back(makeError(
                    "'" + calleeType->toString() + "' is not a function and can`t be callled", e.pos));
                return std::nullopt;
            }

            auto argType = analyzeExpr(*arg, env, errors);
            if(!argType) return std::nullopt; //тип аргумента не удалось получить
        
            if(!typesCompatible(*funcType->from, **argType)){
                errors.push_back(makeError(
                    "argument type '" + (*argType)->toString() + 
                    "' does not match expected type '" + 
                    funcType->from->toString() + "'", e.pos));
                return std::nullopt;
            }

            currentType = funcType->to;
        }

        //каррирование <-> частичное применение
        if(std::get_if<FunctionType>(&currentType->var) && e.args.size() > 1){
            errors.push_back(makeError(
                "partial application allows only one argument at a time, "
                "use style f(a)(b) isntead of f(a, b)", e.pos));
            return std::nullopt;
        }

        return currentType;
}



//если Math.add(1, 2) - identexpr, fieldaccess, callee
std::optional<sPtr<TypeInfo>> Analyzer::analyzeFieldAccess(
    const FieldAccessExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    auto objType = analyzeExpr(*e.object, env, errors); //Math в примере Math.add
    if(!objType) return std::nullopt;

    if(const auto* ident = std::get_if<IdentExpr>(&e.object->var)){
        auto result = accessModuleField(*ident, e, errors);
        if(result) return result;
    }

    //иначе обычный доступ к полю именнованного конструктора
    //если это псевдоним, то раскрываем до настоящего типа
    auto result = accessDataField(*objType, e, errors);
    if(result) return result;

    errors.push_back(makeError(
            "type '" + (*objType)->toString() + "' has no field '" + e.field + "'", e.pos));

    return std::nullopt;
}

std::optional<sPtr<TypeInfo>> Analyzer::accessModuleField(const IdentExpr& ident, 
    const FieldAccessExpr& e, std::vector<SemanticError>& errors){

        auto it = m_moduleEnvs.find(ident.name);
        if(it == m_moduleEnvs.end()) return std::nullopt;

        auto symbol = it->second->lookupLocal(e.field); //add
        if(!symbol){
            errors.push_back(makeError(
                "module '" + ident.name + "' has no member '" + e.field + "'", e.pos));
            return std::nullopt;
        }
        return symbol->type; //вернется тип для последующего сравнения
}

std::optional<sPtr<TypeInfo>> Analyzer::accessDataField(const sPtr<TypeInfo>& objType,
    const FieldAccessExpr& e, std::vector<SemanticError>& errors){


        auto resolved = m_registry.resolveAlias(objType);

        const auto* st = std::get_if<SimpleType>(&resolved->var);
        if(!st) return std::nullopt;

        auto data = m_registry.lookupData(st->name);
        if(!data) return std::nullopt;

        for(const auto& ctor : data -> constructors){
            if(!ctor.isNamed) continue;
            for(int i = 0; i < ctor.fieldNames.size(); i++){
                if(ctor.fieldNames[i] == e.field){
                    return ctor.fieldTypes[i];
                }
            }
        }

        return std::nullopt;
}



//analyzeMatch 
//все ветки одного типа, паттерны(шаблоны, образцы) совместимые с изначальным типом
std::optional<sPtr<TypeInfo>> Analyzer::analyzeMatch(
    const MatchExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    //тип сопоставляемого значения || match x {...} где x - exprNode
    auto targetType = analyzeExpr(*e.target, env, errors);
    if(!targetType) return std::nullopt;

    std::optional<sPtr<TypeInfo>> resultType;

    for(const auto& arm : e.arms){
        //создаем новую область для каждой ветки // Circle(r) -> r * r - r видно только когда shape - circle
        auto armEnv = std::make_shared<Environment>(env);

        analyzePattern(*arm.pattern, *targetType, armEnv, errors); //проверяем образец, связываем имя - 643 ожидает только литерал (не ctor) 

        //само тело ветви от '->' справа || тело может возвращать что угодно
        auto bodyType = analyzeExpr(*arm.body, armEnv, errors);
        if(!bodyType) continue;

        checkMatchArm(arm, resultType, *bodyType, errors); //проверяем что все типы совпадают в MatchArm
    }

    return resultType;
}

//все ветки - одинаковый тип
void Analyzer::checkMatchArm(const MatchArm& arm, std::optional<sPtr<TypeInfo>>& resultType, 
    const sPtr<TypeInfo>& bodyType, std::vector<SemanticError>& errors){

        if(!resultType){
            resultType = bodyType; //если первая типизированная ветка, то храним ее как ожидаемый тип
        } else if(!typesCompatible(**resultType, *bodyType)){
            errors.push_back(makeError(
                "match arms have different types: " + 
                (*resultType)->toString() + "' and '" + 
                bodyType->toString() + "'", arm.pos));
        }

}



//analyze Let in
std::optional<sPtr<TypeInfo>> Analyzer::analyzeLetIn(
    const LetInExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    auto letEnv = std::make_shared<Environment>(env);
    
    for(const auto& binding : e.bindings){
        //"то, что справа от ="
        //let x: int64 = 5 + 3 in x BinaryExpr -> analyzeBinary -> int64
        auto valueType = analyzeExpr(*binding.value, letEnv, errors); 
        if(!valueType) continue;

        if(!checkLetAnnotation(binding, *valueType, errors)) continue;
        defineLetBinding(binding, *valueType, letEnv, errors);
    }

    return analyzeExpr(*e.body, letEnv, errors); //передаем внутреннее выражение - body
}

//вспомогательные функции analyzeLetIn()
//тип значения совпадает с аннотацией || lex x: int64 = 5
bool Analyzer::checkLetAnnotation(const LetBinding& binding, const sPtr<TypeInfo>& valueType, 
    std::vector<SemanticError>& errors){

        if(!binding.type) return true;

        auto annotationType = resolveType(**binding.type, {}, errors);
        if(annotationType && !typesCompatible(*valueType, **annotationType)){
            errors.push_back(makeError(
                "let binding '" + binding.name + "': value type '" + valueType->toString() + 
                "'  does not match annotation '" + (*annotationType)->toString() + "'", binding.pos));
            return false;
        }
        return true;
}

//добавляем в окружение (x с типом int64) 
bool Analyzer::defineLetBinding(const LetBinding& binding, const sPtr<TypeInfo>& valueType,
    sPtr<Environment> letEnv, std::vector<SemanticError>& errors){

        //let x = 5, x = 10 in x - error
        if(!letEnv->define(binding.name, Symbol{binding.name, valueType, false, binding.pos})){
            errors.push_back(makeError(
                "'" + binding.name + "' is already declared in this scope", binding.pos));
            return false;
        }
        
        return true;
}



//analyzeLambda 
std::optional<sPtr<TypeInfo>>Analyzer::analyzeLambda(
    const LambdaExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    auto LambdaEnv = std::make_shared<Environment>(env); //let add = \x : int64 -> x + 1 i add(5)

    auto paramTypes = resolveLambdaParams(e, LambdaEnv, errors);
    if(!paramTypes) return std::nullopt;

    auto bodyType = analyzeExpr(*e.body, LambdaEnv, errors);
    if(!bodyType) return std::nullopt;

    return buildLambdaType(*paramTypes, *bodyType);
}

std::optional<std::vector<sPtr<TypeInfo>>> Analyzer::resolveLambdaParams(const LambdaExpr& e, sPtr<Environment> lambdaEnv, 
    std::vector<SemanticError>& errors){ 
        
        std::vector<sPtr<TypeInfo>> paramTypes;
        for(const auto& param : e.params){

            if(!param.type){
                errors.push_back(makeError(
                    "lambda parameter \'" + param.name + 
                    "\' requires a type annotation, for exapmle \\" + param.name + ": int64 -> ...", param.pos));
                return std::nullopt;
            }

            auto paramType = resolveType(**param.type, {}, errors);
            if(!paramType) return std::nullopt; //не можем построит тип лямбды

            paramTypes.push_back(*paramType);
            if(!lambdaEnv->define(param.name, Symbol{param.name, *paramType, false, param.pos})){
                errors.push_back(makeError(
                    "parameter '" + param.name + "' is already declared", param.pos));
                return std::nullopt;
            }
        }

        return paramTypes;
}



//analyzyConstructor
std::optional<sPtr<TypeInfo>> Analyzer::analyzeConstructor(
    const ConstructorExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    auto ctorInfo = m_registry.lookupConstructor(e.name);

    if(!ctorInfo){
        errors.push_back(makeError(
            "' unknown constructor '" + e.name + "'", e.pos));
        return std::nullopt;
    }

    //количество аргументов в конструкторе
    if(!checkConstructorArgCount(e, *ctorInfo, errors)) return std::nullopt;
    
    //если ADT параметизирован, то есть присутсвует Generic, пока пропускаем это более сложная реализация
    auto dataInfo = m_registry.lookupData(ctorInfo->dataName);
    bool isGeneric = dataInfo && !dataInfo -> typeParams.empty();

    
    if(isGeneric){
        if(e.typeArgs.empty()){
            errors.push_back(makeError(
                "generic constructor '" + e.name +
                "' requires explicit type arguments, use " +
                e.name + "[T](...)", e.pos));
            return std::nullopt;
        }

        if(e.typeArgs.size() != dataInfo->typeParams.size()){
            errors.push_back(makeError(
                "constructor '" + e.name + "' expects " +
                std::to_string(dataInfo->typeParams.size()) +
                " type argument(s), got " +
                std::to_string(e.typeArgs.size()), e.pos));
            return std::nullopt;
        }

        auto typeVarMap = buildTypeVarMap(dataInfo->typeParams); //временная таблица типов
        for(int i = 0; i < (int)dataInfo->typeParams.size(); i++){
            auto resolved = resolveType(*e.typeArgs[i], {}, errors);
            if(!resolved) return std::nullopt;
            typeVarMap[dataInfo->typeParams[i]] = *resolved;
        }

        for(std::size_t i = 0; i < e.args.size(); i++){
            auto argType = analyzeExpr(*e.args[i], env, errors);
            if(!argType) continue;

            //проверка типа со значением
            auto expectedType = changeTypeVars(*ctorInfo->fieldTypes[i], typeVarMap);
            if(!typesCompatible(**argType, *expectedType)){
                errors.push_back(makeError(
                    "constructor '" + e.name + "' argument " +
                    std::to_string(i + 1) + " has type '" +
                    (*argType)->toString() + "', expected '" +
                    expectedType->toString() + "'", e.pos));
            }
        }

        std::vector<sPtr<TypeInfo>> resolvedArgs;
        for(int i = 0; i < (int)dataInfo->typeParams.size(); i++){
            resolvedArgs.push_back(typeVarMap[dataInfo->typeParams[i]]);
        }
        return makeGeneric(ctorInfo->dataName, std::move(resolvedArgs));
    } else { //не generic
        checkConstructorArgs(e, *ctorInfo, env, errors);
    }

    return makeSimple(ctorInfo->dataName);
}

void Analyzer::checkConstructorArgs(const ConstructorExpr& e, const ConstructorInfo& ctorInfo, sPtr<Environment> env, 
    std::vector<SemanticError>& errors){

        for(std::size_t i = 0; i < e.args.size(); i++){
            auto argType = analyzeExpr(*e.args[i], env, errors);
            if(!argType) continue;

            if(!typesCompatible(**argType, *ctorInfo.fieldTypes[i])){
                errors.push_back(makeError(
                    "constructor '" + e.name + "' argument " + 
                    std::to_string(i + 1) + " has type '" + 
                    (*argType)->toString() + "', expected '" + 
                    ctorInfo.fieldTypes[i] -> toString() + "'", e.pos));
            }
        }
}

bool Analyzer::checkConstructorArgCount(const ConstructorExpr& e, const ConstructorInfo& ctorInfo,
    std::vector<SemanticError>& errors){

        if(e.args.size() == ctorInfo.fieldTypes.size()) return true;
        errors.push_back(makeError(
            "' constructor " + e.name + "expects " + 
            std::to_string(ctorInfo.fieldTypes.size()) + "argument(s), got " + 
            std::to_string(e.args.size()), e.pos));
        return false;
}



//Tuple and List
std::optional<sPtr<TypeInfo>> Analyzer::analyzeTuple(
    const TupleExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){ 

    std::vector<sPtr<TypeInfo>> elemTypes;
    bool hasError = false;

    for(const auto& elem : e.elems){
        auto t = analyzeExpr(*elem, env, errors);
        if(!t){
            hasError = true;
            continue;
        }
        elemTypes.push_back(*t);
    }

    if(hasError) return std::nullopt;
    return makeTuple(std::move(elemTypes));
    
}

std::optional<sPtr<TypeInfo>> Analyzer::analyzeList(
    const ListExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    if(e.elems.empty()) return makeList(makeBuiltin("unit"));

    auto firstType = analyzeExpr(*e.elems[0], env, errors);
    if(!firstType) return std::nullopt;

    for(std::size_t i = 1; i < e.elems.size(); i++){
        auto t = analyzeExpr(*e.elems[i], env, errors);
        if(!t) return std::nullopt;

        if(!typesCompatible(**firstType, **t)){
            errors.push_back(makeError(
                "list elements have inconsistent type: '" +
                (*firstType)->toString() + "' and '" + 
                (*t)->toString() + "'", e.pos));
            return std::nullopt;
        }
    }
    return makeList(*firstType);
}


//Cons
std::optional<sPtr<TypeInfo>> Analyzer::analyzeCons(
    const ConsExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){
        
        auto headType = analyzeExpr(*e.head, env, errors);
        auto tailType = analyzeExpr(*e.tail, env, errors);
        if(!headType || !tailType) return std::nullopt;

        auto* listType = std::get_if<ListType>(&(*tailType)->var);
        if(!listType){
            errors.push_back(makeError(
                "cons tail must be a list, got '" + (*tailType)->toString() + "'", e.pos));
            return std::nullopt;
        }

        auto* elemBuiltin = std::get_if<BuiltinType>(&listType->elem->var);
        if(elemBuiltin && elemBuiltin->name == "unit"){
            return makeList(*headType);
        }

        if(!typesCompatible(**headType, *listType -> elem)){
            errors.push_back(makeError(
                "cons head type '" + (*headType) -> toString() +
                "' does not match list element type '" + listType -> elem -> toString() + "'", e.pos));
            return std::nullopt;
        }

        return makeList(*headType);
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//анализ pattern`s

//совместим ли паттерн с типом x
bool Analyzer::analyzePattern(
    const PatternNode& pattern, sPtr<TypeInfo> expectedType, sPtr<Environment> env, std::vector<SemanticError>& errors){
    //expected type - match x, пусть x заранее в функции определен как x : int64, тогда ожидаемый тип int64    

    //wildcard
    if(std::get_if<WildcardPatternNode>(&pattern.var)) return analyzeWildcardPattern();

    //literal
    if(const auto* p = std::get_if<LiteralPatternNode>(&pattern.var)){
        return analyzeLiteralPattern(*p, expectedType, errors);
    }


    //одно имя - одно связывание 
    /* 

    *match(1, 2){
        *(x, x) -> x + 1; //возникает неоднозначность, функц. языки - одно имя в паттерне
    *}
    */

    //для рассмотрения имен рекурсивно в кортеже и списке - xs => x : other
    if(const auto* p = std::get_if<NamePatternNode>(&pattern.var)){
        return analyzeNamePattern(*p, expectedType, env, errors);
    }

    if(const auto* p = std::get_if<TuplePatternNode>(&pattern.var)){
        return analyzeTuplePattern(*p, expectedType, env, errors);
    }

    if(const auto* p = std::get_if<ListPatternNode>(&pattern.var)){
        return analyzeListPattern(*p, expectedType, env, errors);
    }

    //cons x : xs 

    /* 
    *fn sum(xs: [int64]) -> int64 = match xs {
    *    [] -> 0,
    *    x : rest -> x + sum(rest) }
    */

    if(const auto* p = std::get_if<ConsPatternNode>(&pattern.var)){
        return analyzeConsPattern(*p, expectedType, env, errors);
    }

    if(const auto* p = std::get_if<ConstructorPatternNode>(&pattern.var))
    return analyzeConstructorPattern(*p, expectedType, env, errors);

    __builtin_unreachable(); //все случаи это variant из фиксированного набора типов - один всегда есть
}



bool Analyzer::analyzeWildcardPattern(){
    return true;
}

bool Analyzer::analyzeLiteralPattern(const LiteralPatternNode& p, const sPtr<TypeInfo>& expectedType, 
    std::vector<SemanticError>& errors){

        sPtr<TypeInfo> litType;

        switch(p.kind){
            case LiteralPatternNode::Kind::Int: litType = makeBuiltin("int64"); break;
            case LiteralPatternNode::Kind::Real: litType = makeBuiltin("float64"); break;
            case LiteralPatternNode::Kind::String: litType = makeBuiltin("string"); break;
            case LiteralPatternNode::Kind::Bool: litType = makeBuiltin("bool"); break;
        }

        if(!typesCompatible(*expectedType, *litType)){
            errors.push_back(makeError(
                "literal pattern type '" + litType->toString() + 
                "' does not match target type '" + expectedType->toString() + "'", p.pos));
            return false;
        }

        return true;
}

bool Analyzer::analyzeNamePattern(const NamePatternNode& p, const sPtr<TypeInfo>& expectedType, 
    sPtr<Environment> env, std::vector<SemanticError>& errors){

        if(env -> lookupLocal(p.name)){
            errors.push_back(makeError(
                "variable '" + p.name + "' is already bound in this pattern", p.pos));
            return false;
        }

        env->define(p.name, Symbol{p.name, expectedType, false, p.pos});
        return true;
}

bool Analyzer::analyzeTuplePattern(const TuplePatternNode& p, const sPtr<TypeInfo>& expectedType, 
    sPtr<Environment> env, std::vector<SemanticError>& errors){

        auto* tupleType = std::get_if<TupleType>(&expectedType->var);
        if(!tupleType){
            errors.push_back(makeError(
                "tuple pattern does not match '" + expectedType->toString() + 
                "'", p.pos));
            return false;
        }

        if(p.elems.size() != tupleType->elems.size()){
            errors.push_back(makeError(
                "tuple pattern has " + std::to_string(p.elems.size()) + 
                " element(s), but type has " + std::to_string(tupleType->elems.size()), p.pos));
            return false;
        }

        bool ok = true;
        for(std::size_t i = 0; i < p.elems.size(); i++){
            if(!analyzePattern(*p.elems[i], tupleType->elems[i], env, errors)){
                ok = false;
            }
        }

        return ok;
}

bool Analyzer::analyzeListPattern(const ListPatternNode& p, const sPtr<TypeInfo>& expectedType,
    sPtr<Environment> env, std::vector<SemanticError>& errors){
        auto listType = std::get_if<ListType>(&expectedType->var);

        if(!listType){
            errors.push_back(makeError(
                "list pattern does not match '" + expectedType->toString() + 
                "'", p.pos));
            return false;
        }

        bool ok = true; //получаем все ошибки
        for(std::size_t i = 0; i < p.elems.size(); i++){
            if(!analyzePattern(*p.elems[i], listType->elem, env, errors)){
                ok = false;
            }
        }

        return ok;
}

bool Analyzer::analyzeConsPattern(const ConsPatternNode& p, const sPtr<TypeInfo>& expectedType, 
    sPtr<Environment> env, std::vector<SemanticError>& errors){

        auto* listType = std::get_if<ListType>(&expectedType->var);
        if(!listType){
            errors.push_back(makeError(
                "cons pattern does not match '" + expectedType->toString() + 
                "'", p.pos));
            return false;
        }

        //в перспективе x : y : rest
        bool ok = analyzePattern(*p.head, listType->elem, env, errors); //тип элемента списка
        ok = analyzePattern(*p.tail, expectedType, env, errors) && ok;
        return ok;
}

//теперь тоже поддерживает дженерики
bool Analyzer::analyzeConstructorPattern(const ConstructorPatternNode& p, const sPtr<TypeInfo>& expectedType, 
    sPtr<Environment> env, std::vector<SemanticError>& errors){

        auto ctorInfo = m_registry.lookupConstructor(p.name); //существует вообще? //ctorInfo - общий вид
        if(!ctorInfo){
            errors.push_back(makeError(  //несуществующий конструктор в паттерне
                "unknown constructor '" + p.name + "'", p.pos));
            return false;
        }

        if(!checkConstructorOwnership(p, *ctorInfo, expectedType, errors)) return false;

        if(!checkConstructorPatternArgCount(p, *ctorInfo, errors)) return false;

        std::unordered_map<std::string, sPtr<TypeInfo>> typeVarMap;

        //к каждому параметру -> тип
        auto dataInfo = m_registry.lookupData(ctorInfo->dataName);
        if(dataInfo && !dataInfo->typeParams.empty()){
            auto resolvedExpected = m_registry.resolveAlias(expectedType);
            if(const auto* gt = std::get_if<GenericType>(&resolvedExpected->var)){
                for(std::size_t i = 0; i < dataInfo->typeParams.size(); i++){
                    typeVarMap[dataInfo->typeParams[i]] = gt->args[i];
                }
            }
        }

        //Рекурсивно проверяем аргументы
        bool ok = true;
        for(std::size_t i = 0; i < p.args.size(); i++){
            auto fieldType = typeVarMap.empty()
                ? ctorInfo->fieldTypes[i]
                : changeTypeVars(*ctorInfo->fieldTypes[i], typeVarMap);
            if(!analyzePattern(*p.args[i], fieldType, env, errors)){
                ok = false;
            }
        }

        return ok;
}

//вспомогательные конструктора
bool Analyzer::checkConstructorOwnership(const ConstructorPatternNode& p, const ConstructorInfo& ctorInfo,
    const sPtr<TypeInfo>& expectedType, std::vector<SemanticError>& errors){

        auto resolvedExpected = m_registry.resolveAlias(expectedType); //type MyShape = Shape
        std::string typeName;
        
        if(const auto* st = std::get_if<SimpleType>(&resolvedExpected->var)) //принадлежность нужному типу
            typeName = st->name;
        else if(const auto* gt = std::get_if<GenericType>(&resolvedExpected->var))
            typeName = gt->name;
        else
            return true;

        if(typeName != ctorInfo.dataName){
            errors.push_back(makeError(
                "constructor '" + p.name + "' belongs to type '" +
                ctorInfo.dataName + "' but target has type '" +
                expectedType->toString() + "'", p.pos));
            return false;
        }
        return true;

}

bool Analyzer::checkConstructorPatternArgCount(const ConstructorPatternNode& p, const ConstructorInfo& ctorInfo,
    std::vector<SemanticError>& errors){

        if(p.args.size() == ctorInfo.fieldTypes.size()) return true;

        errors.push_back(makeError(
            "constructor '" + p.name + "' has " + 
            std::to_string(ctorInfo.fieldTypes.size()) + 
            " field(s) but pattern has" + std::to_string(p.args.size()), p.pos));
        return false;
}

}