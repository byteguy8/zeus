#include "lzpool.h"
#include <stdlib.h>
#include <assert.h>
#include <inttypes.h>

#define HEADER_SIZE (sizeof(LZPoolHeader))
#define MAGIC_NUMBER 0xDEADBEEF

typedef struct lzsubpool        LZSubPool;
typedef struct lzpool_header    LZPoolHeader;
typedef struct lzpool_slot_list LZPoolSlotList;
typedef struct lzsubpool_list   LZSubPoolList;

typedef uint8_t boolean;

struct lzsubpool{
    size_t    used;
    size_t    capacity;

    void      *slots;

    LZPool    *pool;

    LZSubPool *prev;
    LZSubPool *next;
};

struct lzpool_header{
    size_t       magic;
    boolean      used;

    LZSubPool    *subpool;

    LZPoolHeader *prev;
    LZPoolHeader *next;
};

struct lzpool_slot_list{
    size_t       len;
    LZPoolHeader *head;
    LZPoolHeader *tail;
};

struct lzsubpool_list{
    size_t    len;
    LZSubPool *head;
    LZSubPool *tail;
};

struct lzpool{
    size_t                header_size;
    size_t                slot_size;

    size_t                available;
    size_t                capacity;

    LZPoolSlotList        slots;
    LZSubPoolList         subpools;

    const LZPoolAllocator *allocator;
};

//--------------------------------------------------------------------------//
//                            PRIVATE INTERFACE                             //
//--------------------------------------------------------------------------//
static inline size_t round_size(size_t to, size_t size);
//--------------------------------  MEMORY  --------------------------------//
static inline void *lzalloc(const LZPoolAllocator *allocator, size_t size);
static inline void *lzrealloc(const LZPoolAllocator *allocator, void *ptr, size_t old_size, size_t new_size);
static inline void lzdealloc(const LZPoolAllocator *allocator, void *ptr, size_t size);

#define MEMORY_ALLOC(_allocator, _type, _count) \
    ((_type *)lzalloc((_allocator), sizeof(_type) * (_count)))

#define MEMORY_REALLOC(_allocator, _ptr, _type, _old_count, _new_count) \
    ((_type *)(lzrealloc((_allocator), (_ptr), sizeof(_type) * (_old_count), sizeof(_type) * (_new_count))))

#define MEMORY_DEALLOC(_allocator, _ptr, _type, _count) \
    (lzdealloc((_allocator), (_ptr), sizeof(_type) * (_count)))

static void destroy_subpool(LZSubPool *subpool, const LZPoolAllocator *allocator, size_t header_size, size_t slot_size);

static inline void *slot_chunk(size_t header_size, LZPoolHeader *slot);
static inline LZPoolHeader *slot_from_ptr(const void *ptr, size_t header_size);
static inline LZPoolHeader *get_slot_at(const void *slots, size_t header_size, size_t slot_size, size_t idx);

static void insert_slot(LZPoolSlotList *list, LZPoolHeader *slot);
static void remove_slot(LZPoolSlotList *list, LZPoolHeader *slot);

static void insert_subpool(LZSubPoolList *list, LZSubPool *subpool);
static void remove_subpool(LZSubPoolList *list, LZSubPool *subpool);

static void init_subpool_slots(
    LZSubPool *subpool,
    LZPoolSlotList *slots,
    size_t header_size,
    size_t slot_size
);

static void init_pool(LZPool *pool, const LZPoolAllocator *allocator, size_t slot_size);
static void deinit_pool(LZPool *pool);

//--------------------------------------------------------------------------//
//                          PRIVATE IMPLEMENTATION                          //
//--------------------------------------------------------------------------//
inline size_t round_size(size_t to, size_t size){
    size_t mod = size % to;
    size_t padding = mod == 0 ? 0 : to - mod;

    return padding + size;
}

inline void *lzalloc(const LZPoolAllocator *allocator, size_t size){
    return allocator ? allocator->alloc(size, allocator->ctx) : malloc(size);
}

inline void *lzrealloc(const LZPoolAllocator *allocator, void *ptr, size_t old_size, size_t new_size){
    return allocator ? allocator->realloc(ptr, old_size, new_size, allocator->ctx) : realloc(ptr, new_size);
}

inline void lzdealloc(const LZPoolAllocator *allocator, void *ptr, size_t size){
    if(allocator){
        allocator->dealloc(ptr, size, allocator->ctx);
    }else{
        free(ptr);
    }
}

inline void destroy_subpool(LZSubPool *subpool, const LZPoolAllocator *allocator, size_t header_size, size_t slot_size){
    MEMORY_DEALLOC(allocator, subpool->slots, char, (header_size + slot_size) * subpool->capacity);
    MEMORY_DEALLOC(allocator, subpool, LZSubPool, 1);
}

inline void *slot_chunk(size_t header_size, LZPoolHeader *slot){
    return ((char *)slot) + header_size;
}

inline LZPoolHeader *slot_from_ptr(const void *ptr, size_t header_size){
    LZPoolHeader *header = (LZPoolHeader *)(((char *)ptr) - header_size);
    assert(header->magic == MAGIC_NUMBER && "Corrupted memory dectected");
    return header;
}

inline LZPoolHeader *get_slot_at(const void *slots, size_t header_size, size_t slot_size, size_t idx){
    return (LZPoolHeader *)(((char *)slots) + ((header_size + slot_size) * idx));
}

inline void insert_slot(LZPoolSlotList *list, LZPoolHeader *slot){
    if(list->tail){
        list->tail->next = slot;
        slot->prev = list->tail;
    }else{
        list->head = slot;
    }

    list->len++;
    list->tail = slot;
}

inline void remove_slot(LZPoolSlotList *list, LZPoolHeader *slot){
    if(slot == list->head){
        list->head = slot->next;
    }

    if(slot == list->tail){
        list->tail = slot->prev;
    }

    if(slot->prev){
        slot->prev->next = slot->next;
    }

    if(slot->next){
        slot->next->prev = slot->prev;
    }

    list->len--;
}

void insert_subpool(LZSubPoolList *list, LZSubPool *subpool){
    if(list->tail){
        list->tail->next = subpool;
        subpool->prev = list->tail;
    }else{
        list->head = subpool;
    }

    list->len++;
    list->tail = subpool;
}

void remove_subpool(LZSubPoolList *list, LZSubPool *subpool){
    if(subpool == list->head){
        list->head = subpool->next;
    }

    if(subpool == list->tail){
        list->tail = subpool->prev;
    }

    if(subpool->prev){
        subpool->prev->next = subpool->next;
    }

    if(subpool->next){
        subpool->next->prev = subpool->prev;
    }

    list->len--;
}

void init_subpool_slots(
    LZSubPool *subpool,
    LZPoolSlotList *slots,
    size_t header_size,
    size_t slot_size
){
    size_t slots_count = subpool->capacity;
    char *offset = subpool->slots;
    LZPoolHeader *prev = NULL;

    for (size_t i = 0; i < slots_count; i++){
        LZPoolHeader *slot = (LZPoolHeader *)offset;

        if(prev){
            prev->next = slot;
        }

        *slot = (LZPoolHeader){
            .magic   = MAGIC_NUMBER,
            .used    = 0,
            .subpool = subpool,
            .prev    = prev
        };

        insert_slot(slots, slot);

        offset += header_size + slot_size;
        prev = slot;
    }
}

inline void init_pool(LZPool *pool, const LZPoolAllocator *allocator, size_t slot_size){
    *pool = (LZPool){
        .header_size = round_size(LZPOOL_DEFAULT_ALIGNMENT, HEADER_SIZE),
        .slot_size   = round_size(LZPOOL_DEFAULT_ALIGNMENT, slot_size),
        .capacity    = 0,
        .slots       = (LZPoolSlotList){0},
        .subpools    = (LZSubPoolList){0},
        .allocator   = allocator
    };
}

void deinit_pool(LZPool *pool){
    if(!pool){
        return;
    }

    size_t header_size = pool->header_size;
    size_t slot_size = pool->slot_size;
    const LZPoolAllocator *allocator = pool->allocator;

    LZSubPool *current = pool->subpools.head;
    LZSubPool *next = NULL;

    while (current){
        next = current->next;

        destroy_subpool(current, allocator, header_size, slot_size);

        current = next;
    }
}

//--------------------------------------------------------------------------//
//                          PUBLIC IMPLEMENTATION                           //
//--------------------------------------------------------------------------//
LZPool *lzpool_create(const LZPoolAllocator *allocator, size_t slot_size){
    LZPool *pool = MEMORY_ALLOC(allocator, LZPool, 1);

    if(!pool){
        return NULL;
    }

    init_pool(pool, allocator, slot_size);

    return pool;
}

void lzpool_destroy(LZPool *pool){
    if(!pool){
        return;
    }

    deinit_pool(pool);

    MEMORY_DEALLOC(pool->allocator, pool, LZPool, 1);
}

inline const LZPoolAllocator *lzpool_allocator(const LZPool *pool){
    return pool->allocator;
}

inline size_t lzpool_available_slots_count(const LZPool *pool){
    return pool->slots.len;
}

inline size_t lzpool_subpools_count(const LZPool *pool){
    return pool->subpools.len;
}

inline size_t lzpool_available(const LZPool *pool){
    return pool->available;
}

inline size_t lzpool_capacity(const LZPool *pool){
    return pool->capacity;
}

inline int lzpool_is_used(const void *ptr){
    return slot_from_ptr(ptr, round_size(LZPOOL_DEFAULT_ALIGNMENT, HEADER_SIZE))->used;
}

int lzpool_prealloc_count(LZPool *pool, size_t count){
    size_t header_size = pool->header_size;
    size_t slot_size = pool->slot_size;
    const LZPoolAllocator *allocator = pool->allocator;

    size_t capacity = (header_size + slot_size) * count;
    void *slots = MEMORY_ALLOC(allocator, char, capacity);
    LZSubPool *subpool = MEMORY_ALLOC(allocator, LZSubPool, 1);

    if(!slots || !subpool){
        MEMORY_DEALLOC(allocator, slots, char, slot_size);
        MEMORY_DEALLOC(allocator, subpool, LZSubPool, 1);

        return 1;
    }

    *subpool = (LZSubPool){
        .used = 0,
        .capacity = count,
        .slots = slots,
        .pool = pool,
        .prev = NULL,
        .next = NULL
    };

    init_subpool_slots(subpool, &pool->slots, header_size, slot_size);
    insert_subpool(&pool->subpools, subpool);

    pool->available += capacity;
    pool->capacity += capacity;

    return 0;
}

int lzpool_prealloc_size(LZPool *pool, size_t size){
    size_t header_size = pool->header_size;
    size_t slot_size = pool->slot_size;
    const LZPoolAllocator *allocator = pool->allocator;

    void *slots = MEMORY_ALLOC(allocator, char, size);
    LZSubPool *subpool = MEMORY_ALLOC(allocator, LZSubPool, 1);

    if(!slots || !subpool){
        MEMORY_DEALLOC(allocator, slots, char, slot_size);
        MEMORY_DEALLOC(allocator, subpool, LZSubPool, 1);

        return 1;
    }

    *subpool = (LZSubPool){
        .used = 0,
        .capacity = size / (header_size + slot_size),
        .slots = slots,
        .pool = pool,
        .prev = NULL,
        .next = NULL
    };

    init_subpool_slots(subpool, &pool->slots, header_size, slot_size);
    insert_subpool(&pool->subpools, subpool);

    pool->available += size;
    pool->capacity += size;

    return 0;
}

void *lzpool_alloc(LZPool *pool){
    LZPoolSlotList *slots = &pool->slots;
    LZPoolHeader *slot = slots->head;

    if(slot){
        remove_slot(slots, slot);

        slot->used = 1;
        slot->subpool->used++;
        pool->available -= pool->header_size + pool->slot_size;

        return slot_chunk(pool->header_size, slot);
    }

    return NULL;
}

void *lzpool_alloc_backup_count(LZPool *pool, size_t count){
    LZPoolSlotList *slots = &pool->slots;

    if(slots->len == 0 && lzpool_prealloc_count(pool, count)){
        return NULL;
    }

    return lzpool_alloc(pool);
}

void *lzpool_alloc_backup_size(LZPool *pool, size_t size){
    LZPoolSlotList *slots = &pool->slots;

    if(slots->len == 0 && lzpool_prealloc_size(pool, size)){
        return NULL;
    }

    return lzpool_alloc(pool);
}

void lzpool_dealloc(void *ptr){
    if(!ptr){
        return;
    }

    size_t header_size = round_size(LZPOOL_DEFAULT_ALIGNMENT, HEADER_SIZE);
    LZPoolHeader *slot = slot_from_ptr(ptr, header_size);
    LZSubPool *subpool = slot->subpool;
    LZPool *pool = subpool->pool;

    assert(slot->used && "Trying to free unused memory");

    insert_slot(&pool->slots, slot);

    slot->used = 0;
    subpool->used--;
    pool->available += pool->header_size + pool->slot_size;
}

void lzpool_dealloc_release(void *ptr){
    if(!ptr){
        return;
    }

    size_t header_size = round_size(LZPOOL_DEFAULT_ALIGNMENT, HEADER_SIZE);
    LZPoolHeader *slot = slot_from_ptr(ptr, header_size);
    LZSubPool *subpool = slot->subpool;
    LZPool *pool = subpool->pool;

    assert(slot->used && "Trying to free unused memory");

    insert_slot(&pool->slots, slot);

    slot->used = 0;
    subpool->used--;
    pool->available += pool->header_size + pool->slot_size;

    if(subpool->used == 0){
        size_t slot_size = pool->slot_size;
        size_t slots_count = subpool->capacity;
        void *slots = subpool->slots;

        for (size_t i = 0; i < slots_count; i++){
            LZPoolHeader *current = get_slot_at(slots, header_size, slot_size, i);
            remove_slot(&pool->slots, current);
        }

        pool->capacity -= (header_size + slot_size) * subpool->capacity;

        remove_subpool(&pool->subpools, subpool);
        destroy_subpool(subpool, pool->allocator, header_size, slot_size);
    }
}