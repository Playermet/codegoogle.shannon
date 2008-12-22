#ifndef __LANGOBJ_H
#define __LANGOBJ_H

#include "str.h"
#include "except.h"
#include "baseobj.h"
#include "source.h"


const large int8min   = -128LL;
const large int8max   = 127LL;
const large uint8max  = 255LL;
const large int16min  = -32768LL;
const large int16max  = 32767LL;
const large uint16max = 65535LL;
const large int32min  = -2147483648LL;
const large int32max  = 2147483647LL;
const large uint32max = 4294967295LL;
const large int64min  = LARGE_MIN;
const large int64max  = LARGE_MAX;
const int   memAlign  = sizeof(ptr);


// --- BASIC LANGUAGE OBJECTS ---------------------------------------------- //


class ShType;
class ShConstant;
class ShOrdinal;
class ShBool;
class ShScope;
class ShState;
class ShVector;
class ShString;
class ShSet;
class ShArray;
class ShModule;
class ShQueenBee;


class ShBase: public BaseNamed
{
public:
    ShScope* owner;
    
    ShBase(): BaseNamed(), owner(NULL)  { }
    ShBase(const string& name): BaseNamed(name), owner(NULL)  { }
    
    virtual bool isType() const       { return false; }
    virtual bool isTypeAlias() const  { return false; }
    virtual bool isConstant() const   { return false; }
    virtual bool isScope() const      { return false; }
};


class ShType: public ShBase
{
    ShVector* derivedVectorType;
    ShSet* derivedSetType;

protected:
    virtual string getFullDefinition(const string& objName) const = 0;

public:
    ShType();
    ShType(const string& name);
    virtual ~ShType();
    string getDisplayName(const string& objName) const;
    virtual bool isType() const
            { return true; }
    virtual bool isComplete() const
            { return true; }
    virtual bool isOrdinal() const
            { return false; }
    virtual bool isComparable() const
            { return isOrdinal(); }
    bool canBeArrayIndex() const
            { return isOrdinal() || isComparable(); }
    virtual bool isInt() const
            { return false; }
    virtual bool isChar() const
            { return false; }
    virtual bool isBool() const
            { return false; }
    virtual bool isVector() const
            { return false; }
    virtual bool isString() const
            { return false; }
    virtual bool isArray() const
            { return false; }
    virtual bool isRange() const
            { return false; }
    virtual bool equals(ShType*) const = 0;
    ShVector* deriveVectorType(ShScope* scope);
    ShArray* deriveArrayType(ShType* indexType, ShScope* scope);
    ShSet* deriveSetType(ShBool* elementType, ShScope* scope);
    void setDerivedVectorTypePleaseThisIsCheatingIKnow(ShVector* v)
            { derivedVectorType = v; }
};


class ShTypeAlias: public ShBase
{
public:
    ShType* const base;
    ShTypeAlias(const string& name, ShType* iBase);
    virtual bool isTypeAlias() const  { return true; }
};


class ShVariable: public ShBase
{
public:
    ShType* const type;

    ShVariable(ShType* iType);
    ShVariable(const string& name, ShType* iType);
    virtual bool isArgument()
            { return false; }
};


class ShArgument: public ShVariable
{
public:
    ShArgument(const string& name, ShType* iType);
    virtual bool isArgument()
            { return true; }
};


// --- SCOPE --- //

class ShScope: public ShType
{
protected:
    BaseTable<ShBase> symbols;
    BaseTable<ShModule> uses; // not owned
    BaseList<ShType> types;
    BaseList<ShVariable> vars;
    BaseList<ShTypeAlias> typeAliases;
    BaseList<ShConstant> consts;
    
    ShBase* own(ShBase* obj);
    void addSymbol(ShBase* obj);

public:
    bool complete;

    ShScope(const string& name);
    virtual bool isScope() const
            { return true; }
    virtual bool isComplete() const
            { return complete; };
    void addUses(ShModule*);
    void addAnonType(ShType*);
    void addType(ShType*);
    void addAnonVar(ShVariable*);
    void addVariable(ShVariable*);
    void addTypeAlias(ShTypeAlias*);
    void addConstant(ShConstant*);
    void setCompleted()
            { complete = true; }
    ShBase* find(const string& name) const // doesn't throw
            { return symbols.find(name); }
    ShBase* deepSearch(const string&) const; // throws ENotFound
    ShBase* deepFind(const string&) const; // doesn't throw
    void dump(string indent) const;
};


// --- LANGUAGE TYPES ----------------------------------------------------- //


struct Range
{
    const large min;
    const large max;

    Range(large iMin, large iMax): min(iMin), max(iMax)  { }
    int  physicalSize() const;
};


class ShOrdinal: public ShType
{
public:
    const Range range;
    const int size;

    ShOrdinal(const string& name, large min, large max);
    virtual bool isOrdinal() const
            { return true; }
    virtual bool isCompatibleWith(ShType*) const = 0;
    bool contains(large value) const
            { return value >= range.min && value <= range.max; }
    bool rangeEquals(const Range r) const
            { return range.min == r.min && range.max == r.max; }
};


class ShInteger: public ShOrdinal
{
protected:
    virtual string getFullDefinition(const string& objName) const;

public:
    ShInteger(const string& name, large min, large max)
            : ShOrdinal(name, min, max)  { }
    virtual bool isInt() const
            { return true; }
    virtual bool isCompatibleWith(ShType* type) const
            { return type->isInt(); }
    bool isUnsigned() const
            { return range.min >= 0; }
    virtual bool equals(ShType* type) const
            { return type->isInt() && rangeEquals(((ShInteger*)type)->range); }
};


class ShChar: public ShOrdinal
{
protected:
    virtual string getFullDefinition(const string& objName) const;

public:
    ShChar(const string& name, int min = 0, int max = 255)
            : ShOrdinal(name, min, max)  { }
    virtual bool isChar() const
            { return true; }
    virtual bool isCompatibleWith(ShType* type) const
            { return type->isChar(); }
    virtual bool equals(ShType* type) const
            { return type->isChar() && rangeEquals(((ShChar*)type)->range); }
};


class ShBool: public ShOrdinal
{
protected:
    virtual string getFullDefinition(const string& objName) const;

public:
    ShBool(const string& name): ShOrdinal(name, 0, 1)  { }
    virtual bool isBool() const
            { return true; }
    virtual bool isCompatibleWith(ShType* type) const
            { return type->isBool(); }
    virtual bool equals(ShType* type) const
            { return type->isBool(); }
};


class ShVoid: public ShType
{
protected:
    virtual string getFullDefinition(const string& objName) const;

public:
    ShVoid(const string& name): ShType(name)  { }
    virtual bool equals(ShType* type) const
            { return false; }
};


/*
class ShRange: public ShType
{
public:
    ShOrdinal* base;
    
    ShRange(ShOrdinal* iBase): ShType(), base(iBase)  { }
    virtual bool isRange() const
            { return true; }
    virtual equals(ShType* type) const
            { return type->isRange() && base->equals(((ShRange*)type)->base); }
};
*/


class ShVector: public ShType
{
protected:
    virtual string getFullDefinition(const string& objName) const;

public:
    ShType* const elementType;
    ShVector(ShType* iElementType);
    ShVector(const string& name, ShType* iElementType);
    virtual bool isVector() const
            { return true; }
    virtual bool isString() const
            { return elementType->isChar(); }
    virtual bool isComparable() const
            { return elementType->isChar(); }
    virtual bool equals(ShType* type) const
            { return type->isVector() && elementType->equals(((ShVector*)type)->elementType); }
};


class ShString: public ShVector
{
protected:
    // Note that any char[] is a string, too
    friend class ShQueenBee;
    ShString(const string& name, ShChar* elementType)
            : ShVector(name, elementType)  { }
public:
    virtual bool isString() const
            { return true; }
};


class ShArray: public ShVector
{
protected:
    virtual string getFullDefinition(const string& objName) const;

public:
    ShType* const indexType;
    ShArray(ShType* iElementType, ShType* iIndexType);
    virtual bool isArray() const
            { return true; }
    virtual bool equals(ShType* type) const
            { return type->isArray() && elementType->equals(((ShArray*)type)->elementType)
                && indexType->equals(((ShArray*)type)->indexType); }
};


class ShSet: public ShArray
{
public:
    ShSet(ShBool* iElementType, ShType* iIndexType);
};


/*
class ShState: public ShScope
{
protected:
    BaseList<ShArgument> args;
    BaseList<ShState> states;

    virtual string getFullDefinition(const string& objName) const;
    string getArgsDefinition() const;

public:
    void addArgument(ShArgument*);
    void addState(ShState*);
};
*/


/*
class ShFunction: public ShState
{
    BaseList<ShType> retTypes;
public:
    void addReturnType(ShType* obj)
            { retTypes.add(obj); }
};
*/


// --- LITERAL VALUES ----------------------------------------------------- //

class ShValue
{
    union
    {
        ptr ptr_;
        int int_;
        large large_;
    } value;
public:
    ShType* type;

    ShValue(): type(NULL)  { }
    ShValue(const ShValue& v)
            : type(v.type) { value = v.value; }
    ShValue(ShOrdinal* iType, large iValue)
            : type(iType)  { value.large_ = iValue; }
    ShValue(ShString* iType, const string& iValue)
            : type(iType)  { value.ptr_ = ptr(iValue.c_bytes()); }
    void operator= (const ShValue& v)
            { value = v.value; type = v.type; }
};


class ShConstant: public ShBase
{
public:
    const ShValue value;
    ShConstant(const string& name, const ShValue& iValue)
        : ShBase(name), value(iValue)  { }
    virtual bool isConstant() const   { return true; }
};


// ------------------------------------------------------------------------ //


// --- MODULE --- //

class ShModule: public ShScope
{
    Array<string> stringLiterals;
    string fileName;
    Parser parser;

    string registerString(const string& v);  // TODO: find duplicates (?)

    // --- Compiler ---
    ShScope* currentScope;
    void error(const string& msg)        { parser.error(msg); }
    void error(const char* msg)          { parser.error(msg); }
    void notImpl()                       { error("Feature not implemented"); }
    ShBase* getQualifiedName();
    ShType* getDerivators(ShType*);
//    ShRange* getRangeType();
    ShType* getType(ShBase* previewObj);
    ShBase* getAtom();
    ShValue getOrdinalConst();
    ShValue getConstExpr(ShType* typeHint);
    
    void parseTypeDef();
    void parseConstDef();
    void parseObjectDef(ShBase* previewObj);

protected:
    virtual string getFullDefinition(const string& objName) const;

public:
    bool compiled;

    ShModule(const string& filename);
    void compile();
    void dump(string indent) const;
    virtual bool equals(ShType* type) const
            { return false; }
};


// --- SYSTEM MODULE --- //

class ShQueenBee: public ShModule
{
public:
    ShInteger* const defaultInt;     // "int"
    ShInteger* const defaultLarge;   // "large"
    ShChar* const defaultChar;       // "char"
    ShString* const defaultStr;      // "str"
    ShBool* const defaultBool;       // "bool"
    ShVoid* const defaultVoid;       // "void"
    
    ShQueenBee();
};


// ------------------------------------------------------------------------ //


extern ShQueenBee* queenBee;

void initLangObjs();
void doneLangObjs();

ShModule* findModule(const string& name);
void registerModule(ShModule* module);


#endif
