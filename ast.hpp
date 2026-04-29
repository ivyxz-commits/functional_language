#pragma once 

#include <memory>
#include <vector>
#include "tokens.hpp"


namespace Parser{ 

//в связи с тем, что кода как и логики будет много, предлагаю сразу завести псевдонимы
using Pos = Lexer::SourcePos;

template<typename T>
using Ptr = std::unique_ptr<T>;

//Основные типы узлов


//узлы типов
struct TypeNode;


//узлы образцов
struct PatternNode;


//Узлы выражений
struct ExprNode;


//одним из самых высоких узлов будет узел объявления

struct DeclNode{ 
};

struct Program{ 
    std::vector<Ptr<DeclNode>> decls; 
};



}