#include "vm.h"
#include "module.h"
#include "native/native.h"
#include "native/native_nbarray.h"
#include "obj.h"
#include "vmu.h"
#include "opcode.h"
#include "essentials/memory.h"
#include "value.h"
#include "utils.h"
#include "types_utils.h"

#include "native_module/native_module_str.h"
#include "native_module/native_module_array.h"
#include "native_module/native_module_list.h"
#include "native_module/native_module_dict.h"

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <setjmp.h>

void *vm_alloc(size_t size, void * ctx){
    VM *vm = (VM *)ctx;
    Allocator *allocator = vm->allocator;
    void *real_ctx = allocator->ctx;
    size_t new_allocated_bytes = vm->allocated_bytes + size;

    if(new_allocated_bytes >= vm->allocation_limit_size){
        size_t bytes_before = vm->allocated_bytes;

        vmu_gc(vm);

        size_t bytes_after = vm->allocated_bytes;
        size_t freeded_bytes = bytes_after - bytes_before;

        if(freeded_bytes < size){
            vm->allocation_limit_size *= 2;
        }
    }

    void *ptr = allocator->alloc(size, real_ctx);

    if(!ptr){
        vmu_error(vm, "Failed to allocated %zu bytes: out of memory", size);
    }

    vm->allocated_bytes = new_allocated_bytes;

    return ptr;
}

void *vm_realloc(void *ptr, size_t old_size, size_t new_size, void *ctx){
    VM *vm = (VM *)ctx;
    Allocator *allocator = vm->allocator;
    void *real_ctx = allocator->ctx;
    ssize_t size = (ssize_t)new_size - (ssize_t)old_size;
    size_t new_allocated_bytes = vm->allocated_bytes + size;

    if(new_allocated_bytes > vm->allocation_limit_size){
        size_t bytes_before = vm->allocated_bytes;

        vmu_gc(vm);

        size_t bytes_after = vm->allocated_bytes;
        size_t freeded_bytes = bytes_after - bytes_before;

        if(freeded_bytes < (size_t)size){
            vm->allocation_limit_size *= 2;
        }
    }else if(new_allocated_bytes < vm->allocation_limit_size / 2){
        vm->allocation_limit_size /= 2;
    }

    void *new_ptr = allocator->realloc(ptr, old_size, new_size, real_ctx);

    if(!new_ptr){
        vmu_error(vm, "Failed to allocated %zu bytes: out of memory", new_size - old_size);
    }

    vm->allocated_bytes = new_allocated_bytes;

    return new_ptr;
}

void vm_dealloc(void *ptr, size_t size, void *ctx){
    VM *vm = (VM *)ctx;
    Allocator *allocator = vm->allocator;
    void *real_ctx = allocator->ctx;
    size_t new_allocated_bytes = vm->allocated_bytes - size;

    if(new_allocated_bytes < vm->allocation_limit_size / 2){
        vm->allocation_limit_size /= 2;
    }

    vm->allocated_bytes = new_allocated_bytes;

    allocator->dealloc(ptr, size, real_ctx);
}

//> PRIVATE INTERFACE
// UTILS FUNCTIONS
static int16_t compose_i16(uint8_t *bytes);
static int32_t compose_i32(uint8_t *bytes);
static inline int16_t read_i16(VM *vm);
static inline int32_t read_i32(VM *vm);
static inline int64_t read_i64_const(VM *vm);
static double read_float_const(VM *vm);
static inline char *read_str(VM *vm, size_t *out_len);
static inline void *get_symbol(size_t index, SubModuleSymbolType type, Module *module, VM *vm);
//----------     STACK RELATED FUNCTIONS     ----------//
static inline Value peek(VM *vm);
static inline Value peek_at(uint16_t offset, VM *vm);
static inline Value *peek_at_ptr(uint16_t offset, VM *vm);
static inline void push(Value value, VM *vm);
#define PUSH_EMPTY(_vm) (push(EMPTY_VALUE, (_vm)))
#define PUSH_BOOL(_value, _vm) (push(BOOL_VALUE(_value), (_vm)))
#define PUSH_INT(_value, _vm) (push(INT_VALUE(_value), (_vm)))
#define PUSH_FLOAT(_value, _vm) (push(FLOAT_VALUE(_value), (_vm)))
#define PUSH_OBJ(_obj, _vm)(push(OBJ_VALUE((Obj *)(_obj)), (_vm)))
static inline FnObj *push_fn(Fn *fn, VM *vm);
static ClosureObj *init_closure(MetaClosure *meta, VM *vm);
static Obj *push_native_module(NativeModule *native_module, VM *vm);
static Obj *push_module(Module *module, VM *vm);
static Value pop(VM *vm);
//----------     FRAMES RELATED FUNCTIONS     ----------//
static inline Frame *current_frame(VM *vm);
#define VM_CURRENT_FN(_vm)(current_frame(vm)->fn)
#define VM_CURRENT_ICONSTS(_vm)(VM_CURRENT_FN(_vm)->iconsts)
#define VM_CURRENT_FCONSTS(_vm)(VM_CURRENT_FN(_vm)->fconsts)
#define VM_CURRENT_MODULE(_vm)(VM_CURRENT_FN(_vm)->module)
#define VM_CURRENT_CLOSURE(_vm)(current_frame(vm)->closure)
static inline uint8_t advance(VM *vm);
static inline uint8_t advance_save(VM *vm);
static void add_out_value_to_current_frame(OutValue *value, VM *vm);
static void remove_value_from_current_frame(OutValue *value, VM *vm);
static Frame *push_frame(uint8_t argsc, VM *vm);
static inline void call_fn(uint8_t argsc, const Fn *fn, VM *vm);
static inline void call_closure(uint8_t argsc, Closure *closure, VM *vm);
static inline void pop_frame(VM *vm);
static inline Value *frame_local(uint8_t which, VM *vm);
// OTHERS
static int execute(VM *vm);
//< PRIVATE INTERFACE
//> PRIVATE IMPLEMENTATION
inline int16_t compose_i16(uint8_t *bytes){
    return (int16_t)((uint16_t)bytes[0] << 8) | ((uint16_t)bytes[1]);
}

inline int32_t compose_i32(uint8_t *bytes){
    return (int32_t)((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | ((uint32_t)bytes[3]);
}

static inline int16_t read_i16(VM *vm){
    uint8_t bytes[] = {advance(vm), advance(vm)};
	return compose_i16(bytes);
}

static inline int32_t read_i32(VM *vm){
	uint8_t bytes[4] = {
        advance(vm),
        advance(vm),
        advance(vm),
        advance(vm)
    };

	return compose_i32(bytes);
}

int64_t read_i64_const(VM *vm){
    DynArr *constants = VM_CURRENT_ICONSTS(vm);
    int16_t idx = read_i16(vm);
    return DYNARR_GET_AS(constants, int64_t, (size_t)idx);
}

double read_float_const(VM *vm){
	DynArr *float_values = VM_CURRENT_FCONSTS(vm);
	int16_t idx = read_i16(vm);
	return DYNARR_GET_AS(float_values, double, (size_t)idx);
}

static char *read_str(VM *vm, size_t *out_len){
    DynArr *static_strs = MODULE_STRINGS(VM_CURRENT_MODULE(vm));
    size_t idx = (size_t)read_i16(vm);

    if(idx >= dynarr_len(static_strs)){
        vmu_error(vm, "Illegal module static strings access index");
    }

    DStr raw_str = DYNARR_GET_AS(static_strs, DStr, idx);

    if(out_len){
        *out_len = raw_str.len;
    }

    return raw_str.buff;
}

static inline void *get_symbol(size_t index, SubModuleSymbolType type, Module *module, VM *vm){
    DynArr *symbols = MODULE_SYMBOLS(module);

    if(index >= dynarr_len(symbols)){
        vmu_error(vm, "Failed to get module symbol: index out of bounds");
    }

    SubModuleSymbol *symbol = &DYNARR_GET_AS(symbols, SubModuleSymbol, index);

    if(symbol->type != type){
        vmu_error(vm, "Failed to get module symbol: mismatch types");
    }

    return symbol->value;
}

static inline Value peek(VM *vm){
    if(vm->stack_top == vm->stack){
        vmu_error(vm, "Stack is empty");
    }

    return *(vm->stack_top - 1);
}

static inline Value peek_at(uint16_t offset, VM *vm){
    if(vm->stack == vm->stack_top){
        vmu_internal_error(vm, "Stack is empty");
    }

    if(vm->stack_top - 1 - offset <= vm->stack){
        vmu_internal_error(vm, "Illegal stack peek offset");
    }

    return *(vm->stack_top - 1 - offset);
}

static inline Value *peek_at_ptr(uint16_t offset, VM *vm){
    if(vm->stack == vm->stack_top){
        vmu_internal_error(vm, "Stack is empty");
    }

    if(vm->stack_top - 1 - offset <= vm->stack){
        vmu_internal_error(vm, "Illegal stack peek offset");
    }

    return vm->stack_top - 1 - offset;
}

static inline void push(Value value, VM *vm){
    if(vm->stack_top >= vm->stack + STACK_LENGTH){
        vmu_error(vm, "Stack over flow");
    }

    *((vm->stack_top)++) = value;
}

static inline FnObj *push_fn(Fn *fn, VM *vm){
    FnObj *fn_obj = vmu_create_fn(fn, vm);
    PUSH_OBJ(fn_obj, vm);
    return fn_obj;
}

ClosureObj *init_closure(MetaClosure *meta, VM *vm){
    ClosureObj *closure_obj = vmu_create_closure(meta, vm);
    Closure *closure = closure_obj->closure;
    OutValue *out_values = closure->out_values;
    MetaOutValue *meta_out_values = meta->meta_out_values;
    size_t out_values_len = meta->meta_out_values_len;

    for (size_t i = 0; i < out_values_len; i++){
        OutValue *out_value = &out_values[i];
        MetaOutValue *meta_out_value = &meta_out_values[i];

        out_value->linked = 1;
        out_value->at = meta_out_value->at;
        out_value->value = *frame_local(meta_out_value->at, vm);
        out_value->prev = NULL;
        out_value->next = NULL;

        add_out_value_to_current_frame(out_value, vm);
    }

    return closure_obj;
}

Obj *push_native_module(NativeModule *native_module, VM *vm){
    return NULL;
}

Obj *push_module(Module *module, VM *vm){
    return NULL;
}

inline Value pop(VM *vm){
    if(vm->stack_top == vm->stack){
        vmu_error(vm, "Stack under flow");
    }

    return *(--vm->stack_top);
}

static inline Frame *current_frame(VM *vm){
    if(vm->frame_ptr == vm->frame_stack){
        vmu_error(vm, "Frame stack is empty");
    }

    return vm->frame_ptr - 1;
}

static inline uint8_t advance(VM *vm){
    Frame *frame = current_frame(vm);
    DynArr *chunks = frame->fn->chunks;

    if(frame->ip >= dynarr_len(chunks)){
        vmu_error(vm, "IP excceded chunks length");
    }

    return DYNARR_GET_AS(chunks, uint8_t, frame->ip++);
}

static inline uint8_t advance_save(VM *vm){
    Frame *frame = current_frame(vm);
    DynArr *chunks = frame->fn->chunks;

    if(frame->ip >= dynarr_len(chunks)){
        vmu_error(vm, "IP excceded chunks length");
    }

    uint8_t chunk = DYNARR_GET_AS(chunks, uint8_t, frame->ip++);
    frame->last_offset = frame->ip - 1;

    return chunk;
}

void add_out_value_to_current_frame(OutValue *value, VM *vm){
    Frame *frame = current_frame(vm);

    if(frame->outs_tail){
        frame->outs_tail->next = value;
        value->prev = frame->outs_tail;
    }else{
        frame->outs_head = value;
    }

    frame->outs_tail = value;
}

void remove_value_from_current_frame(OutValue *value, VM *vm){
    Frame *frame = current_frame(vm);

    if(value == frame->outs_head){
        frame->outs_head = value->next;
    }
    if(value == frame->outs_tail){
        frame->outs_tail = value->prev;
    }

    if(value->prev){
        value->prev->next = value->next;
    }
    if(value->next){
        value->next->prev = value->prev;
    }
}

static Frame *push_frame(uint8_t argsc, VM *vm){
    if(vm->frame_ptr >= vm->frame_stack + FRAME_LENGTH){
        vmu_error(vm, "Frame stack is full");
    }

    // frame's locals pointer must always point to the frame's function:
    //     frame->locals == frame->fn
    Value *locals = vm->stack_top - 1 - argsc;

    if(locals < vm->stack){
        vmu_internal_error(vm, "Frame locals out of value stack");
    }

    if(!is_callable(locals)){
        vmu_internal_error(vm, "Frame locals must point to function");
    }

    Frame *frame = vm->frame_ptr++;

    frame->ip = 0;
    frame->last_offset = 0;
    frame->fn = NULL;
    frame->closure = NULL;
    frame->locals = locals;
    frame->outs_head = NULL;
    frame->outs_tail = NULL;

    return frame;
}

static inline void call_fn(uint8_t argsc, const Fn *fn, VM *vm){
    size_t params_count = fn->arity;

    if(argsc != params_count){
        vmu_error(vm, "Failed to call function '%s'. Declared with %d parameter(s), but got %d argument(s)", fn->name, params_count, argsc);
    }

    Frame *frame = push_frame(argsc, vm);

    frame->fn = fn;
}

static inline void call_closure(uint8_t argsc, Closure *closure, VM *vm){
    Fn *fn = closure->meta->fn;
    size_t params_count = fn->arity;

    if(argsc != params_count){
        vmu_error(
            vm,
            "Failed to call closure '%s'. Declared with %d parameter(s), but got %d argument(s)",
            fn->name,
            params_count,
            argsc
        );
    }

    Frame *frame = push_frame(argsc, vm);

    frame->fn = fn;
    frame->closure = closure;
}

static inline void pop_frame(VM *vm){
    if(vm->frame_ptr == vm->frame_stack){
        vmu_error(vm, "Frame stack is empty");
    }

    vm->frame_ptr--;
}

static inline Value *frame_local(uint8_t which, VM *vm){
    Frame *frame = current_frame(vm);
    Value *locals = frame->locals;
    Value *local = locals + 1 + which;

    if(local >= vm->stack_top){
        vmu_error(vm, "Index for frame local pass value stack top");
    }

    return local;
}

static int execute(VM *vm){
    for (;;){
        uint8_t chunk = advance_save(vm);

        switch (chunk){
            case OP_EMPTY:{
                PUSH_EMPTY(vm);
                break;
            }case OP_FALSE:{
                PUSH_BOOL(0, vm);
                break;
            }case OP_TRUE:{
                PUSH_BOOL(1, vm);
                break;
            }case OP_CINT:{
                int64_t i64 = (int64_t)advance(vm);
                PUSH_INT(i64, vm);
                break;
            }case OP_INT:{
                int64_t i64 = read_i64_const(vm);
                PUSH_INT(i64, vm);
                break;
            }case OP_FLOAT:{
                double value = read_float_const(vm);
                PUSH_FLOAT(value, vm);
                break;
            }case OP_STRING:{
                size_t len;
                StrObj *str_obj = NULL;
                char *str = read_str(vm, &len);

                vmu_create_str(0, len, str, vm, &str_obj);
                PUSH_OBJ(str_obj, vm);

                break;
            }case OP_STTE:{
                LZBStr *str = MEMORY_LZBSTR(vm->allocator);
                Template *template = MEMORY_ALLOC(vm->allocator, Template, 1);

                template->str = str;
                template->prev = vm->templates;
                vm->templates = template;

                break;
            }case OP_ETTE:{
                Template *template = vm->templates;

                if(template){
                    LZBStr *str = template->str;
                    size_t buff_len;
                    char *buff = lzbstr_rclone_buff(
                        (LZBStrAllocator *)&vm->front_allocator,
                        str,
                        &buff_len
                    );
                    StrObj *str_obj = NULL;

                    if(vmu_create_str(1, buff_len, buff, vm, &str_obj)){
                        MEMORY_DEALLOC(&vm->front_allocator, char, buff_len + 1, buff);
                    }

                    PUSH_OBJ(str_obj, vm);

                    vm->templates = template->prev;

                    lzbstr_destroy(str);
                    MEMORY_DEALLOC(vm->allocator, Template, 1, template);

                    break;
                }

                vmu_internal_error(vm, "Template stack is empty");

                break;
            }case OP_ARRAY:{
                Value len_value = pop(vm);

                if(!IS_VALUE_INT(len_value)){
                    vmu_error(vm, "Expect 'INT' as array length");
                }

                int64_t array_len = VALUE_TO_INT(len_value);
                ArrayObj *array_obj = vmu_create_array(array_len, vm);

                PUSH_OBJ(array_obj, vm);

                break;
            }case OP_LIST:{
                ListObj *list_obj = vmu_create_list(vm);
                PUSH_OBJ(list_obj, vm);
                break;
            }case OP_DICT:{
                DictObj *dict_obj = vmu_create_dict(vm);
                PUSH_OBJ(dict_obj, vm);
                break;
            }case OP_RECORD:{
                uint16_t len = (uint16_t)read_i16(vm);
                RecordObj *record_obj = vmu_create_record(len, vm);
                PUSH_OBJ(record_obj, vm);
                break;
            }case OP_WTTE:{
                Template *template = vm->templates;
                Value raw_value = pop(vm);

                if(template){
                    LZBStr *str = template->str;
                    vmu_value_to_str_w(raw_value, str);
                    break;
                }

                vmu_internal_error(vm, "Template stack is empty");

                break;
            }case OP_IARRAY:{
                int64_t idx = (int64_t)read_i16(vm);
                Value value = pop(vm);
                Value array_value = peek(vm);
                ArrayObj *array_obj = VALUE_TO_ARRAY(array_value);

                vmu_array_set_at(idx, value, array_obj, vm);

                break;
            }case OP_ILIST:{
                Value value = peek_at(0, vm);
                Value list_value = peek_at(1, vm);

                if(!is_value_list(list_value)){
                    vmu_internal_error(vm, "Expect value of type 'dict', but got something else");
                }

                vmu_list_insert(value, VALUE_TO_LIST(list_value), vm);
                pop(vm);

                break;
            }case OP_IDICT:{
                Value raw_value = peek_at(0, vm);
                Value key_value = peek_at(1, vm);
                Value dict_value = peek_at(2, vm);

                if(!is_value_dict(dict_value)){
                    vmu_internal_error(vm, "Expect value of type 'dict', but got something else");
                }

                vmu_dict_put(key_value, raw_value, VALUE_TO_DICT(dict_value), vm);
                pop(vm);
                pop(vm);

                break;
            }case OP_IRECORD:{
                size_t key_size;
                char *key = read_str(vm, &key_size);
                Value raw_value = peek_at(0, vm);
                Value record_value = peek_at(1, vm);

                if(!is_value_record(record_value)){
                    vmu_internal_error(vm, "Expect value of type 'record', but got something else");
                }

                vmu_record_insert_attr(key_size, key, raw_value, VALUE_TO_RECORD(record_value), vm);
                pop(vm);

                break;
            }case OP_CONCAT:{
                Value right_value = peek_at(0, vm);
                Value left_value = peek_at(1, vm);

                if(is_value_str(left_value) && is_value_str(right_value)){
                    StrObj *left_str_obj = VALUE_TO_STR(left_value);
                    StrObj *right_str_obj = VALUE_TO_STR(right_value);
                    StrObj *result_str_obj = vmu_str_concat(left_str_obj, right_str_obj, vm);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(result_str_obj, vm);

                    break;
                }

                if(is_value_array(left_value) && is_value_array(right_value)){
                    ArrayObj *left_array_obj = VALUE_TO_ARRAY(left_value);
                    ArrayObj *right_array_obj = VALUE_TO_ARRAY(right_value);
                    ArrayObj *new_array_obj = vmu_array_join(left_array_obj, right_array_obj, vm);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(new_array_obj, vm);

                    break;
                }

                if(is_value_list(left_value) && is_value_list(right_value)){
                    ListObj *left_list_obj = VALUE_TO_LIST(left_value);
                    ListObj *right_list_obj = VALUE_TO_LIST(right_value);
                    ListObj *new_list_obj = vmu_list_join(left_list_obj, right_list_obj, vm);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(new_list_obj, vm);

                    break;
                }

                if(is_value_array(left_value) || is_value_array(right_value)){
                    ArrayObj *array_obj = NULL;
                    Value raw_value = {0};

                    if(is_value_array(left_value)){
                        array_obj = VALUE_TO_ARRAY(left_value);
                        raw_value = right_value;
                    }else{
                        array_obj = VALUE_TO_ARRAY(right_value);
                        raw_value = left_value;
                    }

                    ArrayObj *new_array_obj = vmu_array_join_value(raw_value, array_obj, vm);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(new_array_obj, vm);

                    break;
                }

                if(is_value_list(left_value) || is_value_list(right_value)){
                    ListObj *list_obj = NULL;
                    Value raw_value = {0};

                    if(is_value_list(left_value)){
                        list_obj = VALUE_TO_LIST(left_value);
                        raw_value = right_value;
                    }else{
                        list_obj = VALUE_TO_LIST(right_value);
                        raw_value = left_value;
                    }

                    ListObj *new_list_obj = vmu_list_insert_new(raw_value, list_obj, vm);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(new_list_obj, vm);

                    break;
                }

                vmu_error(vm, "Illegal operands for concatenation");

                break;
            }case OP_MULSTR:{
                Value right_value = peek_at(0, vm);
                Value left_value = peek_at(1, vm);

                if(IS_VALUE_INT(left_value) && is_value_str(right_value)){
                    int64_t by = VALUE_TO_INT(left_value);
                    StrObj *str_obj = VALUE_TO_STR(right_value);
                    StrObj *new_str_obj = vmu_str_mul(by, str_obj, vm);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(new_str_obj, vm);

                    break;
                }

                if(is_value_str(left_value) && IS_VALUE_INT(right_value)){
                    int64_t by = VALUE_TO_INT(right_value);
                    StrObj *str_obj = VALUE_TO_STR(left_value);
                    StrObj *new_str_obj = vmu_str_mul(by, str_obj, vm);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(new_str_obj, vm);

                    break;
                }

                vmu_error(vm, "Illegal operands for string multiplication");

                break;
            }case OP_ADD:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    push(INT_VALUE(left + right), vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    push(FLOAT_VALUE(left + right), vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    push(FLOAT_VALUE(left + right), vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using + operator");

                break;
            }case OP_SUB:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    push(INT_VALUE(left - right), vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    push(FLOAT_VALUE(left - right), vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    push(FLOAT_VALUE(left - right), vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using - operator");

                break;
            }case OP_MUL:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    push(INT_VALUE(left * right), vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    push(FLOAT_VALUE(left * right), vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    push(FLOAT_VALUE(left * right), vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using * operator");

                break;
            }case OP_DIV:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    if(right == 0){
                        vmu_error(vm, "Division by zero is undefined");
                    }

                    push(INT_VALUE(left / right), vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    push(FLOAT_VALUE(left / right), vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    if(right == 0.0){
                        vmu_error(vm, "Division by zero is undefined");
                    }

                    push(FLOAT_VALUE(left / right), vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using / operator");

                break;
            }case OP_MOD:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    push(INT_VALUE(left % right), vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using 'mod' operator");

                break;
            }case OP_BNOT:{
                Value value = pop(vm);

                if(IS_VALUE_INT(value)){
                    PUSH_INT(~VALUE_TO_INT(value), vm);
                    break;
                }

                vmu_error(vm, "Unsuported types using '~' operator");

                break;
            }case OP_LSH:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    uint64_t left = (uint64_t)VALUE_TO_INT(left_value);
                    uint64_t right = (uint64_t)VALUE_TO_INT(right_value);

                    PUSH_INT(left << right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using '<<' operator");

                break;
            }case OP_RSH:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    uint64_t left = (uint64_t)VALUE_TO_INT(left_value);
                    uint64_t right = (uint64_t)VALUE_TO_INT(right_value);

                    PUSH_INT(left >> right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using '>>' operator");

                break;
            }case OP_BAND:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_INT(left & right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using '&' operator");

                break;
            }case OP_BXOR:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_INT(left ^ right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using '^' operator");

                break;
            }case OP_BOR:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_INT(left | right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using '|' operator");

                break;
            }case OP_LT:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(left < right, vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(left < right, vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    PUSH_BOOL(left < right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using < operator");

                break;
            }case OP_GT:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(left > right, vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(left > right, vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    PUSH_BOOL(left > right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using > operator");

                break;
            }case OP_LE:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(left <= right, vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(left <= right, vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    PUSH_BOOL(left <= right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using <= operator");

                break;
            }case OP_GE:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(left >= right, vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(left >= right, vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    PUSH_BOOL(left >= right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using >= operator");

                break;
            }case OP_EQ:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_BOOL(left_value) && IS_VALUE_BOOL(right_value)){
                    uint8_t left = VALUE_TO_BOOL(left_value);
                    uint8_t right = VALUE_TO_BOOL(right_value);

                    PUSH_BOOL(left == right, vm);

                    break;
                }

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(left == right, vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(left == right, vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    PUSH_BOOL(left == right, vm);

                    break;
                }

                if(is_value_str(left_value) && is_value_str(right_value)){
                    StrObj *left = VALUE_TO_STR(left_value);
                    StrObj *right = VALUE_TO_STR(right_value);

                    PUSH_BOOL(left == right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using == operator");

                break;
            }case OP_NE:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_BOOL(left_value) && IS_VALUE_BOOL(right_value)){
                    uint8_t left = VALUE_TO_BOOL(left_value);
                    uint8_t right = VALUE_TO_BOOL(right_value);

                    PUSH_BOOL(left != right, vm);

                    break;
                }

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(left != right, vm);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(left != right, vm);

                    break;
                }

                if((IS_VALUE_INT(left_value) || IS_VALUE_FLOAT(left_value)) &&
                   (IS_VALUE_INT(right_value) || IS_VALUE_FLOAT(right_value)))
                {
                    double left;
                    double right;

                    if(IS_VALUE_FLOAT(left_value)){
                        left = VALUE_TO_FLOAT(left_value);
                        right = (double)VALUE_TO_INT(right_value);
                    }else{
                        left = (double)VALUE_TO_INT(left_value);
                        right = VALUE_TO_FLOAT(right_value);
                    }

                    PUSH_BOOL(left != right, vm);

                    break;
                }

                if(is_value_str(left_value) && is_value_str(right_value)){
                    StrObj *left = VALUE_TO_STR(left_value);
                    StrObj *right = VALUE_TO_STR(right_value);

                    PUSH_BOOL(left != right, vm);

                    break;
                }

                vmu_error(vm, "Unsuported types using != operator");

                break;
            }case OP_OR:{
                int16_t jmp_value = read_i16(vm);
                Value value = peek(vm);

                if(!IS_VALUE_BOOL(value)){
                    vmu_error(vm, "Unsupported types using 'or' operator");
                }

                if(VALUE_TO_BOOL(value)){
                    current_frame(vm)->ip += jmp_value;
                    break;
                }

                pop(vm);

                break;
            }case OP_AND:{
                int16_t jmp_value = read_i16(vm);
                Value value = peek(vm);

                if(!IS_VALUE_BOOL(value)){
                    vmu_error(vm, "Unsupported types using 'and' operator");
                }

                if(!VALUE_TO_BOOL(value)){
                    current_frame(vm)->ip += jmp_value;
                    break;
                }

                pop(vm);

                break;
            }case OP_NOT:{
                Value value = pop(vm);

                if(!IS_VALUE_BOOL(value)){
                    vmu_error(vm, "Expect boolean at right side");
                }

                PUSH_BOOL(!VALUE_TO_BOOL(value), vm);

                break;
            }case OP_NNOT:{
                Value value = pop(vm);

                if(IS_VALUE_INT(value)){
                    PUSH_INT(-VALUE_TO_INT(value), vm);
                    break;
                }

                if(IS_VALUE_FLOAT(value)){
                    PUSH_FLOAT(-VALUE_TO_FLOAT(value), vm);
                    break;
                }

                vmu_error(vm, "Expect integer or float at right side");

                break;
            }case OP_LSET:{
                Value value = peek(vm);
                uint8_t index = advance(vm);

                *frame_local(index, vm) = value;

                break;
            }case OP_LGET:{
                uint8_t index = advance(vm);
                Value value = *frame_local(index, vm);

                push(value, vm);

                break;
            }case OP_OSET:{
                uint8_t index = advance(vm);
                Value value = peek(vm);
                Closure *closure = VM_CURRENT_CLOSURE(vm);
                OutValue *out_values = closure->out_values;
                MetaClosure *meta = closure->meta;
                size_t meta_out_values_len = meta->meta_out_values_len;

                for (size_t i = 0; i < meta_out_values_len; i++){
                    OutValue *closure_value = &out_values[i];

                    if(closure_value->at == index){
                        closure_value->value = value;
                        break;
                    }
                }

                pop(vm);

                break;
            }case OP_OGET:{
                uint8_t index = advance(vm);
                Closure *closure = VM_CURRENT_CLOSURE(vm);
                OutValue *out_values = closure->out_values;
                MetaClosure *meta = closure->meta;
                size_t meta_out_values_len = meta->meta_out_values_len;

                for (size_t i = 0; i < meta_out_values_len; i++){
                    OutValue *out_value = &out_values[i];

                    if(out_value->at == index){
                        push(out_value->value, vm);
                        break;
                    }
                }

                break;
            }case OP_GDEF:{
                size_t key_size;
                char *key = read_str(vm, &key_size);
                Value value = pop(vm);
                LZOHTable *globals = MODULE_GLOBALS(VM_CURRENT_FN(vm)->module);

                if(lzohtable_lookup(key_size, key, globals, NULL)){
                    vmu_error(vm, "Cannot define global '%s': already exists", key);
                }

                GlobalValue global_value = {0};

                global_value.access = PRIVATE_GLOVAL_VALUE_TYPE;
                global_value.value = value;

                lzohtable_put_ckv(key_size, key, sizeof(GlobalValue), &global_value, globals, NULL);

                break;
            }case OP_GASET:{
                Module *module = VM_CURRENT_MODULE(vm);
                LZOHTable *globals = MODULE_GLOBALS(module);

                char *key = read_str(vm, NULL);
                size_t key_size = strlen(key);

                GlobalValue *global_value = NULL;

                if(!lzohtable_lookup(key_size, key, globals, (void **)(&global_value))){
                    vmu_error(vm, "Global symbol '%s' does not exists", key);
                    break;
                }

                Value value = global_value->value;
                uint8_t access_type = advance(vm);

                if(is_value_native_module(value) || is_value_module(value)){
                    vmu_error(vm, "Modules cannot modify its access");
                }

                if(access_type == 0){
                    global_value->access = PRIVATE_GLOVAL_VALUE_TYPE;
                }else if(access_type == 1){
                    global_value->access = PUBLIC_GLOBAL_VALUE_TYPE;
                }else{
                    vmu_error(vm, "Illegal access type: %d", access_type);
                }

                break;
            }case OP_GSET:{
                size_t key_size;
                char *key = read_str(vm, &key_size);
                Value value = peek(vm);
                GlobalValue *global_value = NULL;
                LZOHTable *globals = MODULE_GLOBALS(VM_CURRENT_FN(vm)->module);

                if(lzohtable_lookup(key_size, key, globals, (void **)(&global_value))){
                    global_value->value = value;
                    break;
                }

                vmu_error(vm, "Global '%s' does not exists", key);

                break;
            }case OP_GGET:{
                size_t key_size;
                char *key = read_str(vm, &key_size);
                GlobalValue *global_value = NULL;

                if(!lzohtable_lookup(
                	key_size,
                 	key,
                  	MODULE_GLOBALS(VM_CURRENT_FN(vm)->module),
                   	(void **)(&global_value)
                )){
                    vmu_error(
                    	vm,
                     	"Global symbol '%s' does not exists",
                      	key
                    );
                }

                Value value = global_value->value;

                if(is_value_module(value)){
               		ModuleObj *module_obj = OBJ_TO_MODULE(VALUE_TO_OBJ(value));
                	Module *module = module_obj->module;

               		if(!module->submodule->resolved){
	                    Frame *frame = current_frame(vm);

	                    frame->ip = frame->last_offset;
	                    module->prev = vm->modules_stack;
	                    vm->modules_stack_len++;
	                    vm->modules_stack = module;

	                    longjmp(vm->exit_jmp, 3);
	                }
                }

                push(value, vm);

                break;
            }case OP_NGET:{
                size_t key_size;
                char *key = read_str(vm, &key_size);
                Value *out_value = NULL;

                if(lzohtable_lookup(key_size, key, vm->native_fns, (void **)(&out_value))){
                    push(*out_value, vm);
                    break;
                }

                vmu_internal_error(vm, "Unknown native symbol '%s'", key);

                break;
            }case OP_SGET:{
                size_t index = (size_t)read_i32(vm);
                Module *module = VM_CURRENT_MODULE(vm);
                DynArr *symbols = MODULE_SYMBOLS(module);
                size_t symbols_len = dynarr_len(symbols);

                if(index >= symbols_len){
                    vmu_error(
                    	vm,
                     	"Failed to get module symbol: index (%zu) out of bounds",
                      	index
                    );
                }

                SubModuleSymbol symbol = DYNARR_GET_AS(symbols, SubModuleSymbol, index);

                switch (symbol.type){
                    case FUNCTION_SUBMODULE_SYM_TYPE:{
                        Fn *fn = (Fn *)symbol.value;
                        FnObj *fn_obj = vmu_create_fn(fn, vm);

                        PUSH_OBJ(fn_obj, vm);

                        break;
                    }case CLOSURE_SUBMODULE_SYM_TYPE:{
                        MetaClosure *meta_closure = (MetaClosure *)symbol.value;
                        ClosureObj *closure_obj = init_closure(meta_closure, vm);

                        PUSH_OBJ(closure_obj, vm);

                        break;
                    }case NATIVE_MODULE_SUBMODULE_SYM_TYPE:{
                        NativeModule *native_module = (NativeModule *)symbol.value;
                        NativeModuleObj *native_module_obj = vmu_create_native_module(native_module, vm);

                        PUSH_OBJ(native_module_obj, vm);

                        break;
                    }case MODULE_SUBMODULE_SYM_TYPE:{
                        Module *module = (Module *)symbol.value;
                        ModuleObj *module_obj = vmu_create_module_obj(module, vm);

                        PUSH_OBJ(module_obj, vm);

                        if(!module->submodule->resolved){
                            module->prev = vm->modules_stack;
                            vm->modules_stack_len++;
                            vm->modules_stack = module;

                            longjmp(vm->exit_jmp, 3);
                        }

                        break;
                    }default:{
                        vmu_internal_error(vm, "Unknown submodule symbol type");
                    }
                }

                break;
            }case OP_ASET:{
                Value indexable_value = peek_at(0, vm);
                Value idx_value = peek_at(1, vm);
                Value value = peek_at(2, vm);

                if(!IS_VALUE_OBJ(indexable_value)){
                    vmu_error(
                    	vm,
                     	"Illegal assignment target, expect: array, list, dict, nbarray"
                    );
                }

                Obj *target_obj = VALUE_TO_OBJ(indexable_value);

                switch (target_obj->type){
                    case ARRAY_OBJ_TYPE:{
                        if(!IS_VALUE_INT(idx_value)){
                            vmu_error(vm, "Expect index value of type 'int'");
                        }

                        int64_t idx = VALUE_TO_INT(idx_value);
                        ArrayObj *array_obj = VALUE_TO_ARRAY(indexable_value);
                        vmu_array_set_at(idx, value, array_obj, vm);

                        break;
                    }case LIST_OBJ_TYPE:{
                        if(!IS_VALUE_INT(idx_value)){
                            vmu_error(vm, "Expect index value of type 'int'");
                        }

                        int64_t idx = VALUE_TO_INT(idx_value);
                        ListObj *list_obj = VALUE_TO_LIST(indexable_value);
                        vmu_list_set_at(idx, value, list_obj, vm);

                        break;
                    }case DICT_OBJ_TYPE:{
                        DictObj *dict_obj = VALUE_TO_DICT(indexable_value);
                        vmu_dict_put(idx_value, value, dict_obj, vm);

                        break;
                    }case NATIVE_OBJ_TYPE:{
                   		NativeObj *native_obj = OBJ_TO_NATIVE(target_obj);
                    	NativeHeader *native_header = native_obj->native;

                    	switch (native_header->type) {
	                     	case NBARRAY_NATIVE_TYPE:{
								if(!IS_VALUE_INT(idx_value)){
                            		vmu_error(vm, "Expect index value of type 'int'");
                        		}

								if(!IS_VALUE_INT(value)){
									vmu_error(vm, "Expect assignment value of type 'int'");
								}

								NBArrayNative *nbarray_native = (NBArrayNative *)native_header;
                        		int64_t idx = VALUE_TO_INT(idx_value);
                        		int64_t assing_value = VALUE_TO_INT(value);

                          		if(idx < 0 || (size_t)idx >= nbarray_native->len){
                            		vmu_error(vm, "Index out of bounds");
                            	}

                           		nbarray_native->bytes[(size_t)idx] = (unsigned char)assing_value;

								break;
							}default:{
								vmu_error(vm, "Illegal assignment target");
								break;
							}
	                    }

                        break;
                    }default:{
                        vmu_error(vm, "Illegal assignment target");
                    }
                }

                pop(vm);
                pop(vm);

                break;
            }case OP_RSET:{
                size_t key_size;
                char *key = read_str(vm, &key_size);
                Value target_value = pop(vm);
                Value raw_value = peek(vm);

                if(!is_value_record(target_value)){
                    vmu_error(vm, "Expect record in assignment");
                }

                RecordObj *record_obj = VALUE_TO_RECORD(target_value);

                vmu_record_set_attr(key_size, key, raw_value, record_obj, vm);

                break;
            }case OP_POP:{
                pop(vm);
                break;
            }case OP_JMP:{
                int16_t jmp_value = read_i16(vm);
                current_frame(vm)->ip += jmp_value;
                break;
            }case OP_JIF:{
                int16_t jmp_value = read_i16(vm);
                Value value = pop(vm);

                if(!IS_VALUE_BOOL(value)){
                    vmu_error(vm, "Expect boolean as conditional value");
                }

                current_frame(vm)->ip += VALUE_TO_BOOL(value) ? 0 : jmp_value;

                break;
            }case OP_JIT:{
                int16_t jmp_value = read_i16(vm);
                Value value = pop(vm);

                if(!IS_VALUE_BOOL(value)){
                    vmu_error(vm, "Expect boolean as conditional value");
                }

                current_frame(vm)->ip += VALUE_TO_BOOL(value) ? jmp_value : 0;

                break;
            }case OP_CALL:{
                uint8_t args_count = advance(vm);
                Value callable_value = peek_at(args_count, vm);

                if(!IS_VALUE_OBJ(callable_value)){
                    vmu_error(vm, "Target is not callable");
                }

                Obj *callable_obj = VALUE_TO_OBJ(callable_value);

                switch (callable_obj->type){
                    case NATIVE_FN_OBJ_TYPE:{
                        NativeFnObj *native_fn_obj = VALUE_TO_NATIVE_FN(callable_value);
                        NativeFn *native_fn = native_fn_obj->native_fn;
                        Value target = native_fn_obj->target;
                        RawNativeFn raw_fn = native_fn->raw_fn;

                        if(native_fn->arity != args_count){
                            vmu_error(
                                vm,
                                "Failed to call native function '%s'. Declared with %d parameter(s), but got %d argument(s)",
                                native_fn->name,
                                native_fn->arity,
                                args_count
                            );
                        }

                        Value return_value;

                        if(args_count > 0){
                            Value args[args_count];

                            for (int16_t i = args_count; i > 0; i--){
                                args[i - 1] = peek_at(args_count - i, vm);
                            }

                            return_value = raw_fn(args_count, args, target, vm);
                        }else{
                            return_value = raw_fn(0, NULL, target, vm);
                        }

                        vm->stack_top = peek_at_ptr(args_count, vm);

                        push(return_value, vm);

                        break;
                    }case FN_OBJ_TYPE:{
                        FnObj *fn_obj = VALUE_TO_FN(callable_value);
                        const Fn *fn = fn_obj->fn;

                        call_fn(args_count, fn, vm);

                        break;
                    }case CLOSURE_OBJ_TYPE:{
                        ClosureObj *closure_obj = VALUE_TO_CLOSURE(callable_value);
                        Closure *closure = closure_obj->closure;

                        call_closure(args_count, closure, vm);

                        break;
                    }default:{
                        vmu_error(vm, "Target is not callable");
                        break;
                    }
                }

                break;
            }case OP_ACCESS:{
                Value target_value = peek(vm);

                if(!IS_VALUE_OBJ(target_value)){
                    vmu_error(vm, "Expect object as target of access");
                }

                size_t key_size;
                char *key = read_str(vm, &key_size);
                Obj *target_obj = VALUE_TO_OBJ(target_value);

                switch (target_obj->type){
                    case STR_OBJ_TYPE:{
                        NativeFn *native_fn = native_str_get(key_size, key, vm);

                        if(native_fn){
                            NativeFnObj *native_fn_obj = vmu_create_native_fn(target_value, native_fn, vm);

                            pop(vm);
                            PUSH_OBJ(native_fn_obj, vm);

                            break;
                        }

                        vmu_error(vm, "Target does not contain symbol '%s'", key);

                        break;
                    }case ARRAY_OBJ_TYPE:{
                        NativeFn *native_fn = native_array_get(key_size, key, vm);

                        if(native_fn){
                            NativeFnObj *native_fn_obj = vmu_create_native_fn(target_value, native_fn, vm);

                            pop(vm);
                            PUSH_OBJ(native_fn_obj, vm);

                            break;
                        }

                        vmu_error(vm, "Target does not contain symbol '%s'", key);

                        break;
                    }case LIST_OBJ_TYPE:{
                        NativeFn *native_fn = native_list_get(key_size, key, vm);

                        if(native_fn){
                            NativeFnObj *native_fn_obj = vmu_create_native_fn(target_value, native_fn, vm);

                            pop(vm);
                            PUSH_OBJ(native_fn_obj, vm);

                            break;
                        }

                        vmu_error(vm, "Target does not contain symbol '%s'", key);

                        break;
                    }case DICT_OBJ_TYPE:{
                        NativeFn *native_fn = native_dict_get(key_size, key, vm);

                        if(native_fn){
                            NativeFnObj *native_fn_obj = vmu_create_native_fn(target_value, native_fn, vm);

                            pop(vm);
                            PUSH_OBJ(native_fn_obj, vm);

                            break;
                        }

                        vmu_error(vm, "Target does not contain symbol '%s'", key);

                        break;
                    }case RECORD_OBJ_TYPE:{
                        RecordObj *record_obj = OBJ_TO_RECORD(target_obj);
                        Value out_value = vmu_record_get_attr(key_size, key, record_obj, vm);

                        pop(vm);
                        push(out_value, vm);

                        break;
                    }case NATIVE_MODULE_OBJ_TYPE:{
                        NativeModuleObj *native_module_obj = OBJ_TO_NATIVE_MODULE(target_obj);
                        NativeModule *native_module = native_module_obj->native_module;
                        Value *value = NULL;

                        if(!lzohtable_lookup(key_size, key, native_module->symbols, (void **)(&value))){
                            vmu_error(vm, "Native module '%s' does not contain symbol '%s'", native_module->name, key);
                        }

                        pop(vm);
                        push(*value, vm);

                        break;
                    }case MODULE_OBJ_TYPE:{
                        ModuleObj *module_obj = OBJ_TO_MODULE(target_obj);
                        Module *module = module_obj->module;
                        GlobalValue *global_value = NULL;

                        if(!lzohtable_lookup(
                            key_size,
                            key,
                            module->submodule->globals,
                            (void **)(&global_value)
                        )){
                            vmu_error(
                                vm,
                                "Module '%s' do not have '%s' symbol",
                                module->name,
                                key
                            );
                        }

                        if(global_value->access == PRIVATE_GLOVAL_VALUE_TYPE){
                            vmu_error(
                                vm,
                                "Symbol '%s' in module '%s' is private",
                                key,
                                module->name
                            );
                        }

                        pop(vm);
                        push(global_value->value, vm);

                        break;
                    }default:{
                        vmu_error(vm, "Illegal access target");
                        break;
                    }
                }

                break;
            }case OP_INDEX:{
                Value target_value = peek_at(0, vm);
                Value idx_value = peek_at(1, vm);
                Value out_value = {0};

                if(!IS_VALUE_OBJ(target_value)){
                	vmu_error(vm, "Expect object");
                }

                Obj *target_obj = VALUE_TO_OBJ(target_value);

                switch (target_obj->type) {
                	case ARRAY_OBJ_TYPE:{
		                if(!IS_VALUE_INT(idx_value)){
		                    vmu_error(vm, "Expect 'INT' as index");
		                }

		                int64_t idx = VALUE_TO_INT(idx_value);
		                ArrayObj *array_obj = VALUE_TO_ARRAY(target_value);
		                out_value = vmu_array_get_at(idx, array_obj, vm);

               			break;
                 	}case LIST_OBJ_TYPE:{
		                if(!IS_VALUE_INT(idx_value)){
			                vmu_error(vm, "Expect 'INT' as index");
		                }

		                int64_t idx = VALUE_TO_INT(idx_value);
		                ListObj *list_obj = VALUE_TO_LIST(target_value);
		                out_value = vmu_list_get_at(idx, list_obj, vm);

                  		break;
                  	}case DICT_OBJ_TYPE:{
	                    DictObj *dict_obj = VALUE_TO_DICT(target_value);
	                    out_value = vmu_dict_get(idx_value, dict_obj, vm);

                   		break;
                   	}case STR_OBJ_TYPE:{
	                    if(!IS_VALUE_INT(idx_value)){
	                        vmu_error(vm, "Expect 'INT' as index");
	                    }

	                    int64_t idx = VALUE_TO_INT(idx_value);
	                    StrObj *old_str_obj = VALUE_TO_STR(target_value);
	                    StrObj *new_str_obj = vmu_str_char(idx, old_str_obj, vm);
	                    out_value = OBJ_VALUE(new_str_obj);

                  		break;
                    }case NATIVE_OBJ_TYPE:{
                   		NativeObj *native_obj = OBJ_TO_NATIVE(target_obj);
                    	NativeHeader *native_header = native_obj->native;

                    	switch (native_header->type) {
                     		case NBARRAY_NATIVE_TYPE:{
		                       NBArrayNative *nbuff_native = (NBArrayNative *)native_header;

	                     		if(!IS_VALUE_INT(idx_value)){
		                        	vmu_error(vm, "Expect 'INT' as index");
	                       		}

	                       		int64_t idx = VALUE_TO_INT(idx_value);

	                         	if(idx < 0 || (size_t)idx >= nbuff_native->len){
	                        		vmu_error(vm, "Index out of bounds");
	                          	}

	                         	out_value = INT_VALUE(nbuff_native->bytes[(size_t)idx]);

                    			break;
                       		}default:{
                         		vmu_error(vm, "Illegal native type");
                       			break;
                         	}
                     	}

                  		break;
                    }default:{
                    	vmu_error(vm, "Illegal target to index");
                    }
                }

                pop(vm);
                pop(vm);
                push(out_value, vm);

                break;
            }case OP_RET:{
                OutValue *current_out = NULL;
                OutValue *next_out = NULL;

                while(current_out){
                    next_out = current_out->next;

                    current_out->linked = 0;
                    remove_value_from_current_frame(current_out, vm);

                    current_out = next_out;
                }

                Value result_value = pop(vm);
                Frame *frame = current_frame(vm);

                vm->stack_top = frame->locals;

                pop_frame(vm);

                if(vm->modules_stack_len > 1){
                    Module *module = vm->modules_stack;

                    vm->modules_stack_len--;
                    vm->modules_stack = module->prev;

                    module->prev = NULL;
                    module->submodule->resolved = 1;

                    break;
                }

                if(vm->frame_ptr == vm->frame_stack){
                    return vm->exit_code;
                }

                push(result_value, vm);

                break;
            }case OP_IS:{
                Value value = pop(vm);
                uint8_t type = advance(vm);

                if(IS_VALUE_OBJ(value)){
                    Obj *obj = VALUE_TO_OBJ(value);

                    switch(obj->type){
                        case STR_OBJ_TYPE:{
                            push(BOOL_VALUE(type == 4), vm);
                            break;
                        }case ARRAY_OBJ_TYPE:{
                            push(BOOL_VALUE(type == 5), vm);
                            break;
                        }case LIST_OBJ_TYPE:{
                            push(BOOL_VALUE(type == 6), vm);
                            break;
                        }case DICT_OBJ_TYPE:{
                            push(BOOL_VALUE(type == 7), vm);
                            break;
                        }case RECORD_OBJ_TYPE:{
                            push(BOOL_VALUE(type == 8), vm);
                            break;
                        }case NATIVE_FN_OBJ_TYPE:
                         case FN_OBJ_TYPE:
                         case CLOSURE_OBJ_TYPE:{
                            push(BOOL_VALUE(type == 9), vm);
                            break;
                        }default:{
                            vmu_internal_error(vm, "Illegal object type");
                            break;
                        }
                    }
                }else{
                    switch(value.type){
                        case EMPTY_VALUE_TYPE:{
                            push(BOOL_VALUE(type == 0), vm);
                            break;
                        }case BOOL_VALUE_TYPE:{
                            push(BOOL_VALUE(type == 1), vm);
                            break;
                        }case INT_VALUE_TYPE:{
                            push(BOOL_VALUE(type == 2), vm);
                            break;
                        }case FLOAT_VALUE_TYPE:{
                            push(BOOL_VALUE(type == 3), vm);
                            break;
                        }default:{
                            vmu_internal_error(vm, "Illegal value type");
                            break;
                        }
                    }
                }

                break;
            }case OP_TRYO:{
                size_t catch_ip = (size_t)read_i16(vm);
                Exception *exception = lzpool_alloc_x(64, &vm->exceptions_pool);

                exception->catch_ip = catch_ip;
                exception->stack_top = vm->stack_top;
                exception->frame = current_frame(vm);
                exception->prev = vm->exception_stack;
                vm->exception_stack = exception;

                break;
            }case OP_TRYC:{
                Exception *exception = vm->exception_stack;

                if(exception){
                    vm->exception_stack = exception->prev;
                    MEMORY_DEALLOC(vm->allocator, Exception, 1, exception);
                    break;
                }

                vmu_internal_error(vm, "Exception stack is empty");

                break;
            }case OP_THROW:{
                uint8_t has_value = advance(vm);
                Value raw_value = {0};
                StrObj *throw_msg = NULL;

                if(has_value){
                    raw_value = pop(vm);

                    if(is_value_str(raw_value)){
                        throw_msg = VALUE_TO_STR(raw_value);
                    }else if(is_value_record(raw_value)){
                        RecordObj *record = VALUE_TO_RECORD(raw_value);
                        LZOHTable *attrs = record->attrs;

                        if(attrs){
                            char *raw_key = "msg";
                            size_t key_size = 3;
                            Value *msg_ptr_value = NULL;

                            if(lzohtable_lookup(key_size, raw_key, attrs, (void **)(&msg_ptr_value))){
                                Value msg_value = *msg_ptr_value;

                                if(!is_value_str(msg_value)){
                                    vmu_error(vm, "Expect record attribute 'msg' to be of type 'str'");
                                }

                                throw_msg = VALUE_TO_STR(msg_value);
                            }
                        }
                    }
                }

                Exception *exception = vm->exception_stack;

                if(exception){
                    exception->throw_value = raw_value;
                    longjmp(vm->exit_jmp, 2);
                }

                char *raw_throw_msg = throw_msg ? throw_msg->buff : "";

                vmu_error(vm, raw_throw_msg);

                break;
            }case OP_HLT:{
                return 0;
            }default:{
                assert("Illegal opcode");
            }
        }
    }

    return vm->exit_code;
}
//> PRIVATE IMPLEMENTATION
//> PUBLIC IMPLEMENTATION
VM *vm_create(Allocator *allocator){
    LZOHTable *runtime_strs = MEMORY_LZOHTABLE(allocator);
    DynArr *native_symbols = MEMORY_DYNARR_PTR(allocator);
    VM *vm = MEMORY_ALLOC(allocator, VM, 1);

    if(!runtime_strs || !native_symbols || !vm){
        LZOHTABLE_DESTROY(runtime_strs);
        dynarr_destroy(native_symbols);
        MEMORY_DEALLOC(allocator, VM, 1, vm);

        return NULL;
    }

    memset(vm, 0, sizeof(VM));
    vm->runtime_strs = runtime_strs;
    vm->native_symbols = native_symbols;
    vm->allocation_limit_size = ALLOCATE_START_LIMIT;
    vm->allocator = allocator;

    return vm;
}

void vm_destroy(VM *vm){
    if(!vm){
        return;
    }

    vmu_clean_up(vm);

    DynArr *native_symbols = vm->native_symbols;
    const size_t native_symbols_len = dynarr_len(native_symbols);

    for (size_t i = 0; i < native_symbols_len; i++){
        LZOHTable *symbols = (LZOHTable *)dynarr_get_ptr(native_symbols, i);
        LZOHTABLE_DESTROY(symbols);
    }

    LZOHTABLE_DESTROY(vm->runtime_strs);
    dynarr_destroy(native_symbols);

    lzpool_destroy_deinit(&vm->exceptions_pool);
    lzpool_destroy_deinit(&vm->str_objs_pool);
    lzpool_destroy_deinit(&vm->array_objs_pool);
    lzpool_destroy_deinit(&vm->list_objs_pool);
    lzpool_destroy_deinit(&vm->dict_objs_pool);
    lzpool_destroy_deinit(&vm->record_objs_pool);
    lzpool_destroy_deinit(&vm->native_fn_objs_pool);
    lzpool_destroy_deinit(&vm->fn_objs_pool);
    lzpool_destroy_deinit(&vm->closures_pool);
    lzpool_destroy_deinit(&vm->closure_objs_pool);
    lzpool_destroy_deinit(&vm->native_module_objs_pool);
    lzpool_destroy_deinit(&vm->module_objs_pool);

    MEMORY_DEALLOC(vm->allocator, VM, 1, vm);
}

void vm_initialize(VM *vm){
    vm->white_objs = (ObjList){0};
    vm->gray_objs = (ObjList){0};
    vm->black_objs = (ObjList){0};
    vm->templates = NULL;
    vm->exception_stack = NULL;

    lzpool_init(sizeof(Exception), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->exceptions_pool);
    lzpool_init(sizeof(Value), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->values_pool);
    lzpool_init(sizeof(StrObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->str_objs_pool);
    lzpool_init(sizeof(ArrayObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->array_objs_pool);
    lzpool_init(sizeof(ListObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->list_objs_pool);
    lzpool_init(sizeof(DictObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->dict_objs_pool);
    lzpool_init(sizeof(RecordObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->record_objs_pool);
    lzpool_init(sizeof(FnObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->fn_objs_pool);
    lzpool_init(sizeof(NativeFnObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->native_fn_objs_pool);
    lzpool_init(sizeof(Closure), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->closures_pool);
    lzpool_init(sizeof(ClosureObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->closure_objs_pool);
    lzpool_init(sizeof(NativeModuleObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->native_module_objs_pool);
    lzpool_init(sizeof(ModuleObj), (LZPoolAllocator *)VMU_FRONT_ALLOCATOR, &vm->module_objs_pool);

    MEMORY_INIT_ALLOCATOR(vm, vm_alloc, vm_realloc, vm_dealloc, VMU_FRONT_ALLOCATOR);
}

int vm_execute(LZOHTable *native_fns, Module *module, VM *vm){
    switch (setjmp(vm->exit_jmp)){
        case 0:{
            module->submodule->resolved = 1;

            vm->exit_code = OK_VMRESULT;
            vm->stack_top = vm->stack;
            vm->frame_ptr = vm->frame_stack;

            vm->native_fns = native_fns;

            vm->modules_stack_len = 1;
            vm->modules_stack = module;

            Fn *main_fn = module->entry_fn;

            push_fn(main_fn, vm);
            call_fn(0, main_fn, vm);

            return execute(vm);
        }case 1:{
            // In case of error
            return vm->exit_code;
        }case 2:{
            // In case of throws
            Exception *exception = vm->exception_stack;
            Value throw_value = exception->throw_value;
            Frame *frame = exception->frame;

            frame->ip = exception->catch_ip;
            vm->stack_top = exception->stack_top;
            vm->frame_ptr = frame + 1;
            vm->exception_stack = exception->prev;

            lzpool_dealloc(exception);
            push(throw_value, vm);

            return execute(vm);
        }case 3:{
            Fn *import_fn = (Fn *)get_symbol(
                0,
                FUNCTION_SUBMODULE_SYM_TYPE,
                vm->modules_stack,
                vm
            );

            push_fn(import_fn, vm);
            call_fn(0, import_fn, vm);

            return execute(vm);
        }default:{
            assert(0 && "Illegal jump value");
        }
    }

    return -1;
}
//< PUBLIC IMPLEMENTATION
