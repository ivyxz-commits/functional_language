#include "codegen.hpp"


namespace Codegen{

//Patterns matching
//match ???
void CodeGenerator::genMatch(const MatchExpr& e, FuncContext& ctx){
    genExpr(*e.target, ctx); //получаем желаемый тип //в кучу и указатель

    int targetOff = ctx.allocLocal("__target");
    emit("mov [rbp" + std::to_string(targetOff) + "], rax"); //target to stack

    std::string endLabel = freshLabel("match_end");

    for(std::size_t i = 0; i < e.arms.size(); i++){
        const auto& arm = e.arms[i]; //берем ветку
        std::string nextLabel = freshLabel("match_next"); //если эта не подошла

        emit("mov rax, [rbp" + std::to_string(targetOff) + "]");

        std::size_t localsBefore = ctx.locals.size();

        genPattern(*arm.pattern, "rax", nextLabel, ctx);


        genExpr(*arm.body, ctx); //выполняем тело ветки если результат подошел
        emit("jmp " + endLabel);
        
        ctx.locals.resize(localsBefore);

        emitLabel(nextLabel); //следующая проверка если pattern не подошел
    }

    /* 
    *если паттерн подошел прыгнем в endLabel иначе скип и переходим дальше
    */

    //если ни один паттерн не подошел, семантика не проверяет - ??? - исчерпываемость
    std::string panicMsg = freshStrLabel();
    emitDataLabel(panicMsg + "_len");
    emitData("dq 22");
    emitDataLabel(panicMsg + "_dat");
    emitData("db `match: no matching arm`, 0");
    emit("mov rdi, " + panicMsg + "_len");
    emit("call __lang_panic");

    emitLabel(endLabel);
    ctx.removeLocal("__target");
}

//проверка шаблона с заданным значением перед match
void CodeGenerator::genPattern(const PatternNode& pattern, const std::string& valueReg,
    const std::string& failLabel, FuncContext& ctx){

    if(valueReg != "rax") emit("mov rax, " + valueReg);

    //перебор случаев
    if(std::get_if<WildcardPatternNode>(&pattern.var)) return; //всегда подходит

    /* PatternName
    *match 10{
       *x -> x + 1
    *}
    */

    //связываем переменную с текущим значением
    if(const auto* p = std::get_if<NamePatternNode>(&pattern.var)){
        genNamePattern(*p, failLabel, ctx);
        return;
    }

    //если литерал, просто сравниваем значение
    if(const auto* p = std::get_if<LiteralPatternNode>(&pattern.var)){ 
        genLiteralPattern(*p, failLabel);
    }

    //Кортеж
    if(const auto* p = std::get_if<TuplePatternNode>(&pattern.var)){
        genTuplePattern(*p, failLabel, ctx);
        return;
    }

    //ADT
    if(const auto* p = std::get_if<ConstructorPatternNode>(&pattern.var)){ 
        genConstructorPattern(*p, failLabel, ctx);
        return;
    }

    //cons - pattern x : xs
    if(const auto* p = std::get_if<ConsPatternNode>(&pattern.var)){
        genConsPattern(*p, failLabel, ctx);
        return;
    }

    //list [] или [1, 2, 3, ...]
    //проход в нормальном порядке в отличие от создания
    if(const auto* p = std::get_if<ListPatternNode>(&pattern.var)){
        genListPattern(*p, failLabel, ctx);
        return;
    }

}

//вспомогательные
void CodeGenerator::genNamePattern(const NamePatternNode& p, 
    const std::string& valueReg, FuncContext& ctx){

        int off = ctx.allocLocal(p.name); //локальная переменная с именмем паттерна
        emit("mov [rbp" + std::to_string(off) + "], rax");
}

void CodeGenerator::genLiteralPattern(const LiteralPatternNode& p, 
    const std::string& failLabel){

        if(p.kind == LiteralPatternNode::Kind::Int ||
           p.kind == LiteralPatternNode::Kind::Bool){

            genIntBoolLiteralPattern(p, failLabel);
        }

        else if(p.kind == LiteralPatternNode::Kind::Real){
            genRealLiteralPattern(p, failLabel);
        }

        else if(p.kind == LiteralPatternNode::Kind::String){
            genStringLiteralPattern(p, failLabel);
        }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//вспомогательные для genLitPat()
void CodeGenerator::genIntBoolLiteralPattern(const LiteralPatternNode& p, 
    const std::string& failLabel){

        int64_t value = 0;
        if(p.value == "yep") value = 1;
        else if(p.value == "nope") value = 0; 
        else value = static_cast<int64_t>(std::stoll(p.value)); //переводим лексему в число
        emit("cmp rax, " + std::to_string(value));
        emit("jne " + failLabel);

}

void CodeGenerator::genRealLiteralPattern(const LiteralPatternNode& p, 
    const std::string& failLabel){

        double value = std::stod(p.value);
        uint64_t bits = std::bit_cast<uint64_t>(value);
        emit("mov rcx, " + std::to_string(bits));
        emit("cmp rax, rcx");
        emit("jne " + failLabel);

}

void CodeGenerator::genStringLiteralPattern(const LiteralPatternNode& p,
    const std::string& failLabel){

        std::string label = freshStrLabel();
        std::string escaped;

        for(char c : p.value){ //db `hello`, 10, `world`, 0 - "hello\nworld"
            if(c == '\n') escaped += "`, 10, `";
            else if(c == '\\') escaped += "\\\\";
            else if(c == '"') escaped += "\\\"";
            else escaped += c;
        }

        emitDataLabel(label + "_len");
        emitData("dq " + std::to_string(p.value.size()));
        emitDataLabel(label + "_dat"); //после длины следующие 8 байт
        emitData("db `" + escaped + "`, 0"); //до байта 0 
        
        emit("mov rdi, rax"); //rdi - target, rsi - pattern_str
        emit("mov rsi, " + label);
        emit("call lang_str_eq");
        emit("cmp rax, 0");
        emit("jz " + failLabel);
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void CodeGenerator::genTuplePattern(const TuplePatternNode& p, 
    const std::string& failLabel, FuncContext& ctx){

    int ptrOff = ctx.allocLocal("__tupptr");
    emit("mov [rbp" + std::to_string(ptrOff) + "], rax"); //сохраняем указатель на кортеж

    for(std::size_t i = 0; i < p.elems.size(); ++i){
        emit("mov rax, [rbp" + std::to_string(ptrOff) + "]");
        emit("mov rax, [rax + " + std::to_string(8 + i * 8) + "]"); //rax уже указатель на первый элемент

        genPattern(*p.elems[i], "rax", failLabel, ctx); //если не подходит, значит текущая ветка кортежа - skip
    }
    ctx.removeLocal("__tupptr");
}

void CodeGenerator::genConstructorPattern(const ConstructorPatternNode& p, 
    const std::string& failLabel, FuncContext& ctx){

        int tag = getConstructorTag(p.name); //достаем тег
        emit("mov rcx, [rax]");
        emit("cmp rcx, " + std::to_string(tag));
        emit("jne " + failLabel); //тег не совпал - вышли

        int ptrOff = ctx.allocLocal("__ctor_ptr"); //сохраняем указатель на ADT
        emit("mov [rbp" + std::to_string(ptrOff) + "], rax");

        // Рекурсивно проверяем аргументы паттерна
        for(std::size_t i = 0; i < p.args.size(); ++i){
            emit("mov rax, [rbp" + std::to_string(ptrOff) + "]");
            emit("mov rax, [rax + " + std::to_string(8 + i * 8) + "]");
            genPattern(*p.args[i], "rax", failLabel, ctx);
        }

        ctx.removeLocal("__ctor_ptr");
}

void CodeGenerator::genConsPattern(const ConsPatternNode& p, 
    const std::string& failLabel, FuncContext& ctx){

        emit("mov rcx, [rax]");
        emit("cmp rcx, 0");
        emit("jz " + failLabel);

        int listOff = ctx.allocLocal("__list");
        emit("mov [rbp" + std::to_string(listOff) + "], rax");

        emit("mov rax, [rbp" + std::to_string(listOff) + "]");
        emit("mov rcx, [rax + 8]");
        genPattern(*p.head, "rcx", failLabel, ctx);

        emit("mov rax, [rbp" + std::to_string(listOff) + "]");
        emit("mov rcx, [rax + 16]");
        genPattern(*p.tail, "rcx", failLabel, ctx);

        ctx.removeLocal("__list");
}

void CodeGenerator::genListPattern(const ListPatternNode& p,
    const std::string& failLabel, FuncContext& ctx){

        if(p.elems.empty()){
            emit("mov rcx, [rax]");
            emit("cmp rcx, 0");
            emit("jnz " + failLabel); //если [], но список не пустой
            return;
        }

        for(const auto& elem : p.elems){
            emit("mov rcx, [rax]");
            emit("cmp rcx, 0");
            emit("jz " + failLabel); //если список закончился раньше паттерна - Nil => tag = 0

            emit("mov rcx, [rax + 16]"); //tail будет нужен для следующей итерации - разворчиваем cons
            int tailOff = ctx.allocLocal("__lpt");
            emit("mov [rbp" + std::to_string(tailOff) + "], rcx");

            emit("mov rax, [rax + 8]");
            genPattern(*elem, "rax", failLabel, ctx); //проверяем head

            emit("mov rax, [rbp" + std::to_string(tailOff) + "]"); //хвост становится новым cons
            ctx.removeLocal("__lpt");
        }
            
        emit("mov rcx, [rax]");
        emit("cmp rcx, 0");
        emit("jnz " + failLabel); //список не закончился - лишние элементы
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//лямбда и замыкания

// \y -> x + y ==> x - free, берем, например, из let
std::vector<std::string> CodeGenerator::findFreeVars(
    const LambdaExpr& e, const FuncContext& ctx) const{

    std::vector<std::string> result;
    std::vector<std::string> bound;
    for(const auto& p : e.params) bound.push_back(p.name);

    scanExpr(*e.body, bound, ctx, result);
    return result;
}

//захватываем то, что живет на стеке вызывающей функции
void CodeGenerator::scanExpr(const ExprNode& expr, const std::vector<std::string>& bound,
    const FuncContext& ctx, std::vector<std::string>& result) const{

    if(const auto* id = std::get_if<IdentExpr>(&expr.var)){
        bool isBound = false;
        for(const auto& b : bound) if(b == id -> name) isBound = true; //есть ли имя среди связанных?
        if(!isBound && ctx.findLocal(id->name)){ //есть во внешнем ctx
            bool found = false;
            for(const auto& r : result) if(r == id -> name) found = true;
            if(!found) result.push_back(id->name);
        }
        return;
    }

    if(const auto* bin = std::get_if<BinaryExpr>(&expr.var)){
        scanExpr(*bin -> left, bound, ctx, result);
        scanExpr(*bin -> right, bound, ctx, result);
        return;
    }

    if(const auto* unexp = std::get_if<UnaryExpr>(&expr.var)){
        scanExpr(*unexp -> operand, bound, ctx, result);
        return;
    }

    if(const auto* call = std::get_if<CallExpr>(&expr.var)){
        scanExpr(*call -> callee, bound, ctx, result);
        for(const auto& a : call->args) scanExpr(*a, bound, ctx, result);
        return;
    }

    if(const auto* _if = std::get_if<IfExpr>(&expr.var)){
        scanExpr(*_if -> cond, bound, ctx, result);
        scanExpr(*_if -> thenBranch, bound, ctx, result);
        scanExpr(*_if -> elseBranch, bound, ctx, result);
        return;
    }

    if(const auto* let = std::get_if<LetInExpr>(&expr.var)){
        for(const auto& b : let-> bindings) scanExpr(*b.value, bound, ctx, result); //значение let переменных, а потом тела
        scanExpr(*let -> body, bound, ctx, result);
        return;
    }

    if(const auto* ctor = std::get_if<ConstructorExpr>(&expr.var)){
        for(const auto& a : ctor -> args) scanExpr(*a, bound, ctx, result);
        return;
    }

    if(const auto* tuple = std::get_if<TupleExpr>(&expr.var)){
        for(const auto& e : tuple -> elems) scanExpr(*e, bound, ctx, result);
        return;
    }

    if(const auto* list = std::get_if<ListExpr>(&expr.var)){
        for(const auto& e : list->elems) scanExpr(*e, bound, ctx, result);
        return;
    }
    
    if(const auto* match = std::get_if<MatchExpr>(&expr.var)){
        scanExpr(*match -> target, bound, ctx, result);
        for(const auto& arm : match -> arms) scanExpr(*arm.body, bound, ctx, result);
        return;
    }

    if(const auto* lambda = std::get_if<LambdaExpr>(&expr.var)){
        // вложенная лямбда — добавляем её параметры в bound
        std::vector<std::string> newBound = bound;
        for(const auto& p : lambda -> params) newBound.push_back(p.name);
        scanExpr(*lambda->body, newBound, ctx, result);
        return;
    }

    /* внутренняя лямбда может хранить переменные из внешнего let к примеру
    *let base = 10 in
    *let step = 2 in
    *let f = \x: int64 -> (\y: int64 -> x + y + base + step)
    */

    if(const auto* fieldaccess = std::get_if<FieldAccessExpr>(&expr.var)){
        scanExpr(*fieldaccess->object, bound, ctx, result);
        return;
    }
}

//проблема такая же как в genFuncDecl - нужен временный буфер чтобы узнать stacksize
//асемблерный код для тела лямбды | rdi - указатель на захваченные переменные - rsi, rdi, rcx, ... явные параметры
std::string CodeGenerator::genLambdaFunc(const LambdaExpr& e,
    const std::vector<std::string>& captured, FuncContext& outer){ //с внешним контекстом

    std::string funcLabel = freshLambdaLabel();

    std::ostringstream bodyStream; //сохранили основной поток, генерируем все во временный
    std::swap(m_text, bodyStream); //временный сейчас для нас m_text

    emitLabel(funcLabel);
    emit("push rbp");
    emit("mov rbp, rsp");

    FuncContext ctx;
    ctx.name = funcLabel; //имя текущий функции в контекст

    int envOff = ctx.allocLocal("__env");
    emit("mov [rbp" + std::to_string(envOff) + "], rdi"); //ptr на внешнее окружение

    genLambdaCaptured(captured, envOff, ctx);
    genLambdaParams(e, ctx);
    genExpr(*e.body, ctx);

    std::string body = m_text.str(); //чтобы узнать точный размер стека
    std::swap(m_text, bodyStream);

    int stackSize = ctx.alignedStackSize();
    if(stackSize > 0){
        m_lambdas << "    sub rsp, " << stackSize << "\n";
    }

    m_lambdas << body; //весь текст обратно
    m_lambdas << "    mov rsp, rbp\n";
    m_lambdas << "    pop rbp\n";
    m_lambdas << "    ret\n\n";
    return funcLabel;
}

//вспомогательные генерации самого кода лямбда функции
void CodeGenerator::genLambdaCaptured(const std::vector<std::string>& captured, int envOff, FuncContext& ctx){
    for(std::size_t i = 0; i < captured.size(); ++i){
        int offset = ctx.allocLocal(captured[i]);
        emit("mov rax, [rbp" + std::to_string(envOff) + "]");
        emit("mov rax, [rax + " + std::to_string(i * 8) + "]");

        //сохраняем внешнюю переменную как обычную переменную лямбды
        emit("mov [rbp" + std::to_string(offset) + "], rax");
    }
}

void CodeGenerator::genLambdaParams(const LambdaExpr& e, FuncContext& ctx){
    static const char* lambdaArgRegs[] = {"rsi","rdx","rcx","r8","r9"}; //живет до конца программы
    
    for(std::size_t i = 0; i < e.params.size() && i < 5; ++i){
        int offset = ctx.allocLocal(e.params[i].name);
        emit("mov [rbp" + std::to_string(offset) + "], " + std::string(lambdaArgRegs[i]));
    }

    //аналогично genFuncDecl - достаем аргументы 6+ из стека || [rbp + 16], [rbp + 24], ...
    for(std::size_t i = 5; i < e.params.size(); ++i){
        int stackArgOffset = 16 + (i - 5) * 8;
        int offset = ctx.allocLocal(e.params[i].name);
        emit("mov rax, [rbp + " + std::to_string(stackArgOffset) + "]"); //кладем из общего стека (стека вызывающей функции)
        emit("mov [rbp" + std::to_string(offset) + "], rax"); //в стек нашей функции
    }
}

//для замыкания нужен сам код и env - в куче объект замыкания {code ptr, env_ptr}
//основная функция лямбды - работает по аналогу с обычной функцией, но "улучшенная" версия
void CodeGenerator::genLambda(const LambdaExpr& e, FuncContext& ctx){

    auto captured = findFreeVars(e, ctx);

    std::string funcLabel = genLambdaFunc(e, captured, ctx);

    //окружение для захваченных переменных
    std::size_t envSize = captured.empty() ? 0 : captured.size() * 8;

    if(envSize > 0){
        emitAlloc(envSize);
    } else {
        emit("xor rax, rax"); //env = NULL
    }

    int envOff = ctx.allocLocal("__lambda_env"); //сохранить env_ptr //по имени чтобы потом удалить
    emit("mov [rbp" + std::to_string(envOff) + "], rax");

    for(std::size_t i = 0; i < captured.size(); ++i){ //captured в env
        auto varOff = ctx.findLocal(captured[i]); //переменная во внешнем контексте
        if(varOff){
            emit("mov rcx, [rbp" + std::to_string(*varOff) + "]");
            emit("mov rax, [rbp" + std::to_string(envOff) + "]");
            emit("mov [rax + " + std::to_string(i * 8) + "], rcx"); //записываем captured переменную в env
        }
    }

    emitAlloc(16); //rax = closure ptr
    emit("mov rcx, " + funcLabel);
    emit("mov [rax], rcx"); //code ptr
    emit("mov rcx, [rbp" + std::to_string(envOff) + "]");
    emit("mov [rax + 8], rcx"); //env ptr

    ctx.removeLocal("__lambda_env");
}

}