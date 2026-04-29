#pragma once 

#include <memory>
#include "tokens.hpp"

//в связи с тем, что кода как и логики будет много, предлагаю сразу завести псевдонимы

namespace Parser{ 

using Pos = Lexer::SourcePos;

template<typename T>
using Ptr = std::unique_ptr<T>;


}