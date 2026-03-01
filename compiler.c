#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif
#include "scanner.h"




// a single global variable of this struct type
// so we don’t need to pass the state around from function to function in the compiler
typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
} Parser;

// These are all of Lox’s precedence levels in order from lowest to highest.
// Since C implicitly gives successively larger numbers for enums,
// this means that PREC_CALL is numerically larger than PREC_UNARY
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_OR,          // or
    PREC_AND,         // and
    PREC_EQUALITY,    // == !=
    PREC_COMPARISON,  // < > <= >=
    PREC_TERM,        // + -
    PREC_FACTOR,      // * /
    PREC_UNARY,       // ! -
    PREC_CALL,        // . ()
    PREC_PRIMARY
  } Precedence;

typedef void (*ParseFn)();

typedef struct {
    ParseFn prefix; // the function to compile a prefix expression starting with a token of that type
    ParseFn infix;  // the function to compile an infix expression whose left operand is followed by a token of that type
    Precedence precedence; // the precedence of an infix expression that uses that token as an operator
} ParseRule;

Parser parser;

Chunk* compilingChunk;

static Chunk* currentChunk() {
    return compilingChunk;
}


static void errorAt(Token* token, const char* message) {
    if (parser.panicMode) return;
    parser.panicMode = true;
    fprintf(stderr, "[line %d] Error", token->line);

    if (token->type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token->type == TOKEN_ERROR) {
        // Nothing.
    } else {
        fprintf(stderr, " at '%.*s'", token->length, token->start);
    }

    fprintf(stderr, ": %s\n", message);
    parser.hadError = true;
}

static void error(const char* message) {
    errorAt(&parser.previous, message);
}

static void errorAtCurrent(const char* message) {
    errorAt(&parser.current, message);
}

static void advance() {
    // we store the prev
    parser.previous = parser.current;

    for (;;) {
        // It asks the scanner for the next token and stores it for later use
        parser.current = scanToken();

        // break if no error
        if (parser.current.type != TOKEN_ERROR) break;

        // report the location out of the current token when error
        errorAtCurrent(parser.current.start);
    }
}

static void consume(TokenType type, const char* message) {
    if (parser.current.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

static bool check(TokenType type) {
    return parser.current.type == type;
}

static bool match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

static void emitByte(uint8_t byte) {
    // appending a single byte to the chunk.
    writeChunk(currentChunk(), byte, parser.previous.line);
}

static void emitBytes(uint8_t byte1, uint8_t byte2) {
    emitByte(byte1);
    emitByte(byte2);
}

static void emitReturn() {
    emitByte(OP_RETURN);
}

static uint8_t makeConstant(Value value) {
    int constant = addConstant(currentChunk(), value);
    if (constant > UINT8_MAX) {
        error("Too many constants in one chunk.");
        return 0;
    }

    return (uint8_t)constant;
}

static void emitConstant(Value value) {
    emitBytes(OP_CONSTANT, makeConstant(value));
}


static void endCompiler() {
    emitReturn();
#ifdef DEBUG_PRINT_CODE
    if (!parser.hadError) {
        disassembleChunk(currentChunk(), "code");
    }
#endif
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void binary() {
    TokenType operatorType = parser.previous.type;
    ParseRule* rule = getRule(operatorType);
    /**
    * why precedence + 1?
    * 用「比該 operator 高一級的 precedence」來解析，這樣可以正確處理左結合(left-associative)
    *
    * 當你在解析：
    * 2 * 3 + 4
    * 在 * 裡面：
    * 你只想吃 3
    * 不想吃 3 + 4
    * 所以你對右邊說：
    * 「我要的東西必須比我更強（precedence 更高）才可以繼續往右吃。」
    * 而 + 比 * 弱
    * 所以 + 不會被吃進去
    */
    parsePrecedence((Precedence)(rule->precedence + 1));

    switch (operatorType) {
        case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
        case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
        case TOKEN_GREATER:       emitByte(OP_GREATER); break;
        case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
        case TOKEN_LESS:          emitByte(OP_LESS); break;
        case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
        case TOKEN_PLUS:          emitByte(OP_ADD); break;
        case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
        case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
        case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
        default: return; // Unreachable.
    }
}

static void literal() {
    switch (parser.previous.type) {
        case TOKEN_FALSE: emitByte(OP_FALSE); break;
        case TOKEN_NIL: emitByte(OP_NIL); break;
        case TOKEN_TRUE: emitByte(OP_TRUE); break;
        default: return; // Unreachable.
    }
}

static void grouping() {
    // assume the initial ( has already been consumed
    expression();
    consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}


static void number() {
    // assume the token for the number literal has already been consumed and is stored in previous
    double value = strtod(parser.previous.start, NULL);
    emitConstant(NUMBER_VAL(value));
}

static void string() {
    // The + 1 and - 2 parts trim the leading and trailing quotation marks.
    emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
                                    parser.previous.length - 2)));
}

// write the negate instruction after its operand’s bytecode
// since the appears on the left, but this is for the order of execution (for stack-based vm)
static void unary() {
    // the leading token has been consumed and in parser.previous
    TokenType operatorType = parser.previous.type;

    // Compile the operand.
    // we call this to limit it to the appropriate level
    parsePrecedence(PREC_UNARY);

    // Emit the operator instruction.
    switch (operatorType) {
        case TOKEN_BANG: emitByte(OP_NOT); break;
        case TOKEN_MINUS: emitByte(OP_NEGATE); break;
        default: return; // Unreachable.
    }
}

// We map each token type to a different kind of expression.
// We define a function for each expression that outputs the appropriate bytecode.
// Then we build an array of function pointers.
// The indexes in the array correspond to the TokenType enum values,
// and the function at each index is the code to compile an expression of that token type.

// see how grouping and unary are slotted into the prefix parse´r column for their respective token types.
// In the next column, binary is wired up to the four arithmetic infix operators.
// Those infix operators also have their precedences set in the last column
ParseRule rules[] = {
  [TOKEN_LEFT_PAREN]    = {grouping, NULL,   PREC_NONE},
  [TOKEN_RIGHT_PAREN]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_LEFT_BRACE]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RIGHT_BRACE]   = {NULL,     NULL,   PREC_NONE},
  [TOKEN_COMMA]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_DOT]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_MINUS]         = {unary,    binary, PREC_TERM},
  [TOKEN_PLUS]          = {NULL,     binary, PREC_TERM},
  [TOKEN_SEMICOLON]     = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SLASH]         = {NULL,     binary, PREC_FACTOR},
  [TOKEN_STAR]          = {NULL,     binary, PREC_FACTOR},
  [TOKEN_BANG]          = {unary,     NULL,   PREC_NONE},
  [TOKEN_BANG_EQUAL]    = {NULL,     binary,   PREC_EQUALITY},
  [TOKEN_EQUAL]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EQUAL_EQUAL]   = {NULL,     binary,   PREC_EQUALITY},
  [TOKEN_GREATER]       = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_GREATER_EQUAL] = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_LESS]          = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_LESS_EQUAL]    = {NULL,     binary,   PREC_COMPARISON},
  [TOKEN_IDENTIFIER]    = {NULL,     NULL,   PREC_NONE},
  [TOKEN_STRING]        = {string,     NULL,   PREC_NONE},
  [TOKEN_NUMBER]        = {number,   NULL,   PREC_NONE},
  [TOKEN_AND]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_CLASS]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ELSE]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FALSE]         = {literal,     NULL,   PREC_NONE},
  [TOKEN_FOR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_FUN]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_IF]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_NIL]           = {literal,     NULL,   PREC_NONE},
  [TOKEN_OR]            = {NULL,     NULL,   PREC_NONE},
  [TOKEN_PRINT]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_RETURN]        = {NULL,     NULL,   PREC_NONE},
  [TOKEN_SUPER]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_THIS]          = {NULL,     NULL,   PREC_NONE},
  [TOKEN_TRUE]          = {literal,     NULL,   PREC_NONE},
  [TOKEN_VAR]           = {NULL,     NULL,   PREC_NONE},
  [TOKEN_WHILE]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_ERROR]         = {NULL,     NULL,   PREC_NONE},
  [TOKEN_EOF]           = {NULL,     NULL,   PREC_NONE},
};


// This function starts at the current token
// and parses any expression at the given precedence level or higher
//
// For example, say the compiler is sitting on a chunk of code like: `-a.b + c`
// If we call parsePrecedence(PREC_ASSIGNMENT), then it will parse the entire expression
// because + has higher precedence than assignment. If instead we call parsePrecedence(PREC_UNARY),
// it will compile the -a.b and stop there.
// It doesn’t keep going through the + because the addition has lower precedence than unary operators.
static void parsePrecedence(Precedence precedence) {
    advance(); // read the next token
    ParseFn prefixRule = getRule(parser.previous.type)->prefix; // look up the corresponding ParseRule and get prefixRule fn
    if (prefixRule == NULL) {
        error("Expect expression.");
        return;
    }

    // call it
    // That prefix parser compiles the rest of the prefix expression,
    // consuming any other tokens it needs, and returns back here
    prefixRule();

    // Now we look for an infix parser for the next token.
    // If we find one, it means the prefix expression we already compiled might be an operand for it.
    // But only if the call to parsePrecedence() has a precedence that is low enough to permit that infix operator.
    while (precedence <= getRule(parser.current.type)->precedence) {
        // same logic like above
        advance();
        ParseFn infixRule = getRule(parser.previous.type)->infix;
        infixRule();
    }
}

// This function exists solely to handle a declaration cycle in the C code.
// binary() is defined before the rules table so that the table can store a pointer to it.
// That means the body of binary() cannot access the table directly.
// Instead, we wrap the lookup in a function. That lets us forward declare getRule() before the definition of binary(),
// and then define getRule() after the table.
static ParseRule* getRule(TokenType type) {
    return &rules[type];
}

// We simply parse the lowest precedence level `PREC_ASSIGNMENT`,
// which includes all the higher-precedence expressions too
static void expression() {
    parsePrecedence(PREC_ASSIGNMENT);
}

static void printStatement() {
    expression();

    // The grammar expects a semicolon after that, so we consume
    consume(TOKEN_SEMICOLON, "Expect ';' after value.");
    emitByte(OP_PRINT);
}

static void declaration() {
    statement();
}

static void statement() {
    if (match(TOKEN_PRINT)) {
        printStatement();
    }
}


/**
statement      → exprStmt
               | printStmt ;

declaration    → varDecl
               | statement ;



 */
bool compile(const char* source, Chunk* chunk) {
    initScanner(source);
    compilingChunk = chunk;

    parser.hadError = false;
    parser.panicMode = false;

    advance(); // kick off, to get the first token

    while (!match(TOKEN_EOF)) {
        declaration();
    }

    endCompiler();
    return !parser.hadError;
}