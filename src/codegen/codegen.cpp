#include "codegen.hpp"

/*
Логика линейная, каждая инструкция - один шаг генерации ассемблера
Дробить на маленькие части нет смысла - контекст при чтении теряется
и становится сложнее понять, что генерируется
*/


namespace Codegen {


//конструктор класса
CodeGenerator::CodeGenerator(const TypeRegistry& registry, 
    const std::unordered_map<const ExprNode*, sPtr<TypeInfo>>& exprTypes, 
    const std::unordered_map<const CallExpr*, 
    std::unordered_map<std::string, sPtr<TypeInfo>>>& callTypeMaps,
    std::string filename)

    :m_registry(registry),
    m_exprTypes(exprTypes), 
    m_callTypeMaps(callTypeMaps),
    m_filename(std::move(filename)){}



//получение тега конструктора ADT
int CodeGenerator::getConstructorTag(const std::string& ctorName) const{
    auto ctorInfo = m_registry.lookupConstructor(ctorName);
    if(ctorInfo){
        auto dataInfo = m_registry.lookupData(ctorInfo->dataName); //находим тип ADT конструктора
        if(dataInfo){ 
            for(std::size_t i = 0; i < dataInfo->constructors.size(); i++){
                if(dataInfo->constructors[i].name == ctorName){
                    return i; //тег, как прописал выше инжекс конструктора
                }
            }
        }
    }

    throw std::runtime_error("codegen internal error: constructor '" + ctorName + "' not found in registry"); //не должны дойти - семанатика уже сработала
}


//точка входа
std::string CodeGenerator::generate(const Program& prog){
    m_text.str(""); m_data.str(""); m_bss.str(""); m_lambdas.str(""); //внутренняя строка пустая

    m_data << "section .data\n";
    m_data << "__div_zero_len: dq 16\n"; //метка длины 8 байт, значение 16 {length, data[]}
    m_data << "__div_zero_dat: db `division by zero`, 0\n"; //строка + нулевой байт

    m_bss << "section .bss\n";
    m_bss << "    __read_buf: resb 4096\n"; //read_str - input() - пустая память, не имеет значения - ОС выделяет память при запуске
    m_bss << "    __print_int_buf: resb 24\n"; //буфер для печати целого числа Int - 19 чисел

    m_text << "section .note.GNU-stack noalloc noexec nowrite progbits\n";
    m_text << "section .text\n";
    m_text << "global _start\n\n";
    emitFunctionsExterns();
    m_text << "\n";

    //объявления высшего уровня
    for(const auto& decl : prog.decls){
        if(const auto *fn = std::get_if<FuncDecl>(&decl->var)){
            m_funcLabels[fn->name] = "__fn_" + fn->name;
        }
    }

    //генерируем рантайм - записываем в выходной асм файл асемблерный код вспомогательных функций
    emitRuntime();

    for(const auto& decl : prog.decls){ //Ptr
        genDecl(*decl);
    }

    m_text << "_start:\n";
    m_text << "    and rsp, -16\n"; //выравнивание стека без mov rbp, rsp - фрейм не надо сохранять - область использующаяся одной функцикй
    m_text << "    call __fn_main\n"; //push rip + 5 адрес следующей инструкции на стек jmp __fn_main
    m_text << "    mov rdi, rax \n"; 
    m_text << "    mov rax, 60\n"; //syscall exit - код завершения в rdi
    m_text << "    syscall\n\n";

    return m_data.str() + "\n" + m_bss.str() + "\n" + m_text.str() + m_lambdas.str() + m_generics.str();
}

//utilities
std::string CodeGenerator::freshLabel(const std::string& label){ 
    return "." + label + "_" + std::to_string(m_labelCnt++); //.else_0
}

std::string CodeGenerator::freshStrLabel(){
    return "__str_" + std::to_string(m_strCnt++);
}

std::string CodeGenerator::freshLambdaLabel(){
    return "__lambda_" + std::to_string(m_labelCnt++);
}

void CodeGenerator::emit(const std::string& line){ //to section.text
    m_text << "    " << line << "\n";
}

void CodeGenerator::emitData(const std::string& line){
    m_data << "    " << line << "\n";
}

void CodeGenerator::emitLabel(const std::string& label){
    m_text << label << ":\n";
}

void CodeGenerator::emitDataLabel(const std::string& label){
    m_data << label << ":\n";
}



//ABI

//аргументы в регистр
const char* CodeGenerator::argReg(int i){
    static const char* regs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
    if(i < 6) return regs[i];
    return nullptr;
}

void CodeGenerator::emitAlloc(int size){ 
    int aligned = size;
    if(aligned % 8 != 0){
        aligned = aligned + (8 - aligned % 8);
    }

    emit("mov rdi, " + std::to_string(aligned));
    emit("call __lang_malloc"); //lang_malloc руками написан
}


//вспомогательные функции isFloat, isString - в genPattern musthave
bool CodeGenerator::isFloatExpr(const ExprNode& e) const{
    auto it = m_exprTypes.find(&e);
    if(it == m_exprTypes.end()) return false;
    
    //для generic
    auto type = it->second;
    if(const auto* st = std::get_if<SimpleType>(&type->var)){ //только если st
        auto it2 = m_currentTypeVarMap.find(st->name);
        if(it2 != m_currentTypeVarMap.end()) type = it2 -> second;
    }

    const auto *bt = std::get_if<BuiltinType>(&it->second->var);
    return bt && bt ->name == "float64";
}

bool CodeGenerator::isStringExpr(const ExprNode& e) const{
    auto it = m_exprTypes.find(&e);
    if(it == m_exprTypes.end()) return false;

    //для generic
    auto type = it->second;
    if(const auto* st = std::get_if<SimpleType>(&type->var)){ //только если st
        auto it2 = m_currentTypeVarMap.find(st->name);
        if(it2 != m_currentTypeVarMap.end()) type = it2 -> second;
    }
    
    const auto *bt = std::get_if<BuiltinType>(&it->second->var);
    return bt && bt ->name == "string";
}

void CodeGenerator::emitFunctionsExterns(){
    m_text << "extern lang_print_float\n";
    m_text << "extern lang_parse_float\n";
    m_text << "extern lang_parse_int\n";
    m_text << "extern lang_str_eq\n";
    m_text << "\n";
}


//runtime 
void CodeGenerator::emitRuntime(){ 
    emitMalloc();
    emitPrintInt();
    emitPrintString();
    emitReadString();
    emitPanic();
    emitExit();
}

//завершение (нормальное) программы с кодом возврата
void CodeGenerator::emitExit(){
    m_text << "__lang_exit:\n";
    m_text << "    mov rax, 60\n"; //ядро линукс уничтожает процесс и всю его память
    m_text << "    syscall\n\n";
}

//аварийное завершение с сообщением об ошибке
void CodeGenerator::emitPanic(){
    m_text << "__lang_panic:\n";
    m_text << "    push rbp\n";
    m_text << "    mov rbp, rsp\n";
    m_text << "    call __lang_print_str\n"; //печатаем нашу ошибку
    m_text << "    mov rdi, 1\n"; //код ошибки, что-то пошло не так
    m_text << "    mov rax, 60\n";
    m_text << "    syscall\n\n";
}

//rdi: str* -> unit
//str* = {int64 length; char data[]}
void CodeGenerator::emitPrintString(){
    m_text << "__lang_print_str:\n";
    m_text << "    push rbp\n";
    m_text << "    mov rbp, rsp\n";
    m_text << "    push rbx\n"; //saved
    m_text << "\n";
    m_text << "    mov rbx, rdi\n"; //указатель на строку
    m_text << "    mov rdx, [rbx]\n"; //длина
    m_text << "    lea rsi, [rbx + 8]\n"; //сами данные
    m_text << "    mov rax, 1\n"; //syscall write
    m_text << "    mov rdi, 1\n"; //stdout
    m_text << "    syscall\n";
    m_text << "\n";
    m_text << "    pop rbx\n";
    m_text << "    pop rbp\n";
    m_text << "    ret\n\n";
}

//чтение строки из stdin возвращает rax
void CodeGenerator::emitReadString(){
    m_text << "__lang_read_str:\n";
    m_text << "    push rbp\n";
    m_text << "    mov rbp, rsp\n";
    m_text << "    push rbx\n";
    m_text << "\n";
    m_text << "    mov rax, 0\n"; //syscall read
    m_text << "    mov rdi, 0\n";
    m_text << "    mov rsi, __read_buf\n"; // буфер
    m_text << "    mov rdx, 4095\n"; // \0
    m_text << "    syscall\n";
    m_text << "    mov rbx, rax\n"; // rbx = прочитано байт
    m_text << "\n";
    m_text << "    ; убираем завершающий '\\n' если есть\n";
    m_text << "    cmp rbx, 0\n";
    m_text << "    jz .rstr_alloc\n";
    m_text << "    mov rcx, __read_buf\n";
    m_text << "    add rcx, rbx\n";
    m_text << "    dec rcx\n"; // rcx = адрес последнего байта
    m_text << "    cmp byte [rcx], 10\n";
    m_text << "    jne .rstr_alloc\n";
    m_text << "    dec rbx\n";
    m_text << "\n";
    m_text << ".rstr_alloc:\n";
    m_text << "    mov rdi, rbx\n";
    m_text << "    add rdi, 8\n"; // size = длина + 8 байт для length
    m_text << "    call __lang_malloc\n"; //указатель на память в rax = ptr
    m_text << "    mov [rax], rbx\n"; // записываем длину
    m_text << "\n";
    m_text << "    mov rdi, rax\n";
    m_text << "    add rdi, 8\n"; //то, что ввел пользователь
    m_text << "    mov rsi, __read_buf\n";
    m_text << "    mov rcx, rbx\n";
    m_text << "    rep movsb\n"; //копируем rcx байт из rsi to rdi
    m_text << "\n";
    m_text << "    pop rbx\n";
    m_text << "    pop rbp\n";
    m_text << "    ret\n\n";
}

//выделение памяти через malloc - mmap
//проецируем адрес физической памяти напрямую в виртуальное адресное пространство процесса
void CodeGenerator::emitMalloc(){
    //сообщение об ошибке в .data
    m_data << "__malloc_err_len: dq 20\n";
    m_data << "__malloc_err_dat: db `out of memory error`, 0\n";

    m_text << "__lang_malloc:\n";
    m_text << "    push rbp\n";
    m_text << "    mov rbp, rsp\n";
    m_text << "    add rdi, 7\n";  //в большую сторону
    m_text << "    and rdi, -8\n";
    m_text << "    mov rsi, rdi\n";
    m_text << "    mov rdi, 0\n"; //ОС сама решит куда выделять
    m_text << "    mov rdx, 3\n"; //читать, писать
    m_text << "    mov r10, 34\n"; // MAP_PRIVATE | MAP_ANONYMOUS
    m_text << "    mov r8,  -1\n";   //fd не нужен
    m_text << "    mov r9,  0\n";    //offset = 0
    m_text << "    mov rax, 9\n";    //syscall mmap
    m_text << "    syscall\n";
    m_text << "    cmp rax, -1\n";   // проверяем MAP_FAILED
    m_text << "    jne .malloc_ok\n";
    m_text << "    ; ошибка - выводим сообщение и завершаем\n";
    m_text << "    mov rdi, __malloc_err_len\n";
    m_text << "    call __lang_panic\n";
    m_text << ".malloc_ok:\n";
    m_text << "    pop rbp\n";
    m_text << "    ret\n\n";
}

//вывод целого числа
void CodeGenerator::emitPrintInt(){
    m_text << "__lang_print_int:\n";
    m_text << "    push rbp\n";
    m_text << "    mov rbp, rsp\n";
    m_text << "    push rbx\n";
    m_text << "    push r12\n";
    m_text << "    push r13\n";
    m_text << "\n";
    m_text << "    mov rax, rdi\n"; //число в rax
    m_text << "    mov r12, __print_int_buf\n";  //начало буфера
    m_text << "    add r12, 23\n";  // r12 = конец буфера
    m_text << "    mov byte [r12], 10\n";  //'\n' в конец
    m_text << "    dec r12\n";
    m_text << "    mov r13, 0\n"; //флаг отрицательности
    m_text << "\n";
    m_text << "    cmp rax, 0\n";
    m_text << "    jns .pint_pos\n";
    m_text << "    mov r13, 1\n";
    m_text << "    neg rax\n";
    m_text << ".pint_pos:\n";
    m_text << "    mov rbx, 10\n"; //делим на 10
    m_text << ".pint_loop:\n";
    m_text << "    xor rdx, rdx\n";
    m_text << "    div rbx\n"; //rdx:rax 128 битное число 0:123 на 10, остаток в rdx
    m_text << "    add dl, '0'\n"; //цифра в ASCII
    m_text << "    mov [r12], dl\n";
    m_text << "    dec r12\n"; //если цифр уже нет резервиурем место под минус, потом уберем если его нет
    m_text << "    cmp rax, 0\n";
    m_text << "    jnz .pint_loop\n";
    m_text << "\n";
    m_text << "    cmp r13, 0\n";
    m_text << "    jz .pint_nosign\n";
    m_text << "    mov byte [r12], '-'\n"; //добавляем минус
    m_text << "    dec r12\n"; //чтобы минус не пропал
    m_text << ".pint_nosign:\n";
    m_text << "    inc r12\n"; //начало строки
    m_text << "    mov rcx, __print_int_buf\n";
    m_text << "    add rcx, 24\n"; // rcx = конец буфера
    m_text << "    sub rcx, r12\n"; // длина строки, чтобы написали число с минусом или без
    m_text << "    mov rax, 1\n";
    m_text << "    mov rdi, 1\n";
    m_text << "    mov rsi, r12\n"; //адрес начало строки
    m_text << "    mov rdx, rcx\n"; //количество элементов
    m_text << "    syscall\n";
    m_text << "\n";
    m_text << "    pop r13\n";
    m_text << "    pop r12\n";
    m_text << "    pop rbx\n";
    m_text << "    pop rbp\n";
    m_text << "    ret\n\n";
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Declarations
void CodeGenerator::genDecl(const DeclNode& decl){
    if(const auto* fn = std::get_if<FuncDecl>(&decl.var)){
        if(!fn->typeParams.empty()){
            m_genericFuncs[fn->name] = fn; //gener -> decl функции
        } else {   
            genFuncDecl(*fn);
        }
    } else if(const auto* mod = std::get_if<ModuleDecl>(&decl.var)){ 
        genModuleDecl(*mod);
    }
    //TypeAliasDecl и DataDecl - типы и кода не генерируют
}



void CodeGenerator::genModuleDecl(const ModuleDecl& mod){
    for(const auto& decl : mod.decls){ //объявление может быть и сам модуль
        genDecl(*decl);
    }
}



//нужен точный размер стека - std::swap - based on move semantics //по факту один умный проход
void CodeGenerator::genFuncDecl(const FuncDecl& fn){
    FuncContext ctx;  //контекст текущей функции - локальные переменные, offsets, размеры стека
    ctx.name = fn.name;

    emitLabel("__fn_" + fn.name);
    emit("push rbp");
    emit("mov rbp, rsp");
    
    //сохраняем основной поток и генерируем тело во временный //через genExpr бы не смогли, он бы сразу генерировал код
    std::ostringstream bodyStream;
    std::swap(m_text, bodyStream); //m_text теперь пустой, а в bodyStream уже сохраняли стек и написали имя функции
    //пишем все в m_text

    genFuncParams(fn, ctx);
    genExpr(*fn.body, ctx); //выражения, результат в rax

    //print() in main
    if(fn.name == "main"){
        auto it = m_exprTypes.find(fn.body.get());
        if(it != m_exprTypes.end()){
            if(const auto* bt = std::get_if<BuiltinType>(&it->second->var)){
                if(bt->name == "unit") emit("xor rax, rax");
            }
        }
    }

    emit("mov rsp, rbp");
    emit("pop rbp");
    emit("ret");

    std::string body = m_text.str(); //сохраняем сгенерированное тело
    std::swap(m_text, bodyStream); //body stream содержит все, что содержалось в функции

    int stackSize = ctx.alignedStackSize();

    if(stackSize > 65536){ //64KB
        throw std::runtime_error(
            "codegen internal error: stack frame too large (" + 
            std::to_string(stackSize) + ") in function '" + fn.name + "'");
    }

    /* но все реализовано верно
    *if(stackSize < 0){
    *    throw std::runtime_error(
    *    "codegen internal error: negative stack size in function '" + fn.name + "'");   
    }
    */

    if(stackSize > 0){
        emit("sub rsp, " + std::to_string(stackSize));
    }

    m_text << body;
    m_text << "\n";
}

//вспомогательная
void CodeGenerator::genFuncParams(const FuncDecl& fn, FuncContext& ctx){
    for(std::size_t i = 0; i < fn.params.size() && i < 6; i++){
        int off = ctx.allocLocal(fn.params[i].name); //nextOffset -=8
        emit("mov [rbp" + std::to_string(off) + "], " + std::string(argReg(i))); //вызывающая функция позаботиться
    }

    for(std::size_t i = 6; i < fn.params.size(); i++){
        int stackArgOffset = 16 + (i - 6) * 8;

        int off = ctx.allocLocal(fn.params[i].name);
        emit("mov rax, [rbp" + std::to_string(stackArgOffset) + "]");

        emit("mov [rbp" + std::to_string(off) + "], rax");
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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



//Call - логика функций и лямбда
void CodeGenerator::genCall(const CallExpr& e,const ExprNode& node, FuncContext& ctx){

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

    //проверка частичного применения, если оно, то genPartAppl сам разберется
    if(isPartialCall(node)){
        genPartialApply(e, argOffsets, ctx);

        //убираем временные переменные из контекста
        for(std::size_t i = 0; i < argOffsets.size(); i++){
            ctx.removeLocal("__arg");
        }

        return;
    }

    bool isClosureCall = false;
    if(const auto* ident = std::get_if<IdentExpr>(&e.callee->var)){
        if(!m_funcLabels.count(ident->name) && ctx.findLocal(ident->name)){
            isClosureCall = true; //если не FuncLabel и есть в лкоальных - замыкание
        }
    } else {
        isClosureCall = true; //выражение всегда замыкание
    }

    //по ABI аргументы кладутся на стек в обратном порядке, чтобы первый элемент по меньшему адресу rbp
    //a -> [984], b -> [992] | a ближе к стеку => rbp + 16 = a
    if(!isClosureCall){
        for(int i = static_cast<int>(argOffsets.size()) - 1; i >= 6; i--){
            emit("push qword [rbp" + std::to_string(argOffsets[i]) + "]");
        }

        //первые 6 в регистры
        for(int i = 0; i < static_cast<int>(argOffsets.size()) && i < 6; i++){
            emit("mov " + std::string(argReg(i)) +
                ", [rbp" + std::to_string(argOffsets[i]) + "]");
        }
    }

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

    int stackArgs = static_cast<int>(argOffsets.size()) - 6;
    if(stackArgs > 0){
        emit("add rsp, " + std::to_string(stackArgs * 8));
    }

    for(int i = 0; i < static_cast<int>(argOffsets.size()); ++i){
        ctx.removeLocal("__arg");
    }
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

    if(!funcLabel.empty()){
        m_lambdas << "    call " << funcLabel << "\n";
    } else { //{env_ptr, code_ptr (lambda_wrap), closure_ptr(genFunc()), ...}
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

    m_lambdas << "    mov rsp, rbp\n";
    m_lambdas << "    pop rbp\n";
    m_lambdas << "    ret\n\n";

    return wrapLabel;
}

//замыкание {code_ptr, func_ptr, arg1, ...}
void CodeGenerator::genPartialClosure(const std::string& wrapLabel, const std::string& funcLabel,
    const std::vector<int>& argOffsets, FuncContext& ctx){

        int closureSize = 8 + 8 * static_cast<int>(argOffsets.size()); //адрес лямбда обертки - аргументы

        //нужно место под указатель на функцию
        if(funcLabel.empty()){
            int savedOff = ctx.allocLocal("__orig_func_ptr");
            emit("mov [rbp" + std::to_string(savedOff) + "], rax");

            int envSize = 8 + 8 * static_cast<int>(argOffsets.size());
            emit("mov rdi, " + std::to_string(envSize));
            emit("call __lang_malloc");
            int envOff = ctx.allocLocal("__partial_env");
            emit("mov [rbp" + std::to_string(envOff) + "], rax");

            emit("mov rcx, [rbp" + std::to_string(savedOff) + "]");
            emit("mov [rax], rcx");

            for(int i = 0; i < static_cast<int>(argOffsets.size()); i++){
                emit("mov rax, [rbp" + std::to_string(envOff) + "]");
                emit("mov rcx, [rbp" + std::to_string(argOffsets[i]) + "]");
                emit("mov [rax + " + std::to_string(8 + i * 8) + "], rcx");
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
            ctx.removeLocal("__orig_func_ptr");

        } else {
            int envSize = 8 * static_cast<int>(argOffsets.size()); //env отдельно аргументов
            emit("mov rdi, " + std::to_string(envSize));
            emit("call __lang_malloc");
            int envOff = ctx.allocLocal("__partial_env");
            emit("mov [rbp" + std::to_string(envOff) + "], rax"); //env_ptr

            for(int i = 0; i < static_cast<int>(argOffsets.size()); i++){ //аргументы в env
                emit("mov rax, [rbp" + std::to_string(envOff) + "]");
                emit("mov rcx, [rbp" + std::to_string(argOffsets[i]) + "]");
                emit("mov [rax + " + std::to_string(i * 8) + "], rcx");
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
        }


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


//вспомогательные функции для Generic

//преобразование типа в уникальную строку
std::string CodeGenerator::mangleTypeName(const sPtr<TypeInfo>& type){
    if(const auto* bt = std::get_if<BuiltinType>(&type->var)){
        return bt->name;
    }

    //тут как раз и получаем int64, float64, bool, string

    if(const auto* st = std::get_if<SimpleType>(&type->var)){
        return st->name;
    }

    if(const auto* gt = std::get_if<GenericType>(&type->var)){
        std::string s = gt->name; //"Option", [int64]
        for(const auto& arg : gt->args) s += "_" + mangleTypeName(arg);
        return s;
    }

    if(const auto* tt = std::get_if<TupleType>(&type->var)){
        std::string s = "tuple";
        for(const auto& e : tt->elems) s += "_" + mangleTypeName(e);
        return s;
    }

    if(const auto* lt = std::get_if<ListType>(&type->var)){
        return "list_" + mangleTypeName(lt->elem);
    }

    if(const auto* ft = std::get_if<FunctionType>(&type->var)){
        return mangleTypeName(ft->from) + "_to_" + mangleTypeName(ft->to);
    }
    
    return "unknown";
}

//создание уникальной метки - инстанцирование (мономорфизм)
void CodeGenerator::genGenericFuncDecl(const FuncDecl& fn, const std::string& label,
    const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap){

    m_currentTypeVarMap = typeVarMap; //typeVar для float и string

    FuncContext ctx;
    ctx.name = fn.name;

    std::ostringstream savedText;
    std::swap(m_text, savedText);

    emitLabel(label);
    emit("push rbp");
    emit("mov rbp, rsp");

    std::ostringstream bodyStream;
    std::swap(m_text, bodyStream);

    //вспомогательные для genFunc()
    genFuncParams(fn, ctx);
    genExpr(*fn.body, ctx);

    emit("mov rsp, rbp");
    emit("pop rbp");
    emit("ret");

    std::string body = m_text.str();
    std::swap(m_text, bodyStream);

    int stackSize = ctx.alignedStackSize();
    if(stackSize > 0){
        emit("sub rsp, " + std::to_string(stackSize));
    }
    m_text << body;
    m_text << "\n";

    std::string funcCode = m_text.str();
    std::swap(m_text, savedText);
    m_generics << funcCode;

    //сбрасываем typeVarMap
    m_currentTypeVarMap.clear();
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