#ifndef NATIVE_RANDOM_H
#define NATIVE_RANDOM_H

#include "vm/obj.h"
#include "vm/vm.h"
#include "vm/vmu.h"
#include "vm/vm_factory.h"
#include "vm/types_utils.h"

#include "native/xoshiro256.h"
#include "native/native_random.h"

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

NativeModule *random_native_module = NULL;

Value native_fn_random_create(uint8_t argsc, Value *values, Value target, void *context){
	RandomNative *random_native = random_native_create(VMU_NATIVE_FRONT_ALLOCATOR);
	NativeObj *native_obj = vmu_create_native(random_native, VMU_VM);

    random_native->xos256 = xoshiro256_init();

    return OBJ_VALUE(native_obj);
}

Value native_fn_random_create_seed(uint8_t argsc, Value *values, Value target, void *context){
    int64_t seed = validate_value_int_arg(values[0], 1, "seed", VMU_VM);
    RandomNative *random_native = random_native_create(VMU_NATIVE_FRONT_ALLOCATOR);
	NativeObj *native_obj = vmu_create_native(random_native, VMU_VM);

    random_native->xos256 = xoshiro256_init_seed((uint64_t)seed);

    return OBJ_VALUE(native_obj);
}

Value native_fn_random_next(uint8_t argsc, Value *values, Value target, void *context){
    RandomNative *random_native = random_native_validate_value_arg(values[0], 1, "generator", VMU_VM);
    return INT_VALUE(xoshiro256_next(&random_native->xos256));
}

void random_module_init(const Allocator *allocator){
    random_native_module = vm_factory_native_module_create(allocator, "random");

    vm_factory_native_module_add_native_fn(random_native_module, "create", 0, native_fn_random_create);
    vm_factory_native_module_add_native_fn(random_native_module, "create_seed", 1, native_fn_random_create_seed);
    vm_factory_native_module_add_native_fn(random_native_module, "next", 1, native_fn_random_next);
}

#endif
