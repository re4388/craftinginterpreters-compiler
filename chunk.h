#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"

typedef enum {
    OP_RETURN,
  } OpCode;

// Dynamic arrays provide:
// - Cache-friendly, dense storage
// - Constant-time indexed element lookup
// - Constant-time appending to the end of the array (amortized)
typedef struct {
    uint8_t* code;
    int count;    // the number of elements are actually in use
    int capacity; // the number of elements in the array we have allocated
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte);

#endif