#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75


void initTable(Table* table) {
    table->count = 0;
    table->capacity = 0;
    table->entries = NULL;
}

void freeTable(Table* table) {
    FREE_ARRAY(Entry, table->entries, table->capacity);
    initTable(table);
}

static Entry* findEntry(Entry* entries, int capacity,
                        ObjString* key) {

    uint32_t index = key->hash % capacity;
    Entry* tombstone = NULL;

    for (;;) {
        Entry* entry = &entries[index];

        if (entry->key == NULL) {
            if (IS_NIL(entry->value)) {
                // if we have passed a tombstone, we return its bucket instead of the later empty one.
                // If we’re calling findEntry() in order to insert a node,
                // that lets us treat the tombstone bucket as empty and reuse it for the new entry.
                return tombstone != NULL ? tombstone : entry;
            } else {
                // We found a tombstone. save entry to tombstone (can return later) and keep going
                // (we use a NULL key and a true value to represent tombstone)
                if (tombstone == NULL) tombstone = entry;
            }
            // it uses == to compare, this is pointer equality
            // 這個 == 比較的是 a 和 b 是否指向同一塊記憶體
            // we intern string so any string is guaranteed to be textually distinct from all others.
            // so that each sequence of characters is represented by only one string in memory
        } else if (entry->key == key) {
            // We found the key.
            return entry;
        }
        index = (index + 1) % capacity;
    }
}

bool tableGet(Table* table, ObjString* key, Value* value) {
    if (table->count == 0) return false;

    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    *value = entry->value;
    return true;
}

static void adjustCapacity(Table* table, int capacity) {
    Entry* entries = ALLOCATE(Entry, capacity);
    for (int i = 0; i < capacity; i++) {
        entries[i].key = NULL;
        entries[i].value = NIL_VAL;
    }

    /**
     * when we were doing a dynamic array, we could just use realloc() and let the C standard library
     * copy everything over. That doesn’t work for a hash table. Remember that to choose the bucket
     * for each entry, we take its hash key modulo the array size. That means that when the array size changes,
     * entries may end up in different buckets.Those new buckets may have new collisions that we need to deal with.
     * So the simplest way to get every entry where it belongs is to rebuild the table
     * from scratch by re-inserting every entry into the new empty array.
     *
     * for: table->count = 0
     * When we resize the array, we allocate a new array and re-insert all of the existing entries into it.
     * During that process, we don’t copy the tombstones over.
     * They don’t add any value since we’re rebuilding the probe sequences anyway, and would just slow down lookups.
     * That means we need to recalculate the count since it may change during a resize
     */
    table->count = 0;
    for (int i = 0; i < table->capacity; i++) {
        Entry* entry = &table->entries[i];
        if (entry->key == NULL) continue;

        Entry* dest = findEntry(entries, capacity, entry->key);
        dest->key = entry->key;
        dest->value = entry->value;
        table->count++; // Then each time we find a non-tombstone entry, we increment it.
    }

    FREE_ARRAY(Entry, table->entries, table->capacity);
    table->entries = entries;
    table->capacity = capacity;
}


bool tableSet(Table* table, ObjString* key, Value value) {
    if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
        int capacity = GROW_CAPACITY(table->capacity);
        adjustCapacity(table, capacity);
    }

    Entry* entry = findEntry(table->entries, table->capacity, key);
    bool isNewKey = entry->key == NULL;

    // since we run the risk of ending up with no actual empty buckets to terminate a lookup
    // we don’t reduce the count when deleting an entry in the delete code.
    // so the count is no longer the number of entries in the hash table, it’s the number of entries plus tombstones.
    if (isNewKey && IS_NIL(entry->value)) table->count++;

    if (isNewKey) table->count++;

    entry->key = key;
    entry->value = value;
    return isNewKey;
}

bool tableDelete(Table* table, ObjString* key) {
    if (table->count == 0) return false;

    // Find the entry.
    Entry* entry = findEntry(table->entries, table->capacity, key);
    if (entry->key == NULL) return false;

    // We replace the entry with a tombstone.
    // In clox, we use a NULL key and a true value to represent that,
    // but any representation that can’t be confused
    // with an empty bucket or a valid entry works.
    entry->key = NULL;
    entry->value = BOOL_VAL(true);
    return true;
}

void tableAddAll(Table* from, Table* to) {
    for (int i = 0; i < from->capacity; i++) {
        Entry* entry = &from->entries[i];
        if (entry->key != NULL) {
            tableSet(to, entry->key, entry->value);
        }
    }
}


/**
 * It appears we have copy-pasted findEntry(). There is a lot of redundancy, but also a couple of key differences.
 * First, we pass in the raw character array of the key we’re looking for instead of an ObjString.
 * At the point that we call this, we haven’t created an ObjString yet.
 */
ObjString* tableFindString(Table* table, const char* chars,
                           int length, uint32_t hash) {
    if (table->count == 0) return NULL;

    uint32_t index = hash % table->capacity;

    for (;;) {

        Entry* entry = &table->entries[index];

        if (entry->key == NULL) {
            // Stop if we find an empty non-tombstone entry.
            if (IS_NIL(entry->value)) return NULL;
        } else if (entry->key->length == length &&
            entry->key->hash == hash &&
            memcmp(entry->key->chars, chars, length) == 0) {
            // We found it.
            return entry->key;
            }

        index = (index + 1) % table->capacity;
    }
}
