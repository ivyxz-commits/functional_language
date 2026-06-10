#include "cli/options.hpp"
#include "pipeline.hpp"

int main(int argc, char* argv[]){
    auto opts = parseCLI(argc, argv);
    if(!opts) return 1;
    return runPipeline(*opts);
}