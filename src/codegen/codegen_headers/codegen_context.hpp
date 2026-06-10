#pragma once

#include <string>
#include <vector>
#include <optional>

namespace Codegen{

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

         //ищем с конца веткора, первый найденный - последний добавленный - shadowing
        for(auto it = locals.rbegin(); it != locals.rend(); it++){ 
            if(it->name == varName) return it->offset;
        }
        return std::nullopt;
    }

    //тоже для shadowing
    void removeLocal(const std::string& varName){
    for(int i = static_cast<int>(locals.size()) - 1; i >= 0; --i){
        if(locals[i].name == varName){
            locals.erase(locals.begin() + i);
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

}