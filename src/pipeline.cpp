#include "pipeline.hpp"
#include "debug/dump.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "semantic.hpp"
#include "codegen.hpp"
#include <iostream>
#include <sstream>
#include <fstream>

int runPipeline(const CLIOptions& opts){
    
    std::ifstream file(opts.filename); //переменная поток с именем file, пытается открыть файл

    if(!file){
        std::cerr << "error: cannot open file '" << opts.filename << "'\n";
        return 1;
    }

    std::ostringstream buf; //создает буфер обертку
    buf << file.rdbuf();
    std::string source = buf.str(); //копируем все из буфера в строку


    //Лексер
    Lexer::Lexer lexer(source, opts.filename);

    auto lexResult = lexer.tokenize(); //указатель на вектор токенов

    if(opts.dumpTokens){
        printTokens(lexResult.tokens);
    }

    for(const auto& err : lexResult.errors){
        std::cerr << err.format(opts.filename) << "\n";
    }

    if(lexResult.hasErrors()) return 1;

    //Парсер
    Parser::Parser parser(std::move(lexResult.tokens), opts.filename);
    auto progResult = parser.parse();

    if(!progResult){
        std::cerr << progResult.error().format(opts.filename) << "\n";
        return 1;
    }

    auto prog = std::move(*progResult);

    if(opts.dumpAst){
        printAst(prog);
        return 0;
    }

    //Семантика 
    Semantic::Analyzer analyzer(opts.filename);
    auto errors = analyzer.analyze(prog);

    if(!errors.empty()){
        for(const auto& err : errors){
            std::cerr << err.format(opts.filename) << "\n";
        }
        return 1;
    }

    
    //Кодоген
    Codegen::CodeGenerator codegen(
        analyzer.get_registry(), analyzer.getExprTypes(), 
        analyzer.getCallTypeMaps(), opts.filename);

    std::string asmCode = codegen.generate(prog);

    std::string outFile = "output/output.asm";
    std::ofstream out(outFile);

    if(!out){
        std::cerr << "error: cannot open output file '" << outFile << "'\n";
        return 1;
    }
    
    out << asmCode;
    out.close();

    std::cout << "generated: " << outFile << "\n";

    return 0;
}