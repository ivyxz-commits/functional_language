#pragma once
#include "semantic_types.hpp"

namespace Semantic{

std::string builtinToString(const BuiltinType& t);
std::string simpleToString(const SimpleType& t);
std::string genericToString(const GenericType& t);
std::string tupleToString(const TupleType& t);
std::string listToString(const ListType& t);
std::string functionToString(const FunctionType& t);

bool hasMainFunction(const std::vector<Ptr<DeclNode>>& decls);
Pos lastDeclPos(const std::vector<Ptr<DeclNode>>& decls);


std::unordered_map<std::string, sPtr<TypeInfo>> buildTypeVarMap(
    const std::vector<std::string>& typeParams);

std::optional<sPtr<TypeInfo>> analyzeLiteral(const LiteralExpr& e);

bool isArithmetic(BinaryOp op);
bool isComparison(BinaryOp op);
bool isEquality(BinaryOp op);
bool isLogical(BinaryOp op);

// lambda helper
sPtr<TypeInfo> buildLambdaType(
    const std::vector<sPtr<TypeInfo>>& paramTypes, 
    sPtr<TypeInfo> bodyType);

}