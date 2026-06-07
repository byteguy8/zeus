#ifndef NATIVE_MODULE_STR
#define NATIVE_MODULE_STR

#include "essentials/memory.h"
#include "vm_factory.h"
#include "vm_utils.h"
#include "types_utils.h"

static LZOHTable *str_symbols = NULL;

Value native_fn_str_len(VM *vm, uint8_t argsc, Value *values, Value target){
	StrObj *target_str = VALUE_TO_STR(target);
	return INT_VALUE(vm_str_len(target_str));
}

Value native_fn_str_code(VM *vm, uint8_t argsc, Value *values, Value target){
	StrObj *target_str = VALUE_TO_STR(target);
    int64_t at = vm_utils_value_validate_int_arg(vm, values[0], 1, "at");
    return INT_VALUE(vm_str_code(vm, at, target_str));
}

Value native_fn_str_insert(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *target_str_obj = VALUE_TO_STR(target);
    int64_t at = vm_utils_value_validate_int_arg(vm, values[0], 1, "at");
    StrObj *str_obj = vm_utils_value_validate_str_arg(vm, values[1], 1, "string");
    StrObj *new_str_obj = vm_str_insert_at(vm, at, target_str_obj, str_obj);

    return OBJ_VALUE(new_str_obj);
}

Value native_fn_str_remove(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *target_str_obj = VALUE_TO_STR(target);
    int64_t from = vm_utils_value_validate_int_arg(vm, values[0], 1, "from");
    int64_t to = vm_utils_value_validate_int_arg(vm, values[1], 1, "to");
    StrObj *new_str_obj = vm_str_remove(vm, from, to, target_str_obj);

    return OBJ_VALUE(new_str_obj);
}

Value native_fn_str_remove_first(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *target_str_obj = VALUE_TO_STR(target);

    if(target_str_obj->len == 0){
        vm_error(vm, "String is empty");
    }

    StrObj *new_str_obj = vm_str_remove(vm, 0, 1, target_str_obj);

    return OBJ_VALUE(new_str_obj);
}

Value native_fn_str_remove_last(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *target_str_obj = VALUE_TO_STR(target);
    size_t len = target_str_obj->len;

    if(target_str_obj->len == 0){
        vm_error(vm, "String is empty");
    }

    StrObj *new_str_obj = vm_str_remove(vm, len - 1, len, target_str_obj);

    return OBJ_VALUE(new_str_obj);
}

Value native_fn_str_substr(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *target_str_obj = VALUE_TO_STR(target);
    int64_t from = vm_utils_value_validate_int_arg(vm, values[0], 1, "from");
    int64_t to = vm_utils_value_validate_int_arg(vm, values[1], 1, "to");
    StrObj *new_str_obj = vm_str_sub_str(vm, from, to, target_str_obj);

    return OBJ_VALUE(new_str_obj);
}

Value native_fn_str_to_bytes(VM *vm, uint8_t argsc, Value *values, Value target){
    StrObj *target_str_obj = VALUE_TO_STR(target);
    NativeArray *native_array = native_array_create(vm_native_context(vm), target_str_obj->len);
    NativeObj *native_obj = vm_create_native(vm, native_array);

    if(native_array->bytes){
        memcpy(native_array->bytes, target_str_obj->buff, sizeof(char) * target_str_obj->len);
    }

    return OBJ_VALUE(native_obj);
}

NativeFn *native_str_get(size_t key_size, const char *key, VM *vm){
    if(!str_symbols){
        Allocator *allocator = vm_allocator(vm);
        str_symbols = MEMORY_LZOHTABLE(allocator);

        vm_add_native_symbols(vm, str_symbols);

        vm_factory_native_fn_add_info(str_symbols, allocator, "len",          0, native_fn_str_len);
        vm_factory_native_fn_add_info(str_symbols, allocator, "code",         1, native_fn_str_code);
        vm_factory_native_fn_add_info(str_symbols, allocator, "insert",       2, native_fn_str_insert);
        vm_factory_native_fn_add_info(str_symbols, allocator, "remove",       2, native_fn_str_remove);
        vm_factory_native_fn_add_info(str_symbols, allocator, "remove_first", 0, native_fn_str_remove_first);
        vm_factory_native_fn_add_info(str_symbols, allocator, "remove_last",  0, native_fn_str_remove_last);
        vm_factory_native_fn_add_info(str_symbols, allocator, "substr",       2, native_fn_str_substr);
        vm_factory_native_fn_add_info(str_symbols, allocator, "to_bytes",     0, native_fn_str_to_bytes);
    }

    NativeFn *native_fn = NULL;

    lzohtable_lookup(key_size, key, str_symbols, (void **)(&native_fn));

    return native_fn;
}

#endif