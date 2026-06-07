#include "memory.h"
#include "lzflist.h"
#include "lzarena.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

void *lzarena_link_alloc(size_t size, void *ctx){
    return LZARENA_ALLOC(ctx, size);
}

void *lzarena_link_realloc(void *ptr, size_t old_size, size_t new_size, void *ctx){
    return LZARENA_REALLOC(ctx, ptr, old_size, new_size);
}

void lzarena_link_dealloc(void *ptr, size_t size, void *ctx){
	// nothing to be done
}

void *lzflist_link_alloc(size_t size, void *ctx){
    return lzflist_alloc(ctx, size);
}

void *lzflist_link_realloc(void *ptr, size_t old_size, size_t new_size, void *ctx){
    return lzflist_realloc(ctx, ptr, new_size);
}

void lzflist_link_dealloc(void *ptr, size_t size, void *ctx){
	lzflist_dealloc(ctx, ptr);
}

char *memory_clone_cstr(const Allocator *allocator, const char *cstr, size_t *out_len){
    size_t len = strlen(cstr);
    char *cloned_cstr = MEMORY_ALLOC(allocator, char, len + 1);

    if(!cloned_cstr){
        return NULL;
    }

    memcpy(cloned_cstr, cstr, len);
    cloned_cstr[len] = '\0';

    if(out_len){
        *out_len = len;
    }

    return cloned_cstr;
}

void memory_destroy_cstr(const Allocator *allocator, char *cstr){
    if(!cstr){
        return;
    }

    MEMORY_DEALLOC(allocator, char, strlen(cstr) + 1, cstr);
}

Allocator *memory_arena_allocator(const Allocator *allocator, LZArena **out_lzarena){
    LZArena *arena = lzarena_create((LZArenaAllocator *)allocator);
    Allocator *arena_allocator = MEMORY_ALLOC(allocator, Allocator, 1);

    if(!arena || !arena_allocator){
        lzarena_destroy(arena);
        MEMORY_DEALLOC(allocator, Allocator, 1, arena_allocator);

        return NULL;
    }

    MEMORY_INIT_ALLOCATOR(
        arena,
        lzarena_link_alloc,
        lzarena_link_realloc,
        lzarena_link_dealloc,
        arena_allocator
    )

    if(out_lzarena){
        *out_lzarena = arena;
    }

    return arena_allocator;
}

void memory_destroy_arena_allocator(Allocator *allocator){}

Allocator *memory_lzflist_allocator(Allocator *allocator, LZFList **out_lzflist){
    LZFList *lzflist = lzflist_create((LZFListAllocator *)allocator);
    Allocator *lzflist_allocator = MEMORY_ALLOC(allocator, Allocator, 1);

    if(!lzflist || !lzflist_allocator){
        lzflist_destroy(lzflist);
        MEMORY_DEALLOC(allocator, Allocator, 1, lzflist_allocator);

        return NULL;
    }

    MEMORY_INIT_ALLOCATOR(
        lzflist,
        lzflist_link_alloc,
        lzflist_link_realloc,
        lzflist_link_dealloc,
        lzflist_allocator
    )

    if(out_lzflist){
        *out_lzflist = lzflist;
    }

    return lzflist_allocator;
}

void memory_destroy_flist_allocator(Allocator *allocator){}