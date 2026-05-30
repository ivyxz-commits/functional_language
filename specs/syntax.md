# Синтаксис языка

## 1. Общая структура программы

Программа состоит из набора верхнеуровневых объявлений.
На верхнем уровне разрешены только объявления.
Исполняемые инструкции и вычисляемые выражения допускаются только внутри тел функций
или внутри выражений match / if / локальных связываний.

program ::= declaration* EOF

Программа обязана содержать функцию main без параметров, возвращающую int64.

---

## 2. Верхнеуровневые объявления

declaration ::= functionDecl| typeAliasDecl | dataDecl| moduleDecl

---

## 3. Объявление функций

functionDecl ::= 'fn' IDENT '(' parameterList? ')' returnType? '=' expr

parameterList ::= parameter (',' parameter)*
parameter      ::= IDENT ':' type
returnType     ::= '->' type

Функция в базовой версии языка определяется выражением.
Результатом функции является значение выражения справа от '='.

Примеры:

fn inc(x: int64) -> int64 = x + 1
fn abs(x: int64) -> int64 = if x >= 0 then x else -x

---

## 4. Объявления типов

### 4.1. Псевдонимы типов

typeAliasDecl ::= 'type' IDENT '=' type

Синоним типа не создаёт новый тип, а вводит альтернативное имя
для уже существующего типа.

### 4.2. Algebraic Data Types

dataDecl ::= 'data' IDENT typeParams? '=' constructorDecl ('|' constructorDecl)*

typeParams ::= '[' IDENT (',' IDENT)* ']'
constructorDecl ::= IDENT constructorFields?
constructorFields ::= '(' type (',' type)* ')'

Примеры:

data Option[a] = None | Some(a)
data Result[a, e] = Ok(a) | Err(e)
data List[a] = Nil | Cons(a, List[a])

---

## 5. Модули

moduleDecl ::= 'module' IDENT '{' declaration* '}'

Модули группируют верхнеуровневые объявления и формируют
отдельную область видимости.

---

## 6. Типы

type ::= builtinType | simpleType | genericType | tupleType | listType | functionType

builtinType ::= 'int8' | 'int16' | 'int32' | 'int64'| 'uint8' | 'uint16' | 'uint32' | 'uint64'| 'float32' | 'float64'| 'bool'| 'string'| 'unit'

simpleType   ::= IDENT
genericType  ::= IDENT '[' type (',' type)* ']'
tupleType    ::= '(' type ',' type (',' type)* ')'
listType     ::= '[' type ']'
functionType ::= atomicType '->' type
atomicType   ::= builtinType | simpleType | genericType | tupleType | listType | '(' type ')'


примером functionType может быть: 
Пример: Int -> String (принимает число, возвращает строку)

functionType является правоассоциативным:
a -> b -> c разбирается как a -> (b -> c)

---

## 7. Локальные связывания

localBinding ::= immutableBinding | mutableBinding

immutableBinding ::= 'let' IDENT typeAnnotation? '=' expr
mutableBinding   ::= 'mut' IDENT typeAnnotation? '=' expr

typeAnnotation ::= ':' type

В базовой версии let и mut используются внутри выражений через let-in форму.

letExpr ::= 'let' bindingList 'in' expr
bindingList ::= binding (',' binding)*
binding ::= IDENT typeAnnotation? '=' expr

Примеры:

let x = 5 in x + 1

---

## 8. Выражения

expr ::= letExpr | ifExpr | matchExpr | lambdaExpr | logicalOr

lambdaExpr ::= '\' (IDENT ':' type)+ '->' expr
в продвинутой версии:
lambdaExpr ::= '\' IDENT+ '->' expr


logicalOr      ::= logicalAnd ('or' logicalAnd)*
logicalAnd     ::= equality ('and' equality)*
equality       ::= comparison (('==' | '!=') comparison)*
comparison     ::= additive (('<' | '>' | '<=' | '>=') additive)*
additive       ::= multiplicative (('+' | '-') multiplicative)*
multiplicative ::= unary (('*' | '/' | '%') unary)*
unary          ::= ('-' | 'not') unary | postfix
postfix        ::= primary postfixOp*

postfixOp ::= callOp | fieldOp

callOp  ::= '(' argumentList? ')'
argumentList ::= expr (',' expr)*

fieldOp ::= '.' IDENT

primary ::= literal | IDENT | groupedExpr | tupleExpr | listExpr| constructorExpr

groupedExpr ::= '(' expr ')'
tupleExpr   ::= '(' expr ',' expr (',' expr)* ')'
listExpr    ::= '[' (expr (',' expr)*)? ']'
constructorExpr ::= IDENT ('(' (expr (',' expr)*)? ')')?

ifExpr ::= 'if' expr 'then' expr 'else' expr

matchExpr ::= 'match' expr '{' matchExprArm+ '}'
matchExprArm ::= pattern '->' expr ','?

---

## 9. Литералы

literal ::= INT | REAL | STRING | BOOL

---


## 10. Pattern matching

pattern ::= emptyPattern | literalPattern | namePattern | tuplePattern | constructorPattern | listPattern

emptyPattern       ::= '_'
literalPattern     ::= INT | REAL | STRING | BOOL
namePattern        ::= IDENT
tuplePattern       ::= '(' pattern (',' pattern)+ ')'
constructorPattern ::= IDENT ('(' (pattern (',' pattern)*)? ')')?
listPattern        ::= '[' (pattern (',' pattern)*)? ']'
consPattern        ::= pattern ':' pattern

Если consPattern включается в грамматику, он имеет больший приоритет,
чем tuplePattern.

let result = Some(10) in
  if 1 + 2 * 3 > 5          --ifExpr
  then match result {       -- matchExpr
      Some(v) -> v * 2,     --(name)
      _       -> 0
  }
  else 0
---

## 11. Встроенные функции

На синтаксическом уровне встроенные функции не отличаются от обычных вызовов:

print(expr)
input()
exit(code)
panic(message)
map(f, xs)
filter(pred, xs)
foldl(f, init, xs)

---

## 12. Точка входа

Корректная программа обязана содержать объявление:

fn main() -> int64 = ...

Функция:
- объявляется на верхнем уровне
- не принимает аргументов
- возвращает код завершения процесса