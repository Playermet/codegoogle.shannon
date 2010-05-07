

#include "common.h"


void _fatal(int code, const char* msg) 
{
#ifdef DEBUG
    // We want to see the stack backtrace in XCode debugger
    assert(code == 0);
#else
    fprintf(stderr, "\nInternal 0x%04x: %s\n", code, msg);
#endif
    exit(100);
}


void _fatal(int code) 
{
#ifdef DEBUG
    assert(code == 0);
#else
    fprintf(stderr, "\nInternal error [%04x]\n", code);
#endif
    exit(100);
}


void notimpl()
{
    fatal(0x0001, "Feature not implemented yet");
}


exception::exception() throw()  { }
exception::~exception() throw()  { }

void outofmemory()
{
    fatal(0x0001, "Out of memory");
}

static void newdel()
{
    fatal(0x0002, "Global new/delete are disabled");
}

void* operator new(size_t) throw()       { newdel(); return NULL; }
void* operator new[](size_t) throw()     { newdel(); return NULL; }
void  operator delete  (void*) throw()   { newdel(); }
void  operator delete[](void*) throw()   { newdel(); }


#ifdef SHN_THR

#if defined(__GNUC__) && (defined(__i386__) || defined(__I386__)|| defined(__x86_64__))
// multi-threaded version with GCC on i386


atomicint pincrement(atomicint* target)
{
    atomicint temp = 1;
    __asm__ __volatile ("lock ; xaddl %0,(%1)" : "+r" (temp) : "r" (target));
    return temp + 1;
}


atomicint pdecrement(atomicint* target)
{
    atomicint temp = -1;
    __asm__ __volatile ("lock ; xaddl %0,(%1)" : "+r" (temp) : "r" (target));
    return temp - 1;
}


#else

#error Undefined architecture: atomic functions are not available

#endif

#endif


// --- charset ------------------------------------------------------------- //


static unsigned char lbitmask[8] = {0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80};
static unsigned char rbitmask[8] = {0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff};

const char charsetesc = '~';


void charset::include(int min, int max)
{
    if (uchar(min) > uchar(max))
        return;
    int lidx = uchar(min) / 8;
    int ridx = uchar(max) / 8;
    uchar lbits = lbitmask[uchar(min) % 8];
    uchar rbits = rbitmask[uchar(max) % 8];

    if (lidx == ridx) 
    {
        data[lidx] |= lbits & rbits;
    }
    else 
    {
        data[lidx] |= lbits;
        for (int i = lidx + 1; i < ridx; i++)
            data[i] = uchar(-1);
        data[ridx] |= rbits;
    }
}


static unsigned hex4(unsigned c) 
{
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= '0' && c <= '9')
        return c - '0';
    return 0;    
}


static unsigned parsechar(const char*& p) 
{
    unsigned ret = *p;
    if (ret == unsigned(charsetesc))
    {
        p++;
        ret = *p;
        if ((ret >= '0' && ret <= '9') || (ret >= 'a' && ret <= 'f') || (ret >= 'A' && ret <= 'F'))
        {
            ret = hex4(ret);
            p++;
            if (*p != 0)
                ret = (ret << 4) | hex4(*p);
        }
    }
    return ret;
}


void charset::assign(const char* p) 
{
    if (*p == '*' && *(p + 1) == 0)
        fill();
    else 
    {
        clear();
        for (; *p != 0; p++) {
            uchar left = parsechar(p);
            if (*(p + 1) == '-')
            {
                p += 2;
                uchar right = parsechar(p);
                include(left, right);
            }
            else
                include(left);
        }
    }
}


void charset::assign(const charset& s)
    { memcpy(data, s.data, BYTES); }


bool charset::empty() const
{
    for(int i = 0; i < WORDS; i++) 
        if (*((unsigned*)(data) + i) != 0)
            return false;
    return true;
}


void charset::unite(const charset& s) 
{
    for(int i = 0; i < WORDS; i++) 
        *((unsigned*)(data) + i) |= *((unsigned*)(s.data) + i);
}


void charset::subtract(const charset& s) 
{
    for(int i = 0; i < WORDS; i++) 
        *((unsigned*)(data) + i) &= ~(*((unsigned*)(s.data) + i));
}


void charset::intersect(const charset& s) 
{
    for(int i = 0; i < WORDS; i++) 
        *((unsigned*)(data) + i) &= *((unsigned*)(s.data) + i);
}


void charset::invert() 
{
    for(int i = 0; i < WORDS; i++) 
        *((unsigned*)(data) + i) = ~(*((unsigned*)(data) + i));
}


bool charset::le(const charset& s) const 
{
    for (int i = 0; i < WORDS; i++) 
    {
        int w1 = *((unsigned*)(data) + i);
        int w2 = *((unsigned*)(s.data) + i);
        if ((w2 | w1) != w2)
            return false;
    }
    return true;
}


// --- object -------------------------------------------------------------- //


atomicint object::allocated = 0;


object::~object()  { }


void object::release()
{
    assert(_refcount > 0);
    if (pdecrement(&_refcount) == 0)
        delete this;
}


void object::_assignto(object*& p)
{
    if (p != this)
    {
        if (p)
            p->release();
        p = this;
        if (this)
            this->grab();
    }
}


void* object::operator new(size_t self)
{
    void* p = ::pmemalloc(self);
#ifdef DEBUG
    pincrement(&object::allocated);
#endif
    return p;
}


void* object::operator new(size_t self, memint extra)
{
    assert(self + extra > 0);
    void* p = ::pmemalloc(self + extra);
#ifdef DEBUG
    pincrement(&object::allocated);
#endif
    return p;
}


void object::operator delete(void* p)
{
    assert(((object*)p)->_refcount == 0);
#ifdef DEBUG
    pdecrement(&object::allocated);
#endif
    ::pmemfree(p);
}


object* object::dup(size_t self, memint extra)
{
    assert(self + extra > 0);
    assert(self >= sizeof(*this));
    object* o = (object*)::pmemalloc(self + extra);
#ifdef DEBUG
    pincrement(&object::allocated);
#endif    
    memcpy(o, this, self);
    o->_refcount = 0;
    return o;
}


object* object::reallocate(object* p, size_t self, memint extra)
{
    assert(p->_refcount == 1);
    assert(self > 0 && extra >= 0);
    return (object*)::pmemrealloc(p, self + extra);
}


// --- container ----------------------------------------------------------- //


void container::overflow()
    { fatal(0x1002, "Container overflow"); }

void container::idxerr()
    { fatal(0x1003, "Container index error"); }


container::~container()
    { }  // must call finalize() in descendant classes

void container::finalize(void*, memint)
    { }

void container::copy(void* dest, const void* src, memint len)
    { ::memcpy(dest, src, len); }


container* container::allocate(memint cap, memint siz)
{
    assert(siz <= cap);
    assert(siz >= 0);
    if (cap == 0)
        return NULL;
    return new(cap) container(cap, siz);
}


inline memint container::_calc_prealloc(memint newsize)
{
    if (newsize <= memint(8 * sizeof(memint)))
        return 12 * sizeof(memint);
    else
        return newsize + newsize / 2;
}


container* container::reallocate(container* p, memint newsize)
{
    if (newsize < 0)
        overflow();
    if (newsize == 0)
    {
        delete p;
        return NULL;
    }
    assert(p);
    assert(p->unique());
    assert(newsize > p->_capacity || newsize < p->_size);
    p->_capacity = newsize > p->_capacity ? _calc_prealloc(newsize) : newsize;
    if (p->_capacity <= 0)
        overflow();
    p->_size = newsize;
    return (container*)object::reallocate(p, sizeof(*p), p->_capacity);
}


container* container::dup(memint cap, memint siz)
{
    assert(cap > 0);
    assert(siz > 0 && siz <= cap);
    container* c = (container*)object::dup(sizeof(container), cap);
    c->_capacity = cap;
    c->_size = siz;
    return c;
}


// --- bytevec ------------------------------------------------------------- //


void bytevec::_init(const bytevec& v)
{
    _data = v._data;
    if (_data)
        _cont()->grab();
}


char* bytevec::_init(memint len)
{
    chknonneg(len);
    if (len == 0)
        _init();
    else
        _data = container::allocate(len, len)->grab<container>()->data();
    return _data;
}


void bytevec::_init(memint len, char fill)
{
    if (_init(len))
        ::memset(_data, fill, len);
}


void bytevec::_init(const char* buf, memint len)
{
    if (_init(len))
        ::memcpy(_data, buf, len);
}


char* bytevec::_mkunique()
{
    if (!_unique())  // implies non-empty too
    {
        container* c = _cont()->dup(size(), size());
        c->copy(c->data(), _data, size());
        _assign(c);
    }
    return _data;
}


void bytevec::operator= (const bytevec& v)
{
    if (_data != v._data)
        if (v.empty())
            clear();
        else
            _assign(v._cont());
}


void bytevec::assign(const char* buf, memint len)
    { _fin(); _init(buf, len); }


void bytevec::clear()
{
    if (!empty())
    {
        _cont()->finalize(_data, size());
        _fin();
        _init();
    }
}


char* bytevec::_insert(memint pos, memint len, alloc_func alloc)
{
    chknonneg(len);
    chkidxa(pos);
    memint oldsize = size();
    memint newsize = oldsize + len;
    memint remain = oldsize - pos;
    if (empty() || !_unique())
    {
        // Note: first allocation sets capacity = size
        objptr<container> c = alloc(newsize, newsize);  // _cont()->dup(newsize, newsize);
        if (pos > 0)  // copy the first chunk, before 'pos'
            c->copy(c->data(), _data, pos);
        if (remain)  // copy the the remainder
            c->copy(c->data() + pos + len, _data + pos, remain);
        _assign(c);
    }
    else  // if unique
    {
        if (newsize > capacity())
            _data = container::reallocate(_cont(), newsize)->data();
        else
            _cont()->set_size(newsize);
        if (remain)
            ::memmove(_data + pos + len, _data + pos, remain);
    }
    return _data + pos;
}


char* bytevec::_append(memint len, alloc_func alloc)
{
    // _insert(0, len) would do, but we want a faster function
    assert(len > 0);
    memint oldsize = size();
    memint newsize = oldsize + len;
    if (empty() || !_unique())
    {
        // Note: first allocation sets capacity = size
        objptr<container> c = alloc(newsize, newsize); // _cont()->dup(newsize, newsize);
        if (oldsize > 0)
            c->copy(c->data(), _data, oldsize);
        _assign(c);
    }
    else  // if unique
    {
        if (newsize > capacity())
            _data = container::reallocate(_cont(), newsize)->data();
        else
            _cont()->set_size(newsize);
    }
    return _data + oldsize;
}


void bytevec::_erase(memint pos, memint len)
{
    assert(len > 0);
    chkidx(pos);
    memint oldsize = size();
    memint epos = pos + len;
    chkidxa(epos);
    memint newsize = oldsize - len;
    memint remain = oldsize - epos;
    if (newsize == 0)  // also if empty
        clear();
    else if (!_unique())
    {
        container* c = _cont()->dup(newsize, newsize);
        if (pos)
            _cont()->copy(c->data(), _data, pos);
        if (remain)
            _cont()->copy(c->data() + pos, _data + epos, remain);
        _assign(c);
    }
    else // if unique
    {
        char* p = _data + pos;
        _cont()->finalize(p, len);
        if (remain)
            ::memmove(p, p + len, remain);
        _cont()->set_size(newsize);
    }
}


void bytevec::_pop(memint len)
{
    memint oldsize = size();
    memint newsize = oldsize - len;
    chkidx(newsize);
    if (newsize == 0)
        clear();
    else if (!_unique())
    {
        container* c = _cont()->dup(newsize, newsize);
        c->copy(c->data(), _data, newsize);
        _assign(c);
    }
    else // if unique
    {
        _cont()->finalize(_data + newsize, len);
        _cont()->set_size(newsize);
    }
}


void bytevec::insert(memint pos, const char* buf, memint len)
{
    if (len > 0)
    {
        char* p = _insert(pos, len, container::allocate);
        _cont()->copy(p, buf, len);
    }
}


void bytevec::insert(memint pos, const bytevec& v)
{
    if (empty())
    {
        if (pos)
            container::idxerr();
        _init(v);
    }
    else if (!v.empty())
    {
        memint len = v.size();
        // Note: should be done in two steps so that the case (v == *this) works
        char* p = _insert(pos, len, container::allocate);
        _cont()->copy(p, v.data(), len);
    }
}


void bytevec::append(const char* buf, memint len)
{
    if (len > 0)
    {
        char* p = _append(len, container::allocate);
        _cont()->copy(p, buf, len);
    }
}


void bytevec::append(const bytevec& v)
{
    if (empty())
        _init(v);
    else if (!v.empty())
    {
        memint len = v.size();
        // Note: should be done in two steps so that the case (v == *this) works
        char* p = _append(len, container::allocate);
        _cont()->copy(p, v.data(), len);
    }
}


void bytevec::erase(memint pos, memint len)
{
    if (len > 0)
        _erase(pos, len);
}


char* bytevec::_resize(memint newsize, alloc_func alloc)
{
    chknonneg(newsize);
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
        _erase(newsize, oldsize - newsize);
        return NULL;
    }
    else
        return _append(newsize - oldsize, alloc);
}


void bytevec::resize(memint newsize, char fill)
{
    memint oldsize = size();
    char* p = resize(newsize);
    if (p)
        ::memset(p, fill, newsize - oldsize);
}


// --- str ----------------------------------------------------------------- //


void str::_init(const char* buf)
    { bytevec::_init(buf, pstrlen(buf)); }


const char* str::c_str()
{
    if (empty())
        return "";
    container* c = _cont();
    if (c->unique() && c->size() < c->capacity())
        *c->end() = 0;
    else
    {
        ((str*)this)->push_back(char(0));
        _cont()->dec_size();
    }
    return _data;
}


void str::operator= (const char* s)
    { _fin(); _init(s); }

void str::operator= (char c)
    { _fin(); _init(c); }


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
    const char* p = b + size() - 1;
    do
    {
        if (*p == c)
            return p - b;
        p--;
    }
    while (p >= b);
    return npos;
}


memint str::compare(const char* s, memint blen) const
{
    memint alen = size();
    memint len = imin(alen, blen);
    if (len == 0)
        return alen - blen;
    int result = ::memcmp(data(), s, len);
    if (result == 0)
        return alen - blen;
    else
        return result;
}


bool str::operator== (const char* s) const
    { return compare(s, pstrlen(s)) == 0; }


void str::operator+= (const char* s)
    { append(s, pstrlen(s)); }

void str::insert(memint pos, const char* s)
    { bytevec::insert(pos, s, pstrlen(s)); }


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


static const char* _itobase(large value, char* buf, int base, int& len, bool _signed)
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
    ularge v = value;
    if (_signed && base == 10 && value < 0)
    {
        v = -value;
        // since we can't handle the lowest signed value, we just return a built-in string.
        if (large(v) < 0)   // a minimum value negated results in the same value
        {
            if (sizeof(value) == 8)
            {
                len = 20;
                return "-9223372036854775808";
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


static void _itobase2(str& result, large value, int base, int width, char padchar, bool _signed)
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
            result.replace(0, '-');
    }
    else 
        result.assign(p, reslen);
}


str _to_string(large value, int base, int width, char padchar) 
{
    str result;
    _itobase2(result, value, base, width, padchar, true);
    return result;
}


str _to_string(large value)
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


ularge from_string(const char* p, bool* error, bool* overflow, int base)
{
    *error = false;
    *overflow = false;

    if (p == 0 || *p == 0 || base < 2 || base > 64)
        { *error = true; return 0; }

    ularge result = 0;

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

        ularge t = result * unsigned(base);
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


static const charset printable_chars = "~20-~7E~81-~FE";


static void _to_printable(char c, str& s)
{
    if (c == '\\')
        s += "\\\\";
    else if (c == '\'')
        s += "\\\'";
    else if (printable_chars[c])
        s.append(&c, 1);
    else
    {
        s += "\\x";
        s += to_string(uchar(c), 16, 2, '0');
    }
}


str to_printable(char c)
{
    str result;
    _to_printable(c, result);
    return result;
}


str to_printable(const str& s)
{
    str result;
    for (memint i = 0; i < s.size(); i++)
        _to_printable(s[i], result);
    return result;
}


str to_quoted(char c)
    { return "'" + to_printable(c) + "'"; }


str to_quoted(const str& s)
    { return "'" + to_printable(s) + "'"; }


// --- object collections -------------------------------------------------- //


// template class podvec<object*>;


void objvec_impl::release_all()
{
    for (object* const* o = end(); o != begin(); o--)
        if (*o)
            (*o)->release();
    clear();
}


symbol::~symbol()  { }


memint symtbl::compare(memint i, const str& key) const
    { comparator<str> comp; return comp(operator[](i)->name, key); }


bool symtbl::bsearch(const str& key, memint& index) const
    { return ::bsearch(*this, parent::size() - 1, key, index); }


// --- Exceptions ---------------------------------------------------------- //


ecmessage::ecmessage(const char* _msg) throw(): msg(_msg)  { }
ecmessage::~ecmessage() throw()  { }
const char* ecmessage::what() throw()  { return msg; }

emessage::emessage(const str& _msg) throw(): msg(_msg)  { }
emessage::emessage(const char* _msg) throw(): msg(_msg)  { }
emessage::~emessage() throw()  { }
const char* emessage::what() throw()  { return msg.c_str(); }


static str sysErrorStr(int code, const str& arg)
{
    // For some reason strerror_r() returns garbage on my 64-bit Ubuntu. That's unfortunately
    // not the only strange thing about this computer and OS. Could be me, could be hardware
    // or could be libc. Or all.
    // Upd: so I updated both hardware and OS, still grabage on 64 bit, but OK on 32-bit.
    // What am I doing wrong?
//    char buf[1024];
//    strerror_r(code, buf, sizeof(buf));
    str result = strerror(code);
    if (!arg.empty())
        result += " (" + arg + ")";
    return result;
}


esyserr::esyserr(int code, const str& arg) throw()
    : emessage(sysErrorStr(code, arg))  { }


esyserr::~esyserr() throw()  { }

