#include "lzbstr.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

struct lzbstr{
    size_t                offset;
    size_t                buff_len;
    char                  *buff;
    const LZBStrAllocator *allocator;
};

static inline void *lzalloc(const LZBStrAllocator *allocator, size_t size){
    return allocator ? allocator->alloc(size, allocator->ctx) : malloc(size);
}

static inline void *lzrealloc(const LZBStrAllocator *allocator, void *ptr, size_t old_size, size_t new_size){
    return allocator ? allocator->realloc(ptr, old_size, new_size, allocator->ctx) : realloc(ptr, new_size);
}

static inline void lzdealloc(const LZBStrAllocator *allocator, void *ptr, size_t size){
    if(allocator){
        allocator->dealloc(ptr, size, allocator->ctx);
    }else{
        free(ptr);
    }
}

#define MEMORY_ALLOC(_allocator, _type, _count)                         ((_type *)lzalloc((_allocator), sizeof(_type) * (_count)))
#define MEMORY_REALLOC(_allocator, _ptr, _type, _old_count, _new_count) ((_type *)(lzrealloc((_allocator), (_ptr), sizeof(_type) * (_old_count), sizeof(_type) * (_new_count))))
#define MEMORY_DEALLOC(_allocator, _ptr, _type, _count)                 (lzdealloc((_allocator), (_ptr), sizeof(_type) * (_count)))

static inline size_t max(size_t a, size_t b){
    return a > b ? a : b;
}

static inline size_t min(size_t a, size_t b){
    return a < b ? a : b;
}

static inline uint64_t next_pow2m1(uint64_t x) {
    x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x |= x >> 32;

    return x;
}

static inline uint64_t next_pow2(uint64_t x) {
	return next_pow2m1(x - 1) + 1;
}

static inline size_t available_space(LZBStr *str){
    return str->buff_len - str->offset;
}

static int grow(LZBStr *str, char multiplier, size_t required){
    size_t buff_len = str->buff_len;
    size_t new_buff_len = ((size_t)next_pow2((uint64_t)(buff_len + required))) * multiplier;
    void *new_buff = MEMORY_REALLOC(str->allocator, str->buff, char, str->buff_len, new_buff_len);

    if(!new_buff){
        return 1;
    }

    str->buff_len = new_buff_len;
    str->buff = new_buff;

    return 0;
}

LZBStr *lzbstr_create(const LZBStrAllocator *allocator){
    LZBStr *str = MEMORY_ALLOC(allocator, LZBStr, 1);

    if(!str){
        return NULL;
    }

    str->offset = 0;
    str->buff_len = 0;
    str->buff = NULL;
    str->allocator = allocator;

    return str;
}

char *lzbstr_destroy_save_buff(LZBStr *str){
    if(!str){
        return NULL;
    }

    char *buff = str->buff;
    MEMORY_DEALLOC(str->allocator, str, LZBStr, 1);

    return buff;
}

void lzbstr_destroy(LZBStr *str){
    if(!str){
        return;
    }

    const LZBStrAllocator *allocator = str->allocator;

    MEMORY_DEALLOC(allocator, str->buff, char, str->buff_len);
    MEMORY_DEALLOC(allocator, str, LZBStr, 1);
}

inline size_t lzbstr_len(const LZBStr *str){
    return str->offset;
}

size_t lzbstr_capacity(const LZBStr *str){
    return str->buff_len;
}

inline size_t lzbstr_available_space(const LZBStr *str){
    return str->buff_len - str->offset;
}

void lzbstr_reset(LZBStr *str){
    str->offset = 0;

    if(str->buff){
        str->buff[0] = 0;
    }
}

inline int lzbstr_grow_by(LZBStr *str, size_t by){
    return grow(str, 1, by + available_space(str));
}

int lzbstr_reduce(LZBStr *str){
    if(lzbstr_len(str) == 0){
        return LZBSTR_ERR_LEN;
    }

    size_t new_buff_len = str->buff_len / 2;

    if(new_buff_len >= lzbstr_len(str) + 1){
        void *new_buff = MEMORY_REALLOC(
            str->allocator,
            str->buff,
            char,
            str->buff_len,
            new_buff_len
        );

        if(!new_buff){
            return LZBSTR_ERR_ALLOC;
        }

        str->buff_len = new_buff_len;
        str->buff = new_buff;
    }

    return LZBSTR_OK;
}

inline const char *lzbstr_value(LZBStr *str){
    return (const char *)str->buff;
}

int lzbstr_rclone_buff_rng(
    const LZBStr *str,
    const LZBStrAllocator *allocator,
    size_t start,
    size_t len,
    size_t *out_len,
    char **out_value
){
    if(lzbstr_len(str) == 0){
        return LZBSTR_ERR_LEN;
    }

    size_t to = start + len;

    if(start >= to || to > lzbstr_len(str)){
        return LZBSTR_ERR_RANGE;
    }

    char *cloned_buff = MEMORY_ALLOC(allocator, char, len + 1);

    if(!cloned_buff){
        return LZBSTR_ERR_ALLOC;
    }

    memcpy(cloned_buff, str->buff + start, len);
    cloned_buff[len] = 0;

    if(out_len){
        *out_len = len;
    }

    *out_value = cloned_buff;

    return LZBSTR_OK;
}

inline int lzbstr_rclone_buff(LZBStr *str, const LZBStrAllocator *allocator, size_t *out_len, char **out_value){
    return lzbstr_rclone_buff_rng(str, allocator, 0, lzbstr_len(str), out_len, out_value);
}

int lzbstr_append(LZBStr *str, const char *value){
    size_t value_len = strlen(value) + 1;
    size_t available = lzbstr_available_space(str);
    size_t required = max(value_len, available) - min(value_len, available);

    if(value_len > available && grow(str, 2, max(required, 16))){
        return LZBSTR_ERR_ALLOC;
    }

    memcpy(str->buff + str->offset, value, value_len -= 1);
    str->offset += value_len;
    str->buff[str->offset] = 0;

    return LZBSTR_OK;
}

int lzbstr_append_args(LZBStr *str, const char *fmt, ...){
    va_list args;

    va_start(args, fmt);

    int formatted_value_len = vsnprintf(NULL, 0, fmt, args);

    va_end(args);

    assert(formatted_value_len != -1 && "No hay ma na que hacel");

    size_t value_len = ((size_t)formatted_value_len) + 1;
    size_t available = lzbstr_available_space(str);
    size_t required = max(value_len, available) - min(value_len, available);

    if(value_len > available && grow(str, 2, max(required, 16))){
        return LZBSTR_ERR_ALLOC;
    }

    va_start(args, fmt);

    vsnprintf(str->buff + str->offset, value_len, fmt, args);

    va_end(args);

    str->offset += formatted_value_len;

    return LZBSTR_OK;
}

int lzbstr_insert_args(LZBStr *str, size_t from, const char *fmt, ...){
    if(lzbstr_len(str) == 0 && from > 0){
        return LZBSTR_ERR_LEN;
    }

    if(from > lzbstr_len(str)){
        return LZBSTR_ERR_RANGE;
    }

    // 'formatted_value_len' not include the NULL character
    // 'value_len' include the NULL character

    va_list args;

    va_start(args, fmt);

    int formatted_value_len = vsnprintf(NULL, 0, fmt, args);

    va_end(args);

    assert(formatted_value_len != -1 && "No hay ma na que hacel");

    size_t value_len = ((size_t)formatted_value_len) + 1;
    size_t available = available_space(str);
    size_t required = max(value_len, available) - min(value_len, available);

    if(value_len > available && grow(str, 2, max(required, 16))){
        return LZBSTR_ERR_ALLOC;
    }

    if(from < lzbstr_len(str)){
        size_t to = from + formatted_value_len;
        size_t move_len = lzbstr_len(str) - from;
        char character = str->buff[from];

        memmove(str->buff + to, str->buff + from, move_len);

        va_start(args, fmt);

        vsnprintf(str->buff + from, value_len, fmt, args);

        va_end(args);

        str->offset += formatted_value_len;
        str->buff[to] = character;
        str->buff[str->offset] = 0;

        return LZBSTR_OK;
    }

    va_start(args, fmt);

    vsnprintf(str->buff + from, value_len, fmt, args);

    va_end(args);

    str->offset += formatted_value_len;

    return LZBSTR_OK;
}

int lzbstr_remove(LZBStr *str, size_t from, size_t len){
    if(lzbstr_len(str) == 0){
        return LZBSTR_ERR_LEN;
    }

    size_t to = from + len;

    if(from >= to || to > lzbstr_len(str)){
        return LZBSTR_ERR_RANGE;
    }

    if(to == lzbstr_len(str)){
        str->offset = from;
        str->buff[from] = 0;
    }else{
        memmove(str->buff + from, str->buff + to, lzbstr_len(str) - to);

        str->offset -= to - from;
        str->buff[str->offset] = 0;
    }

    return 0;
}