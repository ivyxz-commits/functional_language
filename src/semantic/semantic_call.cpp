#include "semantic.hpp"
#include "semantic_utils.cpp"

namespace Semantic{

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
                auto typeVarMap = inferenceTypeArgs(ident->name, e, env, errors);
                if(!typeVarMap) return std::nullopt;
                m_callTypeMaps[&e] = *typeVarMap;
                calleeType = changeTypeVars(**calleeType, *typeVarMap); //вставляем все разрешенные типы
                checkGenericFuncBody(*m_genericFuncDecls[ident->name], *typeVarMap, env, errors);
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
                m_callTypeMaps[&e] = *typeVarMap; //сохраняем для кодогена
                calleeType = changeTypeVars(**calleeType, *typeVarMap);

                auto it = m_genericFuncDecls.find(ident -> name);
                if(it != m_genericFuncDecls.end()){
                    checkGenericFuncBody(*it->second, *typeVarMap, env, errors);
                }
            }
        }
    }

    
    //проверка перегрузки
    if(const auto* ident = std::get_if<IdentExpr>(&e.callee->var)){
        if(m_overloads.count(ident->name) && m_overloads[ident->name].size() > 1){

            std::vector<sPtr<TypeInfo>> argTypes; //собираем типы аргументов
            for(const auto& arg : e.args){
                auto t = analyzeExpr(*arg, env, errors);
                if(!t) return std::nullopt;
                argTypes.push_back(*t);
            }
            
            const FuncDecl* resolved = resolveOverload(ident->name, argTypes, errors, e.pos);
            if(!resolved) return std::nullopt;
            
            m_resolvedOverloads[&e] = resolved; //для кодогена сохраняем перегрузку в словарик
            
            // возвращаем тип возврата выбранной перегрузки
            auto typeVarMap = buildTypeVarMap(resolved->typeParams);
            return resolveType(**resolved->returnType, typeVarMap, errors);
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

        if(const auto* gt = std::get_if<GenericType>(&type.var)){
            std::vector<sPtr<TypeInfo>> newArgs; //подставлени в аргументы дженерика типа
            for(const auto& arg : gt->args){
                newArgs.push_back(changeTypeVars(*arg, typeVarMap));
            }
            return makeGeneric(gt -> name, std::move(newArgs));
        }

        return std::make_shared<TypeInfo>(type); //для BuiltinType
}


//Унификация generic`ов 

//для function и ADT
void Analyzer::unify(sPtr<TypeInfo> param, sPtr<TypeInfo> arg,
    std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap,
    std::vector<SemanticError>& errors, const Pos& pos){

        if(const auto* st = std::get_if<SimpleType>(&param->var)){
            auto it = typeVarMap.find(st->name);

            if(it != typeVarMap.end()){
                
                if(isNumericWidening(*it->second, *arg)){
                    typeVarMap[st->name] = arg;
                } else if(!typesCompatible(*it->second, *arg)){
                    errors.push_back(makeError(
                        "conflicting types for '" + st->name + "': '" +
                        it->second->toString() + "' and '" +
                        arg->toString() + "'", pos));
                }
            } else {
                typeVarMap[st->name] = arg;
            }
            return;
        }

        //FuncType: T -> U + int64 -> string
        if(const auto* fp = std::get_if<FunctionType>(&param->var)){
            if(const auto* fa = std::get_if<FunctionType>(&arg->var)){
                unify(fp->from, fa->from, typeVarMap, errors, pos);
                unify(fp->to, fa->to, typeVarMap, errors, pos);
            }
            return;
        }

        // GenericType: Box[T] + Box[int64]
        if(const auto* gp = std::get_if<GenericType>(&param->var)){
            if(const auto* ga = std::get_if<GenericType>(&arg->var)){
                if(gp->name == ga->name && gp->args.size() == ga->args.size()){
                    for(std::size_t i = 0; i < gp->args.size(); i++)
                        unify(gp->args[i], ga->args[i], typeVarMap, errors, pos);
                }
            }
            return;
        }

        // ListType: [T] + [int64]
        if(const auto* lp = std::get_if<ListType>(&param->var)){
            if(const auto* la = std::get_if<ListType>(&arg->var)){
                unify(lp->elem, la->elem, typeVarMap, errors, pos);
            }
            return;
        }

        // TupleType: (T, U) + (int64, string)
        if(const auto* tp = std::get_if<TupleType>(&param->var)){
            if(const auto* ta = std::get_if<TupleType>(&arg->var)){
                if(tp->elems.size() == ta->elems.size()){
                    for(int i = 0; i < (int)tp->elems.size(); i++)
                        unify(tp->elems[i], ta->elems[i], typeVarMap, errors, pos);
                }
            }
            return;
        }

}

//выведение параметров
std::optional<std::unordered_map<std::string, sPtr<TypeInfo>>> 
    Analyzer::inferenceTypeArgs(const std::string& funcName, const CallExpr& e,
    sPtr<Environment> env, std::vector<SemanticError>& errors){

        auto& typeParams = m_funcTypeParams[funcName];
        auto& fn = *m_genericFuncDecls[funcName];
        auto fnTypeVarMap = buildTypeVarMap(fn.typeParams);
        std::unordered_map<std::string, sPtr<TypeInfo>> typeVarMap;

        for(std::size_t i = 0; i < e.args.size() && i < fn.params.size(); i++){
            auto paramType = resolveType(*fn.params[i].type, fnTypeVarMap, errors); //SimpleType, Gener, ...
            if(!paramType) return std::nullopt;

            auto argType = analyzeExpr(*e.args[i], env, errors); //int64, string, ListType, ...
            if(!argType) return std::nullopt;
            unify(*paramType, *argType, typeVarMap, errors, e.pos); //создаем typeVarMap
        }

        // проверяем что все typeParams выведены
        for(const auto& tp : typeParams){
            if(!typeVarMap.count(tp)){
                errors.push_back(makeError(
                    "can`t inference type argument '" + tp +
                    "' for function '" + funcName + "'", e.pos));
                return std::nullopt;
            }
        }

        return typeVarMap;
}

std::optional<std::unordered_map<std::string, sPtr<TypeInfo>>> Analyzer::inferenceConstructorTypeArgs(
    const ConstructorExpr& e, const std::optional<DataTypeInfo>& dataInfo, 
    const std::optional<ConstructorInfo>& ctorInfo, sPtr<Environment> env, 
    std::vector<SemanticError>& errors){

    auto fnTypeVarMap = buildTypeVarMap(dataInfo->typeParams);
    std::unordered_map<std::string, sPtr<TypeInfo>> typeVarMap;

    for(std::size_t i = 0; i < e.args.size() && i < ctorInfo->fieldTypes.size(); i++){

        auto fieldType = ctorInfo->fieldTypes[i]; //тип поля конструктора

        auto argType = analyzeExpr(*e.args[i], env, errors); //тип аргумента
        if(!argType) return std::nullopt;
        unify(fieldType, *argType, typeVarMap, errors, e.pos);
    }

    for(const auto& tp : dataInfo->typeParams){ //проверка - все параметры выведены
        if(!typeVarMap.count(tp)){
            errors.push_back(makeError(
                "can't infer type argument '" + tp +
                "' for constructor '" + e.name + "'", e.pos));
            return std::nullopt;
        }
    }
    return typeVarMap;
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


        //явные приведения
        if(ident.name == "float64"){

            if(e.args.size() != 1){
                errors.push_back(makeError("float64() expects 1 argument", e.pos));
                    return std::nullopt;
            }
                
            auto argType = analyzeExpr(*e.args[0], env, errors);
                
            if(!argType) return std::nullopt;
                
            auto* bt = std::get_if<BuiltinType>(&(*argType)->var);
            if(!bt || (bt->name != "int64" && bt->name != "float64")){
                errors.push_back(makeError(
                    "float64() expects int64 or float64, got '" + 
                    (*argType)->toString() + "'", e.pos));
                return std::nullopt;
            }
            return makeBuiltin("float64");
        }

        if(ident.name == "int64"){
            if(e.args.size() != 1){
                errors.push_back(makeError("int64() expects 1 argument", e.pos));
                return std::nullopt;
            }
            auto argType = analyzeExpr(*e.args[0], env, errors);
            if(!argType) return std::nullopt;
            auto* bt = std::get_if<BuiltinType>(&(*argType)->var);
            if(!bt || (bt->name != "int64" && bt->name != "float64")){
                errors.push_back(makeError(
                    "int64() expects int64 or float64, got '" + 
                    (*argType)->toString() + "'", e.pos));
                return std::nullopt;
            }
            return makeBuiltin("int64");
        }

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


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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


}