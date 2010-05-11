#ifndef __VM_H
#define __VM_H

#include "common.h"
#include "runtime.h"
#include "typesys.h"


enum OpCode
{
    // NOTE: the order of many of these instructions in their groups is significant

    opEnd = 0,          // end execution and return
    opNop,
    opExit,             // throws eexit()

    // Const loaders
    opLoadTypeRef,      // [Type*] +obj
    opLoadNull,         // +null
    opLoad0,            // +ord
    opLoad1,            // +ord
    opLoadOrd8,         // [int8] +ord
    opLoadOrd,          // [int] +ord
    opLoadStr,          // [object*] +str
    opLoadEmptyVar,     // [variant::Type:8] + var
    opLoadConst,        // [Definition*] +var

    // Loaders
    opLoadSelfVar,      // [self-idx:8] +var
    opLoadStkVar,       // [stk-idx:8] +var
    opLoadMember,       // [self-idx:8] -stateobj +var

    // Storers
    opInitStkVar,       // [stk-idx:8] -var

    opPop,              // -var

    opChrToStr,         // -ord +str
    opChrCat,           // -ord -str +str
    opStrCat,           // -str -str +str
    opVarToVec,         // -var +vec
    opVarCat,           // -var -vec +vec
    opVecCat,           // -vec -vec +vec

    // Arithmetic binary: -ord, -ord, +ord
    opAdd,              // -int, +int, +int
    opSub,              // -int, +int, +int
    opMul,              // -int, +int, +int
    opDiv,              // -int, +int, +int
    opMod,              // -int, +int, +int
    opBitAnd,           // -int, +int, +int
    opBitOr,            // -int, +int, +int
    opBitXor,           // -int, +int, +int
    opBitShl,           // -int, +int, +int
    opBitShr,           // -int, +int, +int

    // Boolean operations are performed with short evaluation using jumps,
    // except NOT and XOR
    opBoolXor,          // -bool, -bool, +bool

    // Arithmetic unary: -ord, +ord
    opNeg,              // -int, +int
    opBitNot,           // -int, +int
    opNot,              // -bool, +bool

    // Comparators
    opCmpOrd,           // -ord, -ord, +{-1,0,1}
    opCmpStr,           // -str, -str, +{-1,0,1}
    opCmpVar,           // -var, -var, +{0,1}

    // Compare the stack top with 0 and replace it with a bool value.
    // The order of these opcodes is in sync with tokens tokEqual..tokGreaterEq
    opEqual,            // -int, +bool
    opNotEq,            // -int, +bool
    opLessThan,         // -int, +bool
    opLessEq,           // -int, +bool
    opGreaterThan,      // -int, +bool
    opGreaterEq,        // -int, +bool

    // Jumps; [dst] is a relative 16-bit offset.
    opJump,             // [dst 16]
    opJumpTrue,         // [dst 16] -bool
    opJumpFalse,        // [dst 16] -bool
    // Short bool evaluation: pop if jump, leave it otherwise
    opJumpOr,           // [dst 16] (-)bool
    opJumpAnd,          // [dst 16] (-)bool

    opInv,
    opMaxCode = opInv,
};


inline bool isUndoableLoadOp(OpCode op)
    { return (op >= opLoadTypeRef && op <= opLoadConst); }

inline bool isCmpOp(OpCode op)
    { return op >= opEqual && op <= opGreaterEq; }

/*
inline bool isDesignatorLoadOp(OpCode op)
    { return op >= opLoadSelfVar && op <= opLoadStkVar; }

inline OpCode designatorLoadToStore(OpCode op)
    { return OpCode(op + opStoreSelfVar - opLoadSelfVar); }
*/

inline bool isJump(OpCode op)
    { return op >= opJump && op <= opJumpAnd; }

inline bool isBoolJump(OpCode op)
    { return op >= opJumpTrue && op <= opJumpAnd; }


// --- Code Generator ------------------------------------------------------ //


class CodeGen: noncopyable
{
protected:
    State* codeOwner;
    CodeSeg& codeseg;

    struct SimStackItem
    {
        Type* type;
        memint offs;
        SimStackItem(Type* t, memint o)
            : type(t), offs(o)  { }
    };

    podvec<SimStackItem> simStack;  // exec simulation stack
    memint locals;                  // number of local vars allocated
    bool isConstCode;

    template <class T>
        void add(const T& t)                        { codeseg.append<T>(t); }
    void addOp(OpCode op)                           { codeseg.append<uchar>(op); }
    void addOp(Type*, OpCode op);
    template <class T>
        void addOp(OpCode op, const T& a)           { addOp(op); add<T>(a); }
    template <class T>
        void addOp(Type* t, OpCode op, const T& a)  { addOp(t, op); add<T>(a); }
    void addJumpOffs(jumpoffs o)                    { add<jumpoffs>(o); }
    Type* stkPop();
    void stkReplaceTop(Type* t);
    Type* stkTop()
        { return simStack.back().type; }
    memint stkTopOffs()
        { return simStack.back().offs; }
    Type* stkTop(memint i)
        { return simStack.back(i).type; }
    static void error(const char*);
    void undoLastLoad();
    void canAssign(Type* from, Type* to, const char* errmsg = NULL);
    bool tryImplicitCast(Type*);
    void loadStoreVar(Variable*, bool);

public:
    CodeGen(CodeSeg&);
    ~CodeGen();
    
    memint getLocals()      { return locals; }
    State* getState()       { return codeOwner; }
    Type* getTopType()      { return stkTop(); }
    Type* tryUndoTypeRef();
    void deinitLocalVar(Variable*);
    void discard();
    void implicitCast(Type*, const char* errmsg = NULL);

    void loadTypeRef(Type*);
    void loadConst(Type* type, const variant&);
    void loadDefinition(Definition*);
    void loadEmptyCont(Container* type);
    void loadSymbol(ModuleVar*, Symbol*);
    void loadVariable(Variable*);
    void loadMember(const str& ident);
    void loadMember(Variable*);
    void storeRet(Type*);
//    Type* undoDesignatorLoad(str& loader);
//    void storeDesignator(str loaderCode, Type* type);
    void arithmBinary(OpCode op);
    void arithmUnary(OpCode op);
    void boolXor();
    Container* elemToVec();
    void elemCat();
    void cat();
    void cmp(OpCode);
    void _not();
    memint boolJumpForward(OpCode op);
    memint jumpForward(OpCode op);
    void resolveJump(memint jumpOffs);
    void end();
    Type* runConstExpr(Type* expectType, variant& result); // defined in vm.cpp
};


// --- Execution context --------------------------------------------------- //


struct CompilerOptions
{
    bool enableDump;
    bool enableAssert;
    bool linenumInfo;
    bool vmListing;
    memint stackSize;
    strvec modulePath;

    CompilerOptions();
};


class Context: public Scope
{
    friend class Compiler;
protected:
    CompilerOptions options;
    objvec<ModuleInst> moduleInsts;
    ModuleInst* queenBeeInst;

    ModuleInst* addModuleInst(ModuleInst*);
    ModuleInst* loadModule(const str& filePath);
    str lookupSource(const str& modName);
    ModuleInst* getModule(const str&); // for use by the compiler, "uses" clause
public:
    Context();
    ~Context();
    ModuleInst* findModuleInst(Module*);
    variant execute(const str& filePath);
};


// The Virtual Machine. This routine is used for both evaluating const
// expressions at compile time and, obviously, running runtime code. It is
// reenterant and can be launched concurrently in one process so long as
// the arguments passed belong to one thread (except the code seggment which
// is read-only anyway).

void runRabbitRun(Context* context, stateobj* self, rtstack& stack, const char* code);


struct eexit: public ecmessage
    { eexit() throw(); ~eexit() throw(); };


#endif // __VM_H
