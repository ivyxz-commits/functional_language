#pragma once 
#include "ast.hpp"
#include "tokens.hpp"
#include <vector>

void printAst(const Parser::Program& prog);
void printTokens(const std::vector<Lexer::Token>& tokens);
void printType(const Parser::TypeNode& node, int space);
void printPattern(const Parser::PatternNode& node, int space);
void printExpr(const Parser::ExprNode& node, int space);
void printDecl(const Parser::DeclNode& node, int space);
