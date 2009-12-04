
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "runtime.h"
#include "typesys.h"


// --- object & objptr ----------------------------------------------------- //


int object::allocated = 0;


void outofmem()
{
    fatal(0x1001, "Out of memory");
}


object* object::_realloc(object* p, size_t self, memint extra)
{
    assert(p->unique());
    assert(self > 0 && extra >= 0);
    p = (object*)::realloc(p, self + extra);
    if (p == NULL)
        outofmem();
    return p;
}


void object::_assignto(object*& p)
    { if (p != this) { p->release(); p = this->ref(); } }


void* object::operator new(size_t self)
{
    void* p = ::malloc(self);
    if (p == NULL)
        outofmem();
#ifdef DEBUG
    pincrement(&object::allocated);
#endif
    return p;
}


void* object::operator new(size_t self, memint extra)
{
    assert(self + extra > 0);
    void* p = ::malloc(self + extra);
    if (p == NULL)
        outofmem();
#ifdef DEBUG
    pincrement(&object::allocated);
#endif
    return p;
}


void object::operator delete(void* p)
{
    assert(((object*)p)->refcount == 0);
#ifdef DEBUG
    pdecrement(&object::allocated);
#endif
    ::free(p);
}


object::~object()  { }

bool object::empty() const  { return false; }

Type* object::type() const  { return NULL; }


void object::release()
{
    if (this == NULL)
        return;
    assert(refcount > 0); // 0 means static
    if (pdecrement(&refcount) == 0)
        delete this;
}


// --- container & contptr ------------------------------------------------- //


container _null_container;


void container::overflow()
    { fatal(0x1002, "Container overflow"); }


void container::idxerr()
    { fatal(0x1003, "Container index error"); }


container::~container()
    { }


bool container::empty() const // virt. override
    { return _size == 0; }


container* container::new_(memint cap, memint siz)
{
    return new(cap) container(cap, siz);
}


container* container::null_obj()
{
    return &_null_container;
}


void container::finalize(void*, memint)  { }


void container::copy(void* dest, const void* src, memint len)
    { ::memcpy(dest, src, len); }


inline memint container::calc_prealloc(memint newsize)
{
    if (newsize <= 32)
        return 64;
    else
        return newsize + newsize / 2;
}


/*
inline bool container::can_shrink(memint newsize)
{
    return newsize > 64 && newsize < _capacity / 2;
}
*/

container* container::new_growing(memint newsize)
{
    if (newsize <= 0)
        overflow();
    memint newcap = _capacity > 0 ? calc_prealloc(newsize) : newsize;
    if (newcap <= 0)
        overflow();
    return new_(newcap, newsize);
}


container* container::new_precise(memint newsize)
{
    if (newsize <= 0)
        overflow();
    return new_(newsize, newsize);
}


container* container::realloc(memint newsize)
{
    if (newsize <= 0)
        overflow();
    assert(unique());
    assert(newsize > _capacity || newsize < _size);
    _size = newsize;
    _capacity = _size > _capacity ? calc_prealloc(_size) : _size;
    if (_capacity <= 0)
        overflow();
    return (container*)_realloc(this, sizeof(*this), _capacity);
}


const char* contptr::back(memint i) const
{
    if (i <= 0 || i > size())
        container::idxerr();
    return obj->end() - i;
}


char* contptr::_init(container* factory, memint len)
{
    assert(len >= 0);
    if (len > 0)
    {
        obj = factory->new_growing(len)->ref();
        return obj->data();
    }
    else
    {
        obj = factory->null_obj();
        return NULL;
    }
}


void contptr::_init(container* factory, const char* buf, memint len)
{
    char* p = _init(factory, len);
    if (p)
        factory->copy(p, buf, len);
}


void contptr::_dofin()
{
    obj->finalize(obj->data(), obj->size());
    obj->release();
}


void contptr::operator= (const contptr& s)
{
    if (obj != s.obj)
        _assign(s.obj);
}


void contptr::assign(const char* buf, memint len)
{
    container* null = obj->null_obj();
    _fin();
    _init(null, buf, len);
}


void contptr::clear()
{
    if (!empty())
    {
        container* null = obj->null_obj();
        _fin();
        obj = null;
    }
}


char* contptr::mkunique()
{
    if (empty() || unique())
        return obj->data();
    else
    {
        container* o = obj->new_precise(obj->size());
        obj->copy(o->data(), obj->data(), obj->size());
        _assign(o);
        return obj->data();
    }
}


char* contptr::_insertnz(memint pos, memint len)
{
    assert(len > 0);
    chkidxa(pos);
    memint oldsize = size();
    memint newsize = oldsize + len;
    memint remain = oldsize - pos;
    if (unique())
    {
        if (newsize > obj->capacity())
            obj = obj->realloc(newsize);
        else
            obj->set_size(newsize);
        char* p = obj->data(pos);
        if (remain)
            ::memmove(p + len, p, remain);
        return p;
    }
    else
    {
        container* o = obj->new_growing(newsize);
        if (pos)
            obj->copy(o->data(), obj->data(), pos);
        char* p = o->data(pos);
        if (remain)
            obj->copy(p + len, obj->data(pos), remain);
        _assign(o);
        return p;
    }
}


char* contptr::_appendnz(memint len)
{
    assert(len > 0);
    memint oldsize = size();
    memint newsize = oldsize + len;
    if (unique())
    {
        if (newsize > obj->capacity())
            obj = obj->realloc(newsize);
        else
            obj->set_size(newsize);
        return obj->data(oldsize);
    }
    else
    {
        container* o = obj->new_growing(newsize);
        if (oldsize)
            obj->copy(o->data(), obj->data(), oldsize);
        _assign(o);
        return obj->data(oldsize);
    }
}


void contptr::_erasenz(memint pos, memint len)
{
    chkidx(pos);
    memint oldsize = size();
    memint epos = pos + len;
    chkidxa(epos);
    memint newsize = oldsize - len;
    memint remain = oldsize - epos;
    if (newsize == 0)
        clear();
    else if (unique())
    {
        char* p = obj->data(pos);
        obj->finalize(p, len);
        if (remain)
            ::memmove(p, p + len, remain);
        obj->set_size(newsize);
    }
    else
    {
        container* o = obj->new_precise(newsize);
        if (pos)
            obj->copy(o->data(), obj->data(), pos);
        if (remain)
            obj->copy(o->data(pos), obj->data(epos), remain);
        _assign(o);
    }
}


void contptr::_popnz(memint len)
{
    memint oldsize = size();
    memint newsize = oldsize - len;
    chkidx(newsize);
    if (newsize == 0)
        clear();
    else if (unique())
    {
        obj->finalize(obj->data(newsize), len);
        obj->set_size(newsize);
    }
    else
    {
        container* o = obj->new_precise(newsize);
        obj->copy(o->data(), obj->data(), newsize);
        _assign(o);
    }
}


void contptr::insert(memint pos, const char* buf, memint len)
{
    if (len > 0)
        obj->copy(_insertnz(pos, len), buf, len);
}


void contptr::insert(memint pos, const contptr& s)
{
    if (empty())
    {
        if (pos)
            container::idxerr();
        _init(s);
    }
    else
    {
        memint len = s.size();
        if (len)
        {
            // Be careful as s maybe the same as (*this)
            char* p = _insertnz(pos, len);
            obj->copy(p, s.data(), len);
        }
    }
}


void contptr::append(const char* buf, memint len)
{
    if (len > 0)
        obj->copy(_appendnz(len), buf, len);
}


void contptr::append(const contptr& s)
{
    if (empty())
        _init(s);
    else
    {
        memint len = s.size();
        if (len)
        {
            // Be careful as s maybe the same as (*this)
            char* p = _appendnz(len);
            obj->copy(p, s.data(), len);
        }
    }
}


char* contptr::resize(memint newsize)
{
    if (newsize < 0)
        container::overflow();
    memint oldsize = size();
    if (newsize == oldsize)
        return NULL;
    else if (newsize == 0)
    {
        clear();
        return NULL;
    }
    else if (newsize < oldsize)
    {
        _erasenz(newsize, oldsize - newsize);
        return NULL;
    }
    else
        return _appendnz(newsize - oldsize);
}


void contptr::resize(memint newsize, char fill)
{
    memint oldsize = size();
    char* p = resize(newsize);
    if (p)
        memset(p, fill, newsize - oldsize);
}



// --- string -------------------------------------------------------------- //


strcont _null_strcont;


Type* strcont::type() const
    { return defString; }

container* strcont::new_(memint cap, memint siz)
    { return new(cap) strcont(cap, siz); }

container* strcont::null_obj()
    { return &_null_strcont; }


void str::_init(const char* buf, memint len)
{
    if (len > 0)
    {
        // Reserve extra byte for the NULL char
        contptr::_init(&_null_strcont, buf, len + 1);
        obj->dec_size();
    }
    else
        _init();
}


str::str(const char* s)
{
    memint len = pstrlen(s);
    if (len > 0)
        _init(s, len);
    else
        _init();
}


const char* str::c_str() const
{
    if (empty())
        return "";
    else if (obj->has_room())
        *obj->end() = 0;
    else
    {
        ((str*)this)->push_back(char(0));
        obj->dec_size();
    }
    return obj->data();
}


void str::put(memint pos, char c)
{
    chkidx(pos);
    mkunique()[pos] = c;
}


memint str::find(char c) const
{
    if (empty())
        return npos;
    const char* p = data();
    const char* f = (const char*)::memchr(p, c, size());
    if (f == NULL)
        return npos;
    return f - p;
}


memint str::rfind(char c) const
{
    if (empty())
        return npos;
    const char* b = data();
    const char* p = b + size();
    do
    {
        if (*p == c)
            return p - b;
        p--;
    }
    while (p >= b);
    return npos;
}


int str::cmp(const char* s, memint blen) const
{
    memint alen = size();
    memint len = imin(alen, blen);
    if (len == 0)
        return int(alen - blen);
    int result = ::memcmp(data(), s, len);
    if (result == 0)
        return int(alen - blen);
    else
        return result;
}


void str::operator+= (const char* s)
{
    memint len = pstrlen(s);
    if (len > 0)
    {
        append(s, len + 1);
        obj->dec_size();
    }
}


str str::substr(memint pos, memint len) const
{
    if (pos == 0 && len == size())
        return *this;
    if (len <= 0)
        return str();
    chkidxa(pos);
    chkidxa(pos + len);
    return str(data(pos), len);
}


str str::substr(memint pos) const
{
    if (pos == 0)
        return *this;
    chkidxa(pos);
    return str(data(pos), size() - pos);
}


// --- string utilities ---------------------------------------------------- //


static const char* _itobase(long long value, char* buf, int base, int& len, bool _signed)
{
    // internal conversion routine: converts the value to a string 
    // at the end of the buffer and returns a pointer to the first
    // character. this is to get rid of copying the string to the 
    // beginning of the buffer, since finally the string is supposed 
    // to be copied to a dynamic string in itostring(). the buffer 
    // must be at least 65 bytes long.

    static char digits[65] = 
        "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    char* pdigits;
    if (base > 36)
	pdigits = digits;       // start from '.'
    else
	pdigits = digits + 2;   // start from '0'
    
    int i = 64;
    buf[i] = 0;

    bool neg = false;
    unsigned long long v = value;
    if (_signed && base == 10 && value < 0)
    {
        v = -value;
        // since we can't handle the lowest signed value, we just return a built-in string.
        if (((long long)v) < 0)   // an minimum value negated results in the same value
        {
            if (sizeof(value) == 8)
            {
                len = 20;
                return "-9223372036854775808";
            }
            else if (sizeof(value) == 4)
            {
                len = 11;
                return "-2147483648";
            }
            else
                abort();
        }
        neg = true;
    }

    do
    {
        buf[--i] = pdigits[unsigned(v % base)];
        v /= base;
    } while (v > 0);

    if (neg)
        buf[--i] = '-';

    len = 64 - i;
    return buf + i;
}


static void _itobase2(str& result, long long value, int base, int width, char padchar, bool _signed)
{
    result.clear();

    if (base < 2 || base > 64)
        return;

    char buf[65];   // the longest possible string is when base=2
    int reslen;
    const char* p = _itobase(value, buf, base, reslen, _signed);

    if (width > reslen)
    {
        if (padchar == 0)
        {
            // default pad char
            if (base == 10)
                padchar = ' ';
            else if (base > 36)
                padchar = '.';
            else
                padchar = '0';
        }

        bool neg = *p == '-';
        if (neg) { p++; reslen--; }
        width -= reslen;
        if (width > 0)
            result.resize(width, padchar);
        result.append(p, reslen);
        if (neg)
            result.put(0, '-');
    }
    else 
        result.assign(p, reslen);
}


str _to_string(long long value, int base, int width, char padchar) 
{
    str result;
    _itobase2(result, value, base, width, padchar, true);
    return result;
}


str _to_string(long long value)
{
    str result;
    _itobase2(result, value, 10, 0, ' ', true);
    return result;
}


str _to_string(memint value)
{
    str result;
    _itobase2(result, value, 10, 0, ' ', false);
    return result;
}


unsigned long long from_string(const char* p, bool* error, bool* overflow, int base)
{
    *error = false;
    *overflow = false;

    if (p == 0 || *p == 0 || base < 2 || base > 64)
        { *error = true; return 0; }

    unsigned long long result = 0;

    do 
    {
        int c = *p++;

        if (c >= 'a')
        {
            // for the numeration bases that use '.', '/', digits and
            // uppercase letters the letter case is insignificant.
            if (base <= 38)
                c -= 'a' - '9' - 1;
            else  // others use both upper and lower case letters
                c -= ('a' - 'Z' - 1) + ('A' - '9' - 1);
        }
        else if (c > 'Z')
            { *error = true; return 0; }
        else if (c >= 'A')
            c -= 'A' - '9' - 1;
        else if (c > '9')
            { *error = true; return 0; }

        c -= (base > 36) ? '.' : '0';
        if (c < 0 || c >= base)
            { *error = true; return 0; }

        unsigned long long t = result * unsigned(base);
        if (t / base != result)
            { *overflow = true; return 0; }
        result = t;
        t = result + unsigned(c);
        if (t < result)
            { *overflow = true; return 0; }
        result = t;

    }
    while (*p != 0);

    return result;
}


str remove_filename_path(const str& fn)
{
    memint i = fn.rfind('/');
    if (i == str::npos)
    {
        i = fn.rfind('\\');
        if (i == str::npos)
            return fn;
    }
    return fn.substr(i + 1);
}


str remove_filename_ext(const str& fn)
{
    memint i = fn.rfind('.');
    if (i == str::npos)
        return fn;
    return fn.substr(0, i);
}


