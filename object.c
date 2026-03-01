#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"
#include "vm.h"

// this exists mainly to avoid the need to redundantly cast a void* back to the desired type
#define ALLOCATE_OBJ(type, objectType) \
(type*)allocateObject(sizeof(type), objectType)

// The caller passes in the number of bytes
// so that there is room for the extra payload fields needed by the specific object type being created.
static Obj* allocateObject(size_t size, ObjType type) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->type = type;

    object->next = vm.objects;
    vm.objects = object;
    return object;
}


// It creates a new ObjString on the heap and then initializes its fields.
// It’s sort of like a constructor in an OOP language
static ObjString* allocateString(char* chars, int length,
    uint32_t hash) {
    ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
    string->length = length;
    string->chars = chars;

    // For clox, we’ll automatically intern every one
    // That means whenever we create a new unique string, we add it to the table.
    // We’re using the table more like a hash set than a hash table.
    // The keys are the strings and those are all we care about,
    // so we just use nil for the values.
    tableSet(&vm.strings, string, NIL_VAL);
    string->hash = hash;
    return string;
}

/**
* The algorithm is called “FNV-1a”
start with some initial hash value, usually a constant with certain carefully chosen mathematical properties.
Then you walk the data to be hashed. For each byte (or sometimes word), you mix the bits into the hash value somehow,
and then scramble the resulting bits around some.
the basic goal is uniformity — we want the resulting hash values to be as widely scattered
around the numeric range as possible to avoid collisions and clustering.
 */
static uint32_t hashString(const char* key, int length) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619;
    }
    return hash;
}

ObjString* takeString(char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
    if (interned != NULL) {
        // Since ownership is being passed to this function and we no longer need the duplicate string,
        // it’s up to us to free it.
        FREE_ARRAY(char, chars, length + 1);
        return interned;
    }

    return allocateString(chars, length, hash);
}


// We need to terminate the string ourselves because the lexeme points at
// a range of characters inside the monolithic source string and isn’t terminated.
ObjString* copyString(const char* chars, int length) {
    uint32_t hash = hashString(chars, length);

    ObjString* interned = tableFindString(&vm.strings, chars, length,hash);
    if (interned != NULL) return interned;

    char* heapChars = ALLOCATE(char, length + 1);
    memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash);
}

void printObject(Value value) {
    switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
            printf("%s", AS_CSTRING(value));
            break;
    }
}