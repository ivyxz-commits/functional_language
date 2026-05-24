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

//аварийное завершение с сооьщение об ошибке
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




}