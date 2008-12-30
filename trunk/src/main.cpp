
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#include "str.h"
#include "except.h"
#include "langobj.h"
#include "vm.h"


// ------------------------------------------------------------------------- //
// --- HIS MAJESTY THE COMPILER -------------------------------------------- //
// ------------------------------------------------------------------------- //


string typeVsType(ShType* a, ShType* b)
{
    return a->getDefinitionQ() + " vs. " + b->getDefinitionQ();
}


// --- EXPRESSION ---------------------------------------------------------- //

/*
    <nested-expr>, <typecast>, <ident>, <number>, <string>, <char>,
        true, false, null, compound-ctor
    <array-sel>, <fifo-sel>, <function-call>, <mute>
    -, not
    *, /, mod, as
    +, –
    ++
    ==, <>, != <, >, <=, >=, in, is
    and
    or, xor
    ..
    ,
*/

// TODO: execute constant code as much as possible (in VmCodeGen probably)

ShBase* ShModule::getQualifiedName()
{
    string ident = parser.getIdent();
    ShBase* obj = symbolScope->deepFind(ident);
    if (obj == NULL)
        errorNotFound(ident);
    string errIdent = ident;
    while (parser.token == tokPeriod)
    {
        if (!obj->isScope())
            return obj;
        ShScope* scope = (ShScope*)obj;
        parser.next(); // "."
        ident = parser.getIdent();
        errIdent += '.' + ident; // this is important for the hack in getTypeOrNewIdent()
        obj = scope->find(ident);
        if (obj == NULL)
            errorNotFound(errIdent);
    }
    return obj;
}


void ShModule::getConstCompound(ShType* typeHint, ShValue& result)
{
    if (typeHint != NULL && !typeHint->isVector())
        typeHint = NULL; // let the assignment parser decide
    if (parser.skipIf(tokRSquare))
    {
        result.assignVec(queenBee->defaultEmptyVec, emptystr);
    }
    else
    {
        string vec;
        ShType* elemType = typeHint != NULL && typeHint->isVector() ?
            PVector(typeHint)->elementType : NULL;
        int elemSize = -1;
        while (1)
        {
            ShValue value;
            getConstExpr(elemType, value);
            if (elemType == NULL)
                elemType = value.type;
            if (!elemType->canAssign(value.type))
                errorWithLoc("Type mismatch in vector constructor"); // never reached
            if (elemSize == -1)
                elemSize = elemType->staticSize();
            value.assignToBuf(vec.appendn(elemSize));
            if (parser.skipIf(tokRSquare))
                break;
            parser.skip(tokComma, "]");
        }
        result.assignVec(elemType->deriveVectorType(), registerVector(vec));
    }
}


ShType* ShModule::getTypeExpr(bool anyObj)
{
    VmCodeGen tcode;
    parseExpr(tcode);
    try
    {
        ShValue value;
        ShType* type = tcode.runTypeExpr(value, anyObj);
        if (value.type == NULL)
            error("Expression can't be evaluated at compile time");
        return type;
    }
    catch(EInvalidSubrange& e)
    {
        error(e.what());
        return NULL;
    }
}


ShType* ShModule::parseAtom(VmCodeGen& code, bool isLValue)
{
    if (parser.skipIf(tokLParen))
    {
        parseExpr(code);
        parser.skip(tokRParen, ")");
    }

    // numeric literal
    else if (parser.token == tokIntValue)
    {
        large value = parser.intValue; // intValue is unsigned int
        parser.next();
        if (!queenBee->defaultInt->contains(value))
            error("Value out of range (use the 'L' suffix for large consts)");
        code.genLoadIntConst(queenBee->defaultInt, value);
    }

    else if (parser.token == tokLargeValue)
    {
        ularge value = parser.largeValue; // largeValue is unsigned int
        parser.next();
        code.genLoadLargeConst(queenBee->defaultLarge, value);
    }

    // string or char literal
    else if (parser.token == tokStrValue)
    {
        if (parser.strValue.size() == 1)
        {
            int value = (unsigned)parser.strValue[0];
            parser.next();
            code.genLoadIntConst(queenBee->defaultChar, value);
        }
        else
        {
            string s = parser.strValue;
            parser.next();
            code.genLoadVecConst(queenBee->defaultStr, registerString(s).c_bytes());
        }
    }

    // identifier
    else if (parser.token == tokIdent)
    {
        ShBase* obj = getQualifiedName();
        bool isAlias = obj->isTypeAlias();
        // symbolic constant
        if (obj->isConstant())
        {
            ShConstant* c = (ShConstant*)obj;
            code.genLoadConst(c->value.type, c->value.value);
        }

        // type or type alias: static cast
        // only typeid(expr) is allowed for function-style typecasts
        else if (obj->isType() || isAlias)
        {
            // TODO: type casts must be in parseDesignator()
            ShType* type = isAlias ? ((ShTypeAlias*)obj)->base : (ShType*)obj;
            if (parser.token == tokLParen)
            {
                parser.next();
                code.resultTypeHint = type;
                parseExpr(code);
                parser.skip(tokRParen, ")");
                ShType* fromType = code.genTopType();
                if (!fromType->canStaticCastTo(type))
                    error("Can't do static typecast from " + fromType->getDefinitionQ()
                        + " to " + type->getDefinitionQ());
                code.genStaticCast(type);
            }
            else
                code.genLoadTypeRef(getDerivators(type));
        }
        
        else if (obj->isVariable())
        {
            if (isLValue)
                code.genLoadVarRef((ShVariable*)obj);
            else
                code.genLoadVar((ShVariable*)obj);
        }
        
        // TODO: vars, funcs
        else
            notImpl();
    }

    // typeof(...)
    else if (parser.skipIf(tokTypeOf))
    {
        // TODO: currently only works with const expressions, however,
        // it should be possible to get a type of a variable or an array item,
        // and finally a dynamic type of a state. The question is how. The 
        // problem of 'is' and 'as' is related.
        parser.skip(tokLParen, "(");
        code.genLoadTypeRef(getTypeExpr(true));
        parser.skip(tokRParen, ")");
    }

    // sizeof(...)
    else if (parser.skipIf(tokSizeOf))
    {
        parser.skip(tokLParen, "(");
        ShType* type = getTypeExpr(true);
        parser.skip(tokRParen, ")");
        // TODO: actual sizes for states (maybe also vectors/arrays? or len() is enough?)
        code.genLoadIntConst(queenBee->defaultInt, type->staticSize());
    }
    
    // true/false/null
    else if (parser.skipIf(tokTrue))
        code.genLoadIntConst(queenBee->defaultBool, 1);
    else if (parser.skipIf(tokFalse))
        code.genLoadIntConst(queenBee->defaultBool, 0);
    else if (parser.skipIf(tokNull))
        code.genLoadNull();

    // compound ctor (currently only vector)
    else if (parser.skipIf(tokLSquare))
    {
        ShValue comp;
        getConstCompound(code.resultTypeHint, comp);
        if (!comp.type->isVector())
            internal(20);
        code.genLoadVecConst(comp.type, pconst(comp.value.ptr_));
    }
    
    else
        errorWithLoc("Expression syntax");

    return code.genTopType();
}


ShType* ShModule::parseDesignator(VmCodeGen& code, bool isLValue)
{
    return parseAtom(code, isLValue);
}


ShType* ShModule::parseFactor(VmCodeGen& code)
{
    bool isNeg = parser.skipIf(tokMinus);
    ShType* resultType = parseDesignator(code, false);
    if (isNeg)
    {
        resultType = code.genTopType();
        if (!resultType->isInt())
            error("Invalid operand for arithmetic negation");
        code.genUnArithm(opNeg, PInteger(resultType));
    }
    return resultType;
}


ShInteger* ShModule::arithmResultType(ShInteger* left, ShInteger* right)
{
    if (PInteger(left)->isLargeInt() != PInteger(right)->isLargeInt())
        error("Mixing int and large: typecast needed (or 'L' with numbers)");
    ShInteger* resultType = PInteger(left);
    if (PInteger(right)->rangeIsGreaterOrEqual(PInteger(left)))
        resultType = PInteger(right);
    return resultType;
}


ShType* ShModule::parseTerm(VmCodeGen& code)
{
    ShType* left = parseFactor(code);
    while (parser.token == tokMul || parser.token == tokDiv || parser.token == tokMod)
    {
        Token tok = parser.token;
        parser.next();
        ShType* right = parseFactor(code);
        if (left->isInt() && right->isInt())
        {
            left = arithmResultType(PInteger(left), PInteger(right));
            code.genBinArithm(tok == tokMul ? opMul
                : tok == tokDiv ? opDiv : opMod, PInteger(left));
        }
        else
            error("Invalid operands for arithmetic operator");
    }
    return left;
}


ShType* ShModule::parseArithmExpr(VmCodeGen& code)
{
    ShType* left = parseTerm(code);
    while (parser.token == tokPlus || parser.token == tokMinus)
    {
        Token tok = parser.token;
        parser.next();
        ShType* right = parseTerm(code);
        if (left->isInt() && right->isInt())
        {
            left = arithmResultType(PInteger(left), PInteger(right));
            code.genBinArithm(tok == tokPlus ? opAdd : opSub,
                PInteger(left));
        }
        else
            error("Invalid operands for arithmetic operator");
    }
    return left;
}


ShType* ShModule::parseSimpleExpr(VmCodeGen& code)
{
    ShType* left = parseArithmExpr(code);
    if (parser.token == tokCat)
    {
        parser.next();

        offs tmpOffset = 0;
        if (left->isVector())
            tmpOffset = code.genCopyToTempVec();
        else if (left->canBeArrayElement())
        {
            left = left->deriveVectorType();
            tmpOffset = code.genElemToVec(PVector(left));
        }
        else
            error("Invalid vector element type");

        do
        {
            ShType* right = parseArithmExpr(code);
            if (right->isVector())
            {
                if (left->equals(right))
                    code.genVecCat(tmpOffset);
                else
                    error("Operands of vector concatenation are incompatible");
            }
            else if (PVector(left)->elementEquals(right))
                code.genVecElemCat(tmpOffset);
            else
                error("Operands of vector concatenation are incompatible");
        }
        while (parser.skipIf(tokCat));
    }
    return left;
}


ShType* ShModule::parseRelExpr(VmCodeGen& code)
{
    ShType* left = parseSimpleExpr(code);
    if (parser.token >= tokCmpFirst && parser.token <= tokCmpLast)
    {
        OpCode op = OpCode(opCmpFirst + int(parser.token - tokCmpFirst));
        parser.next();
        ShType* right = parseSimpleExpr(code);
        if (left->canCompareWith(right)
                || ((op == opEQ || op == opNE) && left->canCheckEq(right)))
        {
            code.genComparison(op);
            left = code.genTopType();
        }
        else
            error("Type mismatch in comparison: " + typeVsType(left, right));
    }
    return left;
}


ShType* ShModule::parseNotLevel(VmCodeGen& code)
{
    bool isNot = parser.skipIf(tokNot);
    ShType* type = parseRelExpr(code);
    if (isNot)
    {
        if (type->isInt())
            code.genBitNot(PInteger(type));
        else if (type->isBool())
            code.genBoolNot();
        else
            error("Boolean or integer expression expected after 'not'");
    }
    return type;
}


ShType* ShModule::parseAndLevel(VmCodeGen& code)
{
    ShType* left = parseNotLevel(code);
    if (left->isBool())
    {
        if (parser.skipIf(tokAnd))
        {
            int saveOffset = code.genForwardBoolJump(opJumpAnd);
            ShType* right = parseAndLevel(code);
            if (!right->isBool())
                error("Boolean expression expected after 'and'");
            code.genResolveJump(saveOffset);
        }
    }
    else if (left->isInt())
    {
        while (parser.token == tokShl || parser.token == tokShr || parser.token == tokAnd)
        {
            Token tok = parser.token;
            parser.next();
            ShType* right = parseNotLevel(code);
            if (right->isInt())
            {
                if ((tok == tokShl || tok == tokShr) && PInteger(right)->isLargeInt())
                    error("Right operand of a bit shift can not be large");
                left = arithmResultType(PInteger(left), PInteger(right));
                code.genBinArithm(tok == tokShl ? opBitShl
                    : tok == tokShr ? opBitShr : opBitAnd, PInteger(left));
            }
            else
                error("Invalid operands for bitwise operator");
        }
    }
    return left;
}


ShType* ShModule::parseOrLevel(VmCodeGen& code)
{
    ShType* left = parseAndLevel(code);
    if (left->isBool())
    {
        if (parser.skipIf(tokOr))
        {
            int saveOffset = code.genForwardBoolJump(opJumpOr);
            ShType* right = parseOrLevel(code);
            if (!right->isBool())
                error("Boolean expression expected after 'or'");
            code.genResolveJump(saveOffset);
        }
        else if (parser.skipIf(tokXor))
        {
            ShType* right = parseOrLevel(code);
            if (!right->isBool())
                error("Boolean expression expected after 'xor'");
            code.genBoolXor();
        }
    }
    else if (left->isInt())
    {
        while (parser.token == tokOr || parser.token == tokXor)
        {
            Token tok = parser.token;
            parser.next();
            ShType* right = parseAndLevel(code);
            if (right->isInt())
            {
                left = arithmResultType(PInteger(left), PInteger(right));
                code.genBinArithm(tok == tokOr ? opBitOr : opBitXor,
                    PInteger(left));
            }
            else
                error("Invalid operands for bitwise operator");
        }
    }
    return left;
}


ShType* ShModule::parseSubrange(VmCodeGen& code)
{
    ShType* left = parseOrLevel(code);
    if (parser.token == tokRange)
    {
        // Check bounds for left < right maybe? Or maybe not.
        parser.next();
        ShType* right = parseOrLevel(code);
        if (!left->isOrdinal() || !right->isOrdinal())
            error("Only ordinal types are allowed in subranges");
        if (!left->equals(right))
            error("Left and right values of a subrange must be compatible");
        if (POrdinal(left)->isLargeInt() || POrdinal(right)->isLargeInt())
            error("Large subrange bounds are not supported");
        code.genMkSubrange();
        left = code.genTopType();
    }
    return left;
}


ShType* ShModule::parseExpr(VmCodeGen& code, ShType* resultType)
{
    code.resultTypeHint = resultType;
    ShType* topType = parseExpr(code);

    // see if this is an elem-to-vector assignment
    bool hintIsVec = resultType != NULL && resultType->isVector();
    if (hintIsVec && resultType->canAssign(topType) && PVector(resultType)->elementEquals(topType))
        code.genElemToVec(PVector(resultType));

    // ordinal typecast, if necessary, so that a constant has a proper type
    else if (resultType != NULL && resultType->isOrdinal() && !resultType->equals(topType))
        code.genStaticCast(resultType);

    return topType;
}


void ShModule::getConstExpr(ShType* typeHint, ShValue& result)
{
    VmCodeGen code;

    parseExpr(code, typeHint);
    code.runConstExpr(result);
    
    if (result.type == NULL)
        error("Expression can't be evaluated at compile time");

    if (typeHint == NULL)
        typeHint = result.type;
    else
    {
        if (typeHint->isVector() && result.type->isEmptyVec())
            // empty vectors are always of void type, so simply pass the hint type
            result.type = typeHint;
    }

    if (!typeHint->canAssign(result.type))
        error("Type mismatch in constant expression: " + typeVsType(typeHint, result.type));

    // even without a hint a constant can be out of range of it's own type
    // e.g. char(257), so we check the range anyway:
    if (typeHint->isOrdinal() && result.type->isOrdinal()
        && !POrdinal(typeHint)->contains(result))
            error("Value out of range");

    else if (result.type->isRange() && result.rangeMin() >= result.rangeMax())
        error("Invalid range");

}


// --- TYPES --------------------------------------------------------------- //


ShType* ShModule::getDerivators(ShType* type)
{
    if (parser.skipIf(tokLSquare))
    {
        if (parser.skipIf(tokRSquare))
        {
            if (!type->canBeArrayElement())
                error("Invalid vector element type");
            type = type->deriveVectorType();
        }
        else if (parser.skipIf(tokRange))
        {
            parser.skip(tokRSquare, "]");
            if (!type->isOrdinal())
                error("Ranges apply only to ordinal types");
            type = POrdinal(type)->deriveRangeType();
        }
        else
        {
            ShType* indexType = getTypeExpr(false);
            if (indexType == NULL)
                errorWithLoc("Type specification expected");
            parser.skip(tokRSquare, "]");
            if (!indexType->canBeArrayIndex())
                error(indexType->getDefinition() + " can't be used as array index");
            type = type->deriveArrayType(indexType);
        }
        type = getDerivators(type);
    }
    return type;
}


ShType* ShModule::getTypeOrNewIdent(string* ident)
{
    ident->clear();
    
    // Try to guess if this is a new ident or a type spec
    if (parser.token == tokIdent)
    {
        *ident = parser.strValue;
        ShBase* obj = deepFind(*ident);
        // In one of the previous attempts to implement this guessing we were calling
        // full expression parsing just to catch any exceptions and analyze them.
        // Unfortunately excpetion throwing/catching in C++ is too expensive, so we'd
        // better do something rather heuristic here:
        if (obj == NULL || (!obj->isType() && !obj->isTypeAlias() && !obj->isConstant()))
        {
            parser.next();
            return NULL;
        }
    }
    
    return getTypeExpr(false);
}


// --- STATEMENTS/DEFINITIONS ---------------------------------------------- //


ShEnum* ShModule::parseEnumType()
{
    ShEnum* type = new ShEnum();
    varScope->addAnonType(type);
    parser.skip(tokLParen, "(");
    while (1)
    {
        string ident = parser.getIdent();
        int nextVal = type->nextValue();
        if (nextVal == INT_MAX)
            error("Enum constant has just hit the ceilinig, man.");
        ShConstant* value = new ShConstant(ident, type, nextVal);
        addObject(value);
        type->registerConst(value);
        if (parser.skipIf(tokRParen))
            break;
        parser.skip(tokComma, ")");
    }
    type->finish();
    return type;
}


void ShModule::parseTypeDef()
{
    string ident;
    ShType* type;
    if (parser.skipIf(tokEnum))
    {
        ident = parser.getIdent();
        type = parseEnumType();
    }
    else
    {
        type = getTypeExpr(false);
        if (type == NULL)
            errorWithLoc("Type specification expected");
        ident = parser.getIdent();
        type = getDerivators(type);
    }
    addObject(new ShTypeAlias(ident, type));
}


void ShModule::parseVarConstDef(bool isVar, VmCodeGen& code)
{
    string ident;
    ShType* type = NULL;

    if (parser.skipIf(tokEnum))
    {
        ident = parser.getIdent();
        type = parseEnumType();
    }
    else
    {
        type = getTypeOrNewIdent(&ident);
        if (type != NULL)  // if not auto, derivators are possible after the ident
        {
            ident = parser.getIdent();
            type = getDerivators(type);
        }
    }

    parser.skip(tokAssign, "=");

    if (isVar)
    {
        ShType* exprType = parseExpr(code, type);
        if (type == NULL) // auto
            type = exprType;
        else if (!type->canAssign(exprType))
            error("Type mismatch in variable initialization");
        ShVariable* var = new ShVariable(ident, type);
        addObject(var);
        code.genInitVar(var);
    }
    else
    {
        ShValue value;
        getConstExpr(type, value);
        if (type == NULL) // auto
            type = value.type;
        addObject(new ShConstant(ident, value));
    }
}


void ShModule::parseEcho(VmCodeGen& code)
{
    if (parser.token != tokSep)
    {
        while (1)
        {
            parseExpr(code);
            code.genEcho();
            if (parser.skipIf(tokComma))
                code.genOther(opEchoSp);
            else
                break;
        }
    }
    code.genOther(opEchoLn);
}


void ShModule::parseAssert(VmCodeGen& code)
{
    ShType* type = parseExpr(code);
    if (!type->isBool())
        error("Boolean expression expected for assertion");
    code.genAssert(parser);
}


void ShModule::parseOtherStatement(VmCodeGen& code)
{
    ShType* type = parseDesignator(code, true);
    if (parser.skipIf(tokAssign))
    {
        if (!type->isReference())
            error("L-value expected in assignment");
        parseExpr(code, PReference(type)->base);
        code.genStore();
    }
    else
        error("Definition or statement expected");
}


VmCodeGen nullGen;


void ShModule::parseBlock(VmCodeGen& code)
{
    while (!parser.skipIf(tokBlockEnd))
    {
        if (parser.skipIf(tokDef))
            parseTypeDef();
        else if (parser.skipIf(tokConst))
            parseVarConstDef(false, code);
        else if (parser.skipIf(tokVar))
            parseVarConstDef(true, code);
        else if (parser.skipIf(tokEcho))
            parseEcho(options.enableEcho ? code : nullGen);
        else if (parser.skipIf(tokAssert))
            parseAssert(options.enableAssert ? code : nullGen);
        else if (parser.skipIf(tokBegin))
        {
            parser.skipBlockBegin();
            enterBlock(code);
            continue;
        }
        else
            parseOtherStatement(code);
        code.genFinalizeTemps();
        parser.skipSep();
    }
}


void ShModule::enterBlock(VmCodeGen& code)
{
    ShSymScope* saveSymScope = symbolScope;
    parseBlock(code);
    symbolScope = saveSymScope;
}


bool ShModule::compile()
{
    try
    {
        VmCodeGen main;
        VmCodeGen fin;

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

        parseBlock(main);
        parser.skip(tokEof, "<EOF>");

        genFinalizations(fin);

        setupRuntime(main, fin);

#ifdef DEBUG
        queenBee->dump("");
        dump("");
#endif

    }
    catch(Exception& e)
    {
        fprintf(stderr, "%s\n", e.what().c_str());
        nullGen.clear();
        return false;
    }

    nullGen.clear();
    return true;
}


// ------------------------------------------------------------------------- //
// ------------------------------------------------------------------------- //
// ------------------------------------------------------------------------- //



class _AtExit
{
public:
    ~_AtExit()
    {
        doneLangObjs();

        if (Base::objCount != 0)
            fprintf(stderr, "Internal: objCount = %d\n", Base::objCount);
        if (stralloc != 0)
            fprintf(stderr, "Internal: stralloc = %d\n", stralloc);
        if (FifoChunk::chunkCount != 0)
            fprintf(stderr, "Internal: chunkCount = %d\n", FifoChunk::chunkCount);
#ifdef SINGLE_THREADED
        stk.clear();
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
        ShModule module("/Users/hovik/Projects/Shannon/src/z.sn");
#else
        ShModule module("z.sn");
#endif
        module.compile();

        if (module.compiled)
        {
            // TODO: exec mains for all used modules
            module.executeMain();
            module.executeFin();
            if (!stk.empty())
                fatal(CRIT_FIRST + 54, "[VM] Stack in undefined state after execution");
        }
    }
    catch (Exception& e)
    {
        fprintf(stderr, "\n*** Exception: %s\n", e.what().c_str());
    }
    catch (int)
    {
        // run-time error
    }

    return 0;
}

