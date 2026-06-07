#include "vm.h"

#include "native.h"
#include "native/native_array.h"

#include "utils.h"

#include "types_utils.h"
#include "vm_utils.h"

#include "fn.h"
#include "closure.h"
#include "value.h"
#include "obj.h"
#include "module.h"
#include "opcode.h"

#include "native_module/native_module_str.h"
#include "native_module/native_module_array.h"
#include "native_module/native_module_list.h"
#include "native_module/native_module_dict.h"

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

typedef struct native_info{
    char                *name;
    NativeDestroyHelper destroy_helper;
    NativeAccessIdxGet  access_get_fn;
    NativeAccessIdxSet  access_set_fn;
    NativeGetProperty   get_property_fn;
    LZOHTable           *properties;
}NativeInfo;

struct native_context{
    const VM *vm;
    DynArr   *natives_info;
};

#define LOCALS_LENGTH              255
#define FRAME_LENGTH               255
#define STACK_LENGTH               (LOCALS_LENGTH * FRAME_LENGTH)
#define TRIES_LENGTH               1024
#define ALLOCATE_START_LIMIT       MEMORY_MIBIBYTES(16)
#define GROW_ALLOCATE_LIMIT_FACTOR 2

struct frame{
    size_t    ip;
    size_t    prev_ip;
    Value     *locals;
    const Fn  *fn;
};

typedef struct shadow_frame{
    const Closure *closure;
    ForeignValue  *foreigns_head;
    ForeignValue  *foreigns_tail;
}ShadowFrame;

typedef struct template{
    LZBStr          *str;
    struct template *prev;
}Template;

typedef struct exception{
    size_t catch_offset;
    Value  *stack_top;
    Frame  *frame;
}Try;

struct vm{
    bool_t        halt;
    jmp_buf       exit_jmp;
    byte_t        exit_code;

    Value         *stack_top;
    Frame         *frame_top;
    ShadowFrame   *shadow_frame_top;
    Try           *try_top;

    Value         value_stack[STACK_LENGTH];
    Frame         frame_stack[FRAME_LENGTH];
    ShadowFrame   shadow_frame_stack[FRAME_LENGTH];
    Try           try_stack[TRIES_LENGTH];

    NativeContext *native_context;
    LZOHTable     *runtime_strs;
    Template      *templates;
    DynArr        *native_symbols;
    LZOHTable     *native_fns;
    Module        *main_module;
//--------------------------  GARBAGE COLLECTOR  ---------------------------//
    size_t        mem_use;
    size_t        mem_use_limit;
    ObjList       white_objs;
    ObjList       gray_objs;
    ObjList       black_objs;
//--------------------------------  POOLS  ---------------------------------//
    size_t        exceptions_unit_id;
    size_t        values_unit_id;
    size_t        str_objs_unit_id;
    size_t        array_objs_unit_id;
    size_t        list_objs_unit_id;
    size_t        dict_objs_unit_id;
    size_t        record_objs_unit_id;
    size_t        native_objs_unit_id;
    size_t        native_fn_objs_unit_id;
    size_t        fn_objs_unit_id;
    size_t        closures_unit_id;
    size_t        closure_objs_unit_id;
    size_t        native_module_objs_unit_id;
    size_t        module_objs_unit_id;
    DynArr        *objs_pools;
//------------------------------  ALLOCATORS  ------------------------------//
    Allocator     *allocator;
    Allocator     front_allocator;
};

//------------------------------------------------------------------------------------//
//                                 PRIVATE INTERFACE                                  //
//------------------------------------------------------------------------------------//
#define FIND_LOCATION(index, arr) (dynarr_find(arr, &((OpCodeInfo){.offset = index, .line = -1}), compare_locations))

static void internal_error(VM *vm, const char *msg, ...);
static NativeInfo *get_native_info(const NativeContext *context, native_t native);
static Value native_property(NativeContext *context, NativeObj *native_obj, size_t name_len, const char *name);

static void clean_up_record(void *key, void *value, void *extra);
static void clean_up_dict(void *key, void *value, void *extra);
static int compare_locations(const void *a, const void *b);
static int prepare_stacktrace_new(VM *vm, LZBStr *str, unsigned int spaces);

static void init_obj(VM *vm, Obj *obj, ObjType type);

static Value *clone_value(VM *vm, Value value);
static void destroy_value(Value *value);

void *alloc_exception_unit(VM *vm);
void *alloc_value_unit(VM *vm);
void *alloc_str_obj_unit(VM *vm);
void *alloc_array_obj_unit(VM *vm);
void *alloc_list_obj_unit(VM *vm);
void *alloc_dict_obj_unit(VM *vm);
void *alloc_record_obj_unit(VM *vm);
void *alloc_native_obj_unit(VM *vm);
void *alloc_fn_obj_unit(VM *vm);
void *alloc_native_fn_obj_unit(VM *vm);
void *alloc_closure_unit(VM *vm);
void *alloc_closure_obj_unit(VM *vm);
void *alloc_native_module_obj_unit(VM *vm);
void *alloc_module_obj_unit(VM *vm);
void dealloc_unit(void *ptr);

static void destroy_str(VM *vm, StrObj *str_obj);
static void destroy_array(VM *vm, ArrayObj *array_obj);
static void destroy_list(VM *vm, ListObj *list_obj);
static void destroy_dict(VM *vm, DictObj *dict_obj);
static void destroy_record(VM *vm, RecordObj *record_obj);
static void destroy_native(VM *vm, NativeObj *native_obj);
static void destroy_native_fn(VM *vm, NativeFnObj *native_fn_obj);
static void destroy_fn(VM *vm, FnObj *fn_obj);
static void destroy_closure(VM *vm, ClosureObj *closure_obj);
static void destroy_native_module_obj(VM *vm, NativeModuleObj *native_module_obj);
static void destroy_module_obj(VM *vm, ModuleObj *module_obj);

static void prepare_module_globals(VM *vm, Module *module);
static void prepare_worklist(VM *vm);
static void mark_obj(VM *vm, Obj *obj);
static void mark_objs(VM *vm);
static void sweep_objs(VM *vm);
static void normalize_objs(VM *vm);

//------------------------------------------------------------------------------------//
//                               PRIVATE IMPLEMENTATION                               //
//------------------------------------------------------------------------------------//
void internal_error(VM *vm, const char *msg, ...){
    LZBStr *stacktrace_lzbstr = MEMORY_LZBSTR(vm->allocator);
    int print_stacktrace = stacktrace_lzbstr && !prepare_stacktrace_new(vm, stacktrace_lzbstr, 4);

    va_list args;
	va_start(args, msg);

    fprintf(stderr, "Internal Runtime error: ");
	vfprintf(stderr, msg, args);
    fprintf(stderr, "\n");

    va_end(args);

    if(print_stacktrace){
        fprintf(stderr, "%s", lzbstr_value(stacktrace_lzbstr));
    }else{
        fprintf(stderr, "FAILED TO CREATE STACKTRACE\n");
    }

    lzbstr_destroy(stacktrace_lzbstr);

    vm->exit_code = ERR_VM_RESULT;

    longjmp(vm->exit_jmp, 1);
}

NativeInfo *get_native_info(const NativeContext *context, native_t native){
    DynArr *natives_info = context->natives_info;
    size_t natives_info_len = dynarr_len(natives_info);

    assert(native < natives_info_len);

    return dynarr_get_raw(natives_info, native);
}

Value native_property(NativeContext *context, NativeObj *native_obj, size_t name_len, const char *name){
    native_t native = *(native_t *)native_obj->raw_native;
    NativeInfo *native_info = get_native_info(context, native);
    LZOHTable *properties = native_info->properties;
    NativeFn *out_property = NULL;
    VM *vm = native_vm(context);

    if(!lzohtable_lookup(name_len, name, properties, (void **)&out_property)){
        vm_error(vm, "Native '%s' does't have property '%s'", native_info->name, name);
    }

    Value native_fn_obj_target = OBJ_VALUE(native_obj);
    NativeFnObj *native_fn_obj = vm_create_native_fn(vm, native_fn_obj_target, out_property);

    return OBJ_VALUE(native_fn_obj);
}

void clean_up_record(void *key, void *value, void *extra){
    destroy_value(value);
}

void clean_up_dict(void *key, void *value, void *extra){
    destroy_value(key);
    destroy_value(value);
}

int compare_locations(const void *raw_a_info, const void *raw_b_info){
	OpCodeInfo *a_info = (OpCodeInfo *)raw_a_info;
	OpCodeInfo *b_info = (OpCodeInfo *)raw_b_info;

	if(a_info->offset < b_info->offset){
        return -1;
    }else if(a_info->offset > b_info->offset){
        return 1;
    }else{
        return 0;
    }
}

int prepare_stacktrace_new(VM *vm, LZBStr *str, unsigned int spaces){
    for(Frame *frame = vm->frame_stack; frame < vm->frame_top; frame++){
        const Fn *fn = frame->fn;
		DynArr *locations = fn->locations;
		int opcode_info_idx = FIND_LOCATION(frame->prev_ip, locations);
		OpCodeInfo *opcode_info = opcode_info_idx == -1 ? NULL : (OpCodeInfo *)dynarr_get_raw(locations, opcode_info_idx);

		if(opcode_info){
            if(lzbstr_append_args(
                str,
                "%*sin file: '%s' at %s:%d\n",
                spaces,
                "",
                opcode_info->filepath,
				fn->name,
				opcode_info->line
            )){
                return 1;
            }
		}else{
            if(lzbstr_append_args(str, "inside function '%s'\n", fn->name)){
                return 1;
            }
        }
    }

    return 0;
}

inline void init_obj(VM *vm, Obj *obj, ObjType type){
    obj->type = type;
    obj->marked = 0;
    obj->color = WHITE_OBJ_COLOR;
    obj->prev = NULL;
    obj->next = NULL;

    obj_list_insert(obj, &vm->white_objs);
}

inline Value *clone_value(VM *vm, Value value){
    Value *cloned_value = alloc_value_unit(vm);

    *cloned_value = value;

    return cloned_value;
}

inline void destroy_value(Value *value){
    dealloc_unit(value);
}

inline void *alloc_exception_unit(VM *vm){
    return vm_unit_alloc(vm, vm->exceptions_unit_id);
}

inline void *alloc_value_unit(VM *vm){
    return vm_unit_alloc(vm, vm->values_unit_id);
}

inline void *alloc_str_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->str_objs_unit_id);
}

inline void *alloc_array_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->array_objs_unit_id);
}

inline void *alloc_list_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->list_objs_unit_id);
}

inline void *alloc_dict_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->dict_objs_unit_id);
}

inline void *alloc_record_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->record_objs_unit_id);
}

inline void *alloc_native_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->native_objs_unit_id);
}

inline void *alloc_fn_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->fn_objs_unit_id);
}

inline void *alloc_native_fn_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->native_fn_objs_unit_id);
}

inline void *alloc_closure_unit(VM *vm){
    return vm_unit_alloc(vm, vm->closures_unit_id);
}

inline void *alloc_closure_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->closure_objs_unit_id);
}

inline void *alloc_native_module_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->native_module_objs_unit_id);
}

inline void *alloc_module_obj_unit(VM *vm){
    return vm_unit_alloc(vm, vm->module_objs_unit_id);
}

inline void dealloc_unit(void *ptr){
    vm_unit_dealloc(ptr);
}

void destroy_str(VM *vm, StrObj *str_obj){
    assert(str_obj);

    char *key = str_obj->buff;
    size_t key_len = str_obj->len;
    LZOHTable *runtime_strs = vm->runtime_strs;

    LZOHTABLE_REMOVE(key_len, key, runtime_strs);

    if(str_obj->runtime){
        MEMORY_DEALLOC(vm_allocator(vm), char, key_len + 1, key);
    }

    lzpool_dealloc(str_obj);
}

inline void destroy_array(VM *vm, ArrayObj *array_obj){
    assert(array_obj);
    MEMORY_DEALLOC(vm_allocator(vm), Value, array_obj->len, array_obj->values);
    lzpool_dealloc(array_obj);
}

inline void destroy_list(VM *vm, ListObj *list_obj){
    assert(list_obj);
    dynarr_destroy(list_obj->items);
    lzpool_dealloc(list_obj);
}

inline void destroy_dict(VM *vm, DictObj *dict_obj){
    assert(dict_obj);
    lzohtable_destroy_help(NULL, clean_up_dict, dict_obj->key_values);
    lzpool_dealloc(dict_obj);
}

inline void destroy_record(VM *vm, RecordObj *record_obj){
    assert(record_obj);
    lzohtable_destroy_help(NULL, clean_up_record, record_obj->attrs);
    lzpool_dealloc(record_obj);
}

void destroy_native(VM *vm, NativeObj *native_obj){
	assert(native_obj);

    void *raw_native = native_obj->raw_native;
    native_t native = *(native_t *)raw_native;
    NativeDestroyHelper destroy_fn = native_type_destroy_helper_fn(vm->native_context, native);

    assert(destroy_fn);
    destroy_fn(vm_allocator(vm), raw_native);
    lzpool_dealloc(native_obj);
}

inline void destroy_native_fn(VM *vm, NativeFnObj *native_fn_obj){
    assert(native_fn_obj);
    lzpool_dealloc(native_fn_obj);
}

inline void destroy_fn(VM *vm, FnObj *fn_obj){
    assert(fn_obj);
    lzpool_dealloc(fn_obj);
}

void destroy_closure(VM *vm, ClosureObj *closure_obj){
    assert(closure_obj);

    Closure *closure = closure_obj->closure;
    ForeignValue *out_values = closure->foreigns;
    size_t locals_len = closure->locals_len;

    for (size_t i = 0; i < locals_len; i++){
        destroy_value((&out_values[i])->value);
    }

    MEMORY_DEALLOC(vm_allocator(vm), ForeignValue, locals_len, out_values);
    lzpool_dealloc(closure);
    lzpool_dealloc(closure_obj);
}

inline void destroy_native_module_obj(VM *vm, NativeModuleObj *native_module_obj){
    assert(native_module_obj);
    lzpool_dealloc(native_module_obj);
}

inline void destroy_module_obj(VM *vm, ModuleObj *module_obj){
    assert(module_obj);
    lzpool_dealloc(module_obj);
}

void prepare_module_globals(VM *vm, Module *module){
    LZOHTable *globals = MODULE_GLOBALS(module);

    for (size_t i = 0; i < globals->m; i++){
        LZOHTableSlot *slot = &globals->slots[i];

        if(!slot->used){
            continue;
        }

        GlobalValue *global_value = (GlobalValue *)slot->value;
        Value value = global_value->value;

        if(IS_VALUE_OBJ(value) && VALUE_TO_OBJ(value)->color == WHITE_OBJ_COLOR){
            Obj *global_obj = VALUE_TO_OBJ(value);

            global_obj->color = GRAY_OBJ_COLOR;

            obj_list_remove(global_obj);
            obj_list_insert(global_obj, &vm->gray_objs);

            if(global_obj->type == MODULE_OBJ_TYPE){
                prepare_module_globals(vm, OBJ_TO_MODULE(global_obj)->module);
            }
        }
    }
}

void prepare_worklist(VM *vm){
    prepare_module_globals(vm, vm->main_module);

    const Value *stack_top = vm->stack_top;

    for (Value *stack_slot = vm->value_stack; stack_slot < stack_top; stack_slot++){
        Value value = *stack_slot;

        if(IS_VALUE_OBJ(value) && VALUE_TO_OBJ(value)->color == WHITE_OBJ_COLOR){
            Obj *stack_obj = VALUE_TO_OBJ(value);

            stack_obj->color = GRAY_OBJ_COLOR;

            obj_list_remove(stack_obj);
            obj_list_insert(stack_obj, &vm->gray_objs);

            if(stack_obj->type == MODULE_OBJ_TYPE){
                prepare_module_globals(vm, OBJ_TO_MODULE(stack_obj)->module);
            }
        }
    }
}

void mark_obj(VM *vm, Obj *obj){
    assert(obj->color == GRAY_OBJ_COLOR);

    switch (obj->type){
        case STR_OBJ_TYPE:{
            break;
        }case ARRAY_OBJ_TYPE:{
            ArrayObj *array_obj = OBJ_TO_ARRAY(obj);
            size_t len = array_obj->len;
            Value *values = array_obj->values;

            for (size_t i = 0; i < len; i++){
                Value raw_value = values[i];

                if(IS_VALUE_OBJ(raw_value) && VALUE_TO_OBJ(raw_value)->color == WHITE_OBJ_COLOR){
                    Obj *item_obj = VALUE_TO_OBJ(raw_value);

                    item_obj->color = GRAY_OBJ_COLOR;

                    obj_list_remove(item_obj);
                    obj_list_insert(item_obj, &vm->gray_objs);
                    mark_obj(vm, item_obj);
                }
            }

            break;
        }case LIST_OBJ_TYPE:{
            ListObj *list_obj = OBJ_TO_LIST(obj);
            DynArr *items = list_obj->items;
            size_t len = dynarr_len(items);

            for (size_t i = 0; i < len; i++){
                Value raw_value = DYNARR_GET_AS(items, Value, i);

                if(IS_VALUE_OBJ(raw_value) && VALUE_TO_OBJ(raw_value)->color == WHITE_OBJ_COLOR){
                    Obj *item_obj = VALUE_TO_OBJ(raw_value);

                    item_obj->color = GRAY_OBJ_COLOR;

                    obj_list_remove(item_obj);
                    obj_list_insert(item_obj, &vm->gray_objs);
                    mark_obj(vm, item_obj);
                }
            }

            break;
        }case DICT_OBJ_TYPE:{
            DictObj *dict_obj = OBJ_TO_DICT(obj);
            LZOHTable *key_values = dict_obj->key_values;
            size_t m = key_values->m;

            for (size_t i = 0; i < m; i++){
                LZOHTableSlot slot = key_values->slots[i];

                if(!slot.used){
                    continue;
                }

                Value raw_key = *(Value *)(slot.key);
                Value raw_value = *(Value *)(slot.value);

                if(IS_VALUE_OBJ(raw_key) && VALUE_TO_OBJ(raw_key)->color == WHITE_OBJ_COLOR){
                    Obj *key_obj = VALUE_TO_OBJ(raw_key);

                    key_obj->color = GRAY_OBJ_COLOR;

                    obj_list_remove(key_obj);
                    obj_list_insert(key_obj, &vm->gray_objs);
                    mark_obj(vm, key_obj);
                }

                if(IS_VALUE_OBJ(raw_value) && VALUE_TO_OBJ(raw_value)->color == WHITE_OBJ_COLOR){
                    Obj *value_obj = VALUE_TO_OBJ(raw_value);

                    value_obj->color = GRAY_OBJ_COLOR;

                    obj_list_remove(value_obj);
                    obj_list_insert(value_obj, &vm->gray_objs);
                    mark_obj(vm, value_obj);
                }
            }

            break;
        }case RECORD_OBJ_TYPE:{
            RecordObj *record_obj = OBJ_TO_RECORD(obj);
            LZOHTable *attrs = record_obj->attrs;
            LZOHTableSlot *slots = attrs ? attrs->slots : NULL;
            size_t m = slots ? attrs->m : 0;

            for (size_t i = 0; i < m; i++){
                LZOHTableSlot slot = slots[i];

                if(!slot.used){
                    continue;
                }

                Value raw_value = *(Value *)(slot.value);

                if(IS_VALUE_OBJ(raw_value) && VALUE_TO_OBJ(raw_value)->color == WHITE_OBJ_COLOR){
                    Obj *attr_obj = VALUE_TO_OBJ(raw_value);

                    attr_obj->color = GRAY_OBJ_COLOR;

                    obj_list_remove(attr_obj);
                    obj_list_insert(attr_obj, &vm->gray_objs);
                    mark_obj(vm, attr_obj);
                }
            }

            break;
        }case NATIVE_OBJ_TYPE:{
            break;
        }case NATIVE_FN_OBJ_TYPE:{
            NativeFnObj *native_fn_obj = OBJ_TO_NATIVE_FN(obj);
            Value target = native_fn_obj->target;

            if(IS_VALUE_OBJ(target) && VALUE_TO_OBJ(target)->color == WHITE_OBJ_COLOR){
                Obj *target_obj = VALUE_TO_OBJ(target);

                target_obj->color = GRAY_OBJ_COLOR;

                obj_list_remove(target_obj);
                obj_list_insert(target_obj, &vm->gray_objs);
                mark_obj(vm, target_obj);
            }

            break;
        }case FN_OBJ_TYPE:{
            break;
        }case CLOSURE_OBJ_TYPE:{
            break;
        }case NATIVE_MODULE_OBJ_TYPE:{
            break;
        }case MODULE_OBJ_TYPE:{
            break;
        }default:{
            assert(0 && "Illegal object type");
            break;
        }
    }

    obj->color = BLACK_OBJ_COLOR;

    obj_list_remove(obj);
    obj_list_insert(obj, &vm->black_objs);
}

void mark_objs(VM *vm){
    ObjList *gray_objs = &vm->gray_objs;

    while (gray_objs->head){
        mark_obj(vm, gray_objs->head);
    }
}

void sweep_objs(VM *vm){
    ObjList *white_objs = &vm->white_objs;
    Obj *current = white_objs->head;
    Obj *next = NULL;

    while (current){
        next = current->next;

        switch(current->type){
            case STR_OBJ_TYPE:{
                destroy_str(vm, OBJ_TO_STR(current));

                break;
            }case ARRAY_OBJ_TYPE:{
                destroy_array(vm, OBJ_TO_ARRAY(current));

                break;
            }case LIST_OBJ_TYPE:{
                destroy_list(vm, OBJ_TO_LIST(current));

                break;
            }case DICT_OBJ_TYPE:{
                destroy_dict(vm, OBJ_TO_DICT(current));

                break;
            }case RECORD_OBJ_TYPE:{
                destroy_record(vm, OBJ_TO_RECORD(current));

                break;
            }case NATIVE_OBJ_TYPE:{
           		destroy_native(vm, OBJ_TO_NATIVE(current));

                break;
            }case NATIVE_FN_OBJ_TYPE:{
                destroy_native_fn(vm, OBJ_TO_NATIVE_FN(current));

                break;
            }case FN_OBJ_TYPE:{
                destroy_fn(vm, OBJ_TO_FN(current));

                break;
            }case CLOSURE_OBJ_TYPE:{
                destroy_closure(vm, OBJ_TO_CLOSURE(current));

                break;
            }case NATIVE_MODULE_OBJ_TYPE:{
                destroy_native_module_obj(vm, OBJ_TO_NATIVE_MODULE(current));

                break;
            }case MODULE_OBJ_TYPE:{
                destroy_module_obj(vm, OBJ_TO_MODULE(current));

                break;
            }default:{
                assert(0 && "Illegal object type");

                break;
            }
        }

        current = next;
    }

    white_objs->len = 0;
    white_objs->head = NULL;
    white_objs->tail = NULL;
}

void normalize_objs(VM *vm){
    ObjList *white_objs = &vm->white_objs;
    ObjList *black_objs = &vm->black_objs;
    Obj *current = black_objs->head;
    Obj *next = NULL;

    while (current){
        next = current->next;
        current->color = WHITE_OBJ_COLOR;

        obj_list_remove(current);
        obj_list_insert(current, white_objs);

        current = next;
    }
}

//------------------------------------------------------------------------------------//
//                               PUBLIC IMPLEMENTATION                                //
//------------------------------------------------------------------------------------//
inline const char *native_type_get_name(const NativeContext *context, native_t native){
    return get_native_info(context, native)->name;
}

inline NativeDestroyHelper native_type_destroy_helper_fn(const NativeContext *context, native_t native){
    return get_native_info(context, native)->destroy_helper;
}

inline NativeAccessIdxGet native_type_access_idx_get_fn(const NativeContext *context, native_t native){
    return get_native_info(context, native)->access_get_fn;
}

inline NativeAccessIdxSet native_type_access_idx_set_fn(const NativeContext *context, native_t native){
    return get_native_info(context, native)->access_set_fn;
}

NativeGetProperty native_type_get_property_fn(const NativeContext *context, native_t native){
    return get_native_info(context, native)->get_property_fn;
}

native_t native_identificate(
    NativeContext *context,
    const char *name,
    NativeDestroyHelper destroy_helper,
    NativeAccessIdxGet access_get_fn,
    NativeAccessIdxSet access_set_fn,
    NativeGetProperty get_symbol_fn
){
    const VM *vm = context->vm;
    DynArr *natives_info = context->natives_info;
    char *cloned_name = memory_clone_cstr(vm->allocator, name, NULL);

    NativeInfo info = {
        .name           = cloned_name,
        .properties     = NULL,
        .access_get_fn  = access_get_fn,
        .access_set_fn  = access_set_fn,
        .get_property_fn  = get_symbol_fn,
        .destroy_helper = destroy_helper
    };

    dynarr_insert(natives_info, &info);

    return dynarr_len(natives_info) - 1;
}

inline VM *native_vm(NativeContext *context){
    return (VM *)context->vm;
}

inline Allocator *native_allocator(NativeContext *context){
    return (Allocator *)(&context->vm->front_allocator);
}

void native_properties_add_native_fn(NativeContext *context, native_t native, const char *name, uint8_t arity, RawNativeFn raw_native_fn){
    NativeInfo *info = get_native_info(context, native);
    LZOHTable *properties = info->properties;

    if(!properties){
        const Allocator *allocator = native_allocator(context);
        properties = info->properties = MEMORY_LZOHTABLE(allocator);
    }

    NativeFn *native_fn = vm_factory_native_fn_create(
        context->vm->allocator,
        1,
        name,
        arity,
        raw_native_fn
    );

    lzohtable_put_ck(
        strlen(name),
        name,
        native_fn,
        properties,
        NULL
    );
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
	return next_pow2m1(x-1)+1;
}

static inline size_t max(size_t a, size_t b){
    return ((((size_t) 0) - (a >= b)) & a) | ((((size_t) 0) - (a < b)) & b);
}

static inline size_t min(size_t a, size_t b){
    return ((((size_t) 0) - (a <= b)) & a) | ((((size_t) 0) - (a > b)) & b);
}

void *vm_alloc(size_t size, void * ctx){
    VM *vm = (VM *)ctx;
    Allocator *allocator = vm->allocator;
    size_t mem_use = vm->mem_use;
    size_t mem_use_limit = vm->mem_use_limit;
    size_t new_mem_use = mem_use + size;

    if(new_mem_use >= mem_use_limit){
        vm_gc(vm);

        new_mem_use = vm->mem_use;
        size_t freeded_mem = mem_use - new_mem_use;

        if(freeded_mem < size){
            vm->mem_use_limit = next_pow2(mem_use_limit + size);
        }
    }

    void *ptr = MEMORY_ALLOC(allocator, char, size);

    if(!ptr){
        vm_error(
            vm,
            "Failed to allocated %zu bytes: out of memory",
            size
        );
    }

    vm->mem_use = new_mem_use;

    return ptr;
}

void *vm_realloc(void *ptr, size_t old_size, size_t new_size, void *ctx){
    VM *vm = (VM *)ctx;
    Allocator *allocator = vm->allocator;

    size_t mem_use = vm->mem_use;
    size_t mem_use_limit = vm->mem_use_limit;
    size_t size = max(old_size, new_size) - min(old_size, new_size);
    size_t new_mem_use = new_size == 0 ? mem_use - size : mem_use + size;

    if(new_mem_use > mem_use_limit){
        vm_gc(vm);

        new_mem_use = vm->mem_use;
        size_t freeded_mem = mem_use - new_mem_use;

        if(freeded_mem < size){
            vm->mem_use_limit = next_pow2(mem_use_limit + size);
        }
    }

    void *new_ptr = MEMORY_REALLOC(allocator, char, old_size, new_size, ptr);

    if(!new_ptr){
        vm_error(
            vm,
            "Failed to allocated %zu bytes: out of memory",
            new_size - old_size
        );
    }

    vm->mem_use = new_mem_use;

    return new_ptr;
}

void vm_dealloc(void *ptr, size_t size, void *ctx){
    VM *vm = (VM *)ctx;
    Allocator *allocator = vm->allocator;

    MEMORY_DEALLOC(allocator, char, size, ptr);

    size_t possible_new_mem_limit = vm->mem_use_limit / 2;

    if((vm->mem_use -= size) < (size_t)(possible_new_mem_limit * 0.50) &&
       possible_new_mem_limit > ALLOCATE_START_LIMIT
    ){
        vm->mem_use_limit = possible_new_mem_limit;
    }
}

//> PRIVATE INTERFACE
// UTILS FUNCTIONS
static int16_t compose_i16(uint8_t *bytes);
static int32_t compose_i32(uint8_t *bytes);
static int16_t read_i16(VM *vm);
static int32_t read_i32(VM *vm);
static int64_t read_i64_const(VM *vm);
static double read_f64_const(VM *vm);
static char *read_str_const(VM *vm, size_t *out_len);
//----------     STACK RELATED FUNCTIONS     ----------//
static Value peek(VM *vm);
static Value peek_at(VM *vm, uint16_t offset);
static Value *peek_at_ptr(VM *vm, uint16_t offset);
static void push(VM *vm, Value value);
#define PUSH_EMPTY(_vm)         (push((_vm), EMPTY_VALUE))
#define PUSH_BOOL(_vm, _value)  (push((_vm), BOOL_VALUE(_value)))
#define PUSH_INT(_vm, _value)   (push((_vm), INT_VALUE(_value)))
#define PUSH_FLOAT(_vm, _value) (push((_vm), FLOAT_VALUE(_value)))
#define PUSH_OBJ(_vm, _obj)     (push((_vm), OBJ_VALUE((Obj *)(_obj))))
static FnObj *push_fn(VM *vm, Fn *fn);
static Value pop(VM *vm);
//----------     FRAMES RELATED FUNCTIONS     ----------//
static Frame *peek_frame(const VM *vm);
static Frame *push_frame(VM *vm, uint8_t argsc);
static ShadowFrame *peek_shadow_frame(const VM *vm);

#define VM_CURRENT_FN(_vm)      (peek_frame(vm)->fn)
#define VM_CURRENT_MODULE(_vm)  (VM_CURRENT_FN(_vm)->module)
#define VM_CURRENT_ICONSTS(_vm) (VM_CURRENT_FN(_vm)->iconsts)
#define VM_CURRENT_FCONSTS(_vm) (VM_CURRENT_FN(_vm)->fconsts)
#define VM_CURRENT_SCONSTS(_vm) (VM_CURRENT_MODULE(_vm)->context->str_consts)
#define VM_CURRENT_GLOBALS(_vm) (VM_CURRENT_MODULE(_vm)->context->globals)
#define VM_CURRENT_CLOSURE(_vm) (peek_shadow_frame(vm)->closure)

static uint8_t advance(VM *vm);
static uint8_t advance_save(VM *vm);
static uint8_t *advance_by_ptr(VM *vm, size_t by);

static void add_out_value_to_current_frame(VM *vm, ForeignValue *value);
static ForeignValue *remove_value_from_current_frame(VM *vm, ForeignValue *value);

static void call_fn(uint8_t argsc, const Fn *fn, VM *vm);
static void call_closure(uint8_t argsc, Closure *closure, VM *vm);
static void pop_frame(VM *vm);
static Value *frame_local(uint8_t which, VM *vm);
// OTHERS
static ClosureObj *init_closure(VM *vm, Closure *closure);
static int execute(VM *vm);
//< PRIVATE INTERFACE
//> PRIVATE IMPLEMENTATION
inline int16_t compose_i16(uint8_t *bytes){
    return (int16_t)((uint16_t)bytes[0] << 8) | ((uint16_t)bytes[1]);
}

inline int32_t compose_i32(uint8_t *bytes){
    return (int32_t)((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | ((uint32_t)bytes[3]);
}

inline int16_t read_i16(VM *vm){
    uint8_t *bytes = advance_by_ptr(vm, 2);

    return compose_i16(bytes);
}

inline int32_t read_i32(VM *vm){
	uint8_t *bytes = advance_by_ptr(vm, 4);

	return compose_i32(bytes);
}

inline int64_t read_i64_const(VM *vm){
    DynArr *iconsts = VM_CURRENT_ICONSTS(vm);
    size_t idx = (size_t)read_i16(vm);

    return DYNARR_GET_AS(iconsts, int64_t, idx);
}

inline double read_f64_const(VM *vm){
    DynArr *fconsts = VM_CURRENT_FCONSTS(vm);
    size_t idx = (size_t)read_i16(vm);

    return DYNARR_GET_AS(fconsts, double, idx);
}

char *read_str_const(VM *vm, size_t *out_len){
    DynArr *str_consts = VM_CURRENT_SCONSTS(vm);
    size_t idx = (size_t)read_i16(vm);

    if(idx >= dynarr_len(str_consts)){
        internal_error(vm, "Failed to get string constant: index out of bounds");
    }

    DStr str_const = DYNARR_GET_AS(str_consts, DStr, idx);

    *out_len = str_const.len;

    return str_const.buff;
}

inline Value peek(VM *vm){
    return *(vm->stack_top - 1);
}

inline Value peek_at(VM *vm, uint16_t offset){
    if(vm->stack_top - 1 - offset <= vm->value_stack){
        internal_error( vm, "Illegal stack peek offset");
    }

    return *(vm->stack_top - 1 - offset);
}

inline Value *peek_at_ptr(VM *vm, uint16_t offset){
    if(vm->stack_top - 1 - offset <= vm->value_stack){
        internal_error(vm, "Illegal stack peek offset");
    }

    return vm->stack_top - 1 - offset;
}

inline void push(VM *vm, Value value){
    if(vm->stack_top >= vm->value_stack + STACK_LENGTH){
        vm_error(vm, "Stack over flow");
    }

    *((vm->stack_top)++) = value;
}

inline FnObj *push_fn(VM *vm, Fn *fn){
    FnObj *fn_obj = vm_create_fn(vm, fn);

    PUSH_OBJ(vm, fn_obj);

    return fn_obj;
}

inline Value pop(VM *vm){
    if(vm->stack_top == vm->value_stack){
        vm_error(vm, "Stack under flow");
    }

    return *(--vm->stack_top);
}

inline Frame *peek_frame(const VM *vm){
    return vm->frame_top - 1;
}

Frame *push_frame(VM *vm, uint8_t argsc){
    if(vm->frame_top >= vm->frame_stack + FRAME_LENGTH){
        vm_error(vm, "Frame stack is full");
    }

    Frame *frame = vm->frame_top++;

    *frame = (Frame){
        .locals  = vm->stack_top - 1 - argsc
    };

    vm->shadow_frame_top++;

    return frame;
}

inline ShadowFrame *peek_shadow_frame(const VM *vm){
    return vm->shadow_frame_top - 1;
}

inline uint8_t advance(VM *vm){
    Frame *frame = peek_frame(vm);

    return DYNARR_GET_AS(frame->fn->chunks, uint8_t, frame->ip++);
}

inline uint8_t advance_save(VM *vm){
    Frame *frame = peek_frame(vm);
    size_t at = frame->prev_ip = frame->ip++;
    uint8_t chunk = DYNARR_GET_AS(frame->fn->chunks, uint8_t, at);

    return chunk;
}

inline uint8_t *advance_by_ptr(VM *vm, size_t by){
    Frame *frame = peek_frame(vm);

    return (uint8_t *)dynarr_get_raw(frame->fn->chunks, (frame->ip += by) - by);
}

void add_out_value_to_current_frame(VM *vm, ForeignValue *foreign){
    ShadowFrame *shadow_frame = peek_shadow_frame(vm);

    if(shadow_frame->foreigns_tail){
        shadow_frame->foreigns_tail->next = foreign;
        foreign->prev = shadow_frame->foreigns_tail;
    }else{
        shadow_frame->foreigns_head = foreign;
    }

    shadow_frame->foreigns_tail = foreign;
}

ForeignValue *remove_value_from_current_frame(VM *vm, ForeignValue *foreign){
    ForeignValue *next_foreign = foreign->next;
    ShadowFrame *shadow_frame = peek_shadow_frame(vm);

    if(foreign == shadow_frame->foreigns_head){
        shadow_frame->foreigns_head = foreign->next;
    }
    if(foreign == shadow_frame->foreigns_tail){
        shadow_frame->foreigns_tail = foreign->prev;
    }

    if(foreign->prev){
        foreign->prev->next = foreign->next;
    }
    if(foreign->next){
        foreign->next->prev = foreign->prev;
    }

    foreign->prev = NULL;
    foreign->next = NULL;

    return next_foreign;
}

inline void call_fn(uint8_t argsc, const Fn *fn, VM *vm){
    size_t params_count = fn->arity;

    if(argsc != params_count){
        vm_error(
            vm,
            "Failed to call function '%s'. Declared with %d parameter(s), but got %d argument(s)",
            fn->name,
            params_count,
            argsc
        );
    }

    push_frame(vm, argsc)->fn = fn;
}

inline void call_closure(uint8_t argsc, Closure *closure, VM *vm){
    Fn *fn = closure->fn;
    size_t params_count = fn->arity;

    if(argsc != params_count){
        vm_error(
            vm,
            "Failed to call closure '%s'. Declared with %d parameter(s), but got %d argument(s)",
            fn->name,
            params_count,
            argsc
        );
    }

    Frame *frame = push_frame(vm, argsc);
    ShadowFrame *shadow_frame = peek_shadow_frame(vm);

    frame->fn = fn;
    shadow_frame->closure = closure;
}

inline void pop_frame(VM *vm){
    if(vm->frame_top == vm->frame_stack){
        vm_error(
            vm,
            "Frame stack is empty"
        );
    }

    vm->frame_top--;
    vm->shadow_frame_top--;
}

inline Value *frame_local(uint8_t which, VM *vm){
    Frame *frame = peek_frame(vm);
    Value *locals = frame->locals;
    Value *local = locals + 1 + which;

    if(local >= vm->stack_top){
        internal_error(vm, "Illegal frame local offset: %"PRId8"", which);
    }

    return local;
}

#define BINARY_OP(_vm, _OPERATOR)                                                          \
    Value right_value = pop(_vm);                                                          \
    Value left_value = pop(_vm);                                                           \
                                                                                           \
    if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){                             \
        PUSH_INT(_vm, VALUE_TO_INT(left_value) _OPERATOR VALUE_TO_INT(right_value));       \
    }else if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){                   \
        PUSH_FLOAT(_vm, VALUE_TO_FLOAT(left_value) _OPERATOR VALUE_TO_FLOAT(right_value)); \
    }else if(IS_VALUE_INT(left_value) && IS_VALUE_FLOAT(right_value)){                     \
        PUSH_FLOAT(_vm, VALUE_TO_INT(left_value) _OPERATOR VALUE_TO_FLOAT(right_value));   \
    }else if(IS_VALUE_FLOAT(left_value) && IS_VALUE_INT(right_value)){                     \
        PUSH_FLOAT(_vm, VALUE_TO_FLOAT(left_value) _OPERATOR VALUE_TO_INT(right_value));   \
    }else{                                                                                 \
        vm_error(                                                                          \
            vm,                                                                            \
            "Illegal operands types for %s operator",                                      \
            #_OPERATOR                                                                     \
        );                                                                                 \
    }

static int compare_closure_foreigns(const void *a, const void *b){
    ForeignValue *foreign_a = (ForeignValue *)a;
    ForeignValue *foreign_b = (ForeignValue *)b;
    uint8_t local_a = foreign_a->local;
    uint8_t local_b = foreign_b->local;

    if(local_a == local_b){
        return 0;
    }else{
        return local_a > local_b ? 1 : -1;
    }
}

ClosureObj *init_closure(VM *vm, Closure *closure){
    ClosureObj *closure_obj = vm_create_closure(vm, closure);
    ForeignValue *foreigns = closure_obj->closure->foreigns;
    size_t locals_len = closure->locals_len;

    for (size_t i = 0; i < locals_len; i++){
        ForeignValue *foreign = &foreigns[i];
        uint8_t local = closure->locals[i];

        foreign->linked = 1;
        foreign->local = local;
        foreign->value = frame_local(local, vm);

        add_out_value_to_current_frame(vm, foreign);
    }

    return closure_obj;
}

int execute(VM *vm){
    for (;;){
        switch (advance_save(vm)){
            case OP_EMPTY:{
                PUSH_EMPTY(vm);

                break;
            }case OP_FALSE:{
                PUSH_BOOL(vm, 0);

                break;
            }case OP_TRUE:{
                PUSH_BOOL(vm, 1);

                break;
            }case OP_INT:{
                int64_t value = read_i64_const(vm);

                PUSH_INT(vm, value);

                break;
            }case OP_FLOAT:{
                double value = read_f64_const(vm);

                PUSH_FLOAT(vm, value);

                break;
            }case OP_STRING:{
                size_t len;
                StrObj *str_obj = NULL;
                char *str = read_str_const(vm, &len);

                vm_create_str(vm, 0, len, str, &str_obj);
                PUSH_OBJ(vm, str_obj);

                break;
            }case OP_START_TEMPLATE:{
                LZBStr *str = MEMORY_LZBSTR(vm->allocator);
                Template *template = MEMORY_ALLOC(vm->allocator, Template, 1);

                template->str = str;
                template->prev = vm->templates;
                vm->templates = template;

                break;
            }case OP_END_TEMPLATE:{
                Template *template = vm->templates;

                if(template){
                    LZBStr *str = template->str;
                    size_t buff_len;
                    char *buff = NULL;
                    StrObj *str_obj = NULL;

                    lzbstr_rclone_buff(
                        str,
                        (LZBStrAllocator *)&vm->front_allocator,
                        &buff_len,
                        &buff
                    );

                    if(vm_create_str(vm, 1, buff_len, buff, &str_obj)){
                        MEMORY_DEALLOC(&vm->front_allocator, char, buff_len + 1, buff);
                    }

                    PUSH_OBJ(vm, str_obj);

                    vm->templates = template->prev;

                    lzbstr_destroy(str);
                    MEMORY_DEALLOC(vm->allocator, Template, 1, template);

                    break;
                }

                internal_error(vm, "Template stack is empty");

                break;
            }case OP_ARRAY:{
                Value len_value = pop(vm);

                if(!IS_VALUE_INT(len_value)){
                    vm_error(vm, "Expect 'INT' as array length");
                }

                int64_t array_len = VALUE_TO_INT(len_value);
                ArrayObj *array_obj = vm_create_array(vm, array_len);

                PUSH_OBJ(vm, array_obj);

                break;
            }case OP_LIST:{
                ListObj *list_obj = vm_create_list(vm);

                PUSH_OBJ(vm, list_obj);

                break;
            }case OP_DICT:{
                DictObj *dict_obj = vm_create_dict(vm);

                PUSH_OBJ(vm, dict_obj);

                break;
            }case OP_RECORD:{
                uint16_t len = (uint16_t)read_i16(vm);
                RecordObj *record_obj = vm_create_record(vm, len);

                PUSH_OBJ(vm, record_obj);

                break;
            }case OP_WRITE_TEMPLATE:{
                Template *template = vm->templates;
                Value raw_value = pop(vm);

                if(template){
                    LZBStr *str = template->str;
                    vm_utils_value_to_str_w(vm, raw_value, str);
                    break;
                }

                internal_error(vm, "Template stack is empty");

                break;
            }case OP_INIT_ARRAY:{
                int64_t idx = (int64_t)read_i16(vm);
                Value value = pop(vm);
                Value array_value = peek(vm);
                ArrayObj *array_obj = VALUE_TO_ARRAY(array_value);

                vm_array_set_at(vm, idx, value, array_obj);

                break;
            }case OP_INIT_LIST:{
                Value value = peek_at(vm, 0);
                Value list_value = peek_at(vm, 1);

                if(!is_value_list(list_value)){
                    internal_error(vm, "Expect value of type 'dict', but got something else");
                }

                vm_list_insert(vm, value, VALUE_TO_LIST(list_value));
                pop(vm);

                break;
            }case OP_INIT_DICT:{
                Value raw_value = peek_at(vm, 0);
                Value key_value = peek_at(vm, 1);
                Value dict_value = peek_at(vm, 2);

                if(!is_value_dict(dict_value)){
                    internal_error(vm, "Expect value of type 'dict', but got something else");
                }

                vm_dict_put(vm, key_value, raw_value, VALUE_TO_DICT(dict_value));
                pop(vm);
                pop(vm);

                break;
            }case OP_INIT_RECORD:{
                size_t key_size;
                char *key = read_str_const(vm, &key_size);
                Value raw_value = peek_at(vm, 0);
                Value record_value = peek_at(vm, 1);

                if(!is_value_record(record_value)){
                    internal_error(vm, "Expect value of type 'record', but got something else");
                }

                vm_record_insert_attr(vm, key_size, key, raw_value, VALUE_TO_RECORD(record_value));
                pop(vm);

                break;
            }case OP_CONCAT:{
                Value right_value = peek_at(vm, 0);
                Value left_value = peek_at(vm, 1);

                if(is_value_str(left_value) && is_value_str(right_value)){
                    StrObj *left_str_obj = VALUE_TO_STR(left_value);
                    StrObj *right_str_obj = VALUE_TO_STR(right_value);
                    StrObj *result_str_obj = vm_str_concat(vm, left_str_obj, right_str_obj);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(vm, result_str_obj);

                    break;
                }

                if(is_value_array(left_value) && is_value_array(right_value)){
                    ArrayObj *left_array_obj = VALUE_TO_ARRAY(left_value);
                    ArrayObj *right_array_obj = VALUE_TO_ARRAY(right_value);
                    ArrayObj *new_array_obj = vm_array_join(vm, left_array_obj, right_array_obj);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(vm, new_array_obj);

                    break;
                }

                if(is_value_list(left_value) && is_value_list(right_value)){
                    ListObj *left_list_obj = VALUE_TO_LIST(left_value);
                    ListObj *right_list_obj = VALUE_TO_LIST(right_value);
                    ListObj *new_list_obj = vm_list_join(vm, left_list_obj, right_list_obj);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(vm, new_list_obj);

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

                    ArrayObj *new_array_obj = vm_array_join_value(vm, raw_value, array_obj);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(vm, new_array_obj);

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

                    ListObj *new_list_obj = vm_list_insert_new(vm, raw_value, list_obj);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(vm, new_list_obj);

                    break;
                }

                vm_error(vm, "Illegal operands for concatenation");

                break;
            }case OP_MULSTR:{
                Value right_value = peek_at(vm, 0);
                Value left_value = peek_at(vm, 1);

                if(IS_VALUE_INT(left_value) && is_value_str(right_value)){
                    int64_t by = VALUE_TO_INT(left_value);
                    StrObj *str_obj = VALUE_TO_STR(right_value);
                    StrObj *new_str_obj = vm_str_mul(vm, by, str_obj);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(vm, new_str_obj);

                    break;
                }

                if(is_value_str(left_value) && IS_VALUE_INT(right_value)){
                    int64_t by = VALUE_TO_INT(right_value);
                    StrObj *str_obj = VALUE_TO_STR(left_value);
                    StrObj *new_str_obj = vm_str_mul(vm, by, str_obj);

                    pop(vm);
                    pop(vm);
                    PUSH_OBJ(vm, new_str_obj);

                    break;
                }

                vm_error(vm, "Illegal operands for string multiplication");

                break;
            }case OP_ADD:{
                BINARY_OP(vm, +)

                break;
            }case OP_SUB:{
                BINARY_OP(vm, -)

                break;
            }case OP_MUL:{
                BINARY_OP(vm, *)

                break;
            }case OP_DIV:{
                BINARY_OP(vm, /)

                break;
            }case OP_MOD:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    push(vm, INT_VALUE(left % right));

                    break;
                }

                vm_error(vm, "Unsuported types using 'mod' operator");

                break;
            }case OP_BNOT:{
                Value value = pop(vm);

                if(IS_VALUE_INT(value)){
                    PUSH_INT(vm, ~VALUE_TO_INT(value));
                    break;
                }

                vm_error(vm, "Unsuported types using '~' operator");

                break;
            }case OP_LSH:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    uint64_t left = (uint64_t)VALUE_TO_INT(left_value);
                    uint64_t right = (uint64_t)VALUE_TO_INT(right_value);

                    PUSH_INT(vm, left << right);

                    break;
                }

                vm_error(vm, "Unsuported types using '<<' operator");

                break;
            }case OP_RSH:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    uint64_t left = (uint64_t)VALUE_TO_INT(left_value);
                    uint64_t right = (uint64_t)VALUE_TO_INT(right_value);

                    PUSH_INT(vm, left >> right);

                    break;
                }

                vm_error(vm, "Unsuported types using '>>' operator");

                break;
            }case OP_BAND:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_INT(vm, left & right);

                    break;
                }

                vm_error(vm, "Unsuported types using '&' operator");

                break;
            }case OP_BXOR:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_INT(vm, left ^ right);

                    break;
                }

                vm_error(vm, "Unsuported types using '^' operator");

                break;
            }case OP_BOR:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_INT(vm, left | right);

                    break;
                }

                vm_error(vm, "Unsuported types using '|' operator");

                break;
            }case OP_LT:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(vm, left < right);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(vm, left < right);

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

                    PUSH_BOOL(vm, left < right);

                    break;
                }

                vm_error(vm, "Unsuported types using < operator");

                break;
            }case OP_GT:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(vm, left > right);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(vm, left > right);

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

                    PUSH_BOOL(vm, left > right);

                    break;
                }

                vm_error(vm, "Unsuported types using > operator");

                break;
            }case OP_LE:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(vm, left <= right);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(vm, left <= right);

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

                    PUSH_BOOL(vm, left <= right);

                    break;
                }

                vm_error(vm, "Unsuported types using <= operator");

                break;
            }case OP_GE:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(vm, left >= right);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(vm, left >= right);

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

                    PUSH_BOOL(vm, left >= right);

                    break;
                }

                vm_error(vm, "Unsuported types using >= operator");

                break;
            }case OP_EQ:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_BOOL(left_value) && IS_VALUE_BOOL(right_value)){
                    uint8_t left = VALUE_TO_BOOL(left_value);
                    uint8_t right = VALUE_TO_BOOL(right_value);

                    PUSH_BOOL(vm, left == right);

                    break;
                }

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(vm, left == right);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(vm, left == right);

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

                    PUSH_BOOL(vm, left == right);

                    break;
                }

                if(is_value_str(left_value) && is_value_str(right_value)){
                    StrObj *left = VALUE_TO_STR(left_value);
                    StrObj *right = VALUE_TO_STR(right_value);

                    PUSH_BOOL(vm, left == right);

                    break;
                }

                vm_error(vm, "Unsuported types using == operator");

                break;
            }case OP_NE:{
                Value right_value = pop(vm);
                Value left_value = pop(vm);

                if(IS_VALUE_BOOL(left_value) && IS_VALUE_BOOL(right_value)){
                    uint8_t left = VALUE_TO_BOOL(left_value);
                    uint8_t right = VALUE_TO_BOOL(right_value);

                    PUSH_BOOL(vm, left != right);

                    break;
                }

                if(IS_VALUE_INT(left_value) && IS_VALUE_INT(right_value)){
                    int64_t left = VALUE_TO_INT(left_value);
                    int64_t right = VALUE_TO_INT(right_value);

                    PUSH_BOOL(vm, left != right);

                    break;
                }

                if(IS_VALUE_FLOAT(left_value) && IS_VALUE_FLOAT(right_value)){
                    double left = VALUE_TO_FLOAT(left_value);
                    double right = VALUE_TO_FLOAT(right_value);

                    PUSH_BOOL(vm, left != right);

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

                    PUSH_BOOL(vm, left != right);

                    break;
                }

                if(is_value_str(left_value) && is_value_str(right_value)){
                    StrObj *left = VALUE_TO_STR(left_value);
                    StrObj *right = VALUE_TO_STR(right_value);

                    PUSH_BOOL(vm, left != right);

                    break;
                }

                vm_error(vm, "Unsuported types using != operator");

                break;
            }case OP_OR:{
                int16_t jmp_value = read_i16(vm);
                Value value = peek(vm);

                if(!IS_VALUE_BOOL(value)){
                    vm_error(vm, "Unsupported types using 'or' operator");
                }

                if(VALUE_TO_BOOL(value)){
                    peek_frame(vm)->ip += jmp_value;
                    break;
                }

                pop(vm);

                break;
            }case OP_AND:{
                int16_t jmp_value = read_i16(vm);
                Value value = peek(vm);

                if(!IS_VALUE_BOOL(value)){
                    vm_error(vm, "Unsupported types using 'and' operator");
                }

                if(!VALUE_TO_BOOL(value)){
                    peek_frame(vm)->ip += jmp_value;
                    break;
                }

                pop(vm);

                break;
            }case OP_NOT:{
                Value value = pop(vm);

                if(!IS_VALUE_BOOL(value)){
                    vm_error(vm, "Expect boolean at right side");
                }

                PUSH_BOOL(vm, !VALUE_TO_BOOL(value));

                break;
            }case OP_NNOT:{
                Value value = pop(vm);

                if(IS_VALUE_INT(value)){
                    PUSH_INT(vm, -VALUE_TO_INT(value));

                    break;
                }

                if(IS_VALUE_FLOAT(value)){
                    PUSH_FLOAT(vm, -VALUE_TO_FLOAT(value));

                    break;
                }

                vm_error(
                    vm,
                    "Expect integer or float at right side"
                );

                break;
            }case OP_LOCAL_SET:{
                Value value = peek(vm);
                uint8_t index = advance(vm);

                *frame_local(index, vm) = value;

                break;
            }case OP_LOCAL_GET:{
                uint8_t at = advance(vm);
                Value value = *frame_local(at, vm);

                push(vm, value);

                break;
            }case OP_FOREIGN_SET:{
                uint8_t local = advance(vm);
                Value value = peek(vm);

                const Closure *closure = VM_CURRENT_CLOSURE(vm);
                size_t locals_len = closure->locals_len;
                uint8_t *locals = closure->locals;
                ForeignValue *foreigns = closure->foreigns;

                for (size_t i = 0; i < locals_len; i++){
                    uint8_t foreign_local = locals[i];

                    if(foreign_local == local){
                        *foreigns[i].value = value;

                        goto OP_FOREIGN_SET_EXIT;
                    }
                }

                internal_error(vm, "Not foreign value at local %"PRIu8"", local);

            OP_FOREIGN_SET_EXIT:
                break;
            }case OP_FOREIGN_GET:{
                uint8_t local = advance(vm);

                const Closure *closure = VM_CURRENT_CLOSURE(vm);
                size_t locals_len = closure->locals_len;
                uint8_t *locals = closure->locals;
                ForeignValue *foreigns = closure->foreigns;

                for (size_t i = 0; i < locals_len; i++){
                    uint8_t foreign_local = locals[i];

                    if(foreign_local == local){
                        ForeignValue *foreign = &foreigns[i];

                        push(vm, *foreign->value);

                        goto OP_FOREIGN_GET_EXIT;
                    }
                }

                internal_error(vm, "Not foreign value at local %"PRIu8"", local);

            OP_FOREIGN_GET_EXIT:
                break;
            }case OP_GLOBAL_DEF:{
                size_t key_size;
                char *key = read_str_const(vm, &key_size);
                Value value = pop(vm);
                LZOHTable *globals = MODULE_GLOBALS(VM_CURRENT_FN(vm)->module);

                if(lzohtable_lookup(key_size, key, globals, NULL)){
                    vm_error(
                        vm,
                        "Cannot define global '%s': already exists",
                        key
                    );
                }

                GlobalValue global_value = {0};

                global_value.access = PRIVATE_GLOBAL_VALUE_TYPE;
                global_value.value = value;

                lzohtable_put_ckv(key_size, key, sizeof(GlobalValue), &global_value, globals, NULL);

                break;
            }case OP_GLOBAL_ACCESS_SET:{
                Module *module = VM_CURRENT_MODULE(vm);
                LZOHTable *globals = MODULE_GLOBALS(module);

                size_t key_size;
                char *key = read_str_const(vm, &key_size);
                GlobalValue *global_value = NULL;

                if(!lzohtable_lookup(key_size, key, globals, (void **)(&global_value))){
                    vm_error(
                        vm,
                        "Global symbol '%s' does not exists",
                        key
                    );

                    break;
                }

                Value value = global_value->value;
                uint8_t access_type = advance(vm);

                if(is_value_native_module(value) || is_value_module(value)){
                    vm_error(vm, "Modules cannot modify its access");
                }

                if(access_type == 0){
                    global_value->access = PRIVATE_GLOBAL_VALUE_TYPE;
                }else if(access_type == 1){
                    global_value->access = PUBLIC_GLOBAL_VALUE_TYPE;
                }else{
                    vm_error(
                        vm,
                        "Illegal access type: %d",
                        access_type
                    );
                }

                break;
            }case OP_GLOBAL_SET:{
                size_t key_size;
                char *key = read_str_const(vm, &key_size);
                Value value = peek(vm);
                GlobalValue *global_value = NULL;

                if(lzohtable_lookup(
                    key_size,
                    key,
                    VM_CURRENT_GLOBALS(vm),
                    (void **)(&global_value))
                ){
                    global_value->value = value;

                    break;
                }

                vm_error(
                    vm,
                    "Global '%s' does not exists",
                    key
                );

                break;
            }case OP_GLOBAL_GET:{
                size_t key_size;
                char *key = read_str_const(vm, &key_size);
                GlobalValue *global_value = NULL;

                if(!lzohtable_lookup(
                	key_size,
                 	key,
                  	VM_CURRENT_GLOBALS(vm),
                   	(void **)(&global_value)
                )){
                    vm_error(
                    	vm,
                     	"Global '%s' does not exist",
                      	key
                    );
                }

                push(vm, global_value->value);

                break;
            }case OP_NATIVE_GET:{
                size_t key_size = 0;
                char *key = read_str_const(vm, &key_size);
                Value *out_value = NULL;

                if(lzohtable_lookup(
                    key_size,
                    key,
                    vm->native_fns,
                    (void **)(&out_value))
                ){
                    push(vm, *out_value);

                    break;
                }

                internal_error(
                    vm,
                    "Unknown native symbol '%s'",
                    key
                );

                break;
            }case OP_FN:{
                size_t fn_idx = (size_t)read_i32(vm);
                Module *module = VM_CURRENT_MODULE(vm);
                DynArr *fns = MODULE_CLOSURES(module);
                size_t fns_len = dynarr_len(fns);

                if(fn_idx >= fns_len){
                    internal_error(
                    	vm,
                     	"Failed to get module symbol: index (%zu) out of bounds",
                      	fn_idx
                    );
                }

                Fn *fn = (Fn *)dynarr_get_raw(fns, fn_idx);
                FnObj *fn_obj = vm_create_fn(vm, fn);

                PUSH_OBJ(vm, fn_obj);

                break;
            }case OP_CLOSURE:{
                size_t closure_idx = (size_t)read_i32(vm);
                Module *module = VM_CURRENT_MODULE(vm);
                DynArr *closures = MODULE_CLOSURES(module);
                size_t closures_len = dynarr_len(closures);

                if(closure_idx >= closures_len){
                    internal_error(
                    	vm,
                     	"Failed to get module symbol: index (%zu) out of bounds",
                      	closure_idx
                    );
                }

                Closure *closure = (Closure *)dynarr_get_raw(closures, closure_idx);
                ClosureObj *closure_obj = init_closure(vm, closure);

                PUSH_OBJ(vm, closure_obj);

                break;
            }case OP_ARRAY_SET:{
                Value indexable_value = peek_at(vm, 0);
                Value idx_value = peek_at(vm, 1);
                Value value = peek_at(vm, 2);

                if(!IS_VALUE_OBJ(indexable_value)){
                    vm_error(vm, "Illegal assignment target, expect: array, list, dict, native_array");
                }

                Obj *target_obj = VALUE_TO_OBJ(indexable_value);

                switch (target_obj->type){
                    case ARRAY_OBJ_TYPE:{
                        if(!IS_VALUE_INT(idx_value)){
                            vm_error(vm, "Expect index value of type 'int'");
                        }

                        int64_t idx = VALUE_TO_INT(idx_value);
                        ArrayObj *array_obj = VALUE_TO_ARRAY(indexable_value);

                        vm_array_set_at(vm, idx, value, array_obj);

                        break;
                    }case LIST_OBJ_TYPE:{
                        if(!IS_VALUE_INT(idx_value)){
                            vm_error(vm, "Expect index value of type 'int'");
                        }

                        int64_t idx = VALUE_TO_INT(idx_value);
                        ListObj *list_obj = VALUE_TO_LIST(indexable_value);

                        vm_list_set_at(vm, idx, value, list_obj);

                        break;
                    }case DICT_OBJ_TYPE:{
                        DictObj *dict_obj = VALUE_TO_DICT(indexable_value);

                        vm_dict_put(vm, idx_value, value, dict_obj);

                        break;
                    }case NATIVE_OBJ_TYPE:{
                        NativeContext *context = vm->native_context;
                   		NativeObj *native_obj = OBJ_TO_NATIVE(target_obj);
                        native_t native = *(native_t *)native_obj->raw_native;
                        NativeAccessIdxSet access_fn = native_type_access_idx_set_fn(context, native);

                        if(access_fn){
                            if(!IS_VALUE_INT(idx_value)){
                                vm_error(vm, "Expect index value of type 'int'");
                            }

                            if(!IS_VALUE_INT(value)){
                                vm_error(vm, "Expect assignment value of type 'int'");
                            }

                            access_fn(context, native_obj->raw_native, idx_value, value);

                            break;
                        }

                        vm_error(vm, "Illegal assignment target");

                        break;
                    }default:{
                        vm_error(vm, "Illegal assignment target");

                        break;
                    }
                }

                pop(vm);
                pop(vm);

                break;
            }case OP_RECORD_SET:{
                size_t key_size;
                char *key = read_str_const(vm, &key_size);
                Value target_value = pop(vm);
                Value raw_value = peek(vm);

                if(!is_value_record(target_value)){
                    vm_error(vm, "Expect record in assignment");
                }

                RecordObj *record_obj = VALUE_TO_RECORD(target_value);

                vm_record_set_attr(vm, key_size, key, raw_value, record_obj);

                break;
            }case OP_POP:{
                pop(vm);

                break;
            }case OP_OFFSET:{
                uint8_t offset = advance(vm);
                Value *new_stack_top = vm->stack_top - offset;

                if(new_stack_top < vm->value_stack){
                    internal_error(vm, "OP_OFFSET error: illegal 'offset' value %"PRId8"", offset);
                }

                vm->stack_top = new_stack_top;

                break;
            }case OP_JMP:{
                int16_t jmp_value = read_i16(vm);

                peek_frame(vm)->ip += jmp_value;

                break;
            }case OP_JIF:{
                int16_t jmp_value = read_i16(vm);
                Value value = pop(vm);

                if(!IS_VALUE_BOOL(value)){
                    vm_error(vm, "Expect boolean as conditional value");
                }

                peek_frame(vm)->ip += VALUE_TO_BOOL(value) ? 0 : jmp_value;

                break;
            }case OP_JIT:{
                int16_t jmp_value = read_i16(vm);
                Value value = pop(vm);

                if(!IS_VALUE_BOOL(value)){
                    vm_error(vm, "Expect boolean as conditional value");
                }

                peek_frame(vm)->ip += VALUE_TO_BOOL(value) ? jmp_value : 0;

                break;
            }case OP_IMPORT:{
                size_t module_name_len = 0;
                char *module_name = read_str_const(vm, &module_name_len);
                LZOHTable *globals = VM_CURRENT_GLOBALS(vm);
                GlobalValue *global_value = NULL;

                if(!lzohtable_lookup(module_name_len, module_name, globals, (void **)&global_value)){
                    internal_error(vm, "OP_IMPORT expect to import module in globals");
                }

                Value module_value = global_value->value;

                if(!is_value_module(module_value)){
                    internal_error(vm, "OP_IMPORT expect module, but got something else");
                }

                Module *module = OBJ_TO_MODULE(VALUE_TO_OBJ(module_value))->module;

                if(module->context->resolved){
                    break;
                }

                Fn *entry_fn = module->context->entry_fn;

                push_fn(vm, entry_fn);
                call_fn(0, entry_fn, vm);

                break;
            }case OP_CALL:{
                uint8_t args_count = advance(vm);
                Value callable_value = peek_at(vm, args_count);

                if(!IS_VALUE_OBJ(callable_value)){
                    vm_error(vm, "Target is not callable");
                }

                Obj *callable_obj = VALUE_TO_OBJ(callable_value);

                switch (callable_obj->type){
                    case NATIVE_FN_OBJ_TYPE:{
                        NativeFnObj *native_fn_obj = OBJ_TO_NATIVE_FN(callable_obj);
                        NativeFn *native_fn = native_fn_obj->native_fn;
                        Value target = native_fn_obj->target;

                        if(native_fn->arity != args_count){
                            vm_error(
                                vm,
                                "Failed to call native function '%s'. Declared with %d parameter(s), but got %d argument(s)",
                                native_fn->name,
                                native_fn->arity,
                                args_count
                            );
                        }

                        Value *stack_at_args = peek_at_ptr(vm, args_count);
                        Value return_value = native_fn->raw_fn(
                            vm,
                            args_count,
                            args_count == 0 ? NULL : stack_at_args + 1,
                            target
                        );

                        vm->stack_top = stack_at_args;

                        push(vm, return_value);

                        break;
                    }case FN_OBJ_TYPE:{
                        const Fn *fn = OBJ_TO_FN(callable_obj)->fn;

                        call_fn(args_count, fn, vm);

                        break;
                    }case CLOSURE_OBJ_TYPE:{
                        call_closure(args_count, VALUE_TO_CLOSURE(callable_value)->closure, vm);

                        break;
                    }default:{
                        vm_error(vm, "Target is not callable");

                        break;
                    }
                }

                break;
            }case OP_ACCESS:{
                Value target_value = peek(vm);

                if(!IS_VALUE_OBJ(target_value)){
                    vm_error(vm, "Target is not accessable");
                }

                size_t key_size;
                char *key = read_str_const(vm, &key_size);
                Obj *target_obj = VALUE_TO_OBJ(target_value);

                switch(target_obj->type){
                    case STR_OBJ_TYPE:{
                        NativeFn *native_fn = native_str_get(key_size, key, vm);

                        if(native_fn){
                            NativeFnObj *native_fn_obj = vm_create_native_fn(vm, target_value, native_fn);

                            pop(vm);
                            PUSH_OBJ(vm, native_fn_obj);

                            break;
                        }

                        vm_error(
                            vm,
                            "Target does not contain symbol '%s'",
                            key
                        );

                        break;
                    }case ARRAY_OBJ_TYPE:{
                        NativeFn *native_fn = native_module_array_get(key_size, key, vm);

                        if(native_fn){
                            NativeFnObj *native_fn_obj = vm_create_native_fn(vm, target_value, native_fn);

                            pop(vm);
                            PUSH_OBJ(vm, native_fn_obj);

                            break;
                        }

                        vm_error(
                            vm,
                            "Target does not contain symbol '%s'",
                            key
                        );

                        break;
                    }case LIST_OBJ_TYPE:{
                        NativeFn *native_fn = native_list_get(key_size, key, vm);

                        if(native_fn){
                            NativeFnObj *native_fn_obj = vm_create_native_fn(vm, target_value, native_fn);

                            pop(vm);
                            PUSH_OBJ(vm, native_fn_obj);

                            break;
                        }

                        vm_error(
                            vm,
                            "Target does not contain symbol '%s'",
                            key
                        );

                        break;
                    }case DICT_OBJ_TYPE:{
                        NativeFn *native_fn = native_dict_get(key_size, key, vm);

                        if(native_fn){
                            NativeFnObj *native_fn_obj = vm_create_native_fn(vm, target_value, native_fn);

                            pop(vm);
                            PUSH_OBJ(vm, native_fn_obj);

                            break;
                        }

                        vm_error(
                            vm,
                            "Target does not contain symbol '%s'",
                            key
                        );

                        break;
                    }case RECORD_OBJ_TYPE:{
                        RecordObj *record_obj = OBJ_TO_RECORD(target_obj);
                        Value out_value = vm_record_get_attr(vm, key_size, key, record_obj);

                        pop(vm);
                        push(vm, out_value);

                        break;
                    }case NATIVE_MODULE_OBJ_TYPE:{
                        NativeModuleObj *native_module_obj = OBJ_TO_NATIVE_MODULE(target_obj);
                        NativeModule *native_module = native_module_obj->native_module;
                        Value *value = NULL;

                        if(!lzohtable_lookup(
                            key_size,
                            key,
                            native_module->symbols,
                            (void **)(&value))
                        ){
                            vm_error(
                                vm,
                                "Native module '%s' does not contain symbol '%s'",
                                native_module->name, key
                            );
                        }

                        pop(vm);
                        push(vm, *value);

                        break;
                    }case MODULE_OBJ_TYPE:{
                        ModuleObj *module_obj = OBJ_TO_MODULE(target_obj);
                        Module *module = module_obj->module;
                        GlobalValue *global_value = NULL;

                        if(!lzohtable_lookup(
                            key_size,
                            key,
                            module->context->globals,
                            (void **)(&global_value)
                        )){
                            vm_error(
                                vm,
                                "Module '%s' does not have '%s' symbol",
                                module->name,
                                key
                            );
                        }

                        if(global_value->access == PRIVATE_GLOBAL_VALUE_TYPE){
                            vm_error(
                                vm,
                                "Symbol '%s' in module '%s' is private",
                                key,
                                module->name
                            );
                        }

                        pop(vm);
                        push(vm, global_value->value);

                        break;
                    }case NATIVE_OBJ_TYPE:{
                        NativeObj *native_obj = OBJ_TO_NATIVE(target_obj);
                        Value property = native_property(vm->native_context, native_obj, key_size, key);

                        pop(vm);
                        push(vm, property);

                        break;
                    }default:{
                        vm_error(vm, "Illegal access target");

                        break;
                    }
                }

                break;
            }case OP_INDEX:{
                Value target_value = peek_at(vm, 0);
                Value idx_value = peek_at(vm, 1);
                Value out_value = {0};

                if(!IS_VALUE_OBJ(target_value)){
                	vm_error(vm, "Expect object");
                }

                Obj *target_obj = VALUE_TO_OBJ(target_value);

                switch (target_obj->type) {
                	case STR_OBJ_TYPE:{
	                    if(!IS_VALUE_INT(idx_value)){
	                        vm_error(vm, "Expect positive integer as index");
	                    }

	                    int64_t idx = VALUE_TO_INT(idx_value);
	                    StrObj *old_str_obj = VALUE_TO_STR(target_value);
	                    StrObj *new_str_obj = vm_str_char(vm, idx, old_str_obj);

                        out_value = OBJ_VALUE(new_str_obj);

                  		break;
                    }case ARRAY_OBJ_TYPE:{
		                if(!IS_VALUE_INT(idx_value)){
		                    vm_error(vm, "Expect positive integer as index");
		                }

		                int64_t idx = VALUE_TO_INT(idx_value);
		                ArrayObj *array_obj = VALUE_TO_ARRAY(target_value);

                        out_value = vm_array_get_at(vm, idx, array_obj);

               			break;
                 	}case LIST_OBJ_TYPE:{
		                if(!IS_VALUE_INT(idx_value)){
			                vm_error(vm, "Expect positive integer as index");
		                }

		                int64_t idx = VALUE_TO_INT(idx_value);
		                ListObj *list_obj = VALUE_TO_LIST(target_value);

                        out_value = vm_list_get_at(vm, idx, list_obj);

                  		break;
                  	}case DICT_OBJ_TYPE:{
	                    DictObj *dict_obj = VALUE_TO_DICT(target_value);

                        out_value = vm_dict_get(vm, idx_value, dict_obj);

                   		break;
                   	}case NATIVE_OBJ_TYPE:{
                        NativeContext *context = vm->native_context;
                   		NativeObj *native_obj = OBJ_TO_NATIVE(target_obj);
                        native_t native = *(native_t *)native_obj->raw_native;
                        NativeAccessIdxGet access_fn = native_type_access_idx_get_fn(context, native);

                        if(access_fn){
                            if(!IS_VALUE_INT(idx_value)){
		                        vm_error(vm, "Expect 'INT' as index");
	                       	}

                            out_value = access_fn(context, native_obj->raw_native, idx_value);

                            break;
                        }

                        vm_error(vm, "Illegal target to index");

                  		break;
                    }default:{
                    	vm_error(vm, "Illegal target to index");

                        break;
                    }
                }

                pop(vm);
                pop(vm);
                push(vm, out_value);

                break;
            }case OP_EXIT:{
                Frame *frame = peek_frame(vm);
                ModuleContext *context = frame->fn->module->context;

                if(!context->resolved){
                    context->resolved = 1;
                }

                vm->stack_top = frame->locals;

                pop_frame(vm);

                if(vm->frame_top == vm->frame_stack){
                    return vm->exit_code;
                }

                break;
            }case OP_RET:{
                Value result_value = pop(vm);
                Frame *frame = peek_frame(vm);
                ShadowFrame *shadow_frame = peek_shadow_frame(vm);
                ForeignValue *foreign = shadow_frame->foreigns_head;

                while (foreign){
                    foreign->value = clone_value(vm, *foreign->value);
                    foreign = remove_value_from_current_frame(vm, foreign);
                }

                vm->stack_top = frame->locals;

                pop_frame(vm);
                push(vm, result_value);

                if(vm->frame_top == vm->frame_stack){
                    return vm->exit_code;
                }

                break;
            }case OP_IS:{
                Value value = pop(vm);
                uint8_t type = advance(vm);

                if(IS_VALUE_OBJ(value)){
                    Obj *obj = VALUE_TO_OBJ(value);

                    switch(obj->type){
                        case STR_OBJ_TYPE:{
                            PUSH_BOOL(vm, type == 4);

                            break;
                        }case ARRAY_OBJ_TYPE:{
                            PUSH_BOOL(vm, type == 5);

                            break;
                        }case LIST_OBJ_TYPE:{
                            PUSH_BOOL(vm, type == 6);

                            break;
                        }case DICT_OBJ_TYPE:{
                            PUSH_BOOL(vm, type == 7);

                            break;
                        }case RECORD_OBJ_TYPE:{
                            PUSH_BOOL(vm, type == 8);

                            break;
                        }case NATIVE_FN_OBJ_TYPE:
                         case FN_OBJ_TYPE:
                         case CLOSURE_OBJ_TYPE:{
                            PUSH_BOOL(vm, type == 9);

                            break;
                        }default:{
                            internal_error(vm, "Illegal object type");

                            break;
                        }
                    }
                }else{
                    switch(value.type){
                        case EMPTY_VALUE_TYPE:{
                            PUSH_BOOL(vm, type == 0);

                            break;
                        }case BOOL_VALUE_TYPE:{
                            PUSH_BOOL(vm, type == 1);

                            break;
                        }case INT_VALUE_TYPE:{
                            PUSH_BOOL(vm, type == 2);

                            break;
                        }case FLOAT_VALUE_TYPE:{
                            PUSH_BOOL(vm, type == 3);

                            break;
                        }default:{
                            internal_error(vm, "Illegal value type");

                            break;
                        }
                    }
                }

                break;
            }case OP_TRY_IN:{
                size_t catch_offset = (size_t)read_i16(vm);
                Try *try = vm->try_top++;

                if(try >= vm->try_stack + TRIES_LENGTH){
                    vm_error(vm, "Try/catch stack is full");
                }

                try->catch_offset = peek_frame(vm)->ip + catch_offset;
                try->stack_top = vm->stack_top;
                try->frame = vm->frame_top;

                break;
            }case OP_TRY_OUT:{
                Try *try = --vm->try_top;

                if(try < vm->try_stack){
                    internal_error(vm, "Try stack is empty");
                }

                break;
            }case OP_THROW:{
                Value error_description = pop(vm);

                vm_throw(vm, error_description);

                break;
            }case OP_HALT:{
                return 0;
            }default:{
                assert("Illegal opcode");

                break;
            }
        }
    }

    return vm->exit_code;
}
//> PRIVATE IMPLEMENTATION
//> PUBLIC IMPLEMENTATION
VM *vm_create(Allocator *allocator){
    DynArr *natives_info = MEMORY_DYNARR_TYPE(allocator, NativeInfo);
    NativeContext *native_context = MEMORY_ALLOC(allocator, NativeContext, 1);
    LZOHTable *runtime_strs = MEMORY_LZOHTABLE(allocator);
    DynArr *native_symbols = MEMORY_DYNARR_PTR(allocator);
    VM *vm = MEMORY_ALLOC(allocator, VM, 1);

    MEMORY_CHECK(natives_info);
    MEMORY_CHECK(native_context);
    MEMORY_CHECK(runtime_strs);
    MEMORY_CHECK(native_symbols);
    MEMORY_CHECK(vm);

    goto OK;

ERROR:
    dynarr_destroy(natives_info);
    MEMORY_DEALLOC(allocator, NativeContext, 1, native_context);
    LZOHTABLE_DESTROY(runtime_strs);
    dynarr_destroy(native_symbols);
    MEMORY_DEALLOC(allocator, VM, 1, vm);

OK:
    *native_context = (NativeContext){
        .vm = vm,
        .natives_info = natives_info
    };

    *vm = (VM){
        .runtime_strs = runtime_strs,
        .native_symbols = native_symbols,
        .mem_use_limit = ALLOCATE_START_LIMIT,
        .allocator = allocator
    };

    return vm;
}

void vm_destroy(VM *vm){
    if(!vm){
        return;
    }

    NativeContext *native_context = vm->native_context;
    LZOHTable *runtime_strs = vm->runtime_strs;
    DynArr *native_symbols = vm->native_symbols;
    const Allocator *allocator = vm->allocator;

    const size_t native_symbols_len = dynarr_len(native_symbols);

    for (size_t i = 0; i < native_symbols_len; i++){
        LZOHTable *symbols = (LZOHTable *)dynarr_get_ptr(native_symbols, i);

        LZOHTABLE_DESTROY(symbols);
    }

    dynarr_destroy(native_context->natives_info);
    MEMORY_DEALLOC(allocator, NativeContext, 1, native_context);
    LZOHTABLE_DESTROY(runtime_strs);
    dynarr_destroy(native_symbols);
    MEMORY_DEALLOC(allocator, VM, 1, vm);
}

void vm_initialize(VM *vm){
    vm->exit_code = OK_VM_RESULT;

    vm->stack_top = vm->value_stack;
    vm->frame_top = vm->frame_stack;
    vm->shadow_frame_top = vm->shadow_frame_stack;
    vm->try_top = vm->try_stack;

    vm->white_objs = (ObjList){0};
    vm->gray_objs  = (ObjList){0};
    vm->black_objs = (ObjList){0};
    vm->templates  = NULL;

    MEMORY_INIT_ALLOCATOR(vm, vm_alloc, vm_realloc, vm_dealloc, vm_allocator(vm));

    vm->objs_pools = MEMORY_DYNARR_PTR(vm_allocator(vm));

    vm->exceptions_unit_id         = vm_unit_reservate(vm, sizeof(Try));
    vm->values_unit_id             = vm_unit_reservate(vm, sizeof(Value));
    vm->str_objs_unit_id           = vm_unit_reservate(vm, sizeof(StrObj));
    vm->array_objs_unit_id         = vm_unit_reservate(vm, sizeof(ArrayObj));
    vm->list_objs_unit_id          = vm_unit_reservate(vm, sizeof(ListObj));
    vm->dict_objs_unit_id          = vm_unit_reservate(vm, sizeof(DictObj));
    vm->record_objs_unit_id        = vm_unit_reservate(vm, sizeof(RecordObj));
    vm->native_objs_unit_id        = vm_unit_reservate(vm, sizeof(NativeObj));
    vm->fn_objs_unit_id            = vm_unit_reservate(vm, sizeof(FnObj));
    vm->native_fn_objs_unit_id     = vm_unit_reservate(vm, sizeof(NativeFnObj));
    vm->closures_unit_id           = vm_unit_reservate(vm, sizeof(Closure));
    vm->closure_objs_unit_id       = vm_unit_reservate(vm, sizeof(ClosureObj));
    vm->native_module_objs_unit_id = vm_unit_reservate(vm, sizeof(NativeModuleObj));
    vm->module_objs_unit_id        = vm_unit_reservate(vm, sizeof(ModuleObj));

    vm->native_context = MEMORY_ALLOC(vm_allocator(vm), NativeContext, 1);
    *vm->native_context = (NativeContext){
        .natives_info = DYNARR_CREATE_TYPE(vm_allocator(vm), NativeInfo),
        .vm           = vm
    };
}

int vm_execute(LZOHTable *native_fns, Module *module, VM *vm){
    switch (setjmp(vm->exit_jmp)){
        case 0:{
            ModuleContext *context = module->context;
            Fn *entry_fn = (Fn *)context->entry_fn;

            context->resolved = 1;

            vm->native_fns = native_fns;
            vm->main_module = module;

            push_fn(vm, entry_fn);
            call_fn(0, entry_fn, vm);

            return execute(vm);
        }case 1:{
            return vm->exit_code;
        }default:{
            assert(0 && "Illegal jump value");

            break;
        }
    }

    return -1;
}

void vm_error(VM *vm, const char *msg, ...){
    LZBStr *stacktrace_lzbstr = MEMORY_LZBSTR(vm->allocator);
    int print_stacktrace = stacktrace_lzbstr && !prepare_stacktrace_new(vm, stacktrace_lzbstr, 4);

    va_list args;
	va_start(args, msg);

    fprintf(stderr, "Runtime error: ");
	vfprintf(stderr, msg, args);
    fprintf(stderr, "\n");

    va_end(args);

    if(print_stacktrace){
        fprintf(stderr, "%s", lzbstr_value(stacktrace_lzbstr));
    }else{
        fprintf(stderr, "FAILED TO CREATE STACKTRACE\n");
    }

    lzbstr_destroy(stacktrace_lzbstr);

    vm->exit_code = ERR_VM_RESULT;

    longjmp(vm->exit_jmp, 1);
}

void vm_throw(VM *vm, Value value){
    if(vm->try_top > vm->try_stack){
        Try *try = --vm->try_top;

        vm->stack_top = try->stack_top;
        vm->frame_top = try->frame;

        Frame *frame = peek_frame(vm);

        frame->ip = try->catch_offset;
        frame->prev_ip = try->catch_offset;

        push(vm, value);

        return;
    }

    char *str_value = vm_utils_value_to_str(vm, value, NULL);

    vm_error(vm, str_value);
}

void vm_throw_raw(VM *vm, const char *format, ...){
    va_list args;
    va_start(args, format);

    int printed_count = vsnprintf(NULL, 0, format, args);

    va_end(args);

    if(printed_count < 0){
        internal_error(vm, "IO error while trying to throw an error");
    }

    const Allocator *allocator = vm_allocator(vm);
    size_t raw_str_len = (size_t)printed_count;
    char *raw_str = MEMORY_ALLOC(allocator, char, raw_str_len + 1);
    StrObj *str_obj = NULL;

    va_start(args, format);
    vsnprintf(raw_str, raw_str_len + 1, format, args);
    va_end(args);

    if(vm_create_str(vm, 1, raw_str_len, raw_str, &str_obj)){
        MEMORY_DEALLOC(allocator, char, raw_str_len, raw_str);
    }

    if(vm->try_top > vm->try_stack){
        Try *try = --vm->try_top;

        vm->stack_top = try->stack_top;
        vm->frame_top = try->frame;

        Frame *frame = peek_frame(vm);

        frame->ip = try->catch_offset;
        frame->prev_ip = try->catch_offset;

        push(vm, OBJ_VALUE(str_obj));

        return;
    }

    vm_error(vm, "Uncatched Exception\n\t%s", raw_str);
}

inline void vm_gc(VM *vm){
    prepare_worklist(vm);
    mark_objs(vm);
    sweep_objs(vm);
    normalize_objs(vm);
}

inline ObjList *vm_white_objs(VM *vm){
    return &vm->white_objs;
}

inline ObjList *vm_gray_objs(VM *vm){
    return &vm->gray_objs;
}

inline ObjList *vm_black_objs(VM *vm){
    return &vm->black_objs;
}

inline Allocator *vm_allocator(VM *vm){
	return &vm->front_allocator;
}

inline NativeContext *vm_native_context(VM *vm){
    return vm->native_context;
}

inline size_t vm_unit_reservate(VM *vm, size_t size){
	DynArr *pools = vm->objs_pools;
    LZPool *pool = lzpool_create((LZPoolAllocator *)(&vm->front_allocator), size);

    dynarr_insert_ptr(pools, pool);

    return dynarr_len(pools) - 1;
}

inline void *vm_unit_alloc(VM *vm, size_t id){
	DynArr *pools = vm->objs_pools;

    assert(id < dynarr_len(pools));

    LZPool *pool = dynarr_get_ptr(pools, id);

    if(lzpool_subpools_count(pool) > 0 &&
       lzpool_available_slots_count(pool) == 0
    ){
        vm_gc(vm);
    }

    return lzpool_alloc_backup_size(pool, 4096);
}

inline void vm_unit_dealloc(void *ptr){
	lzpool_dealloc(ptr);
}

inline void vm_halt(VM *vm){
    longjmp(vm->exit_jmp, 1);
}

inline void vm_exit(VM *vm, unsigned char code){
    vm->exit_code = code;

    longjmp(vm->exit_jmp, 1);
}

inline void vm_add_native_symbols(VM *vm, LZOHTable *symbols){
    dynarr_insert_ptr(vm->native_symbols, symbols);
}

inline int vm_create_str(VM *vm, char runtime, size_t raw_str_len, char *raw_str, StrObj **out_str_obj){
    size_t len = raw_str_len == 0 ? 1 : raw_str_len;
    LZOHTable *runtime_strs = vm->runtime_strs;
    StrObj *str_obj = NULL;

    if(lzohtable_lookup(
        len,
        raw_str,
        runtime_strs,
        (void **)&str_obj)
    ){
        *out_str_obj = str_obj;

        return 1;
    }

    str_obj = alloc_str_obj_unit(vm);

    init_obj(vm, (Obj *)str_obj, STR_OBJ_TYPE);

    str_obj->runtime = runtime;
    str_obj->len = raw_str_len;
    str_obj->buff = raw_str;

    lzohtable_put(
        len,
        raw_str,
        str_obj,
        runtime_strs,
        NULL
    );

    *out_str_obj = str_obj;

    return 0;
}

StrObj *vm_create_str_raw(VM *vm, const char *format, ...){
    va_list args;
    va_start(args, format);

    int printed_count = vsnprintf(NULL, 0, format, args);

    va_end(args);

    if(printed_count >= 0){
        const Allocator *allocator = vm_allocator(vm);
        size_t raw_str_len = (size_t)printed_count;
        char *raw_str = MEMORY_ALLOC(allocator, char, raw_str_len + 1);
        StrObj *str_obj = NULL;

        va_start(args, format);
        vsnprintf(raw_str, raw_str_len + 1, format, args);
        va_end(args);

        if(vm_create_str(vm, 1, raw_str_len, raw_str, &str_obj)){
            MEMORY_DEALLOC(allocator, char, raw_str_len, raw_str);
        }

        return str_obj;
    }

    internal_error(vm, "IO error");

    return NULL;
}

int vm_str_is_int(StrObj *str_obj){
    size_t len = str_obj->len;
    char *buff = str_obj->buff;

    if(len == 0){
        return 0;
    }

    if(buff[0] == '-' && len == 1){
        return 0;
    }

    for (size_t i = buff[0] == '-' ? 1 : 0; i < len; i++){
        char c = buff[i];

        if(c < '0' || c > '9'){
            return 0;
        }
    }

    return 1;
}

int vm_str_is_float(StrObj *str_obj){
    size_t len = str_obj->len;
    char *buff = str_obj->buff;

    if(len == 0){
        return 0;
    }

    char ndot = 1;
    char is_negative = buff[0] == '-';
    size_t dot_from = is_negative ? 2 : 1;

    if(is_negative && len == 1){
        return 0;
    }

    for (size_t i = is_negative ? 1 : 0; i < len; i++){
        char c = buff[i];

        if(c == '.' && i >= dot_from && ndot){
            ndot = 0;
            continue;
        }

        if(c < '0' || c > '9'){
            return 0;
        }
    }

    return 1;
}

inline int64_t vm_str_len(StrObj *str_obj){
    return (int64_t)(str_obj->len);
}

inline StrObj *vm_str_char(VM *vm, int64_t idx, StrObj *str_obj){
    size_t len = str_obj->len;
    char *buff = str_obj->buff;

    StrObj *char_str_obj = NULL;
    char *new_buff = MEMORY_ALLOC(vm_allocator(vm), char, 2);

    new_buff[0] = buff[vm_utils_validate_idx(vm, len, idx)];
    new_buff[1] = 0;

    if(lzohtable_lookup(
        1,
        new_buff,
        vm->runtime_strs,
        (void **)(&char_str_obj))
    ){
        MEMORY_DEALLOC(vm_allocator(vm), char, 2, new_buff);
        return char_str_obj;
    }

    vm_create_str(vm, 1, 1, new_buff, &char_str_obj);

    return char_str_obj;
}

inline int64_t vm_str_code(VM *vm, int64_t idx, StrObj *str_obj){
    size_t len = str_obj->len;
    char *buff = str_obj->buff;

    return (int64_t)buff[vm_utils_validate_idx(vm, len, idx)];
}

inline StrObj *vm_str_concat(VM *vm, StrObj *a_str_obj, StrObj *b_str_obj){
    size_t a_len = a_str_obj->len;
    size_t b_len = b_str_obj->len;
    size_t new_len = a_len + b_len;
    char *new_buff = MEMORY_ALLOC(vm_allocator(vm), char, new_len + 1);
    StrObj *new_str_obj = NULL;

    memcpy(new_buff, a_str_obj->buff, a_len);
    memcpy(new_buff + a_len, b_str_obj->buff, b_len);

    new_buff[new_len] = 0;

    if(vm_create_str(vm, 1, new_len, new_buff, &new_str_obj)){
        MEMORY_DEALLOC(vm_allocator(vm), char, new_len + 1, new_buff);
        return new_str_obj;
    }

    return new_str_obj;
}

inline StrObj *vm_str_mul(VM *vm, int64_t by, StrObj *str_obj){
    if(by < 0){
        vm_error(
            vm,
            "Failed to multiply string: factor value (%"PRId64") is negative",
            by
        );
    }

    size_t old_len = str_obj->len;
    size_t new_len = old_len * by;
    char *new_buff = MEMORY_ALLOC(vm_allocator(vm), char, new_len + 1);
    StrObj *new_str_obj = NULL;

    for (size_t i = 0; i < new_len; i += old_len){
        memcpy(new_buff + i, str_obj->buff, old_len);
    }

    new_buff[new_len] = 0;

    if(vm_create_str(vm, 1, new_len, new_buff, &new_str_obj)){
        MEMORY_DEALLOC(vm_allocator(vm), char, new_len + 1, new_buff);
    }

    return new_str_obj;
}

StrObj *vm_str_insert_at(VM *vm, int64_t idx, StrObj *a_str_obj, StrObj *b_str_obj){
    if(idx < 0){
        vm_error(
            vm,
            "Failed to insert string: 'at' index %" PRId64 " is negative",
            idx
        );
    }

    size_t at = (size_t)idx;
    size_t a_len = a_str_obj->len;
    size_t b_len = b_str_obj->len;
    char *a_buff = a_str_obj->buff;
    char *b_buff = b_str_obj->buff;

    if(at > a_len){
        vm_error(
            vm,
            "Failed to insert string: 'at' index (%zu) pass string length (%zu)",
            at,
            a_len
        );
    }

    size_t c_len = a_len + b_len;
    char *c_buff = MEMORY_ALLOC(vm_allocator(vm), char, c_len + 1);
    StrObj *c_str_obj = NULL;

    if(at < a_len){
        memcpy(c_buff, a_buff, at);
        memcpy(c_buff + at, b_buff, b_len);
        memcpy(c_buff + at + b_len, a_buff + at, a_len - at);
    }else{
        memcpy(c_buff, a_buff, a_len);
        memcpy(c_buff + a_len, b_buff, b_len);
    }

    c_buff[c_len] = 0;

    if(lzohtable_lookup(
        c_len,
        c_buff,
        vm->runtime_strs,
        (void **)(&c_str_obj))
    ){
        MEMORY_DEALLOC(vm_allocator(vm), char, c_len + 1, c_buff);
        return c_str_obj;
    }

    vm_create_str(vm, 1, c_len, c_buff, &c_str_obj);

    return c_str_obj;
}

StrObj *vm_str_remove(VM *vm, int64_t from, int64_t to, StrObj *str_obj){
    if(from < 0){
        vm_error(
            vm,
            "Failed to remove string: 'from' index %" PRId64 " is negative",
            from
        );
    }

    if(from >= to){
        vm_error(
            vm,
            "Failed to remove string: 'from' index %" PRId64 " is equals or bigger than 'to' index %" PRId64,
            from,
            to
        );
    }

    size_t start = (size_t)from;
    size_t end = (size_t)to;
    size_t old_len = str_obj->len;
    char *old_buff = str_obj->buff;

    if(end > old_len){
        vm_error(
            vm,
            "Failed to remove string: 'to' index (%zu) pass string length (%zu)",
            end,
            old_len
        );
    }

    size_t new_len = old_len - (end - start);
    char *new_buff = MEMORY_ALLOC(vm_allocator(vm), char, new_len + 1);
    size_t left_len = start;
    size_t right_len = old_len - end;
    StrObj *new_str_obj = NULL;

    memcpy(new_buff, old_buff, left_len);
    memcpy(new_buff + left_len, old_buff + end, right_len);

    new_buff[new_len] = 0;

    if(lzohtable_lookup(
        new_len,
        new_buff,
        vm->runtime_strs,
        (void **)(&str_obj))
    ){
        MEMORY_DEALLOC(vm_allocator(vm), char, new_len + 1, new_buff);
        return str_obj;
    }

    vm_create_str(vm, 1, new_len, new_buff, &new_str_obj);

    return new_str_obj;
}

StrObj *vm_str_sub_str(VM *vm, int64_t from, int64_t to, StrObj *str_obj){
    if(from < 0){
        vm_error(
            vm,
            "Failed to sub-string string: 'from' index %" PRId64 " is negative",
            from
        );
    }

    if(from >= to){
        vm_error(
            vm,
            "Failed to sub-string string: 'from' index %" PRId64 " is equals or bigger than 'to' index %" PRId64,
            from,
            to
        );
    }

    size_t start = (size_t)from;
    size_t end = (size_t)to;
    size_t old_len = str_obj->len;
    char *old_buff = str_obj->buff;

    if(end > old_len){
        vm_error(
            vm,
            "Failed to sub-string string: 'to' index (%zu) pass string length (%zu)",
            end,
            old_len
        );
    }

    size_t new_len = end - start;
    char *new_buff = MEMORY_ALLOC(vm_allocator(vm), char, new_len + 1);
    StrObj *new_str_obj = NULL;

    memcpy(new_buff, old_buff + start, new_len);

    new_buff[new_len] = 0;

    if(vm_create_str(vm, 1, new_len, new_buff, &new_str_obj)){
        MEMORY_DEALLOC(vm_allocator(vm), char, new_len + 1, new_buff);
    }

    return new_str_obj;
}

inline ArrayObj *vm_create_array(VM *vm, int64_t len){
    Value *values = MEMORY_ALLOC(vm_allocator(vm), Value, (size_t)len);
    ArrayObj *array_obj = alloc_array_obj_unit(vm);

    memset(values, 0, VALUE_SIZE * (size_t)len);
    init_obj(vm, (Obj *)array_obj, ARRAY_OBJ_TYPE);

    array_obj->len = len;
    array_obj->values = values;

    return array_obj;
}

inline int64_t vm_array_len(ArrayObj *array_obj){
    return (int64_t)array_obj->len;
}

inline Value vm_array_get_at(VM *vm, int64_t idx, ArrayObj *array_obj){
    size_t len = array_obj->len;
    Value *values = array_obj->values;

    return values[vm_utils_validate_idx(vm, len, idx)];
}

inline void vm_array_set_at(VM *vm, int64_t idx, Value value, ArrayObj *array_obj){
    size_t len = array_obj->len;
    Value *values = array_obj->values;

    values[vm_utils_validate_idx(vm, len, idx)] = value;
}

inline Value vm_array_first(VM *vm, ArrayObj *array_obj){
    size_t len = array_obj->len;
    Value *values = array_obj->values;

    return len == 0 ? EMPTY_VALUE : values[0];
}

inline Value vm_array_last(VM *vm, ArrayObj *array_obj){
    size_t len = array_obj->len;
    Value *values = array_obj->values;

    return len == 0 ? EMPTY_VALUE : values[len - 1];
}

inline ArrayObj *vm_array_grow(VM *vm, int64_t by, ArrayObj *array_obj){
    if(by <= 1){
        vm_error(vm, "Expect 'by' value greater than 1");
    }

    size_t len = array_obj->len;
    Value *values = array_obj->values;

    size_t new_len = len * by;
    Value *new_values = MEMORY_ALLOC(vm_allocator(vm), Value, new_len);
    ArrayObj *new_array_obj = alloc_array_obj_unit(vm);

    memcpy(new_values, values, VALUE_SIZE * len);
    memset(new_values + len, 0, VALUE_SIZE * len);
    init_obj(vm, (Obj *)new_array_obj, ARRAY_OBJ_TYPE);

    new_array_obj->len = new_len;
    new_array_obj->values = new_values;

    return new_array_obj;
}

inline ArrayObj *vm_array_join(VM *vm, ArrayObj *a_array_obj, ArrayObj *b_array_obj){
    size_t a_len = a_array_obj->len;
    Value *a_values = a_array_obj->values;

    size_t b_len = b_array_obj->len;
    Value *b_values = b_array_obj->values;

    size_t new_len = a_len + b_len;
    Value *new_values = MEMORY_ALLOC(vm_allocator(vm), Value, new_len);
    ArrayObj *new_array_obj = alloc_array_obj_unit(vm);

    memmove(new_values, a_values, VALUE_SIZE * a_len);
    memmove(new_values + a_len, b_values, VALUE_SIZE * b_len);
    init_obj(vm, (Obj *)new_array_obj, ARRAY_OBJ_TYPE);

    new_array_obj->len = new_len;
    new_array_obj->values = new_values;

    return new_array_obj;
}

inline ArrayObj *vm_array_join_value(VM *vm, Value value, ArrayObj *array_obj){
    size_t len = array_obj->len;
    Value *values = array_obj->values;
    ArrayObj *new_array_obj = alloc_array_obj_unit(vm);
    Value *new_values = new_array_obj->values;

    memcpy(new_values, values, VALUE_SIZE * len);

    new_values[len] = value;

    return new_array_obj;
}

inline ListObj *vm_create_list(VM *vm){
    DynArr *values = MEMORY_DYNARR_TYPE(vm_allocator(vm), Value);
    ListObj *list_obj = alloc_list_obj_unit(vm);

    init_obj(vm, (Obj *)list_obj, LIST_OBJ_TYPE);

    list_obj->items = values;

    return list_obj;
}

inline int64_t vm_list_len(ListObj *list_obj){
    return (int64_t)dynarr_len(list_obj->items);
}

inline int64_t vm_list_clear(ListObj *list_obj){
    DynArr *items = list_obj->items;
    int64_t len = (int64_t)dynarr_len(items);

    dynarr_remove_all(items);

    return len;
}

inline ListObj *vm_list_join(VM *vm, ListObj *a_list_obj, ListObj *b_list_obj){
    DynArr *a_list = a_list_obj->items;
    DynArr *b_list = b_list_obj->items;
    DynArr *c_list = NULL;
    ListObj *list_obj = alloc_list_obj_unit(vm);

    dynarr_join((const DynArrAllocator *)vm_allocator(vm), a_list, b_list, &c_list);
    init_obj(vm, (Obj *)list_obj, LIST_OBJ_TYPE);

    list_obj->items = c_list;

    return list_obj;
}

inline Value vm_list_get_at(VM *vm, int64_t idx, ListObj *list_obj){
    if(idx < 0){
        vm_error(vm, "Failed to get item from list: 'at' index is negative");
    }

    size_t at = (size_t)idx;
    DynArr *items = list_obj->items;
    size_t len = dynarr_len(items);

    if(at >= len){
        vm_error(
            vm,
            "Failed to get item from list: 'at' index (%zu) out of bounds",
            at
        );
    }

    return DYNARR_GET_AS(items, Value, (size_t)idx);
}

inline void vm_list_insert(VM *vm, Value value, ListObj *list_obj){
    DynArr *items = list_obj->items;

    dynarr_insert(items, &value);
}

inline ListObj *vm_list_insert_new(VM *vm, Value value, ListObj *list_obj){
    DynArr *items = list_obj->items;
    DynArr *new_items = dynarr_create_by(
        (const DynArrAllocator *)vm_allocator(vm),
        dynarr_item_size(items),
        dynarr_len(items) + 1
    );
    ListObj *new_list_obj = alloc_list_obj_unit(vm);

    dynarr_append(new_items, items);
    dynarr_insert(new_items, &value);
    init_obj(vm, (Obj *)new_list_obj, LIST_OBJ_TYPE);

    new_list_obj->items = new_items;

    return new_list_obj;
}

void vm_list_insert_at(VM *vm, int64_t idx, Value value, ListObj *list_obj){
    if(idx < 0){
        vm_error(vm, "Failed to insert item to list: 'at' index is negative");
    }

    size_t at = (size_t)idx;
    DynArr *items = list_obj->items;
    size_t len = dynarr_len(items);

    if(at > len){
        vm_error(
            vm,
            "Failed to insert item to list: 'at' index (%zu) out of bounds",
            at
        );
    }

    dynarr_insert_at(items, at, &value);
}

inline Value vm_list_set_at(VM *vm, int64_t idx, Value value, ListObj *list_obj){
    if(idx < 0){
        vm_error(vm, "Failed to set item to list: 'at' index is negative");
    }

    size_t at = (size_t)idx;
    DynArr *items = list_obj->items;
    size_t len = dynarr_len(items);

    if(at >= len){
        vm_error(
            vm,
            "Failed to set item to list: 'at' index (%zu) out of bounds",
            at
        );
    }

    Value out_value = DYNARR_GET_AS(items, Value, at);

    dynarr_set_at(items, idx, &value);

    return out_value;
}

inline Value vm_list_remove_at(VM *vm, int64_t idx, ListObj *list_obj){
    if(idx < 0){
        vm_error(vm, "Failed to remove item from list: 'at' index is negative");
    }

    size_t at = (size_t)idx;
    DynArr *items = list_obj->items;
    size_t len = dynarr_len(items);

    if(at >= len){
        vm_error(vm, "Failed to remove item from list: 'at' index out of bounds");
    }

    Value value = DYNARR_GET_AS(items, Value, at);

    dynarr_remove_index(items, at);
    dynarr_reduce(items);

    return value;
}

inline DictObj *vm_create_dict(VM *vm){
    LZOHTable *key_values = MEMORY_LZOHTABLE(vm_allocator(vm));
    DictObj *dict_obj = alloc_dict_obj_unit(vm);

    init_obj(vm, (Obj *)dict_obj, DICT_OBJ_TYPE);

    dict_obj->key_values = key_values;

    return dict_obj;
}

inline void vm_dict_put(VM *vm, Value key, Value value, DictObj *dict_obj){
    if(IS_VALUE_EMPTY(key)){
        vm_error(vm, "Failed to put key into dict: key cannot be 'empty'");
    }

    LZOHTable *keys_values = dict_obj->key_values;
    void *out_value = NULL;

    lzohtable_put_help(
        VALUE_SIZE,
        clone_value(vm, key),
        clone_value(vm, value),
        keys_values,
        &out_value,
        NULL
    );

    if(out_value){
        destroy_value(out_value);
    }
}

inline void vm_dict_put_cstr_value(VM *vm, const char *str, Value value, DictObj *dict_obj){
    size_t str_len;
    char *cloned_str = memory_clone_cstr(vm_allocator(vm), str, &str_len);
    StrObj *key_str_obj = NULL;

    if(vm_create_str(vm, 1, str_len, cloned_str, &key_str_obj)){
        memory_destroy_cstr(vm_allocator(vm), cloned_str);
    }

    void *out_value = NULL;

    lzohtable_put_help(
        VALUE_SIZE,
        clone_value(vm, OBJ_VALUE(key_str_obj)),
        clone_value(vm, value),
        dict_obj->key_values,
        &out_value,
        NULL
    );

    if(out_value){
        destroy_value(out_value);
    }
}

inline int vm_dict_contains(Value key, DictObj *dict_obj){
    return lzohtable_lookup(VALUE_SIZE, &key, dict_obj->key_values, NULL);
}

inline Value vm_dict_get(VM *vm, Value key, DictObj *dict_obj){
    LZOHTable *keys_values = dict_obj->key_values;
    Value *raw_value = NULL;

    if(lzohtable_lookup(
        VALUE_SIZE,
        &key,
        keys_values,
        (void **)(&raw_value))
    ){
        return *raw_value;
    }

    return (Value){0};
}

inline void vm_dict_remove(Value key, DictObj *dict_obj){
    lzohtable_remove_help(
        VALUE_SIZE,
        &key,
        NULL,
        clean_up_dict,
        dict_obj->key_values
    );
}

inline RecordObj *vm_create_record(VM *vm, uint16_t length){
    LZOHTable *attrs = length == 0 ? NULL : MEMORY_LZOHTABLE(vm_allocator(vm));
    RecordObj *record_obj = alloc_record_obj_unit(vm);

    init_obj(vm, (Obj*)record_obj, RECORD_OBJ_TYPE);

    record_obj->attrs = attrs;

	return record_obj;
}

inline void vm_record_insert_attr(VM *vm, size_t key_size, char *key, Value value, RecordObj *record_obj){
    Value *attr_value = NULL;
    LZOHTable *attrs = record_obj->attrs;

    if(!attrs){
        vm_error(vm, "Cannot set attributes on an empty record");
    }

    if(lzohtable_lookup(key_size, key, attrs, (void **)(&attr_value))){
        *attr_value = value;
        return;
    }

    lzohtable_put_ck(
        key_size,
        key,
        clone_value(vm, value),
        attrs,
        NULL
    );
}

inline void vm_record_set_attr(VM *vm, size_t key_size, char *key, Value value, RecordObj *record_obj){
    Value *attr_value = NULL;
    LZOHTable *attrs = record_obj->attrs;

    if(attrs && lzohtable_lookup(
        key_size,
        key,
        attrs,
        (void **)(&attr_value))
    ){
        *attr_value = value;
        return;
    }

    vm_error(
        vm,
        "Failed to update record: attribute '%s' does not exist",
        key
    );
}

inline Value vm_record_get_attr(VM *vm, size_t key_size, char *key, RecordObj *record_obj){
    LZOHTable *attrs = record_obj->attrs;
    Value *out_value = NULL;

    if(attrs && lzohtable_lookup(
        key_size,
        key,
        attrs,
        (void **)(&out_value))
    ){
        return *out_value;
    }

    vm_error(
        vm,
        "Failed to get attribute: record does not contain attribute '%s'",
        key
    );

    return (Value){0};
}

NativeObj *vm_create_native(VM *vm, void *native){
	NativeObj *native_obj = alloc_native_obj_unit(vm);

	init_obj(vm, (Obj *)native_obj, NATIVE_OBJ_TYPE);

    native_obj->raw_native = native;

	return native_obj;
}

inline NativeFnObj *vm_create_native_fn(VM *vm, Value target, NativeFn *native_fn){
    NativeFnObj *native_fn_obj = alloc_native_fn_obj_unit(vm);

    init_obj(vm, (Obj *)native_fn_obj, NATIVE_FN_OBJ_TYPE);

    native_fn_obj->target = target;
    native_fn_obj->native_fn = native_fn;

    return native_fn_obj;
}

inline FnObj *vm_create_fn(VM *vm, Fn *fn){
    FnObj *fn_obj = alloc_fn_obj_unit(vm);

    init_obj(vm, (Obj *)fn_obj, FN_OBJ_TYPE);

    fn_obj->fn = fn;

    return fn_obj;
}

ClosureObj *vm_create_closure(VM *vm, Closure *closure){
    size_t locals_len = closure->locals_len;
    ForeignValue *foreigns = MEMORY_ALLOC(vm_allocator(vm), ForeignValue, locals_len);
    ClosureObj *closure_obj = alloc_closure_obj_unit(vm);

    for (size_t i = 0; i < locals_len; i++){
        ForeignValue *foreign = &foreigns[i];

        *foreign = (ForeignValue){
            .local = -1
        };
    }

    init_obj(vm, (Obj *)closure_obj, CLOSURE_OBJ_TYPE);

    closure->foreigns = foreigns;
    closure_obj->closure = closure;

    return closure_obj;
}

inline NativeModuleObj *vm_create_native_module(VM *vm, NativeModule *native_module){
    NativeModuleObj *native_module_obj = alloc_native_module_obj_unit(vm);

    init_obj(vm, (Obj *)native_module_obj, NATIVE_MODULE_OBJ_TYPE);

    native_module_obj->native_module = native_module;

    return native_module_obj;
}

inline ModuleObj *vm_create_module_obj(VM *vm, Module *module){
    ModuleObj *module_obj = alloc_module_obj_unit(vm);

    init_obj(vm, (Obj *)module_obj, MODULE_OBJ_TYPE);

    module_obj->module = module;

    return module_obj;
}
//< PUBLIC IMPLEMENTATION