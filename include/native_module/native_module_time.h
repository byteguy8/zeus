#ifndef NATIVE_MODULE_TIME_H
#define NATIVE_MODULE_TIME_H

#include "utils.h"

#include "vm/vm_utils.h"
#include "vm/vm_factory.h"

#include <time.h>

NativeModule *time_native_module = NULL;

Value native_fn_time_sleep_millis(VM *vm, uint8_t argsc, Value *values, Value target){
    int64_t sleep_value = vm_utils_value_validate_int_range_arg(vm, values[0], 1, "millis", 1, UINT32_MAX);

    utils_sleep(sleep_value);

    return EMPTY_VALUE;
}

Value native_fn_time_millis(VM *vm, uint8_t argsc, Value *values, Value target){
    return INT_VALUE(utils_millis());
}

void time_module_init(const Allocator *allocator){
    time_native_module = vm_factory_native_module_create(allocator, "time");

    vm_factory_native_module_add_native_fn(time_native_module, "sleep_millis", 1, native_fn_time_sleep_millis);
    vm_factory_native_module_add_native_fn(time_native_module, "millis", 0, native_fn_time_millis);
}

#endif
