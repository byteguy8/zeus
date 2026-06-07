#include "lzflist.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
    #include <sysinfoapi.h>
    #include <windows.h>
#elif __linux__
    #include <unistd.h>
    #include <sys/mman.h>
#endif

#ifdef _WIN32
    static DWORD windows_page_size(){
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        return sysinfo.dwPageSize;
    }

    #define PAGE_SIZE windows_page_size()
#elif __linux__
    #define PAGE_SIZE sysconf(_SC_PAGESIZE)
#endif

typedef struct lzflregion      LZFLRegion;
typedef struct lzflheader      LZFLHeader;
typedef struct lzflregion_list LZFLRegionList;
typedef struct lzflarea_list   LZFLAreaList;

struct lzflregion{
    size_t            used_bytes;
    size_t            region_size;
    void              *offset;
    void              *subregion;
    struct lzflregion *prev;
    struct lzflregion *next;
};

struct lzflheader{
    size_t            magic;
    size_t            size;
    char              used;
    struct lzflheader *prev;
    struct lzflheader *next;
    struct lzflregion *region;
};

struct lzflregion_list{
    size_t            len;
    struct lzflregion *head;
    struct lzflregion *tail;
};

struct lzflarea_list{
    size_t            len;
    struct lzflheader *head;
    struct lzflheader *tail;
};

struct lzflist{
    size_t           allocable_used_bytes;
    size_t           allocable_reserved_bytes;
    LZFLRegionList   regions;
    LZFLAreaList     free_areas;
    LZFLRegion       *current_region;
    LZFListAllocator *allocator;
};

static size_t region_struct_size = 0;
static size_t header_struct_size = 0;

#define GET_REGION_STRUCT_SIZE() (region_struct_size == 0 ? round_size(LZFLIST_DEFAULT_ALIGNMENT, REGION_SIZE) : region_struct_size)
#define GET_HEADER_STRUCT_SIZE() (header_struct_size == 0 ? round_size(LZFLIST_DEFAULT_ALIGNMENT, HEADER_SIZE) : header_struct_size)

//--------------------------------------------------------------------------//
//                            PRIVATE INTERFACE                             //
//--------------------------------------------------------------------------//
#define HEADER_SIZE (sizeof(LZFLHeader))
#define REGION_SIZE (sizeof(LZFLRegion))
#define LIST_SIZE (sizeof(LZFList))

#define MAGIC_NUMBER 0xDEADBEEF
//--------------------------------  MEMORY  --------------------------------//
static void *lzalloc(LZFListAllocator *allocator, size_t size);
static void *lzrealloc(LZFListAllocator *allocator, void *ptr, size_t old_size, size_t new_size);
static void lzdealloc(LZFListAllocator *allocator, void *ptr, size_t size);

#define MEMORY_ALLOC(_allocator, _type, _count)                         ((_type *)lzalloc((_allocator), sizeof(_type) * (_count)))
#define MEMORY_REALLOC(_allocator, _ptr, _type, _old_count, _new_count) ((_type *)(lzrealloc((_allocator), (_ptr), sizeof(_type) * (_old_count), sizeof(_type) * (_new_count))))
#define MEMORY_DEALLOC(_allocator, _ptr, _type, _count)                 (lzdealloc((_allocator), (_ptr), sizeof(_type) * (_count)))

static void *alloc_backend(size_t size);
static void dealloc_backend(void *ptr, size_t size);
//--------------------------------  UTILS  ---------------------------------//
static size_t round_size(size_t to, size_t size);
static uintptr_t align_addr(size_t alignment, uintptr_t addr);
//--------------------------------  REGION  --------------------------------//
static LZFLRegion *create_region(size_t size, const LZFListAllocator *allocator);
static void destroy_region(LZFLRegion *region);
static int insert_region(LZFLRegion *region, LZFLRegionList *list);
static void remove_region(LZFLRegion *region, LZFLRegionList *list);
//---------------------------------  AREA  ---------------------------------//
static LZFLHeader *create_area(size_t size, LZFLRegion *region, void **out_chunk);
static void *area_chunk(LZFLHeader *area);
static void *area_chunk_end(LZFLHeader *area);
static LZFLHeader *chunk_area(void *ptr);
static size_t calc_region_usable_size(LZFLRegion *region);
static size_t calc_area_size(LZFLHeader *header);
static LZFLHeader *area_next_to(LZFLHeader *area);
static LZFLHeader *is_next_area_free(LZFLHeader *area, LZFLHeader **out_next);
static void *alloc_area(LZFLHeader *area, LZFLAreaList *list);
static void dealloc_area(LZFLHeader *area, LZFLAreaList *list);
static void insert_area_at_end(LZFLHeader *area, LZFLAreaList *list);
static void remove_area(LZFLHeader *area, LZFLAreaList *list);
//-----------------------------  REGION UTILS  -----------------------------//
static void replace_current_region(LZFLRegion *region, LZFList *list);
static int create_and_insert_region(size_t size, LZFList *list);
static void *alloc_from_region(size_t size, LZFLRegion *region, LZFLHeader **out_area);
//--------------------------------  OTHERS  --------------------------------//
static void *look_first_fit(size_t size, LZFList *list, LZFLHeader **out_area);
//--------------------------------------------------------------------------//
//                          PRIVATE IMPLEMENTATION                          //
//--------------------------------------------------------------------------//
inline void *lzalloc(LZFListAllocator *allocator, size_t size){
    return allocator ? allocator->alloc(size, allocator->ctx) : malloc(size);
}

inline void *lzrealloc(LZFListAllocator *allocator, void *ptr, size_t old_size, size_t new_size){
    return allocator ? allocator->realloc(ptr, old_size, new_size, allocator->ctx) : realloc(ptr, new_size);
}

inline void lzdealloc(LZFListAllocator *allocator, void *ptr, size_t size){
    if(allocator){
        allocator->dealloc(ptr, size, allocator->ctx);
    }else{
        free(ptr);
    }
}

void *alloc_backend(size_t size){
#ifndef LZFLIST_BACKEND
    #error "A backend must be defined"
#endif

#if LZFLIST_BACKEND == LZFLIST_BACKEND_MALLOC
    return malloc(size);
#elif LZFLIST_BACKEND == LZFLIST_BACKEND_MMAP
    void *ptr = (char *)mmap(
		NULL,
		size,
		PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS,
		-1,
		0
    );

    return ptr == MAP_FAILED ? NULL : ptr;
#elif LZFLIST_BACKEND == LZFLIST_BACKEND_VIRTUALALLOC
    return VirtualAlloc(
        NULL,
        size,
        MEM_COMMIT,
        PAGE_READWRITE
    );
#else
    #error "Unknown backend"
#endif
}

void dealloc_backend(void *ptr, size_t size){
    if(!ptr){
        return;
    }

#ifndef LZFLIST_BACKEND
    #error "A backend must be defined"
#endif

#if LZFLIST_BACKEND == LZFLIST_BACKEND_MALLOC
    free(ptr);
#elif LZFLIST_BACKEND == LZFLIST_BACKEND_MMAP
    if(munmap(ptr, size) == -1){
        perror(NULL);
    }
#elif LZFLIST_BACKEND == LZFLIST_BACKEND_VIRTUALALLOC
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    #error "Unknown backend"
#endif
}

inline size_t round_size(size_t to, size_t size){
    size_t mod = size % to;
    size_t padding = mod == 0 ? 0 : to - mod;
    return padding + size;
}

inline uintptr_t align_addr(size_t alignment, uintptr_t addr){
    size_t mod = addr % alignment;
    size_t padding = mod == 0 ? 0 : alignment - mod;
    return padding + addr;
}

LZFLRegion *create_region(size_t requested_size, const LZFListAllocator *allocator){
    size_t region_size = GET_REGION_STRUCT_SIZE();

    requested_size += region_size + GET_HEADER_STRUCT_SIZE();

    size_t page_size = (size_t)PAGE_SIZE;
    size_t needed_size = page_size * (requested_size / page_size + 1);
    char *raw_buff = (char *)(allocator ? allocator->alloc(needed_size, allocator->ctx) : alloc_backend(needed_size));

    if (!raw_buff){
        return NULL;
    }

    char *raw_subregion = raw_buff + region_size;
    LZFLRegion *region = (LZFLRegion *)raw_buff;

    region->used_bytes = 0;
    region->region_size = needed_size;
    region->offset = raw_subregion;
    region->subregion = raw_subregion;
    region->prev = NULL;
    region->next = NULL;

    return region;
}

inline void destroy_region(LZFLRegion *region){
    if(!region){
        return;
    }

    dealloc_backend(region, region->region_size);
}

int insert_region(LZFLRegion *region, LZFLRegionList *list){
    region->prev = NULL;
    region->next = NULL;

    if(list->tail){
        region->prev = list->tail;
        list->tail->next = region;
    }else{
        list->head = region;
    }

    list->len++;
    list->tail = region;

    return 0;
}

void remove_region(LZFLRegion *region, LZFLRegionList *list){
    if(region == list->head){
        list->head = region->next;
    }
    if(region == list->tail){
        list->tail = region->prev;
    }

    if(region->prev){
        region->prev->next = region->next;
    }
    if(region->next){
        region->next->prev = region->prev;
    }

    list->len--;

    region->prev = NULL;
    region->next = NULL;
}

// A 'area' represents a portion of memory from the subregion of
// the specified region, which is subdivided in: header and chunk.
LZFLHeader *create_area(size_t size, LZFLRegion *region, void **out_chunk){
    assert(size % LZFLIST_DEFAULT_ALIGNMENT == 0 && "size must be aligned to 'LZFLIST_DEFAULT_ALIGNMENT'");

    char *region_end = ((char *)region) + region->region_size;
    char *offset = (char *)region->offset;

    assert(offset <= region_end && "offset cannot pass region");

    if(offset == region_end){
        return NULL;
    }

    char *chunk_start = offset + HEADER_SIZE;
    char *chunk_end = chunk_start + size;

    if(chunk_end > region_end){
        return NULL;
    }

    LZFLHeader *area = (LZFLHeader *)offset;

    area->magic = MAGIC_NUMBER;
    area->used = 0;
    area->size = size;
    area->prev = NULL;
    area->next = NULL;
    area->region = region;

    region->offset = (void *)chunk_end;

    if(out_chunk){
        *out_chunk = (void *)chunk_start;
    }

    return area;
}

inline void *area_chunk(LZFLHeader *area){
    return (void *)(((char *)area) + GET_HEADER_STRUCT_SIZE());
}

inline void *area_chunk_end(LZFLHeader *area){
    return (void *)(((char *)area_chunk(area)) + area->size);
}

inline LZFLHeader *chunk_area(void *ptr){
    LZFLHeader *area = (LZFLHeader *)(((char *)ptr) - GET_HEADER_STRUCT_SIZE());
    assert(area->magic == MAGIC_NUMBER && "corrupted area");
    return area;
}

inline size_t calc_region_usable_size(LZFLRegion *region){
    return region->region_size - REGION_SIZE;
}

inline size_t calc_area_size(LZFLHeader *area){
    return HEADER_SIZE + area->size;
}

inline LZFLHeader *area_next_to(LZFLHeader *area){
    char *offset = area->region->offset;
    char *next_area = (char *)area_chunk_end(area);

    assert(next_area <= offset && "any area cannot pass offset");

    if(next_area == offset){
        return NULL;
    }

    return (LZFLHeader *)next_area;
}

inline LZFLHeader *is_next_area_free(LZFLHeader *header, LZFLHeader **out_next){
    LZFLHeader *next = area_next_to(header);

    if(next){
        if(out_next){
            *out_next = next;
        }

        return next->used ? NULL : next;
    }

    return NULL;
}

inline void *alloc_area(LZFLHeader *area, LZFLAreaList *list){
    assert(!area->used && "trying to allocate used memory");

    if(list){
        remove_area(area, list);
    }

    area->region->used_bytes += calc_area_size(area);
    area->used = 1;

    return area_chunk(area);
}

inline void dealloc_area(LZFLHeader *area, LZFLAreaList *list){
    assert(area->used && "trying to free unused memory");

    if(list){
        insert_area_at_end(area, list);
    }

    area->region->used_bytes -= calc_area_size(area);
    area->used = 0;
}

void insert_area_at_end(LZFLHeader *header, LZFLAreaList *list){
    header->prev = NULL;
    header->next = NULL;

    if(list->tail){
        list->tail->next = header;
        header->prev = list->tail;
    }else{
        list->head = header;
    }

    list->len++;
    list->tail = header;
}

void remove_area(LZFLHeader *area, LZFLAreaList *list){
    if(area == list->head){
        list->head = area->next;
    }
    if(area == list->tail){
        list->tail = area->prev;
    }

    list->len--;

    if(area->prev){
        area->prev->next = area->next;
    }
    if(area->next){
        area->next->prev = area->prev;
    }

    area->prev = NULL;
    area->next = NULL;
}

inline void replace_current_region(LZFLRegion *region, LZFList *list){
    list->current_region = region;
}

int create_and_insert_region(size_t size, LZFList *list){
    LZFLRegion *region = create_region(size, list->allocator);

    if(region){
        insert_region(region, &list->regions);
        replace_current_region(region, list);

        return 0;
    }

    return 1;
}

void *alloc_from_region(size_t size, LZFLRegion *region, LZFLHeader **out_area){
    LZFLHeader *area = create_area(size, region, NULL);

    if(!area){
        return NULL;
    }

    if(out_area){
        *out_area = area;
    }

    return alloc_area(area, NULL);
}

void *look_first_fit(size_t size, LZFList *list, LZFLHeader **out_area){
    LZFLAreaList *free_areas = &list->free_areas;
    LZFLHeader *current_area = free_areas->head;
    LZFLHeader *next_area = NULL;

    while (current_area){
        next_area = current_area->next;

        if(current_area->size >= size){
            if(out_area){
                *out_area = current_area;
            }

            return alloc_area(current_area, free_areas);
        }

        current_area = next_area;
    }

    return NULL;
}
//--------------------------------------------------------------------------//
//                          PUBLIC IMPLEMENTATION                           //
//--------------------------------------------------------------------------//
LZFList *lzflist_create(LZFListAllocator *allocator){
    LZFList *list = MEMORY_ALLOC(allocator, LZFList, 1);

    if(!list){
        return NULL;
    }

    *list = (LZFList){
        .allocator = allocator
    };

    return list;
}

void lzflist_destroy(LZFList *list){
    if(!list){
        return;
    }

    LZFLRegion *current = list->regions.head;
    LZFLRegion *next = NULL;

    while (current){
        next = current->next;
        destroy_region(current);
        current = next;
    }

    MEMORY_DEALLOC(list->allocator, list, LZFList, 1);
}

inline size_t lzflist_ptr_size(const void *ptr){
    return chunk_area((void *)ptr)->size;
}

int lzflist_prealloc(LZFList *list, size_t size){
    assert(size % PAGE_SIZE == 0 && "'size' must be divisible by 'page size'");

    LZFLRegion *region = create_region(size, list->allocator);

    if(region){
        insert_region(region, &list->regions);
        replace_current_region(region, list);

        list->allocable_reserved_bytes += calc_region_usable_size(region);

        return 0;
    }

    return 1;
}

inline size_t lzflist_regions_count(const LZFList *list){
    return list->regions.len;
}

inline size_t lzflist_free_areas_count(const LZFList *list){
    return list->free_areas.len;
}

inline size_t lzflist_allocable_used_bytes(const LZFList *list){
    return list->allocable_used_bytes;
}

inline size_t lzflist_allocable_reserved_bytes(const LZFList *list){
    return list->allocable_reserved_bytes;
}

inline size_t lzflist_allocable_free_bytes(const LZFList *list){
    return lzflist_allocable_reserved_bytes(list) - lzflist_allocable_used_bytes(list);
}

void *lzflist_alloc(LZFList *list, size_t size){
    if(size == 0){
        return NULL;
    }

    size = round_size(LZFLIST_DEFAULT_ALIGNMENT, size);
    LZFLHeader *area = NULL;
    void *ptr = look_first_fit(size, list, &area);

    if(ptr){
        list->allocable_used_bytes += calc_area_size(area);
        return ptr;
    }

    LZFLRegion *current_region = list->current_region;

    if(current_region && (ptr = alloc_from_region(size, current_region, &area))){
        list->allocable_used_bytes += calc_area_size(area);
        return ptr;
    }

    if(create_and_insert_region(size, list)){
        return NULL;
    }

    ptr = alloc_from_region(size, list->current_region, &area);
    list->allocable_reserved_bytes += calc_region_usable_size(list->current_region);
    list->allocable_used_bytes += calc_area_size(area);

    return ptr;
}

inline void *lzflist_calloc(LZFList *list, size_t size){
    void *ptr = lzflist_alloc(list, size);

    if(ptr){
        memset(ptr, 0, size);
    }

    return ptr;
}

void *lzflist_realloc(LZFList *list, void *ptr, size_t new_size){
    if(!ptr){
        return lzflist_alloc(list, new_size);
    }

    if(new_size == 0){
        lzflist_dealloc(list, ptr);
        return NULL;
    }

    LZFLHeader *old_area = chunk_area(ptr);
    size_t old_size = old_area->size;

    if(new_size <= old_size){
        return ptr;
    }

    void *new_ptr = lzflist_alloc(list, new_size);

    if(new_ptr){
        memcpy(new_ptr, ptr, old_size);
        dealloc_area(old_area, &list->free_areas);
    }

    return new_ptr;
}

void lzflist_dealloc(LZFList *list, void *ptr){
    if(!ptr){
        return;
    }

    LZFLHeader *area = chunk_area(ptr);
    LZFLRegion *region = area->region;
    LZFLAreaList *areas_list = &list->free_areas;

    dealloc_area(area, areas_list);

    list->allocable_used_bytes -= calc_area_size(area);

    if(region->used_bytes == 0){
        LZFLHeader *current_area = (LZFLHeader *)region->subregion;

        while(current_area){
            remove_area(current_area, areas_list);
            current_area = area_next_to(current_area);
        }

        if(region == list->current_region){
            list->current_region = NULL;
        }

        remove_region(region, &list->regions);

        list->allocable_reserved_bytes -= calc_region_usable_size(region);

        destroy_region(region);
    }
}