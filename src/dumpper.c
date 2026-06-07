#include "dumpper.h"

#include "essentials/memory.h"

#include "types.h"

#include "vm/opcode.h"
#include "vm/closure.h"

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>

static int16_t compose_i16(uint8_t *bytes);
static int32_t compose_i32(uint8_t *bytes);

static ModuleContext *current_context(Dumpper *dumpper);
static Fn *current_fn(Dumpper *dumpper);
static DynArr *current_iconsts(Dumpper *dumpper);
static DynArr *current_fconsts(Dumpper *dumpper);
static DynArr *current_strings(Dumpper *dumpper);
static DynArr *current_chunks(Dumpper *dumpper);

static int is_at_end(Dumpper *dumpper);
static uint8_t advance(Dumpper *dumpper);
static int16_t read_i16(Dumpper *dumpper);
static int32_t read_i32(Dumpper *dumpper);
static int64_t read_i64_const(Dumpper *dumpper);
static double read_float_const(Dumpper *dumpper);
static char *read_str(Dumpper *dumpper, size_t *out_len);
static void dump_chunk(
    Dumpper *dumpper,
    int offset_len,
    int bytes_len,
    int opcode_len,
    size_t *last_ip,
    size_t *current_ip,
    uint8_t chunk
);
static void dump_procedure(Dumpper *dumpper, Fn *function);
static void dump_context(Dumpper *dumpper, ModuleContext *context);

inline int16_t compose_i16(uint8_t *bytes){
    return (int16_t)((uint16_t)bytes[0] << 8) | ((uint16_t)bytes[1]);
}

inline int32_t compose_i32(uint8_t *bytes){
    return (int32_t)((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | ((uint32_t)bytes[3]);
}

inline ModuleContext *current_context(Dumpper *dumpper){
    return dumpper->current_context;
}

inline Fn *current_fn(Dumpper *dumpper){
    return dumpper->current_fn;
}

inline DynArr *current_fconsts(Dumpper *dumpper){
    return current_fn(dumpper)->fconsts;
}

inline DynArr *current_iconsts(Dumpper *dumpper){
    return current_fn(dumpper)->iconsts;
}

inline DynArr *current_strings(Dumpper *dumpper){
    return current_context(dumpper)->str_consts;
}

inline DynArr *current_chunks(Dumpper *dumpper){
    return current_fn(dumpper)->chunks;
}

inline int is_at_end(Dumpper *dumpper){
    DynArr *chunks = current_chunks(dumpper);

    return dumpper->ip >= dynarr_len(chunks);
}

inline uint8_t advance(Dumpper *dumpper){
    DynArr *chunks = current_chunks(dumpper);

    return DYNARR_GET_AS(chunks, uint8_t, dumpper->ip++);
}

int16_t read_i16(Dumpper *dumpper){
    uint8_t bytes[2];

	for(size_t i = 0; i < 2; i++){
        bytes[i] = advance(dumpper);
    }

	return compose_i16(bytes);
}

int32_t read_i32(Dumpper *dumpper){
	uint8_t bytes[4];

	for(size_t i = 0; i < 4; i++){
        bytes[i] = advance(dumpper);
    }

	return compose_i32(bytes);
}

int64_t read_i64_const(Dumpper *dumpper){
    DynArr *constants = current_iconsts(dumpper);
    size_t index = (size_t)read_i16(dumpper);

    return DYNARR_GET_AS(constants, int64_t, index);
}

double read_float_const(Dumpper *dumpper){
    DynArr *float_values = current_fconsts(dumpper);
    size_t idx = (size_t)read_i16(dumpper);

    return DYNARR_GET_AS(float_values, double, idx);
}

char *read_str(Dumpper *dumpper, size_t *out_len){
    DynArr *str_consts = current_strings(dumpper);
    size_t idx = (size_t)read_i16(dumpper);
    DStr str = DYNARR_GET_AS(str_consts, DStr, idx);

    if(out_len){
        *out_len = str.len;
    }

    return str.buff;
}

void dump_procedure(Dumpper *dumpper, Fn *function){
    dumpper->ip = 0;
    dumpper->current_fn = function;

    size_t opcode_len = 16;
    LZBStr *helper_str = dumpper->helper_str;

    printf("    PROCEDURE '%s'\n", function->name);
    printf("        OFFSET     OPCODE              INFORMATION\n");
    printf("        ------------------------------------------\n");

    while(!is_at_end(dumpper)){
        lzbstr_reset(helper_str);
        lzbstr_append_args(helper_str, "        %07zu    ", dumpper->ip);

        switch (advance(dumpper)){
            case OP_EMPTY:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_EMPTY");

                break;
            }case OP_FALSE:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_FALSE");

                break;
            }case OP_TRUE:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_TRUE");

                break;
            }case OP_INT:{
                int64_t value = read_i64_const(dumpper);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_INT");
                lzbstr_append_args(helper_str, "    value: %"PRId64"", value);

                break;
            }case OP_FLOAT:{
                read_float_const(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_FLOAT");

                break;
            }case OP_STRING:{
                read_str(dumpper, NULL);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_STRING");

                break;
            }case OP_START_TEMPLATE:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_START_TEMPLATE");

                break;
            }case OP_END_TEMPLATE:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_END_TEMPLATE");

                break;
            }case OP_ARRAY:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_ARRAY");

                break;
            }case OP_LIST:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_LIST");

                break;
            }case OP_DICT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_DICT");

                break;
            }case OP_RECORD:{
                read_i16(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_RECORD");

                break;
            }case OP_WRITE_TEMPLATE:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_WRITE_TEMPLATE");

                break;
            }case OP_INIT_ARRAY:{
                read_i16(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_INIT_ARRAY");

                break;
            }case OP_INIT_LIST:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_INIT_LIST");

                break;
            }case OP_INIT_DICT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_INIT_DICT");

                break;
            }case OP_INIT_RECORD:{
                read_str(dumpper, NULL);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_INIT_RECORD");

                break;
            }case OP_CONCAT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_CONCAT");

                break;
            }case OP_MULSTR:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_MULSTR");

                break;
            }case OP_ADD:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_ADD");

                break;
            }case OP_SUB:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_SUB");

                break;
            }case OP_MUL:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_MUL");

                break;
            }case OP_DIV:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_DIV");

                break;
            }case OP_MOD:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_MOD");

                break;
            }case OP_BNOT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_BNOT");

                break;
            }case OP_LSH:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_LSH");

                break;
            }case OP_RSH:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_RSH");

                break;
            }case OP_BAND:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_BAND");

                break;
            }case OP_BXOR:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_BXOR");

                break;
            }case OP_BOR:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_BOR");

                break;
            }case OP_LT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_LT");

                break;
            }case OP_GT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_GT");

                break;
            }case OP_LE:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_LE");

                break;
            }case OP_GE:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_GE");

                break;
            }case OP_EQ:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_EQ");

                break;
            }case OP_NE:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_NE");

                break;
            }case OP_OR:{
                read_i16(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_OR");

                break;
            }case OP_AND:{
                read_i16(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_AND");

                break;
            }case OP_NNOT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_NNOT");

                break;
            }case OP_NOT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_NOT");

                break;
            }case OP_LOCAL_SET:{
                advance(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_LOCAL_SET");

                break;
            }case OP_LOCAL_GET:{
                uint8_t at = advance(dumpper);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_LOCAL_GET");
                lzbstr_append_args(helper_str, "    at: %"PRIu8"", at);

                break;
            }case OP_FOREIGN_SET:{
                advance(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_OUT_SET");

                break;
            }case OP_FOREIGN_GET:{
                advance(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_OUT_GET");

                break;
            }case OP_GLOBAL_DEF:{
                read_str(dumpper, NULL);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_GLOBAL_DEF");

                break;
            }case OP_GLOBAL_ACCESS_SET:{
                read_str(dumpper, NULL);
                advance(dumpper);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_GASET");

                break;
            }case OP_GLOBAL_SET:{
                read_str(dumpper, NULL);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_GLOBAL_ACCESS_SET");

                break;
            }case OP_GLOBAL_GET:{
                char *global_symbol = read_str(dumpper, NULL);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_GLOBAL_GET");
                lzbstr_append_args(helper_str, "    '%s'", global_symbol);

                break;
            }case OP_NATIVE_GET:{
                char *symbol = read_str(dumpper, NULL);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_NATIVE_GET");
                lzbstr_append_args(helper_str, "    '%s'", symbol);

                break;
            }case OP_CLOSURE:{
                read_i32(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_SYMBOL_GET");

                break;
            }case OP_ARRAY_SET:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_ARRAY_SET");

                break;
            }case OP_RECORD_SET:{
                read_str(dumpper, NULL);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_RECORD_SET");

                break;
            }case OP_POP:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_POP");

                break;
            }case OP_OFFSET:{
                uint8_t offset = advance(dumpper);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_OFFSET");
                lzbstr_append_args(helper_str, "    value: %"PRId8"", offset);

                break;
            } case OP_JMP:{
                int16_t offset = read_i16(dumpper);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_JMP");
                lzbstr_append_args(helper_str, "    offset: %"PRId16" | to: %zu", offset, dumpper->ip + offset);

                break;
            }case OP_JIF:{
                int16_t offset = read_i16(dumpper);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_JIF");
                lzbstr_append_args(helper_str, "    offset: %"PRId16" | to: %zu", offset, dumpper->ip + offset);

                break;
            }case OP_JIT:{
                read_i16(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_JIT");

                break;
            }case OP_IMPORT:{
                char *target = read_str(dumpper, NULL);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_IMPORT");
                lzbstr_append_args(helper_str, "    '%s'", target);

                break;
            }case OP_CALL:{
                uint8_t args_count = advance(dumpper);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_CALL");
                lzbstr_append_args(helper_str, "    arguments: %"PRId8"", args_count);

                break;
            }case OP_ACCESS:{
                char *target = read_str(dumpper, NULL);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_ACCESS");
                lzbstr_append_args(helper_str, "    '%s'", target);

                break;
            }case OP_INDEX:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_INDEX");

                break;
            }case OP_EXIT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_EXIT");

                break;
            }case OP_RET:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_RET");

                break;
            }case OP_IS:{
                advance(dumpper);
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_IS");

                break;
            }case OP_TRY_IN:{
                size_t offset = (size_t)read_i16(dumpper);

                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_TRY_IN");
                lzbstr_append_args(helper_str, "    offset: %zu | to: %zu", offset, dumpper->ip + offset);

                break;
            }case OP_TRY_OUT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_TRY_OUT");

                break;
            }case OP_THROW:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_THROW");

                break;
            }case OP_HALT:{
                lzbstr_append_args(helper_str, "%-*s", opcode_len, "OP_HALT");

                break;
            }default:{
                assert("Illegal opcode\n");

                break;
            }
        }

        printf("%s\n", lzbstr_value(helper_str));
    }
}

void dump_context(Dumpper *dumpper, ModuleContext *context){
    dumpper->current_context = context;

    DynArr *fns = context->fns;
    size_t fns_len = dynarr_len(fns);

    printf("MODULE '%s'\n", context->pathname);

    for (size_t i = 0; i < fns_len; i++){
        Fn *fn = DYNARR_GET_PTR_AS(fns, Fn, i);

        dump_procedure(dumpper, fn);

        if(i + 1 < fns_len){
            printf("\n");
        }
    }

    dumpper->current_context = NULL;
}

Dumpper *dumpper_create(const Allocator *allocator){
    LZBStr *helper_str = MEMORY_LZBSTR(allocator);
	Dumpper *dumpper = MEMORY_ALLOC(allocator, Dumpper, 1);

    if(!helper_str || !dumpper || lzbstr_grow_by(helper_str, 1024)){
        lzbstr_destroy(helper_str);
        MEMORY_DEALLOC(allocator, Dumpper, 1, dumpper);

        return NULL;
    }

    *dumpper = (Dumpper){
        .helper_str = helper_str,
        .allocator = allocator
    };

	return dumpper;
}

void dumpper_dump(LZOHTable *contexts, Module *main_module, Dumpper *dumpper){
    dumpper->contexts = contexts;
    dumpper->main_context = main_module->context;

    dump_context(dumpper, main_module->context);

    size_t itms_count = contexts->n;
    size_t slots_count = contexts->m;

    if(itms_count > 0){
        printf("\n");
    }

    for (size_t itms_counter = 0, slot_idx = 0; itms_counter < itms_count && slot_idx < slots_count; slot_idx++){
        LZOHTableSlot slot = contexts->slots[slot_idx];

        if(!slot.used){
            continue;
        }

        ModuleContext *context = (ModuleContext *)slot.value;

        dump_context(dumpper, context);

        if(itms_counter + 1 < itms_count){
            printf("\n");
        }
    }
}
