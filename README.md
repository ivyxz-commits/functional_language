# ProgLang

Статически типизированный функциональный язык программирования с компилятором в нативный x86-64.

## Возможности

- Алгебраические типы данных (ADT) и pattern matching
- Лямбды и замыкания с захватом переменных
- Иммутабельные связные списки и кортежи
- Пространства имён (модули)
- Рекурсия вместо циклов
- Статическая типизация с проверкой на этапе компиляции


## Требования

- `g++` с поддержкой C++23
- `cmake` 3.20+
- `nasm`
- `gcc`

## Сборка

```bash
cmake -B build && cmake --build build
```


## Запуск

```bash
cmake --build build --target run
```


## Пример программы

fn factorial(n: int64) -> int64 =
    if n == 0 then 1
    else n * factorial(n - 1)

fn main() -> int64 =
let _ = print(factorial(5)) in
0


## Структура проекта

src/ - исходный код компилятора
specs/ - спецификация языка
examples/ - примеры программ
report.md - отчёт о проделанной работе


## Флаги

```bash
# вывод AST дерева в терминале
cmake --build build --target dump-ast

# вывод токенов  
cmake --build build --target dump-tokens
```