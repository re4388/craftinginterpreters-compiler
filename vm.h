#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"
#define STACK_MAX 256


typedef struct {
    Chunk* chunk;

    // instruction pointer, the location of the instruction currently being executed
    // x86, x64, and the CLR call it “IP”. 68k, PowerPC, ARM, p-code, and the JVM call it “PC”, for program counter.
    // the IP always points to the next instruction, not the one currently being handled.
    uint8_t* ip;
    Value stack[STACK_MAX];

    // stackTop points just past the last used element, at the next available one
    // The pointer points at the array element just past the element containing the top value on the stack.
    // we can indicate that the stack is empty by pointing at element zero in the array
    // If we pointed to the top element, then for an empty stack we’d need to point at element -1
    Value* stackTop;

    Table globals; // global varibale

    // to reliably deduplicate all strings, the VM needs to be able to find every string that’s created
    Table strings;

    Obj* objects; //  a pointer to the head of the linked list for gc
} VM;


typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
  } InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
void push(Value value);
Value pop();

#endif