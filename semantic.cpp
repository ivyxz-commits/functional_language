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

}