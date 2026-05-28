#include "semantic.hpp"

namespace Semantic{ 

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

sPtr<TypeInfo> makeFunction(sPtr<TypeInfo> from, sPtr<TypeInfo> to){
    return std::make_shared<TypeInfo>(TypeInfo{FunctionType{std::move(from), std::move(to)}});
}

sPtr<TypeInfo> makeGeneric(const std::string& name, std::vector<sPtr<TypeInfo>> args){
    return std::make_shared<TypeInfo>(TypeInfo{GenericType{name, std::move(args)}});
}   

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

//для красивого вывода ошибок //вывод типа из TypeInfo
std::string TypeInfo::toString() const{
    if(auto* t = std::get_if<BuiltinType>(&var)){ //&this -> var - указатель на текущий объект
        return t->name;
    }

    if(auto *t = std::get_if<SimpleType>(&var)){ 
        return t->name;
    }

    if(auto *t = std::get_if<GenericType>(&var)){ 
        std::string s = t->name + "[";
        for(int i = 0; i < t->args.size(); i++){ 
            if(i) s += ", ";
            s += t->args[i]->toString();
        }
        return s + "]";
    }

    if(auto* t = std::get_if<TupleType>(&var)){ 
        std::string s = "(";
        for(int i = 0; i < t->elems.size(); i++){ 
            if(i) s += ", ";
            s += t->elems[i]->toString();
        }
        return s + ")";
    }

    if(auto* t = std::get_if<ListType>(&var)){ 
        return "[" + t->elem->toString() + "]";
    }

    if(auto *t = std::get_if<FunctionType>(&var)){ 
        return t->from->toString() + " -> " + t->to->toString();
    }

    return "<unknown>";
}

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

        //typeAliasDecl
        if(const auto* alias = std::get_if<TypeAliasDecl>(&decl->var)){ //по адресу
            analyzeAliasDecl(*alias, errors);
        }

        else if(const auto* data = std::get_if<DataDecl>(&decl->var)){
            analyzeDataDecl(*data, errors);
        }

        else if(const auto* fn = std::get_if<FuncDecl>(&decl->var)){ 
            sPtr<TypeInfo> retType;

            if(fn->returnType){
                auto rt = resolveType(**fn -> returnType, {}, errors); //optional and ptr
                retType = rt ? *rt : makeBuiltin("unit"); //невалидный тип
            } else {
                errors.push_back(makeError(
                        "function '" + fn->name + "' missing return type annotation", fn->pos));
                continue;                
            }

            sPtr<TypeInfo> funcType;
            if(fn->params.empty()){
                funcType = makeFunction(makeBuiltin("unit"), retType);
            } else {
                funcType = retType; //правоассоциативность
                bool hasError = false;

                for(int i = static_cast<int>(fn->params.size()) - 1; i >= 0; i--){
                    auto paramType = resolveType(*fn->params[i].type, {}, errors);

                    if(!paramType){
                        hasError = true;
                        continue;
                    }

                    if(!hasError) funcType = makeFunction(*paramType, funcType);
                }

                if(hasError) funcType = makeBuiltin("unit"); //заглушка
            }

            if(!env ->define(fn->name, Symbol{fn->name, funcType, false, fn->pos})){
                errors.push_back(makeError(
                    "function '" + fn->name + "'is already declared", fn->pos));
            }
        } 

        //объявление модуля
        else if(const auto* mod = std::get_if<ModuleDecl>(&decl->var)){
            auto modEnv = std::make_shared<Environment>(env);

            firstPass(mod -> decls, modEnv, errors);

            //положи в словарь по ключу mod -> name значение modEnv
            m_moduleEnvs[mod -> name] = modEnv; //это для будущего обращения к модулю через analyzeFieldAccess
            

            if(!env -> define(mod -> name, Symbol{mod -> name, makeBuiltin("unit"), false, mod -> pos})){
                errors.push_back(makeError(
                    "module '" + mod -> name + "' is already declared", mod->pos));
            }
        }
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

    bool hasMain = false; //наличие main
    Pos lastPos = {1, 1};

    for(const auto& decl : prog.decls){
        if(const auto* fn = std::get_if<FuncDecl>(&decl->var)){
            if(fn->name == "main") hasMain = true;
        }
    }

    if(!hasMain){
        errors.push_back(makeError("program must have a 'main' function", lastPos)); //дошли до конца файла не нашли main
    }

    return errors;
}


//преобразуем тип TypeNode из AST в TypeInfo
//typeVarMap - Таблица подстановки параметров (a -> int64)
//удобнее сравнивать, два одинаковых узла типа, могут быть разными узлами AST (equals)
//TypeNode name - просто строка, в TypeInfo after resolveAlias it is BuiltinType("string") -сразу можно сравнить
std::optional<sPtr<TypeInfo>> Analyzer::resolveType(const TypeNode& node, 
        const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap,
        std::vector<SemanticError>& errors){    

    //BuiltinTypeNode -> BuiltinType
    if(auto* n = std::get_if<BuiltinTypeNode>(&node.var)){
        return makeBuiltin(n -> name); 
    }

    //параметры типа, псведоним или ADT
    if(auto* n = std::get_if<SimpleTypeNode>(&node.var)){
        auto it = typeVarMap.find(n -> name);
        if(it != typeVarMap.end()) return it -> second;

        auto alias = m_registry.lookupAlias(n -> name);
        if(alias) return *alias; //возвращаем тип на который он указывает

        auto data = m_registry.lookupData(n -> name);
        if(data) return makeSimple(n->name);

        errors.push_back(makeError("unknown type '" + n->name + "'", n->pos));
        return std::nullopt;
    }

    if(auto* n = std::get_if<GenericTypeNode>(&node.var)){ 
        auto data = m_registry.lookupData(n->name);

        //ищем data тип в реестре
        if(!data){
            errors.push_back(makeError("unknown generic type'" + n->name + "'", n->pos));
            return std::nullopt;
        }

        //data Option[a] = Ok(a) | Err(e) - ошибка
        if(n->args.size() != data->typeParams.size()){
            errors.push_back(makeError(
                "type '" + n->name + "' expects" + std::to_string(data->typeParams.size()) + 
                " type parameter(s), got " + std::to_string(n->args.size()), n->pos));
            return std::nullopt;
        }

        std::vector<sPtr<TypeInfo>> resolvedArgs;
        bool hasError = false;
        for(const auto& arg: n->args){
            auto resolved = resolveType(*arg, typeVarMap, errors);

            if(!resolved){
                hasError = true; 
                continue;
            }

            resolvedArgs.push_back(std::move(*resolved));
        }

        if(hasError) return std::nullopt;
        return makeGeneric(n->name, std::move(resolvedArgs));
    }

    if(auto* n = std::get_if<ListTypeNode>(&node.var)){
        auto elem = resolveType(*n->elemType, typeVarMap, errors);
        if(!elem) return std::nullopt;
        return makeList(std::move(*elem));
    }

    if(auto* n = std::get_if<TupleTypeNode>(&node.var)){
        std::vector<sPtr<TypeInfo>> elems;
        bool hasError = false;

        for(const auto& elem : n->elems){
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

    if(auto* n = std::get_if<FunctionTypeNode>(&node.var)){
        auto from = resolveType(*n->from, typeVarMap, errors);
        auto to = resolveType(*n->to, typeVarMap, errors);
        if(!from || !to) return std::nullopt;
        return makeFunction(std::move(*from), std::move(*to));
    }

    return std::nullopt;

}


//анализирование объявлений(а на деле это второй проход и функций)
//псевдонимы и ADT зарегетсрированы в m_registry, нужно проверить только тела
void Analyzer::analyzeDecl(const DeclNode& decl, sPtr<Environment> env, std::vector<SemanticError>& errors){
    if(const auto* fn = std::get_if<FuncDecl>(&decl.var)){
        analyzeFuncDecl(*fn, env, errors);
    } 

    else if(const auto* mod = std::get_if<ModuleDecl>(&decl.var)){
        analyzeModuleDecl(*mod, env, errors);
    }
}




//функции анализации
//analyzeFuncDecl - проверка тела функции
void Analyzer::analyzeFuncDecl(const FuncDecl& fn, sPtr<Environment> env, std::vector<SemanticError>& errors){
    auto funcEnv = std::make_shared<Environment>(env); 

    //моя реализация не поддерживает fn smth[a](x: a) -> a = x - у меня функция не параметризована типом
    //std::unordered_map<std::string, sPtr<TypeInfo>> typeVarMap - она соотвественно будет пустой

    for(const auto& param : fn.params){
        auto paramType = resolveType(*param.type, {}, errors); //просто также пустоту передали, так как в таблице и так ничего не будет
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
    auto bodyType = analyzeExpr(*fn.body, funcEnv, errors);
    if(bodyType){
        auto expectedType = resolveType(**fn.returnType, {}, errors);
        if(expectedType && !typesCompatible(**bodyType, **expectedType)){
            errors.push_back(makeError(
                "function '" + fn.name + "'body type '" + 
                (*bodyType)->toString() + "'does not match declared return type '" + 
                (*expectedType)->toString() + "'", fn.pos));
        }
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


void Analyzer::analyzeDataDecl(const DataDecl& data, std::vector<SemanticError>& errors){
    DataTypeInfo info; //информацию собираем о дата-типе

    info.name = data.name; //data Option[a] = //info.name = "Option"
    info.typeParams = data.typeParams;


    std::unordered_map<std::string, sPtr<TypeInfo>> typeVarMap;
    //тут пригодится таблица параметров типа для разрешения полей конструктора
    for(const auto& tp : data.typeParams){
        typeVarMap[tp] = makeSimple(tp); //не неизвестный тип, а simpleType
    }

    for(const auto& ctor : data.constructors){
        ConstructorInfo ctorInfo;
        ctorInfo.name = ctor.name; 
        ctorInfo.dataName = data.name; //имя дата типа которому принадлежит конструктор
        ctorInfo.isNamed = ctor.isNamed;

        for(const auto& field : ctor.fields){
            auto fieldType = resolveType(*field.type, typeVarMap, errors);
            if(!fieldType) continue; //если тип не удалось разрешить
            ctorInfo.fieldTypes.push_back(*fieldType);
            ctorInfo.fieldNames.push_back(field.name);
        }

        info.constructors.push_back(std::move(ctorInfo));
    }

    if(!m_registry.registerData(std::move(info))){
        errors.push_back(makeError(
            "type '" + data.name + "' is already declared", data.pos));
    }
}

void Analyzer::analyzeModuleDecl(const ModuleDecl& mod, sPtr<Environment> env, std::vector<SemanticError>& errors){
    auto it = m_moduleEnvs.find(mod.name);
    if(it == m_moduleEnvs.end()){
        errors.push_back(makeError(
            "module '" + mod.name + "' not found in registry", mod.pos));
        return;
    }

    auto modEnv = it->second;

    //второй проход - проверяем тела всех объявлений внутри модуля
    for(const auto& decl : mod.decls){
        analyzeDecl(*decl, modEnv, errors);
    }
}


//анализатор выражений
std::optional<sPtr<TypeInfo>> Analyzer::analyzeExpr(
    const ExprNode& expr, sPtr<Environment> env, std::vector<SemanticError>& errors){
        
    std::optional<sPtr<TypeInfo>> result;

    if(const auto* e = std::get_if<LiteralExpr>(&expr.var)){
        if(std::get_if<long long>(&e->value)) result = makeBuiltin("int64");
        if(std::get_if<double>(&e->value)) result = makeBuiltin("float64");
        if(std::get_if<std::string>(&e->value)) result = makeBuiltin("string");
        if(std::get_if<bool>(&e->value)) result = makeBuiltin("bool");
        if(std::get_if<std::monostate>(&e->value)) result = makeBuiltin("unit");
    }

    else if(const auto* e = std::get_if<IdentExpr>(&expr.var)){
        auto symbol = env->lookup(e->name);
        if(!symbol){
            errors.push_back(makeError(
                "undefined variable '" + e->name + "'", e->pos));
            return std::nullopt;
        }

        //встроенные функции нельзя использовать как значения
        if(e->name == "print" || e->name == "input" || 
            e->name == "exit"  || e->name == "panic" || 
            e->name == "input_int" || e->name == "input_float"){
            errors.push_back(makeError(
                "'" + e->name + "' must be called with ()", e->pos));
            return std::nullopt;
        }

        result = symbol->type; //тип идентификатора возвращаем
    }

    else if(const auto* e = std::get_if<UnaryExpr>(&expr.var)){
        result = analyzeUnary(*e, env, errors);
    }

    else if(const auto* e = std::get_if<BinaryExpr>(&expr.var)){
        result = analyzeBinary(*e, env, errors);
    }

    else if(const auto* e = std::get_if<CallExpr>(&expr.var)){
        result = analyzeCall(*e, env, errors);
    }

    else if(const auto* e = std::get_if<FieldAccessExpr>(&expr.var)){
        result = analyzeFieldAccess(*e, env, errors);
    }

    else if(const auto* e = std::get_if<IfExpr>(&expr.var)){
        result =  analyzeIf(*e, env, errors);
    }

    else if(const auto* e = std::get_if<MatchExpr>(&expr.var)){
        result = analyzeMatch(*e, env, errors);
    }

    else if(const auto* e = std::get_if<LetInExpr>(&expr.var)){
        result = analyzeLetIn(*e, env, errors);
    }

    else if(const auto* e = std::get_if<LambdaExpr>(&expr.var)){
        result = analyzeLambda(*e, env, errors);
    }

    else if(const auto* e = std::get_if<TupleExpr>(&expr.var)){
        result = analyzeTuple(*e, env, errors);
    }

    else if(const auto* e = std::get_if<ListExpr>(&expr.var)){
        result = analyzeList(*e, env, errors);
    }

    else if(const auto* e = std::get_if<ConstructorExpr>(&expr.var)){
        result = analyzeConstructor(*e, env, errors);
    }

    if(result && *result){
        m_exprTypes[&expr] = *result; //сохраняем тип
    }

    return result; //может быть и nullopt
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

    //арифметические операторы
    if(e.op == BinaryOp::Add || e.op == BinaryOp::Sub || e.op == BinaryOp::Mul ||
        e.op == BinaryOp::Div || e.op == BinaryOp::Mod){
        
        if(!isNumericType(**leftType)){
            errors.push_back(makeError(
                "operator '" + binaryOpToString(e.op) + "' requires numeric type, got '" + 
                (*leftType)->toString() + "'", e.pos));
            return std::nullopt;
        }

        if(!typesCompatible(**leftType, **rightType)){
            errors.push_back(makeError(
                "opeator '" + binaryOpToString(e.op) + "' operands have different types: '" + 
                (*leftType)->toString() + "' and '" + 
                (*rightType)->toString() + "'", e.pos));
            return std::nullopt;
        }

        return *leftType; //тип левого так как оба уже проверили
    }

    if(e.op == BinaryOp::Lt || e.op == BinaryOp::Le ||
        e.op == BinaryOp::Ge || e.op == BinaryOp::Gt){
            
        if(!isNumericType(**leftType)){ 
            errors.push_back(makeError(
                "operator '" + binaryOpToString(e.op) + "' requires numeric type, got '" + 
                (*leftType)->toString() + "'", e.pos));
            return std::nullopt;
        }

            //со строками - лексикографически не вижу пока смысла работать

        if(!typesCompatible(**leftType, **rightType)){
            errors.push_back(makeError(
                "opeator '" + binaryOpToString(e.op) + "' operands have different types: '" + 
                (*leftType)->toString() + "' and '" + 
                (*rightType)->toString() + "'", e.pos));
            return std::nullopt;
        }

        return makeBuiltin("bool");
    }

    if(e.op == BinaryOp::Eq || e.op == BinaryOp::Neq){
        
        if(!typesCompatible(**leftType, **rightType)){
            errors.push_back(makeError(
                "opeator '" + binaryOpToString(e.op) + "' operands have different types: '" + 
                (*leftType)->toString() + "' and '" + 
                (*rightType)->toString() + "'", e.pos));
            return std::nullopt;
        }

        return makeBuiltin("bool");
    }

    //логические операторы
    if(e.op == BinaryOp::And || e.op == BinaryOp::Or){


        if(!isBoolType(**leftType)){
            errors.push_back(makeError(
                "'" + binaryOpToString(e.op) + "' requires bool, got '" + 
                (*leftType)->toString() + "'", e.pos));
            return std::nullopt;
        }

        if(!isBoolType(**rightType)){
            errors.push_back(makeError(
                "'" + binaryOpToString(e.op) + "' requires bool, got '" + 
                (*rightType)->toString() + "'", e.pos));
            return std::nullopt;
        }

        return makeBuiltin("bool");
    }

    return std::nullopt;
}


//analyzeCall 
//Для вызываемой функции 
std::optional<sPtr<TypeInfo>> Analyzer::analyzeCall(
    const CallExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    //обработка print - вынесу в отдельную функцию 
    //перехватываем тип до того, как общая логика проверяет тип
    if(const auto* ident = std::get_if<IdentExpr>(&e.callee->var)){
        if(ident->name == "print"){
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

        if(ident->name == "input_int"){
            if(!e.args.empty()){
                errors.push_back(makeError("input_int expects no arguments", e.pos));
                return std::nullopt;
            }
            return makeBuiltin("int64");
        }

        if(ident->name == "input_float"){
            if(!e.args.empty()){
                errors.push_back(makeError("input_float expects no arguments", e.pos));
                return std::nullopt;
            }
            return makeBuiltin("float64");
        }
    }


    auto calleeType = analyzeExpr(*e.callee, env, errors); //возвратит тип add(1, 2) IdentExpr("add") - в окружении найдет add с типом
    if(!calleeType) return std::nullopt;

    auto currentType = *calleeType; //передаем имя вызываемой функции

    //каждый аргумент вызываемой функции сопоставляется с полностью прописанной версией функции
    for(const auto& arg : e.args){ //каждый аргумент || x: int64, flag: bool smth(50, nope)
        auto* funcType = std::get_if<FunctionType>(&currentType->var); //functype = makeFunction(int64, makeFunct(int64, int64))
        if(!funcType){
            errors.push_back(makeError(
                "'" + (*calleeType)->toString() + "' is not a function and can`t be callled", e.pos));
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

    return currentType;
}

//если Math.add(1, 2) - identexpr, fieldaccess, callee
std::optional<sPtr<TypeInfo>> Analyzer::analyzeFieldAccess(
    const FieldAccessExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    auto objType = analyzeExpr(*e.object, env, errors); //Math в примере Math.add
    if(!objType) return std::nullopt;

    if(const auto* ident = std::get_if<IdentExpr>(&e.object->var)){
        auto it = m_moduleEnvs.find(ident->name);
        if(it != m_moduleEnvs.end()){
            auto symbol = it->second->lookupLocal(e.field); //add
            if(!symbol){
                errors.push_back(makeError(
                    "module '" + ident->name + "' has no member '" + e.field + "'", e.pos));
                return std::nullopt;
            }
            return symbol->type; //вернется тип для последующего сравнения
        }
    }

    //иначе обычный доступ к полю именнованного конструктора
    //если это псевдоним, то раскрываем до настоящего типа
    auto resolved = m_registry.resolveAlias(*objType);

    if(const auto* st = std::get_if<SimpleType>(&resolved->var)){
        auto data = m_registry.lookupData(st->name);

        if(data) {
            for(const auto& ctor : data->constructors){
                if(!ctor.isNamed) continue;
                for(int i = 0; i < ctor.fieldNames.size(); i++){
                    if(ctor.fieldNames[i] == e.field){
                        return ctor.fieldTypes[i];
                    }
                }
            }
        }
    }

    errors.push_back(makeError(
            "type '" + (*objType)->toString() + "' has no field '" + e.field + "'", e.pos));

    return std::nullopt;
}


//analyzeMatch //все ветки одного типа, паттерны(шаблоны, образцы) совместимые с изначальным типом
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

        //само тело ветви от '->' справа
        auto bodyType = analyzeExpr(*arm.body, armEnv, errors);
        if(!bodyType) continue;

        if(!resultType){
            resultType = bodyType; //если первая типизированная ветка, то храним ее как ожидаемый тип
        } else if(!typesCompatible(**resultType, **bodyType)){
            errors.push_back(makeError(
                "match arms have different types: " + 
                (*resultType)->toString() + "' and '" + 
                (*bodyType)->toString() + "'", arm.pos));
        }
    }

    return resultType;
}


//analyze Let in

 std::optional<sPtr<TypeInfo>> Analyzer::analyzeLetIn(
    const LetInExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    auto letEnv = std::make_shared<Environment>(env);
    
    for(const auto& binding : e.bindings){

        //то, что справа от =
        //let x: int64 = 5 + 3 in x BinaryExpr -> analyzeBinary -> int64
        auto valueType = analyzeExpr(*binding.value, letEnv, errors); 
        if(!valueType) continue;

        if(binding.type){
            auto annotationType = resolveType(**binding.type, {}, errors);
            if(annotationType && !typesCompatible(**valueType, **annotationType)){
                errors.push_back(makeError(
                    "let binding '" + binding.name + "': value type '" + (*valueType)->toString() + 
                    "'  does not match annotation '" + (*annotationType)->toString() + "'", binding.pos));
                continue;
            }
        }

        //let x = 5, x = 10 in x
        if(!letEnv->define(binding.name, Symbol{binding.name, *valueType, false, binding.pos})){
            errors.push_back(makeError(
                "'" + binding.name + "' is already declared in this scope", binding.pos));
            continue;
        }
    }

    return analyzeExpr(*e.body, letEnv, errors); //передаем внутреннее выражение
}


//analyzeLambda
 std::optional<sPtr<TypeInfo>>Analyzer::analyzeLambda(
    const LambdaExpr& e, sPtr<Environment> env, std::vector<SemanticError>& errors){

    auto LambdaEnv = std::make_shared<Environment>(env); //let add = \x : int64 -> x + 1 i add(5)

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
        if(!LambdaEnv->define(param.name, Symbol{param.name, *paramType, false, param.pos})){
            errors.push_back(makeError(
                "parameter '" + param.name + "' is already declared", param.pos));
            return std::nullopt;
        }
    }

    auto bodyType = analyzeExpr(*e.body, LambdaEnv, errors);
    if(!bodyType) return std::nullopt; //не можем определить, что возвращает

    //идем справа налево, что получили, что возвратили
    sPtr<TypeInfo> funcType = *bodyType;
    //так как лямбда это функция, то ее тип должен быть FunctionType
    for(int i = static_cast<int>(paramTypes.size()) - 1; i >= 0; i--){
        funcType = makeFunction(paramTypes[i], funcType); //первый проход там bodyType
    }

    return funcType; // -> ... -> ... -> ......
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
    if(e.args.size() != ctorInfo->fieldTypes.size()){
        errors.push_back(makeError(
            "' constructor " + e.name + "expects " + 
            std::to_string(ctorInfo -> fieldTypes.size()) + "argument(s), got " + 
            std::to_string(e.args.size()), e.pos));

        return std::nullopt;
    }
    
    //если ADT параметизирован, то есть присутсвует Generic, пока пропускаем это более сложная реализация
    auto dataInfo = m_registry.lookupData(ctorInfo->dataName);
    bool isGeneric = dataInfo && !dataInfo -> typeParams.empty();

    if(isGeneric){

        for(int i = 0; i < e.args.size(); i++){
            analyzeExpr(*e.args[i], env, errors); //тип определить не может, проверка на корректность аргументов выражения 
        }

        return makeSimple(ctorInfo -> dataName);

    } else {
        for(std::size_t i = 0; i < e.args.size(); i++){
            auto argType = analyzeExpr(*e.args[i], env, errors);
            if(!argType) continue; 

            if(!typesCompatible(**argType, *ctorInfo->fieldTypes[i])){
                errors.push_back(makeError(
                    "constructor '" + e.name + "' argument " + 
                    std::to_string(i + 1) + " has type '" + 
                    (*argType)->toString() + "', expected '" + 
                    ctorInfo->fieldTypes[i] -> toString() + "'", e.pos));
            }
        }

        return makeSimple(ctorInfo -> dataName);
    }
}


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


bool Analyzer::analyzePattern(
    const PatternNode& pattern, sPtr<TypeInfo> expectedType, sPtr<Environment> env, std::vector<SemanticError>& errors){
    //expected type - match x, пусть x заранее в функции определен как x : int64, тогда ожидаемый тип int64    

    //wildcard
    if(std::get_if<WildcardPatternNode>(&pattern.var)){
        return true;
    }

    //literal
    if(const auto* p = std::get_if<LiteralPatternNode>(&pattern.var)){
        sPtr<TypeInfo> litType;

        switch(p->kind){
            case LiteralPatternNode::Kind::Int: litType = makeBuiltin("int64"); break;
            case LiteralPatternNode::Kind::Real: litType = makeBuiltin("float64"); break;
            case LiteralPatternNode::Kind::String: litType = makeBuiltin("string"); break;
            case LiteralPatternNode::Kind::Bool: litType = makeBuiltin("bool"); break;
        }

        if(!typesCompatible(*expectedType, *litType)){
            errors.push_back(makeError(
                "literal pattern type '" + litType->toString() + 
                "' does not match target type '" + expectedType->toString() + "'", p->pos));
            return false;
        }

        return true;
    }

    //одно имя - одно связывание 

    /* 
    *match(1, 2){
        *(x, x) -> x + 1; //возникает неоднозначность, функц. языки - одно имя в паттерне
    *}
    */

    //для рассмотрения имен рекурсивно в кортеже и списке
    if(const auto* p = std::get_if<NamePatternNode>(&pattern.var)){
        if(env -> lookupLocal(p->name)){
            errors.push_back(makeError(
                "variable '" + p->name + "' is already bound in this pattern", p->pos));
            return false;
        }

        env->define(p->name, Symbol{p->name, expectedType, false, p->pos});
        return true;
    }

    if(const auto* p = std::get_if<TuplePatternNode>(&pattern.var)){
        auto* tupleType = std::get_if<TupleType>(&expectedType->var); //указатель на ожидаемый тип

        if(!tupleType){
            errors.push_back(makeError(
                "tuple pattern does not match '" + expectedType->toString() + 
                "'", p->pos));
            return false;
        }

        if(p->elems.size() != tupleType->elems.size()){
            errors.push_back(makeError(
                "tuple pattern has " + std::to_string(p->elems.size()) + 
                " element(s), but type has " + std::to_string(tupleType->elems.size()), p->pos));
            return false;
        }

        bool ok = true;
        for(std::size_t i = 0; i < p->elems.size(); i++){
            if(!analyzePattern(*p->elems[i], tupleType->elems[i], env, errors)){
                ok = false;
            }
        }

        return ok;
    }

    if(const auto* p = std::get_if<ListPatternNode>(&pattern.var)){ 
        auto listType = std::get_if<ListType>(&expectedType->var);

        if(!listType){
            errors.push_back(makeError(
                "list pattern does not match '" + expectedType->toString() + 
                "'", p->pos));
            return false;
        }

        bool ok = true;
        for(std::size_t i = 0; i < p->elems.size(); i++){
            if(!analyzePattern(*p->elems[i], listType->elem, env, errors)){
                ok = false;
            }
        }

        return ok;
    }


    //cons x : xs 

    /* 
    *fn sum(xs: [int64]) -> int64 = match xs {
    *    [] -> 0,
    *    x : rest -> x + sum(rest) }
    */

    if(const auto* p = std::get_if<ConsPatternNode>(&pattern.var)){
        auto* listType = std::get_if<ListType>(&expectedType->var);
        if(!listType){
            errors.push_back(makeError(
                "cons pattern does not match '" + expectedType->toString() + 
                "'", p->pos));
            return false;
        }

        bool ok = analyzePattern(*p->head, listType->elem, env, errors); //тип элемента списка
        ok = analyzePattern(*p->tail, expectedType, env, errors) && ok;
        return ok;
    }

    //обработка конструктора ADT | Some(x), None, Circle(r)
    if(const auto* p = std::get_if<ConstructorPatternNode>(&pattern.var)){
        auto ctorInfo = m_registry.lookupConstructor(p->name); //существует вообще? //ctorInfo - общий вид
        if(!ctorInfo){
            errors.push_back(makeError(  //несуществующий конструктор в паттерне
                "unknown constructor '" + p->name + "'", p->pos));
            return false;
        }

        auto resolvedExpected = m_registry.resolveAlias(expectedType); //type MyShape = Shape

        if(const auto* st = std::get_if<SimpleType>(&resolvedExpected->var)){ //принадлежность нужному типу
            if(st->name != ctorInfo->dataName){
                errors.push_back(makeError(
                    "constructor '" + p->name + "' belongs to type '" + 
                    ctorInfo->dataName + "' but target has type '" + 
                    expectedType -> toString() + "'", p->pos));
                return false;
            }
        } else if (const auto* gt = std::get_if<GenericType>(&resolvedExpected->var)){
            if(gt->name != ctorInfo->dataName){
                errors.push_back(makeError(
                    "constructor '" + p->name + "' belongs to type '" + 
                    ctorInfo->dataName + "' but target has type '" + 
                    expectedType -> toString() + "'", p->pos));
                return false;
            }
        }

        if(p->args.size() != ctorInfo->fieldTypes.size()){
            errors.push_back(makeError(
                "constructor '" + p->name + "' has " + 
                std::to_string(ctorInfo->fieldTypes.size()) + 
                " field(s) but pattern has" + std::to_string(p->args.size()), p->pos));
                return false;
        }

        //Рекурсивно проверяем аргументы
        bool ok = true;
        for(std::size_t i = 0; i < p->args.size(); i++){
            if(!analyzePattern(*p->args[i], ctorInfo->fieldTypes[i], env, errors)){
                ok = false;
            }
        }
        return ok;
    }

    __builtin_unreachable(); //все случаи это variant из фиксированного набора типов - один всегда есть
}



}