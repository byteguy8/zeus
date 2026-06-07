#ifndef MEMORY_H
#define MEMORY_H

#include "lzbstr.h"
#include "dynarr.h"
#include "lzohtable.h"
#include "lzarena.h"
#include "lzpool.h"
#include "lzflist.h"

#include <stddef.h>

typedef struct allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
}Allocator;

typedef struct complex_context{
    void *arg0;
    void *arg1;
}ComplexContext;

#define MEMORY_KIBIBYTES(_count)((_count) * 1024)
#define MEMORY_MIBIBYTES(_count)(MEMORY_KIBIBYTES((_count) * 1024))

#define MEMORY_INIT_ALLOCATOR(_ctx, _alloc, _realloc, _dealloc, _allocator){ \
    (_allocator)->ctx     = (_ctx);                                          \
    (_allocator)->alloc   = (_alloc);                                        \
    (_allocator)->realloc = (_realloc);                                      \
    (_allocator)->dealloc = (_dealloc);                                      \
}

#define MEMORY_CHECK(_allocation)                                  if(!(_allocation)) goto ERROR

#define MEMORY_ALLOC(allocator, type, count)                       ((type *)((allocator)->alloc(sizeof(type) * count, ((allocator)->ctx))))
#define MEMORY_REALLOC(allocator, type, old_count, new_count, ptr) ((type *)((allocator)->realloc(ptr, (sizeof(type) * old_count), (sizeof(type) * new_count), (allocator)->ctx)))
#define MEMORY_DEALLOC(allocator, type, count, ptr)                ((allocator)->dealloc((ptr), (sizeof(type) * count), (allocator)->ctx))

#define MEMORY_LZBSTR(_allocator)                                  (lzbstr_create((LZBStrAllocator *)(_allocator)))
#define MEMORY_DYNARR_TYPE(_allocator, _type)                      (DYNARR_CREATE_TYPE((DynArrAllocator *)(_allocator), _type))
#define MEMORY_DYNARR_PTR(_allocator)                              (DYNARR_CREATE_PTR((DynArrAllocator *)(_allocator)))
#define MEMORY_DYNARR_TYPE_COUNT(_allocator, _type, _count)        (DYNARR_CREATE_TYPE_BY((DynArrAllocator *)(_allocator), _type, (_count)))
#define MEMORY_LZOHTABLE(_allocator)                               (lzohtable_create(64, 0.8, (LZOHTableAllocator *)(_allocator)))
#define MEMORY_LZOHTABLE_LEN(_allocator, _len)                     (lzohtable_create((_len), 0.8, (LZOHTableAllocator *)(_allocator)))
#define MEMORY_LZARENA(_allocator)                                 (lzarena_create((LZArenaAllocator *)(_allocator)))
#define MEMORY_LZPOOL(_allocator, _type)                           (lzpool_create((LZPoolAllocator *)(_allocator), sizeof(_type)))

char *memory_clone_cstr(const Allocator *allocator, const char *cstr, size_t *out_len);
void memory_destroy_cstr(const Allocator *allocator, char *cstr);

Allocator *memory_arena_allocator(const Allocator *allocator, LZArena **out_lzarena);
void memory_destroy_arena_allocator(Allocator *allocator);

Allocator *memory_lzflist_allocator(Allocator *allocator, LZFList **out_lzflist);
void memory_destroy_flist_allocator(Allocator *allocator);

#endif
