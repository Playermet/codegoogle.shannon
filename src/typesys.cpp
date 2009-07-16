

#include "version.h"
#include "common.h"
#include "typesys.h"
#include "vm.h"


// --- LANGUAGE OBJECT ----------------------------------------------------- //


void* langobj::operator new(size_t, mem datasize)
{
    return new char[sizeof(langobj) + datasize * sizeof(variant)];
}


// Defined in runtime.h
langobj::langobj(State* type)
    : object(type)
#ifdef DEBUG
    , varcount(type->dataSize())
#endif
{
    memset(vars, 0, type->dataSize() * sizeof(variant));
}


void langobj::_idx_err()
{
    throw emessage("Object index error");
}


langobj::~langobj()
{
    mem count = PState(get_rt())->dataSize();
    while (count--)
        vars[count].~variant();
}


// --- EXECUTION CONTEXT --------------------------------------------------- //


Context::Context()
    : topModule(NULL)  { }


Module* Context::registerModule(Module* m)
{
    assert(m->id == modules.size());
    if (modules.size() == 255)
        throw emessage("Maximum number of modules reached");
    modules.add(m);
    return topModule = m;
}


Module* Context::addModule(const str& name)
{
    return registerModule(new Module(name, modules.size(), this));
}


void Context::run(varstack& stack)
{
    assert(datasegs.empty());
    for (mem i = 0; i < modules.size(); i++)
    {
        Module* m = modules[i];
        datasegs.add(m->newObject());
    }

    mem level = 0;
    try
    {
        while (level < modules.size())
        {
            modules[level]->run(datasegs[level], stack);
            level++;
        }
    }
    catch (exception&)
    {
        while (level--)
            modules[level]->finalize(datasegs[level], stack);
        throw;
    }

    while (level--)
        modules[level]->finalize(datasegs[level], stack);
    datasegs.clear();
}



// --- CODE SEGMENT -------------------------------------------------------- //

CodeSeg::CodeSeg(State* _state, Context* _context)
  : stksize(0), returns(0), state(_state), context(_context)
#ifdef DEBUG
    , closed(false)
#endif
    { }

CodeSeg::~CodeSeg()  { }

void CodeSeg::clear()
{
    code.clear();
    consts.clear();
    stksize = 0;
    returns = 0;
#ifdef DEBUG
    closed = false;
#endif
}

int CodeSeg::addOp(unsigned c)
    { code.push_back(c); return code.size() - 1; }

void CodeSeg::add8(uint8_t i)
    { code.push_back(i); }

void CodeSeg::add16(uint16_t i)
    { code.append((char*)&i, 2); }

void CodeSeg::addInt(integer i)
    { code.append((char*)&i, sizeof(i)); }

void CodeSeg::addPtr(void* p)
    { code.append((char*)&p, sizeof(p)); }


void CodeSeg::close(mem _stksize, mem _returns)
{
    assert(!closed);
    if (!code.empty())
        addOp(opEnd);
    stksize = _stksize;
    returns = _returns;
#ifdef DEBUG
    closed = true;
#endif
}


// --- TYPE SYSTEM --------------------------------------------------------- //

void typeMismatch()
        { throw emessage("Type mismatch"); }


// Constructor placeholders for the DERIVEX macro
#define new_Fifo(x)     new Fifo(x)
#define new_Vector(x)   new Container(queenBee->defNone, x)
#define new_Set(x)      new Container(x, queenBee->defNone)
#define new_Range(x)    new Range(x)


#define DERIVEX(d) \
    { if (derived##d == NULL) \
        derived##d = owner->registerType(new_##d(this)); \
      return derived##d; }


Base::Base(Type* rt, BaseId _id)
    : Symbol(rt, null_str), baseId(_id)  { }
Base::Base(Type* rt, const str& _name, BaseId _id)
    : Symbol(rt, _name), baseId(_id)  { }


// --- Type ---------------------------------------------------------------- //


Type::Type(Type* rt, TypeId _t)
  : object(rt), typeId(_t), owner(NULL), derivedFifo(NULL),
    derivedVector(NULL), derivedSet(NULL)
{
    assert(rt != NULL);
}


Type::~Type() { }


bool Type::isString()
    { return isVector() && PVector(this)->elem->isChar(); }

bool Type::isCharFifo()
    { return isFifo() && PFifo(this)->isChar(); }

bool Type::canBeArrayIndex()
    { return isOrdinal() && POrdinal(this)->rangeFits(Container::MAX_ARRAY_INDEX); }

bool Type::canBeOrdsetIndex()
    { return isOrdinal() && POrdinal(this)->rangeFits(charset::BITS); }


Fifo* Type::deriveFifo()        { DERIVEX(Fifo) }
Container* Type::deriveVector() { DERIVEX(Vector) }
Container* Type::deriveSet()    { DERIVEX(Set) }


bool Type::identicalTo(Type* t)  // for comparing container elements, indexes
    { return t->is(typeId); }

bool Type::canCastImplTo(Type* t)  // can assign or automatically convert the type without changing the value
    { return identicalTo(t); }

bool Type::isMyType(variant& v)
    { return (v.is_object() && identicalTo(v._object()->get_rt())); }

void Type::runtimeTypecast(variant& v)
    { if (!isMyType(v)) typeMismatch(); }


// --- Variable ----------------------------------------------------------- //


Variable::Variable(const str& _name, Type* _type, mem _id, bool _readOnly)
    : Base(_type, _name, VARIABLE), type(_type), id(_id), readOnly(_readOnly)  { }

Variable::~Variable()  { }


// --- Constant ----------------------------------------------------------- //


Constant::Constant(const str& _name, Type* _type, const variant& _value)
    : Base(_type, _name, DEFINITION), type(_type), value(_value)  { }


Constant::~Constant()  { }


Type* Constant::getAlias()
{
    assert(isTypeAlias());
    return PType(value.as_object());
}


// --- Scope --------------------------------------------------------------- //


Scope::Scope(Scope* _outer): outer(_outer)  { }
Scope::~Scope()  { }


Base* Scope::deepFind(const str& ident) const
{
    Base* b = find(ident);
    if (b != NULL)
        return b;
    for (int i = uses.size() - 1; i >= 0; i--)
    {
        b = uses[i]->find(ident);
        if (b != NULL)
            return b;
    }
    if (outer != NULL)
        return outer->deepFind(ident);
    return NULL;
}


Variable* Scope::addVariable(const str& name, Type* type)
{
    if (vars.size() == 255)
        throw emessage("Maximum number of variables within one scope is reached");
    objptr<Variable> v = new Variable(name, type, vars.size());
    addUnique(v);   // may throw
    vars.add(v);
    return v;
}


// --- State --------------------------------------------------------------- //


State::State(const str& _name, State* _parent, Context* _context)
  : Type(defTypeRef, STATE), Scope(_parent),
    main(this, _context), final(this, _context),
    level(_parent == NULL ? 0 : _parent->level + 1)
{
    setName(_name);
    setOwner(_parent);
}


State::~State()  { }


bool State::identicalTo(Type* t)
    { return t == this; }

bool State::canCastImplTo(Type* t)
    { return t == this; } // TODO: implement inheritance


langobj* State::newObject()
{
    mem s = dataSize();
    if (s == 0)
        return NULL;
    return new(s) langobj(this);
}


bool State::isMyType(variant& v)
    { return (v.is_object() && v._object()->get_rt()->canCastImplTo(this)); }


Constant* State::addConstant(const str& name, Type* type, const variant& value)
{
    objptr<Constant> c = new Constant(name, type, value);
    addUnique(c); // may throw
    consts.add(c);
    return c;
}


Constant* State::addTypeAlias(const str& name, Type* type)
{
    objptr<Constant> c = new Constant(name, defTypeRef, type);
    if (type->name.empty())
        type->setName(name);
    addUnique(c); // may throw
    consts.add(c);
    return c;
}


void State::run(langobj* self, varstack& stack)
{
    if (!main.empty())
        main.run(self, stack);
}


void State::finalize(langobj* self, varstack& stack)
{
    if (!final.empty())
        final.run(self, stack);
}


// --- Module -------------------------------------------------------------- //


Module::Module(const str& _name, mem _id, Context* _context)
        : State(_name, NULL, _context), id(_id)  { }


// --- None ---------------------------------------------------------------- //


None::None(): Type(defTypeRef, NONE)  { }
bool None::isMyType(variant& v)  { return v.is_null(); }


// --- Ordinal ------------------------------------------------------------- //


Ordinal::Ordinal(TypeId _type, integer _left, integer _right)
    : Type(defTypeRef, _type), derivedRange(NULL), left(_left), right(_right)
{
    assert(isOrdinal());
}


Range* Ordinal::deriveRange()   { DERIVEX(Range) }


Ordinal* Ordinal::deriveSubrange(integer _left, integer _right)
{
    if (rangeEq(_left, _right))
        return this;
    if (!isLe(_left, _right))
        throw emessage("Subrange error");
    return new Ordinal(typeId, _left, _right);
}


bool Ordinal::identicalTo(Type* t)
    { return t->is(typeId) && rangeEq(POrdinal(t)); }

bool Ordinal::canCastImplTo(Type* t)
    { return t->is(typeId); }


bool Ordinal::isMyType(variant& v)
{
    return isBool() || (v.is_ordinal() && isInRange(v.as_ordinal()));
}


void Ordinal::runtimeTypecast(variant& v)
{
    if (isBool())
    {
        v = v.to_bool();
        return;
    }
    if (!v.is_ordinal())
        typeMismatch();
    if (!isInRange(v.as_ordinal()))
        throw emessage("Out of range");
    if (isChar())
        v.type = variant::CHAR;
    else if (isInt() || isEnum())
        v.type = variant::INT;
    else
        fatal(0x3001, "Unknown ordinal type");
}


// --- Enumeration --------------------------------------------------------- //


Enumeration::Enumeration(): Ordinal(ENUM, 0, -1), values(new EnumValues())  { }


Enumeration::Enumeration(TypeId _typeId)
    : Ordinal(_typeId, 0, -1), values(new EnumValues())  { }


Enumeration::Enumeration(EnumValues* _values, integer _left, integer _right)
    : Ordinal(ENUM, _left, _right), values(_values)
{
    assert(_left >= 0 && _left <= _right && mem(_right) < _values->size());
}


void Enumeration::addValue(const str& name)
{
    reassignRight(
        values->add(
            owner->addConstant(name, this, variant(values->size()))));
}


bool Enumeration::identicalTo(Type* t)
    { return this == t; }

bool Enumeration::canCastImplTo(Type* t)
    { return t->isEnum() && values == PEnum(t)->values; }


Ordinal* Enumeration::deriveSubrange(integer _left, integer _right)
{
    if (_left == left && _right == right)
        return this;
    if (_left < left || _right > right)
        throw emessage("Subrange error");
    return new Enumeration(values, _left, _right);
}


// --- Range --------------------------------------------------------------- //


Range::Range(Ordinal* _base)
    : Type(defTypeRef, RANGE), base(_base)  { }


bool Range::identicalTo(Type* t)
        { return t->isRange() && base->identicalTo(PRange(t)->base); }

bool Range::canCastImplTo(Type* t)
        { return t->isRange() && base->canCastImplTo(PRange(t)->base); }


// --- Container ---------------------------------------------------------- //


Container::Container(Type* _index, Type* _elem)
    : Type(defTypeRef, NONE), index(_index), elem(_elem)
{
    if (index->isNone())
        setTypeId(VECTOR);
    else if (elem->isNone())
    {
        if (index->canBeOrdsetIndex())
            setTypeId(ORDSET);
        else
            setTypeId(SET);
    }
    else if (index->canBeArrayIndex())
        setTypeId(ARRAY);
    else
        setTypeId(DICT);
}


bool Container::identicalTo(Type* t)
    { return t->is(typeId) && elem->identicalTo(PContainer(t)->elem)
            && index->identicalTo(PContainer(t)->index); }


// --- Fifo ---------------------------------------------------------------- //


Fifo::Fifo(Type* _elem): Type(defTypeRef, FIFO), elem(_elem)  { }


bool Fifo::identicalTo(Type* t)
    { return t->is(typeId) && elem->identicalTo(PFifo(t)->elem); }


// --- Variant ------------------------------------------------------------- //


Variant::Variant(): Type(defTypeRef, VARIANT)  { }
bool Variant::isMyType(variant&)  { return true; }
void Variant::runtimeTypecast(variant&)  { }


// --- TypeReference ------------------------------------------------------- //


TypeReference::TypeReference(): Type(this, TYPEREF)  { }


// --- QueenBee ------------------------------------------------------------ //


QueenBee::QueenBee()
  : Module("system", 0, NULL)
{
    registerType(defTypeRef.get());
    defNone = registerType(new None());
    defInt = registerType(new Ordinal(Type::INT, INTEGER_MIN, INTEGER_MAX));
    defBool = registerType(new Enumeration(Type::BOOL));
    defChar = registerType(new Ordinal(Type::CHAR, 0, 255));
    defStr = NULL;
    defVariant = registerType(new Variant());
    defCharFifo = NULL;
    siovar = NULL;
    serrvar = NULL;
}


void QueenBee::setup()
{
    // This can't be done in the constructor while the global object queenBee
    // is not assigned
    defStr = defChar->deriveVector();
    defCharFifo = defChar->deriveFifo();
    addTypeAlias("typeref", defTypeRef);
    addTypeAlias("none", defNone);
    addConstant("null", defNone, null);
    addTypeAlias("int", defInt);
    defBool->addValue("false");
    defBool->addValue("true");
    addTypeAlias("bool", defBool);
    addTypeAlias("str", defStr);
    addTypeAlias("any", defVariant);
    siovar = addVariable("sio", defCharFifo);
    serrvar = addVariable("serr", defCharFifo);
    addConstant("__ver_major", defInt, SHANNON_VERSION_MAJOR);
    addConstant("__ver_minor", defInt, SHANNON_VERSION_MINOR);
}


void QueenBee::run(langobj* self, varstack&)
{
    (*self)[siovar->id] = &sio;
    (*self)[serrvar->id] = &serr;
}


objptr<TypeReference> defTypeRef;
objptr<QueenBee> queenBee;


void initTypeSys()
{
    // Because all Type objects are also runtime objects, they all have a
    // runtime type of "type reference". The initial typeref object refers to
    // itself and should be created before anything else in the type system.
    defTypeRef = new TypeReference();
    queenBee = new QueenBee();
    queenBee->setup();
    sio.set_rt(queenBee->defCharFifo);
    serr.set_rt(queenBee->defCharFifo);
//    fout->set_rt();
}


void doneTypeSys()
{
    sio.clear_rt();
    serr.clear_rt();
    defTypeRef = NULL;
    queenBee = NULL;
}
