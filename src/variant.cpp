
#define __STDC_LIMIT_MACROS

#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <limits.h>

#include <exception>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <sstream>
#include <iostream>

#include "common.h"
#include "variant.h"

const variant null;
const str null_str;
const class _null_tuple: public tuple { } null_tuple;
const class _null_dict: public dict { } null_dict;
const class _null_ordset: public ordset { } null_ordset;
const class _null_set: public set { } null_set;
const class _null_range: public range { } null_range;


#ifdef DEBUG
int object::alloc = -5; // compensate the three static objects null_xxx above
#endif


object::object()
    : refcount(0)
{
#ifdef DEBUG
    pincrement(&object::alloc);
#endif
}


object::~object()
{
#ifdef DEBUG
    pdecrement(&object::alloc);
    assert(refcount == 0);
#endif
}


object* object::clone() const
    { fatal(0x1001, "object::clone() is not implemented"); return NULL; }
void object::dump(std::ostream& s) const { s << "object"; }
bool object::less_than(object* other) const { return this < other; }


void _release(object* o)
{
    if (o == NULL)
        return;
#ifdef DEBUG
    assert(o->refcount >= 1);
#endif
    if (pdecrement(&o->refcount) == 0)
        delete o;
}


void _replace(object*& p, object* o)
{
    if (p != o)
    {
        release(p);
        p = grab(o);
    }
}


void _unique(object*& o)
{
    object* p = grab(o->clone());
    release(o);
    o = p;
}


#define XCLONE(t) \
    object* t::clone() const { return new t(*this); }

XCLONE(range)
XCLONE(tuple)
XCLONE(dict)
XCLONE(ordset)
XCLONE(set)


range::~range()  { }

bool range::equals(const range& other) const
{
    return (empty() && other.empty())
        || (left == other.left && right == other.right);
}

bool range::less_than(object* o) const
{
    const range& other = *(range*)o;
    if (empty() && other.empty())
        return false;
    if (left < other.left)
        return true;
    if (left > other.left)
        return false;
    return right < other.right;
}

void range::dump(std::ostream& s) const
{
    if (!empty())
        s << left << ".." << right;
}

range* new_range(integer l, integer r)
{
    if (l > r)
        return NULL;
    return new range(l, r);
}


tuple::tuple()  { }
tuple::~tuple() { }

void tuple::erase(mem i, mem count)
{
    impl.erase(impl.begin() + i, impl.begin() + i + count - 1);
}

void tuple::dump(std::ostream& s) const
{
    foreach(tuple_impl::const_iterator, i, impl)
    {
        if (i != impl.begin())
            s << ", ";
        s << *i;
    }
}


dict::dict()    { }
dict::~dict()   { }

void dict::dump(std::ostream& s) const
{
    foreach(dict_impl::const_iterator, i, impl)
    {
        if (i != impl.begin())
            s << ", ";
        s << i->first << ": " << i->second;
    }
}


ordset::ordset()      { }
ordset::~ordset()     { }

void ordset::dump(std::ostream& s) const
{
    int cnt = 0;
    for (int i = 0; i < charset::BITS; i++)
        if (impl[i])
        {
            if (++cnt > 1)
                s << ", ";
            s << i;
        }
}


set::set()      { }
set::~set()     { }

void set::dump(std::ostream& s) const
{
    foreach(set_impl::const_iterator, i, impl)
    {
        if (i != impl.begin())
            s << ", ";
        s << *i;
    }
}


void variant::_init(const str& s)   { type = STR; ::new(&_str_write()) str(s); }
void variant::_init(const char* s)  { type = STR; ::new(&_str_write()) str(s); }
void variant::_init(range* r)       { type = RANGE; val._range = grab(r); }
void variant::_init(integer l, integer r) { type = RANGE; val._range = grab(new_range(l, r)); }
void variant::_init(tuple* t)       { type = TUPLE; val._tuple = grab(t); }
void variant::_init(dict* d)        { type = DICT; val._dict = grab(d); }
void variant::_init(ordset* s)      { type = ORDSET; val._ordset = grab(s); }
void variant::_init(set* s)         { type = SET; val._set = grab(s); }
void variant::_init(object* o)      { type = OBJECT; val._obj = grab(o); }


void variant::_init(const variant& other)
{
    type = other.type;
    switch (type)
    {
    case NONE:
        break;
    case BOOL: val._bool = other.val._bool; break;
    case CHAR: val._char = other.val._char; break;
    case INT:  val._int = other.val._int; break;
    case TINYSET: val._tinyset = other.val._tinyset; break;
    case REAL: val._real = other.val._real; break;
    case STR:
        ::new(&_str_write()) str(other._str_read());
        break;
    default:    // containers and objects
        val._obj = grab(other.val._obj);
        break;
    }
}

/*
#ifdef RANGE_CHECKING

#define CHK_SIGNED(t) { if (val._int < t##_MIN && val._int > t##_MAX) _range_err(); }
#define CHK_UNSIGNED(t) { if (val._int < 0 && val._int > t##_MAX) _range_err(); }

integer variant::_in_signed(integer s) const
{
    switch (s)
    {
    case 1: CHK_SIGNED(INT8); break;
    case 2: CHK_SIGNED(INT16); break;
#ifndef SH64
    case 4: CHK_SIGNED(INT32); break;
#endif
    }
    return val._int;
}

integer variant::_in_unsigned(integer s) const
{
    switch (s)
    {
    case 1: CHK_UNSIGNED(UINT8); break;
    case 2: CHK_UNSIGNED(UINT16); break;
#ifdef SH64
    case 4: CHK_UNSIGNED(UINT32); break;
    case 8: CHK_UNSIGNED(INT64); break; // we don't support unsigned 64-bit
#else
    case 4: CHK_UNSIGNED(INT32); break;
#endif
    }
    return val._int;
}

#endif // RANGE_CHECKING
*/


void variant::_fin2()
{
    switch (type)
    {
    case NONE:
    case BOOL:
    case CHAR:
    case INT:
    case TINYSET:
    case REAL:
        break;
    case STR:
        _str_write().~str();
        break;
    default:    // containers and objects
        release(val._obj);
        break;
    }
}


void variant::dump(std::ostream& s) const
{
    switch (type)
    {
    case NONE: s << "null"; break;
    case BOOL: s << (val._bool ? "true" : "false"); break;
    case CHAR: s << '\'' << val._char << '\''; break;
    case INT:  s << val._int; break;
    case TINYSET:
        {
            s << '[';
            int cnt = 0;
            for (int i = 0; i < TINYSET_BITS; i++)
                if (val._tinyset & (uinteger(1) << i))
                {
                    if (++cnt > 1)
                        s << ", ";
                    s << i;
                }
            s << ']';
        }
        break;
    case REAL: s << val._real; break;
    case STR:  s << '"' << _str_read() << '"'; break;
    default:    // containers and objects
        s << '[';
        if (val._obj != NULL)
            val._obj->dump(s);
        s << ']';
        break;
    }
}


str variant::to_string() const
{
    std::stringstream s;
    dump(s);
    return s.str();
}


bool variant::operator== (const variant& other) const
{
    if (type != other.type)
        return false;
    switch (type)
    {
    case NONE:      return true;
    case BOOL:      return val._bool == other.val._bool;
    case CHAR:      return val._char == other.val._char;
    case INT:       return val._int == other.val._int;
    case TINYSET:   return val._tinyset == other.val._tinyset;
    case REAL:      return val._real == other.val._real;
    case STR:       return _str_read() == other._str_read();
    case RANGE:     return _range_read().equals(other._range_read());
    case ORDSET:    return _ordset_read() == other._ordset_read();
    default:        return val._obj == other.val._obj; // TODO: a virtual call?
    }
}


bool variant::operator< (const variant& other) const
{
    if (type != other.type)
        return type < other.type;
    switch (type)
    {
    case NONE: return false;
    case BOOL: return val._bool < other.val._bool;
    case CHAR: return val._char < other.val._char;
    case INT:  return val._int < other.val._int;
    case TINYSET: return val._tinyset < other.val._tinyset;
    case REAL: return val._real < other.val._real;
    case STR:  return _str_read() < other._str_read();
    default:    // containers and objects
        if (val._obj == NULL)
            return other.val._obj != NULL;
        if (other.val._obj == NULL)
            return false;
        return val._obj->less_than(other.val._obj);
    }
}


void variant::_type_err() { throw evarianttype(); }
void variant::_range_err() { throw evariantrange(); }
void variant::_index_err() { throw evariantindex(); }


integer variant::as_ordinal() const
{
    switch (type)
    {
    case BOOL: return val._bool;
    case CHAR: return unsigned(val._char);
    case INT: return val._int;
    default: _type_err(); return 0;
    }
}


unsigned variant::as_tiny_int() const
{
    integer i = as_ordinal();
    if (i < 0 || i >= TINYSET_BITS)
        _range_err();
    return i;
}


unsigned variant::as_char_int() const
{
    integer i = as_ordinal();
    if (i < 0 || i >= 256)
        _range_err();
    return i;
}


// _xxx_write(): return a unique container implementation, create if necessary
// _xxx_read(): return a const ref to an object, possibly null_xxx if empty

#define XIMPL(t) \
    t& variant::_##t##_write() \
        { if (val._##t == NULL) val._##t = grab(new t()); \
          else  unique(val._##t); \
          return *val._##t; } \
    const t& variant::_##t##_read() const \
        { if (val._##t == NULL) return null_##t; \
          return *val._##t; }

XIMPL(range)
XIMPL(tuple)
XIMPL(dict)
XIMPL(ordset)
XIMPL(set)


mem variant::size() const
{
    switch (type)
    {
    case STR:   return _str_read().size();
    case TUPLE: return _tuple_read().size();
    case DICT:  return _dict_read().size();
    default: _type_err(); return 0;
    }
}


bool variant::empty() const
{
    switch (type)
    {
    case TINYSET: return val._tinyset == 0;
    case STR:   return _str_read().empty();
    case RANGE: if (val._range == NULL) return true; return _range_read().empty();
    case TUPLE: if (val._tuple == NULL) return true; return _tuple_read().empty();
    case DICT:  if (val._dict == NULL) return true; return _dict_read().empty();
    case ORDSET: if (val._ordset == NULL) return true; return _ordset_read().empty();
    case SET:   if (val._set == NULL) return true; return _set_read().empty();
    default: _type_err(); return false;
    }
}


void variant::resize(mem new_size)
{
    switch (type)
    {
    case STR:   _str_write().resize(new_size); break;
    case TUPLE: _tuple_write().resize(new_size); break;
    default: _type_err(); break;
    }
}


void variant::resize(mem n, char c)         { _req(STR); _str_write().resize(n, c); }
char variant::getch(mem i) const            { _req(STR); return _str_read()[i]; }
void variant::append(const str& s)          { _req(STR); _str_write().append(s); }
void variant::append(const char* s)         { _req(STR); _str_write().append(s); }
void variant::append(char c)                { _req(STR); _str_write().push_back(c); }
void variant::append(const variant& v)      { _req(STR); _str_write().append(v.as_str()); }
void variant::push_back(const variant& v)   { _req(TUPLE); _tuple_write().push_back(v); }


str variant::substr(mem index, mem count) const
{
    _req(STR);
    return _str_read().substr(index, count == mem(-1) ? str::npos : count);
}


void variant::insert(mem index, const variant& v)
{
    _req(TUPLE);
    tuple& t = _tuple_write();
    if (index < 0 || index > t.size())
        _index_err();
    t.insert(index, v);
}


void variant::put(mem index, const variant& value)
{
    _req(TUPLE);
    if (index < 0 || index >= _tuple_read().size())
        _index_err();
    _tuple_write().put(index, value);
}


void variant::tie(const variant& key, const variant& value)
{
    _req(DICT);
    dict& d = _dict_write();
    if (value.is_null())
        d.untie(key);
    else
        d[key] = value;
}


void variant::tie(const variant& key)
{
    switch (type)
    {
    case TINYSET: val._tinyset |= uinteger(1) << key.as_tiny_int(); break;
    case ORDSET: _ordset_write().tie(key.as_char_int()); break;
    case SET: _set_write().tie(key); break;
    default: _type_err(); break;
    }
}


void variant::assign(integer left, integer right)
{
    _fin();
    _init(left, right);
}


void variant::erase(mem index)
{
    switch (type)
    {
    case STR: _str_write().erase(index, 1); break;
    case TUPLE: _tuple_write().erase(index); break;
    default: _type_err(); break;
    }
}


void variant::erase(mem index, mem count)
{
    switch (type)
    {
    case STR: _str_write().erase(index, count); break;
    case TUPLE: _tuple_write().erase(index, count); break;
    default: _type_err(); break;
    }
}


void variant::untie(const variant& key)
{
    switch (type)
    {
    case TINYSET: val._tinyset &= ~(uinteger(1) << key.as_tiny_int()); break;
    case DICT: _dict_write().untie(key); break;
    case ORDSET: _ordset_write().untie(key.as_char_int()); break;
    case SET: _set_write().untie(key); break;
    default: _type_err(); break;
    }
}


const variant& variant::operator[] (mem index) const
{
    if (type == DICT)
        return operator[] (variant(index));
    _req(TUPLE);
    if (index < 0 || index >= _tuple_read().size())
        _index_err();
    return _tuple_read()[index];
}


const variant& variant::operator[] (const variant& index) const
{
    _req(DICT);
    dict_iterator it = _dict_read().find(index);
    if (it == _dict_read().end())
        return null;
    return it->second;
}


bool variant::has(const variant& index) const
{
    switch (type)
    {
    case TINYSET:   return val._tinyset & (uinteger(1) << index.as_tiny_int());
    case RANGE:     return _range_read().has(index.as_int());
    case DICT:      return _dict_read().find(index) != _dict_read().end();
    case ORDSET:    return _ordset_read().has(index.as_char_int());
    case SET:       return _set_read().find(index) != _set_read().end();
    default:        _type_err(); return false;
    }
}


integer variant::left() const
    { _req(RANGE); if (val._range == NULL) return 0; return _range_read().left; }

integer variant::right() const
    { _req(RANGE); if (val._range == NULL) return -1; return _range_read().right; }

