#pragma once

#include "codegen_context.hpp"
#include "ast.hpp"
#include "semantic.hpp"

#include <sstream>
#include <set> //уникальные элементы в отсортированном порядке

namespace Codegen{
    
using namespace Parser;
using namespace Semantic;


//обход AST и генерация текста для ассемблера
class CodeGenerator{

public:
    //для конструкторов в ADT 
    //пример в примере при match shape Circl -> tag = 0 - индекс конструктора в векторе dataTypeInfo::constructors
    CodeGenerator(const TypeRegistry& registry, const std::unordered_map<const ExprNode*, sPtr<TypeInfo>>& m_exprTypes,
        const std::unordered_map<const CallExpr*, std::unordered_map<std::string, sPtr<TypeInfo>>>& callTypeMaps,
        const std::unordered_map<const CallExpr*, const FuncDecl*>& resolvedOverloads,
        std::string filename = "<input>");

    //генерация полного .asm файла
    std::string generate(const Program& prog);

private:
    std::string m_filename;

    //будем писать не в консоль, а внутри себя
    std::ostringstream m_text;
    std::ostringstream m_data;
    std::ostringstream m_bss;

    //так как до call можем не дойти - нужна последовательность верная 
    //поэтому в конце, пока по call не прыгнем
    std::ostringstream m_lambdas;
    std::ostringstream m_generics;

    //нужно выдерживать уникальность меток
    int m_labelCnt = 0; //в коде
    int m_strCnt = 0; // .data
    std::unordered_map<std::string, std::string> m_funcLabels; //add и __fn_add //тут лежит тело функции



    //дженерики
    std::unordered_map<std::string, const FuncDecl*> m_genericFuncs;
    std::set<std::string> m_generatedInstances; //чтобы метки функций не повторялись
    std::unordered_map<std::string, sPtr<TypeInfo>> m_currentTypeVarMap;
    //+
    const std::unordered_map<const CallExpr*, 
    std::unordered_map<std::string, sPtr<TypeInfo>>>& m_callTypeMaps;

    const TypeRegistry& m_registry;
    const std::unordered_map<const ExprNode*, sPtr<TypeInfo>>& m_exprTypes;
    int getConstructorTag(const std::string& ctorName) const;

    //перегрузка
    const std::unordered_map<const CallExpr*, const FuncDecl*>& m_resolvedOverloads;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //utilities

    //работа с метками
    std::string freshLabel(const std::string& label = "L"); //.L_0, .L_1, ...
    std::string freshStrLabel();
    std::string freshLambdaLabel();
    void emit(const std::string& line);
    void emitData(const std::string& line);
    void emitLabel(const std::string& label); //метка
    void emitDataLabel(const std::string& label);


    //runtime functions - генерация вспомогательного ассемблерного кода
    void emitRuntime(); //главная функция
    void emitMalloc(); //ADT, замыкания
    void emitPrintInt();
    void emitPrintString();
    void emitReadString();
    void emitPanic();
    void emitExit();

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //declarations
    void genDecl(const DeclNode& decl);

    //Объявление функции со вспомогательной функцией
    void genFuncDecl(const FuncDecl& fn);
    void genFuncParams(const FuncDecl& fn, FuncContext& ctx);

    //объявление функции с generic
    void genGenericFuncDecl(const FuncDecl& fn, const std::string& label,
        const std::unordered_map<std::string, sPtr<TypeInfo>>& typeVarMap);
    //преобразование имени в уникальную строку - информация о типах
    std::string mangleTypeName(const sPtr<TypeInfo>& type); 

    void genModuleDecl(const ModuleDecl& mod);


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //expressions - rax
    void genExpr(const ExprNode& expr, FuncContext& ctx);
    void genIdent(const IdentExpr& e,const ExprNode& node, FuncContext& ctx);
    void genUnary(const UnaryExpr& e, FuncContext& ctx);
    void genIf(const IfExpr& e, FuncContext& ctx);
    void genMatch(const MatchExpr& e, FuncContext& ctx);
    void genLambda(const LambdaExpr& e, FuncContext& ctx);
    void genLetIn(const LetInExpr& e, FuncContext& ctx);
    void genTuple(const TupleExpr& e, FuncContext& ctx);
    void genList(const ListExpr& e, FuncContext& ctx);
    void genCons(const ConsExpr& e, FuncContext& ctx);
    void genConstructor(const ConstructorExpr& e, FuncContext& ctx);
    

    ///////////////////////////////////////////////////////////////////////////////
    void genLiteral(const LiteralExpr& e, FuncContext& ctx);
    //вспомогательные
    void genIntLiteral(int64_t v);
    void genFloatLiteral(double v);
    void genStringLiteral(const std::string& v);    

    ///////////////////////////////////////////////////////////////////////////////
    void genBinary(const BinaryExpr& e, FuncContext& ctx);
    //вспомогательные
    void genFloatOp(BinaryOp op);
    void genIntOp(BinaryOp op);

    ///////////////////////////////////////////////////////////////////////////////
    void genFieldAccess(const FieldAccessExpr& e, FuncContext& ctx);
    //вспомогательные
    bool genModuleFieldAccess(const IdentExpr& ident, const FieldAccessExpr& e);
    void genNamedCtorFieldAccess(const FieldAccessExpr& e);

    ///////////////////////////////////////////////////////////////////////////////
    void genCall(const CallExpr& e, const ExprNode& node, FuncContext& ctx);
    //вспомогательные
    void genCallBuiltin(const IdentExpr& ident, const CallExpr& e);
    void genCallClosure(const std::vector<int>& argOffsets);
    void genCallIdent(const IdentExpr& ident, const CallExpr& e,
        const std::vector<int>& argOffsets, FuncContext& ctx); 


    //вспомогательные для каррирования и частичного применения
     void genPartialApply(const CallExpr& e, const std::vector<int>&
        argOffsets, FuncContext& ctx);

    //вспомогательные
    bool isPartialCall(const ExprNode& e);
    std::string getCalleeFuncLabel(const CallExpr& e, FuncContext& ctx); //label оригинальной функции
    std::string genPartialWrapLambda(const std::string& funcLabel, int capturedCount);
    void genPartialClosure(const std::string& wrapLabel, const std::string& funcLabel,
        const std::vector<int>& argOffsets, FuncContext& ctx); //code_ptr - лямбда обертки и захваченные аргументы 


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //patterns matching
    void genPattern(const PatternNode& pattern, const std::string& valueReg, //target
                    const std::string& failLabel, FuncContext& ctx); //failLabel - метка следующей ветки match

    //вспомогательные
    void genNamePattern(const NamePatternNode& p, 
        const std::string& valueReg, FuncContext& ctx);

    void genTuplePattern(const TuplePatternNode& p, 
        const std::string& failLabel, FuncContext& ctx);

    void genConstructorPattern(const ConstructorPatternNode& p, 
        const std::string& failLabel, FuncContext& ctx);

    void genConsPattern(const ConsPatternNode& p, 
        const std::string& failLabel, FuncContext& ctx);

    void genListPattern(const ListPatternNode& p,
        const std::string& failLabel, FuncContext& ctx);

    void genLiteralPattern(const LiteralPatternNode& p, 
        const std::string& failLabel);
    ////////////////////////////////////////////////////////////
    //у genLitPat() свои подфункции
    void genIntBoolLiteralPattern(const LiteralPatternNode& p, 
        const std::string& failLabel);

    void genRealLiteralPattern(const LiteralPatternNode& p, 
        const std::string& failLabel);

    void genStringLiteralPattern(const LiteralPatternNode& p,
        const std::string& failLabel);




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //замыкания - лямбда функции
    std::string genLambdaFunc(const LambdaExpr& e, const std::vector<std::string>& captured, FuncContext& outer);
    //у нее две свои вспомогательные
    void genLambdaCaptured(const std::vector<std::string>& captured, int envOff, FuncContext& ctx);
    void genLambdaParams(const LambdaExpr& e, FuncContext& ctx);  
    //+
    std::vector<std::string> findFreeVars(const LambdaExpr& e, const FuncContext& ctx) const; // \y -> x + y ==> x - free, берем, например, из let
    //вспомогательная к findFreeVars
    void scanExpr(const ExprNode& expr, const std::vector<std::string>& bound, const FuncContext& ctx, std::vector<std::string>& result) const;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //ABI
    static const char* argReg(int i); //rdi, rsi
    void emitAlloc(int size); //память в куче

    //вспомогательные
    bool isFloatExpr(const ExprNode& e) const;
    bool isStringExpr(const ExprNode& e) const;
    void emitFunctionsExterns(); 
};

}