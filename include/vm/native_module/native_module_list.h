#ifndef NATIVE_MODULE_LIST
#define NATIVE_MODULE_LIST

#include "essentials/memory.h"
#include "vm_utils.h"

static LZOHTable *list_symbols = NULL;

Value native_fn_list_size(VM *vm, uint8_t argsc, Value *values, Value target){
    ListObj *list_obj = VALUE_TO_LIST(target);
    return INT_VALUE(vm_list_len(list_obj));
}

Value native_fn_list_clear(VM *vm, uint8_t argsc, Value *values, Value target){
    ListObj *list_obj = VALUE_TO_LIST(target);
    return INT_VALUE(vm_list_clear(list_obj));
}

Value native_fn_list_to_array(VM *vm, uint8_t argsc, Value *values, Value target){
    ListObj *list_obj = VALUE_TO_LIST(target);
    DynArr *items = list_obj->items;
    size_t len = dynarr_len(items);
    ArrayObj *array_obj = vm_create_array(vm, len);

    for (size_t i = 0; i < len; i++){
        array_obj->values[i] = DYNARR_GET_AS(items, Value, i);
    }

    return OBJ_VALUE(array_obj);
}

Value native_fn_list_first(VM *vm, uint8_t argsc, Value *values, Value target){
    ListObj *list_obj = VALUE_TO_LIST(target);
    DynArr *items = list_obj->items;
    size_t len = dynarr_len(items);

    if(len == 0){
        vm_error(vm, "Failed to get fist list item: list is empty");
    }

    return DYNARR_GET_AS(items, Value, 0);
}

Value native_fn_list_last(VM *vm, uint8_t argsc, Value *values, Value target){
    ListObj *list_obj = VALUE_TO_LIST(target);
    DynArr *items = list_obj->items;
    size_t len = dynarr_len(items);

    if(len == 0){
        vm_error(vm, "Failed to get fist list item: list is empty");
    }

    return DYNARR_GET_AS(items, Value, len - 1);
}

Value native_fn_list_insert(VM *vm, uint8_t argsc, Value *values, Value target){
    ListObj *target_list_obj = VALUE_TO_LIST(target);
    Value value = values[0];

    vm_list_insert(vm, value, target_list_obj);

    return value;
}

Value native_fn_list_insert_at(VM *vm, uint8_t argsc, Value *values, Value target){
    ListObj *target_list_obj = VALUE_TO_LIST(target);
    int64_t at = vm_utils_value_validate_int_arg(vm, values[0], 1, "at");
    Value value = values[1];

    vm_list_insert_at(vm, at, value, target_list_obj);

    return value;
}

Value native_fn_list_remove(VM *vm, uint8_t argsc, Value *values, Value target){
    ListObj *target_list_obj = VALUE_TO_LIST(target);
    int64_t at = vm_utils_value_validate_int_arg(vm, values[0], 1, "at");
    return vm_list_remove_at(vm, at, target_list_obj);
}

NativeFn *native_list_get(size_t key_size, const char *key, VM *vm){
    if(!list_symbols){
        Allocator *allocator = vm_allocator(vm);
        list_symbols = MEMORY_LZOHTABLE(allocator);

        vm_add_native_symbols(vm, list_symbols);

        vm_factory_native_fn_add_info(list_symbols, allocator, "len", 0, native_fn_list_size);
        vm_factory_native_fn_add_info(list_symbols, allocator, "clear", 0, native_fn_list_clear);
        vm_factory_native_fn_add_info(list_symbols, allocator, "to_array", 0, native_fn_list_to_array);
        vm_factory_native_fn_add_info(list_symbols, allocator, "first", 0, native_fn_list_first);
        vm_factory_native_fn_add_info(list_symbols, allocator, "last", 0, native_fn_list_last);
        vm_factory_native_fn_add_info(list_symbols, allocator, "insert", 1, native_fn_list_insert);
        vm_factory_native_fn_add_info(list_symbols, allocator, "insert_at", 2, native_fn_list_insert_at);
        vm_factory_native_fn_add_info(list_symbols, allocator, "remove", 1, native_fn_list_remove);
    }

    NativeFn *native_fn = NULL;
    lzohtable_lookup(key_size, key, list_symbols, (void **)(&native_fn));

    return native_fn;
}

#endif