#ifndef LZBSTR_H
#define LZBSTR_H

#include <stddef.h>

#define LZBSTR_OK            0
#define LZBSTR_ERR_ALLOC     1
#define LZBSTR_ERR_LEN       2
#define LZBSTR_ERR_RANGE     3
#define LZBSTR_ERR_ALLOCATOR 4

typedef struct lzbstr_allocator{
    void *ctx;
    void *(*alloc)(size_t size, void *ctx);
    void *(*realloc)(void *ptr, size_t old_size, size_t new_size, void *ctx);
    void (*dealloc)(void *ptr, size_t size, void *ctx);
}LZBStrAllocator;

typedef struct lzbstr LZBStr;

LZBStr *lzbstr_create(const LZBStrAllocator *allocator);
char *lzbstr_destroy_save_buff(LZBStr *str);
void lzbstr_destroy(LZBStr *str);

size_t lzbstr_len(const LZBStr *str);
size_t lzbstr_capacity(const LZBStr *str);
size_t lzbstr_available_space(const LZBStr *str);

void lzbstr_reset(LZBStr *str);
int lzbstr_grow_by(LZBStr *str, size_t by);
int lzbstr_reduce(LZBStr *str);

const char *lzbstr_value(LZBStr *str);

int lzbstr_rclone_buff_rng(
    const LZBStr *str,
    const LZBStrAllocator *allocator,
    size_t start,
    size_t len,
    size_t *out_len,
    char **out_value
);
int lzbstr_rclone_buff(LZBStr *str, const LZBStrAllocator *allocator, size_t *out_len, char **out_value);

#define LZBSTR_CLONE_BUFF_RNG(_str, _start, _len, _out_len, _out_value) \
    (lzbstr_rclone_buff_rng(_str, NULL, _start, _len, _out_len, _out_value))

#define LZBSTR_CLONE_BUFF(_str, _out_len, _out_value) \
    (lzbstr_rclone_buff(_str, NULL, _out_len, ))

int lzbstr_append(LZBStr *str, const char *value);
int lzbstr_append_args(LZBStr *str, const char *fmt, ...);
int lzbstr_insert_args(LZBStr *str, size_t from, const char *fmt, ...);
int lzbstr_remove(LZBStr *str, size_t from, size_t len);

#endif