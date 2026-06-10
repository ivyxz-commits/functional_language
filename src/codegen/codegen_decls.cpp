#include "codegen.hpp"


namespace Codegen{

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

//перегрузка по любым параметрам
static std::string getTypeNodeName(const TypeNode& node){
    if(const auto* bt = std::get_if<BuiltinTypeNode>(&node.var)) return bt->name;
    if(const auto* st = std::get_if<SimpleTypeNode>(&node.var)) return st->name;

    if(const auto* lt = std::get_if<ListTypeNode>(&node.var)){
        return "list_" + getTypeNodeName(*lt->elemType);
    }

    if(const auto* tt = std::get_if<TupleTypeNode>(&node.var)){
        std::string s = "tuple";
        for(const auto& e : tt->elems) s += "_" + getTypeNodeName(*e);
        return s;
    }

    if(const auto* gt = std::get_if<GenericTypeNode>(&node.var)){
        std::string s = gt->name;
        for(const auto& a : gt->args) s += "_" + getTypeNodeName(*a);
        return s;
    }

    if(const auto* ft = std::get_if<FunctionTypeNode>(&node.var))
        return getTypeNodeName(*ft->from) + "_to_" + getTypeNodeName(*ft->to);
    return "unknown";
}

//нужен точный размер стека - std::swap - based on move semantics //по факту один умный проход
void CodeGenerator::genFuncDecl(const FuncDecl& fn){
    FuncContext ctx;  //контекст текущей функции - локальные переменные, offsets, размеры стека
    ctx.name = fn.name;

    //название функции уникальное для перегрузки
    std::string label = "__fn_" + fn.name;

    for(const auto& p : fn.params){
        label += "_" + getTypeNodeName(*p.type);
    }

    m_funcLabels[fn.name] = label;
    emitLabel(label);


    emit("push rbp");
    emit("mov rbp, rsp");
    
    //сохраняем основной поток и генерируем тело во временный //через genExpr бы не смогли, он бы сразу генерировал код
    std::ostringstream bodyStream;
    std::swap(m_text, bodyStream); //m_text теперь пустой, а в bodyStream уже сохраняли стек и написали имя функции
    //пишем все в m_text

    genFuncParams(fn, ctx);
    genExpr(*fn.body, ctx); //выражения, результат в rax

    //print() in main - некий костыль, для вывода не unit, а нуля
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

    if(stackSize > 0){
        emit("sub rsp, " + std::to_string(stackSize));
    }

    m_text << body;
    m_text << "\n";
}

//вспомогательная для параметров функции
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

/*
*вспомогательные функции для Generic
*/

//преобразование типа в уникальную строку для формирования метки функции
//BuiltinType("int64") -> "int64"
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
//одна функция инстанцируется под разные типы
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

}