
#include "vm.h"
#include "compiler.h"


CodeSeg::CodeSeg(State* s)
    : object(), state(s)
#ifdef DEBUG
    , closed(false)
#endif
    , stackSize(0)
    { }


CodeSeg::~CodeSeg()
    { }


void CodeSeg::close()
{
#ifdef DEBUG
    assert(!closed);
    closed = true;
#endif
    append(opEnd);
}


// --- VIRTUAL MACHINE ----------------------------------------------------- //


static void invOpcode()             { fatal(0x5002, "Invalid opcode"); }
static void doExit()                { throw eexit(); }


static void failAssertion(const str& cond, const str& fn, integer linenum)
    { throw emessage("Assertion failed \"" + cond + "\" at " + fn + ':' + to_string(linenum)); }


static void dumpVar(const str& expr, const variant& var, Type* type)
{
    // TODO: dump to serr?
    sio << "# " << expr;
    if (type)
    {
        sio << ": ";
        type->dumpDef(sio);
    }
    sio << " = ";
    dumpVariant(sio, var, type);
    sio << endl;
}


static void byteDictReplace(varvec& v, integer i, const variant& val)
{
    memint size = v.size();
    if (uinteger(i) > 255)
        container::keyerr();
    if (memint(i) == size)
        v.push_back(val);
    else
    {
        if (memint(i) > size)
            v.grow(memint(i) - size + 1);
        v.replace(memint(i), val);
    }
}


template<class T>
    inline T& ADV(const char*& ip)
        { T& t = *(T*)ip; ip += sizeof(T); return t; }

#define PUSH(v) \
    { ::new(++stk) variant(v);  }

#define PUSHT(t,v) \
        { ::new(++stk) variant(variant::Type(t), v); }

#define POP() \
        { (*stk--).~variant(); }

#define POPPOD() \
        { assert(!stk->is_anyobj()); stk--; }

#define POPTO(dest) \
        { *(podvar*)(dest) = *(podvar*)stk; stk--; } // pop to to uninitialized area


template <class T>
   inline void SETPOD(variant* dest, const T& v)
        { ::new(dest) variant(v); }


#define BINARY_INT(op) { (stk - 1)->_int() op stk->_int(); POPPOD(); }
#define UNARY_INT(op)  { stk->_int() = op stk->_int(); }


void runRabbitRun(Context*, variant* selfvars, rtstack& stack, const char* ip)
{
    // TODO: check for stack overflow
    register variant* stk = stack.bp - 1;
    memint offs; // used in jump calculations
    try
    {
loop:
        switch(*ip++)
        {
        // --- 1. MISC CONTROL
        case opEnd:             goto exit;
        case opNop:             break;
        case opExit:            doExit(); break;

        // --- 2. CONST LOADERS
        case opLoadTypeRef:
            PUSH(ADV<Type*>(ip));
            break;
        case opLoadNull:
            PUSH(variant::null);
            break;
        case opLoad0:
            PUSH(integer(0));
            break;
        case opLoad1:
            PUSH(integer(1));
            break;
        case opLoadByte:
            PUSH(integer(ADV<uchar>(ip)));
            break;
        case opLoadOrd:
            PUSH(ADV<integer>(ip));
            break;
        case opLoadStr:
            PUSH(ADV<str>(ip));
            break;
        case opLoadEmptyVar:
            PUSH(variant::Type(ADV<char>(ip)));
            break;
        case opLoadConst:
            PUSH(ADV<Definition*>(ip)->value);  // TODO: better?
            break;

        // --- 3. LOADERS
        case opLoadSelfVar:
            PUSH(selfvars[ADV<char>(ip)]);
            break;
        case opLoadStkVar:
            PUSH(*(stack.bp + ADV<char>(ip)));
            break;
        case opLoadMember:
            *stk = cast<stateobj*>(stk->_rtobj())->var(ADV<char>(ip));
            break;

        // --- 4. STORERS
        case opInitStkVar:
            POPTO(stack.bp + memint(ADV<char>(ip)));
            break;

        // --- 5. DESIGNATOR OPS, MISC
        case opMkRef:
            SETPOD(stk, new reference((podvar*)stk));
            break;
        case opAutoDeref:
        case opDeref:
            {
                notimpl();
                reference* r = stk->_ref();
                SETPOD(stk, r->var);
                r->release();
            }
            break;
        case opNonEmpty:
            *stk = int(!stk->empty());
            break;
        case opPop:
            POP();
            break;

        // --- 6. STRINGS, VECTORS
        case opChrToStr:
            *stk = str(stk->_int());
            break;
        case opChrCat:
            (stk - 1)->_str().push_back(stk->_uchar());
            POPPOD();
            break;
        case opStrCat:
            (stk - 1)->_str().append(stk->_str());
            POP();
            break;
        case opVarToVec:
            *stk = varvec(*stk);
            break;
        case opVarCat:
            (stk - 1)->_vec().push_back(*stk);
            POP();
            break;
        case opVecCat:
            (stk - 1)->_vec().append(stk->_vec());
            POP();
            break;
        case opStrElem:
            *(stk - 1) = (stk - 1)->_str().at(memint(stk->_int()));  // *OVR
            POPPOD();
            break;
        case opVecElem:
            *(stk - 1) = (stk - 1)->_vec().at(memint(stk->_int()));  // *OVR
            POPPOD();
            break;
        case opStrLen:
            *stk = integer(stk->_str().size());
            break;
        case opVecLen:
            *stk = integer(stk->_vec().size());
            break;

        // --- 7. SETS
        case opElemToSet:
            *stk = varset(*stk);
            break;
        case opSetAddElem:
            (stk - 1)->_set().find_insert(*stk);
            POP();
            break;
        case opElemToByteSet:
            *stk = ordset(stk->_int());
            break;
        case opRngToByteSet:
            *(stk - 1) = ordset((stk - 1)->_int(), stk->_int());
            POPPOD();
            break;
        case opByteSetAddElem:
            (stk - 1)->_ordset().find_insert(stk->_int());
            POPPOD();
            break;
        case opByteSetAddRng:
            (stk - 2)->_ordset().find_insert((stk - 1)->_int(), stk->_int());
            POPPOD();
            POPPOD();
            break;

        // --- 8. DICTIONARIES
        case opPairToDict:
            *(stk - 1) = vardict(*(stk - 1), *stk);
            POP();
            break;
        case opDictAddPair:
            (stk - 2)->_dict().find_replace(*(stk - 1), *stk);
            POP();
            POP();
            break;
        case opPairToByteDict:
            {
                integer i = (stk - 1)->_int();
                SETPOD(stk - 1, varvec());
                byteDictReplace((stk - 1)->_vec(), i, *stk);
                POP();
            }
            break;
        case opByteDictAddPair:
            byteDictReplace((stk - 2)->_vec(), (stk - 1)->_int(), *stk);
            POP();
            POPPOD();
            break;
        case opDictElem:
            {
                const variant* v = (stk - 1)->_dict().find(*stk);
                POP();
                if (v)
                    *stk = *v;  // potentially dangerous if dict has refcount=1, which it shouldn't
                else
                    container::keyerr();
            }
            break;
        case opByteDictElem:
            {
                integer i = stk->_int();
                POPPOD();
                if (i < 0 || i >= stk->_vec().size())
                    container::keyerr();
                const variant& v = stk->_vec()[memint(i)];
                if (v.is_null())
                    container::keyerr();
                *stk = v;  // same as for opDictElem
            }
            break;

        // --- 9. ARITHMETIC
        // TODO: range checking in debug mode
        case opAdd:         BINARY_INT(+=); break;
        case opSub:         BINARY_INT(-=); break;
        case opMul:         BINARY_INT(*=); break;
        case opDiv:         BINARY_INT(/=); break;
        case opMod:         BINARY_INT(%=); break;
        case opBitAnd:      BINARY_INT(&=); break;
        case opBitOr:       BINARY_INT(|=); break;
        case opBitXor:      BINARY_INT(^=); break;
        case opBitShl:      BINARY_INT(<<=); break;
        case opBitShr:      BINARY_INT(>>=); break;
        // case opBoolXor:     SETPOD(stk - 1, bool((stk - 1)->_int() ^ stk->_int())); POPPOD(stk); break;
        case opNeg:         UNARY_INT(-); break;
        case opBitNot:      UNARY_INT(~); break;
        case opNot:         UNARY_INT(!); break;

        // --- 10. BOOLEAN
        case opCmpOrd:
            SETPOD(stk - 1, (stk - 1)->_int() - stk->_int());
            POPPOD();
            break;
        case opCmpStr:
            *(stk - 1) = integer((stk - 1)->_str().compare(stk->_str()));
            POP();
            break;
        case opCmpVar:
            *(stk - 1) = int(*(stk - 1) == *stk) - 1;
            POP();
            break;

        case opEqual:       stk->_int() = stk->_int() == 0; break;
        case opNotEq:       stk->_int() = stk->_int() != 0; break;
        case opLessThan:    stk->_int() = stk->_int() < 0; break;
        case opLessEq:      stk->_int() = stk->_int() <= 0; break;
        case opGreaterThan: stk->_int() = stk->_int() > 0; break;
        case opGreaterEq:   stk->_int() = stk->_int() >= 0; break;

        // --- 11. JUMPS
        case opJump:
                // beware of strange behavior of the GCC optimizer: this should be done in 2 steps
            offs = ADV<jumpoffs>(ip);
            ip += offs;
            break;
        case opJumpFalse:
            UNARY_INT(!);
        case opJumpTrue:
            offs = ADV<jumpoffs>(ip);
            if (stk->_int())
                ip += offs;
            POP();
            break;
        case opJumpAnd:
            UNARY_INT(!);
        case opJumpOr:
            offs = ADV<jumpoffs>(ip);
            if (stk->_int())
                ip += offs;
            else
                POP();
            break;

        // --- 12. DEBUGGING, DIAGNOSTICS
        case opAssert:
            {
                str& cond = ADV<str>(ip);
                str& fn = ADV<str>(ip);
                integer ln = ADV<integer>(ip);
                if (!stk->_int())
                    failAssertion(cond, fn, ln);
                POPPOD();
            }
            break;
        case opDump:
            {
                str& expr = ADV<str>(ip);
                dumpVar(expr, *stk, ADV<Type*>(ip));
                POP();
            }
            break;

        default:
            invOpcode();
            break;
        }
        goto loop;
exit:
        // while (stk >= stack.bp)
        //     POP(stk);
        // TODO: assertion below only for DEBUG build
        assert(stk == stack.bp - 1);
    }
    catch(exception&)
    {
        while (stk >= stack.bp)
            POP();
        throw;
    }
}


eexit::eexit() throw(): ecmessage("Exit called")  {}
eexit::~eexit() throw()  { }


Type* CodeGen::runConstExpr(Type* resultType, variant& result)
{
    if (resultType == NULL)
        resultType = stkTop();
    storeRet(resultType);
    end();
    rtstack stack(codeseg.stackSize + 1);
    stack.push(variant::null);  // storage for the return value
    runRabbitRun(NULL, NULL, stack, codeseg.getCode());
    stack.popto(result);
    return resultType;
}


// --- Execution Context --------------------------------------------------- //


ModuleInstance::ModuleInstance(Module* m)
    : Symbol(m->getName(), MODULEINST, m, NULL), module(m), obj()  { }


void ModuleInstance::run(Context* context, rtstack& stack)
{
    assert(module->isComplete());

    // Assign module vars. This allows to generate code that accesses module
    // static data by variable id, so that code is context-independant
    for (memint i = 0; i < module->uses.size(); i++)
    {
        Variable* v = module->uses[i];
        stateobj* o = context->getModuleObject(v->getModuleType());
        obj->var(v->id) = o;
    }

    // Run module initialization or main code
    runRabbitRun(context, obj->varbase(), stack, module->codeseg->getCode());
}


void ModuleInstance::finalize()
{
    if (!obj.empty())
    {
        try
        {
            obj->collapse();   // destroy possible circular references first
            obj.clear();       // now free the object itself
        }
        catch (exception&)
        {
            fatal(0x5006, "Internal: exception in destructor");
        }
    }
}


CompilerOptions::CompilerOptions()
  : enableDump(true), enableAssert(true), linenumInfo(true),
    vmListing(true), stackSize(8192)
        { modulePath.push_back("./"); }


void CompilerOptions::setDebugOpts(bool flag)
{
    enableDump = flag;
    enableAssert = flag;
    linenumInfo = flag;
    vmListing = flag;
}


static str moduleNameFromFileName(const str& n)
    { return remove_filename_path(remove_filename_ext(n)); }


Context::Context()
    : Scope(NULL), queenBeeInst(addModule(queenBee))  { }


Context::~Context()
    { instances.release_all(); }


ModuleInstance* Context::addModule(Module* m)
{
    objptr<ModuleInstance> inst = new ModuleInstance(m);
    Scope::addUnique(inst);
    instances.push_back(inst->grab<ModuleInstance>());
    return inst;
}


Module* Context::loadModule(const str& filePath)
{
    str modName = moduleNameFromFileName(filePath);
    objptr<Module> m = new Module(modName, filePath);
    addModule(m);
    Compiler compiler(*this, *m, new intext(NULL, filePath));
    compiler.compileModule();
    return m;
}


str Context::lookupSource(const str& modName)
{
    for (memint i = 0; i < options.modulePath.size(); i++)
    {
        str t = options.modulePath[i] + "/" + modName + SOURCE_EXT;
        if (isFile(t.c_str()))
            return t;
    }
    throw emessage("Module not found: " + modName);
}


Module* Context::getModule(const str& modName)
{
    // TODO: find a moudle by full path, not just name (hash by path/name?)
    // TODO: to have a global cache of compiled modules, not just within the econtext
    ModuleInstance* inst = cast<ModuleInstance*>(Scope::find(modName));
    if (inst != NULL)
        return inst->module;
    else
        return loadModule(lookupSource(modName));
}


stateobj* Context::getModuleObject(Module* m)
{
    stateobj* const* o = modObjMap.find(m);
    if (o == NULL)
        fatal(0x5003, "Internal: module not found");
    return *o;
}


void Context::instantiateModules()
{
    // Now that all modules are compiled and their dataseg sizes are known, we can
    // instantiate the objects:
    for (memint i = 0; i < instances.size(); i++)
    {
        ModuleInstance* inst = instances[i];
        inst->obj = inst->module->newInstance();
        assert(modObjMap.find(inst->module) == NULL);
        modObjMap.find_replace(inst->module, inst->obj);
    }
}


void Context::clear()
{
    for (memint i = instances.size(); i--; )
        instances[i]->finalize();
    modObjMap.clear();
}


void Context::dump(const str& listingPath)
{
    if (options.enableDump || options.vmListing)
    {
        outtext f(NULL, listingPath);
        for (memint i = 0; i < instances.size(); i++)
            instances[i]->module->dumpAll(f);
    }
}


variant Context::execute(const str& filePath)
{
    loadModule(filePath);
    dump(remove_filename_ext(filePath) + ".lst");
    instantiateModules();
    rtstack stack(options.stackSize);
    try
    {
        for (memint i = 0; i < instances.size(); i++)
            instances[i]->run(this, stack);
    }
    catch (eexit&)
    {
        // exit operator called, we are ok with it
    }
    catch (exception&)
    {
        clear();
        throw;
    }
    variant result = queenBeeInst->obj->var(queenBee->resultVar->id);
    clear();
    return result;
}

