#ifndef LZPOOL_H
#define LZPOOL_H

#include <stddef.h>

#define LZPOOL_DEFAULT_ALIGNMENT 8

typedef struct lzpool_allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
}LZPoolAllocator;

typedef struct lzpool LZPool;

LZPool *lzpool_create(const LZPoolAllocator *allocator, size_t slot_size);
void lzpool_destroy(LZPool *pool);

#define LZPOOL_CREATE_TYPE(_allocator, _type)      (lzpool_create(_allocator, sizeof(_type)))

const LZPoolAllocator *lzpool_allocator(const LZPool *pool);
size_t lzpool_available_slots_count(const LZPool *pool);
size_t lzpool_subpools_count(const LZPool *pool);
size_t lzpool_available(const LZPool *pool);
size_t lzpool_capacity(const LZPool *pool);
int lzpool_is_used(const void *ptr);

int lzpool_prealloc_count(LZPool *pool, size_t count);
int lzpool_prealloc_size(LZPool *pool, size_t size);

void *lzpool_alloc(LZPool *pool);
void *lzpool_alloc_backup_count(LZPool *pool, size_t count);
void *lzpool_alloc_backup_size(LZPool *pool, size_t size);
void lzpool_dealloc(void *ptr);
void lzpool_dealloc_release(void *ptr);

#endif