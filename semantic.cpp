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



}