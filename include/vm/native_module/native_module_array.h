#ifndef NATIVE_MODULE_ARRAY_H
#define NATIVE_MODULE_ARRAY_H

#include "essentials/memory.h"

#include "types_utils.h"
#include "vm_factory.h"
#include "vm_utils.h"

#include <stdint.h>

static LZOHTable *array_symbols = NULL;

Value native_fn_array_len(VM *vm, uint8_t argsc, Value *values, Value target){
    ArrayObj *target_array_obj = VALUE_TO_ARRAY(target);

    return INT_VALUE(vm_array_len(target_array_obj));
}

Value native_fn_array_make_room(VM *vm, uint8_t argsc, Value *values, Value target){
    ArrayObj *target_array_obj = VALUE_TO_ARRAY(target);
    int64_t by = vm_utils_value_validate_int_arg(vm, values[0], 1, "by");
    ArrayObj *new_array_obj = vm_array_grow(vm, by, target_array_obj);

    return OBJ_VALUE(new_array_obj);
}

Value native_fn_array_to_list(VM *vm, uint8_t argsc, Value *values, Value target){
    ArrayObj *target_array_obj = VALUE_TO_ARRAY(target);
    ListObj *list_obj = vm_create_list(vm);
    size_t len = target_array_obj->len;
    Value *array_values = target_array_obj->values;

    for (size_t i = 0; i < len; i++){
        vm_list_insert(vm, array_values[i], list_obj);
    }

    return OBJ_VALUE(list_obj);
}

Value native_fn_array_first(VM *vm, uint8_t argsc, Value *values, Value target){
    ArrayObj *array_obj = VALUE_TO_ARRAY(target);
    return vm_array_first(vm, array_obj);
}

Value native_fn_array_last(VM *vm, uint8_t argsc, Value *values, Value target){
    ArrayObj *array_obj = VALUE_TO_ARRAY(target);
    return vm_array_last(vm, array_obj);
}

NativeFn *native_module_array_get(size_t key_size, const char *key, VM *vm){
    if(!array_symbols){
        Allocator *allocator = vm_allocator(vm);
        array_symbols = MEMORY_LZOHTABLE(allocator);

        vm_add_native_symbols(vm, array_symbols);

        vm_factory_native_fn_add_info(array_symbols, allocator, "len",     0, native_fn_array_len);
        vm_factory_native_fn_add_info(array_symbols, allocator, "grow",    1, native_fn_array_make_room);
        vm_factory_native_fn_add_info(array_symbols, allocator, "to_list", 0, native_fn_array_to_list);
        vm_factory_native_fn_add_info(array_symbols, allocator, "first",   0, native_fn_array_first);
        vm_factory_native_fn_add_info(array_symbols, allocator, "last",    0, native_fn_array_last);
    }

    NativeFn *native_fn = NULL;
    lzohtable_lookup(key_size, key, array_symbols, (void **)(&native_fn));

    return native_fn;
}

#endif