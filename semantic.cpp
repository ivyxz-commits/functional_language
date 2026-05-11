#include "semantic.hpp"

namespace Semantic{ 

//реализация вспомогательных конструкторов для TypeInfo

sPtr<TypeInfo> makeBuiltin(const std::string& name){
    return std::make_shared<TypeInfo>(TypeInfo{BuiltinType{name}});
}

sPtr<TypeInfo> makeSimle(const std::string& name){ 
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
        "exit", makeFunction(makeBuiltin("unit"), makeBuiltin("string")),
        false, {0, 0}});

    env -> define("exit", Symbol{ 
        "exit", makeFunction(makeBuiltin("int64"), makeBuiltin("unit")),
        false, {0, 0}});

    env->define("panic", Symbol{
        "panic", makeFunction(makeBuiltin("string"), makeBuiltin("unit")),
        false, {0, 0}});

    return env;
}

//реализация firstPass() soon...

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Analyzer::Analyzer(std::string filename) : m_filename(std::move(filename)) {}

//точка входа, публичная функция analyze
std::vector<SemanticError> Analyzer::analyze(const Program& prog){ 
    std::vector <SemanticError> errors;

    auto globalEnv = makeBuiltinEnv();
    
    //все объявления высшего уровня, для возможности ссылания друг на друга
    firstPass(prog, globalEnv, errors);

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
        }

        //data Option[a] = Ok(a) | Err(e) - ошибка
        if(n->args.size() != data->typeParams.size()){
            errors.push_back(makeError(
                "type '" + n->name + "' expects" + std::to_string(data->typeParams.size()) + 
                " type parameter(s), got " + std::to_string(n->args.size()), n->pos));
            return std::nullopt;
        }

        std::vector<sPtr<TypeInfo>> resolvedArgs;
        for(const auto& arg: n->args){
            auto resolved = resolveType(*arg, typeVarMap, errors);
            if(!resolved) return std::nullopt;
            resolvedArgs.push_back(std::move(*resolved));
        }

        return makeGeneric(n->name, std::move(resolvedArgs));
    }

    if(auto* n = std::get_if<ListTypeNode>(&node.var)){
        auto elem = resolveType(*n->elemType, typeVarMap, errors);
        if(!elem) return std::nullopt;
        return makeList(std::move(*elem));
    }

    if(auto* n = std::get_if<TupleTypeNode>(&node.var)){
        std::vector<sPtr<TypeInfo>> elems;
        for(const auto& elem : n->elems){
            auto resolved = resolveType(*elem, typeVarMap, errors);
            if(!resolved) return std::nullopt;
            elems.push_back(std::move(*resolved));
        }
        return makeTuple(std::move(elems));
    }

    if(auto* n = std::get_if<FunctionTypeNode>(&node.var)){
        auto from = resolveType(*n->from, typeVarMap, errors);
        if(!from) return std::nullopt;
        auto to = resolveType(*n->to, typeVarMap, errors);
        if(!to) return std::nullopt;
        return makeFunction(std::move(*from), std::move(*to));
    }

}

}