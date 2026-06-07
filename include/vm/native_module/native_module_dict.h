#ifndef NATIVE_MODULE_DICT
#define NATIVE_MODULE_DICT

#include "vm_factory.h"
#include "vm_utils.h"

static LZOHTable *dict_symbols = NULL;

Value native_dict_fn_len(VM *vm, uint8_t argsc, Value *values, Value target){
    DictObj *dict_obj = VALUE_TO_DICT(target);
    LZOHTable *key_values = dict_obj->key_values;
    return INT_VALUE((int64_t)key_values->n);
}

Value native_dict_fn_contains(VM *vm, uint8_t argsc, Value *values, Value target){
    DictObj *dict_obj = VALUE_TO_DICT(target);
    Value key = values[0];
    return BOOL_VALUE((int64_t)vm_dict_contains(key, dict_obj));
}

Value native_dict_fn_clear(VM *vm, uint8_t argsc, Value *values, Value target){
    DictObj *dict_obj = VALUE_TO_DICT(target);
    LZOHTable *key_values = dict_obj->key_values;
    size_t len = key_values->n;

    LZOHTABLE_CLEAR(key_values);

    return INT_VALUE((int64_t)len);
}

Value native_dict_fn_remove(VM *vm, uint8_t argsc, Value *values, Value target){
    DictObj *dict_obj = VALUE_TO_DICT(target);
    Value key = values[0];

    vm_dict_remove(key, dict_obj);

    return EMPTY_VALUE;
}

Value native_dict_fn_pairs(VM *vm, uint8_t argsc, Value *values, Value target){
    DictObj *dict_obj = VALUE_TO_DICT(target);
    LZOHTable *key_values = dict_obj->key_values;

    size_t n = key_values->n;
    size_t m = key_values->m;
    LZOHTableSlot *slots = key_values->slots;

    ArrayObj *pairs_array_obj = vm_create_array(vm, n);

    for (size_t i = 0, o = 0; i < m; i++){
        LZOHTableSlot slot = slots[i];

        if(!slot.used){
            continue;
        }

        RecordObj *pair_record_obj = vm_create_record(vm, 2);

        vm_record_insert_attr(
            vm,
            3,
            "key",
            *((Value *)slot.key),
            pair_record_obj
        );

        vm_record_insert_attr(
            vm,
            5,
            "value",
            *((Value *)slot.value),
            pair_record_obj
        );

        pairs_array_obj->values[o++] = OBJ_VALUE(pair_record_obj);

        if(o >= n){
            break;
        }
    }

    return OBJ_VALUE(pairs_array_obj);
}

NativeFn *native_dict_get(size_t key_size, const char *key, VM *vm){
    if(!dict_symbols){
        Allocator *allocator = vm_allocator(vm);
        dict_symbols = MEMORY_LZOHTABLE(allocator);

        vm_add_native_symbols(vm, dict_symbols);

        vm_factory_native_fn_add_info(dict_symbols, allocator, "len", 0, native_dict_fn_len);
        vm_factory_native_fn_add_info(dict_symbols, allocator, "contains", 1, native_dict_fn_contains);
        vm_factory_native_fn_add_info(dict_symbols, allocator, "clear", 0, native_dict_fn_clear);
        vm_factory_native_fn_add_info(dict_symbols, allocator, "remove", 1, native_dict_fn_remove);
        vm_factory_native_fn_add_info(dict_symbols, allocator, "pairs", 0, native_dict_fn_pairs);
    }

    NativeFn *native_fn = NULL;
    lzohtable_lookup(key_size, key, dict_symbols, (void **)(&native_fn));

    return native_fn;
}

#endif