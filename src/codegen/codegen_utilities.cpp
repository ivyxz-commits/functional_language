#include "codegen.hpp"


namespace Codegen{


//конструктор класса
CodeGenerator::CodeGenerator(const TypeRegistry& registry, 
    const std::unordered_map<const ExprNode*, sPtr<TypeInfo>>& exprTypes, 
    const std::unordered_map<const CallExpr*, 
    std::unordered_map<std::string, sPtr<TypeInfo>>>& callTypeMaps,
        const std::unordered_map<const CallExpr*, const FuncDecl*>& resolvedOverloads,
    std::string filename)

    :m_registry(registry),
    m_exprTypes(exprTypes), 
    m_callTypeMaps(callTypeMaps),
    m_resolvedOverloads(resolvedOverloads),
    m_filename(std::move(filename)){}


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

    throw std::runtime_error("codegen internal error: constructor '" + 
        ctorName + "' not found in registry"); //не должны дойти - семанатика уже сработала
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

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////s
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
    
}