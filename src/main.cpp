
#include "common.h"
// #include "runtime.h"
// #include "parser.h"
// #include "typesys.h"
// #include "vm.h"
// #include "compiler.h"


// --- vector -------------------------------------------------------------- //


template <class T>
class vector: public podvec<T>
{
protected:
    enum { Tsize = sizeof(T) };
    typedef podvec<T> parent;
    typedef T* Tptr;
    typedef Tptr& Tref;

    class cont: public container
    {
    protected:

        void finalize(void* p, memint len)
        {
            (char*&)p += len - Tsize;
            for ( ; len; len -= Tsize, Tref(p)--)
                Tptr(p)->~T();
        }

        void copy(void* dest, const void* src, memint len)
        {
            for ( ; len; len -= Tsize, Tref(dest)++, Tref(src)++)
                new(dest) T(*Tptr(src));
        }

        cont(memint cap, memint siz): container(cap, siz)  { }

    public:
        static container* allocate(memint cap, memint siz)
            { return new(cap) cont(cap, siz); }

        ~cont()
            { if (_size) { finalize(data(), _size); _size = 0; } }
    };

public:
    vector(): parent()  { }

    // Override stuff that requires allocation of 'vector::cont'
    void insert(memint pos, const T& t)
            { new(bytevec::_insert(pos * Tsize, Tsize, cont::allocate)) T(t); }
    void push_back(const T& t)
            { new(bytevec::_append(Tsize, cont::allocate)) T(t); }
    void resize(memint newsize)
            { notimpl(); /* bytevec::_resize(newsize, cont::allocate); */ }

    // Give a chance to alternative constructors, e.g. str can be constructed
    // from (const char*). Without these templates below temp objects are
    // created and then copied into the vector. Though these are somewhat
    // dangerous too.
    template <class U>
        void insert(memint pos, const U& u)
            { new(bytevec::_insert(pos * Tsize, Tsize, cont::allocate)) T(u); }
    template <class U>
        void push_back(const U& u)
            { new(bytevec::_append(Tsize, cont::allocate)) T(u); }
    template <class U>
        void replace(memint i, const U& u)
            { parent::atw(i) = u; }

    bool find_insert(const T& item)
    {
        if (parent::empty())
        {
            push_back(item);
            return true;
        }
        else
            return parent::find_insert(item);
    }
};


// --- dict ---------------------------------------------------------------- //


template <class Tkey, class Tval>
class dict
{
protected:

    void chkidx(memint i) const     { if (umemint(i) >= umemint(size())) container::idxerr(); }

    class dictobj: public object
    {
    public:
        vector<Tkey> keys;
        vector<Tval> values;
    };

    dictobj* _obj;

public:
    dict()                      : _obj(NULL)  { }
    dict(const dict& d)         : _obj(d._obj)  { if (_obj) _obj->grab(); }
    ~dict()                     { if (_obj) _obj->release(); }

    bool empty() const          { return _obj == NULL; }
    memint size() const         { return _obj ? _obj->keys->size() : 0; }
    bool operator== (const dict& d) const { return _obj == d._obj; }

    void clear()                { if (_obj) { _obj->release(); _obj = NULL; } }
    void operator= (const dict&);

    const Tkey& key(memint i) const  { chkidx(i); return _obj->keys[i];  }
    const Tval& value(memint i) const  { chkidx(i); return _obj->values[i];  }
    void replace(memint i, const Tval& v)  { chkidx(i); _obj->values.replace(i, v); }
    void erase(memint i)  { chkidx(i); _obj->keys.erase(i); _obj->values.erase(i); }

    struct item_type { const Tkey& key; Tval& value; };
    item_type operator[] (memint i) const
    {
        chkidx(i);
        item_type r;
        r.key = _obj->keys[i];
        r.value = _obj->values[i];
        return r;
    }

    const Tval* find(const Tkey& k) const
    {
        memint i;
        if (bsearch(k, i))
            return &_obj->values[i];
        else
            return NULL;
    }

    void find_replace(const Tkey& k, const Tval& v)
    {
        memint i;
        if (!bsearch(k, i))
        {
            if (_obj == NULL)
                (_obj = new dictobj())->grab();
            _obj->keys.insert(i, k);
            _obj->values.insert(i, v);
        }
        else
            replace(i, v);
    }

    void find_erase(const Tkey& k)
    {
        memint i;
        if (bsearch(k, i))
            erase(i);
    }

    memint compare(memint i, const Tkey& k) const
        { comparator<Tkey> comp; return comp(_obj->keys[i], k); }

    bool bsearch(const Tkey& k, memint& i) const
        { return ::bsearch(*this, size() - 1, k, i); }
};


// ------------------------------------------------------------------------- //

// --- tests --------------------------------------------------------------- //


// #include "typesys.h"


void ut_fail(unsigned line, const char* e)
{
    fprintf(stderr, "%s:%u: test failed `%s'\n", __FILE__, line, e);
    exit(200);
}

#define fail(e)     ut_fail(__LINE__, e)
#define check(e)    { if (!(e)) fail(#e); }

#define check_throw(a) \
    { bool chk_throw = false; try { a; } catch(exception&) { chk_throw = true; } check(chk_throw); }


void test_vector()
{
    vector<str> v1;
    v1.push_back("ABC");
    check(v1[0] == "ABC");
    vector<str> v2 = v1;
    check(v2[0] == "ABC");
    v1.push_back("DEF");
    v1.push_back("GHI");
    v1.push_back("JKL");
    vector<str> v3 = v1;
    check(v1.size() == 4);
    check(v2.size() == 1);
    check(v3.size() == 4);
    str s1 = "ABC";
    check(v1[0] == s1);
    check(v1[1] == "DEF");
    check(v1[2] == "GHI");
    check(v1[3] == "JKL");
    v1.erase(2);
    check(v1[0] == "ABC");
    check(v1[1] == "DEF");
    check(v1[2] == "JKL");
    check(v1.back() == "JKL");
    v3 = v1;
    v1.replace(2, "MNO");
    check(v1[2] == "MNO");
    check(v3[2] == "JKL");
}


#ifdef XCODE
    const char* filePath = "../../src/tests/test.shn";
#else
    const char* filePath = "tests/test.shn";
#endif


int main()
{
//    sio << "Shannon v" << SHANNON_VERSION_MAJOR
//        << '.' << SHANNON_VERSION_MINOR
//        << '.' << SHANNON_VERSION_FIX
//        << " Copyright (c) 2008-2010 Hovik Melikyan" << endl << endl;

    test_vector();

    int exitcode = 0;
/*
    initTypeSys();
    try
    {
        Context context;
        variant result = context.execute(filePath);

        if (result.is_none())
            exitcode = 0;
        else if (result.is_ord())
            exitcode = int(result._ord());
        else if (result.is_str())
        {
            serr << result._str() << endl;
            exitcode = 102;
        }
        else
            exitcode = 103;
    }
    catch (exception& e)
    {
        serr << "Error: " << e.what() << endl;
        exitcode = 101;
    }
    doneTypeSys();
*/

#ifdef DEBUG
    if (object::allocated != 0)
    {
        fprintf(stderr, "object::allocated: %d\n", object::allocated);
        _fatal(0xff01);
    }
#endif
    return exitcode;
}

