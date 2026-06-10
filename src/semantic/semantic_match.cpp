#include "semantic.hpp"
#include "semantic_utilities.hpp"

namespace Semantic{

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


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
        std::unordered_map<std::string, sPtr<TypeInfo>> typeVarMap;

        //неявные типы
        if(e.typeArgs.empty()){
            auto inferred = inferenceConstructorTypeArgs(e, dataInfo, ctorInfo, env, errors);
            if(!inferred) return std::nullopt;
            typeVarMap = *inferred;
        } else {

            if(e.typeArgs.size() != dataInfo->typeParams.size()){
                errors.push_back(makeError(
                    "constructor '" + e.name + "' expects " +
                    std::to_string(dataInfo->typeParams.size()) +
                    " type argument(s), got " +
                    std::to_string(e.typeArgs.size()), e.pos));
                return std::nullopt;
            }

            typeVarMap = buildTypeVarMap(dataInfo->typeParams); //временная таблица типов
            for(std::size_t i = 0; i < dataInfo->typeParams.size(); i++){
                auto resolved = resolveType(*e.typeArgs[i], {}, errors);
                if(!resolved) return std::nullopt;
                typeVarMap[dataInfo->typeParams[i]] = *resolved;
            }
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
        for(std::size_t i = 0; i < dataInfo->typeParams.size(); i++){
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

/*
*вспомогательные конструктора
*/

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