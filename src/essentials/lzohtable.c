#include "lzohtable.h"
#include <stdlib.h>
#include <assert.h>
#include <string.h>

static inline void *lzalloc(size_t size, LZOHTableAllocator *allocator){
    return allocator ? allocator->alloc(size, allocator->ctx) : malloc(size);
}

static inline void *lzrealloc(void *ptr, size_t old_size, size_t new_size, LZOHTableAllocator *allocator){
    return allocator ? allocator->realloc(ptr, old_size, new_size, allocator->ctx) : realloc(ptr, new_size);
}

static inline void lzdealloc(void *ptr, size_t size, LZOHTableAllocator *allocator){
    allocator ? allocator->dealloc(ptr, size, allocator->ctx) : free(ptr);
}

#define MEMORY_ALLOC(_type, _count, _allocator)((_type *)lzalloc(sizeof(_type) * (_count), (_allocator)))
#define MEMORY_REALLOC(_ptr, _type, _old_count, _new_count, allocator)((type *)(lzrealloc((_ptr), sizeof(_type) * (_old_count), sizeof(_type) * (_new_count), (_allocator))))
#define MEMORY_DEALLOC(_ptr, _type, _count, _allocator)(lzdealloc((_ptr), sizeof(_type) * (_count), (_allocator)))

#define SLOT_SIZE sizeof(LZOHTableSlot)

static inline int is_power_of_two(uintptr_t x){
    return (x & (x - 1)) == 0;
}

static inline lzohtable_hash_t fnv_1a_hash(size_t key_size, const uint8_t *key){
    const uint64_t prime = 0x00000100000001b3;
    const uint64_t basis = 0xcbf29ce484222325;
    uint64_t hash = basis;

    for (size_t i = 0; i < key_size; i++){
        hash ^= key[i];
        hash *= prime;
    }

    return hash;
}

static LZOHTableSlot* robin_hood_lookup(const void *key, size_t key_size, LZOHTable *table, size_t *out_idx){
    size_t m = table->m;
    LZOHTableSlot *slots = table->slots;
    lzohtable_hash_t hash = fnv_1a_hash(key_size, key);
    size_t i = hash & (m - 1);
    size_t probe = 0;

    while (1){
        LZOHTableSlot slot = slots[i];

        if(!slot.used){
            break;
        }

        if(slot.probe < probe){
            break;
        }

        if(key_size == slot.key_size && memcmp(key, slot.key, key_size) == 0){
            if(out_idx){
                *out_idx = i;
            }

            return slots + i;
        }

        i = (i + 1) & (m - 1);
        probe++;
    }

    return NULL;
}

static int robin_hood_insert(
    size_t m,
    LZOHTableSlot moving_slot,
    LZOHTableSlot *slots,
    char *out_vcpy,
    void **out_old_value,
    size_t *out_old_value_size
){
    size_t count = 0;
    size_t i = moving_slot.hash & (m - 1);

    while(count < m){
        LZOHTableSlot current_slot = slots[i];

        if(current_slot.used){
            if(moving_slot.key_size == current_slot.key_size && memcmp(moving_slot.key, current_slot.key, moving_slot.key_size) == 0){
                if(out_vcpy){
                    *out_vcpy = current_slot.vcpy;
                }

                if(out_old_value){
                    *out_old_value = current_slot.value;
                }

                if(out_old_value_size){
                    *out_old_value_size = current_slot.value_size;
                }

                LZOHTableSlot *final_slot = slots + i;

                final_slot->value_size = moving_slot.value_size;
                final_slot->value = moving_slot.value;

                return 2;
            }

            if(moving_slot.probe > current_slot.probe){
                LZOHTableSlot rich_slot = current_slot;

                *(slots + i) = moving_slot;
                moving_slot = rich_slot;
            }

            count++;
            moving_slot.probe++;
            i = (i + 1) & (m - 1);

            continue;
        }

        *(slots + i) = moving_slot;

        return 3;
    }

    return 1;
}

static LZOHTableSlot *create_slots(size_t m, LZOHTableAllocator *allocator){
    assert(is_power_of_two(m));

    LZOHTableSlot *slots = MEMORY_ALLOC(LZOHTableSlot, m, allocator);

    if(!slots){
        return NULL;
    }

    memset(slots, 0, SLOT_SIZE * m);

    return slots;
}

static inline void destroy_slots(size_t m, LZOHTableSlot *slots, LZOHTableAllocator *allocator){
    MEMORY_DEALLOC(slots, LZOHTableSlot, m, allocator);
}

static LZOHTableSlot *grow_slots(size_t old_m, LZOHTableAllocator *allocator, size_t *out_new_m){
    assert(is_power_of_two(old_m));

    size_t new_m = old_m * 2;
    LZOHTableSlot *new_slots = MEMORY_ALLOC(LZOHTableSlot, new_m, allocator);

    if(!new_slots){
        return NULL;
    }

    memset(new_slots, 0, SLOT_SIZE * new_m);

    if(out_new_m){
        *out_new_m = new_m;
    }

    return new_slots;
}

int copy_paste_slots(LZOHTable *table){
    size_t new_m;
    size_t old_m = table->m;
    LZOHTableSlot *old_slots = table->slots;
    LZOHTableAllocator *allocator = table->allocator;
    LZOHTableSlot *new_slots = grow_slots(old_m, allocator, &new_m);

    if(!new_slots){
        return 1;
    }

    for (size_t i = 0; i < old_m; i++){
        LZOHTableSlot old_slot = old_slots[i];

        if(!old_slot.used){
            continue;
        }

        LZOHTableSlot moving_slot = (LZOHTableSlot){
            .used = 1,
            .hash = old_slot.hash,
            .probe = 0,
            .kcpy = old_slot.kcpy,
            .vcpy = old_slot.vcpy,
            .key_size = old_slot.key_size,
            .value_size = old_slot.value_size,
            .key = old_slot.key,
            .value = old_slot.value
        };

        robin_hood_insert(
            new_m,
            moving_slot,
            new_slots,
            NULL,
            NULL,
            NULL
        );
    }

    destroy_slots(old_m, old_slots, allocator);

    table->m = new_m;
    table->slots = new_slots;

    return 0;
}

LZOHTable *lzohtable_create(size_t m, float lfth, LZOHTableAllocator *allocator){
    LZOHTableSlot *slots = create_slots(m, allocator);
    LZOHTable *table = MEMORY_ALLOC(LZOHTable, 1, allocator);

    if(!slots || !table){
        destroy_slots(m, slots, allocator);
        MEMORY_DEALLOC(table, LZOHTable, 1, allocator);

        return NULL;
    }

    table->n = 0;
    table->m = m;
    table->lfth = lfth;
    table->slots = slots;
    table->allocator = allocator;

    return table;
}

void lzohtable_destroy_help(const void *extra, lzohtable_clean_up *clean_up_helper, LZOHTable *table){
    if(!table){
        return;
    }

    LZOHTableAllocator *allocator = table->allocator;

    lzohtable_clear_help(extra, clean_up_helper, table);
    destroy_slots(table->m, table->slots, allocator);
    MEMORY_DEALLOC(table, LZOHTable, 1, allocator);
}

void lzohtable_print(void (*print_helper)(size_t count, size_t len, size_t idx, size_t probe, size_t key_size, size_t value_size, void *key, void *value), LZOHTable *table){
    size_t count = 1;
    size_t n = table->n;
    size_t m = table->m;

    for (size_t i = 0; i < m; i++){
        LZOHTableSlot slot = table->slots[i];

        if(slot.used){
            print_helper(count++, n, i, slot.probe, slot.key_size, slot.value_size, slot.key, slot.value);
        }
    }
}

int lzohtable_lookup(size_t key_size, const void *key, LZOHTable *table, void **out_value){
    lzohtable_hash_t hash = fnv_1a_hash(key_size, key);
    size_t i = hash & (table->m - 1);
    size_t m = table->m;
    size_t probe = 0;

    while (1){
        LZOHTableSlot slot = table->slots[i];

        if(!slot.used){
            break;
        }

        if(slot.probe < probe){
            break;
        }

        if(key_size == slot.key_size && memcmp(key, slot.key, key_size) == 0){
            if(out_value){
                *out_value = slot.value;
            }

            return 1;
        }

        i = (i + 1) & (m - 1);
        probe++;
    }

    return 0;
}

void lzohtable_clear_help(const void *extra, lzohtable_clean_up *clean_up_helper, LZOHTable *table){
    size_t m = table->m;
    LZOHTableSlot *slots = table->slots;
    LZOHTableAllocator *allocator = table->allocator;

    for (size_t i = 0; i < m; i++){
        LZOHTableSlot *slot = &slots[i];

        if(slot->used){
            char kcpy = slot->kcpy;
            char vcpy = slot->vcpy;
            size_t slot_key_size = slot->key_size;
            size_t slot_value_size = slot->value_size;
            void *slot_key = slot->key;
            void *slot_value = slot->value;

            void *external_key = kcpy ? NULL : slot_key;
            void *external_value = vcpy ? NULL : slot_value;

            if(clean_up_helper){
                clean_up_helper(external_key, external_value, (void *)extra);
            }

            if(kcpy){
                MEMORY_DEALLOC(slot_key, char, slot_key_size, allocator);
            }

            if(vcpy){
                MEMORY_DEALLOC(slot_value, char, slot_value_size, allocator);
            }

            memset(slot, 0, SLOT_SIZE);
        }
    }

    table->n = 0;
}

int lzohtable_put(size_t key_size, const void *key, const void *value, LZOHTable *table, lzohtable_hash_t *out_hash){
    if(LZOHTABLE_LOAD_FACTOR(table) >= table->lfth && copy_paste_slots(table)){
        return 1;
    }

    lzohtable_hash_t hash = fnv_1a_hash(key_size, key);
    LZOHTableSlot moving_slot = (LZOHTableSlot){
        .used = 1,
        .hash = hash,
        .probe = 0,
        .kcpy = 0,
        .vcpy = 0,
        .key_size = key_size,
        .value_size = 0,
        .key = (void *)key,
        .value = (void *)value
    };

    if(robin_hood_insert(table->m, moving_slot, table->slots, NULL, NULL, NULL) == 3){
        table->n++;
    }

    if(out_hash){
        *out_hash = hash;
    }

    return 0;
}

int lzohtable_put_ck(size_t key_size, const void *key, const void *value, LZOHTable *table, lzohtable_hash_t *out_hash){
    LZOHTableAllocator *allocator = table->allocator;
    void *copied_key = MEMORY_ALLOC(char, key_size, allocator);

    if(!copied_key){
        return 1;
    }

    if(LZOHTABLE_LOAD_FACTOR(table) >= table->lfth && copy_paste_slots(table)){
        MEMORY_DEALLOC(copied_key, char, key_size, allocator);
        return 1;
    }

    memcpy(copied_key, key, key_size);

    lzohtable_hash_t hash = fnv_1a_hash(key_size, copied_key);
    LZOHTableSlot moving_slot = (LZOHTableSlot){
        .used = 1,
        .hash = hash,
        .probe = 0,
        .kcpy = 1,
        .vcpy = 0,
        .key_size = key_size,
        .value_size = 0,
        .key = copied_key,
        .value = (void *)value
    };

    switch (robin_hood_insert(table->m, moving_slot, table->slots, NULL, NULL, NULL)){
        case 2:{
            // The 'key' already exist
            MEMORY_DEALLOC(copied_key, char, key_size, allocator);
            break;
        }case 3:{
            // The 'key' did not exist (new 'register')
            table->n++;
            break;
        }default:{
            break;
        }
    }

    if(out_hash){
        *out_hash = hash;
    }

    return 0;
}

int lzohtable_put_help(
    size_t key_size,
    const void *key,
    const void *value,
    LZOHTable *table,
    void **out_value,
    lzohtable_hash_t *out_hash
){
    if(LZOHTABLE_LOAD_FACTOR(table) >= table->lfth && copy_paste_slots(table)){
        return 1;
    }

    lzohtable_hash_t hash = fnv_1a_hash(key_size, key);
    LZOHTableSlot moving_slot = (LZOHTableSlot){
        .used = 1,
        .hash = hash,
        .probe = 0,
        .kcpy = 0,
        .vcpy = 0,
        .key_size = key_size,
        .value_size = 0,
        .key = (void *)key,
        .value = (void *)value
    };

    if(robin_hood_insert(table->m, moving_slot, table->slots, NULL, out_value, NULL) == 3){
        table->n++;
    }

    if(out_hash){
        *out_hash = hash;
    }

    return 0;
}

int lzohtable_put_ckv(size_t key_size, const void *key, size_t value_size, const void *value, LZOHTable *table, lzohtable_hash_t *out_hash){
    LZOHTableAllocator *allocator = table->allocator;
    void *copied_key = MEMORY_ALLOC(char, key_size, allocator);
    void *copied_value = MEMORY_ALLOC(char, value_size, allocator);

    if(!copied_key || !copied_value){
        MEMORY_DEALLOC(copied_key, char, key_size, allocator);
        MEMORY_DEALLOC(copied_value, char, value_size, allocator);
        return 1;
    }

    if(LZOHTABLE_LOAD_FACTOR(table) >= table->lfth && copy_paste_slots(table)){
        MEMORY_DEALLOC(copied_key, char, key_size, allocator);
        MEMORY_DEALLOC(copied_value, char, value_size, allocator);
        return 1;
    }

    memcpy(copied_key, key, key_size);
    memcpy(copied_value, value, value_size);

    lzohtable_hash_t hash = fnv_1a_hash(key_size, copied_key);
    LZOHTableSlot moving_slot = (LZOHTableSlot){
        .used = 1,
        .hash = hash,
        .probe = 0,
        .kcpy = 1,
        .vcpy = 1,
        .key_size = key_size,
        .value_size = value_size,
        .key = copied_key,
        .value = copied_value
    };
    char old_vcpy;
    size_t old_value_size;
    void *old_value = NULL;

    switch (robin_hood_insert(table->m, moving_slot, table->slots, &old_vcpy, &old_value, &old_value_size)){
        case 2:{
            // The 'key' already exist
            MEMORY_DEALLOC(copied_key, char, key_size, allocator);

            if(old_vcpy){
                MEMORY_DEALLOC(old_value, char, old_value_size, allocator);
            }

            break;
        }case 3:{
            // The 'key' did not exist (new 'register')
            table->n++;
            break;
        }default:{
            break;
        }
    }

    return 0;
}

void lzohtable_remove_help(size_t key_size, const void *key, const void *extra, lzohtable_clean_up *clean_up_helper, LZOHTable *table){
    size_t idx;
    LZOHTableSlot *slot = robin_hood_lookup(key, key_size, table, &idx);

    if(!slot){
        return;
    }

    size_t m = table->m;
    LZOHTableAllocator *allocator = table->allocator;

    char kcpy = slot->kcpy;
    char vcpy = slot->vcpy;
    void *slot_key = slot->key;
    void *slot_value = slot->value;

    void *external_key = kcpy ? NULL : slot_key;
    void *external_value = vcpy ? NULL : slot_value;

    if(clean_up_helper){
        clean_up_helper(external_key, external_value, (void *)extra);
    }

    if(kcpy){
        MEMORY_DEALLOC(slot_key, char, slot->key_size, allocator);
    }

    if(vcpy){
        MEMORY_DEALLOC(slot_value, char, slot->value_size, allocator);
    }

    memset(slot, 0, SLOT_SIZE);

    size_t i = (idx + 1) & (m - 1);

    while(1){
        LZOHTableSlot *current_slot = &table->slots[i];

        if(current_slot->used){
            if(current_slot->probe == 0){
                break;
            }

            LZOHTableSlot *previous_slot = &table->slots[(i - 1) & (m - 1)];

            *previous_slot = *current_slot;
            previous_slot->probe--;

            memset(current_slot, 0, SLOT_SIZE);
        }else{
            break;
        }

        i = (i + 1) & (m - 1);
    }

    table->n--;
}