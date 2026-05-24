#pragma once

#include "ast.hpp"
#include "semantic.hpp"
#include <sstream>

namespace Codegen{
    
using namespace Parser;
using namespace Semantic;

//всегда на стеке
struct LocalVar{
    std::string name;
    int offset; //смещение от rbp: -8, -16
};

//информация нужная при генерации одной функции
struct FuncContext{
    std::string name;
    int stackSize = 0;
    int nextOffset = 0; //следующее свободное смещение
    std::vector<LocalVar> locals;

    int allocLocal(const std::string& varName){
        nextOffset -= 8;
        locals.push_back({varName, nextOffset});
        stackSize = -nextOffset;
        return nextOffset; //где лежит переменная
    }

    //1. локальные на стеке -> глобальные функции -> встроенные
    std::optional<int> findLocal(const std::string& varName) const {
        for(const auto& l : locals){
            if(l.name == varName) return l.offset;
        }
        return std::nullopt;
    }

    void removeLocal(const std::string& varName){
        for(auto it = locals.begin(); it != locals.end(); it++){
            if(it->name == varName){
                locals.erase(it);
                return;
            }
        }
    }

    //должен быть выравнен по 16 байт
    int alignedStackSize() const{
        if(stackSize % 16 == 0) return stackSize;
        return stackSize + (16 - stackSize % 16);
    }
};

//обход AST и генерация текста для ассемблера
class CodeGenerator{

public:
    //для конструкторов в ADT 
    //пример в примере при match shape Circl -> tag = 0 - индекс конструктора в векторе dataTypeInfo::constructors
    CodeGenerator(std::string filename = "<input>", const TypeRegistry& registry);

    //генерация полного .asm файла
    std::string generate(const Program& prog);

private:
    std::string m_filename;

    //будем писать не в консоль, а внутри себя
    std::ostringstream m_text;
    std::ostringstream m_data;
    std::ostringstream m_bss;

    //нужно выдерживать уникальность меток
    int m_labelCnt = 0; //в коде
    int m_strCnt = 0; // .data
    std::unordered_map<std::string, std::string> m_funcLabels; //add и __fn_add //тут лежит тело функции
    std::unordered_map<std::string,std::string> m_funcLabels;

    const TypeRegistry& m_registry;
    int getConstructorTag(const std::string& ctorName) const;

    




}




}