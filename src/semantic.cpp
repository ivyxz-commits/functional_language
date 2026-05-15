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
    if(auto* t = std::get_if<BuiltinType>(&var)){ 
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
        "print", makeFunction(makeBuiltin("string"), makeBuiltin("unit")),
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
        if(data) return makeSimle(n->name);

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




}