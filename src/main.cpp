
#include <stdio.h>

#include "str.h"
#include "except.h"
#include "langobj.h"


// --- VIRTUAL MACHINE ----------------------------------------------------- //


enum OpCode
{
    opNop,          // []
    opEnd,          // []
    
    // pushers
    opLoad0,        // []                   +1
    opLoadInt,      // [int]                +1
    opLoadLarge,    // [int,int]            +2
    opLoadChar,     // [int]                +1
    opLoadFalse,    // []                   +1
    opLoadTrue,     // []                   +1
    opLoadNull,     // []                   +1
    opLoadNullStr,  // []                   +1
    opLoadStr,      // [str-data-ptr]       +1
    opLoadTypeRef,  // [ShType*]            +1
    
    // poppers
    opMkSubrange,   // []                -2 +1

    // TODO: linenum, rangecheck opcodes
};


union VmQuant
{
    OpCode op_;
    int int_;
    ptr ptr_;
};


class VmStack: public noncopyable
{
protected:
    PodStack<VmQuant> stack;
public:
    VmStack();
    int size() const          { return stack.size(); }
    bool empty() const        { return stack.empty(); }
    void clear()              { return stack.clear(); }
    VmQuant& push()           { return stack.push(); }
    VmQuant  pop()            { return stack.pop(); }
    void  pushInt(int v)      { push().int_ = v; }
    void  pushPtr(ptr v)      { push().ptr_ = v; }
    void  pushLarge(large v);
    int   popInt()            { return pop().int_; }
    ptr   popPtr()            { return pop().ptr_; }
    large popLarge();
    int   topInt()            { return stack.top().int_; }
    ptr   topPtr()            { return stack.top().ptr_; }
    large topLarge();
};


class VmCode: public noncopyable
{
protected:
    struct EmulStackItem
    {
        ShType* type;
        EmulStackItem(ShType* iType): type(iType)  { }
    };

    PodArray<VmQuant> code;
    PodStack<EmulStackItem> emulStack;
    ShScope* compilationScope;

    void addOp(OpCode o)  { code.add().op_ = o; }
    void addInt(int v)    { code.add().int_ = v; }
    void addPtr(ptr v)    { code.add().ptr_ = v; }

    static void run(VmQuant* p);

public:
    VmCode(ShScope* iCompilationScope);
    
    void genLoadConst(const ShValue&);
    void genMkSubrange();
    void endGeneration();
    
    ShValue runConst();
    ShType* topType() const  { return emulStack.top().type; }
};


// ------------------------------------------------------------------------- //


class ENoContext: public Exception
{
public:
    virtual string what() const { return "No execution context"; }
};



VmCode::VmCode(ShScope* iCompilationScope)
    : code(), compilationScope(iCompilationScope)  { }


void VmCode::genLoadConst(const ShValue& v)
{
    emulStack.push(v.type);
    if (v.type->isOrdinal())
    {
        if (v.type->isBool())
        {
            addOp(v.value.int_ ? opLoadTrue : opLoadFalse);
        }
        else if (v.type->isChar())
        {
            addOp(opLoadChar);
            addInt(v.value.int_);
        }
        else if (((ShOrdinal*)v.type)->isLargeSize())
        {
            addOp(opLoadLarge);
            addInt(int(v.value.large_));
            addInt(int(v.value.large_ >> 32));
        }
        else if (v.value.int_ == 0)
        {
            addOp(opLoad0);
        }
        else
        {
            addOp(opLoadInt);
            addInt(v.value.int_);
        }
    }
    else if (v.type->isString())
    {
        const string& s = PTR_TO_STRING(v.value.ptr_);
        if (s.empty())
        {
            addOp(opLoadNullStr);
        }
        else
        {
            addOp(opLoadStr);
            addPtr(ptr(s.c_bytes()));
        }
    }
    else if (v.type->isVoid())
    {
        addOp(opLoadNull);
    }
    else
        throw EInternal(50, "unknown type in VmCode::genLoadConst()");
}


void VmCode::genMkSubrange()
{
    emulStack.pop();
    ShType* type = emulStack.pop().type;
    if (!type->isOrdinal())
        throw EInternal(51, "Ordinal type expected");
    emulStack.push(((ShOrdinal*)type)->deriveRangeType(compilationScope));
    addOp(opMkSubrange);
}


void VmCode::endGeneration()
{
    addOp(opEnd);
}


#ifdef SINGLE_THREADED

VmStack vmStack;

#endif


void VmCode::run(VmQuant* p)
{
    while (1)
    {
        switch ((p++)->op_)
        {
        case opNop: break;
        case opEnd: return;
        // pushers
        case opLoad0: vmStack.pushInt(0); break;
        case opLoadInt: vmStack.pushInt((p++)->int_); break;
        case opLoadLarge: vmStack.pushInt((p++)->int_); vmStack.pushInt((p++)->int_); break;
        case opLoadChar: vmStack.pushInt((p++)->int_); break;
        case opLoadFalse: vmStack.pushInt(0); break;
        case opLoadTrue: vmStack.pushInt(1); break;
        case opLoadNull: vmStack.pushInt(0); break;
        case opLoadNullStr: vmStack.pushPtr(emptystr); break;
        case opLoadStr: vmStack.pushPtr((p++)->ptr_); break;
        case opLoadTypeRef: vmStack.pushPtr((p++)->ptr_); break;
        
        // poppers
        case opMkSubrange: /* two ints become a subrange, haha! */ break;
        default: fatal(CRIT_FIRST + 50, "[VM] Unknown opcode");
        }
    }
}


ShValue VmCode::runConst()
{
    if (!vmStack.empty())
        fatal(CRIT_FIRST + 51, "[VM] Stack not clean before const run");

    run(&code._at(0));

    if (emulStack.size() != 1)
        fatal(CRIT_FIRST + 52, "[VM] Emulation stack in undefined state after const run");

    ShType* type = emulStack.pop().type;

    int expectSize = type->isLargeSize() ? 2 : 1;
    if (vmStack.size() != expectSize)
        fatal(CRIT_FIRST + 53, "[VM] Stack in undefined state after const run");

    if (type->isLargeSize())
        return ShValue(type, vmStack.popLarge());
    if (type->isPointer())
        return ShValue(type, vmStack.popPtr());
    else
        return ShValue(type, vmStack.popInt());
}


// ------------------------------------------------------------------------- //
// --- HIS MAJESTY THE COMPILER -------------------------------------------- //
// ------------------------------------------------------------------------- //


// --- CONST EXPRESSION ---------------------------------------------------- //

/*
    (expr), ident, int, string, char
    -, not
    *, /, div, mod, and, shl, shr, as
    +, –, or, xor
    ..
    =, <>, <, >, <=, >=, in, is
*/

ShBase* ShModule::getQualifiedName()
{
    // qualified-name ::= { ident "." } ident
    string ident = parser.getIdent();
    ShBase* obj = currentScope->deepFind(ident);
    if (obj == NULL)
        error("Unknown identifier '" + ident + "'");
    while (parser.token == tokPeriod)
    {
        if (!obj->isScope())
            return obj;
        ShScope* scope = (ShScope*)obj;
        parser.next(); // "."
        ident = parser.getIdent();
        obj = scope->find(ident);
        if (obj == NULL)
            error("'" + ident + "' is not known within '" + scope->name + "'");
    }
    return obj;
}


void ShModule::parseAtom(VmCode& code)
{
    if (parser.token == tokLParen)
    {
        parser.next();
        parseExpr(code);
        parser.skip(tokRParen, ")");
    }
    else if (parser.token == tokIntValue)
    {
        large value = parser.intValue;
        ShInteger* type = queenBee->defaultInt->contains(value) ?
            queenBee->defaultInt : queenBee->defaultLarge;
        parser.next();
        code.genLoadConst(ShValue(type, parser.intValue));
    }
    else if (parser.token == tokStrValue)
    {
        if (parser.strValue.size() == 1)
        {
            int value = (unsigned)parser.strValue[0];
            parser.next();
            code.genLoadConst(ShValue(queenBee->defaultChar, value));
        }
        else
        {
            const string s = parser.strValue;
            parser.next();
            code.genLoadConst(ShValue(queenBee->defaultStr, s));
        }
    }
    else if (parser.token == tokIdent)
    {
        ShBase* obj = getQualifiedName();
        if (obj->isConstant())
            code.genLoadConst(((ShConstant*)obj)->value);
        else
            errorWithLoc("Constant expected");
    }
}


void ShModule::parseFactor(VmCode& code)
{
    parseAtom(code);
}


void ShModule::parseTerm(VmCode& code)
{
    parseFactor(code);
}


void ShModule::parseSimpleExpr(VmCode& code)
{
    parseTerm(code);
}


void ShModule::parseSubrange(VmCode& code)
{
    parseSimpleExpr(code);
    if (parser.token == tokRange)
    {
        // TODO: check bounds for left < right maybe?
        ShType* left = code.topType();
        if (!left->isOrdinal())
            error("Only ordinal types are allowed in subranges");
        ShOrdinal* leftOrd = (ShOrdinal*)left;
        parser.next();
        parseSimpleExpr(code);
        ShType* right = code.topType();
        if (!right->isOrdinal())
            error("Only ordinal types are allowed in subranges");
        ShOrdinal* rightOrd = (ShOrdinal*)right;
        if (!leftOrd->isCompatibleWith(rightOrd))
            error("Left and right values of a subrange must be compatible");
        if (leftOrd->isLargeSize() || rightOrd->isLargeSize())
            error("Large subrange bounds are not supported");
        code.genMkSubrange();
    }
}


void ShModule::parseExpr(VmCode& code)
{
    parseSubrange(code);
}


ShValue ShModule::getConstExpr(ShType* typeHint)
{
    VmCode code(currentScope);
    parseExpr(code);
    code.endGeneration();
    ShValue result = code.runConst();

    if (typeHint == NULL)
        typeHint = result.type;

    if (typeHint->isOrdinal() && result.type->isOrdinal())
    {
        ShOrdinal* ordHint = (ShOrdinal*)typeHint;
        if (!ordHint->isCompatibleWith(result.type))
            error("Type mismatch in constant expression");
        if (!ordHint->contains(result.largeValue()))
            error("Value out of range");
    }
    else if (!typeHint->canAssign(result))
        error("Type mismatch in constant expression");

    return result;
}


// --- TYPES --------------------------------------------------------------- //

/*
ShOrdinal* ShModule::getRangeType()
{
    return NULL;
}
*/

ShType* ShModule::getDerivators(ShType* type)
{
    // array-derivator ::= "[" [ type ] "]"
    if (parser.token == tokLSquare)
    {
        parser.next();
        if (parser.token == tokRSquare)
        {
            type = type->deriveVectorType(currentScope);
            parser.next();
        }
        else if (parser.token == tokRange)
        {
            parser.next();
            parser.skip(tokRSquare, "]");
            if (!type->isOrdinal())
                error("Ranges apply only to ordinal types");
            type = ((ShOrdinal*)type)->deriveRangeType(currentScope);
        }
        else
        {
            ShType* indexType = getType();
            parser.skip(tokRSquare, "]");
            if (!indexType->canBeArrayIndex())
                error(indexType->getDisplayName("") + " can't be used as array index");
            type = type->deriveArrayType(indexType, currentScope);
        }
        type = getDerivators(type);
    }
    return type;
}


ShType* ShModule::getType()
{
    // type ::= type-id { type-derivator }
    // type-id ::= qualified-name | "typeof" "(" type-expr ")" | range

    // TODO: check if this is a range first
    ShType* type = NULL;
    ShBase* obj = getQualifiedName();
    if (obj->isTypeAlias())
        type = ((ShTypeAlias*)obj)->base;
    else if (obj->isType())
        type = (ShType*)obj;
    if (type == NULL)
        errorWithLoc("Expected type specifier");
    return getDerivators(type);
}


// --- STATEMENTS/DEFINITIONS ---------------------------------------------- //


void ShModule::parseTypeDef()
{
    ShType* type = getType();
    string ident = parser.getIdent();
    type = getDerivators(type);
    addObject(new ShTypeAlias(ident, type));
}


void ShModule::parseVarConstDef(bool isVar)
{
    // TODO: skip type if ident is new or if it is followed by '='
    ShType* type = getType();
    string ident = parser.getIdent();
    type = getDerivators(type);
    parser.skip(tokAssign, "=");
    ShValue value = getConstExpr(type);
    if (isVar)
        addObject(new ShVariable(ident, type)); // TODO: initializer
    else
        addObject(new ShConstant(ident, value));
}


void ShModule::compile()
{
    try
    {
        currentScope = this;
        
        parser.next();
        
        if (parser.token == tokModule)
        {
            parser.next();
            string modName = parser.getIdent();
            if (strcasecmp(modName.c_str(), name.c_str()) != 0)
                error("Module name mismatch");
            setNamePleaseThisIsBadIKnow(modName);
            parser.skipSep();
        }

        while (parser.token != tokEof)
        {
            if (parser.skipIf(tokDef))
                parseTypeDef();
            else if (parser.skipIf(tokConst))
                parseVarConstDef(false);
            else if (parser.skipIf(tokVar))
                parseVarConstDef(true);
            else
                errorWithLoc("Expected definition or statement");
            parser.skipSep();
        }

        compiled = true;

#ifdef DEBUG
        dump("");
#endif

    }
    catch(Exception& e)
    {
        fprintf(stderr, "%s\n", e.what().c_str());
    }
}


// ------------------------------------------------------------------------- //
// ------------------------------------------------------------------------- //
// ------------------------------------------------------------------------- //


VmStack::VmStack()
        : stack()  { }

void VmStack::pushLarge(large v)
        { push().int_ = int(v); push().int_ = int(v >> 32); }

large VmStack::popLarge()
        { return (large(popInt()) << 32) | popInt(); }

large VmStack::topLarge()
        { return (large(stack.at(-1).int_) << 32) | stack.at(-2).int_; }


// ------------------------------------------------------------------------- //


class _AtExit
{
public:
    ~_AtExit()
    {
        if (Base::objCount != 0)
            fprintf(stderr, "Internal: objCount = %d\n", Base::objCount);
        if (stralloc != 0)
            fprintf(stderr, "Internal: stralloc = %d\n", stralloc);
        if (FifoChunk::chunkCount != 0)
            fprintf(stderr, "Internal: chunkCount = %d\n", FifoChunk::chunkCount);
#ifdef SINGLE_THREADED
        vmStack.clear();
#endif
        if (stackimpl::stackAlloc != 0)
            fprintf(stderr, "Internal: stackAlloc = %d\n", stackimpl::stackAlloc);
    }
} _atexit;




int main()
{
    try
    {
        initLangObjs();
        
#ifdef XCODE
        ShModule module("../../src/tests/test.sn");
#else
        ShModule module("tests/test.sn");
#endif
        module.compile();
        
        doneLangObjs();
    }
    catch (Exception& e)
    {
        fprintf(stderr, "\n*** Exception: %s\n", e.what().c_str());
    }

    return 0;
}

