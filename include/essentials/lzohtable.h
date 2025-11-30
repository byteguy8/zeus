#ifndef LZOHTABLE_H
#define LZOHTABLE_H

#include <stdint.h>
#include <stddef.h>

typedef void lzohtable_clean_up(void *key, void *value, void *extra);

typedef struct lzohtable_allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
}LZOHTableAllocator;

typedef uint64_t lzohtable_hash_t;

typedef struct lzohtable_slot{
    unsigned char used;

    size_t probe;
    lzohtable_hash_t hash;

    char kcpy;
    char vcpy;
    size_t key_size;
    size_t value_size;

    void *key;
    void *value;
}LZOHTableSlot;

typedef struct lzohtable{
    size_t n;                      // count of distinct elements
    size_t m;                      // count of slots
    float lfth;                    // load factor threshold
    LZOHTableSlot *slots;
    LZOHTableAllocator *allocator;
}LZOHTable;

#define LZOHTABLE_LOAD_FACTOR(_table)(((float)(_table)->n) / ((float)(_table)->m))

LZOHTable *lzohtable_create(size_t m, float lfth, LZOHTableAllocator *allocator);

void lzohtable_destroy_help(const void *extra, lzohtable_clean_up *clean_up_helper, LZOHTable *table);

#define LZOHTABLE_DESTROY(_table)(lzohtable_destroy_help(NULL, NULL, (_table)))

void lzohtable_print(void (*print_helper)(size_t count, size_t len, size_t idx, size_t probe, size_t key_size, size_t value_size, void *key, void *value), LZOHTable *table);

int lzohtable_lookup(size_t key_size, const void *key, LZOHTable *table, void **out_value);

void lzohtable_clear_help(const void *extra, lzohtable_clean_up *clean_up_helper, LZOHTable *table);

#define LZOHTABLE_CLEAR(_table)(lzohtable_clear_help(NULL, NULL, (_table)))

int lzohtable_put(size_t key_size, const void *key, const void *value, LZOHTable *table, lzohtable_hash_t *out_hash);

int lzohtable_put_ck(size_t key_size, const void *key, const void *value, LZOHTable *table, lzohtable_hash_t *out_hash);
int lzohtable_put_help(
    size_t key_size,
    const void *key,
    const void *value,
    LZOHTable *table,
    void **out_value,
    lzohtable_hash_t *out_hash
);

int lzohtable_put_ckv(size_t key_size, const void *key, size_t value_size, const void *value, LZOHTable *table, lzohtable_hash_t *out_hash);

void lzohtable_remove_help(size_t key_size, const void *key, const void *extra, lzohtable_clean_up *clean_up_helper, LZOHTable *table);

#define LZOHTABLE_REMOVE(_size, _key, _table)(lzohtable_remove_help((_size), (_key), NULL, NULL, (_table)))

#endif