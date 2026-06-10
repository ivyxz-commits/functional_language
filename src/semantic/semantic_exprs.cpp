#include "semantic.hpp"
#include "semantic_utilities.hpp"

namespace Semantic{

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

/*
*разбор каждого из возможных выражений
*/

//Ident
std::optional<sPtr<TypeInfo>> Analyzer::analyzeIdent(const IdentExpr& e, 
    sPtr<Environment> env, std::vector<SemanticError>& errors){
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

    if(isNumericWidening(**thenType, **elseType) || isNumericWidening(**elseType, **thenType)){
        return makeBuiltin("float64");
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

        if(isNumericWidening(*left, *right) || isNumericWidening(*right, *left)){
            return makeBuiltin("float64");
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



//analyze Let_in
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

}