

#include "common.h"
#include "runtime.h"
#include "source.h"
#include "symbols.h"
#include "typesys.h"
#include "vm.h"


// --- THE VIRTUAL MACHINE ------------------------------------------------- //


static void invOpcode()        { throw EInvOpcode(); }


template<class T>
    inline void PUSH(variant*& stk, const T& v) { ::new(++stk) variant(v);  }
inline void POP(variant*& stk)                  { (*stk--).~variant(); }


#define BINARY_INT(op) { (stk - 1)->_int_write() op stk->_int(); POP(stk); }
#define UNARY_INT(op)  { stk->_int_write() = op stk->_int(); }


void CodeSeg::doRun(variant* stk, const uchar* ip)
{
    variant* stkbase = stk;
    try
    {
        while (1)
        {
            switch(*ip++)
            {
            case opInv:     invOpcode(); break;
            case opEnd:     return;
            case opNop:     break;

            // Arithmetic
            // TODO: range checking in debug mode
            case opAdd:     BINARY_INT(+=); break;
            case opSub:     BINARY_INT(-=); break;
            case opMul:     BINARY_INT(*=); break;
            case opDiv:     BINARY_INT(/=); break;
            case opMod:     BINARY_INT(%=); break;
            case opBitAnd:  BINARY_INT(&=); break;
            case opBitOr:   BINARY_INT(|=); break;
            case opBitXor:  BINARY_INT(^=); break;
            case opBitShl:  BINARY_INT(<<=); break;
            case opBitShr:  BINARY_INT(>>=); break;
            case opNeg:     UNARY_INT(-); break;
            case opBitNot:  UNARY_INT(~); break;
            case opNot:     UNARY_INT(-); break;

            // Vector/string concatenation
            case opCharToStr:   *stk = str(1, stk->_uchar()); break;
            case opCharCat:     (stk - 1)->_str_write().push_back(stk->_uchar()); POP(stk); break;
            case opStrCat:      (stk - 1)->_str_write().append(stk->_str_read()); POP(stk); break;
            case opVarToVec:    *stk = new tuple(1, *stk); break;
            case opVarCat:      (stk - 1)->_tuple_write().push_back(*stk); POP(stk); break;
            case opVecCat:      (stk - 1)->_tuple_write().append(stk->_tuple_read()); POP(stk); break;

            // Safe typecasts
            case opToBool:  *stk = stk->to_bool(); break;
            case opToStr:   *stk = stk->to_string(); break;
            case opToType:  { Type* t = *(Type**)ip; ip += sizeof(Type*); t->runtimeTypecast(*stk); } break;
            case opDynCast: notimpl(); break; // TODO:

            // Const loaders
            case opLoadNull:        PUSH(stk, null); break;
            case opLoadFalse:       PUSH(stk, false); break;
            case opLoadTrue:        PUSH(stk, true); break;
            case opLoadChar:        PUSH(stk, *ip++); break;
            case opLoad0:           PUSH(stk, integer(0)); break;
            case opLoad1:           PUSH(stk, integer(1)); break;
            case opLoadInt:         PUSH(stk, *(integer*)ip); ip += sizeof(integer); break;
            case opLoadNullStr:     PUSH(stk, null_str); break;
            case opLoadNullRange:   PUSH(stk, new_range()); break;
            case opLoadNullTuple:   PUSH(stk, new_tuple()); break;
            case opLoadNullDict:    PUSH(stk, new_dict()); break;
            case opLoadNullOrdset:  PUSH(stk, new_ordset()); break;
            case opLoadNullSet:     PUSH(stk, new_set()); break;
            case opLoadNullFifo:    PUSH(stk, new_fifo()); break;
            case opLoadConst:       PUSH(stk, consts[*ip++]); break;
            case opLoadTypeRef:     PUSH(stk, *(object**)ip); ip += sizeof(object*); break;

            default: invOpcode(); break;
            }
        }
    }
    catch(exception& e)
    {
        while (stk <= stkbase)
            POP(stk);
        // TODO: stack is not freed here
        throw e;
    }
    assert(stk == stkbase + returns - 1);
}


void CodeSeg::run(varstack& stack)
{
    assert(closed);
    if (code.empty())
        return;
    doRun(stack.reserve(stksize) - 1, (const uchar*)code.data());
    stack.free(stksize - returns);
}


void ConstCode::run(variant& result)
{
    varstack stack;
    CodeSeg::run(stack);
    result = stack.top();
    stack.pop();
    assert(stack.size() == 0);
}



// --- CODE GENERATOR ------------------------------------------------------ //


DEF_EXCEPTION(ETypeMismatch, "Type mismatch")
DEF_EXCEPTION(EExprTypeMismatch, "Expression type mismatch")
DEF_EXCEPTION(EInvalidTypecast, "Invalid typecast")


class CodeGen: noncopyable
{
protected:

    struct stkinfo
    {
        Type* type;
        stkinfo(Type* t): type(t) { }
    };

    CodeSeg& codeseg;

    std::vector<stkinfo> genStack;
    varstack valStack;
    mem stkMax;
#ifdef DEBUG
    mem stkSize;
#endif

    void stkPush(Type* t, const variant& v);
    void stkPush(Type* t)
            { stkPush(t, null); }
    void stkPush(Constant* c)
            { stkPush(c->type, c->value); }
    const stkinfo& stkTop() const;
    Type* stkTopType() const
            { return stkTop().type; }
    const variant& topValue() const
            { return valStack.top(); }
    Type* stkPop();

    bool tryCast(Type*, Type*);
    
public:
    CodeGen(CodeSeg&);
    ~CodeGen();

    void endConstExpr(Type* expectType);
    void loadBool(bool);
    void loadChar(uchar);
    void loadInt(integer);
    void loadConst(Type*, const variant&);
    void arithmBinary(OpCode);
    void arithmUnary(OpCode);
    void elemToVec();
    void elemCat();
    void cat();
    void explicitCastTo(Type*);
    void implicitCastTo(Type*);
};


CodeGen::CodeGen(CodeSeg& _codeseg)
    : codeseg(_codeseg), stkMax(0)
#ifdef DEBUG
    , stkSize(0)
#endif    
{
    assert(codeseg.empty());
}


CodeGen::~CodeGen()  { }


void CodeGen::endConstExpr(Type* expectType)
{
    codeseg.close(stkMax, 1);
    Type* resultType = stkPop();
    if (expectType != NULL && !resultType->canCastImplTo(expectType))
        throw EExprTypeMismatch();
    assert(genStack.size() == 0);
}


void CodeGen::loadBool(bool v)
{
    codeseg.addOp(v ? opLoadTrue : opLoadFalse);
    stkPush(queenBee->defBool, v);
}


void CodeGen::loadChar(uchar v)
{
    codeseg.addOp(opLoadChar);
    codeseg.add8(v);
    stkPush(queenBee->defChar, v);
}


void CodeGen::loadInt(integer v)
{
    if (v == 0)
        codeseg.addOp(opLoad0);
    else if (v == 1)
        codeseg.addOp(opLoad1);
    else
    {
        codeseg.addOp(opLoadInt);
        codeseg.addInt(v);
    }
    stkPush(queenBee->defInt, v);
}


void CodeGen::loadConst(Type* type, const variant& value)
{
    if (value.is_nonpod())
    {
        mem n = codeseg.consts.size();
        codeseg.consts.push_back(value);
        if (n < 256)
        {
            codeseg.addOp(opLoadConst);
            codeseg.add8(uchar(n));
        }
        else if (n < 65536)
        {
            codeseg.addOp(opLoadConst2);
            codeseg.add16(ushort(n));
        }
        else
            throw emessage("Maximum number of constants in a block reached");
        stkPush(type);
    }
    else
    {
        switch (value.getType())
        {
        case variant::BOOL: loadBool(value.as_bool()); break;
        case variant::CHAR: loadChar(value.as_char()); break;
        case variant::INT:  loadInt(value.as_int()); break;
        default: _fatal(0x4001);
        }
    }
}


void CodeGen::arithmBinary(OpCode op)
{
    assert(op >= opAdd && op <= opBitShr);
    Type* t1 = stkPop(), * t2 = stkPop();
    if (!t1->isInt() || !t2->isInt())
        throw ETypeMismatch();
    codeseg.addOp(op);
    stkPush(t1);
}


void CodeGen::arithmUnary(OpCode op)
{
    assert(op >= opNeg && op <= opNot);
    if (!stkTopType()->isInt())
        throw ETypeMismatch();
    codeseg.addOp(op);
}


void CodeGen::elemToVec()
{
    Type* elemType = stkPop();
    codeseg.addOp(elemType->isChar() ? opCharToStr : opVarToVec);
    stkPush(elemType->deriveVector());
}


void CodeGen::elemCat()
{
    Type* elemType = stkPop();
    Type* vecType = stkTopType();
    if (!vecType->isVector())
        throw emessage("Vector type expected");
    if (!elemType->canCastImplTo(PVector(vecType)->elem))
        throw ETypeMismatch();
    codeseg.addOp(elemType->isChar() ? opCharCat : opVarCat);
}


void CodeGen::cat()
{
    Type* right = stkPop();
    Type* left = stkTopType();
    if (!left->isVector() || !right->isVector())
        throw emessage("Vector type expected");
    if (!right->canCastImplTo(left))
        throw ETypeMismatch();
    codeseg.addOp(PVector(left)->elem->isChar() ? opStrCat : opVecCat);
}


// Try implicit cast and return true if succeeded. This code is shared
// between the explicit and implicit typecast routines.
bool CodeGen::tryCast(Type* from, Type* to)
{
    if (to->isVariant())
        ;   // everything in the VM is a variant anyway
    else if (to->isBool())
        codeseg.addOp(opToBool);    // everything can be cast to bool
    else if (to->isString())
        codeseg.addOp(opToStr);     // ... as well as to string
    else if (from->canCastImplTo(to))
        ;   // means variant copying is considered safe, including State casts (not implemented yet)
    else
        return false;
    return true;
}


void CodeGen::explicitCastTo(Type* to)
{
    if (stkTopType()->identicalTo(to))
        return;
    Type* from = stkPop();
    if (tryCast(from, to))
        ;   // implicit typecast does the job
    else if (from->isVariant() || (from->isOrdinal() && to->isOrdinal()))
    {
        // Ordinals must be casted at runtime so that the variant type of the
        // value on the stack is correct for subsequent operations.
        codeseg.addOp(opToType);    // calls to->runtimeTypecast(v)
        codeseg.addPtr(to);
    }
    else if (from->isState() && to->isState())
    {
        // Implicit type cast wasn't possible, so try run-time typecast
        codeseg.addOp(opDynCast);
        codeseg.addPtr(to);
    }
    else
        throw EInvalidTypecast();
    stkPush(to);
}


void CodeGen::implicitCastTo(Type* to)
{
    if (stkTopType()->identicalTo(to))
        return;
    Type* from = stkPop();
    if (!tryCast(from, to))
        throw ETypeMismatch();
    stkPush(to);
}


void CodeGen::stkPush(Type* type, const variant& value)
{
    genStack.push_back(stkinfo(type));
    valStack.push(value);
    if (genStack.size() > stkMax)
        stkMax = genStack.size();
#ifdef DEBUG
    stkSize++;
#endif
}


const CodeGen::stkinfo& CodeGen::stkTop() const
    { return genStack.back(); }


Type* CodeGen::stkPop()
{
    assert(genStack.size() == valStack.size() && genStack.size() > 0);
    Type* result = genStack.back().type;
    genStack.pop_back();
    valStack.pop();
#ifdef DEBUG
    stkSize--;
#endif
    return result;
}


// --- varlist ------------------------------------------------------------- //


// --- tests --------------------------------------------------------------- //


#define check(x) assert(x)


int main()
{
    initTypeSys();
    try
    {
        Parser parser("x", new in_text("x"));

        Module m("test", 0, NULL);
        Constant* c = m.addConstant("c", queenBee->defChar, char(1));

        // Arithmetic, typecasts
        variant r;
        ConstCode seg;
        {
            CodeGen gen(seg);
            gen.loadConst(c->type, c->value);
            gen.explicitCastTo(queenBee->defVariant);
            gen.explicitCastTo(queenBee->defBool);
            gen.explicitCastTo(queenBee->defInt);
            gen.loadInt(9);
            gen.arithmBinary(opAdd);
            gen.loadInt(2);
            gen.arithmUnary(opNeg);
            gen.arithmBinary(opSub);
            gen.explicitCastTo(queenBee->defStr);
            gen.endConstExpr(queenBee->defStr);
        }
        seg.run(r);
        check(r.as_str() == "12");
        
        // String operations
        c = m.addConstant("s", queenBee->defStr, "ef");
        seg.clear();
        {
            CodeGen gen(seg);
            gen.loadChar('a');
            gen.elemToVec();
            gen.loadChar('b');
            gen.elemCat();
            gen.loadConst(queenBee->defStr, "cd");
            gen.cat();
            gen.loadConst(c->type, c->value);
            gen.cat();
            gen.endConstExpr(queenBee->defStr);
        }
        seg.run(r);
        check(r.as_str() == "abcdef");
        
        // Vector concatenation
        tuple* t = new tuple(1, 3);
        t->push_back(4);
        c = m.addConstant("v", queenBee->defInt->deriveVector(), t);
        seg.clear();
        {
            CodeGen gen(seg);
            gen.loadInt(1);
            gen.elemToVec();
            gen.loadInt(2);
            gen.elemCat();
            gen.loadConst(c->type, c->value);
            gen.cat();
            gen.endConstExpr(queenBee->defInt->deriveVector());
        }
        seg.run(r);
        check(r.to_string() == "[1, 2, 3, 4]");
    }
    catch (std::exception& e)
    {
        ferr << "Exception: " << e.what() << endl;
        return 101;
    }
    doneTypeSys();
#ifdef DEBUG
    assert(object::alloc == 0);
#endif
}

