#include "codegen.hpp"

namespace Codegen{

//expressions
void CodeGenerator::genExpr(const ExprNode& expr, FuncContext& ctx){

    if(const auto* e = std::get_if<LiteralExpr>(&expr.var)){
        genLiteral(*e, ctx);
    }
    
    else if (const auto* e = std::get_if<IdentExpr>(&expr.var)){
        genIdent(*e, expr, ctx);
    } 
    
    else if (const auto* e = std::get_if<UnaryExpr>(&expr.var)){
        genUnary(*e, ctx);
    }
    
    else if (const auto* e = std::get_if<BinaryExpr>(&expr.var)){
        genBinary(*e, ctx);
    }

    else if (const auto* e = std::get_if<CallExpr>(&expr.var)){
        genCall(*e, expr, ctx); //нужно для functionType
    }

    else if (const auto* e = std::get_if<IfExpr>(&expr.var)){
        genIf(*e, ctx);
    }

    else if (const auto* e = std::get_if<MatchExpr>(&expr.var)){
        genMatch(*e, ctx);
    }

    else if (const auto* e = std::get_if<LetInExpr>(&expr.var)){
        genLetIn(*e, ctx);
    }

    else if (const auto* e = std::get_if<LambdaExpr>(&expr.var)){
        genLambda(*e, ctx);
    }

    else if (const auto* e = std::get_if<TupleExpr>(&expr.var)){
        genTuple(*e, ctx);
    }

    else if (const auto* e = std::get_if<ListExpr>(&expr.var)){
        genList(*e, ctx);
    }

    else if (const auto* e = std::get_if<ConstructorExpr>(&expr.var)){ //let c = Circle(3.14) in ...
        genConstructor(*e, ctx);
    }

    else if (const auto* e = std::get_if<FieldAccessExpr>(&expr.var)){
        genFieldAccess(*e, ctx);
    }

    else if(const auto* e = std::get_if<ConsExpr>(&expr.var)){
        genCons(*e, ctx);
    }
}

//Ident
void CodeGenerator::genIdent(const IdentExpr& e, const ExprNode& node, FuncContext& ctx){
    auto off = ctx.findLocal(e.name); //Локальные переменные на стеке;
    if(off){
        emit("mov rax, [rbp" + std::to_string(*off) + "]");
        return;
    }

    //fn add()
    auto it = m_funcLabels.find(e.name);
    if(it != m_funcLabels.end()){

        //тип functionType? - используется как значение
        auto typeIt = m_exprTypes.find(&node);
        if(typeIt != m_exprTypes.end() && 
        std::get_if<FunctionType>(&typeIt->second->var)){
            emit("mov rdi, 16"); //замыкание
            emit("call __lang_malloc");
            emit("mov rcx, " + it->second);
            emit("mov [rax], rcx");
            emit("mov qword [rax+8], 0");
        } else {
            emit("mov rax, " + it->second); //адрес метки - обычное использование
        }
        return;
    }

    //встроенных функций нет, они будут в genCall()
}


//Literals
void CodeGenerator::genLiteral(const LiteralExpr& e, FuncContext& ctx){
    
    if(const auto* v = std::get_if<int64_t>(&e.value)) genIntLiteral(*v);

    //будем представлять наши вещественные числа как последовательность байт, rax не будет знать, что в нем лежит float
    else if(const auto* v = std::get_if<double>(&e.value)) genFloatLiteral(*v);

    //строка - структура {length, data[]} - длина 8, дата столько сколько влезет
    else if(const auto* v = std::get_if<std::string>(&e.value)) genStringLiteral(*v);

    else if(const auto* v = std::get_if<bool>(&e.value)){
        emit("mov rax, " + std::string(*v ? "1" : "0"));
    }

    //функции, например, которые ничего не возвращают
    else if(std::get_if<std::monostate>(&e.value)){
        emit("xor rax, rax"); //unit = 0
    }
}

//вспомогательные
void CodeGenerator::genIntLiteral(int64_t v){
    emit("mov rax, " + std::to_string(v));
}

void CodeGenerator::genFloatLiteral(double v){
    uint64_t bits = std::bit_cast<uint64_t>(v); //храним сырое представление битов

    //потом addsd процессору - возьми нижние 64 бита и сложи их как double например
    //для регистр <- константа нужен movabs для 64 битных
    emit("mov rax, " + std::to_string(bits) + " ; float64 " + std::to_string(v)); //перекидываем сырые биты в rax 
}

void CodeGenerator::genStringLiteral(const std::string& v){
    std::string lbl = freshStrLabel(); //уникальная метка для строки .data

    std::string escaped;

    for(char c : v){ //db `hello`, 10, `world`, 0 - "hello\nworld"
        if(c == '\n') escaped += "`, 10, `";
        else if(c == '\\') escaped += "\\\\";
        else if(c == '"') escaped += "\\\"";
        else escaped += c;
    }

    emitDataLabel(lbl + "_len");
    emitData("dq " + std::to_string(v.size()));
    emitDataLabel(lbl + "_dat"); //после длины следующие 8 байт
    emitData("db `" + escaped + "`, 0"); //до байта 0 
    emit("mov rax, " + lbl + "_len");
}


//Unary
void CodeGenerator::genUnary(const UnaryExpr& e, FuncContext& ctx){
    genExpr(*e.operand, ctx);

    if(e.op == UnaryOp::Neg){
        if(isFloatExpr(*e.operand)){
            emit("movq xmm0, rax");
            emit("mov rcx, 0x8000000000000000"); //маска для 1 старшого бита
            emit("movq xmm1, rcx"); //8 байт за одну операцию
            emit("xorpd xmm0, xmm1");
            emit("movq rax, xmm0");
        } else {
            emit("neg rax");
        }
    } else if(e.op == UnaryOp::Not){
        emit("xor rax, 1"); // 0 v 1 | 1 v 1 = false
    }
}


//Binary
void CodeGenerator::genBinary(const BinaryExpr& e, FuncContext& ctx){
    bool leftIsFloat  = isFloatExpr(*e.left);
    bool rightIsFloat = isFloatExpr(*e.right);
    bool isFloat = leftIsFloat || rightIsFloat;
    emit("; genBinary isFloat=" + std::string(isFloat ? "true" : "false"));

    genExpr(*e.left, ctx);
    if(isFloat && !leftIsFloat){
        emit("cvtsi2sd xmm0, rax"); //int64 => float64
        emit("movq rax, xmm0");
    }

    int tmpOff = ctx.allocLocal("__binlhs");
    emit("mov [rbp" + std::to_string(tmpOff) + "], rax");

    genExpr(*e.right, ctx);
    if(isFloat && !rightIsFloat){
        emit("cvtsi2sd xmm0, rax"); // int64 => float64
        emit("movq rax, xmm0");
    }

    if(isFloat) emit("movq xmm1, rax");
    emit("mov rcx, rax");

    emit("mov rax, [rbp" + std::to_string(tmpOff) + "]");
    ctx.removeLocal("__binlhs");

    if(isFloat) emit("movq xmm0, rax");

    if(isFloat) genFloatOp(e.op);
    else genIntOp(e.op);
}

//вспомогательные
void CodeGenerator::genFloatOp(BinaryOp op){
    switch(op){
        case BinaryOp::Add: emit("addsd xmm0, xmm1"); break;
        case BinaryOp::Sub: emit("subsd xmm0, xmm1"); break;
        case BinaryOp::Mul: emit("mulsd xmm0, xmm1"); break;
        case BinaryOp::Div: 
            emit("xorpd xmm2, xmm2");
            emit("ucomisd xmm1, xmm2");
            emit("jne .div_ok_" + std::to_string(m_labelCnt));
            emit("call __lang_panic");
            emit("mov rdi, __div_zero_len");
            emitLabel(".div_ok_" + std::to_string(m_labelCnt++));
            emit("divsd xmm0, xmm1"); break;
        case BinaryOp::Eq:
            emit("ucomisd xmm0, xmm1");
            emit("sete al");
            emit("movzx rax, al");
            return;
        case BinaryOp::Neq:
            emit("ucomisd xmm0, xmm1");
            emit("setne al");
            emit("movzx rax, al");
            return;
        case BinaryOp::Lt:
            emit("ucomisd xmm0, xmm1");
            emit("setb al");
            emit("movzx rax, al");
            return;
        case BinaryOp::Le:
            emit("ucomisd xmm0, xmm1");
            emit("setbe al");
            emit("movzx rax, al");
            return;
        case BinaryOp::Gt:
            emit("ucomisd xmm0, xmm1"); // меняем порядок
            emit("seta al");
            emit("movzx rax, al");
            return;
        case BinaryOp::Ge:
            emit("ucomisd xmm0, xmm1");
            emit("setae al");
            emit("movzx rax, al");
            return;
        default: break;
    }
    emit("movq rax, xmm0");
}

void CodeGenerator::genIntOp(BinaryOp op){
    switch(op){
        case BinaryOp::Add: emit("add rax, rcx"); break;
        case BinaryOp::Sub: emit("sub rax, rcx"); break;
        case BinaryOp::Mul: emit("imul rax, rcx"); break;
        case BinaryOp::Div:
            emit("cmp rcx, 0"); //проверка деления на 0
            emit("jne .div_ok_" + std::to_string(m_labelCnt));
            emit("mov rdi, __div_zero_len");
            emit("call __lang_panic");
            emitLabel(".div_ok_" + std::to_string(m_labelCnt++));
            emit("cqo");
            emit("idiv rcx"); //rax - частное
            break;
        case BinaryOp::Mod:
        emit("cmp rcx, 0"); //проверка деления на 0
            emit("jne .div_ok_" + std::to_string(m_labelCnt));
            emit("mov rdi, __div_zero_len");
            emit("call __lang_panic");
            emitLabel(".div_ok_" + std::to_string(m_labelCnt++));
            emit("cqo");
            emit("idiv rcx");
            emit("mov rax, rdx");
            break;
        case BinaryOp::Eq:
            emit("cmp rax, rcx"); //ZF = 1?
            emit("sete al"); //al = 1?
            emit("movzx rax, al");
            break;
        case BinaryOp::Neq:
            emit("cmp rax, rcx");
            emit("setne al");
            emit("movzx rax, al");
            break;
        case BinaryOp::And:
            emit("and rax, rcx");
            break;
        case BinaryOp::Or:
            emit("or rax, rcx");
            break;
        case BinaryOp::Lt:
            emit("cmp rax, rcx");
            emit("setl al");
            emit("movzx rax, al");
            break;
        case BinaryOp::Le:
            emit("cmp rax, rcx");
            emit("setle al");
            emit("movzx rax, al");
            break;
        case BinaryOp::Gt: //ZF == 0 SF == и OF
            emit("cmp rax, rcx");
            emit("setg al");
            emit("movzx rax, al");
            break;
        case BinaryOp::Ge:
            emit("cmp rax, rcx");
            emit("setge al"); //127 - (-1) //10000000
            emit("movzx rax, al");
            break;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//Call - логика функций и лямбда
void CodeGenerator::genCall(const CallExpr& e,const ExprNode& node, FuncContext& ctx){

    std::vector<int> argOffsets = genCallPrepareArgs(e, ctx);

    //проверка частичного применения, если оно, то genPartAppl сам разберется
    if(isPartialCall(node)){
        genPartialApply(e, argOffsets, ctx);

        //убираем временные переменные из контекста
        for(std::size_t i = 0; i < argOffsets.size(); i++){
            ctx.removeLocal("__arg");
        }

        return;
    }

    bool isClosureCall = genCallCheckClosure(e, ctx); //замыкание или нет
    
    //по ABI аргументы кладутся на стек в обратном порядке, чтобы первый элемент по меньшему адресу rbp
    //a -> [984], b -> [992] | a ближе к стеку => rbp + 16 = a
    if(!isClosureCall) genCallLoadArgs(argOffsets);


    if(const auto* ident = std::get_if<IdentExpr>(&e.callee->var)){
        genCallIdent(*ident, e, argOffsets, ctx);
    } else { //вызываемое не идентификатор, результат выражения
        genExpr(*e.callee, ctx);
        genCallClosure(argOffsets); //здесь описаноо следующее
    }
        
    //если функция принимает 2 аргумента, а передаю 1, -> новая функция которая ждет второй аргумент
        /* 
        *fn double(x: int64) -> int64 -> int64 =
        *\y: int64 -> x * y //возвратим 2 * y - адрес функции | x = 2

        *fn main() -> int64 =
        *double(2)(10) //double(2) вовзращает лямбду
        */

    genCallCleanup(argOffsets, ctx);
}

//вспомогательные
void CodeGenerator::genCallBuiltin(const IdentExpr& ident, const CallExpr& e){
    if(ident.name == "print"){
        if(!e.args.empty()){
            if(isFloatExpr(*e.args[0])){
                emit("call lang_print_float");
            } else if(isStringExpr(*e.args[0])){
                emit("call __lang_print_str");
            } else {
                emit("call __lang_print_int");
            }
        }
    }

    else if(ident.name == "input"){
        emit("call __lang_read_str");
    }

    //читаем и парсим int
    else if(ident.name == "input_int"){
        emit("call __lang_read_str");
        emit("mov rdi, rax");
        emit("call lang_parse_int");
    }

    else if(ident.name == "input_float"){
        emit("call __lang_read_str");
        emit("mov rdi, rax");
        emit("call lang_parse_float");
    }

    /*
    else if(ident->name == "parse_int"){
        emit("call lang_parse_int");
    }

    else if(ident->name == "parse_float"){
        emit("call lang_parse_float");
    } */ //пока без этого

    else if(ident.name == "exit"){
        emit("call __lang_exit");
    }

    else if(ident.name == "panic"){
        emit("call __lang_panic");
    }

    //приведения типов
    else if(ident.name == "float64"){
        auto it = m_exprTypes.find(e.args[0].get());
        
        if(it != m_exprTypes.end()){
            auto* bt = std::get_if<BuiltinType>(&it->second->var);
            if(bt && bt->name == "int64"){
                emit("cvtsi2sd xmm0, rdi");
                emit("movq rax, xmm0");
            } else {
                emit("movq rax, rdi"); //уже float64
            }
        }
    }

    //дробную часть обрежем
    else if(ident.name == "int64"){
        auto it = m_exprTypes.find(e.args[0].get());
        if(it != m_exprTypes.end()){
            auto* bt = std::get_if<BuiltinType>(&it->second->var);
            if(bt && bt->name == "float64"){
                emit("movq xmm0, rdi");
                emit("cvttsd2si rax, xmm0");
            } else {
                emit("mov rax, rdi"); //уже int64
            }
        }
    }


}
    
void CodeGenerator::genCallClosure(const std::vector<int>& argOffsets){ 
    emit("mov r11, [rax]");
    emit("mov rdi, [rax + 8]");

    for(int i = static_cast<int>(argOffsets.size()) - 1; i >= 5; i--){
        emit("push qword [rbp" + std::to_string(argOffsets[i]) + "]");
    }

    for(int i = static_cast<int>(argOffsets.size()) - 1; i >= 0 && i < 5; i--){
        emit("mov " + std::string(argReg(i + 1)) +
                ", [rbp" + std::to_string(argOffsets[i]) + "]");
    }

    emit("call r11"); //call __fn_double - лямбда замыкание
    
    int stackArgs = static_cast<int>(argOffsets.size()) - 5;
    if(stackArgs > 0){
        emit("add rsp, " + std::to_string(stackArgs * 8));
    }
    /* 
    *fn double(x: int64) -> int64 -> int64 =
    *\y: int64 -> x * y

    *fn main() -> int64 =
    *double(2)(10)
    */
}

//обрабатывает функцию по имени
void CodeGenerator::genCallIdent(const IdentExpr& ident, const CallExpr& e,
    const std::vector<int>& argOffsets, FuncContext& ctx){ 

        //дженерик функция?
        if(m_genericFuncs.count(ident.name)){
            auto it = m_callTypeMaps.find(&e);
            if(it != m_callTypeMaps.end()){
                auto& typeVarMap = it->second;

                std::string suffix;
                for(const auto& tp : m_genericFuncs[ident.name]->typeParams){

                    auto tit = typeVarMap.find(tp);
                    if(tit != typeVarMap.end())
                        suffix += "_" + mangleTypeName(tit->second);
                }

                std::string label = "__fn_" + ident.name + suffix;
                if(!m_generatedInstances.count(label)){
                    m_generatedInstances.insert(label);
                    genGenericFuncDecl(*m_genericFuncs[ident.name], label, typeVarMap);
                }
                emit("call " + label);
                return;
            }
        }

        //выбираем нужную метку
        auto overloadIt = m_resolvedOverloads.find(&e);
        
        if(overloadIt != m_resolvedOverloads.end()){
            const FuncDecl* fn = overloadIt->second;
            std::string label = "__fn_" + fn->name;
            for(const auto& p : fn->params){
                label += "_" + getTypeNodeName(*p.type);
            }
            emit("call " + label);
            return;
        }

        auto it = m_funcLabels.find(ident.name);
        if(it != m_funcLabels.end()){
            emit("call " + it->second);
            return;
        }

        auto off = ctx.findLocal(ident.name);
        if(off){
            emit("mov rax, [rbp" + std::to_string(*off) + "]");
            genCallClosure(argOffsets); //ptr_lambda + captured_env
        }

        if(genCallBuiltin(ident, e), true) return; //встроенная функция
}

/*
*Дополнительные вспомогательные
*/

std::vector<int> CodeGenerator::genCallPrepareArgs(const CallExpr& e, FuncContext& ctx){
    
    sPtr<TypeInfo> calleeType = nullptr;
    auto calleeIt = m_exprTypes.find(e.callee.get());
    if(calleeIt != m_exprTypes.end()) calleeType = calleeIt->second;

    std::vector<int> argOffsets;
    for(std::size_t i = 0; i < e.args.size(); i++){
        genExpr(*e.args[i], ctx);

        if(calleeType){ //int64 -> float64
            auto t = calleeType;
            for(std::size_t j = 0; j < i; j++){
                if(auto* ft = std::get_if<FunctionType>(&t->var)){
                    t = ft->to;
                }
            }

            if(auto* ft = std::get_if<FunctionType>(&t -> var)){
                auto* paramBt = std::get_if<BuiltinType>(&ft -> from -> var);
                auto argIt = m_exprTypes.find(e.args[i].get());
                if(argIt != m_exprTypes.end()){
                    auto* argBt = std::get_if<BuiltinType>(&argIt -> second->var);
                    if(paramBt && argBt &&
                    paramBt->name == "float64" && argBt -> name == "int64"){
                        emit("cvtsi2sd xmm0, rax");
                        emit("movq rax, xmm0");
                    }
                }
            }
        }

        int off = ctx.allocLocal("__arg");
        emit("mov [rbp" + std::to_string(off) + "], rax");
        argOffsets.push_back(off);
    }

    return argOffsets;
}

bool CodeGenerator::genCallCheckClosure(const CallExpr& e, FuncContext& ctx){
    if(const auto* ident = std::get_if<IdentExpr>(&e.callee->var)){
        if(!m_funcLabels.count(ident->name) && ctx.findLocal(ident->name)){
            return true; //если не FuncLabel и есть в лкоальных - замыкание
        }
    } else {
        return false; //выражение всегда замыкание
    }

    return false; //выражение - всегда замыкание
}

void CodeGenerator::genCallLoadArgs(const std::vector<int>& argOffsets){
    
    for(int i = static_cast<int>(argOffsets.size()) - 1; i >= 6; i--){
            emit("push qword [rbp" + std::to_string(argOffsets[i]) + "]");
        }

    //первые 6 в регистры
    for(int i = 0; i < static_cast<int>(argOffsets.size()) && i < 6; i++){
        emit("mov " + std::string(argReg(i)) +
            ", [rbp" + std::to_string(argOffsets[i]) + "]");
    }
}

void CodeGenerator::genCallCleanup(const std::vector<int>& argOffsets, FuncContext& ctx){
    
    int stackArgs = static_cast<int>(argOffsets.size()) - 6;
    if(stackArgs > 0){
        emit("add rsp, " + std::to_string(stackArgs * 8));
    }

    for(int i = 0; i < static_cast<int>(argOffsets.size()); ++i){
        ctx.removeLocal("__arg");
    }
}


/*
*вспомогательные функции для частичного применения
*/

bool CodeGenerator::isPartialCall(const ExprNode& e){
    auto it = m_exprTypes.find(&e);
    if(it == m_exprTypes.end()) return false;
     if(!std::get_if<FunctionType>(&it->second->var)) return false;

    //доп проыерка callee - сам может быть FuncType
    //число переданных аргументов меньше числа параметров

    const CallExpr* call = std::get_if<CallExpr>(&e.var);
    if(!call) return false;
    
    auto calleeIt = m_exprTypes.find(call->callee.get());
    if(calleeIt == m_exprTypes.end()) return false;
    
    int paramCount = 0; //сколько параметров у callee
    auto t = calleeIt->second;
    while(auto* ft = std::get_if<FunctionType>(&t->var)){ //пропускаем unit -> X без параметров
        if(auto* bt = std::get_if<BuiltinType>(&ft->from->var)){
            if(bt->name == "unit") break;
        }
        paramCount++;
        t = ft->to;
    }
    
    return static_cast<int>(call->args.size()) < paramCount; //только если параметров меньше
}

//либо имя фукции - либо переменная или выражения и тогда указатель в rax
std::string CodeGenerator::getCalleeFuncLabel(const CallExpr& e, FuncContext& ctx){
    if(const auto* ident = std::get_if<IdentExpr>(&e.callee->var)){ //callee - идентификатор
        auto it = m_funcLabels.find(ident->name);
        if(it != m_funcLabels.end()){
            return it->second;
        }
        auto off = ctx.findLocal(ident->name); //локальная переменная (замыкание)
        if(off){
            emit("mov rax, [rbp" + std::to_string(*off) + "]"); //closure_ptr из стека
        }
        return "";
    }
    genExpr(*e.callee, ctx); //Call - genFunc()(5) 
    return "";
}

std::string CodeGenerator::genPartialWrapLambda(const std::string& funcLabel, int capturedCount){
    std::string wrapLabel = freshLambdaLabel(); // уникальный label: __lambda_N

    int raw = (capturedCount + 2) * 8;
    int stackSize = ((raw + 15) / 16) * 16;

    emitPartialWrapSetup(wrapLabel, stackSize, capturedCount);

    if(!funcLabel.empty()){
        m_lambdas << "    call " << funcLabel << "\n";
    } else {
        emitPartialWrapClosureCall(wrapLabel, capturedCount);
    }

    m_lambdas << "    mov rsp, rbp\n";
    m_lambdas << "    pop rbp\n";
    m_lambdas << "    ret\n\n";

    return wrapLabel;
}

//+
//к genPartialWrapLambda
//две вспомогательные для функции где настраивается стек и для самого тела

void CodeGenerator::emitPartialWrapSetup(const std::string& wrapLabel, int stackSize, int capturedCount){
    
    m_lambdas << wrapLabel << ":\n";
    m_lambdas << "    push rbp\n";
    m_lambdas << "    mov rbp, rsp\n";
    m_lambdas << "    sub rsp, " << stackSize << "\n";
    m_lambdas << "    mov [rbp-8], rdi\n";
    m_lambdas << "    mov [rbp-16], rsi\n";

    //кладем захваченные на стек
    for(int i = 0; i < capturedCount; i++){
        m_lambdas << "    mov rax, [rbp-8]\n";
        m_lambdas << "    mov rcx, [rax + " << (i * 8) << "]\n";
        m_lambdas << "    mov [rbp-" << (24 + i * 8) << "], rcx\n";
    }

    //сначала в кладем захваченные, так как //fn add(x : int64, ...) - x первый аргумент
    for(int i = 0; i < capturedCount && i < 6; i++){
        m_lambdas << "    mov " << argReg(i) << ", [rbp-" << (24 + i * 8) << "]\n";
    }

    if(capturedCount < 6){
        m_lambdas << "    mov " << argReg(capturedCount) << ", [rbp-16]\n";
    } else {
        //на стек - в обратном порядке через call
        m_lambdas << "    mov rax, [rbp-16]\n";
        m_lambdas << "    push rax\n";
    }
}

void CodeGenerator::emitPartialWrapClosureCall(const std::string& wrapLabel, int capturedCount){ 
    m_lambdas << "    mov rax, [rbp-8]\n"; //наш env_ptr
    m_lambdas << "    mov rax, [rax + 0]\n"; //замыкание оригинала - первый слот env
    m_lambdas << "    mov r11, [rax]\n"; //code_ptr оригинала
    m_lambdas << "    mov rcx, [rax + 8]\n"; //env_ptr оригинала в rdi

    for(int i = 0; i < capturedCount; i++){
        m_lambdas << "    mov rax, [rbp-8]\n";
        m_lambdas << "    mov rax, [rax + " << (8 + i * 8) << "]\n";
        m_lambdas << "    mov [rbp-" << (24 + i * 8) << "], rax\n";
    }

    for(int i = 0; i < capturedCount && i < 6; i++){
        m_lambdas << "    mov " << argReg(i) << ", [rbp-" << (24 + i * 8) << "]\n";
    }

    if(capturedCount < 6){
        m_lambdas << "    mov " << argReg(capturedCount) << ", [rbp-16]\n";
    }

    m_lambdas << "    test rcx, rcx\n";
    m_lambdas << "    jz .call_direct_" << wrapLabel << "\n";

    m_lambdas << "    mov rdi, rcx\n";
    m_lambdas << ".call_direct_" << wrapLabel << ":\n";
    m_lambdas << "    call r11\n";
}



//замыкание {code_ptr, func_ptr, arg1, ...}
void CodeGenerator::genPartialClosure(const std::string& wrapLabel, const std::string& funcLabel,
    const std::vector<int>& argOffsets, FuncContext& ctx){

    if(funcLabel.empty()){
        genPartialClosureUnknown(wrapLabel, argOffsets, ctx);
    } else {
        genPartialClosureKnown(wrapLabel, funcLabel, argOffsets, ctx);
    }
}

//+ 2 вспомогательные функции

//когда знаем метку функции
void CodeGenerator::genPartialClosureKnown(const std::string& wrapLabel, const std::string& funcLabel,
    const std::vector<int>& argOffsets, FuncContext& ctx){

        int envSize = 8 * static_cast<int>(argOffsets.size());
        emit("mov rdi, " + std::to_string(envSize));
        emit("call __lang_malloc");
        int envOff = ctx.allocLocal("__partial_env");
        emit("mov [rbp" + std::to_string(envOff) + "], rax");

        for(int i = 0; i < static_cast<int>(argOffsets.size()); i++){
            emit("mov rax, [rbp" + std::to_string(envOff) + "]");
            emit("mov rcx, [rbp" + std::to_string(argOffsets[i]) + "]");
            emit("mov [rax + " + std::to_string(i * 8) + "], rcx");
        }

        emit("mov rdi, 16");
        emit("call __lang_malloc");
        int ptrOff = ctx.allocLocal("__partial_ptr");
        emit("mov [rbp" + std::to_string(ptrOff) + "], rax");

        emit("mov rcx, " + wrapLabel);
        emit("mov [rax], rcx");

        emit("mov rcx, [rbp" + std::to_string(envOff) + "]");
        emit("mov [rax + 8], rcx");

        emit("mov rax, [rbp" + std::to_string(ptrOff) + "]");
        ctx.removeLocal("__partial_ptr");
        ctx.removeLocal("__partial_env");
}

//когда не знаем метку, то замыкание
void CodeGenerator::genPartialClosureUnknown(const std::string& wrapLabel,
    const std::vector<int>& argOffsets, FuncContext& ctx){

        int savedOff = ctx.allocLocal("__orig_func_ptr");
        emit("mov [rbp" + std::to_string(savedOff) + "], rax");

        int envSize = 8 + 8 * static_cast<int>(argOffsets.size()); //env отдельно аргументов
        emit("mov rdi, " + std::to_string(envSize));
        emit("call __lang_malloc");
        int envOff = ctx.allocLocal("__partial_env");
        emit("mov [rbp" + std::to_string(envOff) + "], rax"); //env_ptr

        emit("mov rcx, [rbp" + std::to_string(savedOff) + "]");
        emit("mov [rax], rcx");

        for(int i = 0; i < static_cast<int>(argOffsets.size()); i++){ //аргументы в env
            emit("mov rax, [rbp" + std::to_string(envOff) + "]");
            emit("mov rcx, [rbp" + std::to_string(argOffsets[i]) + "]");
            emit("mov [rax + " + std::to_string(8 + i * 8) + "], rcx");
        }

        emit("mov rdi, 16"); //{code_ptr, env_ptr}
        emit("call __lang_malloc");
        int ptrOff = ctx.allocLocal("__partial_ptr");
        emit("mov [rbp" + std::to_string(ptrOff) + "], rax");

        emit("mov rcx, " + wrapLabel); // code_ptr = wrapLabel
        emit("mov [rax], rcx");

        emit("mov rcx, [rbp" + std::to_string(envOff) + "]"); //env_ptr теперь указатель на env
        emit("mov [rax + 8], rcx");

        emit("mov rax, [rbp" + std::to_string(ptrOff) + "]");
        ctx.removeLocal("__partial_ptr");
        ctx.removeLocal("__partial_env");
        ctx.removeLocal("__orig_func_ptr");
}



void CodeGenerator::genPartialApply(const CallExpr& e, const std::vector<int>&
    argOffsets, FuncContext& ctx){

        //label оригинальной функции
        std::string funcLabel = getCalleeFuncLabel(e, ctx);

        //лямбда обертка - вызываем оригинал, сколько аргументов захвачено
        std::string wrapLabel = genPartialWrapLambda(funcLabel, static_cast<int>(argOffsets.size()));

        //создаем замыкание в куче {code_ptr, [func_ptr], arg1, ...}
        genPartialClosure(wrapLabel, funcLabel, argOffsets, ctx); //rax = closure_ptr
    }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


//If
void CodeGenerator::genIf(const IfExpr& e, FuncContext& ctx){
    std::string elseLabel = freshLabel("else");
    std::string endLabel = freshLabel("endif");

    genExpr(*e.cond, ctx); //условие rax
    emit("cmp rax, 0");
    emit("jz " + elseLabel);

    genExpr(*e.thenBranch, ctx); //ветка then
    emit("jmp " + endLabel);

    emitLabel(elseLabel);
    genExpr(*e.elseBranch, ctx);

    emitLabel(endLabel); //после выполнения нужно ветки
}

//LetIn
void CodeGenerator::genLetIn(const LetInExpr& e, FuncContext& ctx){
    std::vector<std::string> boundNames; //let x, y = ... (x and y)
    
    for(const auto& binding : e.bindings){
        genExpr(*binding.value, ctx);
        int off = ctx.allocLocal(binding.name);
        emit("mov [rbp" + std::to_string(off) + "], rax"); //локальную ячейку
        boundNames.push_back(binding.name);
    }

    genExpr(*e.body, ctx); //обрабатываем тело

    //убираем связанные имена из контекста
    for(const auto& name : boundNames){
        ctx.removeLocal(name);
    }
}

//Tuple {int64 count; int64 elems[count]} - куча
void CodeGenerator::genTuple(const TupleExpr& e, FuncContext& ctx){
    std::size_t n = e.elems.size();
    std::size_t size = 8 + n * 8;

    std::vector<int> offsets; //сдвиги временных переменных на стеке

    for(const auto& elem : e.elems){
        genExpr(*elem, ctx);
        int offset = ctx.allocLocal("__telem");

        emit("mov [rbp" + std::to_string(offset) + "], rax");
        offsets.push_back(offset); //потом достанем со стека
    }

    emitAlloc(size); //rax указатель на кортеж
    emit("mov qword [rax], " + std::to_string(n)); //count

    int ptrOff = ctx.allocLocal("__tuple_ptr");    //указатель на кортеж
    emit("mov [rbp" + std::to_string(ptrOff) + "], rax");

    //теперь будем загружать элементы в кортеж
    for(std::size_t i = 0; i < n; i++){
        emit("mov rcx, [rbp" + std::to_string(offsets[i]) + "]");
        emit("mov rax, [rbp" + std::to_string(ptrOff) + "]");
        emit("mov [rax + " + std::to_string(8 + i*8) + "], rcx");
    }

    emit("mov rax, [rbp" + std::to_string(ptrOff) + "]"); //указатель на кортеж
    ctx.removeLocal("__tuple_ptr");

    for(int i = 0; i < n; i++) ctx.removeLocal("__telems"); //удаляем временные имена элементов из контекста
}



//со списками интереснее, список может быть двух типов пустым Nil и непустым cons(head, tail)
//у пустого тег - 0, у непустого тег 1, чтобы в паттернс мэтчинг различать
//любой список заканчивается Nil, Cons(2, Cons(3, Nil)) - у последнего элемента тоже должен быть хвост - дальше списка нет

//cons = {tag = 1; int64 head; ptr tail} | Nil = {tag = 0}
void CodeGenerator::genList(const ListExpr& e, FuncContext& ctx){
    emitAlloc(8); //Nil
    emit("mov qword [rax], 0");

    int listOff = ctx.allocLocal("__list");
    emit("mov [rbp" + std::to_string(listOff) + "], rax");

    for(int i = static_cast<int>(e.elems.size()) - 1; i >= 0; i--){ 
        genExpr(*e.elems[i], ctx);
        int elemOff = ctx.allocLocal("__lelem");
        emit("mov [rbp" + std::to_string(elemOff) + "], rax"); 

        //Cons {tag = 1, head, tail}
        emitAlloc(24); //ptr rax
        emit("mov qword [rax], 1");

        emit("mov rcx, [rbp" + std::to_string(elemOff) + "]"); //сохраненный элемент
        emit("mov [rax + 8], rcx");

        emit("mov rcx, [rbp" + std::to_string(listOff) + "]"); //текущий список это хвост
        emit("mov [rax + 16], rcx");

        emit("mov [rbp" + std::to_string(listOff) + "], rax"); //текущий список начинается с нового cons

        ctx.removeLocal("__lelem");
    }
    
    emit("mov rax, [rbp" + std::to_string(listOff) + "]"); //ptr list = rax

    ctx.removeLocal("__list");
}


//ADT constructor { int64 tag; int64 fields[]} //tag конструктора - его индекс - генерация похожа на tuple
void CodeGenerator::genConstructor(const ConstructorExpr& e, FuncContext& ctx){
    std::size_t n = e.args.size();
    std::size_t size = 8 + n * 8;

    std::vector<int> offsets;

    for(const auto& arg : e.args){
        genExpr(*arg, ctx);
        int offset = ctx.allocLocal("__carg");
        emit("mov [rbp" + std::to_string(offset) + "], rax");
        offsets.push_back(offset);
    }

    emitAlloc(size);

    int tag = getConstructorTag(e.name);
    emit("mov qword [rax], " + std::to_string(tag));

    int ptrOff = ctx.allocLocal("__ctor_ptr");
    emit("mov [rbp" + std::to_string(ptrOff) + "], rax");

    for(std::size_t i = 0; i < n; ++i){
        emit("mov rcx, [rbp" + std::to_string(offsets[i]) + "]");
        emit("mov rax, [rbp" + std::to_string(ptrOff) + "]");
        emit("mov [rax + " + std::to_string(8 + i * 8) + "], rcx");
    }

    emit("mov rax, [rbp" + std::to_string(ptrOff) + "]"); //указатель на конструктор
    ctx.removeLocal("__ctor_ptr");

    for(int i = 0; i < n; i++) ctx.removeLocal("__carg"); //удаляем временные имена элементов из контекста
}


//field access || модуль либо же именнованный конструкторы
void CodeGenerator::genFieldAccess(const FieldAccessExpr& e, FuncContext& ctx){

    if(const auto* ident = std::get_if<IdentExpr>(&e.object->var)){
        if(genModuleFieldAccess(*ident, e)) return;
    }

    //вычислили объект, ищем его в полях конструктора -> gen mov rax, [rax + 8]
    genExpr(*e.object, ctx);
    genNamedCtorFieldAccess(e);
}

//вспомогательные
bool CodeGenerator::genModuleFieldAccess(const IdentExpr& ident, const FieldAccessExpr& e){
    std::string fullName = ident.name + "." + e.field;

    auto it = m_funcLabels.find(fullName); //ищем полное описанное имя среди сгенерированных функций
    if(it != m_funcLabels.end()) return false;
    emit("mov rax, " + it->second); //адрес функции
    return true;
}

void CodeGenerator::genNamedCtorFieldAccess(const FieldAccessExpr& e){ 
    auto it = m_exprTypes.find(e.object.get());
    if(it == m_exprTypes.end()) return;

    const auto* st = std::get_if<SimpleType>(&it->second->var);
    if(!st) return;
        
    auto dataInfo = m_registry.lookupData(st->name); //находим ADT
    if(!dataInfo) return;
        
    for(const auto& ctor : dataInfo->constructors){ //поле во всех ctors
        for(std::size_t i = 0; i < ctor.fieldNames.size(); ++i){
            if(ctor.fieldNames[i] == e.field){
                emit("mov rax, [rax + " + 
                    std::to_string(8 + i * 8) + "]"); //tag + field0 + ...
                return;
            }
        }
    }
}


//cons - {tag, head, tail}
void CodeGenerator::genCons(const ConsExpr& e, FuncContext& ctx){

    genExpr(*e.head, ctx); //head
    int headOff = ctx.allocLocal("__cons_head");
    emit("mov [rbp" + std::to_string(headOff) + "], rax");

    genExpr(*e.tail, ctx); //tail
    int tailOff = ctx.allocLocal("__cons_tail");
    emit("mov [rbp" + std::to_string(tailOff) + "], rax");

    emitAlloc(24);
    emit("mov qword [rax], 1"); // tag = 1 (Cons)
    emit("mov rcx, [rbp" + std::to_string(headOff) + "]");
    emit("mov [rax + 8], rcx");
    emit("mov rcx, [rbp" + std::to_string(tailOff) + "]");
    emit("mov [rax + 16], rcx");

    ctx.removeLocal("__cons_head");
    ctx.removeLocal("__cons_tail");
}
    
}