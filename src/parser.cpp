
#include "parser.h"


static class Keywords
{
    struct kwinfo { const char* kw; Token token; };
    static kwinfo keywords[];

    int count;

public:
    Keywords()
    {
        for (kwinfo* k = keywords; k->kw != NULL; k++)
        {
#ifdef DEBUG
            if (count > 0)
                if (strcmp(k->kw, (k - 1)->kw) <= 0)
                    fatal(0x2040, "Keyword verification failed");
#endif
            count++;
        }
    }

    int compare(int index, const char* b) const
        { return strcmp(keywords[index].kw, b); }

    Token find(const char* s)
    {
        int index;
        if (::bsearch(*this, count - 1, s, index))
            return keywords[index].token;
        else
            return tokUndefined;
    }

} keywords;


Keywords::kwinfo Keywords::keywords[] =
    {
        // NOTE: this list must be kept in sorted order
        {"and", tokAnd},
        {"as", tokAs},
        {"assert", tokAssert},
        {"begin", tokBegin},
        {"break", tokBreak},
        {"case", tokCase},
        {"const", tokConst},
        {"continue", tokContinue},
        {"def", tokDef},
        {"dump", tokDump},
        {"elif", tokElif},
        {"else", tokElse},
        {"enum", tokEnum},
        {"exit", tokExit},
        {"if", tokIf},
        {"in", tokIn},
        {"is", tokIs},
        {"mod", tokMod},
        {"not", tokNot},
        {"or", tokOr},
        {"return", tokReturn},
        {"shl", tokShl},
        {"shr", tokShr},
        {"typeof", tokTypeOf},
        {"var", tokVar},
        {"while", tokWhile},
        {"xor", tokXor},
        {NULL, tokUndefined}
    };


static str parserErrorStr(const str& filename, int linenum, const str& msg)
{
    str s;
    if (!filename.empty())
        s = filename + "(" + to_string(integer(linenum)) + "): ";
    return s + msg;
}


EParser::EParser(const str& fn, int l, const str& m)
    : emessage(parserErrorStr(fn, l, m))  { }


Parser::Parser(fifo* inp)
    : input(inp), linenum(1), prevIdent(), saveToken(tokUndefined),
      token(tokUndefined), strValue(), intValue(0)  { }


Parser::~Parser()
    { }


void Parser::error(const str& msg)
    { throw EParser(getFileName(), linenum, msg); }

void Parser::errorWithLoc(const str& msg)
    { error(msg + errorLocation()); }

void Parser::error(const char* msg)
    { error(str(msg)); }

void Parser::errorWithLoc(const char* msg)
    { errorWithLoc(str(msg)); }


str Parser::errorLocation() const
{
    str msg;
    if (!strValue.empty())
    {
        str s = strValue;
        if (s.size() > 20)
            s = s.substr(0, 20) + "...";
        msg += " near '" + s + "'";
    }
    return msg;
}


const charset wsChars = "\t ";
const charset identFirst = "A-Za-z_";
const charset identRest = "0-9A-Za-z_";
const charset digits = "0-9";
const charset printableChars = "~20-~7E~81-~FE";
const charset commentChars = printableChars + wsChars;

inline bool is_eol_char(char c)
    { return c == '\n' || c == '\r'; }


inline void Parser::skipWs()
    { input->skip(wsChars); }


void Parser::skipEol()
{
    assert(input->eol());
    input->skip_eol();
    linenum++;
}


void Parser::parseStringLiteral()
{
    static const charset stringChars = printableChars - charset("'\\");
    static const charset hexDigits = "0-9A-Fa-f";
    strValue.clear();
    while (true)
    {
        strValue += input->token(stringChars);
        if (input->eof())
            error("Unexpected end of file in string literal");
        char c = input->get();
        if (is_eol_char(c))
            error("Unexpected end of line in string literal");
        if (c == '\'')
            return;
        else if (c == '\\')
        {
            switch (c = input->get())
            {
            case 't': strValue += '\t'; break;
            case 'r': strValue += '\r'; break;
            case 'n': strValue += '\n'; break;
            case 'x':
                {
                    str s;
                    if (hexDigits[input->preview()])
                    {
                        s += input->get();
                        if (hexDigits[input->preview()])
                            s += input->get();
                        bool e, o;
                        ularge value = from_string(s.c_str(), &e, &o, 16);
                        strValue += char(value);
                    }
                    else
                        error("Bad hex sequence");
                }
                break;
            default: strValue += c; break;
            }
        }
        else
            error("Illegal character in string literal " + to_printable(c));
    }
}


void Parser::skipMultilineComment()
{
    static const charset skipChars = commentChars - '*';
    while (true)
    {
        input->skip(skipChars);
        if (input->eol())
        {
            if (input->eof())
                error("Unexpected end of file in comments");
            skipEol();
            continue;
        }
        char e = input->get();
        if (e == '*')
        {
            if (input->preview() == '/')
            {
                input->get();
                break;
            }
        }
        else
            error("Illegal character in comments " + to_printable(e));
    }
}


void Parser::skipSinglelineComment()
{
    input->skip(commentChars);
    if (!input->eol())
        error("Illegal character in comments " + to_printable(input->preview()));
}



Token Parser::next()
{
    assert(token != tokPrevIdent);
restart:
    strValue.clear();
    intValue = 0;

    skipWs();
    int c = input->preview();

    // --- EOF ---
    if (c == -1)
    {
        strValue = "<EOF>";
        if (token != tokSep)   // return tokSep at EOF, but only once
            return token = tokSep;
        return token = tokEof;
    }

    // --- EOL ---
    else if (is_eol_char(c))
    {
        do
        {
            skipEol();
            skipWs();
        }
        while (input->eol());
        if (input->preview() == '{')
            goto restart; // will return tokBlockBegin, even though it's on a new line
        if (token == tokBlockBegin || token == tokBlockEnd || token == tokSingleBlock)
            goto restart;
        strValue = "<EOL>";
        return token = tokSep;
    }

    // --- Identifier or keyword ---
    if (identFirst[c])
    {
        strValue = input->get();
        strValue += input->token(identRest);
        Token tok = keywords.find(strValue.c_str());
        if (tok != tokUndefined)
            return token = tok;
        else
            return token = tokIdent;
    }

    // --- Number ---
    else if (digits[c])
    {
        // TODO: floating point
        bool e, o;
        strValue = input->token(identRest);
        str s = strValue;
        bool isHex = s.size() > 2 && s[0] == '0' && s[1] == 'x';
        if (isHex)
            s.erase(0, 2);
        ularge v = from_string(s.c_str(), &e, &o, isHex ? 16 : 10);
        if (e)
            error("'" + strValue + "' is not a valid number");
        if (o || (v > ularge(INTEGER_MAX) + 1))
            error("Numeric overflow (" + strValue + ")");
        intValue = uinteger(v);
        return token = tokIntValue;
    }

    // --- Special chars and sequences ---
    else
    {
        strValue = input->get();
        switch (c)
        {
        case '\\':
            input->skip(wsChars);
            if (!input->eol())
                error("New line expected after '\\'");
            skipEol();
            goto restart;
        case ',': return token = tokComma;
        case '.': return token = (input->get_if('.') ? tokRange : tokPeriod);
        case '\'': parseStringLiteral(); return token = tokStrValue;
        case ';': return token = tokSep;
        case ':': return token = tokSingleBlock;
        case '+': return token = tokPlus;
        case '-': return token = tokMinus;
        case '/': 
            if (input->get_if('/'))
            {
                skipSinglelineComment();
                goto restart;
            }
            else if (input->get_if('*'))
            {
                skipMultilineComment();
                goto restart;
            }
            return token = tokDiv;
        case '*': return token = tokMul;
        case '[': return token = tokLSquare;
        case ']': return token = tokRSquare;
        case '(': return token = tokLParen;
        case ')': return token = tokRParen;
        case '{': return token = tokBlockBegin;
        case '}': return token = tokBlockEnd;
        case '<':
            if (input->get_if('='))
                return token = tokLessEq;
            else if (input->get_if('>'))
                return token = tokNotEq;
            else
                return tokLAngle;
        case '>': return token = (input->get_if('=') ? tokGreaterEq : tokRAngle);
        case '=': return token = (input->get_if('=') ? tokEqual : tokAssign);
        case '!': return token = (input->get_if('=') ? tokNotEq : tokExclam);
        case '|': return token = tokCat; break;
        }
    }

    error("Illegal character " + to_printable(c));

    return tokUndefined;
}


void Parser::undoIdent(const str& ident)
{
    prevIdent = ident;
    saveToken = token;
    token = tokPrevIdent;
}


void Parser::redoIdent()
{
    prevIdent.clear();
    token = saveToken;
    saveToken = tokUndefined;
}


str Parser::getIdentifier()
{
    // This function doesn't call next() because in many cases we want
    // compiler's error messages to point to this identifier rather than
    // the next token.
    if (token != tokIdent)
        errorWithLoc("Identifier expected");
    return strValue;
}


void Parser::expect(Token tok, const char* errName)
{
    if (token != tok)
        errorWithLoc(str(errName) + " expected");
    next();
}
