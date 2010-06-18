
#include "vm.h"


// TODO: store the current file name in a named const, say __FILE__


CodeGen::CodeGen(CodeSeg& c, State* treg)
    : codeOwner(c.getStateType()), typeReg(treg), codeseg(c), locals(0), isConstCode(true)
        { if (treg == NULL) treg = codeOwner; }


CodeGen::~CodeGen()
    { }


void CodeGen::error(const char* msg)
    { throw emessage(msg); }


void CodeGen::error(const str& msg)
    { throw emessage(msg); }


void CodeGen::addOp(Type* type, OpCode op)
{
    simStack.push_back(SimStackItem(type, codeseg.size()));
    if (simStack.size() > codeseg.stackSize)
        codeseg.stackSize = simStack.size();
    addOp(op);
}


void CodeGen::undoLastLoad()
{
    memint offs = stkTopItem().offs;
    assert(offs >= 0 && offs < codeseg.size());
    if (isUndoableLoadOp(OpCode(codeseg[offs])))
    {
        stkPop();
        codeseg.resize(offs);
    }
    else
        // discard();
        notimpl();
}


Type* CodeGen::stkPop()
{
    Type* result = simStack.back().type;
    simStack.pop_back();
    return result;
}


void CodeGen::stkReplaceTop(Type* t)
{
    memint offs = stkTopItem().offs;
    simStack.pop_back();
    simStack.push_back(SimStackItem(t, offs));
}


bool CodeGen::tryImplicitCast(Type* to)
{
    Type* from = stkTop();

    if (from == to)
        return true;

    if (to->isVariant() || from->canAssignTo(to))
    {
        // canAssignTo() should take care of polymorphic typecasts
        stkReplaceTop(to);
        return true;
    }

    // TODO: automatic deref and mkref?

    // Vector elements are automatically converted to vectors when necessary,
    // e.g. char -> str
    if (to->isAnyVec() && from->identicalTo(PContainer(to)->elem))
    {
        elemToVec();
        return true;
    }

    if (from->isNullCont() && to->isAnyCont())
    {
        undoLastLoad();
        loadEmptyCont(PContainer(to));
        return true;
    }

    return false;
}


void CodeGen::implicitCast(Type* to, const char* errmsg)
{
    // TODO: better error message, something like <type> expected; use Type::dump()
    if (!tryImplicitCast(to))
        error(errmsg == NULL ? "Type mismatch" : errmsg);
}


void CodeGen::explicitCast(Type* to)
{
    if (tryImplicitCast(to))
        return;

    Type* from = stkTop();

    if (from->isAnyOrd() && to->isAnyOrd())
        stkReplaceTop(to);
    
    // TODO: better error message with type defs
    error("Invalid explicit typecast");
}


void CodeGen::deinitLocalVar(Variable* var)
{
    // This is called from BlockScope.
    // TODO: don't generate POPs if at the end of a function in RELEASE mode
    assert(var->isLocalVar());
    assert(locals == simStack.size());
    if (var->id != locals - 1)
        fatal(0x6002, "Invalid local var id");
    locals--;
    popValue();
}


void CodeGen::popValue()
{
    stkPop();
    addOp(opPop);
}


memint CodeGen::beginDiscardable()
    { return codeseg.size(); }


void CodeGen::endDiscardable(memint offs)
{
    assert(stkSize() == 0 || stkTopItem().offs < offs);
    codeseg.resize(offs);
}


Type* CodeGen::tryUndoTypeRef()
{
    memint offs = stkTopItem().offs;
    if (codeseg[offs] == opLoadTypeRef)
    {
        Type* type = codeseg.at<Type*>(offs + 1);
        stkPop();
        codeseg.resize(offs);
        return type;
    }
    else
        return NULL;
}


bool CodeGen::deref(bool autoDeref)
{
    Type* type = stkTop();
    if (!type->isReference())
        return false;
    type = type->getValueType();
    if (type->isDerefable())
    {
        stkPop();
        addOp(type, autoDeref ? opAutoDeref : opDeref);
    }
    else
        notimpl();
    return true;
}


void CodeGen::nonEmpty()
{
    Type* type = stkTop();
    if (!type->isBool())
    {
        stkPop();
        addOp(queenBee->defBool, opNonEmpty);
    }
}


void CodeGen::loadTypeRef(Type* type)
{
    addOp<Type*>(defTypeRef, opLoadTypeRef, type);
}


void CodeGen::loadConst(Type* type, const variant& value)
{
    // NOTE: compound consts should be held by a smart pointer somewhere else
    switch(value.getType())
    {
    case variant::VOID:
        addOp(type, opLoadNull);
        return;
    case variant::ORD:
        {
            assert(type->isAnyOrd());
            integer i = value._int();
            if (i == 0)
                addOp(type, opLoad0);
            else if (i == 1)
                addOp(type, opLoad1);
            else if (uinteger(i) <= 255)
                addOp<uchar>(type, opLoadByte, i);
            else
                addOp<integer>(type, opLoadOrd, i);
        }
        return;
    case variant::REAL:
        notimpl();
        break;
    case variant::VARPTR:
        break;    
    case variant::STR:
        assert(type->isByteVec());
        addOp<object*>(type, opLoadStr, value._str().obj);
        return;
    case variant::VEC:
    case variant::SET:
    case variant::ORDSET:
    case variant::DICT:
    case variant::REF:
        break;
    case variant::RTOBJ:
        if (value._rtobj()->getType()->isTypeRef())
        {
            loadTypeRef(cast<Type*>(value._rtobj()));
            return;
        }
        break;
    }
    fatal(0x6001, "Internal: unknown constant literal");
}


void CodeGen::loadDefinition(Definition* def)
{
    Type* type = def->type;
    if (type->isTypeRef() || type->isVoid() || def->type->isAnyOrd() || def->type->isByteVec())
        loadConst(def->type, def->value);
    else
        addOp<Definition*>(def->type, opLoadConst, def);
}


static variant::Type typeToVarType(Container* t)
{
    // Currently only works for containers
    switch (t->typeId)
    {
    case Type::NULLCONT:
        return variant::VOID;
    case Type::VEC:
        return t->isByteVec() ? variant::STR : variant::VEC;
    case Type::SET:
        return t->isByteSet() ? variant::ORDSET : variant::SET;
    case Type::DICT:
        return t->isByteDict() ? variant::VEC : variant::DICT;
    default:
        notimpl();
        return variant::VOID;
    }
}


void CodeGen::loadEmptyCont(Container* contType)
    { addOp<char>(contType, opLoadEmptyVar, typeToVarType(contType)); }


void CodeGen::loadSymbol(Variable* moduleVar, Symbol* sym)
{
    if (sym->isDefinition())
        loadDefinition(PDefinition(sym));
    else if (sym->isVariable())
    {
        if (moduleVar != NULL)
        {
            loadVariable(moduleVar);
            loadMember(PVariable(sym));
        }
        else
            loadVariable(PVariable(sym));
    }
    else
        notimpl();
}


void CodeGen::loadVariable(Variable* var)
{
    assert(var->host != NULL);
    assert(var->id >= 0 && var->id <= 127);
    if (codeOwner == NULL)
        error("Variables not allowed in constant expressions");
    else
        isConstCode = false;
    // TODO: check parent states too
    if (var->isSelfVar() && var->host == codeOwner->selfPtr)
        addOp<char>(var->type, opLoadSelfVar, var->id);
    else if (var->isLocalVar() && var->host == codeOwner)
        addOp<char>(var->type, opLoadStkVar, var->id);
    else
        notimpl();
}


void CodeGen::loadMember(Variable* var)
{
    if (codeOwner == NULL)
        error("Variables not allowed in constant expressions");
    else
        isConstCode = false;
    Type* stateType = stkPop();
    // TODO: check parent states too
    if (!stateType->isAnyState() || var->host != stateType
            || !var->isSelfVar())
        error("Invalid member selection");
    addOp<char>(var->type, opLoadMember, var->id);
}


void CodeGen::loadMember(const str& ident)
{
    Type* stateType = stkTop();
    if (!stateType->isAnyState())
        error("Invalid member selection");
    Symbol* sym = PState(stateType)->findShallow(ident);
    if (sym->isVariable())
        loadMember(PVariable(sym));
    else if (sym->isDefinition())
    {
        undoLastLoad();
        loadDefinition(PDefinition(sym));
    }
    else
        notimpl();
}


void CodeGen::storeRet(Type* type)
{
    implicitCast(type);
    stkPop();
    addOp<char>(opInitStkVar, codeOwner ? codeOwner->prototype->retVarId() : -1);
}


void CodeGen::loadContainerElem()
{
    // This is square brackets op - can be string, vector, array or dictionary.
    OpCode op = opInv;
    Type* contType = stkTop(2);
    if (contType->isAnyVec())
    {
        implicitCast(queenBee->defInt, "Vector index must be integer");
        op = contType->isByteVec() ? opStrElem : opVecElem;
    }
    else if (contType->isAnyDict())
    {
        implicitCast(PContainer(contType)->index, "Dictionary key type mismatch");
        op = contType->isByteDict() ? opByteDictElem : opDictElem;
    }
    else
        error("Vector/dictionary type expected");
    stkPop();
    stkPop();
    addOp(PContainer(contType)->elem, op);
}


void CodeGen::length()
{
    Type* type = stkTop();
    if (type->isNullCont())
    {
        undoLastLoad();
        loadConst(queenBee->defInt, 0);
    }
    else if (type->isAnyVec())
    {
        stkPop();
        addOp(queenBee->defInt, type->isByteVec() ? opStrLen : opVecLen);
    }
    else
        error("'#' expects vector or string");
}


Container* CodeGen::elemToVec()
{
    Type* elemType = stkPop();
    Container* contType = elemType->deriveVec(typeReg);
    addOp(contType, contType->isByteVec() ? opChrToStr : opVarToVec);
    return contType;
}


void CodeGen::elemCat()
{
    Type* vecType = stkTop(2);
    if (!vecType->isAnyVec())
        error("Vector/string type expected");
    implicitCast(PContainer(vecType)->elem, "Vector/string element type mismatch");
    stkPop();
    addOp(vecType->isByteVec() ? opChrCat: opVarCat);
}


void CodeGen::cat()
{
    Type* vecType = stkTop(2);
    if (!vecType->isAnyVec())
        error("Left operand is not a vector");
    implicitCast(vecType, "Vector/string types do not match");
    stkPop();
    addOp(vecType->isByteVec() ? opStrCat : opVecCat);
}


void CodeGen::elemToSet()
{
    Type* elemType = stkPop();
    Container* setType = elemType->deriveSet(typeReg);
    addOp(setType, setType->isByteSet() ? opElemToByteSet : opElemToSet);
}


void CodeGen::rangeToSet()
{
    implicitCast(stkTop(2), "Incompatible range bounds");
    stkPop();
    Type* left = stkPop();
    if (!left->isAnyOrd())
        error("Non-ordinal range bounds");
    Container* setType = left->deriveSet(typeReg);
    if (!setType->isByteSet())
        error("Invalid element type for ordinal set");
    addOp(setType, opRngToByteSet);
}


void CodeGen::setAddElem()
{
    Type* setType = stkTop(2);
    if (!setType->isAnySet())
        error("Set type expected");
    implicitCast(PContainer(setType)->index, "Set element type mismatch");
    stkPop();
    addOp(setType->isByteSet() ? opByteSetAddElem : opSetAddElem);
}


void CodeGen::checkRangeLeft()
{
    Type* setType = stkTop(2);
    if (!setType->isByteSet())
        error("Byte set type expected");
    implicitCast(PContainer(setType)->index, "Set element type mismatch");
}


void CodeGen::setAddRange()
{
    Type* setType = stkTop(3);
    if (!setType->isByteSet())
        error("Byte set type expected");
    implicitCast(PContainer(setType)->index, "Set element type mismatch");
    stkPop();
    stkPop();
    addOp(opByteSetAddRng);
}


void CodeGen::pairToDict()
{
    Type* val = stkPop();
    Type* key = stkPop();
    Container* dictType = val->deriveContainer(typeReg, key);
    addOp(dictType, dictType->isByteDict() ? opPairToByteDict : opPairToDict);
}


void CodeGen::checkDictKey()
{
    Type* dictType = stkTop(2);
    if (!dictType->isAnyDict())
        error("Dictionary type expected");
    implicitCast(PContainer(dictType)->index, "Dictionary key type mismatch");
}


void CodeGen::dictAddPair()
{
    Type* dictType = stkTop(3);
    if (!dictType->isAnyDict())
        error("Dictionary type expected");
    implicitCast(PContainer(dictType)->elem, "Dictionary element type mismatch");
    stkPop();
    stkPop();
    addOp(dictType->isByteDict() ? opByteDictAddPair : opDictAddPair);
}


void CodeGen::arithmBinary(OpCode op)
{
    assert(op >= opAdd && op <= opBitShr);
    Type* right = stkPop();
    Type* left = stkPop();
    if (!right->isInt() || !left->isInt())
        error("Operand types do not match binary operator");
    addOp(left->identicalTo(right) ? left : queenBee->defInt, op);
}


void CodeGen::arithmUnary(OpCode op)
{
    assert(op >= opNeg && op <= opNot);
    Type* type = stkTop();
    if (!type->isInt())
        error("Operand type doesn't match unary operator");
    addOp(op);
}


void CodeGen::cmp(OpCode op)
{
    assert(isCmpOp(op));
    Type* left = stkTop(2);
    implicitCast(left, "Type mismatch in comparison");
    Type* right = stkTop();
    if (left->isAnyOrd() && right->isAnyOrd())
        addOp(opCmpOrd);
    else if (left->isByteVec() && right->isByteVec())
        addOp(opCmpStr);
    else
    {
        if (op != opEqual && op != opNotEq)
            error("Only equality can be tested for this type");
        addOp(opCmpVar);
    }
    stkPop();
    stkPop();
    addOp(queenBee->defBool, op);
}


void CodeGen::_not()
{
    Type* type = stkTop();
    if (type->isInt())
        addOp(opBitNot);
    else
    {
        implicitCast(queenBee->defBool, "Boolean or integer operand expected");
        addOp(opNot);
    }
}

/*
void CodeGen::boolXor()
{
    if (!stkPop()->isBool() || !stkPop()->isBool())
        error("Operand types do not match binary operator");
    addOp(queenBee->defBool, opBoolXor);
}
*/

memint CodeGen::boolJumpForward(OpCode op)
{
    assert(isBoolJump(op));
    implicitCast(queenBee->defBool, "Boolean expression expected");
    stkPop();
    return jumpForward(op);
}


memint CodeGen::jumpForward(OpCode op)
{
    assert(isJump(op));
    memint pos = codeseg.size();
    addOp(op);
    addJumpOffs(0);
    return pos;
}


void CodeGen::resolveJump(memint jumpOffs)
{
    assert(jumpOffs <= codeseg.size() - 1 - memint(sizeof(jumpoffs)));
    assert(isJump(OpCode(codeseg.at<uchar>(jumpOffs))));
    integer offs = integer(codeseg.size()) - integer(jumpOffs + 1 + sizeof(jumpoffs));
    if (offs > 32767)
        error("Jump target is too far away");
    codeseg.atw<jumpoffs>(jumpOffs + 1) = offs;
}


void CodeGen::assertion(const str& cond, const str& fileName, integer line)
{
    implicitCast(queenBee->defBool, "Boolean expression expected for 'assert'");
    stkPop();
    addOp(opAssert, cond.obj);
    add(fileName.obj);
    add(line);
}


void CodeGen::dumpVar(const str& expr)
{
    Type* type = stkPop();
    addOp(opDump, expr.obj);
    add(type);
}


void CodeGen::end()
{
    codeseg.close();
    assert(simStack.size() - locals == 0);
}


