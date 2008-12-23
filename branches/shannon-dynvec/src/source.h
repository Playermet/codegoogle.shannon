#ifndef __SOURCE_H
#define __SOURCE_H

#include "str.h"
#include "except.h"
#include "charset.h"
#include "contain.h"
#include "baseobj.h"


#define INFILE_BUFSIZE 8192
#define DEFAULT_TAB_SIZE 8


class InText: public Base
{
protected:
    char* buffer;
    int   bufsize;
    int   bufpos;
    int   linenum;
    int   column;
    bool  eof;
    int   tabsize;
    
    void error(int code) throw(ESysError);
    virtual void validateBuffer() = 0;
    void doSkipEol();
    void skipTo(char c);
    void token(const charset& chars, string& result, bool skip);

public:
    InText();
    virtual ~InText();
    
    virtual string getFileName() = 0;
    int  getLineNum()       { return linenum; }
    int  getColumn()        { return column; }
    bool getEof()           { return eof; }
    bool getEol();
    bool isEolChar(char c)  { return c == '\r' || c == '\n'; }
    char preview();
    char get();
    void skipEol();
    void skipLine();
    string token(const charset& chars) throw(ESysError);
    void skip(const charset& chars) throw(ESysError);
};



class InFile: public InText
{
protected:
    string filename;
    int  fd;

    virtual void validateBuffer();

public:
    InFile(const string& filename);
    virtual ~InFile();
    virtual string getFileName();
};


class EParser: public EMessage
{
protected:
    string filename;
    int linenum;
public:
    EParser(const string& ifilename, int ilinenum, const string& msg)
        : EMessage(msg), filename(ifilename), linenum(ilinenum)  { }
    virtual ~EParser() throw();
    virtual string what() const throw();
};


enum Token
{
    tokUndefined = -1,
    tokBegin, tokEnd, tokSep, // these will depend on C-style vs. Python-style modes in the future
    tokEof,
    tokIdent, tokIntValue, tokStrValue,
    // keywords
    tokModule, tokConst, tokDef,
    // special chars and sequences
    tokComma, tokPeriod, tokDiv, tokMul,
    tokLSquare, tokRSquare, /* tokLCurly, tokRCurly, */
    tokLAngle, tokLessThan = tokLAngle, tokRAngle, tokGreaterThan = tokRAngle,
    tokEqual,
};


enum SyntaxMode { syntaxIndent, syntaxCurly };


class Parser
{
protected:
    InText* input;
    bool blankLine;
    Stack<int> indentStack;
    int linenum;

    string errorLocation() const;
    void parseStringLiteral();
    void skipMultilineComment();
    void skipSinglelineComment();
    int getLineNum() { return linenum; }

public:
    bool singleLineBlock; // if a: b = c
    Token token;
    string strValue;
    ularge intValue;
    
    Parser(const string& filename);
    ~Parser();
    
    Token next();
    Token nextBegin();
    Token nextEnd();

    void error(const string& msg);
    void errorWithLoc(const string& msg);
    void error(const char*);
    void errorWithLoc(const char*);
    string skipIdent();
    void skipSep();
    void skip(Token tok, const char* errName);
    string getIdent();
    
    int indentLevel()  { return indentStack.top(); }
};


string extractFileName(string filepath);
string mkPrintable(char c);
string mkPrintable(const string&);

#endif
