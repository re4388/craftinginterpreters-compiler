#ifndef clox_chunk_h
#define clox_chunk_h


#include "common.h"
#include "value.h"

typedef enum {
    OP_NEGATE,
    OP_CONSTANT,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_NOT,
    OP_RETURN,
  } OpCode;

// Dynamic arrays provide:
// - Cache-friendly, dense storage
// - Constant-time indexed element lookup
// - Constant-time appending to the end of the array (amortized)
typedef struct {
    uint8_t* code;
    int* lines;  // Each number in the array is the line number for the corresponding byte in the bytecode.
    ValueArray constants;
    int count;    // the number of elements are actually in use
    int capacity; // the number of elements in the array we have allocated
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif