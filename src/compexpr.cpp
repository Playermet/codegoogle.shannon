
#include "vm.h"
#include "compiler.h"


// --- EXPRESSION ---------------------------------------------------------- //

/*
    <nested-expr>  <ident>  <number>  <string>  <char>  <type-spec>
        <vec-ctor>  <dict-ctor>  <if-func>  <typeof>
    @ <array-sel>  <member-sel>  <function-call>  ^
    unary-  ?  #  as  is
    |
    *  /  mod
    +  –
    ==  <>  <  >  <=  >=  in
    not
    and
    or  xor
    range, enum
*/


void Compiler::enumeration(const str& firstIdent)
{
    Enumeration* enumType = state->registerType(new Enumeration());
    enumType->addValue(state, scope, firstIdent);
    expect(tokComma, ",");
    do
    {
        enumType->addValue(state, scope, getIdentifier());
    }
    while (skipIf(tokComma));
    codegen->loadTypeRef(enumType);
}


Type* Compiler::getTypeDerivators(Type* type)
{
    // TODO: anonymous functions are static, named ones are not
    if (skipIf(tokLSquare))  // container
    {
        if (skipIf(tokRSquare))
            return getTypeDerivators(type)->deriveVec(state);
        else
        {
            Type* indexType = getTypeValue(false);
            expect(tokRSquare, "]");
            if (indexType->isVoid())
                return getTypeDerivators(type)->deriveVec(state);
            else
                return getTypeDerivators(type)->deriveContainer(state, indexType);
        }
    }

    else if (skipIf(tokLessThan))  // fifo
    {
        expect(tokGreaterThan, "'>'");
        return getTypeDerivators(type)->deriveFifo(state);
    }

    else if (skipIf(tokLParen))  // prototype/function
    {
        Prototype* proto = state->registerType(new Prototype(type));
        if (!skipIf(tokRParen))
        {
            do
            {
                Type* argType = getTypeValue(true);
                str ident = getIdentifier();
                argType = getTypeDerivators(argType);
                proto->addFormalArg(ident, argType);
            }
            while (skipIf(tokComma));
            expect(tokRParen, "')'");
        }
        // TODO: nope, it should look up the next token and see if it's a block
        // if (token == tokRParen)
        //     return proto;
        State* newState = state->registerType(new State(state, proto));
        stateBody(newState);
        return newState;
    }

    else if (skipIf(tokCaret)) // ^
    {
        type = getTypeDerivators(type);
        if (type->isReference())
            error("Double reference");
        if (!type->isDerefable())
            error("Reference can not be derived from this type");
        return type->getRefType();
    }

    return type;
}


void Compiler::identifier(const str& ident)
{
    // Go up the current scope hierarchy within the module
    Scope* sc = scope;
    do
    {
        // TODO: implement loading from outer scopes
        Symbol* sym = sc->find(ident);
        if (sym)
        {
            codegen->loadSymbol(sym);
            return;
        }
        sc = sc->outer;
    }
    while (sc != NULL);

    // Look up in used modules; search backwards
    for (memint i = module.usedModuleInsts.size(); i--; )
    {
        SelfVar* m = module.usedModuleInsts[i];
        Symbol* sym = m->getModuleType()->find(ident);
        if (sym)
        {
            if (codegen->isCompileTime())
                codegen->loadSymbol(sym);
            else
            {
                memint offs = codegen->getCurrentOffs();
                codegen->loadVariable(m);
                codegen->loadMember(sym, &offs);
            }
            return;
        }
    }

    throw EUnknownIdent(ident);
}


void Compiler::vectorCtor(Type* typeHint)
{
    if (typeHint && !typeHint->isAnyVec())
        error("Vector constructor not expected here");
    Container* type = PContainer(typeHint);
    if (skipIf(tokRSquare))
    {
        codegen->loadEmptyConst(type ? type : queenBee->defNullCont);
        return;
    }
    expression(type ? type->elem : NULL);
    type = codegen->elemToVec(type);
    while (skipIf(tokComma))
    {
        expression(type->elem);
        codegen->elemCat();
    }
    expect(tokRSquare, "]");
}


void Compiler::dictCtor(Type* typeHint)
{
    if (typeHint && !typeHint->isAnySet() && !typeHint->isAnyDict())
        error("Set/dict constructor not expected here");
    Container* type = PContainer(typeHint);
    if (skipIf(tokRCurly))
    {
        codegen->loadEmptyConst(type ? type : queenBee->defNullCont);
        return;
    }

    expression(type ? type->index : NULL);

    // Dictionary
    if (skipIf(tokAssign))
    {
        expression(type ? type->elem : NULL);
        type = codegen->pairToDict();
        while (skipIf(tokComma))
        {
            expression(type->index);
            codegen->checkDictKey();
            expect(tokAssign, "=");
            expression(type->elem);
            codegen->dictAddPair();
        }
    }

    // Set
    else
    {
        if (skipIf(tokRange))
        {
            expression(type ? type->index : NULL);
            type = codegen->rangeToSet();
        }
        else
            type = codegen->elemToSet();
        while (skipIf(tokComma))
        {
            expression(type->index);
            if (skipIf(tokRange))
            {
                codegen->checkRangeLeft();
                expression(type->index);
                codegen->setAddRange();
            }
            else
                codegen->setAddElem();
        }
    }

    expect(tokRCurly, "}");
}


void Compiler::typeOf()
{
    memint undoOffs = codegen->getCurrentOffs();
    designator(defTypeRef);
    Type* type = codegen->getTopType();
    codegen->undoDesignator(undoOffs);
    codegen->loadTypeRef(type);
}


void Compiler::ifFunc()
{
    expect(tokLParen, "(");
    expression(queenBee->defBool);
    memint jumpFalse = codegen->boolJumpForward(opJumpFalse);
    expect(tokComma, ",");
    expression(NULL);
    Type* exprType = codegen->getTopType();
    codegen->justForget(); // will get the expression type from the second branch
    memint jumpOut = codegen->jumpForward();
    codegen->resolveJump(jumpFalse);
    expect(tokComma, ",");
    expression(exprType);
    codegen->resolveJump(jumpOut);
    expect(tokRParen, ")");
}


void Compiler::actualArgs(Prototype* proto)
{
    // TODO: named arguments (?), default arguments
    if (!proto->returnType->isVoid())
        codegen->loadEmptyConst(proto->returnType);
    memint i = 0;
    while (i < proto->formalArgs.size())
    {
        FormalArg* arg = proto->formalArgs[i];
        expression(arg->type);
        i++;
        if (i == proto->formalArgs.size())
            break;
        expect(tokComma, "','");
    }
    expect(tokRParen, "')'");
}


void Compiler::atom(Type* typeHint)
{
    if (token == tokPrevIdent)  // from partial (typeless) definition
    {
        identifier(getPrevIdent());
        redoIdent();
    }

    else if (token == tokIntValue)
    {
        codegen->loadConst(queenBee->defInt, integer(intValue));
        next();
    }

    else if (token == tokStrValue)
    {
        str value = strValue;
        if (value.size() == 1)
            codegen->loadConst(queenBee->defChar, value[0]);
        else
        {
            module.registerString(value);
            codegen->loadConst(queenBee->defStr, value);
        }
        next();
    }

    else if (token == tokIdent)
    {
        identifier(strValue);
        next();
    }

    else if (skipIf(tokLParen))
    {
        if (codegen->isCompileTime())
            constExpr(typeHint);
        else
            expression(typeHint);
        expect(tokRParen, "')'");
    }

    else if (skipIf(tokLSquare))
        vectorCtor(typeHint);

    else if (skipIf(tokLCurly))
        dictCtor(typeHint);

    else if (skipIf(tokIf))
        ifFunc();

    else if (skipIf(tokTypeOf))
        typeOf();

    else if (skipIf(tokThis))
        codegen->loadThis();

    else
        error("Expression syntax");

    while (token == tokWildcard)
    {
        Type* type = codegen->tryUndoTypeRef();
        if (type != NULL)
        {
            next(); // *
            codegen->loadTypeRef(getTypeDerivators(type));
        }
        else
            break;
    }
}


void Compiler::designator(Type* typeHint)
{
    memint undoOffs = codegen->getCurrentOffs();
    bool isAt = skipIf(tokAt);
    Type* refTypeHint = typeHint && typeHint->isReference() ? PReference(typeHint)->to : NULL;

    atom(refTypeHint ? refTypeHint : typeHint);

    while (1)
    {
        if (skipIf(tokPeriod))
        {
            // undoOffs is needed when the member is a constant, in which case
            // all previous loads should be discarded - they are not needed to
            // load a constant
            codegen->deref();
            codegen->loadMember(getIdentifier(), &undoOffs);
        }

        else if (skipIf(tokLSquare))
        {
            codegen->deref();
            expression(NULL);
            if (skipIf(tokRange))
            {
                if (token == tokRSquare)
                    codegen->loadConst(defVoid, variant());
                else
                    expression(codegen->getTopType());
                codegen->loadSubvec();
            }
            else
                codegen->loadContainerElem();
            expect(tokRSquare, "]");
        }

        else if (skipIf(tokLParen))
        {
            Type* type = codegen->tryUndoTypeRef();
            if (type->isAnyState())
            {
                actualArgs(PState(type)->prototype);
                codegen->call(PState(type));
                if (PState(type)->returnVar == NULL)
                    throw evoidfunc();
            }
            else
                error("Not a function");
        }
/*
        else if (skipIf(tokCaret))
        {
            if (!codegen->getTopType()->isReference())
                error("'^' has no effect");
            codegen->deref();
        }
*/
        else
            break;
    }

    if (isAt || refTypeHint)
        codegen->mkref();
    else
        codegen->deref();
}


void Compiler::factor(Type* typeHint)
{
    bool isNeg = skipIf(tokMinus);
    bool isQ = skipIf(tokQuestion);
    bool isLen = skipIf(tokSharp);

    memint undoOffs = codegen->getCurrentOffs();
    designator(typeHint);

    if (isLen)
        codegen->length();
    if (isQ)
        codegen->nonEmpty();
    if (isNeg)
        codegen->arithmUnary(opNeg);
    if (skipIf(tokAs))
    {
        Type* type = getTypeValue(true);
        // TODO: default value in parens?
        codegen->explicitCast(type);
    }
    if (skipIf(tokIs))
        codegen->isType(getTypeValue(true), undoOffs);
}


void Compiler::concatExpr(Container* contType)
{
    factor(contType);
    if (skipIf(tokCat))
    {
        Type* top = codegen->getTopType();
        if (top->isAnyVec())
            if (contType)
                codegen->implicitCast(contType);
            else
                contType = PContainer(top);
        else
            contType = codegen->elemToVec(contType);
        do
        {
            factor(contType);
            if (codegen->tryImplicitCast(contType))
                codegen->cat();
            else
                codegen->elemCat();
        }
        while (skipIf(tokCat));
    }
}


void Compiler::term()
{
    concatExpr(NULL);
    while (token == tokMul || token == tokDiv || token == tokMod)
    {
        OpCode op = token == tokMul ? opMul
            : token == tokDiv ? opDiv : opMod;
        next();
        factor(NULL);
        codegen->arithmBinary(op);
    }
}


void Compiler::arithmExpr()
{
    term();
    while (token == tokPlus || token == tokMinus)
    {
        OpCode op = token == tokPlus ? opAdd : opSub;
        next();
        term();
        codegen->arithmBinary(op);
    }
}


void Compiler::relation()
{
    arithmExpr();
    if (skipIf(tokIn))
    {
        arithmExpr();
        Type* right = codegen->getTopType();
        if (right->isTypeRef())
            codegen->inBounds();
        else if (right->isAnyCont())
            codegen->inCont();
        else if (right->isAnyOrd() && skipIf(tokRange))
        {
            arithmExpr();
            codegen->inRange();
        }
        else
            error("Operator 'in' expects container, numeric range, or ordinal type ref");
    }
    else if (token >= tokEqual && token <= tokGreaterEq)
    {
        OpCode op = OpCode(opEqual + int(token - tokEqual));
        next();
        arithmExpr();
        codegen->cmp(op);
    }
}


void Compiler::notLevel()
{
    bool isNot = skipIf(tokNot);
    relation();
    if (isNot)
        codegen->_not();
}


void Compiler::andLevel()
{
    notLevel();
    while (token == tokShl || token == tokShr || token == tokAnd)
    {
        Type* type = codegen->getTopType();
        if (type->isBool() && skipIf(tokAnd))
        {
            memint offs = codegen->boolJumpForward(opJumpAnd);
            andLevel();
            codegen->resolveJump(offs);
            break;
        }
        else // if (type->isInt())
        {
            OpCode op = token == tokShl ? opBitShl
                    : token == tokShr ? opBitShr : opBitAnd;
            next();
            notLevel();
            codegen->arithmBinary(op);
        }
    }
}


void Compiler::orLevel()
{
    andLevel();
    while (token == tokOr || token == tokXor)
    {
        Type* type = codegen->getTopType();
        // TODO: boolean XOR? Beautiful thing, but not absolutely necessary
        if (type->isBool() && skipIf(tokOr))
        {
            memint offs = codegen->boolJumpForward(opJumpOr);
            orLevel();
            codegen->resolveJump(offs);
            break;
        }
        else // if (type->isInt())
        {
            OpCode op = token == tokOr ? opBitOr : opBitXor;
            next();
            andLevel();
            codegen->arithmBinary(op);
        }
    }
}


void Compiler::caseValue(Type* ctlType)
{
    expression(ctlType);
    if (skipIf(tokRange))
    {
        expression(ctlType);
        codegen->caseInRange();
    }
    else
        codegen->caseCmp();
    if (skipIf(tokComma))
    {
        memint offs = codegen->boolJumpForward(opJumpOr);
        caseValue(ctlType);
        codegen->resolveJump(offs);
    }
}


void Compiler::expression(Type* expectType)
{
    if (expectType == NULL || expectType->isBool())
        orLevel();
    else if (expectType->isAnyCont())
        concatExpr(PContainer(expectType));
    else if (expectType->isReference())
        designator(expectType);
    else
        arithmExpr();
    if (expectType)
        codegen->implicitCast(expectType);
}


void Compiler::constExpr(Type* expectType)
{
    assert(codegen->isCompileTime());
    if (token == tokIdent)  // Enumeration maybe?
    {
        str ident = strValue;
        if (next() != tokComma)
        {
            undoIdent(ident);
            goto ICouldHaveDoneThisWithoutGoto;
        }
        enumeration(ident);
    }
    else
    {
ICouldHaveDoneThisWithoutGoto:
        expression(expectType == NULL || expectType->isTypeRef() ? NULL : expectType);
        if (skipIf(tokRange))  // Subrange
        {
            expression(NULL);
            codegen->createSubrangeType();
        }
    }
    if (expectType)
        codegen->implicitCast(expectType);
}


Type* Compiler::getConstValue(Type* expectType, variant& result, bool atomType)
{
    CodeSeg constCode(NULL);
    CodeGen constCodeGen(constCode, state, true);
    CodeGen* prevCodeGen = exchange(codegen, &constCodeGen);
    Type* resultType = NULL;
    try
    {
        if (atomType)
            designator(expectType);
        else
            constExpr(expectType);
        if (codegen->getTopType()->isReference())
            error("References not allowed in const expressions");
        resultType = constCodeGen.runConstExpr(expectType, result);
        codegen = prevCodeGen;
    }
    catch(exception&)
    {
        codegen = prevCodeGen;
        throw;
    }
    return resultType;
}


Type* Compiler::getTypeValue(bool atomType)
{
    // atomType excludes enums and subrange type definitions and thus shorthens
    // the parsing path
    variant result;
    getConstValue(defTypeRef, result, atomType);
    return cast<Type*>(result._rtobj());
}
