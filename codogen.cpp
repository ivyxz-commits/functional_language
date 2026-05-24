#include "codogen.hpp"

namespace Codegen {

CodeGenerator::CodeGenerator(std::string filename, const TypeRegistry& registry)
    :m_filename(std::move(filename)), m_registry(registry) {}


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
    m_text.str(""); m_data.str(""); m_bss.str(""); //внутренняя строка пустая

    m_data << "section .data\n";
    m_bss << "section .bss\n";
    m_bss << "    __read_buf: resb 4096\n"; //read_str - input() - пустая память, не имеет значения - ОС выделяет память при запуске
    m_bss << "    __print_int_buf: resb 24\n"; //буфер для печати целого числа Int - 19 чисел

    m_text << "section .text\n";
    m_text << "global _start\n\n"; //without libc

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
    m_text << "    and rsp, -16"; //выравнивание стека без mov rbp, rsp - фрейм не надо сохранять - область использующаяся одной функцикй
    m_text << "    call __fn_main\n"; //push rip + 5 адрес следующей инструкции на стек jmp __fn_main
    m_text << "    mov rdi, rax \n"; 
    m_text << "    mov rax, 60\n"; //syscall exit - код завершения в rdi
    m_text << "    syscall\n\n";

    return m_data.str() + "\n" + m_bss.str() + "\n" + m_text.str();
}

//utilities
std::string CodeGenerator::freshLabel(const std::string& label){ 
    return "." + label + "_" + std::to_string(m_labelCnt++); //.else_0
}

std::string CodeGenerator::freshStrLabel(){
    return "__str_" + std::to_string(m_strCnt++);
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
    m_text << label << ":\n";
}



//ABI

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

//положить на стек и взять с него
int CodeGenerator::pushToStack(FuncContext& ctx, const std::string& label = "__tmp"){
    int offset = ctx.allocLocal(label); //выделили место на стеке и получили offset //+ locals | +nextOffset 
    emit("mov [rbp" + std::to_string(offset) + "], rax");
    return offset;
}

int CodeGenerator::loadFromStack(int offset, const std::string& destReg = "rax"){
    emit("mov" + destReg + ", [rbp" + std::to_string(offset) + "]");
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

//завершение программы с кодом возврата
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

//парсинг интов и реализацию вещественных чисел буду прописывать позже
//плюсом еще добавим аллокатор для грамотной работы с памятью


//начинаем реализовать основу, остальные отдельные моменты будут прописаны позже
//Declarations
void CodeGenerator::genDecl(const DeclNode& decl){
    if(const auto* fn = std::get_if<FuncDecl>(&decl.var)){
        genFuncDecl(*fn);
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

    for(int i; i < fn.params.size() && i < 6; i++){
        int off = ctx.allocLocal(fn.params[i].name); //nextOffset -=8
        emit("mov [rbp" + std::to_string(off) + "], " + std::string(argReg(i))); //вызывающая функция позаботиться
    }

    for(int i = 6; i < fn.params.size(); i++){
        int stackArgOffset = 16 + (i - 6) * 8;

        int off = ctx.allocLocal(fn.params[i].name);
        emit("mov [rbp" + std::to_string(stackArgOffset) + "]");

        emit("mov [rbp" + std::to_string(off) + "], rax");
    }

    genExpr(*fn.body, ctx); //выражения, результат в rax

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

//expressions
void CodeGenerator::genExpr(const ExprNode& expr, FuncContext& ctx){

    if(const auto* e = std::get_if<LiteralExpr>(&expr.var)){
        genLiteral(*e, ctx);
    }
    
    else if (const auto* e = std::get_if<IdentExpr>(&expr.var)){
        genIdent(*e, ctx);
    } 
    
    else if (const auto* e = std::get_if<UnaryExpr>(&expr.var)){
        genUnary(*e, ctx);
    }
    
    else if (const auto* e = std::get_if<BinaryExpr>(&expr.var)){
        genBinary(*e, ctx);
    }

    else if (const auto* e = std::get_if<CallExpr>(&expr.var)){
        genCall(*e, ctx);
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

    else if (const auto* e = std::get_if<ConstructorExpr>(&expr.var)){
        genConstructor(*e, ctx);
    }

    else if (const auto* e = std::get_if<FieldAccessExpr>(&expr.var)){
        genFieldAccess(*e, ctx);
    }
}





}