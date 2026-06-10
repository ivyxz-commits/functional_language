#include "options.hpp"
#include <iostream>

std::optional<CLIOptions> parseCLI(int argc, char* argv[]){
    if(argc < 2){
        std::cerr << "usage lang <file> [--dump-tokens] [--dump-ast]\n";
        return std::nullopt;
    }

    CLIOptions opts;
    opts.filename = argv[1];

    for (int i = 2; i < argc; i++){
        std::string flag = argv[i];
        if(flag == "--dump-tokens") opts.dumpTokens = true;
        if(flag == "--dump-ast") opts.dumpAst = true;
        else{
            std::cerr << "unknown flag: " << flag << "/n";
            return std::nullopt;
        }
    }

    return opts;
}