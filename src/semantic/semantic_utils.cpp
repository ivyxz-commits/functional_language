#include "semantic_utils.hpp"

namespace Semantic{

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

//позиция для main()
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


}