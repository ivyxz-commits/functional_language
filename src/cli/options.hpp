#pragma once
#include <string>
#include <optional>

struct CLIOptions{ //все параметры запуска компилятора
    std::string filename;
    bool dumpTokens = false;
    bool dumpAst = false;
};

std::optional<CLIOptions> parseCLI(int argc, char* argv[]);